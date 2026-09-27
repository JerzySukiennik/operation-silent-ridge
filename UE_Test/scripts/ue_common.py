# Shared helpers for the UE editor Python build scripts (paths, logging, asset helpers).
import unreal, os, json
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # Operation Silent Ridge/
GAME = os.path.join(ROOT, "game")
UET = os.path.join(ROOT, "UE_Test")
SRC = os.path.join(UET, "Niepotrzebne", "src")
HLSL = os.path.join(UET, "scripts", "hlsl")
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

def log(*a):
    unreal.log("OSR " + " ".join(str(x) for x in a))
    print("OSR", *a, flush=True)

def import_file(path, dest, name=None, replace=True):
    """Interchange import without the content-browser sync that crashes the commandlet (no Slate)."""
    im = unreal.InterchangeManager.get_interchange_manager_scripted()
    before = set(EAL.list_assets(dest, recursive=True, include_folder=False)) if EAL.does_directory_exist(dest) else set()
    sd = unreal.InterchangeManager.create_source_data(path)
    prm = unreal.ImportAssetParameters()
    prm.set_editor_property("is_automated", True)
    try:
        prm.set_editor_property("replace_existing", replace)
    except Exception:
        pass
    if name:
        try:
            prm.set_editor_property("destination_name", name)
        except Exception:
            pass
    ok = im.import_asset(dest, sd, prm)
    after = set(EAL.list_assets(dest, recursive=True, include_folder=False))
    new = sorted(after - before)
    objs = [unreal.load_asset(p.split(".")[0]) for p in new]
    if name and len(objs) == 1 and objs[0].get_name() != name:
        np_ = dest + "/" + name
        EAL.rename_asset(objs[0].get_path_name(), np_)
        objs = [unreal.load_asset(np_)]
    for o in objs:
        EAL.save_loaded_asset(o)
    log("imported", os.path.basename(path), "ok", ok, "->", [o.get_path_name() for o in objs if o])
    return objs
