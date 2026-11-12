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


def current_manifest(weapon='P345'):
    if weapon not in ('P345', 'Glock'):
        raise ValueError('Unreviewed weapon resource manifest')
    text = HEADER.read_text('utf-8')
    actual = []
    for suffix in ('Models', 'Sounds', 'LocalWaves', 'Events'):
        group = weapon + suffix
        source = group
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
        if group == 'GlockParticle':
            if not re.search(r'\bGlockParticle\s*=\s*P345Particle\s*;', text):
                raise ValueError('Unreviewed Glock particle alias')
            source = 'P345Particle'
        match = re.search(r'\b' + source + r'\s*\{\s*"([^"]+)",\s*"([^"]+)"\s*\};', text)
        if not match:
            raise ValueError('Authoritative resource path missing: ' + group)
        actual.append((group, *match.groups()))
    pins = PINS if weapon == 'P345' else GLOCK_PINS
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
    if weapon == 'Glock' and not re.search(r'\bGlockClipTexture\s*=\s*P345ClipTexture\s*;', text):
        raise ValueError('Unreviewed Glock clip texture alias')
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
              'shot-decals': 'shot-decals'}[args.component]
    family = 'P345' if weapon == 'shot-decals' else weapon
    table_hash = current_manifest(family)
    assets, studio = [], []
    weapon_pins = PINS if family == 'P345' else GLOCK_PINS
    pins = SHOT_DECAL_PINS if weapon == 'shot-decals' else weapon_pins + SHOT_DECAL_PINS
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
                [(4, 7, 'magout'), (4, 40, 'magplace'), (4, 52, 'magin'),
                 (5, 7, 'magout'), (5, 40, 'magplace'), (5, 52, 'magin'),
                 (5, 77, 'rackback'), (5, 82, 'slide'), (17, 9, 'switch'),
                 (18, 9, 'switch'), (29, 35, 'rackback'), (29, 44, 'slide')])
    if args.component != 'shot-decals' and [(e['sequence'], e['frame'], e['options']) for e in studio] != [
            (s, f, f'weapons/{family.lower()}/{family.lower()}_'+name+'.wav') for s, f, name in expected]:
        raise ValueError('Original '+family+' Studio5004 event identity differs')
    for event in studio:
        key = 'weapons_new/' + event['options'].split('/', 1)[1]
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
    parser.add_argument('--component', choices=('p345-and-shot-decals', 'glock-and-shot-decals', 'shot-decals'),
                        default='p345-and-shot-decals')
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
