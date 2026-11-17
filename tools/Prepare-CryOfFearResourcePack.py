# SPDX-License-Identifier: GPL-3.0-or-later
"""Plan verified first-pistol and shot-decal namespaced resources (never deploy)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
HEADER = ROOT / 'native/guest/cof/resource_catalog.hpp'
PINS = [
    {
        "group": "P345Models",
        "original": "models/weapons/p345/v_p345.mdl",
        "host": "models/guest/cof/p345/v_p345.mdl",
        "bytes": 1737724,
        "sha256": "d3ca4e13236be0b84759641454b85439e278429ca4f4ee8b09e4c853b6fcf85b"
    },
    {
        "group": "P345Models",
        "original": "models/weapons/p345/w_p345.mdl",
        "host": "models/guest/cof/p345/w_p345.mdl",
        "bytes": 74364,
        "sha256": "3bd9d2e7cac4e46cfb416bc8d94e00757991815aa07f8cf6629c7cfe09888200"
    },
    {
        "group": "P345Models",
        "original": "police/models/weapons/p345/p_p345.mdl",
        "host": "models/guest/cof/p345/p_p345.mdl",
        "bytes": 67896,
        "sha256": "c090798e8754084506c6859c268c4db08961c3b0d609afba6a14e50457626bb0"
    },
    {
        "group": "P345Models",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "P345Models",
        "original": "models/shell.mdl",
        "host": "models/guest/cof/common/shell.mdl",
        "bytes": 70344,
        "sha256": "5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"
    },
    {
        "group": "P345Models",
        "original": "models/w_9mmclip.mdl",
        "host": "models/guest/cof/common/w_9mmclip.mdl",
        "bytes": 1772,
        "sha256": "2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"
    },
    {
        "group": "P345Models",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "P345Models",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "P345Models",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "P345Models",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_click.wav",
        "host": "guest/cof/weapons/p345/p345_click.wav",
        "bytes": 7806,
        "sha256": "5b5f75a9a3b250e11712e125c34ef1ea8b7e5c1a5ffac8ff68a09d7d41e1ec47"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_magin.wav",
        "host": "guest/cof/weapons/p345/p345_magin.wav",
        "bytes": 43798,
        "sha256": "a840576031952505ada5fddc057cea09719636b987c32332716ec6a0f8ff7342"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_magout.wav",
        "host": "guest/cof/weapons/p345/p345_magout.wav",
        "bytes": 23500,
        "sha256": "218f1903824ac082cee893a8b5bc68c603efb07b613e8cf9a806d097a5abd7dc"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_magplace.wav",
        "host": "guest/cof/weapons/p345/p345_magplace.wav",
        "bytes": 9834,
        "sha256": "dd39c34e7e1650cc4d7ece6413dad329ad94f9df3cbcd1b60dcc4987f6f13642"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_rackback.wav",
        "host": "guest/cof/weapons/p345/p345_rackback.wav",
        "bytes": 11070,
        "sha256": "8dd8e1c16bfa31dc56cba1fc5b90a01fb14da90d2da04bb5d0e3fc8e840cbe65"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_slide.wav",
        "host": "guest/cof/weapons/p345/p345_slide.wav",
        "bytes": 11686,
        "sha256": "b9ed1a1cef6275382398e090d04d8d4e2040b9ef1c4141cd0a48ce7e05212dd4"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/p345/p345_fire.wav",
        "host": "guest/cof/weapons/p345/p345_fire.wav",
        "bytes": 44024,
        "sha256": "245eeffc3dd5349c46dd29deb80b699ce67862a17b017d6d756f100765ecf007"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "P345Sounds",
        "original": "player/police/Swing/Swing1.wav",
        "host": "guest/cof/player/police/Swing/Swing1.wav",
        "bytes": 46076,
        "sha256": "72ad87048f17225cc696be76ba4623bab4c169606300b134243bb207c9344248"
    },
    {
        "group": "P345Sounds",
        "original": "player/police/Swing/Swing2.wav",
        "host": "guest/cof/player/police/Swing/Swing2.wav",
        "bytes": 35178,
        "sha256": "23f01531592a61ef8cdd1f11f7c80f614666bbe795418f1eccc88e697be9eb5b"
    },
    {
        "group": "P345Sounds",
        "original": "player/police/Swing/Swing3.wav",
        "host": "guest/cof/player/police/Swing/Swing3.wav",
        "bytes": 44216,
        "sha256": "26ca79e9575ad9d90687eea53e75f4ee22a2eaae4e0e1422cda1769321e6f3d1"
    },
    {
        "group": "P345Sounds",
        "original": "player/police/Swing/Swing4.wav",
        "host": "guest/cof/player/police/Swing/Swing4.wav",
        "bytes": 37976,
        "sha256": "bd6317e251285827a705298591231a31729882305dff54321b64b0b70833aeb7"
    },
    {
        "group": "P345Sounds",
        "original": "player/police/Swing/Swing5.wav",
        "host": "guest/cof/player/police/Swing/Swing5.wav",
        "bytes": 23372,
        "sha256": "ae0085cd5a580303460770e2bb97eda506ccd87dde2019a129c90c896f03dd94"
    },
    {
        "group": "P345Sounds",
        "original": "player/police/Swing/Swing6.wav",
        "host": "guest/cof/player/police/Swing/Swing6.wav",
        "bytes": 28018,
        "sha256": "df8e230506ce0985f3038f18b009266bf5458624a84219f6185561353c7a118c"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/mobile/punch_swing.wav",
        "host": "guest/cof/weapons/mobile/punch_swing.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/mobile/punch_smack.wav",
        "host": "guest/cof/weapons/mobile/punch_smack.wav",
        "bytes": 7092,
        "sha256": "70164307237d414829ca9a7edbda804c5050e845f977688cb7cd25d6b1d83657"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "P345Sounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_click.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_click.wav",
        "bytes": 105764,
        "sha256": "8b5795f01e723a880753ca7ff697ce30cd4331a11bad4c9e79b79216bbd532a9"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_magin.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_magin.wav",
        "bytes": 290128,
        "sha256": "42ec705fd1df763aa5fa24edc6392cb6b759ee1bf1e8ca2f7ca44f1ab1049ee6"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_magout.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_magout.wav",
        "bytes": 97906,
        "sha256": "983040c34b276c567e8ec184d6b47bfd20c2f68c5889bebe33a4af0f3a804af2"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_magplace.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_magplace.wav",
        "bytes": 68322,
        "sha256": "0158f4cd2999ec347db7e66dd9537413ed75c8f447c2383d0ee32435d4798b9e"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_rackback.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_rackback.wav",
        "bytes": 15438,
        "sha256": "2d0af9bd17929edb1f617fb73275a2a909294d82a39370c3e0bb34e00efcf612"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_slide.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_slide.wav",
        "bytes": 156128,
        "sha256": "ebeb9241bb1aa3e69721f24b01b527f53fd8f492aca99583d601c6f4a84abc60"
    },
    {
        "group": "P345LocalWaves",
        "original": "weapons_new/p345/p345_fire.wav",
        "host": "sound/guest/cof/weapons_new/p345/p345_fire.wav",
        "bytes": 574454,
        "sha256": "b10cdc51dca8fdde716cc2178b60ad24a5d93c70e5bf7531ac077f87651f0428"
    },
    {
        "group": "P345Events",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "P345Events",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "P345Inventory",
        "original": "inventoryitems/weapons/weapon_p345.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_p345.txt",
        "bytes": 461,
        "sha256": "376a05dad38387981be4a7f88a1a6444582f5781afd209df9a3d5a7ea18bb2fe"
    },
    {
        "group": "P345Particle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]  # Filled from the accepted resource05 receipt, never from a candidate.
# Original client10059110 ->10069630 consumes these exact five RGB24 files.
# IDs/hashes were verified before the custom-decal implementation, not inferred
# from a candidate texture. Keep this group independently selectable for tests.
# Audited original assets for the Glock namespace; shared bytes retain the same pins.
GLOCK_PINS = [
    {
        "group": "GlockModels",
        "original": "models/weapons/glock/v_glock.mdl",
        "host": "models/guest/cof/glock/v_glock.mdl",
        "bytes": 1489976,
        "sha256": "80cc917866e61fbb23337f6a990dc72f42277f2bd3d5a1d77201d1d57a406ffe"
    },
    {
        "group": "GlockModels",
        "original": "models/weapons/glock/w_glock.mdl",
        "host": "models/guest/cof/glock/w_glock.mdl",
        "bytes": 86044,
        "sha256": "4c13a134a6899dd5e50342bb1e4b2f3e3a5f92bdf1c054cc1197c3bd8ee2e766"
    },
    {
        "group": "GlockModels",
        "original": "police/models/weapons/glock/p_glock.mdl",
        "host": "models/guest/cof/glock/p_glock.mdl",
        "bytes": 87616,
        "sha256": "ac158045589ac76fcff9f99ce057872f8037ccdcb8f77acb6314cafca65e9d49"
    },
    {
        "group": "GlockModels",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "GlockModels",
        "original": "models/shell.mdl",
        "host": "models/guest/cof/common/shell.mdl",
        "bytes": 70344,
        "sha256": "5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"
    },
    {
        "group": "GlockModels",
        "original": "models/w_9mmclip.mdl",
        "host": "models/guest/cof/common/w_9mmclip.mdl",
        "bytes": 1772,
        "sha256": "2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"
    },
    {
        "group": "GlockModels",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "GlockModels",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "GlockModels",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "GlockModels",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_switch.wav",
        "host": "guest/cof/weapons/glock/glock_switch.wav",
        "bytes": 26434,
        "sha256": "d002295c862315e75542063c750d165a05fbba79b0ebdac2902b2f02ed395d85"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_magin.wav",
        "host": "guest/cof/weapons/glock/glock_magin.wav",
        "bytes": 9442,
        "sha256": "8df0be58e73eefd7d4e1110243dd03f7d17ffe9034e89357f4137587b883e697"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_magout.wav",
        "host": "guest/cof/weapons/glock/glock_magout.wav",
        "bytes": 16230,
        "sha256": "caefab0cf197e18da146cc6e9a0d0f73b697ca7b1064937886ff4ef184ab1955"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_magplace.wav",
        "host": "guest/cof/weapons/glock/glock_magplace.wav",
        "bytes": 15846,
        "sha256": "a384fefb4ee5b615aab437dd5ca5f1e0edc83e4e9ed964f2ae1eca84a9888a9c"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_rackback.wav",
        "host": "guest/cof/weapons/glock/glock_rackback.wav",
        "bytes": 8638,
        "sha256": "8388145848f88579794f8d33d099cb960445c4787582440ec0cd35496106c092"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_slide.wav",
        "host": "guest/cof/weapons/glock/glock_slide.wav",
        "bytes": 18742,
        "sha256": "e0000e541bcb51944d99e886f27df6bf3989acf967b0f8d2d89ed8cdf0ea2e84"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/glock/glock_fire.wav",
        "host": "guest/cof/weapons/glock/glock_fire.wav",
        "bytes": 45290,
        "sha256": "890e37b88b48e2f459d286688a6e0b50d99f958983d4edb12492118bc6f475b4"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/rifle/rifle_insert.wav",
        "host": "guest/cof/weapons/rifle/rifle_insert.wav",
        "bytes": 22084,
        "sha256": "b8c23fab1deaa2ad3782b1c9cc4039b8d1d5ea457f6415a338c7638ada77d550"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "GlockSounds",
        "original": "player/police/Swing/Swing1.wav",
        "host": "guest/cof/player/police/Swing/Swing1.wav",
        "bytes": 46076,
        "sha256": "72ad87048f17225cc696be76ba4623bab4c169606300b134243bb207c9344248"
    },
    {
        "group": "GlockSounds",
        "original": "player/police/Swing/Swing2.wav",
        "host": "guest/cof/player/police/Swing/Swing2.wav",
        "bytes": 35178,
        "sha256": "23f01531592a61ef8cdd1f11f7c80f614666bbe795418f1eccc88e697be9eb5b"
    },
    {
        "group": "GlockSounds",
        "original": "player/police/Swing/Swing3.wav",
        "host": "guest/cof/player/police/Swing/Swing3.wav",
        "bytes": 44216,
        "sha256": "26ca79e9575ad9d90687eea53e75f4ee22a2eaae4e0e1422cda1769321e6f3d1"
    },
    {
        "group": "GlockSounds",
        "original": "player/police/Swing/Swing4.wav",
        "host": "guest/cof/player/police/Swing/Swing4.wav",
        "bytes": 37976,
        "sha256": "bd6317e251285827a705298591231a31729882305dff54321b64b0b70833aeb7"
    },
    {
        "group": "GlockSounds",
        "original": "player/police/Swing/Swing5.wav",
        "host": "guest/cof/player/police/Swing/Swing5.wav",
        "bytes": 23372,
        "sha256": "ae0085cd5a580303460770e2bb97eda506ccd87dde2019a129c90c896f03dd94"
    },
    {
        "group": "GlockSounds",
        "original": "player/police/Swing/Swing6.wav",
        "host": "guest/cof/player/police/Swing/Swing6.wav",
        "bytes": 28018,
        "sha256": "df8e230506ce0985f3038f18b009266bf5458624a84219f6185561353c7a118c"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/mobile/punch_swing.wav",
        "host": "guest/cof/weapons/mobile/punch_swing.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/mobile/punch_smack.wav",
        "host": "guest/cof/weapons/mobile/punch_smack.wav",
        "bytes": 7092,
        "sha256": "70164307237d414829ca9a7edbda804c5050e845f977688cb7cd25d6b1d83657"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "GlockSounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_switch.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_switch.wav",
        "bytes": 176736,
        "sha256": "48d7d2cdf962fcb5dfee268af7f4bef46d00f084ea818664d25e65c078ea8d97"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_magin.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_magin.wav",
        "bytes": 126812,
        "sha256": "37288e494487664217faa403f88312941e58735a3a34165e520c6914d72c2c43"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_magout.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_magout.wav",
        "bytes": 68826,
        "sha256": "f7509ff95166bd626a9a0ded256d3350e6d0573127192b849b977a7d6fc86b7d"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_magplace.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_magplace.wav",
        "bytes": 107592,
        "sha256": "e1e20819fc6fcf3d0bc3cdc10826efaf960a51f543130b61c1df22fcecf759ec"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_rackback.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_rackback.wav",
        "bytes": 60520,
        "sha256": "49b75747d04e6e5a38c91fff93b0ee942d34943358e6b82ac10827ab15c868ae"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_slide.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_slide.wav",
        "bytes": 126496,
        "sha256": "93be99d0687b8e589b0d3cc1cbc3efe6a9853dd2693ef66cfa2e383d96ae26ed"
    },
    {
        "group": "GlockLocalWaves",
        "original": "weapons_new/glock/glock_fire.wav",
        "host": "sound/guest/cof/weapons_new/glock/glock_fire.wav",
        "bytes": 921626,
        "sha256": "5e475b56ae3c41a512a9e75be00248aa9c5714a5e7472cefcb9f4a15ff3880c9"
    },
    {
        "group": "GlockEvents",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "GlockEvents",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "GlockInventory",
        "original": "inventoryitems/weapons/weapon_glock.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_glock.txt",
        "bytes": 645,
        "sha256": "6feaf3751ef40181e87f69912af7b817b9a1b93c2143aa7281276f2007b424d8"
    },
    {
        "group": "GlockParticle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]

REVOLVER_PINS = [
    {
        "group": "RevolverModels",
        "original": "models/weapons/revolver/v_revolver.mdl",
        "host": "models/guest/cof/revolver/v_revolver.mdl",
        "bytes": 1685284,
        "sha256": "27ea82a1bf22e9538a57b1f71180b0d58685b02ed8245420400951051af6fe1b"
    },
    {
        "group": "RevolverModels",
        "original": "models/weapons/revolver/w_revolver.mdl",
        "host": "models/guest/cof/revolver/w_revolver.mdl",
        "bytes": 63600,
        "sha256": "31b7463ccdebfcf40055a174314ffcae1383ed98128a616fa249dd6f95149bba"
    },
    {
        "group": "RevolverModels",
        "original": "police/models/weapons/revolver/p_revolver.mdl",
        "host": "models/guest/cof/revolver/p_revolver.mdl",
        "bytes": 65248,
        "sha256": "2a54b7b911ed2e5145947eed39779db5ec83b6707701eeaf9826cb73f8f69312"
    },
    {
        "group": "RevolverModels",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "RevolverModels",
        "original": "models/shell.mdl",
        "host": "models/guest/cof/common/shell.mdl",
        "bytes": 70344,
        "sha256": "5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"
    },
    {
        "group": "RevolverModels",
        "original": "models/w_9mmclip.mdl",
        "host": "models/guest/cof/common/w_9mmclip.mdl",
        "bytes": 1772,
        "sha256": "2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"
    },
    {
        "group": "RevolverModels",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "RevolverModels",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "RevolverModels",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "RevolverModels",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/revolver/revolver_fire.wav",
        "host": "guest/cof/weapons/revolver/revolver_fire.wav",
        "bytes": 51550,
        "sha256": "f99ebc8d5d3f54a9b4ab8a7a1bb6223719222e90a675f33d7e2770371f9b7bc8"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/revolver/revolver_shell.wav",
        "host": "guest/cof/weapons/revolver/revolver_shell.wav",
        "bytes": 45818,
        "sha256": "e821d70cc8328b800b892bba21e8d03c69202bf2ca9dac87c5ed5846657fcd44"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/revolver/revolver_cylinder_open.wav",
        "host": "guest/cof/weapons/revolver/revolver_cylinder_open.wav",
        "bytes": 58734,
        "sha256": "0be13a639f9a57db70170cfc906c2ecdf4dd4a20fc3b280162620f0fed29827b"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/revolver/revolver_insert.wav",
        "host": "guest/cof/weapons/revolver/revolver_insert.wav",
        "bytes": 16246,
        "sha256": "9a342344b9c2b65ebf7cca2c40a935d2d87c90139f5078df371e1b8a3d4cfa86"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/revolver/revolver_cylinder_close.wav",
        "host": "guest/cof/weapons/revolver/revolver_cylinder_close.wav",
        "bytes": 14524,
        "sha256": "401b90081bf64c50bcf8da8b65a89bb8dcdd3f8b0e4114404f8d59b1ff99ae9c"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "RevolverSounds",
        "original": "player/police/Swing/Swing1.wav",
        "host": "guest/cof/player/police/Swing/Swing1.wav",
        "bytes": 46076,
        "sha256": "72ad87048f17225cc696be76ba4623bab4c169606300b134243bb207c9344248"
    },
    {
        "group": "RevolverSounds",
        "original": "player/police/Swing/Swing2.wav",
        "host": "guest/cof/player/police/Swing/Swing2.wav",
        "bytes": 35178,
        "sha256": "23f01531592a61ef8cdd1f11f7c80f614666bbe795418f1eccc88e697be9eb5b"
    },
    {
        "group": "RevolverSounds",
        "original": "player/police/Swing/Swing3.wav",
        "host": "guest/cof/player/police/Swing/Swing3.wav",
        "bytes": 44216,
        "sha256": "26ca79e9575ad9d90687eea53e75f4ee22a2eaae4e0e1422cda1769321e6f3d1"
    },
    {
        "group": "RevolverSounds",
        "original": "player/police/Swing/Swing4.wav",
        "host": "guest/cof/player/police/Swing/Swing4.wav",
        "bytes": 37976,
        "sha256": "bd6317e251285827a705298591231a31729882305dff54321b64b0b70833aeb7"
    },
    {
        "group": "RevolverSounds",
        "original": "player/police/Swing/Swing5.wav",
        "host": "guest/cof/player/police/Swing/Swing5.wav",
        "bytes": 23372,
        "sha256": "ae0085cd5a580303460770e2bb97eda506ccd87dde2019a129c90c896f03dd94"
    },
    {
        "group": "RevolverSounds",
        "original": "player/police/Swing/Swing6.wav",
        "host": "guest/cof/player/police/Swing/Swing6.wav",
        "bytes": 28018,
        "sha256": "df8e230506ce0985f3038f18b009266bf5458624a84219f6185561353c7a118c"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/mobile/punch_swing.wav",
        "host": "guest/cof/weapons/mobile/punch_swing.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/mobile/punch_smack.wav",
        "host": "guest/cof/weapons/mobile/punch_smack.wav",
        "bytes": 7092,
        "sha256": "70164307237d414829ca9a7edbda804c5050e845f977688cb7cd25d6b1d83657"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "RevolverSounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "RevolverLocalWaves",
        "original": "weapons_new/revolver/revolver_fire.wav",
        "host": "sound/guest/cof/weapons_new/revolver/fire.wav",
        "bytes": 677120,
        "sha256": "45252fd6afb51f75b7db0020c82652754da38e833ddb9bc9484c3cdccb4d685a"
    },
    {
        "group": "RevolverLocalWaves",
        "original": "weapons_new/revolver/revolver_cylinder_open.wav",
        "host": "sound/guest/cof/weapons_new/revolver/cylinder_open.wav",
        "bytes": 132156,
        "sha256": "a2d87c7265b23bbc41af831e9b7a54dbdec3702795f5277fe8bba3fa97b09200"
    },
    {
        "group": "RevolverLocalWaves",
        "original": "weapons_new/revolver/revolver_insert.wav",
        "host": "sound/guest/cof/weapons_new/revolver/insert.wav",
        "bytes": 69204,
        "sha256": "dca9543460912c9f1f14f6f25403b4d46cac555b3ac612402d51d423126993a1"
    },
    {
        "group": "RevolverLocalWaves",
        "original": "weapons_new/revolver/revolver_cylinder_close.wav",
        "host": "sound/guest/cof/weapons_new/revolver/cylinder_close.wav",
        "bytes": 193502,
        "sha256": "cb77b3782f2ae88563eeff0f73f6283dc6fa0761ce0ff37fd170a6432914e1cd"
    },
    {
        "group": "RevolverEvents",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "RevolverEvents",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "RevolverEvents",
        "original": "events/revolvershells.sc",
        "host": "events/guest/cof/revolvershells.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "RevolverInventory",
        "original": "inventoryitems/weapons/weapon_revolver.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_revolver.txt",
        "bytes": 496,
        "sha256": "584ecbd431f86ee5d1d385cc63c4116109d028c4814b7c7436d85c539396ae4b"
    },
    {
        "group": "RevolverParticle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]

VP70_PINS = [
    {
        "group": "VP70Models",
        "original": "models/weapons/vp70/v_vp70.mdl",
        "host": "models/guest/cof/vp70/v_vp70.mdl",
        "bytes": 2001764,
        "sha256": "30af770987dc0430ff48bd6631d955e5e3b63c9dc486e02ef707b27fb4370a77"
    },
    {
        "group": "VP70Models",
        "original": "models/weapons/vp70/w_vp70.mdl",
        "host": "models/guest/cof/vp70/w_vp70.mdl",
        "bytes": 106820,
        "sha256": "23a98bcbdfc126e52c135995193f154c06319d3eae5948e5e07691843b671492"
    },
    {
        "group": "VP70Models",
        "original": "police/models/weapons/vp70/p_vp70.mdl",
        "host": "models/guest/cof/vp70/p_vp70.mdl",
        "bytes": 108216,
        "sha256": "a4412beb27cbd51478651b87290bb1bff66133478229c56c5879136c5cab93e8"
    },
    {
        "group": "VP70Models",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "VP70Models",
        "original": "models/shell.mdl",
        "host": "models/guest/cof/common/shell.mdl",
        "bytes": 70344,
        "sha256": "5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"
    },
    {
        "group": "VP70Models",
        "original": "models/w_9mmclip.mdl",
        "host": "models/guest/cof/common/w_9mmclip.mdl",
        "bytes": 1772,
        "sha256": "2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"
    },
    {
        "group": "VP70Models",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "VP70Models",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "VP70Models",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "VP70Models",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/vp70/vp70_cock.wav",
        "host": "guest/cof/weapons/vp70/vp70_cock.wav",
        "bytes": 27562,
        "sha256": "a6fcc8d0dff72a10add67ce457d77fb80559c1a712f659410139723c7d810bc5"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/vp70/vp70_magin.wav",
        "host": "guest/cof/weapons/vp70/vp70_magin.wav",
        "bytes": 33118,
        "sha256": "f032f755db3df539d5f66dea7d8453a31e6cd529e7bdacc44e04b53b9be5b031"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/vp70/vp70_magout.wav",
        "host": "guest/cof/weapons/vp70/vp70_magout.wav",
        "bytes": 19744,
        "sha256": "212d76fd2f62d15778747b8e99723e7691c47aeb037ded97ddf3ec1f7bf76b7b"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/vp70/vp70_draw_first_seq.wav",
        "host": "guest/cof/weapons/vp70/vp70_draw_first_seq.wav",
        "bytes": 168900,
        "sha256": "7bb4310c9f3bd21ac70f5d7dea9ec8c140b20ed3f08aa845cc52dc4cbe00a9ed"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/vp70/vp70_fire.wav",
        "host": "guest/cof/weapons/vp70/vp70_fire.wav",
        "bytes": 55168,
        "sha256": "ef5dac6c5e6725d64f2791a584749650030118c8200712846c5f9e03e5f88ae1"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "VP70Sounds",
        "original": "player/police/Swing/Swing1.wav",
        "host": "guest/cof/player/police/Swing/Swing1.wav",
        "bytes": 46076,
        "sha256": "72ad87048f17225cc696be76ba4623bab4c169606300b134243bb207c9344248"
    },
    {
        "group": "VP70Sounds",
        "original": "player/police/Swing/Swing2.wav",
        "host": "guest/cof/player/police/Swing/Swing2.wav",
        "bytes": 35178,
        "sha256": "23f01531592a61ef8cdd1f11f7c80f614666bbe795418f1eccc88e697be9eb5b"
    },
    {
        "group": "VP70Sounds",
        "original": "player/police/Swing/Swing3.wav",
        "host": "guest/cof/player/police/Swing/Swing3.wav",
        "bytes": 44216,
        "sha256": "26ca79e9575ad9d90687eea53e75f4ee22a2eaae4e0e1422cda1769321e6f3d1"
    },
    {
        "group": "VP70Sounds",
        "original": "player/police/Swing/Swing4.wav",
        "host": "guest/cof/player/police/Swing/Swing4.wav",
        "bytes": 37976,
        "sha256": "bd6317e251285827a705298591231a31729882305dff54321b64b0b70833aeb7"
    },
    {
        "group": "VP70Sounds",
        "original": "player/police/Swing/Swing5.wav",
        "host": "guest/cof/player/police/Swing/Swing5.wav",
        "bytes": 23372,
        "sha256": "ae0085cd5a580303460770e2bb97eda506ccd87dde2019a129c90c896f03dd94"
    },
    {
        "group": "VP70Sounds",
        "original": "player/police/Swing/Swing6.wav",
        "host": "guest/cof/player/police/Swing/Swing6.wav",
        "bytes": 28018,
        "sha256": "df8e230506ce0985f3038f18b009266bf5458624a84219f6185561353c7a118c"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/mobile/punch_swing.wav",
        "host": "guest/cof/weapons/mobile/punch_swing.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/mobile/punch_smack.wav",
        "host": "guest/cof/weapons/mobile/punch_smack.wav",
        "bytes": 7092,
        "sha256": "70164307237d414829ca9a7edbda804c5050e845f977688cb7cd25d6b1d83657"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "VP70Sounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "VP70LocalWaves",
        "original": "weapons_new/vp70/vp70_cock.wav",
        "host": "sound/guest/cof/weapons_new/vp70/vp70_cock.wav",
        "bytes": 27574,
        "sha256": "7944b710b1f4927bcc910d16bd9c90744beb8592050b34f7c5fedd610f310e64"
    },
    {
        "group": "VP70LocalWaves",
        "original": "weapons_new/vp70/vp70_magin.wav",
        "host": "sound/guest/cof/weapons_new/vp70/vp70_magin.wav",
        "bytes": 33118,
        "sha256": "f032f755db3df539d5f66dea7d8453a31e6cd529e7bdacc44e04b53b9be5b031"
    },
    {
        "group": "VP70LocalWaves",
        "original": "weapons_new/vp70/vp70_magout.wav",
        "host": "sound/guest/cof/weapons_new/vp70/vp70_magout.wav",
        "bytes": 19756,
        "sha256": "003c24dfe3427745568036dbbc65bcb6d4077021b68cc152e038b9a2f57c3195"
    },
    {
        "group": "VP70LocalWaves",
        "original": "weapons_new/vp70/vp70_draw_first_seq.wav",
        "host": "sound/guest/cof/weapons_new/vp70/vp70_draw_first_seq.wav",
        "bytes": 168900,
        "sha256": "7bb4310c9f3bd21ac70f5d7dea9ec8c140b20ed3f08aa845cc52dc4cbe00a9ed"
    },
    {
        "group": "VP70LocalWaves",
        "original": "weapons_new/vp70/vp70_fire.wav",
        "host": "sound/guest/cof/weapons_new/vp70/vp70_fire.wav",
        "bytes": 55180,
        "sha256": "1ca6725f02105abeb10c8de26707241eea09475b9d3e8419fb33facfe0c6173a"
    },
    {
        "group": "VP70Events",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "VP70Events",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "VP70Inventory",
        "original": "inventoryitems/weapons/weapon_vp70.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_vp70.txt",
        "bytes": 459,
        "sha256": "96b54688cd5733995a31fff1d2d49569f21611272a56ed8cfb1933f743165846"
    },
    {
        "group": "VP70Particle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]

TMP_PINS = [
    {"group":"TMPModels","original":"models/weapons/tmp/v_tmp.mdl","host":"models/guest/cof/tmp/v_tmp.mdl","bytes":1545688,"sha256":"66a2cf7a084ccbcb7ab5b1c0dfa599b302e750f6599be204c092ae075af6c7a4"},
    {"group":"TMPModels","original":"models/weapons/tmp/w_tmp.mdl","host":"models/guest/cof/tmp/w_tmp.mdl","bytes":86164,"sha256":"ac48ff7c2ec3a695393ff339fb09612c665a87bf63a378312837ed1f7f8ea1a5"},
    {"group":"TMPModels","original":"police/models/weapons/tmp/p_tmp.mdl","host":"models/guest/cof/tmp/p_tmp.mdl","bytes":58164,"sha256":"ad599b80f9e9d4e37cbb0f981d218a7741f414bfa7d3ddfd4be86368273da87f"},
    {"group":"TMPModels","original":"models/null.mdl","host":"models/guest/cof/common/null.mdl","bytes":1868,"sha256":"97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"},
    {"group":"TMPModels","original":"models/shell.mdl","host":"models/guest/cof/common/shell.mdl","bytes":70344,"sha256":"5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"},
    {"group":"TMPModels","original":"models/w_9mmclip.mdl","host":"models/guest/cof/common/w_9mmclip.mdl","bytes":1772,"sha256":"2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"},
    {"group":"TMPModels","original":"sprites/bubble.spr","host":"sprites/guest/cof/bubble.spr","bytes":1086,"sha256":"377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"},
    {"group":"TMPModels","original":"sprites/fog2.spr","host":"sprites/guest/cof/fog2.spr","bytes":4926,"sha256":"6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"},
    {"group":"TMPModels","original":"models/flash1.mdl","host":"models/guest/cof/common/flash1.mdl","bytes":133896,"sha256":"3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"},
    {"group":"TMPModels","original":"sprites/gunsmoke.spr","host":"sprites/guest/cof/gunsmoke.spr","bytes":214062,"sha256":"f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_charger_back.wav","host":"guest/cof/weapons/tmp/tmp_charger_back.wav","bytes":14132,"sha256":"dd84d09f53f464a9ebfba13960bc716dcb5f9133439586b9fe700f757933a938"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_charger_forward.wav","host":"guest/cof/weapons/tmp/tmp_charger_forward.wav","bytes":27568,"sha256":"9659a01be227b6ed8ced116e24022d56d7acbfad870814bcd1468d464111df6f"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_magin.wav","host":"guest/cof/weapons/tmp/tmp_magin.wav","bytes":29310,"sha256":"e82fbf7a3daa4f598755332199cacb975421579ddc8bca4ddbea2468d37eda86"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_magout.wav","host":"guest/cof/weapons/tmp/tmp_magout.wav","bytes":28338,"sha256":"d85a01d6922558dabb21b2858f7f2ee6422f239ba8726eeb50cb7db1d90ffc87"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_stock.wav","host":"guest/cof/weapons/tmp/tmp_stock.wav","bytes":24944,"sha256":"8f44a727c41b46e3546625add8a49889fc3bd33dd19924be8818dcb65133c0d2"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_shoot_loop.wav","host":"guest/cof/weapons/tmp/tmp_shoot_loop.wav","bytes":87364,"sha256":"e1578c915bd81db4c40ee942b5a7671166be3fcc4caa7378b1ad0c2e1a3549db"},
    {"group":"TMPSounds","original":"weapons/tmp/tmp_shoot_end.wav","host":"guest/cof/weapons/tmp/tmp_shoot_end.wav","bytes":45792,"sha256":"56c53c3d581ee89f379cbd71a2fb28b98cb30d5ac71696376cf68a5766ab330b"},
    {"group":"TMPSounds","original":"weapons/item_get.wav","host":"guest/cof/weapons/item_get.wav","bytes":22110,"sha256":"e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"},
    {"group":"TMPSounds","original":"weapons/weapon_get.wav","host":"guest/cof/weapons/weapon_get.wav","bytes":21560,"sha256":"35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"},
    {"group":"TMPSounds","original":"weapons/sleeve_generic1.wav","host":"guest/cof/weapons/sleeve_generic1.wav","bytes":24268,"sha256":"4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"},
    {"group":"TMPSounds","original":"weapons/sleeve_generic2.wav","host":"guest/cof/weapons/sleeve_generic2.wav","bytes":33128,"sha256":"0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"},
    {"group":"TMPSounds","original":"weapons/sleeve_generic3.wav","host":"guest/cof/weapons/sleeve_generic3.wav","bytes":17450,"sha256":"76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"},
    {"group":"TMPSounds","original":"player/police/Swing/Swing1.wav","host":"guest/cof/player/police/Swing/Swing1.wav","bytes":46076,"sha256":"72ad87048f17225cc696be76ba4623bab4c169606300b134243bb207c9344248"},
    {"group":"TMPSounds","original":"player/police/Swing/Swing2.wav","host":"guest/cof/player/police/Swing/Swing2.wav","bytes":35178,"sha256":"23f01531592a61ef8cdd1f11f7c80f614666bbe795418f1eccc88e697be9eb5b"},
    {"group":"TMPSounds","original":"player/police/Swing/Swing3.wav","host":"guest/cof/player/police/Swing/Swing3.wav","bytes":44216,"sha256":"26ca79e9575ad9d90687eea53e75f4ee22a2eaae4e0e1422cda1769321e6f3d1"},
    {"group":"TMPSounds","original":"player/police/Swing/Swing4.wav","host":"guest/cof/player/police/Swing/Swing4.wav","bytes":37976,"sha256":"bd6317e251285827a705298591231a31729882305dff54321b64b0b70833aeb7"},
    {"group":"TMPSounds","original":"player/police/Swing/Swing5.wav","host":"guest/cof/player/police/Swing/Swing5.wav","bytes":23372,"sha256":"ae0085cd5a580303460770e2bb97eda506ccd87dde2019a129c90c896f03dd94"},
    {"group":"TMPSounds","original":"player/police/Swing/Swing6.wav","host":"guest/cof/player/police/Swing/Swing6.wav","bytes":28018,"sha256":"df8e230506ce0985f3038f18b009266bf5458624a84219f6185561353c7a118c"},
    {"group":"TMPSounds","original":"weapons/mobile/punch_swing.wav","host":"guest/cof/weapons/mobile/punch_swing.wav","bytes":17450,"sha256":"76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"},
    {"group":"TMPSounds","original":"weapons/mobile/punch_smack.wav","host":"guest/cof/weapons/mobile/punch_smack.wav","bytes":7092,"sha256":"70164307237d414829ca9a7edbda804c5050e845f977688cb7cd25d6b1d83657"},
    {"group":"TMPSounds","original":"weapons/weapon_fire_empty.wav","host":"guest/cof/weapons/weapon_fire_empty.wav","bytes":22340,"sha256":"b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"},
    {"group":"TMPSounds","original":"weapons/weapon_fire_empty_noclick.wav","host":"guest/cof/weapons/weapon_fire_empty_noclick.wav","bytes":18228,"sha256":"de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"},
    {"group":"TMPLocalWaves","original":"weapons_new/tmp/tmp_charger_back.wav","host":"sound/guest/cof/weapons_new/tmp/tmp_charger_back.wav","bytes":62264,"sha256":"e5289f7ee065c2dbc01c8c0b312872c19aa9598a34ec9b202b358bc951ad7b0a"},
    {"group":"TMPLocalWaves","original":"weapons_new/tmp/tmp_charger_forward.wav","host":"sound/guest/cof/weapons_new/tmp/tmp_charger_forward.wav","bytes":114932,"sha256":"cfa64cbacc520bb2c735e3c2d44a27258a9c5eee2bf445af49b8c2200272856c"},
    {"group":"TMPLocalWaves","original":"weapons_new/tmp/tmp_magin.wav","host":"sound/guest/cof/weapons_new/tmp/tmp_magin.wav","bytes":195520,"sha256":"f5eabdab0be2f39dbfa50191f790d75d21f7feafcedcdf8dc717c0f376a3751c"},
    {"group":"TMPLocalWaves","original":"weapons_new/tmp/tmp_magout.wav","host":"sound/guest/cof/weapons_new/tmp/tmp_magout.wav","bytes":189168,"sha256":"703d679b6bf1cb6dfde25c8bce3618c4edea1f0dcc8b29ae6d29feef4346e136"},
    {"group":"TMPLocalWaves","original":"weapons_new/tmp/tmp_stock.wav","host":"sound/guest/cof/weapons_new/tmp/tmp_stock.wav","bytes":167002,"sha256":"46d73bd1923a02fe9112e00c795c2c6ad581a0b716d4e5cf6d3917e8c8172ea7"},
    {"group":"TMPEvents","original":"events/muzzleflash.sc","host":"events/guest/cof/muzzleflash.sc","bytes":1778,"sha256":"2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"},
    {"group":"TMPEvents","original":"events/viewpunch.sc","host":"events/guest/cof/viewpunch.sc","bytes":1778,"sha256":"2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"},
    {"group":"TMPInventory","original":"inventoryitems/weapons/weapon_tmp.txt","host":"inventoryitems/guest/cof/weapons/weapon_tmp.txt","bytes":447,"sha256":"f39a3846ec28effe200a231e63d133ba06c59f73d756006dcb0a086614719002"},
    {"group":"TMPParticle","original":"aurora/gunsmoke.aur","host":"aurora/guest/cof/gunsmoke.aur","bytes":685,"sha256":"e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"}
]
MP5_PINS = [
    {"group":"MP5Models","original":"models/weapons/mp5/v_mp5.mdl","host":"models/guest/cof/mp5/v_mp5.mdl","bytes":2283620,"sha256":"d76dcbc785acba0d82535fdec24ed0dd2995cd2e19ba1290d06a30e365389cd4"},
    {"group":"MP5Models","original":"models/weapons/mp5/w_mp5.mdl","host":"models/guest/cof/mp5/w_mp5.mdl","bytes":1343356,"sha256":"c056c1f8f09e96d7b09c9d589272fc66dca4d8d993b0e80097ee16d5987ba3c5"},
    {"group":"MP5Models","original":"models/weapons/mp5/w_mp5.mdl","host":"models/guest/cof/mp5/p_mp5.mdl","bytes":1343356,"sha256":"c056c1f8f09e96d7b09c9d589272fc66dca4d8d993b0e80097ee16d5987ba3c5"},
    {"group":"MP5Models","original":"models/null.mdl","host":"models/guest/cof/common/null.mdl","bytes":1868,"sha256":"97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"},
    {"group":"MP5Models","original":"models/shell.mdl","host":"models/guest/cof/common/shell.mdl","bytes":70344,"sha256":"5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"},
    {"group":"MP5Models","original":"models/w_9mmclip.mdl","host":"models/guest/cof/common/w_9mmclip.mdl","bytes":1772,"sha256":"2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"},
    {"group":"MP5Models","original":"sprites/bubble.spr","host":"sprites/guest/cof/bubble.spr","bytes":1086,"sha256":"377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"},
    {"group":"MP5Models","original":"sprites/fog2.spr","host":"sprites/guest/cof/fog2.spr","bytes":4926,"sha256":"6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"},
    {"group":"MP5Models","original":"models/flash1.mdl","host":"models/guest/cof/common/flash1.mdl","bytes":133896,"sha256":"3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"},
    {"group":"MP5Models","original":"sprites/gunsmoke.spr","host":"sprites/guest/cof/gunsmoke.spr","bytes":214062,"sha256":"f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"},
    {"group":"MP5ClientModels","original":"models/flash2.mdl","host":"models/guest/cof/common/flash2.mdl","bytes":122304,"sha256":"f7f098c0db5eb3254458593d14c6b5a2e2e37af2d4afc1a1e56bb166c1df778c"},
    {"group":"MP5Sounds","original":"weapons/mp5/mp5_charger_back.wav","host":"guest/cof/weapons/mp5/mp5_charger_back.wav","bytes":14132,"sha256":"dd84d09f53f464a9ebfba13960bc716dcb5f9133439586b9fe700f757933a938"},
    {"group":"MP5Sounds","original":"weapons/mp5/mp5_charger_forward.wav","host":"guest/cof/weapons/mp5/mp5_charger_forward.wav","bytes":27568,"sha256":"9659a01be227b6ed8ced116e24022d56d7acbfad870814bcd1468d464111df6f"},
    {"group":"MP5Sounds","original":"weapons/mp5/mp5_magin.wav","host":"guest/cof/weapons/mp5/mp5_magin.wav","bytes":29310,"sha256":"e82fbf7a3daa4f598755332199cacb975421579ddc8bca4ddbea2468d37eda86"},
    {"group":"MP5Sounds","original":"weapons/mp5/mp5_magout.wav","host":"guest/cof/weapons/mp5/mp5_magout.wav","bytes":28338,"sha256":"d85a01d6922558dabb21b2858f7f2ee6422f239ba8726eeb50cb7db1d90ffc87"},
    {"group":"MP5Sounds","original":"weapons/mp5/mp5_shoot_loop.wav","host":"guest/cof/weapons/mp5/mp5_shoot_loop.wav","bytes":73174,"sha256":"fcc5c4b8fe193eff5a4bd3b3514693cb4463ac7d4c002e9fdc0b84a34b7b6a9c"},
    {"group":"MP5Sounds","original":"weapons/mp5/mp5_shoot_end.wav","host":"guest/cof/weapons/mp5/mp5_shoot_end.wav","bytes":45792,"sha256":"56c53c3d581ee89f379cbd71a2fb28b98cb30d5ac71696376cf68a5766ab330b"},
    {"group":"MP5Sounds","original":"weapons/item_get.wav","host":"guest/cof/weapons/item_get.wav","bytes":22110,"sha256":"e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"},
    {"group":"MP5Sounds","original":"weapons/weapon_get.wav","host":"guest/cof/weapons/weapon_get.wav","bytes":21560,"sha256":"35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"},
    {"group":"MP5Sounds","original":"weapons/sleeve_generic1.wav","host":"guest/cof/weapons/sleeve_generic1.wav","bytes":24268,"sha256":"4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"},
    {"group":"MP5Sounds","original":"weapons/sleeve_generic2.wav","host":"guest/cof/weapons/sleeve_generic2.wav","bytes":33128,"sha256":"0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"},
    {"group":"MP5Sounds","original":"weapons/sleeve_generic3.wav","host":"guest/cof/weapons/sleeve_generic3.wav","bytes":17450,"sha256":"76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"},
    {"group":"MP5Sounds","original":"player/police/Swing/Swing1.wav","host":"guest/cof/player/police/Swing/Swing1.wav","bytes":46076,"sha256":"72ad87048f17225cc696be76ba4623bab4c169606300b134243bb207c9344248"},
    {"group":"MP5Sounds","original":"player/police/Swing/Swing2.wav","host":"guest/cof/player/police/Swing/Swing2.wav","bytes":35178,"sha256":"23f01531592a61ef8cdd1f11f7c80f614666bbe795418f1eccc88e697be9eb5b"},
    {"group":"MP5Sounds","original":"player/police/Swing/Swing3.wav","host":"guest/cof/player/police/Swing/Swing3.wav","bytes":44216,"sha256":"26ca79e9575ad9d90687eea53e75f4ee22a2eaae4e0e1422cda1769321e6f3d1"},
    {"group":"MP5Sounds","original":"player/police/Swing/Swing4.wav","host":"guest/cof/player/police/Swing/Swing4.wav","bytes":37976,"sha256":"bd6317e251285827a705298591231a31729882305dff54321b64b0b70833aeb7"},
    {"group":"MP5Sounds","original":"player/police/Swing/Swing5.wav","host":"guest/cof/player/police/Swing/Swing5.wav","bytes":23372,"sha256":"ae0085cd5a580303460770e2bb97eda506ccd87dde2019a129c90c896f03dd94"},
    {"group":"MP5Sounds","original":"player/police/Swing/Swing6.wav","host":"guest/cof/player/police/Swing/Swing6.wav","bytes":28018,"sha256":"df8e230506ce0985f3038f18b009266bf5458624a84219f6185561353c7a118c"},
    {"group":"MP5Sounds","original":"weapons/mobile/punch_swing.wav","host":"guest/cof/weapons/mobile/punch_swing.wav","bytes":17450,"sha256":"76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"},
    {"group":"MP5Sounds","original":"weapons/mobile/punch_smack.wav","host":"guest/cof/weapons/mobile/punch_smack.wav","bytes":7092,"sha256":"70164307237d414829ca9a7edbda804c5050e845f977688cb7cd25d6b1d83657"},
    {"group":"MP5Sounds","original":"weapons/weapon_fire_empty.wav","host":"guest/cof/weapons/weapon_fire_empty.wav","bytes":22340,"sha256":"b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"},
    {"group":"MP5Sounds","original":"weapons/weapon_fire_empty_noclick.wav","host":"guest/cof/weapons/weapon_fire_empty_noclick.wav","bytes":18228,"sha256":"de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"},
    {"group":"MP5LocalWaves","original":"weapons_new/mp5/mp5_charger_back.wav","host":"sound/guest/cof/weapons_new/mp5/mp5_charger_back.wav","bytes":184724,"sha256":"2b027b7b5b5c3ad6ed4d7c2b8681786c611f0344eae72d3511a957b19c1bd5ec"},
    {"group":"MP5LocalWaves","original":"weapons_new/mp5/mp5_charger_forward.wav","host":"sound/guest/cof/weapons_new/mp5/mp5_charger_forward.wav","bytes":359330,"sha256":"3e4f69da2842cba52cb8ee356b7590ab1f213a43775c034e43dd02cc9084ea64"},
    {"group":"MP5LocalWaves","original":"weapons_new/mp5/mp5_magin.wav","host":"sound/guest/cof/weapons_new/mp5/mp5_magin.wav","bytes":382292,"sha256":"a19e0d724f78a0d0ae7bcd2b244ea00d00ca2e980baf65b338e1a5a09ab10ebf"},
    {"group":"MP5LocalWaves","original":"weapons_new/mp5/mp5_magout.wav","host":"sound/guest/cof/weapons_new/mp5/mp5_magout.wav","bytes":369590,"sha256":"772847f992d1cf58566ba9c37f641cf16444b7c8453224834b7c566ab17c88e0"},
    {"group":"MP5Events","original":"events/muzzleflash.sc","host":"events/guest/cof/muzzleflash.sc","bytes":1778,"sha256":"2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"},
    {"group":"MP5Events","original":"events/viewpunch.sc","host":"events/guest/cof/viewpunch.sc","bytes":1778,"sha256":"2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"},
    {"group":"MP5Inventory","original":"inventoryitems/weapons/weapon_mp5.txt","host":"inventoryitems/guest/cof/weapons/weapon_mp5.txt","bytes":455,"sha256":"917fd1e6fa3eda9a94de9ad80561680502ce70abd6623d5dedaabe2ce2fb578d"},
    {"group":"MP5Particle","original":"aurora/gunsmoke.aur","host":"aurora/guest/cof/gunsmoke.aur","bytes":685,"sha256":"e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"},
]
MP5_PLAYER_MODEL = {"bytes":1344808,"sha256":"7390f49357e9fcdc726324447ee9603d438eb673423e67be0fa876b7dc14e1a2"}

SHOT_DECAL_PINS = [dict(group='ShotDecals', original=f'gfx/decals/shot{i}.tga',
    host=f'gfx/guest/cof/decals/shot{i}.tga', bytes=12306, sha256=sha)
    for i, sha in enumerate((
        '1942371dee349f9d7468644a1ebc3c7f44d97b3f1d305f1af40d79b1e589e1ab',
        'ca19ecbc94ba986e4ad5c6fa1a40fe8d57591112a5de6211854aab3bfca835ed',
        'd0ddd8c6f875d8f23e93a86d359ae435d0716f0bc4e12d11eadcf19093781aba',
        'fe0c41d78383d4609697d960567ecdc2f13042c255d33a2ec2ab4b0df138856c',
        '1942371dee349f9d7468644a1ebc3c7f44d97b3f1d305f1af40d79b1e589e1ab'), 1)]
ORIGINAL_MODULE_SHA256 = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
CONSUMERS = (
    'native/guest/cof/client/weapon_effects.cpp',
    'native/guest/cof/client/presentation.cpp',
    'native/csol/client/weapons/guest_cof_audio.cpp',
    'native/csol/client/weapons/guest_cof_particles.cpp',
    'native/csol/client/weapons/guest_cof_decals.cpp',
    'native/guest/cof/client/custom_decal.cpp',
    'native/engine/csol_loader.cpp',
    'external/ReHLDS/rehlds/engine/sv_main.cpp',
    'external/ReGameDLL_CS/regamedll/engine/studio.h',
)
CLIP_TEXTURE = dict(original='models/w_9mmclipT.mdl',
    host='models/guest/cof/common/w_9mmclipT.mdl', bytes=5096,
    sha256='18ce87d7f1e23f69f479a99a2e5c49ee5ea95e2712ade3b4c6496fbc172bae3d')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def no_links(path):
    """Reject existing link/reparse ancestors before resolving or creating."""
    path = path.absolute()
    for part in reversed((path, *path.parents)):
        if part.is_symlink() or os.path.isjunction(part):
            raise ValueError('Path traverses a link/junction: ' + str(part))
        if part.exists() and getattr(part.stat(), 'st_file_attributes', 0) & 0x400:
            raise ValueError('Path traverses a reparse point: ' + str(part))
    return path.resolve()


def workspace_target(path):
    value = no_links(path)
    root = no_links(BUILD)
    if value == root or not value.is_relative_to(root):
        raise ValueError('Output must be strictly inside this workspace build directory')
    return value


def safe_relative(name):
    if not name or '\\' in name or ':' in name or name.startswith('/') or any(
            part in ('', '.', '..') for part in name.split('/')):
        raise ValueError('Unsafe resource path: ' + name)
    return name


M16_PINS = [
    {
        "group": "M16Models",
        "original": "models/weapons/m16/v_m16.mdl",
        "host": "models/guest/cof/m16/v_m16.mdl",
        "bytes": 2598940,
        "sha256": "8aaa8cc0ce4badc51b2f176752f8a858cddf95b113e011eff97ad912b7aa467b"
    },
    {
        "group": "M16Models",
        "original": "models/weapons/m16/w_m16.mdl",
        "host": "models/guest/cof/m16/w_m16.mdl",
        "bytes": 338396,
        "sha256": "e88ed15271bc6ad2eb8e841d15628e308faf3f976076179b34d47edf0d03de9e"
    },
    {
        "group": "M16Models",
        "original": "police/models/weapons/m16/p_m16.mdl",
        "host": "models/guest/cof/m16/p_m16.mdl",
        "bytes": 340240,
        "sha256": "69e88ff618b4000e572a067712247f0e5f9f7ea4e886d51080ee0d6dc00a5fd9"
    },
    {
        "group": "M16Models",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "M16Models",
        "original": "models/556shell.mdl",
        "host": "models/guest/cof/common/556shell.mdl",
        "bytes": 7572,
        "sha256": "31e55cd51a988b887b20ebf97e0d2593c70f13ba08b8d3282be7a3337ba40e9b"
    },
    {
        "group": "M16Models",
        "original": "models/w_9mmclip.mdl",
        "host": "models/guest/cof/common/w_9mmclip.mdl",
        "bytes": 1772,
        "sha256": "2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"
    },
    {
        "group": "M16Models",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "M16Models",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "M16Models",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "M16Models",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "M16ClientModels",
        "original": "models/flash2.mdl",
        "host": "models/guest/cof/common/flash2.mdl",
        "bytes": 122304,
        "sha256": "f7f098c0db5eb3254458593d14c6b5a2e2e37af2d4afc1a1e56bb166c1df778c"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_cock.wav",
        "host": "guest/cof/weapons/m16/m16_cock.wav",
        "bytes": 43096,
        "sha256": "9dfa7b46815e94d147d99bab6c76a9981730b27a0769eba15498c745ba838d35"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_magin.wav",
        "host": "guest/cof/weapons/m16/m16_magin.wav",
        "bytes": 54118,
        "sha256": "940a2f580f1136f5c86d5c2149734a1e293e7e18de4b6c88117fb3be99c578c4"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_magout.wav",
        "host": "guest/cof/weapons/m16/m16_magout.wav",
        "bytes": 20722,
        "sha256": "7280f4ed8833c2daf9e543c83d84a609416b32e70edc9e686749cb56d88d8164"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_fire.wav",
        "host": "guest/cof/weapons/m16/m16_fire.wav",
        "bytes": 38738,
        "sha256": "477867f4d6413cb7624578e1c80e6a3abe723bf390b9ceca23b4409f9d0ea00f"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_magtap.wav",
        "host": "guest/cof/weapons/m16/m16_magtap.wav",
        "bytes": 25306,
        "sha256": "5404a39aac51a8c1ee779565f80412b96ef28a48528274a0ca7fd6bbd4160084"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_cock1.wav",
        "host": "guest/cof/weapons/m16/m16_cock1.wav",
        "bytes": 18902,
        "sha256": "f3af6e5c0299a33b3fcd706a6c0d43d04525432e20ec88962bb2a7f4b3c575f7"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_cock2.wav",
        "host": "guest/cof/weapons/m16/m16_cock2.wav",
        "bytes": 20796,
        "sha256": "7bc3c50581217669e567926f34f0a703de6ddd2230fd02218b494ebead172b95"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/m16/m16_switch.wav",
        "host": "guest/cof/weapons/m16/m16_switch.wav",
        "bytes": 15974,
        "sha256": "73198807427112ace1c9ac0d4c0d4345ecd2a02601f59c6aeae48b2685af8274"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/melee_swing.wav",
        "host": "guest/cof/weapons/melee_swing.wav",
        "bytes": 33256,
        "sha256": "631515a589da923dce206ba2c15f8c8a348b4bec8ca96fa34c3940d2a16632d7"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/melee_hit.wav",
        "host": "guest/cof/weapons/melee_hit.wav",
        "bytes": 39556,
        "sha256": "9f5bc3c8d8bbb8ff84e12b974b71fe22ad81a7bb07a15d35b58fd0f2af96f37a"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "M16Sounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_magin.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_magin.wav",
        "bytes": 357532,
        "sha256": "f2be106e7c7f900885eee13da0e2b4bd09f44581fcf52a5db1cd809c130e1664"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_magout.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_magout.wav",
        "bytes": 274478,
        "sha256": "50d61f04b75faf36dd6d9d40aa23c9aba491248c744130fcea8cc98f2dfd1d39"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_fire.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_fire.wav",
        "bytes": 505418,
        "sha256": "b5728b348f0c24ad9d865ec6cc19b411ce878334e66cc8079d4a0a113f8a4267"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_magtap.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_magtap.wav",
        "bytes": 334334,
        "sha256": "61acfd24ed6e0e7dee05fbe6eef7fe40fc627935cd9eef7f6ae9985e2ead6e76"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_cock1.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_cock1.wav",
        "bytes": 123192,
        "sha256": "0da055f4dabc433f76910990cb1e2c44e6026fd0fe1dc614c6719e10c9c7f205"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_cock2.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_cock2.wav",
        "bytes": 135566,
        "sha256": "bb34f2ab0a1704f74faca3354385c125919cbacd94e7b7aada659c8fb27cbf06"
    },
    {
        "group": "M16LocalWaves",
        "original": "weapons_new/m16/m16_switch.wav",
        "host": "sound/guest/cof/weapons_new/m16/m16_switch.wav",
        "bytes": 108430,
        "sha256": "a04d3c8c152fff96da7501b836f1efe24e5860cb31fd32e35cff9ff48dbc4e59"
    },
    {
        "group": "M16Events",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "M16Events",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "M16Inventory",
        "original": "inventoryitems/weapons/weapon_m16.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_m16.txt",
        "bytes": 479,
        "sha256": "ee699b41cd5096221823d4678f0d6fc5b8dad8fb92fabe28857568ad0e22e34b"
    },
    {
        "group": "M16Particle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]

G43_PINS = [
    {
        "group": "G43Models",
        "original": "models/weapons/g43/v_g43.mdl",
        "host": "models/guest/cof/g43/v_g43.mdl",
        "bytes": 2466432,
        "sha256": "bb14a6182a47e36fe89973aa6a78a6a65caac6529141b8e2287605b32f249031"
    },
    {
        "group": "G43Models",
        "original": "models/weapons/g43/w_g43.mdl",
        "host": "models/guest/cof/g43/w_g43.mdl",
        "bytes": 263612,
        "sha256": "fffd5b22be8a3f8fb4ed076d26fab24dabe9384c8144cf68173662c5af5a9cf5"
    },
    {
        "group": "G43Models",
        "original": "police/models/weapons/g43/p_g43.mdl",
        "host": "models/guest/cof/g43/p_g43.mdl",
        "bytes": 265184,
        "sha256": "e5a2a3e637a8e1234a4a53b90b85b9a347203a442aa5b6722623ad465c101613"
    },
    {
        "group": "G43Models",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "G43Models",
        "original": "models/shell.mdl",
        "host": "models/guest/cof/common/shell.mdl",
        "bytes": 70344,
        "sha256": "5ccd27014190edee6aa8eb62d5408d1de75863d81a3c51e915e49f32ff8c5060"
    },
    {
        "group": "G43Models",
        "original": "models/w_9mmclip.mdl",
        "host": "models/guest/cof/common/w_9mmclip.mdl",
        "bytes": 1772,
        "sha256": "2afde7ef1e3d987d203f517715dedc482768cc666c964a617990326734ee00e1"
    },
    {
        "group": "G43Models",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "G43Models",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "G43Models",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "G43Models",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "G43ClientModels",
        "original": "models/flash2.mdl",
        "host": "models/guest/cof/common/flash2.mdl",
        "bytes": 122304,
        "sha256": "f7f098c0db5eb3254458593d14c6b5a2e2e37af2d4afc1a1e56bb166c1df778c"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/g43/g43_fire.wav",
        "host": "guest/cof/weapons/g43/g43_fire.wav",
        "bytes": 65832,
        "sha256": "3c4827663674fbb89e2b57af02da7f9f2d8693fb2ee6ae88f6f5007a2e616549"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/g43/g43_cock.wav",
        "host": "guest/cof/weapons/g43/g43_cock.wav",
        "bytes": 28574,
        "sha256": "7987575fdd77d544e8ac94f34507cfdcfbce0e0d4c9db1b1def1d356380a633e"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/g43/g43_magin.wav",
        "host": "guest/cof/weapons/g43/g43_magin.wav",
        "bytes": 33824,
        "sha256": "bd12036858fa76fb98d55ce9b6015c191e792d038d8fd4bcf074e460d5103c49"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/g43/g43_magout.wav",
        "host": "guest/cof/weapons/g43/g43_magout.wav",
        "bytes": 23194,
        "sha256": "4fe69fc0db75d5ed1d6fbd41f3aa3cea19c126233880997537793181129ca65f"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/melee_swing.wav",
        "host": "guest/cof/weapons/melee_swing.wav",
        "bytes": 33256,
        "sha256": "631515a589da923dce206ba2c15f8c8a348b4bec8ca96fa34c3940d2a16632d7"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/melee_hit.wav",
        "host": "guest/cof/weapons/melee_hit.wav",
        "bytes": 39556,
        "sha256": "9f5bc3c8d8bbb8ff84e12b974b71fe22ad81a7bb07a15d35b58fd0f2af96f37a"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "G43Sounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "G43LocalWaves",
        "original": "weapons_new/g43/g43_fire.wav",
        "host": "sound/guest/cof/weapons_new/g43/g43_fire.wav",
        "bytes": 859298,
        "sha256": "5e4f1d08c61ecd83eed995b62550841ec6ea7096d2bdec51e42783e8fcc6c0b1"
    },
    {
        "group": "G43LocalWaves",
        "original": "weapons_new/g43/g43_cock.wav",
        "host": "sound/guest/cof/weapons_new/g43/g43_cock.wav",
        "bytes": 377018,
        "sha256": "55e635ee6a15c8fa795296807d93e4ae9571ae94884340d0b35f7f1aab0cf6cc"
    },
    {
        "group": "G43LocalWaves",
        "original": "weapons_new/g43/g43_magin.wav",
        "host": "sound/guest/cof/weapons_new/g43/g43_magin.wav",
        "bytes": 225000,
        "sha256": "57ea49fb15808e92275c3421d6da6c2f7d321c3027d532589017605731f91750"
    },
    {
        "group": "G43LocalWaves",
        "original": "weapons_new/g43/g43_magout.wav",
        "host": "sound/guest/cof/weapons_new/g43/g43_magout.wav",
        "bytes": 306752,
        "sha256": "24f4f9882e38240f70431417b569e1b2e6f5495de80c6ca904a409ff41eb3bd0"
    },
    {
        "group": "G43Events",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "G43Events",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "G43Inventory",
        "original": "inventoryitems/weapons/weapon_g43.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_g43.txt",
        "bytes": 463,
        "sha256": "871d479e3245a5596f0f247d9841c5cf4a0f5ac9cddda517b91787197d1f3e91"
    },
    {
        "group": "G43Particle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]

SHOTGUN_PINS = [
    {
        "group": "ShotgunModels",
        "original": "models/weapons/shotgun/v_shotgun.mdl",
        "host": "models/guest/cof/shotgun/v_shotgun.mdl",
        "bytes": 1772036,
        "sha256": "08ad6ab4a5fe3156b2577a795aa00a9b1e3afe5c6c96ae3558f41b5f5e6f15f6"
    },
    {
        "group": "ShotgunModels",
        "original": "models/weapons/shotgun/w_shotgun.mdl",
        "host": "models/guest/cof/shotgun/w_shotgun.mdl",
        "bytes": 71288,
        "sha256": "8c60a8fc156ecf40a8a30f3913fd8c2cf734c5db8ccbe5c400ab8bdb376fe69e"
    },
    {
        "group": "ShotgunModels",
        "original": "police/models/weapons/shotgun/p_shotgun.mdl",
        "host": "models/guest/cof/shotgun/p_shotgun.mdl",
        "bytes": 72212,
        "sha256": "60911831719dc57cca1bcb34801c8b74e24443372afe13d5230e336316a5a5bc"
    },
    {
        "group": "ShotgunModels",
        "original": "models/null.mdl",
        "host": "models/guest/cof/common/null.mdl",
        "bytes": 1868,
        "sha256": "97fc6326a1d63b97242c766381c7be774d0dc89b0e28fa10189a5937f17a1890"
    },
    {
        "group": "ShotgunModels",
        "original": "models/shotgunshell.mdl",
        "host": "models/guest/cof/common/shotgunshell.mdl",
        "bytes": 18940,
        "sha256": "3ed81cecc17a5640dd28ec04284498b13209ce7bc354f7be161dc5946f5a1220"
    },
    {
        "group": "ShotgunModels",
        "original": "sprites/bubble.spr",
        "host": "sprites/guest/cof/bubble.spr",
        "bytes": 1086,
        "sha256": "377276c6b6960a1415248bc96e1d370e5eeb504ffd6d95c0f81e480ab82a46eb"
    },
    {
        "group": "ShotgunModels",
        "original": "sprites/fog2.spr",
        "host": "sprites/guest/cof/fog2.spr",
        "bytes": 4926,
        "sha256": "6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f"
    },
    {
        "group": "ShotgunModels",
        "original": "models/flash1.mdl",
        "host": "models/guest/cof/common/flash1.mdl",
        "bytes": 133896,
        "sha256": "3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c"
    },
    {
        "group": "ShotgunModels",
        "original": "sprites/gunsmoke.spr",
        "host": "sprites/guest/cof/gunsmoke.spr",
        "bytes": 214062,
        "sha256": "f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/shotgun/shoot.wav",
        "host": "guest/cof/weapons/shotgun/shoot.wav",
        "bytes": 81194,
        "sha256": "6ad25f543b809559146a562452abd77f9a7b6511cd4b1eb6eacc6031f1e28d27"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/shotgun/insert.wav",
        "host": "guest/cof/weapons/shotgun/insert.wav",
        "bytes": 29934,
        "sha256": "00b99a52ad023913c9648ca3b7e9055f17675dff61c3c276b4c6cba1ddee2318"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/shotgun/pump_forward.wav",
        "host": "guest/cof/weapons/shotgun/pump_forward.wav",
        "bytes": 32494,
        "sha256": "ffdc8f4bb11145b357e5bc3382321bab8250f2af3234feaf83898b098b63b86d"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/shotgun/pump_back.wav",
        "host": "guest/cof/weapons/shotgun/pump_back.wav",
        "bytes": 16968,
        "sha256": "429daa4d7a653092f292134242d87cc0a1a85be667a76dc54d868154051911f6"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/shotgun/pump_seq.wav",
        "host": "guest/cof/weapons/shotgun/pump_seq.wav",
        "bytes": 50338,
        "sha256": "078d2ac3b0d5ae437d94635f5ab6dd63d05464d586f6c649b8075fc48ef94481"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/item_get.wav",
        "host": "guest/cof/weapons/item_get.wav",
        "bytes": 22110,
        "sha256": "e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/weapon_get.wav",
        "host": "guest/cof/weapons/weapon_get.wav",
        "bytes": 21560,
        "sha256": "35c31cf77cd0ff236061696c22a69bde596bacb29ae2bde946f98ed50b6158a8"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/sleeve_generic1.wav",
        "host": "guest/cof/weapons/sleeve_generic1.wav",
        "bytes": 24268,
        "sha256": "4ac664f754635a82bd4dac6677e8f67bd79e449eff33fbb4de7d65a3bbcc5655"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/sleeve_generic2.wav",
        "host": "guest/cof/weapons/sleeve_generic2.wav",
        "bytes": 33128,
        "sha256": "0337c5b98fe21726962cf9dc4ae1d9605a6b7ac7190a17eed4ed0e7a254758c1"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/sleeve_generic3.wav",
        "host": "guest/cof/weapons/sleeve_generic3.wav",
        "bytes": 17450,
        "sha256": "76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/melee_swing.wav",
        "host": "guest/cof/weapons/melee_swing.wav",
        "bytes": 33256,
        "sha256": "631515a589da923dce206ba2c15f8c8a348b4bec8ca96fa34c3940d2a16632d7"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/melee_hit.wav",
        "host": "guest/cof/weapons/melee_hit.wav",
        "bytes": 39556,
        "sha256": "9f5bc3c8d8bbb8ff84e12b974b71fe22ad81a7bb07a15d35b58fd0f2af96f37a"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/weapon_fire_empty.wav",
        "host": "guest/cof/weapons/weapon_fire_empty.wav",
        "bytes": 22340,
        "sha256": "b4d1f0ef30b30276bc2a1b0c7dfc222e00251abce594df6e791d8b0937fcf629"
    },
    {
        "group": "ShotgunSounds",
        "original": "weapons/weapon_fire_empty_noclick.wav",
        "host": "guest/cof/weapons/weapon_fire_empty_noclick.wav",
        "bytes": 18228,
        "sha256": "de0ae838bdbfa7f75bc1b10b35814b4aa19a574d589adf36f071061b63d9ed8f"
    },
    {
        "group": "ShotgunLocalWaves",
        "original": "weapons_new/shotgun/shoot.wav",
        "host": "sound/guest/cof/weapons_new/shotgun/shoot.wav",
        "bytes": 1064300,
        "sha256": "2a3dfba015f7aa3a82e1d223729d4a3c855c221a67def6f4dca0f0625e559221"
    },
    {
        "group": "ShotgunLocalWaves",
        "original": "weapons_new/shotgun/insert.wav",
        "host": "sound/guest/cof/weapons_new/shotgun/insert.wav",
        "bytes": 394784,
        "sha256": "66e189b139ef7b2c19043ce41d301a75dafd45247e453e45ad87c23d23d5d9c4"
    },
    {
        "group": "ShotgunLocalWaves",
        "original": "weapons_new/shotgun/pump_forward.wav",
        "host": "sound/guest/cof/weapons_new/shotgun/pump_forward.wav",
        "bytes": 428234,
        "sha256": "014e6e6bc2b10db75e2ca57985fe30d2f53aa1ffb359979cc2bc177454797e98"
    },
    {
        "group": "ShotgunLocalWaves",
        "original": "weapons_new/shotgun/pump_back.wav",
        "host": "sound/guest/cof/weapons_new/shotgun/pump_back.wav",
        "bytes": 225446,
        "sha256": "c0d01b3e8b35e8db31714cec253fc6ba9ca276cffca269d21a311916791bcfb6"
    },
    {
        "group": "ShotgunLocalWaves",
        "original": "weapons_new/shotgun/pump_seq.wav",
        "host": "sound/guest/cof/weapons_new/shotgun/pump_seq.wav",
        "bytes": 661292,
        "sha256": "e8851caf210083cb58894c236f8d7e5efc863de1a229108fb485924f37125262"
    },
    {
        "group": "ShotgunEvents",
        "original": "events/muzzleflash.sc",
        "host": "events/guest/cof/muzzleflash.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "ShotgunEvents",
        "original": "events/viewpunch.sc",
        "host": "events/guest/cof/viewpunch.sc",
        "bytes": 1778,
        "sha256": "2c7d43b8dc6ea01a32acddb7798b9dabf0ec44c7a6dcf75160539a7fe53e029d"
    },
    {
        "group": "ShotgunInventory",
        "original": "inventoryitems/weapons/weapon_shotgun.txt",
        "host": "inventoryitems/guest/cof/weapons/weapon_shotgun.txt",
        "bytes": 490,
        "sha256": "83e6a516976eb93890bd29f1e83135df896ef2dc4884f53ff68a1b9c2d143441"
    },
    {
        "group": "ShotgunParticle",
        "original": "aurora/gunsmoke.aur",
        "host": "aurora/guest/cof/gunsmoke.aur",
        "bytes": 685,
        "sha256": "e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    }
]

def current_manifest(weapon='P345'):
    if weapon not in ('P345', 'Glock', 'Revolver', 'VP70', 'TMP', 'MP5', 'M16', 'G43', 'Shotgun'):
        raise ValueError('Unreviewed weapon resource manifest')
    text = HEADER.read_text('utf-8')
    actual = []
    groups = ('Models', 'ClientModels', 'Sounds', 'LocalWaves', 'Events') if weapon in ('MP5','M16','G43') else (
        'Models', 'Sounds', 'LocalWaves', 'Events')
    for suffix in groups:
        group = weapon + suffix
        source = group
        alias={'M16ClientModels':'MP5ClientModels','M16Events':'MP5Events','G43ClientModels':'MP5ClientModels','G43Events':'MP5Events','ShotgunEvents':'P345Events'}.get(group)
        if alias:
            if not re.search(r'\b'+group+r'\s*=\s*'+alias+r'\s*;',text):raise ValueError('Unreviewed M16 resource alias')
            source=alias
        if group == 'GlockEvents':
            if not re.search(r'\bGlockEvents\s*=\s*P345Events\s*;', text):
                raise ValueError('Unreviewed Glock event alias')
            source = 'P345Events'
        match = re.search(r'\b' + source + r'\s*\{\{(.*?)\}\};', text, re.S)
        if not match:
            raise ValueError('Authoritative resource table missing: ' + group)
        pairs = re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', match[1])
        actual.extend((group, *pair) for pair in pairs)
    for suffix in ('Inventory', 'Particle'):
        group = weapon + suffix
        source = group
        if group in ('M16Particle','G43Particle'):
            if not re.search(r'\b'+group+r'\s*=\s*MP5Particle\s*;',text):raise ValueError('Unreviewed M16 particle alias')
            source='MP5Particle'
        if group in ('ShotgunInventory','ShotgunParticle'):
            source='ShotgunCore'+suffix
            if not re.search(r'\b'+group+r'\s*=\s*'+source+r'\s*;',text):raise ValueError('Unreviewed shotgun metadata alias')
        if group == 'GlockParticle':
            if not re.search(r'\bGlockParticle\s*=\s*P345Particle\s*;', text):
                raise ValueError('Unreviewed Glock particle alias')
            source = 'P345Particle'
        match = re.search(r'\b' + source + r'\s*\{\s*"([^"]+)",\s*"([^"]+)"\s*\};', text)
        if not match:
            raise ValueError('Authoritative resource path missing: ' + group)
        actual.append((group, *match.groups()))
    pins = {'P345': PINS, 'Glock': GLOCK_PINS, 'Revolver': REVOLVER_PINS,
            'VP70': VP70_PINS, 'TMP': TMP_PINS, 'MP5': MP5_PINS, 'M16': M16_PINS, 'G43': G43_PINS, 'Shotgun': SHOTGUN_PINS}[weapon]
    wanted = [(p['group'], p['original'], p['host']) for p in pins]
    if actual != wanted:
        raise ValueError('Resource header differs from the reviewed '+weapon+' manifest; review new identities first')
    shot = re.search(r'\bShotDecals\s*\{\{(.*?)\}\};', text, re.S)
    if not shot or re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', shot[1]) != [
            (p['original'], p['host']) for p in SHOT_DECAL_PINS]:
        raise ValueError('Shot decal paths differ from the five reviewed original files')
    dependency = re.search(r'\bP345ClipTexture\s*\{\s*"([^"]+)",\s*"([^"]+)"\s*\};', text)
    if not dependency or dependency.groups() != (CLIP_TEXTURE['original'], CLIP_TEXTURE['host']):
        raise ValueError('Clip texture dependency path differs from the reviewed companion')
    if weapon in ('Glock', 'Revolver', 'VP70', 'TMP', 'MP5', 'M16', 'G43') and not re.search(r'\b'+weapon+r'ClipTexture\s*=\s*P345ClipTexture\s*;', text):
        raise ValueError('Unreviewed '+weapon+' clip texture alias')
    return digest(HEADER.read_bytes())


def integer(data, at):
    return struct.unpack_from('<i', data, at)[0]


def model_events(data, allow_clip_texture=False):
    """Read the real Studio v10 event table using the SDK's checked layout."""
    if len(data) < 244 or data[:4] != b'IDST' or integer(data, 4) != 10 or integer(data, 72) != len(data):
        raise ValueError('Invalid complete Studio v10 model')
    count, start = integer(data, 164), integer(data, 168)
    if count < 1 or count > 2048 or start < 244 or start + count * 176 > len(data):
        raise ValueError('Invalid sequence table')
    # Existing ReHLDS already handles texture/group dependencies. This frozen
    # pack deliberately admits only the audited embedded, single-group inputs.
    external = integer(data, 180) == 0 and integer(data, 184) == 0
    if integer(data, 172) != 1 or (external and not allow_clip_texture) or (
            not external and (integer(data, 180) < 1 or integer(data, 184) < 244)):
        raise ValueError('Model adds unreviewed external texture/sequence dependencies')
    texture_count, texture_start = integer(data, 180), integer(data, 184)
    if texture_start + texture_count * 80 > len(data):
        raise ValueError('Invalid embedded texture table')
    for index in range(texture_count):
        name = data[texture_start+80*index:texture_start+80*index+64].split(b'\0', 1)[0].lower()
        if name.startswith((b'$', b'#', b'@', b'cso_')):
            raise ValueError('Model activates an unreviewed GoldCraft special dependency path')
    events = []
    for sequence in range(count):
        at = start + sequence * 176
        total, offset = integer(data, at + 48), integer(data, at + 52)
        if total < 0 or total > 4096 or offset < 0 or offset + total * 76 > len(data):
            raise ValueError('Invalid Studio event table')
        for index in range(total):
            event = offset + index * 76
            raw = data[event+12:event+76]
            if b'\0' not in raw:
                raise ValueError('Unterminated Studio event options')
            events.append(dict(sequence=sequence, frame=integer(data, event),
                event=integer(data, event+4), options=raw.split(b'\0', 1)[0].decode('ascii')))
    return events


def file_identity(path):
    stat = path.stat()
    return dict(device=stat.st_dev, fileId=stat.st_ino, size=stat.st_size, modifiedNs=stat.st_mtime_ns)


def checked_bytes(path, pin):
    before = file_identity(path)
    if not path.is_file() or before['size'] != pin['bytes']:
        raise ValueError('Resource size/type differs: ' + str(path))
    data = path.read_bytes()
    if file_identity(path) != before or digest(data) != pin['sha256']:
        raise ValueError('Resource SHA256/identity differs: ' + str(path))
    return data, before


def plan(args):
    if args.server_model_sounds != 'disabled':
        raise ValueError('Original Studio5004 logical keys require disabled automatic model-sound scanning; public aliases are forbidden')
    original = no_links(args.original_game_root)
    if not original.is_dir():
        raise ValueError('Original game root is not a directory')
    inherited = no_links(args.inherited_game_root) if args.inherited_game_root else None
    if inherited is not None and not inherited.is_dir():
        raise ValueError('Inherited game root is not a directory')
    output, plan_path = workspace_target(args.output), workspace_target(args.plan)
    if output.exists() or plan_path == output or plan_path.is_relative_to(output):
        raise ValueError('Candidate must be fresh and must not contain its dry-run plan')
    weapon = {'p345-and-shot-decals': 'P345', 'glock-and-shot-decals': 'Glock',
              'revolver-and-shot-decals': 'Revolver', 'vp70-and-shot-decals': 'VP70',
              'tmp-and-shot-decals': 'TMP', 'mp5-and-shot-decals': 'MP5', 'm16-and-shot-decals': 'M16', 'g43-and-shot-decals': 'G43', 'shotgun-and-shot-decals': 'Shotgun', 'shot-decals': 'shot-decals'}[args.component]
    family = 'P345' if weapon == 'shot-decals' else weapon
    player_model = getattr(args, 'authored_player_model', None)
    if family == 'MP5':
        if player_model is None:
            raise ValueError('MP5 requires --authored-player-model; original W is an authoring input, not a P substitute')
        player_model = workspace_target(player_model)
    elif player_model is not None:
        raise ValueError('An authored player model is only reviewed for MP5')
    table_hash = current_manifest(family)
    assets, studio = [], []
    weapon_pins = {'P345': PINS, 'Glock': GLOCK_PINS, 'Revolver': REVOLVER_PINS,
                   'VP70': VP70_PINS, 'TMP': TMP_PINS, 'MP5': MP5_PINS, 'M16': M16_PINS, 'G43': G43_PINS, 'Shotgun': SHOTGUN_PINS}[family]
    pins = SHOT_DECAL_PINS if weapon == 'shot-decals' else weapon_pins + SHOT_DECAL_PINS
    if family=='Shotgun' and (original/'sound/weapons_new/syringe/syringe_sleeve.wav').exists():
        raise ValueError('Original missing shotgun draw request now exists; re-audit before authoring')
    for pin in pins:
        source_name = safe_relative(pin['original'])
        sound = pin['group'] in (family+'Sounds', family+'LocalWaves')
        source_relative = ('sound/' if sound else '') + source_name
        source = no_links(original / source_relative)
        source_kind = 'original-game'
        source_root = original
        if not source.is_file() and source_name == 'models/w_9mmclip.mdl':
            if inherited is None:
                raise ValueError('Original w_9mmclip is absent; pass --inherited-game-root explicitly')
            source_root, source_kind = inherited, 'explicit-inherited-game'
            source = no_links(inherited / source_relative)
        if not source.is_relative_to(source_root):
            raise ValueError('Source escapes its explicit root')
        if not source.is_file():
            raise ValueError('Missing original resource: ' + str(source))
        data, identity = checked_bytes(source, pin)
        destination = ('sound/' if pin['group'] == family+'Sounds' else '') + safe_relative(pin['host'])
        registered_name = pin['host'] if pin['group'] == family+'Sounds' else destination
        if len(registered_name.encode('utf-8')) >= 64:
            raise ValueError('Resource name exceeds actual engine MAX_QPATH: '+registered_name)
        if not any(destination.startswith(p) for p in (
                'models/guest/cof/', 'sprites/guest/cof/', 'sound/guest/cof/',
                'events/guest/cof/', 'inventoryitems/guest/cof/', 'aurora/guest/cof/', 'gfx/guest/cof/')):
            raise ValueError('Non-namespaced destination rejected')
        row = dict(group=pin['group'], original=pin['original'], destination=destination,
            source=str(source), sourceRoot=str(source_root), sourceKind=source_kind,
            bytes=len(data), sha256=pin['sha256'], sourceIdentity=identity,
            originalBytesPreserved=True)
        if source_name.endswith('.mdl'):
            events = model_events(data, source_name == 'models/w_9mmclip.mdl')
            if source_name == f'models/weapons/{family.lower()}/v_{family.lower()}.mdl':
                studio = [event for event in events if event['event'] == 5004]
            elif any(event['event'] == 5004 for event in events):
                raise ValueError('Additional model has unreviewed Studio5004 audio')
        if family == 'MP5' and pin['host'] == 'models/guest/cof/mp5/p_mp5.mdl':
            original_input = dict(source=str(source), bytes=len(data), sha256=pin['sha256'], sourceIdentity=identity)
            authored, authored_identity = checked_bytes(player_model, MP5_PLAYER_MODEL)
            model_events(authored)
            row.update(source=str(player_model), sourceRoot=str(BUILD),
                sourceKind='explicit-host-authored-player-model', bytes=len(authored),
                sha256=MP5_PLAYER_MODEL['sha256'], sourceIdentity=authored_identity,
                originalBytesPreserved=False, originalInput=original_input)
        assets.append(row)
        if source_name == 'models/w_9mmclip.mdl' and integer(data, 184) == 0:
            # Exact ReHLDS PrecacheModelTexture rule: replace .mdl with T.mdl.
            # This is a mandatory dependency from the same explicit VFS root.
            texture = no_links(source_root / CLIP_TEXTURE['original'])
            texture_data, texture_identity = checked_bytes(texture, CLIP_TEXTURE)
            if len(texture_data) < 244 or texture_data[:4] != b'IDST' or integer(texture_data, 4) != 10 or (
                    integer(texture_data, 72) != len(texture_data) or integer(texture_data, 180) != 1 or
                    integer(texture_data, 184) != 244):
                raise ValueError('Inherited clip texture companion layout differs')
            assets.append(dict(group=family+'Dependencies', original=CLIP_TEXTURE['original'],
                destination=CLIP_TEXTURE['host'], source=str(texture), sourceRoot=str(source_root),
                sourceKind=source_kind, bytes=len(texture_data), sha256=CLIP_TEXTURE['sha256'],
                sourceIdentity=texture_identity, originalBytesPreserved=True,
                consumer='ReHLDS PrecacheModelTexture: textureindex0 -> same-name T.mdl'))
    if len({a['destination'].lower() for a in assets}) != len(assets):
        raise ValueError('Duplicate destination')
    remote = {p['original']: p for p in weapon_pins if p['group'] == family+'Sounds'}
    local = {p['original']: p for p in weapon_pins if p['group'] == family+'LocalWaves'}
    expected = ([(4, 19, 'magout'), (4, 57, 'magplace'), (4, 63, 'magin'),
                 (5, 19, 'magout'), (5, 57, 'magplace'), (5, 63, 'magin'), (5, 81, 'slide')]
                if family == 'P345' else
                [(4, 0, 'cylinder_open'), *[(i, 13, 'insert') for i in range(5, 10)],
                 (10, 9, 'cylinder_close')] if family == 'Revolver' else
                [(6, 10, 'magout'), (6, 51, 'magin'), (7, 10, 'magout'), (7, 51, 'magin'),
                 (7, 76, 'cock')] if family == 'VP70' else
                [(2, 20, 'stock'), (5, 21, 'magout'), (5, 51, 'magin'),
                 (6, 21, 'magout'), (6, 51, 'magin'), (6, 75, 'charger_back'),
                 (6, 83, 'charger_forward')] if family == 'TMP' else
                [[3,17,"magout"],[3,49,"magin"],[3,66,"magtap"],[4,17,"magout"],[4,49,"magin"],[4,66,"magtap"],[4,90,"cock1"],[4,97,"cock2"],[6,8,"switch"],[10,17,"magout"],[10,49,"magin"],[10,66,"magtap"],[11,17,"magout"],[11,49,"magin"],[11,66,"magtap"],[11,90,"cock1"],[11,97,"cock2"],[13,7,"switch"],[32,7,"switch"],[33,7,"switch"]] if family == 'M16' else
                [(28,40,'magout'),(28,98,'magin'),(28,135,'cock'),(29,40,'magout'),(29,98,'magin')] if family == 'G43' else
                [(2, 30, 'magout'), (2, 67, 'magin'), (3, 17, 'charger_back'),
                 (3, 53, 'magout'), (3, 88, 'magin'), (3, 108, 'charger_forward')] if family == 'MP5' else
                [(4, 7, 'magout'), (4, 40, 'magplace'), (4, 52, 'magin'),
                 (5, 7, 'magout'), (5, 40, 'magplace'), (5, 52, 'magin'),
                 (5, 77, 'rackback'), (5, 82, 'slide'), (17, 9, 'switch'),
                 (18, 9, 'switch'), (29, 35, 'rackback'), (29, 44, 'slide')])
    shotgun_events=[(1,11,'weapons/shotgun/pump_back.wav'),(1,18,'weapons/shotgun/pump_forward.wav'),
        (3,3,'weapons/shotgun/insert.wav'),(5,0,'weapons/shotgun/pump_seq.wav'),
        (6,1,'weapons/syringe/syringe_sleeve.wav'),(16,11,'weapons/shotgun/pump_back.wav'),
        (16,17,'weapons/shotgun/pump_forward.wav')]
    if family=='Shotgun' and [(e['sequence'],e['frame'],e['options']) for e in studio]!=shotgun_events:
        raise ValueError('Original shotgun Studio5004 events differ')
    if family!='Shotgun' and args.component != 'shot-decals' and [(e['sequence'], e['frame'], e['options']) for e in studio] != [
            (s, f, f'weapons/{family.lower()}/{family.lower()}_'+name+'.wav') for s, f, name in expected]:
        raise ValueError('Original '+family+' Studio5004 event identity differs')
    for event in studio:
        key = 'weapons_new/' + event['options'].split('/', 1)[1]
        if family=='Shotgun' and key=='weapons_new/syringe/syringe_sleeve.wav':
            event.update(localLogicalKey=key,localAsset=None,remoteAsset=None,
                originalMissingLocalFile=True,originalDisposition='play2D null -> return without fallback',
                forbiddenAutomaticScanPath='sound/'+event['options'])
            continue
        event.update(localLogicalKey=key, localAsset=local[key]['host'],
            remoteAsset=remote[event['options']]['host'],
            forbiddenAutomaticScanPath='sound/' + event['options'])
    return dict(schema=1, weapon=weapon,
        scope='reviewed five shot decals only' if weapon == 'shot-decals' else
            f'reviewed {family} {len(weapon_pins)} resources and five shot decals',
        originalModuleSha256=ORIGINAL_MODULE_SHA256, output=str(output),
        sourceGameRoot=str(original), inheritedGameRoot=str(inherited) if inherited else None,
        resourceHeaderSha256=table_hash,
        toolSha256=digest(Path(__file__).read_bytes()),
        consumerSha256={p: digest((ROOT/p).read_bytes()) for p in CONSUMERS},
        assets=assets, primaryAssets=len(pins), dependencyAssets=len(assets)-len(pins), studio5004=studio,
        policy=dict(originalModelEventsPreserved=True, publicSoundAliases=False,
            serverAutoModelSoundsRequired=0, liveServerConfigurationVerified=False,
            localVoiceAdmission=False, particleAdmission=False, weaponBackendReady=False,
            playerModelGripAccepted=False, deployment=False,
            extraModelSidecarsAllowed=False,
            particleSpriteMapping={'sprites/fog2.spr': 'sprites/guest/cof/fog2.spr'}))


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def atomic_file(path, data):
    no_links(path)
    if path.exists():
        raise ValueError('Refusing to replace existing file: ' + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    no_links(path.parent)
    fd, temporary = tempfile.mkstemp(prefix='.'+path.name+'.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as file:
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        if digest(Path(temporary).read_bytes()) != digest(data):
            raise ValueError('Atomic file verification failed')
        if path.exists():
            raise ValueError('Destination appeared during preparation')
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def write_candidate(value):
    output = workspace_target(Path(value['output']))
    if output.exists():
        raise ValueError('Candidate already exists')
    output.parent.mkdir(parents=True, exist_ok=True)
    workspace_target(output)
    temporary = Path(tempfile.mkdtemp(prefix='.'+output.name+'.', dir=output.parent))
    published = False
    try:
        copied = []
        for row in value['assets']:
            source = no_links(Path(row['source']))
            data, identity = checked_bytes(source, row)
            if identity != row['sourceIdentity']:
                raise ValueError('Source changed since dry-run')
            destination = temporary / safe_relative(row['destination'])
            atomic_file(destination, data)
            actual = file_identity(destination)
            if (actual['device'], actual['fileId']) == (identity['device'], identity['fileId']) or destination.stat().st_nlink != 1:
                raise ValueError('Candidate must be an independent file, never a hard link')
            copied.append(dict(destination=row['destination'], sha256=digest(destination.read_bytes()),
                bytes=actual['size'], independent=True))
        manifest = dict(value, status='candidate-only', validation=dict(
            copied=copied, files=len(copied), bytes=sum(p['bytes'] for p in copied), independentCopies=True))
        atomic_file(temporary/'manifest.json', json_bytes(manifest))
        # A final source identity/hash gate catches mutation after an early copy.
        for row in value['assets']:
            _, identity = checked_bytes(no_links(Path(row['source'])), row)
            if identity != row['sourceIdentity']:
                raise ValueError('Source changed before publication')
            if 'originalInput' in row:
                original = row['originalInput']
                _, identity = checked_bytes(no_links(Path(original['source'])), original)
                if identity != original['sourceIdentity']:
                    raise ValueError('Player-model authoring input changed before publication')
        family = 'P345' if value['weapon'] == 'shot-decals' else value['weapon']
        if current_manifest(family) != value['resourceHeaderSha256'] or digest(Path(__file__).read_bytes()) != value['toolSha256'] or any(
                digest((ROOT/path).read_bytes()) != wanted for path, wanted in value['consumerSha256'].items()):
            raise ValueError('Resource contract/consumer changed before candidate publication')
        workspace_target(output)
        if output.exists():
            raise ValueError('Candidate appeared before publication')
        os.rename(temporary, output)
        published = True
        return manifest
    finally:
        if not published and temporary.exists():
            # Generated temporary tree only; never traverse a reparse point.
            resolved = workspace_target(temporary)
            for parent, directories, files in os.walk(resolved, followlinks=False):
                for name in directories + files:
                    no_links(Path(parent)/name)
            shutil.rmtree(resolved)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-game-root', type=Path, required=True)
    parser.add_argument('--component', choices=('p345-and-shot-decals', 'glock-and-shot-decals', 'revolver-and-shot-decals', 'vp70-and-shot-decals', 'tmp-and-shot-decals', 'mp5-and-shot-decals', 'm16-and-shot-decals', 'g43-and-shot-decals', 'shotgun-and-shot-decals', 'shot-decals'),
                        default='p345-and-shot-decals')
    parser.add_argument('--authored-player-model', type=Path,
        help='Explicit pinned host-authored MP5 P model, inside workspace build; never an original W copy')
    parser.add_argument('--inherited-game-root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--server-model-sounds', choices=('disabled', 'enabled'), required=True,
        help='Required deployment policy declaration; this tool does not inspect a live server')
    parser.add_argument('--write', action='store_true', help='Apply an already reviewed dry-run plan')
    parser.add_argument('--expected-plan-sha256')
    args = parser.parse_args(argv)
    value = plan(args)
    plan_path = workspace_target(args.plan)
    if args.write:
        if not args.expected_plan_sha256 or not re.fullmatch('[0-9a-f]{64}', args.expected_plan_sha256):
            raise ValueError('--write requires the dry-run --expected-plan-sha256')
        raw = plan_path.read_bytes()
        if digest(raw) != args.expected_plan_sha256 or json.loads(raw) != value:
            raise ValueError('Dry-run plan/hash no longer matches exact current inputs and consumers')
        result = write_candidate(value)
        print(json.dumps(dict(status='candidate-only', output=value['output'], files=len(value['assets']),
            manifestSha256=digest(json_bytes(result)), deployment=False)))
    else:
        if args.expected_plan_sha256:
            raise ValueError('Plan hash is only meaningful with --write')
        raw = json_bytes(value)
        atomic_file(plan_path, raw)
        print(json.dumps(dict(status='dry-run', plan=str(plan_path), planSha256=digest(raw),
            output=value['output'], files=len(value['assets']), candidateWritten=False)))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, struct.error) as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        raise SystemExit(2)
