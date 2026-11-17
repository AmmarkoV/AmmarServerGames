#!/usr/bin/env python3
"""Create a tiny synthetic map pack for the HTTP stress/long-game tests.

The pack is deliberately generated into a temporary directory so the shipped
LOTR data is never touched.  It has enough territories and factions for a
four-player game, one stronghold and one site of power.
"""

import json
import os
import sys


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: tiny_pack.py <output-directory>\n")
        return 2
    destination = sys.argv[1]
    os.makedirs(destination, exist_ok=True)

    factions = [
        {"id": 0, "name": "North", "color": "#4f78b8",
         "sprites": {"unit1": "pieces/unit1.svg", "unit3": "pieces/unit3.svg",
                     "unit5": "pieces/unit5.svg", "leader": "pieces/leader.svg"}},
        {"id": 1, "name": "South", "color": "#b86b4f",
         "sprites": {"unit1": "pieces/unit1.svg", "unit3": "pieces/unit3.svg",
                     "unit5": "pieces/unit5.svg", "leader": "pieces/leader.svg"}},
        {"id": 2, "name": "East", "color": "#5a9b63",
         "sprites": {"unit1": "pieces/unit1.svg", "unit3": "pieces/unit3.svg",
                     "unit5": "pieces/unit5.svg", "leader": "pieces/leader.svg"}},
        {"id": 3, "name": "West", "color": "#a38a4c",
         "sprites": {"unit1": "pieces/unit1.svg", "unit3": "pieces/unit3.svg",
                     "unit5": "pieces/unit5.svg", "leader": "pieces/leader.svg"}},
    ]
    regions = [
        {"id": 0, "name": "Upper", "bonus": 2, "color": "#8090a0"},
        {"id": 1, "name": "Lower", "bonus": 2, "color": "#a09080"},
    ]
    territories = []
    for i in range(8):
        x = 80 + (i % 4) * 140
        y = 120 + (i // 4) * 220
        territories.append({
            "id": i,
            "name": f"Land {i}",
            "region": 0 if i < 4 else 1,
            "path": (f"M {x - 45},{y - 35} L {x + 45},{y - 35} "
                     f"L {x + 45},{y + 35} L {x - 45},{y + 35} Z"),
            "label": [x, y],
            "stronghold": i == 0,
            "site": i == 1,
            "adjacent": sorted({(i - 1) % 8, (i + 1) % 8}),
        })
    cards = []
    for i in range(8):
        cards.append({"territory": i, "symbol": "ABC"[i % 3]})
    map_data = {
        "name": "Tiny Stress Map",
        "version": 1,
        "image": {"file": "board.svg", "width": 600, "height": 440},
        "factions": factions,
        "regions": regions,
        "territories": territories,
        "links": [],
        "territoryCards": cards,
        "adventureCards": [],
        "setup": {
            "2": {"armies": 10, "neutral": False},
            "3": {"armies": 12, "neutral": False},
            "4": {"armies": 16, "neutral": False},
        },
        "rules": {
            "setBonus": {"AAA": 4, "BBB": 4, "CCC": 4, "ABC": 6},
            "minReinforce": 3,
            "territoriesPerArmy": 3,
            "strongholdReinforce": 1,
            "leadersPerPlayer": 1,
            "leaderCost": 2,
            "dieCap": 6,
        },
    }
    with open(os.path.join(destination, "map.json"), "w",
              encoding="utf-8") as handle:
        json.dump(map_data, handle, indent=2)
        handle.write("\n")
    # Like the LOTR pack, territories are small anchor hexes around the
    # label (the browser test relies on the client's larger hit areas).
    with open(os.path.join(destination, "board.svg"), "w",
              encoding="utf-8") as handle:
        handle.write('<svg xmlns="http://www.w3.org/2000/svg" '
                     'width="600" height="440">\n'
                     '<style>.territory{fill:#fff;fill-opacity:.06;'
                     'stroke:#333;stroke-width:1.8}</style>\n')
        for territory in territories:
            x, y = territory["label"]
            points = " L ".join(
                f"{x + dx},{y + dy}" for dx, dy in
                ((14, 0), (7, 12), (-7, 12), (-14, 0), (-7, -12), (7, -12)))
            handle.write(f'<path id="t{territory["id"]}" class="territory" '
                         f'd="M {points} Z"/>\n')
        handle.write('</svg>\n')
    os.makedirs(os.path.join(destination, "pieces"), exist_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
