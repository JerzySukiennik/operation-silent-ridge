# M3 mission content: /Game/Mission/M_MissionLit (vertex-coloured lit surface for the procedural plant / SAM sites / airbase / weapons,
# world-space grime noise, snow on up-facing faces masked by vertex alpha), M_MissionPuffAdd / M_MissionPuffTr (soft round sprites
# and ribbons for fire, smoke, flares, tracers; depth fade), and the mission audio (Niepotrzebne/m3/audio -> /Game/Mission/Audio).
import unreal, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
DEST = "/Game/Mission"


def node(m, cls, x, y, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


# ---------------------------------------------------------------- M_MissionLit
m = new_material("M_MissionLit", DEST)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
code = """
float3 wp = WP * 0.01;
float n1 = Texture2DSample(Noise, NoiseSampler, wp.xy * 0.011).r;
float n2 = Texture2DSample(Noise, NoiseSampler, wp.xz * 0.043 + wp.yz * 0.043 + 0.37).g;
float n = n1 * 0.6 + n2 * 0.4;
float3 base = VC * Color.rgb * (0.8 + 0.4 * n);
float up = saturate((N.z - 0.6) / 0.3);
float snow = saturate(up * VA * SnowAmount * saturate(n * 1.6 + 0.1));
Rough = lerp(Roughness, 0.6, snow);
return lerp(base, float3(0.82, 0.85, 0.9), snow);
"""
c = custom_node(m, code, ["VC", "VA", "WP", "N", "Noise", "Color", "Roughness", "SnowAmount"], [("Rough", CMOT.CMOT_FLOAT1)], -400, 0)
vc = node(m, unreal.MaterialExpressionVertexColor, -900, -300)
conn(vc, "", c, "VC")
conn(vc, "A", c, "VA")
conn(node(m, unreal.MaterialExpressionWorldPosition, -900, -200), "", c, "WP")
conn(node(m, unreal.MaterialExpressionVertexNormalWS, -900, -100), "", c, "N")
conn(tex_obj(m, "Noise", "/Game/Textures/T_noise", -900, 0, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR), "", c, "Noise")
conn(node(m, unreal.MaterialExpressionVectorParameter, -900, 100, parameter_name="Color", default_value=unreal.LinearColor(1, 1, 1, 1)), "", c, "Color")
conn(scalar(m, "Roughness", 0.85, -900, 200), "", c, "Roughness")
conn(scalar(m, "SnowAmount", 1.0, -900, 300), "", c, "SnowAmount")
prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.recompile_material(m)
EAL.save_loaded_asset(m)
log("material M_MissionLit")


# ---------------------------------------------------------------- puffs (unlit sprites / ribbons)
def puff(name, additive):
    m = new_material(name, DEST)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    code = """
float2 d = UV * 2.0 - 1.0;
float r = saturate(1.0 - dot(d, d));
float2 uv = UV * Tiling + float2(T * Scroll * 0.37, T * Scroll);
float n = Texture2DSample(Noise, NoiseSampler, uv * 0.7).r * 0.6 + Texture2DSample(Noise, NoiseSampler, uv * 1.9 + 0.31).g * 0.4;
float nf = lerp(1.0, saturate(n * 1.8 - 0.25) * 1.5, NoiseAmt);
float a = saturate(VA * Opacity * r * r * nf * DF);
Alpha = a;
return VC * Color.rgb * Intensity * (ADD > 0.5 ? a : 1.0);
""".replace("ADD", "1.0" if additive else "0.0")
    c = custom_node(m, code, ["VC", "VA", "UV", "T", "Noise", "Color", "Intensity", "Opacity", "NoiseAmt", "Tiling", "Scroll", "DF"], [("Alpha", CMOT.CMOT_FLOAT1)], -400, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, -400)
    conn(vc, "", c, "VC")
    conn(vc, "A", c, "VA")
    conn(node(m, unreal.MaterialExpressionTextureCoordinate, -900, -340), "", c, "UV")
    conn(node(m, unreal.MaterialExpressionTime, -900, -280), "", c, "T")
    conn(tex_obj(m, "Noise", "/Game/Textures/T_noise", -900, -220, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR), "", c, "Noise")
    conn(node(m, unreal.MaterialExpressionVectorParameter, -900, -160, parameter_name="Color", default_value=unreal.LinearColor(1, 1, 1, 1)), "", c, "Color")
    y = -100
    for pn, dv in [("Intensity", 1.0), ("Opacity", 1.0), ("NoiseAmt", 0.45), ("Tiling", 1.0), ("Scroll", 0.15)]:
        conn(scalar(m, pn, dv, -900, y), "", c, pn)
        y += 60
    df = node(m, unreal.MaterialExpressionDepthFade, -700, 300)
    df.set_editor_property("fade_distance_default", 250.0)
    one = node(m, unreal.MaterialExpressionConstant, -900, 300, r=1.0)
    conn(one, "", df, "Opacity")
    conn(df, "", c, "DF")
    prop(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not additive:
        prop(c, "Alpha", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log("material", name)


puff("M_MissionPuffAdd", True)
puff("M_MissionPuffTr", False)

# ---------------------------------------------------------------- audio
WAV = os.path.join(UET, "Niepotrzebne", "m3", "audio")
ADEST = DEST + "/Audio"
tasks = []
for f in sorted(os.listdir(WAV)):
    if not f.endswith(".wav"):
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(WAV, f))
    t.set_editor_property("destination_path", ADEST)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    tasks.append(t)
AT.import_asset_tasks(tasks)
LOOPS = {"gun_loop", "rwr_lock", "rwr_launch"}
for p in EAL.list_assets(ADEST, recursive=False):
    a = unreal.load_asset(p.split(".")[0])
    if isinstance(a, unreal.SoundWave):
        a.set_editor_property("looping", a.get_name() in LOOPS)
        EAL.save_loaded_asset(a)
        log("sound", a.get_name(), "loop", a.get_editor_property("looping"))
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("MISSION CONTENT DONE")
