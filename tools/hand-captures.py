#!/usr/bin/env python3
"""Capture real, reproducible hand-camera frames and before/after comparisons.

    python3 tools/hand-captures.py --suite --weapon both --before build/SESSION/game-before
    python3 tools/hand-captures.py --tag iteration-02

Raw PNGs, pinned configs, recipes, logs and hashes live in a unique build/
directory. Only explicit final files are exported to screenshots/; earlier
exports are never read, relabelled or stacked into subsequent screenshots.
Capture profiles force first person; supplied profiles remain unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SETUP = "botfreeze on; hud off; showfps off; wait 60"
DEFAULT_CLOSEUP = "vmorbit -65 10 0.48 1"

# Weapon-space camera stations are independent of hand geometry and wrist edits.
# Each pose starts in a fresh process; shots themselves advance no simulation.
SUITE_VIEWS = ("fingers", "trigger-contact", "trigger-reverse", "support-fingers",
               "support", "support-reverse", "trigger", "first-person")
VIEW_LABELS = {"support": "STUETZHAND / DAUMENBALLEN",
               "support-reverse": "STUETZHAND / GEGENANSICHT",
               "trigger": "ABZUGSHAND / DAUMENANSATZ",
               "first-person": "SPIELANSICHT",
               "fingers": "FINGER / HANDANSATZ",
               "trigger-contact": "ZEIGEFINGER / ABZUG",
               "trigger-reverse": "ABZUG / GEGENANSICHT",
               "support-fingers": "STUETZHAND / FINGER"}
POSE_COMMANDS = {"hip": "", "ads": "+ads; wait 60;",
                 "reload": "tap fire; wait 30; tap reload; wait 45;"}


def suite_camera(weapon, view):
    if view == "first-person":
        return "vmorbit off"
    if view in ("trigger-contact", "trigger-reverse"):
        bore, rise = (0.04, -0.0333) if weapon == "ar" else (0.018, -0.0175)
        yaw = 70 if view == "trigger-contact" else -80
        pitch = 20 if view == "trigger-contact" else 35
        return f"vmbore {yaw} {pitch} 0.26 {bore} {rise} 30"
    if view == "fingers":
        bore = 0.0 if weapon == "ar" else -0.037
        return f"vmbore -80 -15 0.30 {bore} -0.040 30"
    if view == "trigger":
        bore = 0.0 if weapon == "ar" else -0.037
        return f"vmbore 80 -15 0.34 {bore} -0.040 30"
    bore = 0.263 if weapon == "ar" else 0.208
    yaw = -80 if view == "support-fingers" else 65 if view == "support" else 140
    return f"vmbore {yaw} 10 0.36 {bore} -0.025 30"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def first_person_profile(data):
    """Pin the capture camera without changing the supplied profile on disk."""
    profile, count = re.subn(
        rb"(?m)^[ \t]*third_person(?:[ \t]+[^\r\n]*)?\r?$", b"third_person 0", data)
    if not count:
        profile = profile.rstrip(b"\r\n") + b"\nthird_person 0\n"
    return profile


def validate_png(path, width, height):
    """Reject incomplete, concatenated, animated and unexpectedly tiled exports."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not a PNG: {path}")
    offset, headers, endings = 8, 0, 0
    compressed = bytearray()
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError(f"truncated PNG chunk: {path}")
        size, kind = struct.unpack_from(">I4s", data, offset)
        end = offset + 12 + size
        if end > len(data):
            raise ValueError(f"truncated PNG payload: {path}")
        payload = data[offset + 8:end - 4]
        crc = struct.unpack_from(">I", data, end - 4)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError(f"PNG checksum mismatch: {path}")
        if kind == b"IHDR":
            headers += 1
            if offset != 8 or payload != struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0):
                raise ValueError(f"expected one {width}x{height} RGB frame: {path}")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind in (b"acTL", b"fcTL", b"fdAT"):
            raise ValueError(f"animated screenshot: {path}")
        elif kind == b"IEND":
            endings += 1
            if size or end != len(data):
                raise ValueError(f"extra data after the screenshot: {path}")
        offset = end
    if headers != 1 or endings != 1 or not compressed:
        raise ValueError(f"incomplete screenshot: {path}")
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed)
    raw += decoder.flush()
    if not decoder.eof or decoder.unused_data or len(raw) != height * (width * 3 + 1):
        raise ValueError(f"incorrect screenshot scanline count: {path}")
    if any(raw[y * (width * 3 + 1)] > 4 for y in range(height)):
        raise ValueError(f"invalid screenshot scanline filter: {path}")


def capture(binary, directory, tag, args, config):
    full = directory / f"000-{tag}.png"
    close = directory / f"001-{tag}-closeup.png"
    # Harness paths have no quoting syntax; relative names also keep the recipe
    # runnable when a checkout's absolute directory contains whitespace.
    recipe = directory / f"{tag}.script"
    recipe.write_text(f"weapon {args.weapon}; {args.setup};\n"
                      f"shot {full.name}; budget;\n"
                      f"{args.closeup}; shot {close.name}; budget;\n")
    profile = directory / f"{tag}.cfg"
    profile.write_bytes(config)
    log_path = directory / f"{tag}.log"
    command = [str(binary), "--seed", str(args.seed), "--w", str(args.width),
               "--h", str(args.height), "--config", profile.name,
               "--script", recipe.name]
    with log_path.open("w") as log:
        subprocess.run(command, cwd=directory, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    lines = log_path.read_text().splitlines()
    if [line for line in lines if line.startswith("shot ")] != [
            f"shot {full.name}", f"shot {close.name}"]:
        raise ValueError(f"expected exactly two successful individual shots: {log_path}")
    budgets = [line for line in lines if line.startswith("budget ")]
    if len(budgets) != 2:
        raise ValueError(f"missing render budgets: {log_path}")
    for budget in budgets:
        for field in ("drops", "world_drops", "ui_drops", "ev_drops", "decor_invalid"):
            if not re.search(r"\b" + field + r"=0\b", budget):
                raise ValueError(f"capture exceeded {field}: {log_path}")
    for path in (full, close):
        validate_png(path, args.width, args.height)
    return [dict(tag=tag, view=view, raw=str(path), raw_sha256=digest(path))
            for view, path in (("first person", full), ("hand closeup", close))]


def stage_export(entry, directory, args):
    raw = Path(entry["raw"])
    staged = directory / "exports" / raw.name
    width, height = args.width, args.height
    if args.captions:
        # One input, one drawtext and one output frame. Captions always start
        # from the untouched raw capture, including on repeated invocations.
        label = f"{entry['tag'].upper()} / {entry['view'].upper()}"
        caption = directory / (raw.stem + ".caption.txt")
        caption.write_text(label + "\n")
        height += 36
        filters = (f"pad={width}:{height}:0:36:color=0x111a22,"
                   f"drawtext=textfile={caption.name}:expansion=none:"
                   "x=12:y=9:fontsize=18:fontcolor=white,format=rgb24")
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-filter_threads", "1",
                        "-i", raw.name, "-vf", filters, "-frames:v", "1",
                        "-threads", "1", str(staged)], cwd=directory, check=True)
        entry["caption"] = label
    else:
        shutil.copyfile(raw, staged)
    validate_png(staged, width, height)
    entry.update(export=str(args.output_dir / staged.name),
                 export_sha256=digest(staged), width=width, height=height)
    return staged


def suite_capture(binary, directory, tag, weapon, pose, repeat, config, args):
    take = directory / f"{tag}-{weapon}-{pose}-{repeat}"
    take.mkdir()
    profile = take / "capture.cfg"
    profile.write_bytes(config)
    recipe = take / "capture.script"
    setup = f"weapon {weapon}; bots 0; {args.setup}; {POSE_COMMANDS[pose]}\n"
    recipe.write_text(setup + "".join(
        f"{suite_camera(weapon, view)}; shot {view}.png; budget;\n"
        for view in args.views))
    log_path = take / "capture.log"
    with log_path.open("w") as log:
        subprocess.run([str(binary), "--seed", str(args.seed), "--w", str(args.width),
                        "--h", str(args.height), "--config", profile.name,
                        "--script", recipe.name], cwd=take, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    lines = log_path.read_text().splitlines()
    shots = [line for line in lines if line.startswith("shot ")]
    if shots != [f"shot {view}.png" for view in args.views]:
        raise ValueError(f"incomplete capture matrix: {log_path}")
    budgets = [line for line in lines if line.startswith("budget ")]
    if len(budgets) != len(args.views):
        raise ValueError(f"incomplete budget matrix: {log_path}")
    entries = []
    for view, budget in zip(args.views, budgets):
        for field in ("drops", "world_drops", "ui_drops", "ev_drops", "decor_invalid"):
            if not re.search(r"\b" + field + r"=0\b", budget):
                raise ValueError(f"capture exceeded {field}: {log_path}")
        raw = take / f"{view}.png"
        validate_png(raw, args.width, args.height)
        entries.append(dict(tag=tag, weapon=weapon, pose=pose, view=view, repeat=repeat,
                            raw=str(raw), raw_sha256=digest(raw), budget=budget,
                            camera=suite_camera(weapon, view), recipe=str(recipe),
                            recipe_sha256=digest(recipe),
                            input_config_sha256=hashlib.sha256(config).hexdigest(),
                            saved_config_sha256=digest(profile)))
    return entries


def suite_export(entries, versions, weapons, pose, view, directory, args):
    """Only pad/label/arrange complete raw frames; never retouch game pixels."""
    inputs, filters, labels = [], [], []
    for weapon in weapons:
        for tag, _ in versions:
            entry = next(e for e in entries if e["tag"] == tag and e["weapon"] == weapon
                         and e["pose"] == pose and e["view"] == view and e["repeat"] == 0)
            index = len(labels)
            inputs += ["-i", entry["raw"]]
            caption = directory / f"caption-{pose}-{view}-{index}.txt"
            label = "VORHER" if tag == "before" else "NACHHER" if tag == "after" else tag.upper()
            caption.write_text(f"{label}  |  {weapon.upper()} {pose.upper()}  |  {VIEW_LABELS[view]}\n")
            filters.append(f"[{index}:v]pad={args.width}:{args.height + 36}:0:36:"
                           f"color=0x111a22,drawtext=textfile={caption.name}:expansion=none:"
                           f"x=12:y=9:fontsize=18:fontcolor=white,format=rgb24[v{index}]")
            labels.append(f"[v{index}]")
    layout = "|".join(f"{(i % len(versions)) * args.width}_{(i // len(versions)) * (args.height + 36)}"
                      for i in range(len(labels)))
    if len(labels) == 1:
        filters.append(f"{labels[0]}null[out]")
    else:
        filters.append("".join(labels) + f"xstack=inputs={len(labels)}:layout={layout}[out]")
    name = f"hands-{pose}-{view}.png"
    staged = directory / "exports" / name
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-filter_complex_threads", "1", *inputs,
                    "-filter_complex", ";".join(filters), "-map", "[out]", "-frames:v", "1",
                    "-threads", "1", str(staged)], cwd=directory, check=True)
    width, height = args.width * len(versions), (args.height + 36) * len(weapons)
    validate_png(staged, width, height)
    return dict(path=str(staged), name=name, sha256=digest(staged), width=width, height=height)


def suite_main(args):
    versions = [(args.tag, args.binary.resolve())]
    if args.before:
        versions.insert(0, ("before", args.before.resolve()))
    weapons = ("ar", "sr") if args.weapon == "both" else (args.weapon,)
    directory = Path(tempfile.mkdtemp(prefix="hand-suite-", dir=ROOT / "build"))
    (directory / "exports").mkdir()
    print("Evidence:", directory, flush=True)
    identities = {tag: dict(path=str(binary), sha256=digest(binary)) for tag, binary in versions}
    # Freeze executables too: a concurrent rebuild must never mix two versions.
    frozen = []
    for tag, binary in versions:
        target = directory / f"game-{tag}"
        shutil.copy2(binary, target)
        if digest(target) != identities[tag]["sha256"]:
            raise ValueError("binary changed while taking the snapshot")
        frozen.append((tag, target))
    pinned = directory / "source.cfg"
    if args.config_source:
        pinned.write_bytes(args.config_source.read_bytes())
    else:
        with (directory / "config.log").open("w") as log:
            subprocess.run([str(frozen[0][1]), "--config", str(pinned), "--print-config"],
                           cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
    config = first_person_profile(pinned.read_bytes())
    (directory / "input.cfg").write_bytes(config)
    entries = []
    for tag, binary in frozen:
        for weapon in weapons:
            for pose in args.poses:
                reference = None
                for repeat in range(args.repeat):
                    take = suite_capture(binary, directory, tag, weapon, pose, repeat, config, args)
                    hashes = [entry["raw_sha256"] for entry in take]
                    if reference is not None and hashes != reference:
                        raise ValueError(f"non-reproducible pixels: {tag}/{weapon}/{pose}; evidence {directory}")
                    reference = hashes
                    entries.extend(take)
                print(f"Verified: {tag}/{weapon}/{pose}, {len(args.views)} views x {args.repeat} identical takes", flush=True)
    exports = [suite_export(entries, versions, weapons, pose, view, directory, args)
               for pose in args.poses for view in args.views]
    manifest = dict(schema=1, seed=args.seed, width=args.width, height=args.height,
                    setup=args.setup, repeats=args.repeat, poses=args.poses, views=args.views,
                    binaries=identities, input_config_sha256=digest(directory / "input.cfg"),
                    entries=entries, exports=exports)
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for entry in exports:
        target = args.output_dir / entry["name"]
        # Keep earlier explicit exports as evidence rather than silently losing them.
        if target.exists():
            archive = directory / "previous-exports"
            archive.mkdir(exist_ok=True)
            shutil.copy2(target, archive / target.name)
        shutil.copyfile(entry["path"], target)
        print("Screenshot:", target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/game")
    parser.add_argument("--before", type=Path, help="preserved pre-change binary")
    parser.add_argument("--tag", default="after", help="after, before or an iteration name")
    parser.add_argument("--config-source", type=Path,
                        help="shared profile; capture copies force third_person 0")
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--weapon", choices=("ar", "sr", "both"), default="ar")
    parser.add_argument("--suite", action="store_true", help="fixed weapon-space views and paired comparisons")
    parser.add_argument("--poses", nargs="+", choices=tuple(POSE_COMMANDS), default=["hip"])
    parser.add_argument("--views", nargs="+", choices=SUITE_VIEWS, default=list(SUITE_VIEWS[:4]))
    parser.add_argument("--repeat", type=int, default=2, help="suite: identical fresh-process takes (2..10)")
    parser.add_argument("--setup", default=DEFAULT_SETUP, help="shared initial harness commands")
    parser.add_argument("--closeup", default=DEFAULT_CLOSEUP, help="vmorbit/vmbore camera command")
    parser.add_argument("--captions", action="store_true", help="add a single 36px caption band to each frame")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "screenshots")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]*", args.tag):
        parser.error("--tag must contain only letters, digits, hyphens or underscores")
    if args.before and args.tag == "before":
        parser.error("--before and --tag before would export the same names")
    if not 0 <= args.seed <= 4294967295 or not 64 <= args.width <= 8192 or not 64 <= args.height <= 8192:
        parser.error("seed must be uint32; width and height must be in 64..8192")
    if not re.fullmatch(r"\s*(?:vmorbit|vmbore)\s+[-+.\deE\s]+", args.closeup):
        parser.error("--closeup must be a numeric vmorbit or vmbore camera command")
    args.output_dir = args.output_dir.resolve()
    if not 2 <= args.repeat <= 10:
        parser.error("--repeat must be in 2..10 to verify pixel reproducibility")
    if len(set(args.views)) != len(args.views) or len(set(args.poses)) != len(args.poses):
        parser.error("--views and --poses must not contain duplicates")
    (ROOT / "build").mkdir(exist_ok=True)
    if args.suite:
        return suite_main(args)
    if args.weapon == "both":
        parser.error("--weapon both requires --suite")
    versions = [(args.tag, args.binary.resolve())]
    if args.before:
        versions.insert(0, ("before", args.before.resolve()))
    identities = {tag: digest(binary) for tag, binary in versions}
    (ROOT / "build").mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="hand-captures-", dir=ROOT / "build"))
    (directory / "exports").mkdir()
    print("Evidence:", directory, flush=True)
    pinned = directory / "source.cfg"
    if args.config_source:
        pinned.write_bytes(args.config_source.read_bytes())
    else:
        # The oldest binary owns defaults for this comparison. Both versions
        # consume copies, so changed build defaults cannot change the camera.
        with (directory / "config.log").open("w") as log:
            subprocess.run([str(versions[0][1]), "--config", str(pinned), "--print-config"],
                           cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
    source_config = pinned.read_bytes()
    (directory / "input.cfg").write_bytes(source_config)
    config = first_person_profile(source_config)
    pinned.write_bytes(config)
    entries = []
    for tag, binary in versions:
        entries.extend(capture(binary, directory, tag, args, config))
    if any(digest(binary) != identities[tag] for tag, binary in versions):
        raise ValueError("binary changed during capture; rerun with stable binaries")
    staged = [stage_export(entry, directory, args) for entry in entries]
    manifest = dict(seed=args.seed, width=args.width, height=args.height,
                    weapon=args.weapon, setup=args.setup, closeup=args.closeup,
                    input_config_sha256=digest(directory / "input.cfg"),
                    config_sha256=digest(pinned), binaries={tag: dict(path=str(binary), sha256=identities[tag])
                    for tag, binary in versions}, entries=entries)
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for path in staged:
        target = args.output_dir / path.name
        # Existing captures from other iterations are preserved; these explicit
        # names are the only outputs that this invocation replaces.
        shutil.copyfile(path, target)
        print("Screenshot:", target)


if __name__ == "__main__":
    main()
