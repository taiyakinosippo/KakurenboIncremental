# -*- coding: utf-8 -*-
# Fab からダウンロードした FBX（プレイヤーの見た目・宝石）を UE のアセットにする（エディタの Python で動かす）。
# Tools/ImportSourceArt.ps1 から呼ぶ。手でエディタを開いて取り込む必要はない。
#
#   KakurenboIncremental/SourceArt/Player/Male_002.fbx（＋ CharacterTexture_*.png）→ /Game/Player
#   KakurenboIncremental/SourceArt/Gem/Diamond_Shape2.fbx（＋ Diamond_Master2_*.jpeg / png）→ /Game/Gem
#
# どちらも Fab のアセットなので Git には入れない（.gitignore 済み）。別のパソコンでは FBX を同じ場所に置いてからこのスクリプトを動かす。
import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "SourceArt")
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[ImportSourceArt] " + msg)


def import_file(path, dest, name=None):
    if not os.path.exists(path):
        log("missing: " + path)
        return []
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    if name:
        task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    log("imported %s -> %s" % (os.path.basename(path), paths))
    return paths


def texture(path, dest, normal=False, linear=False):
    # FBX を取り込むと、FBX が指している画像も一緒に取り込まれることがある。あればそれを使う（同じ画像を 2 つ作らない）
    existing = dest + "/" + os.path.splitext(os.path.basename(path))[0]
    if eal.does_asset_exist(existing):
        tex = unreal.load_asset(existing)
    else:
        tex = None
        for p in import_file(path, dest):
            asset = unreal.load_asset(p)
            if isinstance(asset, unreal.Texture2D):
                tex = asset
    if tex and (normal or linear):
        if normal:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        eal.save_loaded_asset(tex)
    return tex


def new_material(name, dest):
    full = dest + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    return tools.create_asset(name, dest, unreal.Material, unreal.MaterialFactoryNew())


def sample(mat, tex, x, y, sampler=None):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, x, y)
    node.set_editor_property("texture", tex)
    if sampler is not None:
        node.set_editor_property("sampler_type", sampler)
    return node


def color_param(mat, name, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def scalar_param(mat, name, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def multiply(mat, a, a_out, b, b_out, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, a_out, node, "A")
    mel.connect_material_expressions(b, b_out, node, "B")
    return node


def import_gem():
    folder = os.path.join(SOURCE, "Gem")
    if not os.path.isdir(folder):
        log("no gem source")
        return
    dest = "/Game/Gem"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    # FBX には形の違う宝石がいくつも入っている（Circle_001…）。名前の札（Text_…）は使わないので消す
    imported = import_file(os.path.join(folder, "Diamond_Shape2.fbx"), dest)
    for p in imported:
        name = p.split("/")[-1].split(".")[0]
        asset = unreal.load_asset(p) if not name.startswith("Text") else None
        empty = isinstance(asset, unreal.StaticMesh) and (asset.get_bounding_box().max - asset.get_bounding_box().min).length() < 0.01
        if name.startswith("Text") or empty:
            eal.delete_asset(p.split(".")[0])

    texture(os.path.join(folder, "Diamond_Master2_BaseColor.jpeg"), dest)
    texture(os.path.join(folder, "Diamond_Master2_Roughness.jpeg"), dest, linear=True)
    texture(os.path.join(folder, "Diamond_Shape2_Diamond_Master2_Normal.png"), dest, normal=True)
    log("gem done")


def import_player():
    folder = os.path.join(SOURCE, "Player")
    if not os.path.isdir(folder):
        log("no player source")
        return
    dest = "/Game/Player"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    import_file(os.path.join(folder, "Male_002.fbx"), dest)
    texture(os.path.join(folder, "CharacterTexture_BaseColor.png"), dest)
    texture(os.path.join(folder, "CharacterTexture_Roughness.png"), dest, linear=True)
    log("player done")


def create_surface_material():
    # どのテクスチャでも使える共通のマテリアル（Fab のアセットは参照しないので Git に入れる）。
    # ゲームが実行時に Data/Surfaces.csv のテクスチャを差し込み、Color で色を付ける（壁が傷つくと暗く赤くなる、など）
    dest = "/Game/Kakurenbo/Materials"
    mat = new_material("M_KakuSurface", dest)
    # プレイヤー（スケルタルメッシュ）・床と壁の板（インスタンス）・宝石（Nanite）にも使うので、使い道の印を付けておく
    for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes", "used_with_nanite"):
        mat.set_editor_property(flag, True)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1500, 200)
    uv_scale = scalar_param(mat, "UVScale", 1.0, -1500, 350)
    uvs = multiply(mat, uv, "", uv_scale, "", -1250, 250)

    def tex_param(name, default_path, sampler, x, y):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", unreal.load_asset(default_path))
        node.set_editor_property("sampler_type", sampler)
        mel.connect_material_expressions(uvs, "", node, "UVs")
        return node

    base = tex_param("BaseColorTex", "/Engine/EngineResources/WhiteSquareTexture", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1000, -100)
    normal = tex_param("NormalTex", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1000, 500)
    rough = tex_param("RoughnessTex", "/Engine/EngineMaterials/BaseFlattenLinearColor", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, -1000, 250)
    tint = color_param(mat, "Color", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -1000, -300)
    colored = multiply(mat, base, "RGB", tint, "", -650, -200)
    mel.connect_material_property(colored, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough_scale = scalar_param(mat, "RoughnessScale", 1.0, -800, 400)
    rough_out = multiply(mat, rough, "R", rough_scale, "", -500, 300)
    mel.connect_material_property(rough_out, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    metal = scalar_param(mat, "Metallic", 0.0, -500, 100)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    glow = scalar_param(mat, "Emissive", 0.0, -650, 0)
    emissive = multiply(mat, colored, "", glow, "", -350, 0)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("surface material done")


def report():
    for folder in ("/Game/Gem", "/Game/Player", "/Game/Kakurenbo"):
        for path in eal.list_assets(folder, recursive=True):
            asset = unreal.load_asset(path)
            log("asset %s (%s)" % (path, asset.get_class().get_name() if asset else "?"))


import sys
if "surface" in sys.argv[-1:] or "--surface-only" in sys.argv:
    create_surface_material()
else:
    import_gem()
    import_player()
    create_surface_material()
report()
log("done")