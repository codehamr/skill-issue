#!/usr/bin/env python3
"""Capture natural procedural weather as untouched game PNGs.

    python3 media/weather.py
    python3 media/weather.py --only 07-lightning

No biome, sun or weather override is used. The game validates every selected
seed through its map dump. Each image gets a fresh default configuration.
"""

import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "media/.cache/weather"
OUTPUT = ROOT / "screenshots/2026-09-07-procedural-weather"
WEATHER = ("Klar", "Nebel", "Regen", "Gewitter", "Sonnenregen")
SHOTS = (
    dict(key="01-clear-forest", title="Licht zwischen den Bäumen", seed=7, weather=0,
         camera="17 3.2 19 -0.73 0.02", tick=180,
         caption="Eine Waldmulde mit schrägen, bewachsenen Hängen, offenen Wegen und warmem Tageslicht."),
    dict(key="02-clear-night", title="Eine klare Winternacht", seed=5, weather=0,
         camera="16 2.8 -18 -2.55 0.10", tick=180,
         caption="Schnee, Nordlicht und die Beleuchtung der Außenposten entstehen aus demselben Seed."),
    dict(key="03-mist-forest", title="Nebel über dem Wald", seed=4, weather=1,
         camera="17 2.0 19 -0.73 0.07", tick=180,
         caption="Wurzeln und Farne wachsen auf den Böschungen. Hinter den freien Außenwegen verblassen Bäume und Berge im Nebel."),
    dict(key="04-mist-yard", title="Feuchter Hof", seed=6, weather=1,
         camera="-17 2.1 19 0.73 0.02", tick=180,
         caption="Mauerwerk und nasser Boden geben dem Nebel eine andere Umgebung."),
    dict(key="05-rain", title="Regen auf dem Gelände", seed=43, weather=2,
         camera="17 1.9 19 -0.73 0.03", tick=180,
         caption="Wind verschiebt den Regen; Pfützen und feuchte Flächen fangen das flache Licht ein."),
    dict(key="06-storm-before", title="Kurz vor dem Blitz", seed=38, weather=3,
         camera="-16 2.1 -18 2.17 0.13", tick=559,
         caption="Gewitter über einer prozeduralen Industriearena. Diese Kamera bleibt für die nächsten beiden Bilder unverändert."),
    dict(key="07-lightning", title="Die Entladung", seed=38, weather=3,
         camera="-16 2.1 -18 2.17 0.13", tick=562,
         caption="Drei Simulationsticks später: Der Blitz zeichnet sich am Himmel ab und hellt die Umgebung auf."),
    dict(key="08-storm-after", title="Nach dem Blitz", seed=38, weather=3,
         camera="-16 2.1 -18 2.17 0.13", tick=584,
         caption="Die Beleuchtung kehrt zurück. Der Donner folgt im Spiel zeitversetzt; die Aufnahme zeigt die gleiche unveränderte Szene."),
    dict(key="09-sunshower", title="Sonnenregen über dem Sand", seed=59, weather=4,
         camera="17 2.1 -18 -2.625 0.15", tick=180,
         caption="Ein seltener Sonnenregen. Der Regenbogen liegt der tatsächlichen Sonne dieser Arena gegenüber."),
    dict(key="10-match-settings", title="Match Settings", seed=7, weather=0,
         camera="17 3.2 19 -0.73 0.02", tick=0, menu=True,
         caption="Die Einstellungen enthalten Botanzahl, Schwierigkeit und Fraglimit. Wetter und Umgebung werden automatisch beim Erzeugen der Arena bestimmt."),
)

STYLE = """
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#0c151b;color:#e7f0f1}
*{box-sizing:border-box}body{margin:0}main{max-width:1480px;margin:auto;padding:48px 24px}
header{max-width:820px;margin-bottom:32px}h1{font-size:clamp(2.1rem,5vw,4rem);letter-spacing:-.045em;line-height:1.05;margin:12px 0 18px}
p{line-height:1.65;color:#afc1c8}.kicker{color:#a5d8c2;text-transform:uppercase;letter-spacing:.15em;font-size:.8rem}
a{color:inherit;text-underline-offset:4px}nav{display:flex;gap:20px;flex-wrap:wrap;margin:26px 0;color:#a5d8c2}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:24px}figure{margin:0;background:#14232a;border:1px solid #29404a;border-radius:14px;overflow:hidden}
.hero{grid-column:1/-1}img{display:block;width:100%;height:auto}figcaption{padding:20px 22px 24px}h2{margin:0 0 9px;font-size:1.18rem}
figcaption p{margin:0}.meta{font-size:.8rem;color:#8eaeb9;margin-top:12px}footer{margin-top:32px;font-size:.88rem;color:#91aab5}
.links a{display:block;text-decoration:none;border:1px solid #29404a;background:#14232a;border-radius:14px;padding:28px}.links h2{color:#e7f0f1}.links p{margin-bottom:0}
@media(max-width:760px){main{padding:28px 14px}.grid{grid-template-columns:1fr}.hero{grid-column:auto}}
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_record(binary):
    path = ROOT / "build/tuning/artifacts" / (binary.name + ".json")
    record = json.loads(path.read_text())
    if record.get("binary_sha256") != digest(binary):
        raise RuntimeError("The binary does not match its completed build record.")
    for name, expected in record["input"]["sources"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError(f"Build the current sources before capturing: {name}")
    return record


def recipe(shot):
    commands = ["botfreeze on", "hud off", "showfps off", "map", "sun",
                "cam " + shot["camera"]]
    if shot["tick"]:
        commands.append("wait " + str(shot["tick"]))
    if shot.get("menu"):
        commands += ["menu", "uiframe", "nav down", "uiframe", "nav ok",
                     "uiframe", "uiframe", "uiframe", "uistat"]
    return commands


def capture(shot, binary, output, width, height):
    commands = recipe(shot)
    key = shot["key"]
    with tempfile.TemporaryDirectory(prefix=key + "-", dir=CACHE) as temporary:
        temp = Path(temporary)
        image = temp / "capture.png"
        script = "; ".join(commands + ["shot " + image.relative_to(ROOT).as_posix(), "budget"])
        argv = [str(binary), "--seed", str(shot["seed"]), "--w", str(width),
                "--h", str(height), "--config", str(temp / "fresh.cfg"), "--do", script]
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
        log = result.stdout + result.stderr
        (output / "logs" / (key + ".log")).write_text(log)
        if result.returncode:
            raise RuntimeError(f"{key}: game exited {result.returncode}; inspect its log.")
        expected = f"map seed={shot['seed']} weather={shot['weather']}"
        if expected not in log.splitlines():
            raise RuntimeError(f"{key}: the game's actual weather did not match {expected!r}.")
        if shot.get("menu") and "page=match" not in log:
            raise RuntimeError("Match Settings was not reached through the UI.")
        budget = next((line for line in log.splitlines() if line.startswith("budget ")), "")
        counters = dict(field.split("=", 1) for field in budget.split()[1:] if "=" in field)
        if any(counters.get(name) != "0" for name in
               ("drops", "world_drops", "ui_drops", "ev_drops", "decor_invalid")):
            raise RuntimeError(f"{key}: geometry, UI or event capacity was exceeded.")
        header = image.read_bytes()[:24]
        if header[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", header[16:24]) != (width, height):
            raise RuntimeError(f"{key}: invalid PNG dimensions.")
        target = output / (key + ".png")
        shutil.move(image, target)
        # Retain a directly runnable script with the final output filename.
        replay = commands + ["shot " + target.relative_to(ROOT).as_posix(), "budget"]
        (output / "recipes" / (key + ".script")).write_text(";\n".join(replay) + "\n")
    print(f"{key}: seed={shot['seed']} weather={shot['weather']} {width}×{height}", flush=True)
    return dict(shot, file=target.name, width=width, height=height, sha256=digest(target),
                commands=commands, config="Fresh built-in game defaults; no environment overrides.",
                game_state=[line for line in log.splitlines() if line.startswith(
                    ("map seed=", "biome kind=", "map theme=", "sun elev=", "ui menu=", "budget "))])


def links(root):
    candidates = (("soldier/index.html", "Soldat & Animationen"),
                  ("trailer-review/index.html", "Trailer & Spielaufnahmen"))
    return [(name, title) for name, title in candidates if (root / name).is_file()]


def gallery(output, entries):
    esc = html.escape
    cards = []
    for i, entry in enumerate(entries):
        key = esc(entry["file"])
        cards.append(f'''<figure class="{'hero' if i == 0 else ''}"><a href="{key}"><img src="{key}"
width="{entry['width']}" height="{entry['height']}" alt="{esc(entry['title'])}" loading="{'eager' if i == 0 else 'lazy'}"></a>
<figcaption><h2>{esc(entry['title'])}</h2><p>{esc(entry['caption'])}</p>
<p class="meta">{WEATHER[entry['weather']]} · Seed {entry['seed']} · Tick {entry['tick']} ·
<a href="recipes/{entry['key']}.script">Aufnahmebefehle</a> · <a href="logs/{entry['key']}.log">Spielprotokoll</a></p></figcaption></figure>''')
    nav = '<a href="../index.html">Alle Bildnachweise</a>'
    nav += "".join(f'<a href="../{name}">{title}</a>' for name, title in links(output.parent))
    review = (' · <a href="review/index.html">Nachtlicht im Vergleich</a>'
              if (output / "review/index.html").is_file() else "")
    doc = f'''<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Prozedurale Welten & Wetter · Skill Issue</title><style>{STYLE}</style></head><body><main>
<header><p class="kicker">Skill Issue · Spielaufnahmen</p><h1>Eine neue Welt.<br>Ein anderes Wetter.</h1>
<p>Prozedurale Arenen zwischen Wald, Schnee, Sand und nassem Mauerwerk. Alle fünf Wetterarten entstehen automatisch aus gewöhnlichen Map-Seeds. Die Galerie zeigt unveränderte PNG-Aufnahmen aus dem Spiel.</p></header>
<nav>{nav}</nav><section class="grid">{''.join(cards)}</section>
<footer><p>Keine Biome-, Sonnen- oder Wetter-Overrides. Bilder öffnen sich in voller Auflösung.
<a href="manifest.json">Seeds, Build-Hashes und Aufnahmedaten</a>{review} · Reproduktion: <code>python3 media/weather.py</code>.</p></footer></main></body></html>'''
    (output / "index.html").write_text(doc)
    if output != OUTPUT:
        return
    tiles = [('<a href="2026-09-07-procedural-weather/index.html"><h2>Prozedurale Welten & Wetter</h2>'
              '<p>Wald, Winter, Regen, Nebel und ein Gewitter in drei aufeinanderfolgenden Aufnahmen.</p></a>')]
    descriptions = {"Soldat & Animationen": "Modellansichten, Hände an der Waffe und Bewegungssequenzen.",
                    "Trailer & Spielaufnahmen": "GIF, Video und Kontaktbogen des aktuellen Schnitts aus echten Spielaufnahmen."}
    for name, title in links(output.parent):
        tiles.append(f'<a href="{name}"><h2>{title}</h2><p>{descriptions[title]}</p></a>')
    (output.parent / "index.html").write_text(f'''<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bildnachweise · Skill Issue</title><style>{STYLE}</style></head><body><main><header><p class="kicker">Skill Issue · Galerie</p>
<h1>Das Spiel in Bildern.</h1><p>Aufgeräumte Bildfolgen zeigen prozedurale Welten, Wetter und die Arbeit am Soldatenmodell. Jede Galerie bewahrt ihre Aufnahmedaten.</p></header>
<section class="grid links">{''.join(tiles)}</section><footer>Originale Spielaufnahmen. Keine Retusche und keine künstlich ergänzten Bildinhalte.</footer></main></body></html>''')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--only", choices=[shot["key"] for shot in SHOTS])
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/game")
    args = parser.parse_args()
    if args.width < 320 or args.height < 180:
        parser.error("Capture dimensions must be at least 320×180.")
    binary, output = args.binary.resolve(), args.output.resolve()
    if not output.is_relative_to(ROOT):
        parser.error("Use an output directory within this workspace for runnable harness recipes.")
    build = build_record(binary)
    CACHE.mkdir(parents=True, exist_ok=True)
    for name in (output, output / "logs", output / "recipes"):
        name.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.json"
    entries = {}
    if args.only and manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
        if previous["binary_sha256"] != build["binary_sha256"]:
            raise RuntimeError("Existing captures use another build; regenerate the complete gallery.")
        entries = {entry["key"]: entry for entry in previous["shots"]}
    for shot in SHOTS:
        if args.only and args.only != shot["key"]:
            continue
        entries[shot["key"]] = capture(shot, binary, output, args.width, args.height)
        if build_record(binary)["binary_sha256"] != build["binary_sha256"]:
            raise RuntimeError("The build changed during capture; regenerate the complete gallery.")
    ordered = [entries[shot["key"]] for shot in SHOTS if shot["key"] in entries]
    manifest = dict(format_version=1, generator="media/weather.py", generator_sha256=digest(Path(__file__)),
                    binary_sha256=build["binary_sha256"], build_fingerprint=build["fingerprint"],
                    source_hashes=build["input"]["sources"], capture="Untouched game PNGs.",
                    environment_overrides=False, shots=ordered)
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    gallery(output, ordered)
    print("Gallery:", output / "index.html", flush=True)


if __name__ == "__main__":
    main()
