# Imports textures (Poly Haven packs, masks, noise, water, tree atlases, ocean depth) and meshes (F-35C static, fir) into /Game.
import unreal, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *
TC = unreal.TextureCompressionSettings
TEX = os.path.join(GAME, "assets/world/textures")
TREES = os.path.join(GAME, "assets/world/trees")
TER = os.path.join(GAME, "assets/world/terrain")

def tex(path, name, kind):
    if EAL.does_asset_exist("/Game/Textures/" + name):
        if name not in os.environ.get("OSR_REIMPORT", "").split(","):
            return
        EAL.delete_asset("/Game/Textures/" + name)
    t = import_file(path, "/Game/Textures", name)[0]
    if kind == "albedo":
        t.set_editor_property("srgb", True); t.set_editor_property("compression_settings", TC.TC_DEFAULT)
    elif kind == "normal":
        t.set_editor_property("srgb", False); t.set_editor_property("compression_settings", TC.TC_NORMALMAP)
    elif kind == "mask":
        t.set_editor_property("srgb", False); t.set_editor_property("compression_settings", TC.TC_BC7)
    elif kind == "linear":
        t.set_editor_property("srgb", False); t.set_editor_property("compression_settings", TC.TC_VECTOR_DISPLACEMENTMAP)
    elif kind == "gray":
        t.set_editor_property("srgb", False); t.set_editor_property("compression_settings", TC.TC_ALPHA)
    if kind in ("mask", "gray"):
        t.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP); t.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(t)

for base in ["rock_macro", "rock_cliff", "scree", "scree_macro", "snow", "snow_macro", "ground", "forest_floor", "shore"]:
    tex(os.path.join(TEX, base + "_albedo_h.png"), "T_" + base + "_A", "albedo")
    if os.path.exists(os.path.join(TEX, base + "_normal.png")):
        tex(os.path.join(TEX, base + "_normal.png"), "T_" + base + "_N", "normal")
tex(os.path.join(TEX, "noise.png"), "T_noise", "linear")
tex(os.path.join(TEX, "water_normal_a.png"), "T_water_a", "normal")
tex(os.path.join(TEX, "water_normal_b.png"), "T_water_b", "normal")
tex(os.path.join(TER, "masks_a.png"), "T_masks_a", "mask")
tex(os.path.join(TER, "masks_b.png"), "T_masks_b", "mask")
tex(os.path.join(SRC, "ocean_depth.png"), "T_ocean_depth", "gray")
tex(os.path.join(SRC, "bake_2048.png"), "T_bake", "mask")
tex(os.path.join(TREES, "fir_twig_albedo.png"), "T_fir_twig_A", "albedo")
tex(os.path.join(TREES, "fir_twig_normal.png"), "T_fir_twig_N", "normal")
tex(os.path.join(TREES, "fir_bark_albedo.png"), "T_fir_bark_A", "albedo")

for path, dest in [(os.path.join(SRC, "f35c_static.glb"), "/Game/F35C"), (os.path.join(TREES, "fir.glb"), "/Game/Trees")]:
    if not EAL.list_assets(dest, recursive=True):
        objs = import_file(path, dest)
for dest in ["/Game/F35C", "/Game/Trees"]:
    for p in EAL.list_assets(dest, recursive=True):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            bb = a.get_bounding_box()
            log("mesh", p, "tris", a.get_num_triangles(0) if hasattr(a, "get_num_triangles") else "?", "bbox", bb.min, bb.max, "mats", [m.material_interface.get_path_name() if m.material_interface else None for m in a.static_materials])
        else:
            log("asset", p, type(a).__name__)
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("IMPORT DONE")
