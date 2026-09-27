# Stage 2 content: imports the 12 jet audio recordings (WAV converted from game/assets/audio/*.ogg) into /Game/Audio and authors the VFX materials /Game/VFX/M_VfxAdditive + M_VfxTranslucent (Custom HLSL, vertex-colour driven).
import unreal, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *
MEL = unreal.MaterialEditingLibrary
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")

# ---------------------------------------------------------------- audio
if os.environ.get("OSR_SKIP_AUDIO"):
    pass
WAV = os.path.join(UET, "Niepotrzebne", "audio_wav")
tasks = []
for f in sorted(os.listdir(WAV)):
    if not f.endswith(".wav"):
        continue
    n = f[:-4]
    if EAL.does_asset_exist("/Game/Audio/" + n) and "audio" not in os.environ.get("OSR_REIMPORT", ""):
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(WAV, f))
    t.set_editor_property("destination_path", "/Game/Audio")
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    tasks.append(t)
if tasks:
    AT.import_asset_tasks(tasks)
for p in EAL.list_assets("/Game/Audio", recursive=False):
    a = unreal.load_asset(p.split(".")[0])
    if isinstance(a, unreal.SoundWave):
        a.set_editor_property("looping", a.get_name().endswith("_loop"))
        EAL.save_loaded_asset(a)
        log("sound", a.get_name(), "loop", a.get_editor_property("looping"), "dur", a.get_editor_property("duration"))

# ---------------------------------------------------------------- VFX materials
CMOT = unreal.CustomMaterialOutputType
def build(name, additive):
    full = "/Game/VFX/" + name
    if EAL.does_asset_exist(full):
        m = unreal.load_asset(full)
        MEL.delete_all_material_expressions(m)
    else:
        m = AT.create_asset(name, "/Game/VFX", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    # draw after motion blur: otherwise the plume/vapour is smeared by the fast background behind it
    m.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    m.set_editor_property("used_with_static_lighting", False)
    for flag in ["used_with_static_lighting"]:
        pass
    code = """
float2 uv = UV * Tiling + float2(T * Scroll * 0.37, T * Scroll);
float n = Texture2DSample(Noise, NoiseSampler, uv).r * 0.6 + Texture2DSample(Noise, NoiseSampler, uv * 2.7 + 0.31).g * 0.4;
float nf = lerp(1.0, saturate(n * 1.8 - 0.25) * 1.5, NoiseAmt);
float ndv = abs(dot(normalize(N), normalize(V)));
float edge = EdgeSoft > 0.0 ? pow(saturate(ndv), EdgeSoft) : 1.0;
Alpha = saturate(VA * Opacity * nf * edge);
return VC.rgb * Color * Intensity * (ADD > 0.5 ? VA * nf * edge : 1.0);
""".replace("ADD", "1.0" if additive else "0.0")
    c = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -400, 0)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", CMOT.CMOT_FLOAT3)
    ins = []
    names = ["VC", "VA", "UV", "T", "N", "V", "Noise", "Color", "Intensity", "Opacity", "NoiseAmt", "Tiling", "Scroll", "EdgeSoft"]
    for n in names:
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", n); ins.append(ci)
    c.set_editor_property("inputs", ins)
    co = unreal.CustomOutput(); co.set_editor_property("output_name", "Alpha"); co.set_editor_property("output_type", CMOT.CMOT_FLOAT1)
    c.set_editor_property("additional_outputs", [co])
    y = [-600]
    def node(cls, **props):
        e = MEL.create_material_expression(m, cls, -900, y[0]); y[0] += 60
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e
    vcn = node(unreal.MaterialExpressionVertexColor)
    MEL.connect_material_expressions(vcn, "", c, "VC")
    MEL.connect_material_expressions(vcn, "A", c, "VA")
    MEL.connect_material_expressions(node(unreal.MaterialExpressionTextureCoordinate), "", c, "UV")
    MEL.connect_material_expressions(node(unreal.MaterialExpressionTime), "", c, "T")
    MEL.connect_material_expressions(node(unreal.MaterialExpressionVertexNormalWS), "", c, "N")
    MEL.connect_material_expressions(node(unreal.MaterialExpressionCameraVectorWS), "", c, "V")
    t = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Noise", texture=unreal.load_asset("/Game/Textures/T_noise"), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    MEL.connect_material_expressions(t, "", c, "Noise")
    col = node(unreal.MaterialExpressionVectorParameter, parameter_name="Color", default_value=unreal.LinearColor(1, 1, 1, 1))
    MEL.connect_material_expressions(col, "", c, "Color")
    for pn, dv in [("Intensity", 1.0), ("Opacity", 1.0), ("NoiseAmt", 0.5), ("Tiling", 1.0), ("Scroll", 1.0), ("EdgeSoft", 0.0)]:
        MEL.connect_material_expressions(node(unreal.MaterialExpressionScalarParameter, parameter_name=pn, default_value=dv), "", c, pn)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not additive:
        MEL.connect_material_property(c, "Alpha", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log("vfx material", name)
build("M_VfxAdditive", True)
build("M_VfxTranslucent", False)
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("STAGE2 CONTENT DONE")
