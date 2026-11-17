#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the LOTR data pack (map.json + board.svg) for FRISK.

The plan (PLAN.md step 2.5) describes a generator for an original map.  The
owner chose to ship the RISK: The Lord of the Rings Trilogy Edition board as
the first data pack instead, so this script holds that pack's data.  The
generated files are checked in; this script is kept only for regeneration.

Geometry note: the board art is a raster scan whose printed territory borders
are illustrations rather than clean vector outlines.  Territories therefore
use anchor hexes centred on the printed labels (`path` per territory), while
the board art itself supplies the coastlines and printed labels.  The renderer
tints the hexes by owner and overlays army counts; this keeps every territory
clickable and all game data authoritative in the pack.

Run:
    python3 -I tools/mkmap.py
from the frisk directory.  It rewrites res/maps/lotr/{map.json,board.svg}.
"""

import json
import math
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.path.join(ROOT, "res", "maps", "lotr")
WIDTH, HEIGHT = 1088, 1600
HEX_RADIUS = 20.0

# ---------------------------------------------------------------------------
# Regions (board legend: reinforcement bonus printed on the gameboard)
# ---------------------------------------------------------------------------
REGIONS = [
    ("Arnor", 7, "#c78363"),
    ("Rhovanion", 5, "#d6b45c"),
    ("Rohan", 4, "#8fa993"),
    ("Mirkwood", 4, "#6e9160"),
    ("Eriador", 3, "#d8d074"),
    ("Rhûn", 2, "#8b7a62"),
    ("Gondor", 7, "#9b5f2f"),
    ("Mordor", 2, "#7b7250"),
    ("Haradwaith", 2, "#d8b33c"),
]

REGION_OF = {
    "Arnor": [
        "Forodwaith", "EasternAngmar", "Angmar", "Rhudaur", "WeatherHills",
        "Fornost", "NorthDowns", "Borderlands", "Buckland", "OldForest",
        "SouthDowns",
    ],
    "Eriador": [
        "Forlindon", "Mithlond", "LuneValley", "EvendimHills", "Harlindon",
        "TowerHills", "TheShire",
    ],
    "Mirkwood": [
        "Carrock", "NorthMirkwood", "AnduinValley", "EasternMirkwood",
        "SouthMirkwood",
    ],
    "Rhovanion": [
        "Moria", "Lorien", "GladdenFields", "Esgaroth", "TheWold",
        "BrownLands", "EmynMuil", "DeadMarshes",
    ],
    "Rohan": [
        "Eregion", "Dunland", "Fangorn", "GapOfRohan", "WestRohan",
        "Minhiriath", "Enedwaith",
    ],
    "Gondor": [
        "Andrast", "Anfalas", "Belfalas", "DruwaithIaur", "Ithilien",
        "Lamedon", "Lebennin", "MinasTirith", "SouthIthilien", "ValeOfErech",
    ],
    "Mordor": [
        "Barad-Dur", "Gorgoroth", "MinasMorgul", "MountDoom", "Nurn",
        "UdunVale",
    ],
    "Haradwaith": [
        "Harad", "NearHarad", "DeepHarad", "Umbar", "Harondor", "Khand",
    ],
    "Rhûn": ["NorthRhun", "SouthRhun", "RhunHills", "WitheredHeath"],
}

# Internal key -> printed name.  Keys are ASCII so the adjacency data is easy
# to read; names are what players see.
DISPLAY = {
    "Andrast": "Andrast", "AnduinValley": "Anduin Valley",
    "Anfalas": "Anfalas", "Angmar": "Angmar", "Barad-Dur": "Barad-Dûr",
    "Belfalas": "Belfalas", "Borderlands": "Borderlands",
    "BrownLands": "Brown Lands", "Buckland": "Buckland",
    "Carrock": "Carrock", "DeadMarshes": "Dead Marshes",
    "DeepHarad": "Deep Harad", "DruwaithIaur": "Druwaith Iaur",
    "Dunland": "Dunland", "EasternAngmar": "Eastern Angmar",
    "EasternMirkwood": "Eastern Mirkwood", "EmynMuil": "Emyn Muil",
    "Enedwaith": "Enedwaith", "Eregion": "Eregion",
    "Esgaroth": "Esgaroth", "EvendimHills": "Evendim Hills",
    "Fangorn": "Fangorn", "Forlindon": "Forlindon",
    "Fornost": "Fornost", "Forodwaith": "Forodwaith",
    "GapOfRohan": "Gap of Rohan", "GladdenFields": "Gladden Fields",
    "Gorgoroth": "Gorgoroth", "Harad": "Harad",
    "Harlindon": "Harlindon", "Harondor": "Harondor",
    "Ithilien": "Ithilien", "Khand": "Khand", "Lamedon": "Lamedon",
    "Lebennin": "Lebennin", "Lorien": "Lothlórien",
    "LuneValley": "Lune Valley", "MinasMorgul": "Minas Morgul",
    "MinasTirith": "Minas Tirith", "Minhiriath": "Minhiriath",
    "Mithlond": "Mithlond", "Moria": "Moria",
    "MountDoom": "Mount Doom", "NearHarad": "Near Harad",
    "NorthDowns": "North Downs", "NorthMirkwood": "North Mirkwood",
    "NorthRhun": "North Rhûn", "Nurn": "Nurn",
    "OldForest": "Old Forest", "Rhudaur": "Rhudaur",
    "RhunHills": "Rhûn Hills", "SouthDowns": "South Downs",
    "SouthIthilien": "South Ithilien",
    "SouthMirkwood": "South Mirkwood", "SouthRhun": "South Rhûn",
    "TheShire": "The Shire", "TheWold": "The Wold",
    "TowerHills": "Tower Hills", "UdunVale": "Udûn Vale",
    "Umbar": "Umbar", "ValeOfErech": "Vale of Erech",
    "WeatherHills": "Weather Hills", "WestRohan": "West Rohan",
    "WitheredHeath": "Withered Heath",
}

# Label anchor positions on the 1088x1600 board: inside the printed territory,
# next to its printed name.  Re-checked 2026-11-17 against board.jpg and the
# sharper photo res/textures/map_full.jpg (31 anchors had drifted into
# neighbouring territories, e.g. Belfalas sat in Mordor).
LABELS = {
    "Andrast": (255, 1240), "AnduinValley": (740, 505), "Anfalas": (365, 1225),
    "Angmar": (525, 168), "Barad-Dur": (995, 1190), "Belfalas": (540, 1300),
    "Borderlands": (450, 235), "BrownLands": (888, 640), "Buckland": (398, 418),
    "Carrock": (695, 290), "DeadMarshes": (785, 945), "DeepHarad": (655, 1550),
    "DruwaithIaur": (305, 1170), "Dunland": (480, 670),
    "EasternAngmar": (705, 130), "EasternMirkwood": (835, 485),
    "EmynMuil": (800, 735), "Enedwaith": (455, 840), "Eregion": (540, 575),
    "Esgaroth": (880, 305), "EvendimHills": (340, 260),
    "Fangorn": (560, 855), "Forlindon": (122, 295), "Fornost": (420, 355),
    "Forodwaith": (590, 95), "GapOfRohan": (570, 945),
    "GladdenFields": (700, 515), "Gorgoroth": (905, 1255),
    "Harad": (790, 1530), "Harlindon": (230, 580), "Harondor": (700, 1410),
    "Ithilien": (715, 1125), "Khand": (1035, 1455), "Lamedon": (520, 1205),
    "Lebennin": (595, 1262), "Lorien": (640, 685),
    "LuneValley": (260, 250), "MinasMorgul": (800, 1235),
    "MinasTirith": (612, 1158), "Minhiriath": (305, 660),
    "Mithlond": (172, 440), "Moria": (610, 565), "MountDoom": (880, 1180),
    "NearHarad": (930, 1480), "NorthDowns": (430, 300),
    "NorthMirkwood": (775, 300), "NorthRhun": (980, 110), "Nurn": (880, 1308),
    "OldForest": (455, 440), "Rhudaur": (620, 300), "RhunHills": (1005, 835),
    "SouthDowns": (475, 495), "SouthIthilien": (705, 1305),
    "SouthMirkwood": (805, 645), "SouthRhun": (990, 440),
    "TheShire": (340, 505), "TheWold": (655, 795), "TowerHills": (330, 360),
    "UdunVale": (800, 1105), "Umbar": (655, 1485),
    "ValeOfErech": (445, 1200), "WeatherHills": (480, 390),
    "WestRohan": (385, 1015), "WitheredHeath": (925, 170),
}

# Printed strongholds (one per named site drawn on the board).
STRONGHOLDS = {
    "Moria",          # Mines of Moria
    "EvendimHills",   # Annuminas
    "Fangorn",        # Isengard (north of the Gap of Rohan border)
    "WestRohan",      # Helm's Deep
    "Rhudaur",        # Rivendell
    "SouthMirkwood",  # Dol Guldur
    "UdunVale",       # Udûn
    "Barad-Dur",
    "MinasMorgul",
    "MinasTirith",
    "Umbar",          # City of the Corsairs
}

# Printed sites of power (official board has 22).
SITES = {
    # Each site's territory is printed on its mission card.
    "Fornost": "Bree", "Forodwaith": "Carn Dûm", "DeadMarshes": "Dagorlad",
    "Belfalas": "Dol Amroth", "BrownLands": "Easterling Encampment",
    "GapOfRohan": "Edoras", "NorthMirkwood": "Elven Halls",
    "Esgaroth": "Erebor", "ValeOfErech": "Erech", "Eregion": "Gates of Moria",
    "Mithlond": "Grey Havens", "Khand": "Haradrim Encampment",
    "Harlindon": "Harlond", "TheShire": "Hobbiton", "Lorien": "Lothlórien",
    "MountDoom": "Mount Doom", "EasternAngmar": "Mount Gundabad",
    "Ithilien": "Osgiliath", "Lebennin": "Pelargir", "TheWold": "Rauros",
    "Minhiriath": "Tharbad", "WeatherHills": "Weathertop",
}

# Land borders as they read on the board.  Sea links are kept separate below.
ADJ = {
    "Forlindon": ["LuneValley"],
    "Mithlond": ["TowerHills", "Harlindon"],
    "LuneValley": ["Forlindon", "EvendimHills", "NorthDowns", "TowerHills"],
    "EvendimHills": ["LuneValley", "NorthDowns", "TowerHills", "TheShire"],
    "Harlindon": ["Mithlond", "TowerHills"],
    "TowerHills": ["LuneValley", "EvendimHills", "Mithlond", "Harlindon",
                   "TheShire"],
    "TheShire": ["TowerHills", "EvendimHills", "NorthDowns", "Buckland"],
    "Forodwaith": ["EasternAngmar", "Angmar"],
    "EasternAngmar": ["Forodwaith", "Angmar", "Rhudaur"],
    "Angmar": ["Forodwaith", "EasternAngmar", "Rhudaur", "NorthDowns",
               "Borderlands"],
    "Rhudaur": ["EasternAngmar", "Angmar", "WeatherHills", "Borderlands"],
    "NorthDowns": ["LuneValley", "EvendimHills", "Angmar", "Borderlands",
                   "Fornost", "TheShire"],
    "Fornost": ["NorthDowns", "WeatherHills", "Buckland"],
    "Borderlands": ["Angmar", "Rhudaur", "NorthDowns", "OldForest"],
    "WeatherHills": ["Fornost", "Rhudaur", "Buckland", "OldForest",
                     "SouthDowns"],
    "Buckland": ["TheShire", "Fornost", "WeatherHills", "OldForest",
                 "SouthDowns"],
    "OldForest": ["Borderlands", "WeatherHills", "Buckland", "SouthDowns"],
    "SouthDowns": ["WeatherHills", "Buckland", "OldForest", "Minhiriath",
                   "Eregion"],
    "Carrock": ["NorthMirkwood", "AnduinValley"],
    "NorthMirkwood": ["Carrock", "AnduinValley", "EasternMirkwood",
                      "Esgaroth"],
    "AnduinValley": ["Carrock", "NorthMirkwood", "GladdenFields",
                     "EasternMirkwood", "SouthMirkwood", "Lorien"],
    "EasternMirkwood": ["NorthMirkwood", "AnduinValley", "SouthMirkwood",
                        "Esgaroth", "GladdenFields"],
    "SouthMirkwood": ["AnduinValley", "EasternMirkwood", "Lorien",
                      "BrownLands"],
    "Esgaroth": ["NorthMirkwood", "EasternMirkwood", "WitheredHeath"],
    "Moria": ["Eregion", "Lorien"],
    "Lorien": ["Moria", "AnduinValley", "SouthMirkwood", "GladdenFields",
               "BrownLands", "EmynMuil"],
    "GladdenFields": ["AnduinValley", "EasternMirkwood", "Lorien"],
    "TheWold": ["Fangorn", "GapOfRohan", "WestRohan", "EmynMuil",
                "BrownLands", "DeadMarshes"],
    "BrownLands": ["Lorien", "SouthMirkwood", "EmynMuil", "TheWold",
                   "DeadMarshes"],
    "EmynMuil": ["Lorien", "BrownLands", "TheWold", "DeadMarshes"],
    "DeadMarshes": ["EmynMuil", "BrownLands", "TheWold", "UdunVale",
                    "Ithilien"],
    "Eregion": ["SouthDowns", "Enedwaith", "Dunland", "Moria"],
    "Dunland": ["Eregion", "Enedwaith", "Minhiriath", "GapOfRohan",
                "Fangorn"],
    "Fangorn": ["Dunland", "GapOfRohan", "WestRohan", "TheWold"],
    "GapOfRohan": ["Dunland", "Fangorn", "WestRohan", "TheWold"],
    "WestRohan": ["GapOfRohan", "Fangorn", "TheWold", "ValeOfErech",
                  "Lamedon"],
    "Minhiriath": ["SouthDowns", "Enedwaith", "Dunland"],
    "Enedwaith": ["Minhiriath", "Eregion", "Dunland"],
    "Andrast": ["Anfalas", "DruwaithIaur"],
    "Anfalas": ["Andrast", "DruwaithIaur", "Lamedon", "Belfalas"],
    "DruwaithIaur": ["Andrast", "Anfalas", "ValeOfErech", "Lamedon"],
    "ValeOfErech": ["DruwaithIaur", "Lamedon", "WestRohan"],
    "Lamedon": ["Anfalas", "DruwaithIaur", "ValeOfErech", "Lebennin",
                "Belfalas", "WestRohan"],
    "Lebennin": ["Lamedon", "Belfalas", "MinasTirith", "SouthIthilien"],
    "Belfalas": ["Anfalas", "Lamedon", "Lebennin"],
    "MinasTirith": ["Lebennin", "Ithilien", "SouthIthilien"],
    "Ithilien": ["MinasTirith", "SouthIthilien", "DeadMarshes",
                 "MinasMorgul"],
    "SouthIthilien": ["Ithilien", "MinasTirith", "Lebennin", "Harondor",
                      "MinasMorgul"],
    "UdunVale": ["DeadMarshes", "Gorgoroth", "MountDoom"],
    "Gorgoroth": ["UdunVale", "MountDoom", "Barad-Dur", "Nurn",
                  "MinasMorgul"],
    "MountDoom": ["UdunVale", "Gorgoroth", "Barad-Dur", "Nurn"],
    "Barad-Dur": ["Gorgoroth", "MountDoom", "Nurn"],
    "Nurn": ["Gorgoroth", "MountDoom", "Barad-Dur", "NearHarad"],
    "MinasMorgul": ["Ithilien", "SouthIthilien", "Gorgoroth"],
    "Harad": ["Umbar", "NearHarad", "DeepHarad"],
    "NearHarad": ["Harad", "DeepHarad", "Khand", "Harondor", "Nurn"],
    "DeepHarad": ["Harad", "NearHarad", "Khand"],
    "Umbar": ["Harad", "Harondor"],
    "Harondor": ["Umbar", "NearHarad", "SouthIthilien"],
    "Khand": ["NearHarad", "DeepHarad", "RhunHills", "SouthRhun"],
    "NorthRhun": ["WitheredHeath", "SouthRhun", "RhunHills"],
    "SouthRhun": ["NorthRhun", "RhunHills", "Khand"],
    "RhunHills": ["NorthRhun", "SouthRhun", "Khand"],
    "WitheredHeath": ["NorthRhun", "Esgaroth"],
}

# Corrections from the 2026-11-17 audit against the board photo.  Rivers are
# only crossed at the bridges drawn on the board and mountain ranges are
# impassable (rulebook p.3); the original table had several edges across the
# Anduin, through the White Mountains and between opposite ends of Haradwaith.
ADJ_ADD = [('Rhudaur', 'Eregion'), ('BrownLands', 'SouthRhun'), ('Enedwaith', 'WestRohan'), ('Umbar', 'DeepHarad'), ('Harondor', 'Harad'), ('AnduinValley', 'EmynMuil'), ('SouthMirkwood', 'EmynMuil'), ('Lorien', 'TheWold')]
ADJ_REMOVE = [('DeepHarad', 'Khand'), ('DeepHarad', 'NearHarad'), ('WestRohan', 'TheWold'), ('WestRohan', 'ValeOfErech'), ('WestRohan', 'Lamedon'), ('Lorien', 'AnduinValley'), ('Lorien', 'SouthMirkwood'), ('Lorien', 'EmynMuil'), ('Lorien', 'BrownLands'), ('GladdenFields', 'EasternMirkwood'), ('TheWold', 'BrownLands'), ('TheWold', 'DeadMarshes')]
for _a, _b in ADJ_ADD:
    ADJ[_a].append(_b)
    ADJ[_b].append(_a)
for _a, _b in ADJ_REMOVE:
    ADJ[_a].remove(_b)
    ADJ[_b].remove(_a)

# Bridges drawn on the board (river crossings); each joins adjacent territories.
BRIDGES = [
    ("LuneValley", "EvendimHills"), ("TheShire", "Buckland"),
    ("WeatherHills", "Rhudaur"), ("Rhudaur", "Eregion"), ("Eregion", "Dunland"),
    ("Minhiriath", "Dunland"), ("Carrock", "AnduinValley"),
    ("NorthMirkwood", "EasternMirkwood"), ("NorthMirkwood", "Esgaroth"),
    ("GladdenFields", "AnduinValley"), ("BrownLands", "SouthRhun"),
    ("TheWold", "EmynMuil"), ("TheWold", "GapOfRohan"),
    ("Enedwaith", "WestRohan"), ("MinasTirith", "Ithilien"),
    ("Umbar", "DeepHarad"), ("Harondor", "Harad"),
]

# Mountain ranges and the territories next to them (The Way Is Under the
# Mountains lets an invasion cross between two territories of one range).
MOUNTAINS = [
    ("Blue Mountains (north)", ["Forlindon", "LuneValley", "Mithlond"]),
    ("Blue Mountains (south)", ["Harlindon", "Mithlond", "TheShire"]),
    ("Misty Mountains", ["Forodwaith", "EasternAngmar", "Angmar", "Rhudaur",
                         "Carrock", "Eregion", "Moria", "GladdenFields",
                         "Lorien", "Dunland", "Fangorn"]),
    ("Grey Mountains", ["EasternAngmar", "WitheredHeath", "Carrock",
                        "NorthMirkwood"]),
    ("Mountains of Rhûn", ["WitheredHeath", "NorthRhun", "Esgaroth",
                           "SouthRhun"]),
    ("Hills of Rhûn", ["BrownLands", "RhunHills", "SouthRhun"]),
    ("White Mountains", ["WestRohan", "GapOfRohan", "DruwaithIaur",
                         "ValeOfErech", "Lamedon", "MinasTirith", "Lebennin",
                         "Andrast", "Anfalas"]),
    ("Ash Mountains", ["DeadMarshes", "BrownLands", "UdunVale", "Barad-Dur"]),
    ("Mountains of Shadow", ["Ithilien", "SouthIthilien", "UdunVale",
                             "MinasMorgul", "Gorgoroth", "Nurn", "Harondor",
                             "NearHarad", "Khand"]),
]

# Sea routes drawn dashed on the board.  They are additional adjacency for the
# engine, and their ends are the board's ports.
LINKS = [
    ("Forodwaith", "Forlindon"),
    ("Forlindon", "Mithlond"),
    ("Mithlond", "Minhiriath"),
    ("Minhiriath", "Belfalas"),
    ("Minhiriath", "Umbar"),
    ("Belfalas", "Umbar"),
    ("Belfalas", "Harondor"),
    ("Anfalas", "Umbar"),
    ("Enedwaith", "Harlindon"),
    ("BrownLands", "RhunHills"),
]

# Rulebook: yellow and green are the good armies, black and red the evil ones.
FACTIONS = [
    {"name": "Elves of the West", "color": "#e0c341", "side": "good"},
    {"name": "Riders of Rohan", "color": "#4f8a45", "side": "good"},
    {"name": "Legions of Mordor", "color": "#b03834", "side": "evil"},
    {"name": "Hordes of Isengard", "color": "#2b2b2b", "side": "evil"},
]

EFFECT_IDS = {
    # generic effects (kept for other packs)
    "HOLD_TERRITORIES", "HOLD_REGION", "CONQUER_N_THIS_TURN",
    "LEADER_REACH", "HOLD_STRONGHOLDS",
    "REGION_ATTRITION", "REINFORCE_ALL", "LOSE_LEADER_UNLESS", "SEA_CLOSED",
    "ATTACK_BONUS", "DEFENSE_BONUS", "REROLL", "EXTRA_REINFORCE",
    "FREE_FORTIFY", "RECRUIT_LEADER", "CANCEL_EVENT",
    # printed LOTR cards (PLAN.md 10b)
    "FREE_LEADER", "SIDE_CONTROL", "SEABORNE_TOLL", "LOCAL_UPRISING",
    "RAIDERS", "MUSTERING", "WINTER_STORMS",
    "AMBUSH", "SEABORNE_BONUS", "LEADER_MOVE", "INVADED_REINFORCE", "GRIMA",
    "EXTRA_REDEPLOY", "NO_STRONGHOLD", "WITHDRAW", "MOUNTAIN_PASS",
    "STORM_REPEL", "BRIDGE_BLOCK", "SITE_MISSION",
}

# The printed adventure deck (scans in res/textures/cards, copied into the
# pack as card art).  The ten Fellowship cards are left out: FRISK has no
# Ring track.  Texts are the printed ones.
CARD_ART = os.path.join(ROOT, "res", "textures", "cards")


def _event(cid, title, text, effect, args, image):
    return {"id": cid, "kind": "event", "title": title,
            "text": "Play immediately. " + text, "effect": effect,
            "args": args, "image": "cards/" + image}


def _power(cid, title, timing, text, effect, args, image):
    return {"id": cid, "kind": "power", "title": title,
            "text": timing + " " + text, "effect": effect,
            "args": dict(args, points=1), "image": "cards/" + image}


def _mission(cid, site, site_territory, reward, evil, good, epts, gpts, image,
             **extra):
    if reward == "card":
        text = ("Draw 1 extra Territory card at the end of your turn "
                "(up to a maximum of 5 in your hand).")
    elif reward == "oathbreakers":
        text = ("Gather the Oathbreakers. Put 6 battalions on this card. "
                "Anytime you declare an invasion, you may place these "
                "battalions into the Territory you are invading from.")
    elif reward == "region":
        text = "Receive extra battalions anywhere in %s." % extra["region"]
    elif reward == "territory":
        text = "Receive extra battalions in %s." % extra["territory"]
    elif reward == "port":
        text = "Receive extra battalions in any 1 Port."
    else:
        text = "Receive extra battalions in any Territory."
    args = {"site": site_territory, "reward": reward,
            "good": good, "evil": evil,
            "points": {"good": gpts, "evil": epts}}
    args.update(extra)
    return {"id": cid, "kind": "mission", "title": site,
            "text": "%s (%s). %s" % (site, site_territory, text),
            "effect": "SITE_MISSION", "args": args,
            "image": "cards/" + image}


_APPOINT = ("Place a second Leader in any Territory you control. If you "
            "already have 2 Leaders in play, then discard this card.")
_GRIMA = ("Remove up to 2 enemy battalions from the Territory you are "
          "invading and add them to your invading Territory. (1 defending "
          "battalion must always remain.)")
_STRONGHOLD = "The Stronghold loses its +1 advantage until the invasion ends."
_MOUNTAINS = ("Invade from one Territory next to a mountain range into one "
              "other Territory next to the same mountain range.")
_BRIDGE = ("Choose one bridge to block until the start of your next turn. "
           "No battalions may cross the bridge unless accompanied by a Leader.")

ADVENTURE_CARDS = [
    # Events ("Play immediately")
    _event("e01", "Appoint a Second Leader", _APPOINT, "FREE_LEADER",
           {"max": 2}, "AppointASecondLeader.jpg"),
    _event("e02", "Appoint a Second Leader", _APPOINT, "FREE_LEADER",
           {"max": 2}, "AppointASecondLeader2.jpg"),
    _event("e03", "Appoint a Second Leader", _APPOINT, "FREE_LEADER",
           {"max": 2}, "AppointASecondLeader3.jpg"),
    _event("e04", "Appoint a Second Leader", _APPOINT, "FREE_LEADER",
           {"max": 2}, "AppointASecondLeader4.jpg"),
    _event("e05", "Aragorn Arrives",
           "If a Good force controls Minas Tirith, that player receives 10 "
           "extra battalions there; otherwise, the Evil force loses 2 "
           "battalions there. (1 battalion must always remain.)",
           "SIDE_CONTROL", {"territory": "Minas Tirith", "side": "good",
                            "gain": 10, "loss": 2}, "AragornArrives.jpg"),
    _event("e06", "The Black Gates Open",
           "If an Evil force controls Udûn Vale, that player receives 10 "
           "extra battalions there; otherwise, the Good force loses 2 "
           "battalions there. (1 battalion must always remain.)",
           "SIDE_CONTROL", {"territory": "Udûn Vale", "side": "evil",
                            "gain": 10, "loss": 2}, "TheBlackGatesOpen.jpg"),
    _event("e07", "The Entmoot",
           "If a Good force controls Fangorn, that player receives 2 extra "
           "battalions there; otherwise, the Evil force loses 2 battalions "
           "there. (1 battalion must always remain.)",
           "SIDE_CONTROL", {"territory": "Fangorn", "side": "good",
                            "gain": 2, "loss": 2}, "TheEntmoot.jpg"),
    _event("e08", "They've Brought a Cavetroll!",
           "If an Evil force controls Moria, that player receives 2 extra "
           "battalions there; otherwise, the Good force loses 2 battalions "
           "there. (1 battalion must always remain.)",
           "SIDE_CONTROL", {"territory": "Moria", "side": "evil",
                            "gain": 2, "loss": 2}, "TheyveBroughtACavetroll.jpg"),
    _event("e09", "Corsairs of Umbar",
           "Until the start of your next turn, all seaborne invasions lose "
           "3 battalions before they begin invading. (1 battalion must "
           "always remain.)", "SEABORNE_TOLL", {"loss": 3},
           "CorsairsOfUmbar.jpg"),
    _event("e10", "Local Uprising",
           "Draw a Territory card and secretly look at it. When you control "
           "that Territory, reveal the card and add 4 battalions to that "
           "Territory.", "LOCAL_UPRISING", {"n": 4}, "LocalUprising.jpg"),
    _event("e11", "Raiders",
           "Draw the top 3 Territory cards. Half of the battalions in each "
           "of these Territories are destroyed. Round any losses down, e.g. "
           "5 battalions = lose 2 battalions. (1 battalion must always "
           "remain.)", "RAIDERS", {"cards": 3}, "Raiders.jpg"),
    _event("e12", "The Mustering of Middle-earth",
           "Draw the top 3 Territory cards. Each of these Territories "
           "receive 3 extra battalions.", "MUSTERING",
           {"cards": 3, "n": 3}, "TheMusteringOfMiddleEarth.jpg"),
    _event("e13", "Winter Storms",
           "Draw the top 3 Territory cards. No player can invade into or out "
           "of these Territories until the start of your next turn.",
           "WINTER_STORMS", {"cards": 3}, "WinterStorms.jpg"),
    # Powers
    _power("p01", "Ambush!", "Play before invading a Territory.",
           "Gain 3 extra battalions in the Territory you are invading from.",
           "AMBUSH", {"n": 3}, "Ambush.jpg"),
    _power("p02", "Black Sails", "Play after declaring a seaborne invasion.",
           "Add 1 to your highest die roll for the entire invasion.",
           "SEABORNE_BONUS", {"n": 1}, "BlackSails.jpg"),
    _power("p03", "Borne to Another Place", "Play at the start of your turn.",
           "Move one of your Leaders to any Territory you control. The "
           "Leader can then continue as normal.", "LEADER_MOVE", {},
           "BorneToAnotherPlace.jpg"),
    _power("p04", "Courage Alone Will Not Save You!",
           "Play after an enemy declares an invasion on one of your "
           "Territories.", "Gain 4 extra battalions in the Territory being "
           "invaded.", "INVADED_REINFORCE", {"n": 4},
           "CourageAloneWIllNotSaveYou.jpg"),
    _power("p05", "Gríma Wormtongue", "Play before invading a Territory.",
           _GRIMA, "GRIMA", {"n": 2}, "GrimaWormtongue.jpg"),
    _power("p06", "Gríma Wormtongue", "Play before invading a Territory.",
           _GRIMA, "GRIMA", {"n": 2}, "GrimaWormtongue2.jpg"),
    _power("p07", "Move by Night",
           "Play after your last invasion for this turn.",
           "Make up to 3 extra redeployments.", "EXTRA_REDEPLOY", {"n": 3},
           "MoveByNight.jpg"),
    _power("p08", "Siege Machines",
           "Play before invading a Territory with a Stronghold.",
           _STRONGHOLD, "NO_STRONGHOLD", {}, "SiegeMachines.jpg"),
    _power("p09", "The Enemy Is at Hand!",
           "Play before invading a Territory with a Stronghold.",
           _STRONGHOLD, "NO_STRONGHOLD", {}, "TheEnemyIsAtHand.jpg"),
    _power("p10", "Strategic Withdrawal",
           "Play after an enemy declares an invasion on one of your "
           "Territories.", "You may move all but 1 battalion from your "
           "invaded Territory into an adjacent Territory that you control.",
           "WITHDRAW", {}, "StrategicWithdrawal.jpg"),
    _power("p11", "The Way Is Under the Mountains",
           "Play after declaring an invasion.", _MOUNTAINS, "MOUNTAIN_PASS",
           {}, "TheWayIsUnderTheMountains.jpg"),
    _power("p12", "The Way Is Under the Mountains",
           "Play after declaring an invasion.", _MOUNTAINS, "MOUNTAIN_PASS",
           {}, "TheWayIsUnderTheMountains2.jpg"),
    _power("p13", "Violent Storms",
           "Play after a seaborne battle is declared on one of your "
           "Territories with a Port.", "Destroy all attacking battalions in "
           "this battle (not this invasion).", "STORM_REPEL", {},
           "ViolentStorms.jpg"),
    _power("p14", "You Shall Not Pass!", "Play at the start of your turn.",
           _BRIDGE, "BRIDGE_BLOCK", {}, "YouShallNotPass.jpg"),
    _power("p15", "You Shall Not Pass!", "Play at the start of your turn.",
           _BRIDGE, "BRIDGE_BLOCK", {}, "YouShallNotPass2.jpg"),
    # Site missions: reward for (evil, good) and points (evil, good)
    _mission("m01", "Bree", "Fornost", "region", 6, 2, 2, 1, "Bree.jpg",
             region="Arnor"),
    _mission("m02", "Carn Dûm", "Forodwaith", "region", 2, 2, 2, 2,
             "CarnDum.jpg", region="Arnor"),
    _mission("m03", "Dagorlad", "Dead Marshes", "region", 2, 2, 1, 4,
             "Dagorlad.jpg", region="Rhovanion"),
    _mission("m04", "Dol Amroth", "Belfalas", "port", 2, 2, 2, 2,
             "DolAmroth.jpg"),
    _mission("m05", "Easterling Encampment", "Brown Lands", "territory",
             2, 6, 1, 2, "EasterlingEncampment.jpg", territory="Brown Lands"),
    _mission("m06", "Edoras", "Gap of Rohan", "card", 0, 0, 2, 2,
             "Edoras.jpg"),
    _mission("m07", "Elven Halls", "North Mirkwood", "any", 1, 2, 2, 2,
             "ElvenHalls.jpg"),
    _mission("m08", "Erebor", "Esgaroth", "territory", 2, 4, 1, 2,
             "Erebor.jpg", territory="Esgaroth"),
    _mission("m09", "Erech", "Vale of Erech", "oathbreakers", 6, 6, 1, 1,
             "Erech.jpg"),
    _mission("m10", "Gates of Moria", "Eregion", "territory", 4, 2, 1, 2,
             "GatesOfMoria.jpg", territory="Eregion"),
    _mission("m11", "Grey Havens", "Mithlond", "territory", 4, 2, 2, 2,
             "GreyHavens.jpg", territory="Mithlond"),
    _mission("m12", "Haradrim Encampment", "Khand", "region", 4, 2, 1, 4,
             "HaradrimEncampment.jpg", region="Haradwaith"),
    _mission("m13", "Harlond", "Harlindon", "card", 0, 0, 3, 2,
             "Harlond.jpg"),
    _mission("m14", "Hobbiton", "The Shire", "region", 2, 2, 4, 1,
             "Hobbiton.jpg", region="Eriador"),
    _mission("m15", "Lothlórien", "Lothlórien", "card", 0, 0, 2, 2,
             "Lothlorien.jpg"),
    _mission("m16", "Mount Doom", "Mount Doom", "any", 5, 2, 2, 6,
             "MountDoom.jpg"),
    _mission("m17", "Mount Gundabad", "Eastern Angmar", "region", 4, 4, 1, 2,
             "MountGundabad.jpg", region="Arnor"),
    _mission("m18", "Osgiliath", "Ithilien", "region", 4, 2, 1, 3,
             "Osgiliath.jpg", region="Gondor"),
    _mission("m19", "Pelargir", "Lebennin", "region", 5, 5, 1, 1,
             "Pelargir.jpg", region="Gondor"),
    _mission("m20", "Rauros", "The Wold", "region", 2, 2, 2, 2,
             "Rauros.jpg", region="Rhovanion"),
    _mission("m21", "Tharbad", "Minhiriath", "region", 4, 4, 2, 2,
             "Tharbad.jpg", region="Rohan"),
    _mission("m22", "Weathertop", "Weather Hills", "region", 2, 2, 3, 1,
             "Weathertop.jpg", region="Arnor"),
]


def hex_path(x, y, r=HEX_RADIUS):
    pts = []
    for k in range(6):
        a = math.radians(60.0 * k)
        pts.append((x + r * math.cos(a), y + r * math.sin(a)))
    return "M " + " L ".join("%.1f,%.1f" % p for p in pts) + " Z"


def territory_order():
    names = []
    for _, members in REGION_OF.items():
        names.extend(members)
    return names


PORTS = sorted({n for link in LINKS for n in link})


def build_map():
    order = territory_order()
    assert len(order) == 64, len(order)
    assert len(set(order)) == 64
    for n in order:
        assert n in DISPLAY and n in LABELS, n

    region_ids = {r[0]: i for i, r in enumerate(REGIONS)}
    territory_ids = {n: i for i, n in enumerate(order)}

    # Balanced territory-card symbols (plan validation: counts within 1).
    symbols = {}
    for i, n in enumerate(order):
        symbols[n] = "ABC"[i % 3]

    # Validate the adjacency graph before writing anything.
    for a, ns in ADJ.items():
        for b in ns:
            assert b in ADJ, (a, b)
            assert a in ADJ[b], "asymmetric edge %s-%s" % (a, b)
    graph = {n: set(ADJ[n]) for n in order}
    for a, b in LINKS:
        assert a in graph and b in graph, (a, b)
        graph[a].add(b)
        graph[b].add(a)
    seen = set()
    stack = [order[0]]
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        stack.extend(graph[n] - seen)
    assert len(seen) == len(order), "graph disconnected: %s" % (
        sorted(set(order) - seen),)

    regions = []
    for i, (name, bonus, color) in enumerate(REGIONS):
        regions.append({"id": i, "name": name, "bonus": bonus,
                        "color": color})

    factions = []
    for i, f in enumerate(FACTIONS):
        factions.append({
            "id": i,
            "name": f["name"],
            "color": f["color"],
            "side": f["side"],
            "sprites": {
                "unit1": "pieces/unit1.svg",
                "unit3": "pieces/unit3.svg",
                "unit5": "pieces/unit5.svg",
                "leader": "pieces/leader.svg",
            },
        })

    territories = []
    for i, n in enumerate(order):
        x, y = LABELS[n]
        territories.append({
            "id": i,
            "name": DISPLAY[n],
            "region": region_ids[next(r for r, members in REGION_OF.items()
                                      if n in members)],
            "path": hex_path(x, y),
            "label": [x, y],
            "stronghold": n in STRONGHOLDS,
            "site": n in SITES,
            "port": n in PORTS,
            "adjacent": sorted(territory_ids[m] for m in ADJ[n]),
        })

    links = []
    for a, b in LINKS:
        xa, ya = LABELS[a]
        xb, yb = LABELS[b]
        links.append({
            "a": territory_ids[a],
            "b": territory_ids[b],
            "kind": "sea",
            "path": "M %d,%d L %d,%d" % (xa, ya, xb, yb),
        })

    territory_cards = []
    for n in order:
        territory_cards.append({"territory": territory_ids[n],
                                "symbol": symbols[n]})
    territory_cards.append({"wild": True})
    territory_cards.append({"wild": True})

    for c in ADVENTURE_CARDS:
        assert c["effect"] in EFFECT_IDS, c["id"]
        assert c["kind"] in ("mission", "event", "power"), c["id"]
        if c["effect"] == "SITE_MISSION":
            site = next(k for k, v in DISPLAY.items() if v == c["args"]["site"])
            assert site in SITES, c["id"]
    for a, b in BRIDGES:
        assert b in ADJ[a], "bridge %s-%s joins non-adjacent territories" % (a, b)
    for _name, members in MOUNTAINS:
        for n in members:
            assert n in DISPLAY, n

    return {
        "name": "The Lord of the Rings (Trilogy Edition)",
        "version": 1,
        "image": {"file": "board.svg", "width": WIDTH, "height": HEIGHT},
        "factions": factions,
        "regions": regions,
        "territories": territories,
        "links": links,
        "bridges": [{"a": territory_ids[a], "b": territory_ids[b]}
                    for a, b in BRIDGES],
        "mountains": [{"name": name,
                       "territories": sorted(territory_ids[n] for n in members)}
                      for name, members in MOUNTAINS],
        "territoryCards": territory_cards,
        "adventureCards": ADVENTURE_CARDS,
        "setup": {
            "2": {"armies": 60, "neutral": True, "neutralArmies": 2,
                  "neutralShare": 33},
            "3": {"armies": 52, "neutral": False},
            "4": {"armies": 45, "neutral": False},
        },
        "rules": {
            "setBonus": {"AAA": 4, "BBB": 6, "CCC": 8, "ABC": 10},
            "minReinforce": 3,
            "territoriesPerArmy": 3,
            "strongholdReinforce": 1,
            "leadersPerPlayer": 2,
            "leaderCost": 3,
            "dieCap": 6,
            "adventureStartHand": 4,
            "drawOnLeaderConquest": True,
        },
        "_order": order,
    }


def build_svg(data):
    by_name = {t["name"]: t for t in data["territories"]}
    order = data["_order"]
    region_names = {r["id"]: r["name"] for r in data["regions"]}
    lines = []
    lines.append('<?xml version="1.0" encoding="UTF-8"?>')
    lines.append('<svg xmlns="http://www.w3.org/2000/svg" '
                 'xmlns:xlink="http://www.w3.org/1999/xlink" '
                 'width="%d" height="%d" viewBox="0 0 %d %d">'
                 % (WIDTH, HEIGHT, WIDTH, HEIGHT))
    lines.append("  <title>The Lord of the Rings (Trilogy Edition)</title>")
    lines.append("  <style>")
    lines.append("    .territory { fill: #ffffff; fill-opacity: 0.06;"
                 " stroke: #10240f; stroke-opacity: 0.55;"
                 " stroke-width: 1.8; vector-effect: non-scaling-stroke; }")
    lines.append("    .link { fill: none; stroke: #12283a;"
                 " stroke-opacity: 0.60; stroke-width: 2.0;"
                 " stroke-dasharray: 5 5;"
                 " vector-effect: non-scaling-stroke; }")
    lines.append("  </style>")
    lines.append('  <image id="board-image" x="0" y="0" width="%d" '
                 'height="%d" href="board.jpg" xlink:href="board.jpg" '
                 'preserveAspectRatio="none"/>' % (WIDTH, HEIGHT))
    lines.append('  <g id="links">')
    for l in data["links"]:
        a = data["territories"][l["a"]]["name"]
        b = data["territories"][l["b"]]["name"]
        lines.append('    <path id="l%d-%d" class="link" data-a="%s" '
                     'data-b="%s" data-kind="%s" d="%s">'
                     '<title>%s - %s</title></path>'
                     % (l["a"], l["b"], a, b, l["kind"], l["path"], a, b))
    lines.append("  </g>")
    lines.append('  <g id="territories">')
    for t in data["territories"]:
        name = t["name"]
        lines.append('    <path id="t%d" class="territory" '
                     'data-name="%s" data-region="%s" '
                     'data-label="%d,%d" d="%s">'
                     '<title>%s</title></path>'
                     % (t["id"], name,
                        region_names[t["region"]], t["label"][0],
                        t["label"][1], t["path"], name))
    lines.append("  </g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def main():
    data = build_map()
    order = data.pop("_order")
    os.makedirs(PACK, exist_ok=True)
    json_path = os.path.join(PACK, "map.json")
    svg_path = os.path.join(PACK, "board.svg")
    os.makedirs(os.path.join(PACK, "cards"), exist_ok=True)
    for card in data["adventureCards"]:
        name = card["image"].split("/", 1)[1]
        for folder in ("action", "missions"):
            source = os.path.join(CARD_ART, folder, name)
            if os.path.exists(source):
                shutil.copyfile(source, os.path.join(PACK, card["image"]))
                break
        else:
            raise SystemExit("missing card art %s" % name)
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(build_svg({**data, "_order": order}))
    print("wrote", json_path)
    print("wrote", svg_path)
    print("%d territories, %d regions, %d links, %d territory cards, "
          "%d adventure cards"
          % (len(data["territories"]), len(data["regions"]),
             len(data["links"]), len(data["territoryCards"]),
             len(data["adventureCards"])))


if __name__ == "__main__":
    main()
