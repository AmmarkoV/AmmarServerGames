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

# Label anchor positions on the 1088x1600 board (approx. centre of the
# printed territory name; checked against the board art).
LABELS = {
    "Andrast": (455, 1265), "AnduinValley": (740, 505), "Anfalas": (650, 1245),
    "Angmar": (620, 190), "Barad-Dur": (900, 1145), "Belfalas": (1000, 1235),
    "Borderlands": (450, 235), "BrownLands": (785, 735), "Buckland": (398, 418),
    "Carrock": (650, 260), "DeadMarshes": (785, 945), "DeepHarad": (1045, 1545),
    "DruwaithIaur": (610, 1135), "Dunland": (480, 670),
    "EasternAngmar": (705, 130), "EasternMirkwood": (835, 485),
    "EmynMuil": (700, 735), "Enedwaith": (360, 645), "Eregion": (540, 575),
    "Esgaroth": (880, 305), "EvendimHills": (340, 260),
    "Fangorn": (560, 855), "Forlindon": (210, 260), "Fornost": (420, 355),
    "Forodwaith": (655, 120), "GapOfRohan": (570, 945),
    "GladdenFields": (700, 515), "Gorgoroth": (905, 1255),
    "Harad": (550, 1500), "Harlindon": (230, 580), "Harondor": (605, 1365),
    "Ithilien": (715, 1125), "Khand": (950, 1435), "Lamedon": (860, 1175),
    "Lebennin": (685, 1185), "Lorien": (640, 685),
    "LuneValley": (260, 250), "MinasMorgul": (800, 1235),
    "MinasTirith": (690, 1145), "Minhiriath": (305, 660),
    "Mithlond": (282, 485), "Moria": (610, 565), "MountDoom": (805, 1168),
    "NearHarad": (800, 1455), "NorthDowns": (430, 300),
    "NorthMirkwood": (760, 260), "NorthRhun": (980, 110), "Nurn": (825, 1315),
    "OldForest": (455, 440), "Rhudaur": (620, 300), "RhunHills": (870, 785),
    "SouthDowns": (475, 495), "SouthIthilien": (705, 1305),
    "SouthMirkwood": (805, 645), "SouthRhun": (1030, 850),
    "TheShire": (340, 505), "TheWold": (740, 845), "TowerHills": (330, 360),
    "UdunVale": (800, 1105), "Umbar": (570, 1445),
    "ValeOfErech": (750, 1145), "WeatherHills": (480, 390),
    "WestRohan": (610, 965), "WitheredHeath": (835, 130),
}

# Printed strongholds (one per named site drawn on the board).
STRONGHOLDS = {
    "Moria",          # Mines of Moria
    "EvendimHills",   # Annuminas
    "GapOfRohan",     # Isengard
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
    "TheShire",       # Hobbiton
    "NorthMirkwood",  # Elven Halls
    "Buckland",       # Bree
    "Angmar",         # Carn Dûm
    "EasternAngmar",  # Mount Gundabad
    "Mithlond",       # Grey Havens
    "Enedwaith",      # Tharbad
    "Harlindon",      # Harlond
    "Lorien",         # Lothlórien
    "WeatherHills",   # Weathertop
    "Moria",          # Gates of Moria
    "Esgaroth",       # Erebor
    "WestRohan",      # Edoras
    "RhunHills",      # Easterling Encampment
    "Ithilien",       # Osgiliath
    "Lebennin",       # Pelargir
    "NearHarad",      # Haradrim Encampment
    "ValeOfErech",    # Erech
    "TheWold",        # Rauros
    "Belfalas",       # Dol Amroth
    "DeadMarshes",    # Dagorlad
    "MountDoom",
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

# Sea routes / bridges drawn dashed on the board.  They are additional
# adjacency for the engine.
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

FACTIONS = [
    {"name": "Elves of the West", "color": "#e0c341"},
    {"name": "Riders of Rohan", "color": "#4f8a45"},
    {"name": "Legions of Mordor", "color": "#b03834"},
    {"name": "Hordes of Isengard", "color": "#2b2b2b"},
]

EFFECT_IDS = {
    "HOLD_TERRITORIES", "HOLD_REGION", "CONQUER_N_THIS_TURN",
    "LEADER_REACH", "HOLD_STRONGHOLDS",
    "REGION_ATTRITION", "REINFORCE_ALL", "LOSE_LEADER_UNLESS", "SEA_CLOSED",
    "ATTACK_BONUS", "DEFENSE_BONUS", "REROLL", "EXTRA_REINFORCE",
    "FREE_FORTIFY", "RECRUIT_LEADER", "CANCEL_EVENT",
}

ADVENTURE_CARDS = [
    # Missions
    {"id": "m01", "kind": "mission", "title": "The Shire Must Stand",
     "text": "Hold The Shire and Buckland at the start of your turn.",
     "effect": "HOLD_TERRITORIES",
     "args": {"territories": ["The Shire", "Buckland"], "reward": 2}},
    {"id": "m02", "kind": "mission", "title": "Crown of the South",
     "text": "Control all of Gondor at the start of your turn.",
     "effect": "HOLD_REGION", "args": {"region": "Gondor", "reward": 3}},
    {"id": "m03", "kind": "mission", "title": "Lightning War",
     "text": "Conquer 3 territories in a single turn.",
     "effect": "CONQUER_N_THIS_TURN", "args": {"n": 3, "reward": 2}},
    {"id": "m04", "kind": "mission", "title": "The Golden Wood",
     "text": "Bring a leader to Lothlórien.",
     "effect": "LEADER_REACH", "args": {"territory": "Lothlórien",
                                        "reward": 2}},
    {"id": "m05", "kind": "mission", "title": "Keeps of the West",
     "text": "Hold 2 strongholds at the start of your turn.",
     "effect": "HOLD_STRONGHOLDS", "args": {"n": 2, "reward": 2}},
    {"id": "m06", "kind": "mission", "title": "Watch on the Anduin",
     "text": "Hold Anduin Valley and Gladden Fields at the start of your turn.",
     "effect": "HOLD_TERRITORIES",
     "args": {"territories": ["Anduin Valley", "Gladden Fields"],
              "reward": 2}},
    {"id": "m07", "kind": "mission", "title": "The North Reborn",
     "text": "Control all of Arnor at the start of your turn.",
     "effect": "HOLD_REGION", "args": {"region": "Arnor", "reward": 3}},
    {"id": "m08", "kind": "mission", "title": "Doom Awaits",
     "text": "Bring a leader to Mount Doom.",
     "effect": "LEADER_REACH", "args": {"territory": "Mount Doom",
                                        "reward": 3}},
    {"id": "m09", "kind": "mission", "title": "Shadow of Mordor",
     "text": "Hold 3 strongholds at the start of your turn.",
     "effect": "HOLD_STRONGHOLDS", "args": {"n": 3, "reward": 4}},
    {"id": "m10", "kind": "mission", "title": "Wrath of the Ents",
     "text": "Conquer 4 territories in a single turn.",
     "effect": "CONQUER_N_THIS_TURN", "args": {"n": 4, "reward": 3}},
    {"id": "m11", "kind": "mission", "title": "Riders of the South",
     "text": "Control all of Haradwaith at the start of your turn.",
     "effect": "HOLD_REGION", "args": {"region": "Haradwaith", "reward": 2}},
    {"id": "m12", "kind": "mission", "title": "The Northern Watch",
     "text": "Hold Forodwaith and Angmar at the start of your turn.",
     "effect": "HOLD_TERRITORIES",
     "args": {"territories": ["Forodwaith", "Angmar"], "reward": 2}},
    # Events
    {"id": "e01", "kind": "event", "title": "Winter Storms",
     "text": "Each player loses 1 army in every Rhovanion territory they hold.",
     "effect": "REGION_ATTRITION", "args": {"region": "Rhovanion",
                                            "loss": 1}},
    {"id": "e02", "kind": "event", "title": "Fell Winter",
     "text": "Each player loses 1 army in every Eriador territory they hold.",
     "effect": "REGION_ATTRITION", "args": {"region": "Eriador", "loss": 1}},
    {"id": "e03", "kind": "event", "title": "The Mustering of Middle-earth",
     "text": "Each player adds 2 armies to a stronghold they control.",
     "effect": "REINFORCE_ALL", "args": {"n": 2}},
    {"id": "e04", "kind": "event", "title": "Call to Arms",
     "text": "Each player adds 1 army to a stronghold they control.",
     "effect": "REINFORCE_ALL", "args": {"n": 1}},
    {"id": "e05", "kind": "event", "title": "The Black Breath",
     "text": "Discard a territory card or lose a leader.",
     "effect": "LOSE_LEADER_UNLESS", "args": {}},
    {"id": "e06", "kind": "event", "title": "Ambush in the Wild",
     "text": "Discard a territory card or lose a leader.",
     "effect": "LOSE_LEADER_UNLESS", "args": {}},
    {"id": "e07", "kind": "event", "title": "Corsairs of Umbar",
     "text": "The Umbar-Belfalas sea route is closed until your next turn.",
     "effect": "SEA_CLOSED", "args": {"a": "Umbar", "b": "Belfalas"}},
    {"id": "e08", "kind": "event", "title": "Black Sails",
     "text": "The Mithlond-Minhiriath sea route is closed until your next turn.",
     "effect": "SEA_CLOSED", "args": {"a": "Mithlond", "b": "Minhiriath"}},
    {"id": "e09", "kind": "event", "title": "The Shadow Stirs",
     "text": "Each player loses 1 army in every Mordor territory they hold.",
     "effect": "REGION_ATTRITION", "args": {"region": "Mordor", "loss": 1}},
    {"id": "e10", "kind": "event", "title": "Aragorn Arrives",
     "text": "Each player adds 2 armies to a stronghold they control.",
     "effect": "REINFORCE_ALL", "args": {"n": 2}},
    {"id": "e11", "kind": "event", "title": "Grima Wormtongue",
     "text": "Discard a territory card or lose a leader.",
     "effect": "LOSE_LEADER_UNLESS", "args": {}},
    {"id": "e12", "kind": "event", "title": "Violent Storms",
     "text": "Each player loses 1 army in every Haradwaith territory they hold.",
     "effect": "REGION_ATTRITION", "args": {"region": "Haradwaith",
                                            "loss": 1}},
    # Powers
    {"id": "p01", "kind": "power", "title": "You Shall Not Pass",
     "text": "Add 1 to one attack die in a battle.",
     "effect": "ATTACK_BONUS", "args": {"n": 1}},
    {"id": "p02", "kind": "power", "title": "Courage Alone Will Not Save You",
     "text": "Add 1 to one attack die in a battle.",
     "effect": "ATTACK_BONUS", "args": {"n": 1}},
    {"id": "p03", "kind": "power", "title": "The Black Gates Open",
     "text": "Add 1 to your highest defence die.",
     "effect": "DEFENSE_BONUS", "args": {"n": 1}},
    {"id": "p04", "kind": "power", "title": "They've Brought a Cave Troll",
     "text": "Add 2 to your highest defence die.",
     "effect": "DEFENSE_BONUS", "args": {"n": 2}},
    {"id": "p05", "kind": "power", "title": "Borne to Another Place",
     "text": "Re-roll your dice once in a battle just rolled.",
     "effect": "REROLL", "args": {}},
    {"id": "p06", "kind": "power", "title": "The Hunt for the Ringbearer",
     "text": "Re-roll your dice once in a battle just rolled.",
     "effect": "REROLL", "args": {}},
    {"id": "p07", "kind": "power", "title": "The Entmoot",
     "text": "Take 3 extra armies this reinforcement phase.",
     "effect": "EXTRA_REINFORCE", "args": {"n": 3}},
    {"id": "p08", "kind": "power", "title": "Siege Machines",
     "text": "Take 2 extra armies this reinforcement phase.",
     "effect": "EXTRA_REINFORCE", "args": {"n": 2}},
    {"id": "p09", "kind": "power", "title": "Move by Night",
     "text": "Make a second fortify move this turn.",
     "effect": "FREE_FORTIFY", "args": {}},
    {"id": "p10", "kind": "power", "title": "Appoint a Second Leader",
     "text": "Place a leader for free.",
     "effect": "RECRUIT_LEADER", "args": {}},
    {"id": "p11", "kind": "power", "title": "The Enemy Is at Hand",
     "text": "Negate an event just drawn by any player.",
     "effect": "CANCEL_EVENT", "args": {}},
    {"id": "p12", "kind": "power", "title": "The Way Is Under the Mountains",
     "text": "Add 1 to one attack die in a battle.",
     "effect": "ATTACK_BONUS", "args": {"n": 1}},
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

    return {
        "name": "The Lord of the Rings (Trilogy Edition)",
        "version": 1,
        "image": {"file": "board.svg", "width": WIDTH, "height": HEIGHT},
        "factions": factions,
        "regions": regions,
        "territories": territories,
        "links": links,
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
