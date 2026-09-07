#!/usr/bin/env python3
"""Capture matched arena comparisons. Raw evidence stays under build/.

    python3 media/gallery.py --before build/SESSION/game-before
Only the final comparison collage is written to screenshots/; it is never input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
VIEWS = (
    ("forest", 7, "forest day clear", "14 1.62 17 -0.73 0.07", ""),
    ("sunset", 59, "dunes sunset haze", "14 1.62 17 -0.73 0.07", "sun 10 318"),
    ("square", 7, "forest day clear", "35 32 38 -0.74 -0.58", ""),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(binary, directory, key, seed, commands, width=1280, height=720):
    target = directory / (key + ".png")
    script = ";\n".join(commands + ["shot " + str(target), "budget"]) + "\n"
    recipe = directory / (key + ".script")
    recipe.write_text(script)
    with (directory / (key + ".log")).open("w") as log:
        subprocess.run([str(binary), "--seed", str(seed), "--w", str(width), "--h", str(height),
                        "--config", str(directory / (key + ".cfg")), "--script", str(recipe)],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    header = target.read_bytes()[:24]
    if header[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", header[16:24]) != (width, height):
        raise ValueError("incomplete capture: " + key)
    text = (directory / (key + ".log")).read_text()
    budget = next(line for line in text.splitlines() if line.startswith("budget "))
    for field in ("drops", "world_drops", "ui_drops", "ev_drops", "decor_invalid"):
        if not re.search(r"\b" + field + r"=0\b", budget):
            raise ValueError("capture exceeded " + field + ": " + key)
    return dict(key=key, seed=seed, image=str(target), sha256=digest(target), commands=commands)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/game")
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="media-gallery-", dir=ROOT / "build"))
    versions = [("after", args.binary.resolve())]
    if args.before:
        versions.insert(0, ("before", args.before.resolve()))
    identities = {label: digest(binary) for label, binary in versions}
    entries = []
    for name, seed, environment, camera, sun in VIEWS:
        for label, binary in versions:
            commands = ["environment " + environment, "botfreeze on", "hud off", "showfps off"]
            if sun:
                commands.append(sun)
            commands += ["cam " + camera, "wait 180"]
            entries.append(capture(binary, directory, name + "-" + label, seed, commands))
    if any(digest(binary) != identities[label] for label, binary in versions):
        raise ValueError("binary changed during comparison")
    (directory / "manifest.json").write_text(json.dumps(
        dict(binaries=identities, entries=entries), indent=2) + "\n")
    output = ROOT / "screenshots/arenas.png"
    output.parent.mkdir(exist_ok=True)
    # Explicit files only: never discover previous exports or gallery images.
    command = ["ffmpeg", "-y", "-v", "error", "-filter_complex_threads", "1"]
    for entry in entries:
        command += ["-i", entry["image"]]
    filters = []
    for index, entry in enumerate(entries):
        label = entry["key"].replace("-", " / ").upper()
        filters.append(f"[{index}:v]scale=640:360,pad=640:390:0:30:color=0x111a22,"
                       f"drawtext=text='{label}':x=14:y=8:fontsize=16:fontcolor=white[v{index}]")
    count = len(versions)
    layout = "|".join(f"{(i % count) * 640}_{(i // count) * 390}" for i in range(len(entries)))
    filters.append("".join(f"[v{i}]" for i in range(len(entries))) +
                   f"xstack=inputs={len(entries)}:layout={layout}[out]")
    staged = directory / "arenas.png"
    command += ["-filter_complex", ";".join(filters), "-map", "[out]", "-frames:v", "1", str(staged)]
    subprocess.run(command, check=True)
    output.write_bytes(staged.read_bytes())
    print("Comparison:", output, "\nEvidence:", directory)


if __name__ == "__main__":
    main()
