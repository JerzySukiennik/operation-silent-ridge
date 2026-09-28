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


# ---------------------------------------------------------------- material authoring helpers (v2 build; ue_02 keeps its own copies)
MEL = unreal.MaterialEditingLibrary
CMOT = unreal.CustomMaterialOutputType


def new_material(name, path="/Game/Materials"):
    full = path + "/" + name
    if EAL.does_asset_exist(full):
        m = unreal.load_asset(full)
        MEL.delete_all_material_expressions(m)   # rebuild in place so references stay valid
        return m
    return AT.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())


def tex_obj(mat, name, texpath, x, y, sampler_type, clamp=False):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", unreal.load_asset(texpath))
    e.set_editor_property("sampler_type", sampler_type)
    e.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS if clamp else unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    return e


def custom_node(mat, code, inputs, outputs, x=-400, y=0, out_type=None):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type or CMOT.CMOT_FLOAT3)
    c.set_editor_property("description", "OSR")
    ins = []
    for n in inputs:
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", n); ins.append(ci)
    c.set_editor_property("inputs", ins)
    outs = []
    for n, t in outputs:
        co = unreal.CustomOutput(); co.set_editor_property("output_name", n); co.set_editor_property("output_type", t); outs.append(co)
    c.set_editor_property("additional_outputs", outs)
    return c


def conn(a, aout, b, bin_):
    if not MEL.connect_material_expressions(a, aout, b, bin_):
        log("CONNECT FAILED", a.get_name(), aout, "->", b.get_name(), bin_)


def prop(a, aout, p):
    if not MEL.connect_material_property(a, aout, p):
        log("PROP CONNECT FAILED", a.get_name(), aout, p)


def scalar(mat, name, value, x, y):
    s = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", name); s.set_editor_property("default_value", float(value))
    return s


def import_texture(path, name, kind, dest="/Game/Textures", force=False):
    """kind: albedo | normal | mask (linear BC7, clamped) | maskwrap (linear BC7, wrapped)."""
    TC = unreal.TextureCompressionSettings
    full = dest + "/" + name
    if EAL.does_asset_exist(full) and not force:
        return unreal.load_asset(full)
    if EAL.does_asset_exist(full):
        # replace in place (deleting a texture that loaded materials reference crashes the async loader)
        tmp = os.path.join(os.path.dirname(path), "_reimport", name + os.path.splitext(path)[1])
        os.makedirs(os.path.dirname(tmp), exist_ok=True)
        import shutil; shutil.copyfile(path, tmp)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", tmp); task.set_editor_property("destination_path", dest)
        task.set_editor_property("destination_name", name); task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True); task.set_editor_property("save", False)
        AT.import_asset_tasks([task])
        t = unreal.load_asset(full)
        log("reimported", name)
    else:
        t = import_file(path, dest, name)[0]
    if kind == "albedo":
        t.set_editor_property("srgb", True); t.set_editor_property("compression_settings", TC.TC_DEFAULT)
    elif kind == "normal":
        t.set_editor_property("srgb", False); t.set_editor_property("compression_settings", TC.TC_NORMALMAP)
    else:
        t.set_editor_property("srgb", False); t.set_editor_property("compression_settings", TC.TC_BC7)
    if kind == "mask":
        t.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP); t.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(t)
    return t
