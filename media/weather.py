#!/usr/bin/env python3
"""Capture natural procedural weather as untouched game PNGs.

    python3 media/weather.py
    python3 media/weather.py --only 07-lightning

Named match settings and random arenas use the real game constructor. Each
image gets a fresh configuration; all choices and navigation are retained.
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
OUTPUT = ROOT / "screenshots/environment-settings"
WEATHER = ("Klar", "Leichter Dunst", "Bewölkt", "Nebel", "Regen", "Gewitter", "Sonnenregen")
SHOTS = (
    dict(key="01-desert-sunset", title="Orange Abendsonne über dem Sand", seed=59, weather=1,
         environment="dunes sunset haze", camera="17 3.2 19 -0.73 0.02", tick=180,
         caption="Tiefes, orangefarbenes Sonnenlicht, lange Schatten und warmer Staub in der Ferne. Der Sand gibt Licht in die Schatten zurück."),
    dict(key="02-desert-golden", title="Die goldene Stunde", seed=59, weather=1,
         environment="dunes golden haze", camera="17 3.2 19 -0.73 0.02", tick=180,
         caption="Die gleiche Wüste mit höherer Abendsonne. Gelände und Deckung bleiben gleich; das Licht verändert die Stimmung."),
    dict(key="03-desert-day", title="Strahlender Wüstentag", seed=59, weather=0,
         environment="dunes day clear", camera="17 3.2 19 -0.73 0.02", tick=180,
         caption="Klarer Himmel, heller Sand und deutlich kürzere Schatten: Tageslicht bleibt ein häufiger Zufallsfall."),
    dict(key="04-random-forest", title="Ein sonniger Zufallswald", seed=7, weather=0,
         camera="14 1.62 17 -0.73 0.07", tick=180,
         caption="Diese Waldkarte entsteht vollständig aus Zufallseinstellungen. Wälder und Wüsten machen jeweils etwa 30 Prozent der Auswahl aus."),
    dict(key="05-mist-forest", title="Nebel bleibt eine Abwechslung", seed=4, weather=3,
         environment="forest day fog", camera="14 1.62 17 -0.73 0.07", tick=180,
         caption="Dichter Waldnebel ist weiterhin gezielt wählbar. Im Zufall tritt er deutlich seltener auf als klare Luft oder leichter Dunst."),
    dict(key="06-rain", title="Regen im Industriehof", seed=43, weather=4,
         environment="marsh day rain", camera="17 1.9 19 -0.73 0.03", tick=180,
         caption="Wind und Regen gehören weiter zur Auswahl. Zufallswetter berücksichtigt jetzt das Biom."),
    dict(key="07-lightning", title="Ein seltenes Gewitter", seed=38, weather=5,
         environment="marsh day storm", camera="-16 2.1 -18 2.17 0.13", tick=562,
         caption="Ein Blitz erhellt den Hof; der Donner folgt im Spiel zeitversetzt. Gewitter sind selten, bleiben aber direkt wählbar."),
    dict(key="08-cloudy-snow", title="Bewölkter Wintertag", seed=22, weather=2,
         environment="frost day cloudy", camera="17 3.0 19 -0.73 0.04", tick=180,
         caption="Bewölkt ist eine eigene Auswahl mit weichem Tageslicht, ohne Regen oder dichten Nebel."),
    dict(key="09-sunshower", title="Sonnenregen über dem Sand", seed=59, weather=6,
         environment="dunes golden sunshower", camera="17 2.1 -18 -2.625 0.15", tick=180,
         caption="Ein seltener Sonnenregen; der Regenbogen liegt der tatsächlichen Sonne gegenüber."),
    dict(key="10-match-settings", title="Biom, Tageszeit und Wetter selbst wählen", seed=59, weather=1,
         environment="dunes golden haze", camera="17 3.2 19 -0.73 0.02", tick=0, menu=True, selected=True,
         caption="Wüste, Goldene Stunde und leichter Dunst werden über die echten Menüzeilen gewählt. Die Auswahl gilt ab der nächsten lokalen Arena."),
    dict(key="11-random-settings", title="Zufall bleibt der Standard", seed=7, weather=0,
         camera="14 1.62 17 -0.73 0.07", tick=0, menu=True,
         caption="Alle drei Einstellungen starten auf RANDOM. Feste Entscheidungen bleiben gespeichert, Zufall wird bei jeder neuen Arena erneut bestimmt."),
    dict(key="12-clear-night", title="Eine bewusst gewählte Winternacht", seed=5, weather=0,
         environment="frost night clear", camera="16 2.8 -18 -2.55 0.10", tick=180,
         caption="Nacht ist eine Tageszeit statt eines eigenen Bioms. In der Zufallsauswahl entstehen nur noch rund fünf Prozent Nachtkarten."),
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
    commands = []
    if shot.get("environment"):
        commands.append("environment " + shot["environment"])
    commands += ["botfreeze on", "hud off", "showfps off", "map", "sun",
                 "cam " + shot["camera"]]
    if shot["tick"]:
        commands.append("wait " + str(shot["tick"]))
    if shot.get("menu"):
        commands += ["menu", "uiframe", "nav down", "uiframe", "nav ok",
                     "uiframe", "uiframe", "uiframe"]
        if shot.get("selected"):
            for action in ("down", "down", "down", "right", "right", "down", "right", "right", "down", "right", "right"):
                commands += ["nav " + action, "uiframe"]
        commands.append("uistat")
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
        if shot.get("validate_weather", True) and expected not in log.splitlines():
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
                commands=commands, config="Fresh built-in defaults; environment and menu choices are recorded in commands.",
                game_state=[line for line in log.splitlines() if line.startswith(
                    ("environment biome=", "map seed=", "biome kind=", "map theme=", "sun elev=", "ui menu=", "budget "))])


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
<p>Prozedurale Arenen zwischen Wald, Schnee, Sand und nassem Mauerwerk. Biom, Tageszeit und sieben Wetterarten lassen sich unabhängig wählen. Zufall bevorzugt helle Tage, warmes Abendlicht und trockene Wüsten. Die Galerie zeigt unveränderte PNG-Aufnahmen aus dem Spiel.</p></header>
<nav>{nav}</nav><section class="grid">{''.join(cards)}</section>
<footer><p>Unveränderte Spielbilder. Alle Umgebungsoptionen sind in den Aufnahmerezepten dokumentiert. Bilder öffnen sich in voller Auflösung.
<a href="manifest.json">Seeds, Build-Hashes und Aufnahmedaten</a>{review} · Reproduktion: <code>python3 media/weather.py</code>.</p></footer></main></body></html>'''
    (output / "index.html").write_text(doc)
    if output != OUTPUT:
        return
    tiles = [('<a href="environment-settings/index.html"><h2>Prozedurale Welten & Wetter</h2>'
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
