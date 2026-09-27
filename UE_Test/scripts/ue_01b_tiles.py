# Imports the Nanite terrain tiles (GLB) into /Game/Terrain, enables Nanite and assigns M_Terrain; also enables Nanite on the fir.
import unreal, os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *
TD = os.path.join(UET, "Niepotrzebne", "tiles")
man = json.load(open(os.path.join(TD, "manifest.json")))
mat = unreal.load_asset("/Game/Materials/M_Terrain")
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

def nanite(mesh, on=True):
    ns = mesh.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", on)
    try:
        SMES.set_nanite_settings(mesh, ns, apply_changes=True)
    except Exception as ex:
        mesh.set_editor_property("nanite_settings", ns)

todo = [t for t in man if not EAL.does_asset_exist("/Game/Terrain/" + t["name"] + "/" + t["name"])]
log("tiles to import", len(todo), "of", len(man))
for k, t in enumerate(todo):
    objs = import_file(os.path.join(TD, t["name"] + ".glb"), "/Game/Terrain/" + t["name"])
    meshes = [o for o in objs if isinstance(o, unreal.StaticMesh)]
    if not meshes:
        log("tile import produced no mesh", t["name"], objs); continue
    mesh = meshes[0]
    if mesh.get_path_name().split(".")[0] != "/Game/Terrain/" + t["name"] + "/" + t["name"]:
        EAL.rename_asset(mesh.get_path_name(), "/Game/Terrain/" + t["name"] + "/" + t["name"])
        mesh = unreal.load_asset("/Game/Terrain/" + t["name"] + "/" + t["name"])
    mesh.set_material(0, mat)
    nanite(mesh, True)
    EAL.save_loaded_asset(mesh)
    if k % 10 == 0:
        log("tiles imported", k + 1, "/", len(todo))
fir = [unreal.load_asset(p) for p in EAL.list_assets("/Game/Trees", recursive=True)]
for a in fir:
    if isinstance(a, unreal.StaticMesh):
        mats = a.static_materials
        for i, sm in enumerate(mats):
            nm = str(sm.material_slot_name).lower()
            a.set_material(i, unreal.load_asset("/Game/Materials/M_FirBark" if "bark" in nm else "/Game/Materials/M_FirNeedles"))
        nanite(a, os.environ.get("OSR_TREE_NANITE", "1") == "1")
        EAL.save_loaded_asset(a)
        log("fir", a.get_path_name(), [str(s.material_slot_name) for s in mats], "nanite", a.get_editor_property("nanite_settings").enabled)
# one sample tile bbox (axis check)
t0 = man[0]; m0 = unreal.load_asset("/Game/Terrain/" + t0["name"] + "/" + t0["name"])
if m0:
    bb = m0.get_bounding_box(); log("tile0", t0["name"], "bbox", bb.min, bb.max)
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("TILES DONE")
