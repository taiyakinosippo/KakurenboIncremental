# -*- coding: utf-8 -*-
# Fab の大きなパック（StylizedKitchen 1.2 GB・Isometric-Interiors）から、ゲームで使う家具・飾り・模様だけを取り出して
# KakurenboIncremental/SourceArt/FabProps に置く（FBX と 1K の PNG と manifest.json）。パックそのものは Git に入れない。
# 取り出した物は Tools/ImportSourceArt.ps1 が /Game/Kakurenbo/Mansion/Props に取り込む（Medieval Room Kit と同じやり方）。
#
# パックがあるパソコンで 1 回だけ動かせばよい（結果の SourceArt/FabProps は Git に入っている）。2 段階：
#   1) エディタの Python（パックを読む）：
#      UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<このファイルの絶対パス>"
#      → SourceArt/FabProps/FBX/*.fbx・Raw/*.png・manifest.json
#   2) ふつうの Python（Pillow で 1K に縮め、粗さのテクスチャを ORM の並びにそろえる）：
#      python Tools/ExtractFabProps.py --shrink
#      → SourceArt/FabProps/Textures/*.png（Raw は消す）
import json
import os
import sys

# 使うメッシュ（Furniture.csv・Decor.csv から使っている物）
MESHES = [
    "/Game/StylizedKitchen/Meshes/SM_Bookshelf",
    "/Game/StylizedKitchen/Meshes/SM_Desk02",
    "/Game/StylizedKitchen/Meshes/SM_Desk",
    "/Game/StylizedKitchen/Meshes/SM_LeatherCouch",
    "/Game/StylizedKitchen/Meshes/SM_Bathtub",
    "/Game/StylizedKitchen/Meshes/SM_Bathroom_Sink",
    "/Game/StylizedKitchen/Meshes/SM_TableBasse_01",
    "/Game/StylizedKitchen/Meshes/SM_SmallTable_01",
    "/Game/StylizedKitchen/Meshes/SM_Picture_01",
    "/Game/StylizedKitchen/Meshes/SM_Picture_03",
    "/Game/StylizedKitchen/Meshes/SM_Frames_01",
    "/Game/StylizedKitchen/Meshes/SM_Door_Frame",
    "/Game/Isometric-Interiors/Meshes/SM_Pot_Flower_1",
]
# 床・壁の模様（Surfaces.csv から使っている物）。名前 → 種類（base / normal）
SURFACES = {
    "T_Bathroom_Floor_Tile_D": "base", "T_Bathroom_Floor_Tile_N": "normal",
    "T_Wallpaper_A_D": "base", "T_Wallpaper_A_N": "normal",
    "T_Wallpaper_B_D": "base", "T_Wallpaper_B_N": "normal",
    "T_Plaster_Wall_01_D": "base", "T_Plaster_Wall_02_N": "normal",
    "T_Stucco_Wall_01_D": "base", "T_Stucco_Wall_01_N": "normal",
}
SURFACE_FOLDER = "/Game/StylizedKitchen/Textures/"
# マテリアルからテクスチャが分からないメッシュの色のテクスチャ（Isometric の色の表）
EXTRA_BASE = {"SM_Pot_Flower_1": "/Game/Isometric-Interiors/Textures/Texture_Atlas"}
MAX_SIZE = 1024

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "KakurenboIncremental", "SourceArt", "FabProps")


def extract():
    import unreal
    mel = unreal.MaterialEditingLibrary

    def log(msg):
        unreal.log("[ExtractFabProps] " + msg)

    os.makedirs(os.path.join(OUT, "FBX"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "Raw"), exist_ok=True)

    def export(asset, filename):
        task = unreal.AssetExportTask()
        task.object = asset
        task.filename = filename
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        if filename.endswith(".fbx"):
            task.options = unreal.FbxExportOption()
        ok = unreal.Exporter.run_asset_export_task(task)
        log("export %s -> %s (%s)" % (asset.get_name(), os.path.basename(filename), ok))
        return ok

    exported = {}

    def tex(t):
        # テクスチャを Raw/<名前>.png に書き出す（同じ物は 1 回だけ）
        if not t:
            return ""
        name = t.get_name()
        if name not in exported:
            exported[name] = export(t, os.path.join(OUT, "Raw", name + ".png"))
        return name if exported[name] else ""

    manifest = {"meshes": {}, "surfaces": {}}
    for path in MESHES:
        mesh = unreal.load_asset(path)
        if not mesh:
            log("missing " + path)
            continue
        name = mesh.get_name()
        export(mesh, os.path.join(OUT, "FBX", name + ".fbx"))
        slots = []
        for sm in mesh.static_materials:
            mat = sm.material_interface
            slot = {"name": str(sm.material_slot_name), "base": "", "normal": "", "orm": "", "packing": "orm", "color": [1.0, 1.0, 1.0], "glass": False}
            if mat:
                parent = mat.get_base_material().get_name() if mat.get_base_material() else ""
                slot["glass"] = "Glass" in parent or "Glass" in mat.get_name()
                if isinstance(mat, unreal.MaterialInstance):
                    params = {str(p): mel.get_material_instance_texture_parameter_value(mat, p) for p in mel.get_texture_parameter_names(mat)}
                    # MM_Base01: BaseColor / NORMAL / ORM（R=AO G=粗さ B=金属）。MM_Basic: Base_Color / Normal / Metal/Rough/Emissive
                    slot["base"] = tex(params.get("BaseColor") or params.get("Base_Color"))
                    slot["normal"] = tex(params.get("NORMAL") or params.get("Normal"))
                    if params.get("ORM"):
                        slot["orm"] = tex(params.get("ORM"))
                    elif params.get("Metal/Rough/Emissive"):
                        slot["orm"] = tex(params.get("Metal/Rough/Emissive"))
                        slot["packing"] = "mre"
                    if "Color" in [str(p) for p in mel.get_vector_parameter_names(mat)]:
                        c = mel.get_material_instance_vector_parameter_value(mat, "Color")
                        slot["color"] = [c.r, c.g, c.b]
                elif isinstance(mat, unreal.Material):
                    # マテリアルそのもの（パラメータの既定値のテクスチャ）。例：Isometric の M_Texture_Atlas（色の表）
                    for t in mel.get_used_textures(mat):
                        lower = t.get_name().lower()
                        if lower.endswith("_n") or "normal" in lower:
                            slot["normal"] = slot["normal"] or tex(t)
                        elif "orm" in lower or "mre" in lower or "rough" in lower:
                            pass
                        else:
                            slot["base"] = slot["base"] or tex(t)
            if not slot["base"] and name in EXTRA_BASE:
                slot["base"] = tex(unreal.load_asset(EXTRA_BASE[name]))
            slots.append(slot)
            log("  %s slot %s: %s" % (name, slot["name"], slot))
        box = mesh.get_bounding_box()
        size = box.max - box.min
        manifest["meshes"][name] = {"source": path, "slots": slots, "size": [size.x, size.y, size.z]}
    for name, kind in SURFACES.items():
        t = unreal.load_asset(SURFACE_FOLDER + name)
        if t and tex(t):
            manifest["surfaces"][name] = kind
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    log("done")


def shrink():
    from PIL import Image
    raw = os.path.join(OUT, "Raw")
    dest = os.path.join(OUT, "Textures")
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(OUT, "manifest.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    packing = {}
    normals = {k for k, v in manifest["surfaces"].items() if v == "normal"}
    for mesh in manifest["meshes"].values():
        for slot in mesh["slots"]:
            if slot["orm"]:
                packing[slot["orm"]] = slot["packing"]
            if slot["normal"]:
                normals.add(slot["normal"])
    for file in sorted(os.listdir(raw)):
        name, ext = os.path.splitext(file)
        im = Image.open(os.path.join(raw, file))
        im = im.convert("RGBA" if im.mode in ("RGBA", "LA", "P") and name not in normals and name not in packing else "RGB")
        if max(im.size) > MAX_SIZE:
            scale = MAX_SIZE / max(im.size)
            im = im.resize((max(1, round(im.size[0] * scale)), max(1, round(im.size[1] * scale))), Image.LANCZOS)
        if packing.get(name) == "mre":
            # Metal/Rough/Emissive → ORM（R=AO は 1、G=粗さ、B=金属）
            r, g, b = im.convert("RGB").split()
            im = Image.merge("RGB", (Image.new("L", im.size, 255), g, r))
        im.save(os.path.join(dest, name + ".png"), optimize=True)
        print("shrink %s %s -> %s" % (file, packing.get(name, ""), im.size))
    # 粗さのテクスチャが無い物に使う ORM（遮蔽なし・ざらざら 0.8・金属でない）。M_KakuProp の既定のテクスチャにもなる
    Image.new("RGB", (8, 8), (255, 204, 0)).save(os.path.join(dest, "T_KakuDefaultORM.png"))
    for file in os.listdir(raw):
        os.remove(os.path.join(raw, file))
    os.rmdir(raw)
    print("done")


if __name__ == "__main__":
    if "--shrink" in sys.argv:
        shrink()
    else:
        extract()
