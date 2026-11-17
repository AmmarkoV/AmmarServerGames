#!/usr/bin/env python3
"""Deliberately corrupt a copy of a map pack for mapcheck negative tests."""

import json
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: mutate_pack.py <pack-dir> <case>\n")
        return 2
    pack, case = sys.argv[1], sys.argv[2]
    path = pack + "/map.json"
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    territories = data["territories"]

    if case == "asymmetric":
        first = territories[0]
        if not first["adjacent"]:
            raise SystemExit("first territory has no neighbour to remove")
        other_id = first["adjacent"][0]
        territories[other_id]["adjacent"].remove(first["id"])
    elif case == "orphan":
        target = next(t for t in territories if t["name"] == "Carrock")
        for t in territories:
            t["adjacent"] = [j for j in t["adjacent"] if j != target["id"]]
        target["adjacent"] = []
        data["links"] = [l for l in data["links"]
                         if target["id"] not in (l["a"], l["b"])]
    elif case == "unknown_effect":
        data["adventureCards"][0]["effect"] = "NOT_A_REAL_EFFECT"
    elif case == "uncovered_territory":
        for i, card in enumerate(data["territoryCards"]):
            if "territory" in card:
                del data["territoryCards"][i]
                break
    elif case == "label_out_of_bounds":
        territories[0]["label"][0] = data["image"]["width"] + 10
    elif case == "broken_symbols":
        for card in data["territoryCards"]:
            if "symbol" in card:
                card["symbol"] = "A"
    else:
        raise SystemExit("unknown case " + case)

    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
