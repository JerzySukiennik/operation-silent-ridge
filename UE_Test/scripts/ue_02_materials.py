# Authors M_Terrain, M_Ocean (Custom HLSL ports of the Godot shaders), M_FirNeedles and M_FirBark via MaterialEditingLibrary.
import unreal, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *
MEL = unreal.MaterialEditingLibrary
CMOT = unreal.CustomMaterialOutputType

def new_material(name, path="/Game/Materials"):
    full = path + "/" + name
    if EAL.does_asset_exist(full):
        m = unreal.load_asset(full)
        MEL.delete_all_material_expressions(m)   # rebuild in place so references from meshes stay valid
        return m
    return AT.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())

def tex_obj(mat, name, texpath, x, y, sampler_type):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", unreal.load_asset(texpath))
    e.set_editor_property("sampler_type", sampler_type)
    try:
        e.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    except Exception as ex:
        log("sampler_source not set", ex)
    return e

def custom_node(mat, code, inputs, outputs, x=-400, y=0, out_type=CMOT.CMOT_FLOAT3):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type)
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
    ok = MEL.connect_material_expressions(a, aout, b, bin_)
    if not ok:
        log("CONNECT FAILED", a.get_name(), aout, "->", b.get_name(), bin_)

def prop(a, aout, p):
    ok = MEL.connect_material_property(a, aout, p)
    if not ok:
        log("PROP CONNECT FAILED", a.get_name(), aout, p)

ST = unreal.MaterialSamplerType
# ---------------------------------------------------------------- terrain
m = new_material("M_Terrain")
m.set_editor_property("tangent_space_normal", False)
code = open(os.path.join(HLSL, "terrain.hlsl")).read()
texs = [("RockM", "T_rock_macro_A", ST.SAMPLERTYPE_COLOR), ("RockMN", "T_rock_macro_N", ST.SAMPLERTYPE_NORMAL),
        ("Rock", "T_rock_cliff_A", ST.SAMPLERTYPE_COLOR), ("RockN", "T_rock_cliff_N", ST.SAMPLERTYPE_NORMAL),
        ("Scree", "T_scree_A", ST.SAMPLERTYPE_COLOR), ("ScreeN", "T_scree_N", ST.SAMPLERTYPE_NORMAL),
        ("ScreeM", "T_scree_macro_A", ST.SAMPLERTYPE_COLOR), ("Snow", "T_snow_A", ST.SAMPLERTYPE_COLOR),
        ("SnowN", "T_snow_N", ST.SAMPLERTYPE_NORMAL), ("SnowM", "T_snow_macro_A", ST.SAMPLERTYPE_COLOR),
        ("Ground", "T_ground_A", ST.SAMPLERTYPE_COLOR), ("GroundN", "T_ground_N", ST.SAMPLERTYPE_NORMAL),
        ("Floor", "T_forest_floor_A", ST.SAMPLERTYPE_COLOR), ("Shore", "T_shore_A", ST.SAMPLERTYPE_COLOR),
        ("MasksA", "T_masks_a", ST.SAMPLERTYPE_LINEAR_COLOR), ("MasksB", "T_masks_b", ST.SAMPLERTYPE_LINEAR_COLOR),
        ("Noise", "T_noise", ST.SAMPLERTYPE_LINEAR_COLOR)]
texs = [("Bake", "T_bake", ST.SAMPLERTYPE_LINEAR_COLOR)] + texs
inputs = ["P", "N", "Cam", "TreeFar", "BounceGain", "SunLux", "AOStrength"] + [t[0] for t in texs]
c = custom_node(m, code, inputs, [("NormalWS", CMOT.CMOT_FLOAT3), ("Rough", CMOT.CMOT_FLOAT1), ("AOut", CMOT.CMOT_FLOAT1), ("Spec", CMOT.CMOT_FLOAT1), ("Emis", CMOT.CMOT_FLOAT3)])
wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -300)
vn = MEL.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -900, -250)
cam = MEL.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, -900, -200)
tf = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, -150)
tf.set_editor_property("parameter_name", "TreeFar"); tf.set_editor_property("default_value", 2600.0)
conn(wp, "", c, "P"); conn(vn, "", c, "N"); conn(cam, "", c, "Cam"); conn(tf, "", c, "TreeFar")
for k, (pn, dv) in enumerate([("BounceGain", float(os.environ.get("OSR_BOUNCE", "1.0"))), ("SunLux", float(os.environ.get("OSR_SUN_LUX", "10"))), ("AOStrength", float(os.environ.get("OSR_AO", "1.0")))]):
    sp = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -1100, -300 + k * 50)
    sp.set_editor_property("parameter_name", pn); sp.set_editor_property("default_value", dv)
    conn(sp, "", c, pn)
for k, (pin, asset, st) in enumerate(texs):
    e = tex_obj(m, pin, "/Game/Textures/" + asset, -900, -100 + k * 60, st)
    conn(e, "", c, pin)
prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
prop(c, "NormalWS", unreal.MaterialProperty.MP_NORMAL)
prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
prop(c, "AOut", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
prop(c, "Spec", unreal.MaterialProperty.MP_SPECULAR)
prop(c, "Emis", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
m.set_editor_property("used_with_nanite", True)
MEL.recompile_material(m); EAL.save_loaded_asset(m)
log("M_Terrain done")

# ---------------------------------------------------------------- ocean
m = new_material("M_Ocean")
m.set_editor_property("tangent_space_normal", False)
code = open(os.path.join(HLSL, "ocean.hlsl")).read()
otex = [("WaveA", "T_water_a", ST.SAMPLERTYPE_NORMAL), ("WaveB", "T_water_b", ST.SAMPLERTYPE_NORMAL),
        ("Noise", "T_noise", ST.SAMPLERTYPE_LINEAR_COLOR), ("Depth", "T_ocean_depth", ST.SAMPLERTYPE_ALPHA)]
c = custom_node(m, code, ["P", "Cam", "T"] + [t[0] for t in otex], [("NormalWS", CMOT.CMOT_FLOAT3), ("Rough", CMOT.CMOT_FLOAT1)])
wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -300)
cam = MEL.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, -900, -250)
tm = MEL.create_material_expression(m, unreal.MaterialExpressionTime, -900, -200)
conn(wp, "", c, "P"); conn(cam, "", c, "Cam"); conn(tm, "", c, "T")
for k, (pin, asset, st) in enumerate(otex):
    e = tex_obj(m, pin, "/Game/Textures/" + asset, -900, -100 + k * 60, st)
    if pin == "Depth":
        e.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    conn(e, "", c, pin)
prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
prop(c, "NormalWS", unreal.MaterialProperty.MP_NORMAL)
prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.recompile_material(m); EAL.save_loaded_asset(m)
log("M_Ocean done")

# ---------------------------------------------------------------- fir needles / bark
for name, alb, nrm, masked in [("M_FirNeedles", "T_fir_twig_A", "T_fir_twig_N", True), ("M_FirBark", "T_fir_bark_A", None, False)]:
    m = new_material(name)
    ts = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    ts.set_editor_property("parameter_name", "Albedo"); ts.set_editor_property("texture", unreal.load_asset("/Game/Textures/" + alb))
    tint = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, -200)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.5, 0.6, 0.53, 1.0) if masked else unreal.LinearColor(1, 1, 1, 1))
    vc = MEL.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, -350)
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 0)
    conn(ts, "RGB", mul, "A"); conn(tint, "", mul, "B")
    if masked:
        mul2 = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 0)
        conn(mul, "", mul2, "A"); conn(vc, "", mul2, "B")
        prop(mul2, "", unreal.MaterialProperty.MP_BASE_COLOR)
        prop(ts, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("opacity_mask_clip_value", 0.45)
        m.set_editor_property("two_sided", True)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        sss = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 150)
        conn(mul2, "", sss, "A"); sss.set_editor_property("const_b", 0.35); prop(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        tn = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 300)
        tn.set_editor_property("parameter_name", "Normal"); tn.set_editor_property("texture", unreal.load_asset("/Game/Textures/" + nrm))
        tn.set_editor_property("sampler_type", ST.SAMPLERTYPE_NORMAL)
        prop(tn, "RGB", unreal.MaterialProperty.MP_NORMAL)
    else:
        prop(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # baked sky occlusion at the tree's ground position x self-occlusion towards the trunk base (no Lumen)
    aoc = custom_node(m, "float2 uv = (float2(P.x, P.y) * 0.01 + 40000.0) / 80000.0; float sky = Texture2DSample(Bake, BakeSampler, uv).a; return sky * lerp(0.32, 1.0, saturate(L.z / 2000.0));", ["P", "L", "Bake"], [], -400, 450, CMOT.CMOT_FLOAT1)
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -700, 450), "", aoc, "P")
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionLocalPosition, -700, 500), "", aoc, "L")
    bt = tex_obj(m, "Bake", "/Game/Textures/T_bake", -700, 550, ST.SAMPLERTYPE_LINEAR_COLOR)
    conn(bt, "", aoc, "Bake")
    prop(aoc, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    r = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -250, 300); r.set_editor_property("r", 0.9)
    prop(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log(name, "done")
log("MATERIALS DONE")
