# -*- coding: utf-8 -*-
# Fab からダウンロードした FBX（プレイヤーの見た目・宝石）を UE のアセットにする（エディタの Python で動かす）。
# Tools/ImportSourceArt.ps1 から呼ぶ。手でエディタを開いて取り込む必要はない。
#
#   KakurenboIncremental/SourceArt/Player/Male_002.fbx（＋ CharacterTexture_*.png）→ /Game/Player
#   KakurenboIncremental/SourceArt/Gem/Diamond_Shape2.fbx（＋ Diamond_Master2_*.jpeg / png）→ /Game/Gem
#   KakurenboIncremental/SourceArt/Cat/*.fbx（Fab「Cat long haired」。モデルと歩く・走るアニメーション＋画像）→ /Game/Cat（--cat-only でこれだけ）
#   KakurenboIncremental/SourceArt/MedievalRoomKit（FBX＋1K の画像）→ /Game/Kakurenbo/Mansion/Medieval（--medieval-only でこれだけ）
#   KakurenboIncremental/SourceArt/FabProps（Tools/ExtractFabProps.py が取り出した FBX＋1K の画像＋manifest.json）→ /Game/Kakurenbo/Mansion/Props（--props-only でこれだけ）
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


def texture(path, dest, normal=False, linear=False, max_size=0):
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
    if tex and max_size > 0:
        # 8K の写真のテクスチャはゲームには大きすぎるので、使う大きさを抑える（元の画像は SourceArt に残る）
        tex.set_editor_property("max_texture_size", max_size)
        eal.save_loaded_asset(tex)
    if tex and (normal or linear):
        if normal:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        eal.save_loaded_asset(tex)
    return tex


def new_material(name, dest):
    full = dest + "/" + name
    if eal.does_asset_exist(full):
        # 作り直すときは、中のノードを全部消してから組み直す（同じアセットを使い続ける）
        mat = unreal.load_asset(full)
        mel.delete_all_material_expressions(mat)
        return mat
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


def import_cat():
    # Fab「Cat long haired」：FBX（モデル＋歩く・走るアニメーション）。ファイル名はパックによって違うので、フォルダの中を全部見る。
    # 一番大きい FBX をモデルとして取り込み、残りの FBX はそのモデルの骨のアニメーションとして取り込む。
    # 名前は決めておく（DefaultGame.ini の CatMesh・CatAnimWalk・CatAnimRun）：SK_Cat / A_CatWalk / A_CatRun
    folder = os.path.join(SOURCE, "Cat")
    if not os.path.isdir(folder):
        log("no cat source")
        return
    dest = "/Game/Cat"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    fbxs = []
    for root, _, files in os.walk(folder):
        for f in files:
            if f.lower().endswith(".fbx"):
                fbxs.append(os.path.join(root, f))
    if not fbxs:
        log("no cat fbx")
        return
    fbxs.sort(key=lambda f: -os.path.getsize(f))
    main = fbxs[0]

    def options(skeleton=None):
        opt = unreal.FbxImportUI()
        opt.import_mesh = skeleton is None
        opt.import_as_skeletal = True
        opt.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
        opt.import_animations = True
        opt.import_materials = skeleton is None
        opt.import_textures = skeleton is None
        if skeleton:
            opt.skeleton = skeleton
        opt.automated_import_should_detect_type = False
        return opt

    task = unreal.AssetImportTask()
    task.filename = main
    task.destination_path = dest
    task.destination_name = "SK_Cat"
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options()
    tools.import_asset_tasks([task])
    imported = list(task.imported_object_paths)
    log("cat main %s -> %s" % (os.path.basename(main), imported))
    mesh = None
    for path in imported:
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.SkeletalMesh):
            mesh = asset
    if not mesh:
        log("cat: no skeletal mesh in " + main)
        return
    skeleton = mesh.skeleton
    for other in fbxs[1:]:
        t = unreal.AssetImportTask()
        t.filename = other
        t.destination_path = dest
        t.automated = True
        t.replace_existing = True
        t.save = True
        t.options = options(skeleton)
        tools.import_asset_tasks([t])
        log("cat anim %s -> %s" % (os.path.basename(other), list(t.imported_object_paths)))

    # アニメーションの名前をそろえる（walk / run が入っている名前。無ければ最初のものを歩く）
    anims = [a for a in eal.list_assets(dest, recursive=True) if isinstance(unreal.load_asset(a), unreal.AnimSequence)]
    def find(word):
        for a in anims:
            if word in a.split("/")[-1].lower():
                return a
        return None
    walk = find("walk") or (anims[0] if anims else None)
    run = find("run") or walk
    for src, name in ((walk, "A_CatWalk"), (run, "A_CatRun")):
        if src and src.split("/")[-1].split(".")[0] != name and not eal.does_asset_exist(dest + "/" + name):
            if src == walk and run == walk and name == "A_CatRun":
                eal.duplicate_asset(dest + "/A_CatWalk", dest + "/" + name)
            else:
                eal.rename_asset(src.split(".")[0], dest + "/" + name)
    # 画像が FBX と同じフォルダにあれば取り込み、マテリアルに付いていなければ色の画像を付ける
    for root, _, files in os.walk(folder):
        for f in files:
            if f.lower().endswith((".png", ".jpg", ".jpeg", ".tga")):
                low = f.lower()
                texture(os.path.join(root, f), dest, normal="normal" in low or "_n." in low, linear="rough" in low)
    eal.save_directory(dest)
    log("cat done: anims %s" % [a.split("/")[-1] for a in eal.list_assets(dest, recursive=True) if isinstance(unreal.load_asset(a), unreal.AnimSequence)])


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


def import_quiet_wall():
    # 消音壁の見た目（Fab「Stucco Wall」。しっくい壁の写真のテクスチャを 2K に縮めたもの）
    folder = os.path.join(SOURCE, "QuietWall")
    if not os.path.isdir(folder):
        log("no quiet wall source")
        return
    dest = "/Game/QuietWall"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    texture(os.path.join(folder, "Stucco_Wall_vigrejf_2K_BaseColor.jpg"), dest)
    texture(os.path.join(folder, "Stucco_Wall_vigrejf_2K_Normal.png"), dest, normal=True)
    texture(os.path.join(folder, "Stucco_Wall_vigrejf_2K_Roughness.jpg"), dest, linear=True)
    log("quiet wall done")


def import_wood():
    # 床・壁・木の壁の木の模様（Fab「Substance Materials Vol 01 - Wood」の 16 種類を 1K に縮めたもの。元のパックは 2.8GB あるので使わない）
    folder = os.path.join(SOURCE, "Wood")
    if not os.path.isdir(folder):
        log("no wood source")
        return
    dest = "/Game/Kakurenbo/Wood"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    for i in range(1, 17):
        name = "Wood%02d" % i
        texture(os.path.join(folder, name + "_Base_Color.jpg"), dest)
        texture(os.path.join(folder, name + "_Normal.png"), dest, normal=True)
        texture(os.path.join(folder, name + "_Roughness.jpg"), dest, linear=True)
    log("wood done")

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
    # 模様の色を抜く（Desaturate 0〜1）。色の付いた木を「白く塗った板」などにしてから Color を掛ける
    desat = mel.create_material_expression(mat, unreal.MaterialExpressionDesaturation, -800, -100)
    mel.connect_material_expressions(base, "RGB", desat, "")
    desat_amount = scalar_param(mat, "Desaturate", 0.0, -1000, -150)
    mel.connect_material_expressions(desat_amount, "", desat, "Fraction")
    colored = multiply(mat, desat, "", tint, "", -650, -200)
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


def create_smoke_material():
    # 煙幕の球に使う半透明のマテリアル（縁ほど薄く、床と交わる所もぼかす）。
    # ゲームが球ごとに Opacity を変える（カメラの近く・カメラとプレイヤーの間の球は薄くして、自分の姿と行く先が見えるように）
    dest = "/Game/Kakurenbo/Materials"
    mat = new_material("M_KakuSmoke", dest)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    color = color_param(mat, "Color", unreal.LinearColor(0.55, 0.55, 0.6, 1.0), -900, -250)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # 夜の暗い所でも見えるよう、少しだけ自分で光る
    glow = scalar_param(mat, "Glow", 0.12, -900, -50)
    emissive = multiply(mat, color, "", glow, "", -600, -100)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = scalar_param(mat, "Opacity", 0.7, -1200, 250)
    fresnel = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -1200, 450)
    fresnel.set_editor_property("exponent", 2.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.0)
    edge = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -950, 450)
    mel.connect_material_expressions(fresnel, "", edge, "")
    soft = multiply(mat, opacity, "", edge, "", -750, 350)
    depth = mel.create_material_expression(mat, unreal.MaterialExpressionDepthFade, -550, 400)
    depth.set_editor_property("fade_distance_default", 60.0)
    ok = mel.connect_material_expressions(soft, "", depth, "Opacity")
    mel.connect_material_property(depth, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("smoke material done (depth fade connected %s)" % ok)


def create_prop_material():
    # 小物・家具の共通のマテリアル（色・ノーマル・ORM＝R 遮蔽・G ざらざら・B 金属・光る模様）。
    # Medieval Room Kit の家具（FBX）に使う。テクスチャはマテリアルインスタンスで差し替える
    dest = "/Game/Kakurenbo/Materials"
    mat = new_material("M_KakuProp", dest)
    def tex_param(name, default_path, sampler, x, y):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", unreal.load_asset(default_path))
        node.set_editor_property("sampler_type", sampler)
        return node
    base = tex_param("BaseColorTex", "/Engine/EngineResources/WhiteSquareTexture", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, -200)
    normal = tex_param("NormalTex", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -900, 400)
    # ORM の既定：遮蔽なし・ざらざら 0.8・金属でない小さな画像（エンジンの BaseFlattenLinearColor は "HLOD" の文字が写る）
    default_orm = "/Engine/EngineMaterials/BaseFlattenLinearColor"
    orm_png = os.path.join(SOURCE, "FabProps", "Textures", "T_KakuDefaultORM.png")
    if os.path.exists(orm_png) and texture(orm_png, dest, linear=True):
        default_orm = dest + "/T_KakuDefaultORM"
    orm = tex_param("ORMTex", default_orm, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, -900, 100)
    glow_tex = tex_param("EmissiveTex", "/Engine/EngineResources/Black", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, 700)
    tint = color_param(mat, "Color", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -900, -400)
    colored = multiply(mat, base, "RGB", tint, "", -600, -300)
    mel.connect_material_property(colored, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(orm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(orm, "B", unreal.MaterialProperty.MP_METALLIC)
    glow = scalar_param(mat, "Emissive", 0.0, -900, 900)
    emissive = multiply(mat, glow_tex, "RGB", glow, "", -600, 750)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("prop material done")
    return mat


# Medieval Room Kit（Fab・CC BY 4.0）：FBX と、テクスチャを 1K に縮めたもの（SourceArt/MedievalRoomKit）
# メッシュの名前 → テクスチャの名前（T_<名前>_DefaultMaterial_*）
MEDIEVAL = [
    ("Barrel/Barrel.fbx", "Barrel"),
    ("Bed/Bed.fbx", "Bed"),
    ("Chests/Chests.fbx", "Ghests"),
    ("Column/Column_1.fbx", "Column"),
    ("Column/Column_2.fbx", "Column"),
    ("Column/Column_3.fbx", "Column"),
    ("Door/Door.fbx", "Door"),
    ("Door/Door_1.fbx", "Door"),
    ("Fireplace/Fireplace.fbx", "Fireplace"),
    ("Shelf/Large_Shelf.fbx", "Large_shelf"),
    ("Shelf/Shelf_1m.fbx", "Large_shelf"),
    ("Shelf/Shelf_2m.fbx", "Large_shelf"),
    ("Table_Chairs/Chair.fbx", "Table_Chairs"),
    ("Table_Chairs/Chair_1.fbx", "Table_Chairs"),
    ("Table_Chairs/Table.fbx", "Table_Chairs"),
    ("Wall-mounted_torch/Wall-mounted_torch.fbx", "Wall-mounted_torch"),
    ("Widow/Window_2m.fbx", "Widow"),
    ("Widow/Window_2m_1.fbx", "Widow"),
    ("Books/Books_1.fbx", "Books"),
    ("Books/Books_2.fbx", "Books"),
]
# 床・壁に貼る模様として使うテクスチャ（Surfaces.csv から使う）
MEDIEVAL_SURFACES = ["StoneWall", "WoodenFloor", "StoneCeiling_low"]


def import_medieval():
    folder = os.path.join(SOURCE, "MedievalRoomKit")
    if not os.path.isdir(folder):
        log("no medieval room kit source")
        return
    dest = "/Game/Kakurenbo/Mansion/Medieval"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    prop = create_prop_material()
    textures = os.path.join(folder, "Textures")
    instances = {}

    def instance_for(group):
        if group in instances:
            return instances[group]
        prefix = os.path.join(textures, "T_%s_DefaultMaterial_" % group)
        base = texture(prefix + "BaseColor.png", dest + "/Textures")
        normal = texture(prefix + "Normal.png", dest + "/Textures", normal=True)
        orm = texture(prefix + "OcclusionRoughnessMetallic.png", dest + "/Textures", linear=True)
        glow = texture(prefix + "Emissive.png", dest + "/Textures") if os.path.exists(prefix + "Emissive.png") else None
        mi = tools.create_asset("MI_" + group.replace("-", "_"), dest + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, prop)
        for param, tex in (("BaseColorTex", base), ("NormalTex", normal), ("ORMTex", orm), ("EmissiveTex", glow)):
            if tex:
                mel.set_material_instance_texture_parameter_value(mi, param, tex)
        if glow:
            mel.set_material_instance_scalar_parameter_value(mi, "Emissive", 4.0)
        eal.save_loaded_asset(mi)
        instances[group] = mi
        return mi

    for (rel, group) in MEDIEVAL:
        mi = instance_for(group)
        imported = import_file(os.path.join(folder, "FBX", rel), dest + "/Meshes")
        for p in imported:
            asset = unreal.load_asset(p)
            if isinstance(asset, unreal.StaticMesh):
                for i in range(asset.get_num_sections(0) if hasattr(asset, "get_num_sections") else 1):
                    asset.set_material(i, mi)
                eal.save_loaded_asset(asset)
                box = asset.get_bounding_box()
                size = box.max - box.min
                log("medieval mesh %s size %.0f x %.0f x %.0f" % (p, size.x, size.y, size.z))
    # FBX と一緒に作られた使わないマテリアル・テクスチャは消す
    for p in eal.list_assets(dest + "/Meshes", recursive=True):
        asset = unreal.load_asset(p)
        if asset and not isinstance(asset, unreal.StaticMesh):
            eal.delete_asset(p.split(".")[0])
    for name in MEDIEVAL_SURFACES:
        prefix = os.path.join(textures, "T_%s_DefaultMaterial_" % name)
        texture(prefix + "BaseColor.png", dest + "/Textures")
        texture(prefix + "Normal.png", dest + "/Textures", normal=True)
        texture(prefix + "OcclusionRoughnessMetallic.png", dest + "/Textures", linear=True)
    log("medieval done")


def create_glass_material():
    # 額のガラス（ほとんど透明）。StylizedKitchen の絵の額に使う
    dest = "/Game/Kakurenbo/Materials"
    mat = new_material("M_KakuGlass", dest)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    color = color_param(mat, "Color", unreal.LinearColor(0.8, 0.85, 0.9, 1.0), -600, -200)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = scalar_param(mat, "Roughness", 0.1, -600, 0)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    opacity = scalar_param(mat, "Opacity", 0.12, -600, 150)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


# Fab の StylizedKitchen・Isometric-Interiors から、使う家具・飾り・模様だけを取り出したもの（SourceArt/FabProps。
# Tools/ExtractFabProps.py が作る。FBX・1K の PNG・manifest.json）→ /Game/Kakurenbo/Mansion/Props
def import_fab_props():
    import json
    folder = os.path.join(SOURCE, "FabProps")
    manifest_path = os.path.join(folder, "manifest.json")
    if not os.path.exists(manifest_path):
        log("no fab props source")
        return
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    dest = "/Game/Kakurenbo/Mansion/Props"
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    prop = create_prop_material()  # 作り直す（同じアセットのまま。Medieval のマテリアルインスタンスもそのまま使える）
    default_orm = unreal.load_asset("/Game/Kakurenbo/Materials/T_KakuDefaultORM")
    glass = create_glass_material()
    textures = os.path.join(folder, "Textures")
    normals = {k for k, v in manifest["surfaces"].items() if v == "normal"}
    orms = set()
    for mesh in manifest["meshes"].values():
        for slot in mesh["slots"]:
            if slot["normal"]:
                normals.add(slot["normal"])
            if slot["orm"]:
                orms.add(slot["orm"])
    loaded = {}

    def tex(name):
        if not name:
            return None
        if name not in loaded:
            loaded[name] = texture(os.path.join(textures, name + ".png"), dest + "/Textures", normal=name in normals, linear=name in orms)
        return loaded[name]

    instances = {}

    def instance_for(slot):
        key = (slot["base"], slot["normal"], slot["orm"], tuple(round(c, 3) for c in slot["color"]))
        if key in instances:
            return instances[key]
        name = "MI_" + (slot["base"][2:] if slot["base"].startswith("T_") else (slot["base"] or "Plain"))
        while eal.does_asset_exist(dest + "/Materials/" + name):
            name += "_2"
        mi = tools.create_asset(name, dest + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, prop)
        for param, t in (("BaseColorTex", tex(slot["base"])), ("NormalTex", tex(slot["normal"])), ("ORMTex", tex(slot["orm"]) or default_orm)):
            if t:
                mel.set_material_instance_texture_parameter_value(mi, param, t)
        c = slot["color"]
        mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
        eal.save_loaded_asset(mi)
        instances[key] = mi
        return mi

    for name, mesh in manifest["meshes"].items():
        for p in import_file(os.path.join(folder, "FBX", name + ".fbx"), dest + "/Meshes"):
            asset = unreal.load_asset(p)
            if isinstance(asset, unreal.StaticMesh):
                for i, slot in enumerate(mesh["slots"]):
                    asset.set_material(i, glass if slot["glass"] else instance_for(slot))
                eal.save_loaded_asset(asset)
                box = asset.get_bounding_box()
                size = box.max - box.min
                log("prop mesh %s size %.0f x %.0f x %.0f (was %.0f x %.0f x %.0f) slots %s" % (p, size.x, size.y, size.z, mesh["size"][0], mesh["size"][1], mesh["size"][2],
                    [str(m.material_slot_name) for m in asset.static_materials]))
    # FBX と一緒に作られた使わないマテリアル・テクスチャは消す
    for p in eal.list_assets(dest + "/Meshes", recursive=True):
        asset = unreal.load_asset(p)
        if asset and not isinstance(asset, unreal.StaticMesh):
            eal.delete_asset(p.split(".")[0])
    for name in manifest["surfaces"]:
        tex(name)
    log("fab props done")


def report():
    for folder in ("/Game/Gem", "/Game/Player", "/Game/QuietWall", "/Game/Cat"):
        for path in eal.list_assets(folder, recursive=True):
            asset = unreal.load_asset(path)
            log("asset %s (%s)" % (path, asset.get_class().get_name() if asset else "?"))


import sys
if "surface" in sys.argv[-1:] or "--surface-only" in sys.argv:
    create_surface_material()
elif "--smoke-only" in sys.argv:
    create_smoke_material()
elif "--medieval-only" in sys.argv:
    import_medieval()
elif "--props-only" in sys.argv:
    import_fab_props()
elif "--cat-only" in sys.argv:
    import_cat()
else:
    import_gem()
    import_cat()
    import_player()
    import_quiet_wall()
    import_wood()
    create_surface_material()
    create_smoke_material()
    import_medieval()
    import_fab_props()
report()
log("done")