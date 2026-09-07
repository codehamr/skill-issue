#!/usr/bin/env python3
"""Capture natural boundaries with natural seeds and optional matching before views.

    python3 media/boundaries.py --before build/SESSION/game-before

Untouched game PNGs, fresh configs, retained recipes, budgets and binary hashes.
"""

import argparse
import html
import json
from pathlib import Path

from weather import CACHE, ROOT, STYLE, build_record, capture, digest


SHOTS = (
    dict(key="01-clearing", title="Lichtung auf Spielerhöhe", seed=7, weather=0,
         camera="14 1.62 17 -0.73 0.07", tick=180,
         caption="Eine unregelmäßige Waldmulde mit schrägen Böschungen und flach auslaufenden Schultern."),
    dict(key="02-cut", title="Am Fuß der Böschung", seed=7, weather=0,
         camera="21 1.62 8 0.9 0.1", tick=180,
         caption="Stein, Erde und bewachsene Hänge bilden die tatsächliche Grenze der Spielfläche."),
    dict(key="03-reservoir", title="Der Wasserturm", seed=7, weather=0,
         camera="-12 1.62 -8 3.14159 0.22", tick=180,
         caption="Der Tank hinter dem südlichen Hang dient als Orientierungspunkt."),
    dict(key="04-pump", title="Die Pumpstation", seed=7, weather=0,
         camera="14 1.62 8 0 0.20", tick=180,
         caption="Auf der gegenüberliegenden Seite steht ein niedriger Zweckbau."),
    dict(key="05-mist", title="Nebel in der Waldmulde", seed=4, weather=1,
         camera="14 1.62 17 -0.73 0.07", tick=180,
         caption="Die nahe Böschung bleibt erkennbar; der Wald dahinter verliert sich im Nebel."),
    dict(key="06-rain", title="Regen am Außenweg", seed=29, weather=2,
         camera="-16 1.62 -17 -2.3 0.12", tick=180,
         caption="Ein weiterer natürlicher Wald-Seed prüft Kanten und Lesbarkeit bei Regen."),
    dict(key="07-sunshower", title="Wald im Sonnenregen", seed=130, weather=4,
         camera="14 1.62 17 -0.73 0.07", tick=180,
         caption="Wetter und Beleuchtung stammen unverändert aus dem Map-Seed."),
    dict(key="08-overview", title="Fels, Wald und Außenwege", seed=33, weather=0,
         camera="35 32 38 -0.74 -0.58", tick=180,
         caption="Die Übersicht zeigt die gekrümmte Spielfläche, die freien Außenwege und den bewachsenen Hang."),
    dict(key="09-dunes", title="Sandige Hänge", seed=59, weather=4,
         camera="14 1.62 -17 -2.625 0.15", tick=180,
         caption="Auch die Wüstenkarte erhält eine natürliche Kontur mit sandigen Schultern."),
    dict(key="10-frost", title="Schneemulde bei Nacht", seed=5, weather=0,
         camera="14 1.62 -17 -2.55 0.1", tick=180,
         caption="Schnee liegt auf den Hängen; die Leuchten stehen auf dem Boden der Lichtung."),
    dict(key="11-outline", title="Ein anderer Waldgrundriss", seed=7, weather=0,
         camera="35 32 38 -0.74 -0.58", tick=180,
         caption="Ein zweiter Seed zeigt eine andere Kontur und Verteilung der bewachsenen Schultern."),

)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/game")
    parser.add_argument("--before", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "screenshots/natural-boundaries")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    args = parser.parse_args()
    binary = args.binary.resolve()
    record = build_record(binary)
    output = args.output.resolve()
    for directory in (output, output / "logs", output / "recipes", CACHE):
        directory.mkdir(parents=True, exist_ok=True)
    binaries = [("after", binary)]
    if args.before:
        binaries.insert(0, ("before", args.before.resolve()))
    entries, cards = [], []
    for shot in SHOTS:
        views = []
        for version, executable in binaries:
            fingerprint = digest(executable)
            entry = capture(dict(shot, key=shot["key"] + "-" + version), executable,
                            output, args.width, args.height)
            if digest(executable) != fingerprint:
                raise RuntimeError("Binary changed during boundary capture")
            entry.update(version=version, binary_sha256=fingerprint)
            entries.append(entry)
            label = "Vorher" if version == "before" else "Nachher"
            views.append(f'<figure><a href="{entry["file"]}"><img src="{entry["file"]}" '
                         f'width="{args.width}" height="{args.height}" loading="lazy" '
                         f'alt="{html.escape(shot["title"])} – {label}"></a>'
                         f'<figcaption><h2>{label} · {html.escape(shot["title"])}</h2>'
                         f'<p>{html.escape(shot["caption"]) if version == "after" else "Gleicher Seed, gleiche Kamera und Konfiguration vor dem Umbau."}</p>'
                         f'<p class="meta">Seed {shot["seed"]} · '
                         f'<a href="recipes/{entry["key"]}.script">Rezept</a> · '
                         f'<a href="logs/{entry["key"]}.log">Protokoll</a></p></figcaption></figure>')
        cards.extend(views)
    (output / "manifest.json").write_text(json.dumps(
        dict(generator="media/boundaries.py", build=record, shots=entries),
        indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (output / "index.html").write_text(
        '<!doctype html><html lang="de"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Natürliche Levelgrenzen · Skill Issue</title><style>' + STYLE + '</style>'
        '<main><header><h1>Die Landschaft gibt den Rand vor.</h1>'
        '<p>Spielaufnahmen mit natürlichen Seeds. Die meisten Ansichten zeigen die Welt aus '
        '1,62 Metern Höhe; zwei Übersichten prüfen die Konturen von oben.</p></header>'
        '<section class="grid">' + ''.join(cards) + '</section><footer>'
        '<a href="manifest.json">Build, Seeds und Bild-Hashes</a></footer></main></html>',
        encoding="utf-8")
    print("Gallery:", output / "index.html")


if __name__ == "__main__":
    main()
