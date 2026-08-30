#!/usr/bin/env python3
"""Create the deterministic player-model screenshot set."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


DEBUG_ROOT = Path(__file__).resolve().parent
REPO_ROOT = DEBUG_ROOT.parents[1]
DEFAULT_GAME = REPO_ROOT / "build" / "game"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class Capture:
    group: str
    path: str
    description: str
    commands: tuple[str, ...]


PUPPET_BASE = (
    "bots 1",
    "wait 1",
    "puppet on",
    "puppet warp 8 8",
    "puppet face 0",
    "puppet ready 1",
    "weapon ar",
    "hud off",
    "showfps off",
    "wait 90",
)

PLAYER_BASE = (
    "bots 1",
    "wait 1",
    "botfreeze on",
    "hud off",
    "showfps off",
)


def exterior(
    group: str,
    path: str,
    description: str,
    pose: tuple[str, ...],
    look_height: float,
    camera: tuple[float, float, float],
) -> Capture:
    x, y, z = camera
    return Capture(
        group,
        path,
        description,
        PUPPET_BASE
        + pose
        + (
            f"camaim {look_height}",
            f"cambot {x} {y} {z}",
        ),
    )


CAPTURES = (
    exterior(
        "standing",
        "stand_ext_front3q.png",
        "Standing AR, front-left three-quarter view",
        (),
        0.90,
        (-1.55, 1.15, -2.15),
    ),
    exterior(
        "standing",
        "stand_ext_left.png",
        "Standing AR, left profile",
        (),
        0.90,
        (-2.65, 1.12, 0.00),
    ),
    exterior(
        "standing",
        "stand_ext_rear3q.png",
        "Standing AR, rear-left three-quarter view",
        (),
        0.90,
        (-1.55, 1.15, 2.15),
    ),
    exterior(
        "run",
        "run_ext_side.png",
        "Running AR, left profile",
        ("puppet move 0 -1", "puppet speed 5.5", "wait 45"),
        0.90,
        (-2.65, 1.12, 0.00),
    ),
    exterior(
        "run",
        "run_start_ext_side.png",
        "AR six ticks into the stand-to-run transition, left profile",
        ("puppet move 0 -1", "puppet speed 5.5", "wait 6"),
        0.90,
        (-2.65, 1.12, 0.00),
    ),
    exterior(
        "run",
        "run_sr_start_ext_side.png",
        "Sniper six ticks into the low-ready-to-run transition, left profile",
        (
            "weapon sr",
            "puppet ready 0",
            "wait 90",
            "puppet move 0 -1",
            "puppet speed 5.5",
            "wait 6",
        ),
        0.90,
        (-2.65, 1.12, 0.00),
    ),
    exterior(
        "run",
        "run_sr_ext_side.png",
        "Running sniper, left profile",
        (
            "weapon sr",
            "puppet ready 0",
            "wait 90",
            "puppet move 0 -1",
            "puppet speed 5.5",
            "wait 45",
        ),
        0.90,
        (-2.65, 1.12, 0.00),
    ),
    exterior(
        "run",
        "run_stop_ext_front3q.png",
        "AR six ticks into a lateral run-to-stop transition",
        (
            "puppet move 1 0",
            "puppet speed 5.5",
            "wait 45",
            "puppet move 0 0",
            "puppet speed 0",
            "wait 6",
        ),
        0.90,
        (-1.55, 1.15, -2.15),
    ),
    exterior(
        "crouch",
        "crouch_idle_ext_side.png",
        "Stationary one-knee crouch, left profile",
        ("puppet crouch 1", "wait 90"),
        0.62,
        (-2.65, 0.86, 0.00),
    ),
    exterior(
        "crouch",
        "crouch_move_ext_side.png",
        "Moving two-foot crouch, left profile",
        (
            "puppet crouch 1",
            "puppet move 0 -1",
            "puppet speed 2.3",
            "wait 15",
        ),
        0.70,
        (-2.65, 0.90, 0.00),
    ),
    exterior(
        "aim",
        "aim_down_ext_side.png",
        "Steep downward AR aim, left profile",
        ("puppet pitch -72", "wait 60"),
        0.90,
        (-2.65, 1.12, 0.00),
    ),
    exterior(
        "aim",
        "aim_down_ext_front3q.png",
        "Steep downward AR aim, front-left three-quarter view",
        ("puppet pitch -72", "wait 60"),
        0.90,
        (-1.55, 1.15, -2.15),
    ),
    Capture(
        "body",
        "stand_fp_body.png",
        "First-person steep-down view of the standing body",
        PLAYER_BASE + ("weapon ar", "wait 60", "look 0 5000"),
    ),
    Capture(
        "body",
        "crouch_fp_body.png",
        "First-person steep-down view of the crouched body",
        PLAYER_BASE
        + ("weapon ar", "+crouch", "wait 60", "look 0 5000"),
    ),
    Capture(
        "ads",
        "ads_fp_ar.png",
        "First-person AR ADS sight picture",
        PLAYER_BASE + ("weapon ar", "+ads", "wait 90"),
    ),
    Capture(
        "ads",
        "ads_fp_sniper.png",
        "First-person sniper ADS sight picture",
        PLAYER_BASE + ("weapon sr", "+ads", "wait 90"),
    ),
    exterior(
        "ads",
        "ads_stand_ext_front3q.png",
        "Standing AR ADS, front-left three-quarter view",
        ("puppet ads 1", "wait 90"),
        0.90,
        (-1.55, 1.15, -2.15),
    ),
    exterior(
        "ads",
        "ads_stand_ext_side.png",
        "Standing AR ADS, right profile",
        ("puppet ads 1", "wait 90"),
        0.90,
        (2.65, 1.12, 0.00),
    ),
    exterior(
        "ads",
        "ads_crouch_ext_front3q.png",
        "Crouched AR ADS, front-right three-quarter view",
        ("puppet ads 1", "puppet crouch 1", "wait 90"),
        0.62,
        (1.55, 0.86, -2.15),
    ),
    exterior(
        "ads",
        "ads_crouch_ext_side.png",
        "Crouched AR ADS, left profile",
        ("puppet ads 1", "puppet crouch 1", "wait 90"),
        0.62,
        (-2.65, 0.86, 0.00),
    ),
    exterior(
        "lean",
        "lean_hip_left.png",
        "Left lean from the front, hip fire",
        ("puppet lean -1", "wait 90"),
        0.86,
        (0.00, 1.12, -2.65),
    ),
    exterior(
        "lean",
        "lean_hip_right.png",
        "Right lean from the front, hip fire",
        ("puppet lean 1", "wait 90"),
        0.86,
        (0.00, 1.12, -2.65),
    ),
    exterior(
        "lean",
        "lean_ads_left.png",
        "Left lean from the front, AR ADS",
        ("puppet ads 1", "puppet lean -1", "wait 90"),
        0.86,
        (0.00, 1.12, -2.65),
    ),
    exterior(
        "lean",
        "lean_ads_right.png",
        "Right lean from the front, AR ADS",
        ("puppet ads 1", "puppet lean 1", "wait 90"),
        0.86,
        (0.00, 1.12, -2.65),
    ),
    exterior(
        "slide",
        "slide_ext_front3q.png",
        "AR slide, front-left three-quarter view",
        ("puppet move 0 -1", "puppet speed 7", "puppet slide", "wait 8"),
        0.55,
        (-1.55, 0.86, -2.15),
    ),
    exterior(
        "slide",
        "slide_ext_side.png",
        "AR slide, left profile",
        ("puppet move 0 -1", "puppet speed 7", "puppet slide", "wait 8"),
        0.55,
        (-2.65, 0.82, 0.00),
    ),
    exterior(
        "slide",
        "slide_sideways_ext_front3q.png",
        "Rightward lateral AR slide, front-left three-quarter view",
        ("puppet move 1 0", "puppet speed 7", "puppet slide", "wait 8"),
        0.55,
        (-1.55, 0.86, -2.15),
    ),
    exterior(
        "slide",
        "slide_left_ext_front3q.png",
        "Leftward lateral AR slide, front-left three-quarter view",
        ("puppet move -1 0", "puppet speed 7", "puppet slide", "wait 8"),
        0.55,
        (-1.55, 0.86, -2.15),
    ),
    exterior(
        "slide",
        "slide_right_ext_side.png",
        "Rightward lateral AR slide, anatomical left profile",
        ("puppet move 1 0", "puppet speed 7", "puppet slide", "wait 8"),
        0.55,
        (-2.65, 0.82, 0.00),
    ),
    exterior(
        "slide",
        "slide_left_ext_side.png",
        "Leftward lateral AR slide, anatomical left profile",
        ("puppet move -1 0", "puppet speed 7", "puppet slide", "wait 8"),
        0.55,
        (-2.65, 0.82, 0.00),
    ),
    Capture(
        "slide",
        "slide_fp.png",
        "First-person AR slide viewmodel",
        PLAYER_BASE
        + (
            "weapon ar",
            "look 4000 0",
            "+forward",
            "wait 20",
            "+crouch",
            "wait 8",
        ),
    ),
    exterior(
        "detail",
        "detail_hip_hands.png",
        "Close-up of the AR and both hand positions at hip",
        (),
        1.22,
        (-0.78, 1.30, -1.08),
    ),
    exterior(
        "detail",
        "detail_ads_hands.png",
        "Close-up of the AR and both hand positions in ADS",
        ("puppet ads 1", "wait 90"),
        1.22,
        (-0.78, 1.30, -1.08),
    ),
    exterior(
        "detail",
        "detail_ads_stock_side.png",
        "Lit right-side close-up of the ADS stock-to-shoulder contact",
        ("puppet ads 1", "wait 90", "sun 40 90"),
        1.22,
        (1.35, 1.28, 0.00),
    ),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) != 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        raise RuntimeError(f"capture did not produce a valid PNG: {path}")
    return struct.unpack(">II", header[16:24])


def select_captures(groups: list[str] | None) -> list[Capture]:
    if not groups:
        return list(CAPTURES)
    wanted = set(groups)
    return [capture for capture in CAPTURES if capture.group in wanted]


def capture_one(
    capture: Capture,
    game: Path,
    seed: int,
    width: int,
    height: int,
) -> dict[str, object]:
    destination = DEBUG_ROOT / capture.path
    destination.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="skill-issue-debug-") as temp_name:
        if any(char.isspace() for char in temp_name):
            raise RuntimeError("temporary path contains whitespace; harness paths cannot")
        temp_root = Path(temp_name)
        temp_png = temp_root / "capture.png"
        config = temp_root / "fresh.cfg"
        command_text = "; ".join(capture.commands + (f"shot {temp_png}",))
        command = [
            str(game),
            "--seed",
            str(seed),
            "--config",
            str(config),
            "--w",
            str(width),
            "--h",
            str(height),
            "--do",
            command_text,
        ]
        environment = os.environ.copy()
        environment.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
        environment.setdefault("LP_NUM_THREADS", "1")
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            raise RuntimeError(
                f"game exited with {result.returncode} for {capture.path}"
                + (f":\n{detail}" if detail else "")
            )
        actual_size = png_size(temp_png)
        if actual_size != (width, height):
            raise RuntimeError(
                f"wrong PNG size for {capture.path}: {actual_size}, "
                f"expected {(width, height)}"
            )
        staged = destination.with_name(
            f".{destination.stem}.capture-{os.getpid()}.png"
        )
        try:
            shutil.copyfile(temp_png, staged)
            os.replace(staged, destination)
        finally:
            staged.unlink(missing_ok=True)

    logical_command = "; ".join(
        capture.commands + (f"shot screenshots/debug/{capture.path}",)
    )
    return {
        "description": capture.description,
        "group": capture.group,
        "seed": seed,
        "size": [width, height],
        "sha256": sha256(destination),
        "binary_sha256": sha256(game),
        "script": logical_command,
    }


def update_manifest(records: dict[str, dict[str, object]], game: Path) -> None:
    path = DEBUG_ROOT / "manifest.json"
    old_records: dict[str, dict[str, object]] = {}
    valid_paths = {capture.path for capture in CAPTURES}
    if path.exists():
        try:
            old = json.loads(path.read_text(encoding="utf-8"))
            old_records = {
                key: value
                for key, value in old.get("captures", {}).items()
                if key in valid_paths
            }
        except (json.JSONDecodeError, OSError, AttributeError):
            old_records = {}
    old_records.update(records)
    manifest = {
        "format": 1,
        "binary": os.path.relpath(game, REPO_ROOT),
        "captures": dict(sorted(old_records.items())),
    }
    temp = path.with_suffix(".json.tmp")
    temp.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    os.replace(temp, path)


def parse_args() -> argparse.Namespace:
    groups = sorted({capture.group for capture in CAPTURES})
    parser = argparse.ArgumentParser(
        description="Capture the deterministic player-model debug screenshot set."
    )
    parser.add_argument(
        "--game",
        type=Path,
        default=DEFAULT_GAME,
        help="game binary (default: build/game)",
    )
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument(
        "--group",
        action="append",
        choices=groups,
        help="capture only this group; repeat to select more than one",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="list captures without running the game",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    captures = select_captures(args.group)
    if args.list:
        for capture in captures:
            print(f"{capture.group:8} {capture.path} - {capture.description}")
        return 0

    game = args.game.expanduser().resolve()
    if not game.is_file() or not os.access(game, os.X_OK):
        print(f"capture: game binary is not executable: {game}", file=sys.stderr)
        return 2
    if args.width < 320 or args.height < 240:
        print("capture: width/height must be at least 320x240", file=sys.stderr)
        return 2

    records: dict[str, dict[str, object]] = {}
    for index, capture in enumerate(captures, 1):
        print(f"[{index:02}/{len(captures):02}] {capture.path}", flush=True)
        try:
            records[capture.path] = capture_one(
                capture, game, args.seed, args.width, args.height
            )
        except (OSError, RuntimeError) as error:
            print(f"capture: {error}", file=sys.stderr)
            return 1

    update_manifest(records, game)
    print(f"captured {len(captures)} PNGs under {DEBUG_ROOT}")
    print(f"manifest: {DEBUG_ROOT / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
