#!/usr/bin/env python3
"""Render the biome gallery as untouched game PNGs, with reproducible recipes.

    python3 media/biomes.py --width 960 --height 540
    python3 media/biomes.py --width 1920 --height 1080
    python3 media/biomes.py --only aurora

Every shot gets a fresh default config. Logs and temporary configs stay in
media/.cache/biomes; screenshots, index.html and manifest.json share the output
directory. No image conversion, retouching or external Python packages are used.
"""

import argparse
import hashlib
import html
import json
import platform
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "media" / ".cache" / "biomes"
DEFAULT_OUTPUT = ROOT / "screenshots" / "biomes" / "final"

# Camera yaw/pitch are radians. Overrides are recorded separately so the
# manifest distinguishes natural map draws from controlled detail studies.
SHOTS = (
    dict(key="aurora", title="Nordlicht am Außenposten", seed=28,
         caption="Leise grüne Lichtbänder über Schnee und Tannen. Zwischen den Laternen bleiben dunkle Wege.",
         biome=None, sun=None, camera="16 2.8 -18 -2.55 0.10"),
    dict(key="night", title="Nachtschicht", seed=4,
         caption="Warmes Laternenlicht und kühle Fluter geben dem Gelände eine andere Tiefe.",
         biome="night", sun=None, camera="18 4 19 -0.74 -0.12"),
    dict(key="red-post", title="Rotes Wachlicht", seed=19,
         caption="Ein roter Lichtpunkt markiert den Wachposten; Mauern unterbrechen den Lichtkegel.",
         biome=None, sun=None, camera="7 2.1 16 -0.95 0.05"),
    dict(key="winter", title="Stiller Winter", seed=22,
         caption="Heller Schnee, unregelmäßige Nadelbäume und ferne Bergzüge verändern den Charakter der Arena.",
         biome=None, sun=None, camera="17 3.0 19 -0.73 0.04"),
    dict(key="forest", title="Wege im Wald", seed=4,
         caption="Gras und Büsche säumen die Deckung. Die Wege durch das Gelände bleiben offen.",
         biome=None, sun=None, camera="17 2 19 -0.73 0.07"),
    dict(key="undergrowth", title="Farn am feuchten Mauerfuß", seed=13,
         caption="Kleine Farnbüschel und Gräser besetzen geschützte Ecken. Feuchte Flächen wechseln mit rauem Boden.",
         biome="marsh", sun="32 145", camera="10.65 0.33 11.6 -0.517 -0.235"),
    dict(key="dunes", title="Dünen im Morgenlicht", seed=59,
         caption="Flaches Licht zeichnet Sand und Mauern. Vereinzeltes Glitzern und leichter Staub bleiben dezent.",
         biome=None, sun=None, camera="17 5 19 -0.73 -0.13"),
    dict(key="sand-detail", title="Sand im Gegenlicht", seed=59,
         caption="Kleine helle Körner blitzen im flachen Licht auf. Feine Sandrippen und Staub halten den Boden lebendig.",
         biome=None, sun=None, camera="-8 0.65 18 0.515 -0.16"),
    dict(key="quarry", title="Industrie am Horizont", seed=2,
         caption="Schlichte Zweckbauten und Vegetation geben dem Gelände einen eigenen Hintergrund.",
         biome=None, sun="32 145", camera="-17 3 19 0.73 0.03"),
    dict(key="masonry-z", title="Versetzter Ausbruch", seed=28,
         caption="Fehlende Ziegel bilden einen kantigen, versetzten Durchbruch. Die Ränder bleiben massiv.",
         biome="marsh", sun="38 145", camera="-9.9 1.5 -8.5 0 -0.07"),
    dict(key="masonry-l", title="Einzelne Ziegel fehlen", seed=47,
         caption="Ein kleiner L-förmiger Ausbruch ersetzt feine Risse durch eine klare Maueröffnung.",
         biome="marsh", sun="38 -70", camera="9.2 1.45 0.25 1.5708 -0.06"),
    dict(key="masonry-end", title="Stufig gebrochene Kante", seed=19,
         caption="Grobe Stufen und einzelne abgebrochene Ziegel lösen das Mauerende auf.",
         biome="marsh", sun="38 145", camera="10.5 1.6 -6.8 0 -0.08"),
)


def png_dimensions(path):
    with path.open("rb") as stream:
        header = stream.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"Game did not produce a PNG: {path}")
    return struct.unpack(">II", header[16:24])


def recipe(shot):
    commands = ["botfreeze on", "hud off", "showfps off"]
    if shot["biome"] is not None:
        commands.append("biome " + shot["biome"])
    if shot["sun"] is not None:
        commands.append("sun " + shot["sun"])
    commands.extend(["biome", "sun", "cam " + shot["camera"]])
    return commands


def render(shot, binary, output, width, height):
    destination = output / (shot["key"] + ".png")
    commands = recipe(shot)
    with tempfile.TemporaryDirectory(prefix=shot["key"] + "-", dir=CACHE) as temporary:
        temp = Path(temporary)
        # Harness tokens are whitespace-delimited. A temporary path under this
        # repository avoids ambiguity even when the final output path has spaces.
        capture = temp / "capture.png"
        capture_relative = capture.relative_to(ROOT).as_posix()
        script = "; ".join(commands + ["shot " + capture_relative])
        argv = [str(binary), "--seed", str(shot["seed"]), "--w", str(width),
                "--h", str(height), "--config", str(temp / "fresh.cfg"), "--do", script]
        result = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True)
        log = result.stdout + result.stderr
        (CACHE / (shot["key"] + ".log")).write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{shot['key']}: game exited {result.returncode}; see {CACHE / (shot['key'] + '.log')}")
        dimensions = png_dimensions(capture)
        if dimensions != (width, height):
            raise RuntimeError(f"{shot['key']}: got {dimensions}, expected {(width, height)}")
        # Moving preserves the game's exact PNG bytes, including when --output
        # points at another filesystem.
        shutil.move(str(capture), str(destination))
    print(f"{shot['key']}: {width}×{height} → {destination.relative_to(ROOT) if destination.is_relative_to(ROOT) else destination}", flush=True)
    return dict(key=shot["key"], file=destination.name, title=shot["title"],
                caption=shot["caption"], seed=shot["seed"], width=width, height=height,
                biome_override=shot["biome"], sun_override=shot["sun"],
                commands=commands + ["shot " + destination.as_posix()],
                config="Fresh game defaults; temporary config for each capture.",
                game_state=[line for line in log.splitlines()
                            if line.startswith(("biome ", "sun "))],
                sha256=hashlib.sha256(destination.read_bytes()).hexdigest())


def gallery(output, entries):
    cards = []
    for index, entry in enumerate(entries):
        esc = html.escape
        filename = esc(entry["file"], quote=True)
        title, caption = esc(entry["title"]), esc(entry["caption"])
        loading = "eager" if index == 0 else "lazy"
        cards.append(f'''<figure class="{'hero' if index == 0 else 'shot'}">
  <a class="picture" href="{filename}" aria-label="{title}: Original öffnen">
    <img src="{filename}" width="{entry['width']}" height="{entry['height']}"
         alt="{title}. {caption}" loading="{loading}" decoding="async">
  </a>
  <figcaption><h2>{title}</h2><p>{caption}</p></figcaption>
</figure>''')
    document = '''<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Biome &amp; Licht · Skill Issue</title>
<style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#10171b;color:#e9eeec}
*{box-sizing:border-box}body{margin:0}main{max-width:1500px;margin:auto;padding:56px 24px 36px}
header{max-width:760px;margin-bottom:36px}.eyebrow{font-size:.76rem;text-transform:uppercase;letter-spacing:.2em;color:#8ccbb7;margin:0 0 15px}
h1{font-size:clamp(2.3rem,5vw,4rem);font-weight:650;letter-spacing:-.04em;line-height:1.08;margin:0 0 18px}
header p{color:#acbbb9;line-height:1.65;font-size:1.05rem;margin:0}
.gallery{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:30px 24px}
figure{margin:0;min-width:0;background:#162126;border:1px solid #26363b;border-radius:12px;overflow:hidden}
.hero{grid-column:1/-1}.picture{display:block;background:#090e11;overflow:hidden}img{display:block;width:100%;height:auto}
.picture:focus-visible{outline:3px solid #8ccbb7;outline-offset:-3px}figcaption{padding:20px 22px 24px}
h2{font-size:1.14rem;font-weight:600;line-height:1.3;margin:0 0 8px}figcaption p{color:#adbfbd;font-size:.94rem;line-height:1.6;margin:0;max-width:850px}
footer{margin-top:32px;color:#829995;font-size:.85rem;line-height:1.6}a{color:inherit}footer a{text-underline-offset:3px}
@media(max-width:760px){main{padding:32px 14px 24px}.gallery{grid-template-columns:1fr;gap:20px}.hero{grid-column:auto}figcaption{padding:17px 18px 21px}}
</style></head><body><main>
<header><p class="eyebrow">Skill Issue · Galerie</p><h1>Biome &amp; Licht</h1>
<p>Nachtposten, winterliche Weite und bewachsene Wege. Unveränderte Spielaufnahmen zeigen die neuen Landschaften, Lichtstimmungen und Mauerformen.</p></header>
<section class="gallery" aria-label="Spielaufnahmen">''' + "\n".join(cards) + '''</section>
<footer>Ein Bild öffnen, um die Aufnahme in voller Auflösung anzusehen. <a href="manifest.json">Seeds und Aufnahmeeinstellungen</a></footer>
</main></body></html>
'''
    (output / "index.html").write_text(document, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--only", choices=[shot["key"] for shot in SHOTS])
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "game")
    args = parser.parse_args()
    if args.width < 320 or args.height < 180:
        parser.error("Capture size must be at least 320×180.")
    binary, output = args.binary.resolve(), args.output.resolve()
    if not binary.is_file():
        parser.error(f"Build the game first: {binary}")
    CACHE.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.json"
    existing = {}
    if args.only and manifest_path.exists():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        existing = {entry["key"]: entry for entry in previous.get("shots", [])
                    if (output / entry["file"]).is_file()}
    selected = [shot for shot in SHOTS if not args.only or shot["key"] == args.only]
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    for shot in selected:
        entry = render(shot, binary, output, args.width, args.height)
        if hashlib.sha256(binary.read_bytes()).hexdigest() != binary_hash:
            raise RuntimeError("The game binary changed during capture; rerun with one finished build.")
        entry["binary_sha256"] = binary_hash
        entry["architecture"] = platform.machine()
        existing[shot["key"]] = entry
    entries = [existing[shot["key"]] for shot in SHOTS if shot["key"] in existing]
    manifest = dict(format_version=1, generator="media/biomes.py",
                    capture="Untouched PNG output from the game; no postprocessing.",
                    binary=str(binary), shots=entries)
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    gallery(output, entries)
    print(f"Gallery: {output / 'index.html'}", flush=True)


if __name__ == "__main__":
    main()
