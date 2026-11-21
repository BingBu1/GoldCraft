#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the two firearm and five melee resource sets from an owned CoF installation."""
import argparse
import importlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile

from csol_hud_resources import contract, dependencies
from cryoffear_studio import Studio, inverse, multiply

models = importlib.import_module("Generate-CryOfFearPlayerModel")
cards = importlib.import_module("Prepare-CryOfFearPresentation")
grips = importlib.import_module("Prepare-CryOfFearLightEquipment")
ROOT = Path(__file__).resolve().parents[1]
KEYS = ("browning", "rifle", "axe", "branch", "nightstick", "sledgehammer", "switchblade")
DESCRIPTIONS = (
    "13 发弹匣，使用副武器弹药补给。", "5 发弹仓，逐发装填。开镜使用 +cof_aim，右键推搡。",
    "挥斧攻击。", "左键挥击，右键推击。", "左键挥击。", "挥动十字镐消耗 8 点体力。",
    "右键切换握法，左键按当前握法出刀。",
)


def relocate_events(data, sources):
    """Keep studio sequence timing, redirect the native 5004 sound callback."""
    view = bytearray(data)
    sequences, offset = struct.unpack_from("<ii", view, 164)
    for i in range(sequences):
        count, start = struct.unpack_from("<ii", view, offset + i * 176 + 48)
        for j in range(count):
            row = start + j * 76
            if struct.unpack_from("<i", view, row + 4)[0] != 5004:
                continue
            field = row + 12
            sound = bytes(view[field:field + 64]).split(b"\0")[0].decode("ascii")
            if "sound/" + sound not in sources:
                raise ValueError("Unpinned weapon sequence sound: " + sound)
            renamed = ("guest/cof/" + sound).encode("ascii")
            if len(renamed) >= 64:
                raise ValueError("Studio sound path too long")
            view[field:field + 64] = renamed.ljust(64, b"\0")
    return bytes(view)


def player_model(key, view, player, template, recipe):
    # Align the original gripping fingers with the matching standing CS hand.
    # Only triangles bound to the weapon are emitted, even when its texture
    # is shared with the original arms.
    pistol = key == "browning"
    fingers = (["r_wrist", "r_index_00", "r_thumb_00", "r_index_02", "r_thumb_01"] if pistol
               else ["r_wr", "r_in1", "r_th1", "r_in3", "r_th3"])
    extension = "ref_aim_onehanded" if pistol else "ref_aim_knife"
    standing = next(i for i in range(player.i(164)) if player.string(player.i(168) + i*176, 32) == extension)
    hand = "Bip01 R Hand"
    host_hand, host_grip = grips.hand_frame(player, standing,
        [hand, "Bip01 R Finger1", "Bip01 R Finger0", "Bip01 R Finger11", "Bip01 R Finger01"])
    sequence = 1
    _, source_grip = grips.hand_frame(view, sequence, fingers)
    weapon_part = "ref_gun" if pistol else "ref_axe"
    part = next(p for g in view.bodyparts for p in g["models"] if p["name"] == weapon_part)
    bones = sorted({view.bones[i]["name"] for i in part["verticesBones"]})
    anchor = "browning_frame" if pistol else "axe"
    pose = view.sample(sequence, 0, 0)
    matrix = multiply(inverse(host_hand), multiply(host_grip, multiply(inverse(source_grip), pose[view.bone_index(anchor)])))
    textures = {view.skins[0][mesh["skinref"]] for mesh in part["meshes"]}
    calibration = dict(schema=1, status="verified", attachments="all",
        evidence=dict(path=str(recipe),sha256=models.digest(recipe.read_bytes())),
        source=dict(sha256=view.sha256, bone=anchor, sequence=dict(index=sequence,label="idle",frame=0,blend=0)),
        target=dict(sha256=template.sha256, bone=hand, handBone=hand, binding="hand",
                    sequence=dict(index=0,label="idle",frame=0,blend=0)),
        sourceVertexBones=bones, textures=[dict(name=view.textures[i]["name"],sha256=view.textures[i]["sha256"])
                                          for i in sorted(textures)], sourceAnchorToTargetBone=matrix)
    output, receipt = models.generate(view, template, calibration)
    if receipt["selectedTriangles"] != sum(len(m["triangles"]) for m in part["meshes"]):
        raise ValueError("Incomplete third-person weapon mesh: " + key)
    return output


def card(files, key, data, ammo):
    width, height, pixels = cards.tga32(data)
    image = cards.fit(width, height, pixels, 170, 80)
    files[f"gfx/csol/buy/cof_{key}.tga"] = cards.hud.tga(170, 80, image)
    files[f"resource/csol/hud/v1/cof_{key}_weapon.tga"] = cards.hud.tga(170, 45, cards.fit(width, height, pixels, 170, 45))
    files[f"sprites/guest/cof/c{key}.spr"] = cards.native_sprite(170, 80, image)
    rows = [f"{slot} {resolution} guest/cof/c{key} 0 0 170 80"
            for resolution in (320, 640) for slot in ("weapon", "weapon_s")]
    if ammo:
        frames = cards.hud.decode_sprite_frames(ammo)
        if key == "browning":
            if len(frames) != 14:
                raise ValueError("Expected the original 0..13 Browning magazine frames")
            # This card is static. The current clip and reserve use CS inventory messages.
            w, h, rgba, _ = max(frames, key=lambda f: sum(f[2][3::4]))
        else:
            w, h, rgba, _ = cards.hud.decode_sprite(ammo)
        icon = cards.fit(w, h, rgba, 32, 32)
        files[f"resource/csol/hud/v1/cof_{key}_ammo.tga"] = cards.hud.tga(32, 32, icon)
        files[f"sprites/guest/cof/{key}_ammo.spr"] = cards.native_sprite(32, 32, icon)
        rows.extend(f"ammo {res} guest/cof/{key}_ammo 0 0 32 32" for res in (320, 640))
    files[f"sprites/weapon_cof_{key}.txt"] = ("\n".join([str(len(rows)), *rows])+"\n").encode("ascii")


def prepare(original, cstrike, output):
    original, cstrike, output = map(models.ordinary, (original, cstrike, output))
    if output.exists() or ROOT / "build" not in output.parents or any(p in output.parents for p in (original, cstrike)):
        raise ValueError("Supply a new build directory outside both input trees")
    pins = json.loads((ROOT/"tools/cryoffear_arsenal_sources.json").read_bytes())
    source = {}
    for rel, digest in pins.items():
        path = models.ordinary(original/rel)
        if not path.is_relative_to(original):
            raise ValueError("Unexpected input location")
        data = path.read_bytes()
        if models.digest(data) != digest:
            raise ValueError("CoF input changed: "+rel)
        source[rel] = data

    player = Studio(models.ordinary(cstrike/"models/player/gign/gign.mdl").read_bytes(), opaque_controllers=True)
    template = Studio.read(models.ordinary(cstrike/"models/p_elite.mdl"))
    if player.sha256 != grips.PLAYER or template.sha256 != grips.TEMPLATE:
        raise ValueError("CS player grip reference changed")
    header = (ROOT/"native/guest/cof/arsenal.hpp").read_text("utf8")
    menu_header = (ROOT/"native/csol/buy_menu.hpp").read_text("utf8")
    def menu_file(symbol):
        return re.search(r'\b'+symbol+r'\[\]="([^"]+)";', menu_header)[1]
    hud_constants, hud_images = contract(ROOT)
    hud_path = hud_constants["CatalogPath"]
    hud = json.loads(models.ordinary(cstrike/hud_path).read_bytes())
    menu = json.loads(models.ordinary(cstrike/menu_file("EquipmentPresentationResource")).read_bytes())
    if menu["version"] not in (3, 4):
        raise ValueError("Expected weapon detail cards")
    first_id = int(re.search(r"enum Id\s*\{\s*Browning=(\d+)", header)[1])
    files = {}
    for index, key in enumerate(KEYS):
        definition = re.search(r'Definition\{\w+,"'+key+r'","([^"]+)","([^"]+)"', header)
        if not definition:
            raise ValueError("Missing host definition for "+key)
        classname, title = definition.groups()
        inv = "weapon_browning_wheelchair" if key == "browning" else "weapon_"+key
        metadata = source[f"inventoryitems/weapons/{inv}.txt"].decode("utf8")
        picture = re.search(r'"inventory_picture"\s*\{\s*data\s*"([^"]+)"', metadata, re.I)[1].lower()
        ammo = {"rifle": "sprites/cof_rifle_mag_icon.spr","browning": "sprites/cof_browning_mag.spr"}.get(key)
        card(files, key, source[picture], source[ammo] if ammo else None)
        rel = f"models/weapons/{key}/"
        dst = f"models/guest/cof/{key}/"
        name = "wheelchair_browning" if key == "browning" else key
        view = source[rel + "v_" + name + ".mdl"]
        files[dst+"v_"+key+".mdl"] = relocate_events(view, source)
        files[dst+"w_"+key+".mdl"] = source[rel+"w_"+key+".mdl"]
        if key in ("axe", "browning"):
            files[dst+"p_"+key+".mdl"] = player_model(key, Studio(view), player, template, Path(__file__).resolve())
        else:
            files[dst+"p_"+key+".mdl"] = source["police/"+rel+"p_"+key+".mdl"]
        if key == "rifle":
            files[dst+"v_rifle_scope.mdl"] = relocate_events(source[rel+"v_rifle_scope.mdl"], source)
        weapon_art = f"resource/csol/hud/v1/cof_{key}_weapon.tga"
        ammo_art = f"resource/csol/hud/v1/cof_{key}_ammo.tga" if ammo else ""
        hud_row = dict(name=classname, title=title, weapon=weapon_art, selected=weapon_art, ammo=ammo_art, ammo2="")
        matching = [i for i, row in enumerate(hud["weapons"]) if row["name"] == classname]
        if len(matching) > 1:
            raise ValueError("Duplicate HUD entry for " + classname)
        if matching:
            hud["weapons"][matching[0]] = hud_row
        else:
            hud["weapons"].append(hud_row)
        ident = first_id+index
        for rows, row in [(menu["images"], dict(id=ident,path=f"gfx/csol/buy/cof_{key}.tga",width=170,height=80)),
                          (menu["weaponInfo"]["items"],dict(id=ident,itemId=ident,name=title,description=DESCRIPTIONS[index],
                            table=False,values=[""]*10,grade=[-1]*3))]:
            if any(old["id"] == ident for old in rows):
                raise ValueError(f"Purchase ID {ident} already exists in the baseline")
            rows.append(row)
    for name, data in source.items():
        if name.startswith("sound/"):
            files["sound/guest/cof/"+name[6:]] = data
    for shared in ("shell.mdl","556shell.mdl"):
        files["models/guest/cof/common/"+shared] = source["models/"+shared]
    files["events/guest/cof/viewpunch.sc"] = source["events/viewpunch.sc"]
    files["sprites/guest/cof/bubble.spr"] = source["sprites/bubble.spr"]
    files["configs/guest/cof/arsenal_damage.json"] = (ROOT/"native/guest/cof/arsenal_damage.json").read_bytes()
    files[menu_file("ArsenalPresentationResource")] = json.dumps(menu,ensure_ascii=False,separators=(",",":")).encode("utf8")
    files[hud_path] = json.dumps(hud, ensure_ascii=False, separators=(",", ":")).encode("utf8")
    for name in dependencies(files[hud_path], hud_constants, hud_images):
        if name not in files and not models.ordinary(cstrike/name).is_file():
            raise ValueError("Missing base HUD resource " + name)
    output.parent.mkdir(parents=True,exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=output.name+"-",dir=output.parent))
    try:
        rows = []
        for name, data in sorted(files.items()):
            dest = stage/name
            dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(data)
            rows.append(dict(source=str(output/name),destination=name,bytes=len(data),sha256=models.digest(data)))
        (stage/"resource-plan.json").write_text(json.dumps(dict(files=rows),indent=2),"utf8")
        cards.commit_output(stage,output)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return len(files)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("original","cstrike","output"):
        parser.add_argument("--"+option, type=Path, required=True)
    args = parser.parse_args()
    print(f"{prepare(args.original,args.cstrike,args.output)} resources prepared")
