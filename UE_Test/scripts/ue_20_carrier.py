# M3 carrier content (editor Python, headless): imports the procedural carrier (scripts/carrier/*), deck textures, F-35C body +
# gear parts + tailhook (split from the Godot GLB), helmet; authors the carrier/crew/light/wake materials. All under /Game/Carrier.
# Prereq (outside the editor): scripts/carrier/build_all.sh (Blender + texture generators -> Niepotrzebne/m3/*_src).
# Steps via env OSR_M3 (comma list, default all): tex,carrier,f35,mat,audio
import unreal, os, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *

CSRC = os.path.join(UET, "Niepotrzebne", "m3", "carrier_src")
FSRC = os.path.join(UET, "Niepotrzebne", "m3", "f35_src")
ASRC = os.path.join(UET, "Niepotrzebne", "m3", "audio_src")
STEPS = os.environ.get("OSR_M3", "tex,carrier,f35,mat,audio").split(",")
ST = unreal.MaterialSamplerType
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
MAT = "/Game/Carrier/Materials"


def nanite(mesh, on=True):
    ns = mesh.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", on)
    try:
        SMES.set_nanite_settings(mesh, ns, apply_changes=True)
    except Exception:
        mesh.set_editor_property("nanite_settings", ns)


def import_mesh(path, dest, name):
    """Imports one GLB into dest (fresh folder), returns the static mesh renamed to `name`; stray imported materials/textures are removed."""
    if EAL.does_directory_exist(dest + "/_imp_" + name):
        EAL.delete_directory(dest + "/_imp_" + name)
    tmp = dest + "/_imp_" + name
    objs = import_file(path, tmp)
    sm = next((o for o in objs if isinstance(o, unreal.StaticMesh)), None)
    if not sm:
        log("NO MESH in", path)
        return None
    full = dest + "/" + name
    if EAL.does_asset_exist(full):
        EAL.delete_asset(full)          # only C++ loads these by path: nothing to keep
    EAL.rename_asset(sm.get_path_name(), full)
    sm = unreal.load_asset(full)
    EAL.delete_directory(tmp)
    return sm


def slot_names(sm):
    return [str(s.get_editor_property("material_slot_name")) for s in sm.get_editor_property("static_materials")]


# ---------------------------------------------------------------- textures
if "tex" in STEPS:
    import_texture(os.path.join(CSRC, "T_CarrierDeck.png"), "T_CarrierDeck", "albedo", dest="/Game/Carrier/Textures", force=True)
    t = unreal.load_asset("/Game/Carrier/Textures/T_CarrierDeck")
    t.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP); t.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
    EAL.save_loaded_asset(t)
    import_texture(os.path.join(CSRC, "T_Nonskid_N.png"), "T_Nonskid_N", "normal", dest="/Game/Carrier/Textures", force=True)
    log("M3 TEX DONE")

# ---------------------------------------------------------------- materials
if "mat" in STEPS:
    # hull/island/fittings: haze grey with waterline boot top + anti-fouling below (object-space Z), mild grime from world noise
    m = new_material("M_CarrierHull", MAT)
    m.set_editor_property("two_sided", True)
    c = custom_node(m, """
float z = LP.z / 100.0;
float3 grey = Tint;
float3 col = z < -1.2 ? float3(0.10, 0.028, 0.022) : (z < 1.6 ? float3(0.014, 0.014, 0.015) : grey);
float g = frac(sin(dot(floor(WP.xy / 170.0), float2(12.9898, 78.233))) * 43758.5453);
float streak = saturate(frac(WP.x / 530.0 + WP.y / 910.0) * 1.0);
col *= lerp(0.92, 1.06, g) * lerp(1.0, 0.93, smoothstep(16.0, 19.0, z) * 0.0 + streak * 0.15);
Rough = lerp(0.62, 0.5, g);
return col;
""", ["LP", "WP", "Tint"], [("Rough", CMOT.CMOT_FLOAT1)])
    lp = MEL.create_material_expression(m, unreal.MaterialExpressionLocalPosition, -900, -200)
    wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -100)
    tint = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 0)
    tint.set_editor_property("parameter_name", "Tint"); tint.set_editor_property("default_value", unreal.LinearColor(0.155, 0.17, 0.18, 1))
    conn(lp, "", c, "LP"); conn(wp, "", c, "WP"); conn(tint, "", c, "Tint")
    prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR); prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)

    # island: same haze grey family, a touch darker/bluer (instance of the hull material)
    mi_path = MAT + "/MI_CarrierIsland"
    mi = unreal.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else AT.create_asset("MI_CarrierIsland", MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, unreal.load_asset(MAT + "/M_CarrierHull"))
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(0.13, 0.145, 0.16, 1))
    EAL.save_loaded_asset(mi)

    # flight deck: markings texture (A = paint), tiling nonskid normal, rougher nonskid vs smoother paint
    m = new_material("M_CarrierDeck", MAT)
    m.set_editor_property("two_sided", True)
    c = custom_node(m, """
float4 d = Deck.Sample(DeckSampler, UV);
float2 tuv = UV * float2(335.0 / 1.6, 92.0 / 1.6);
float3 n1 = Nonskid.Sample(NonskidSampler, tuv).xyz * 2 - 1;
float3 n2 = Nonskid.Sample(NonskidSampler, tuv * 0.23 + 0.37).xyz * 2 - 1;
float3 n = normalize(float3((n1.xy + n2.xy * 0.6) * lerp(1.0, 0.35, d.a), 1));
Rough = lerp(0.86, 0.52, d.a);
Nrm = n;
return d.rgb;
""", ["UV", "Deck", "Nonskid"], [("Rough", CMOT.CMOT_FLOAT1), ("Nrm", CMOT.CMOT_FLOAT3)])
    uv = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    conn(uv, "", c, "UV")
    conn(tex_obj(m, "Deck", "/Game/Carrier/Textures/T_CarrierDeck", -900, -100, ST.SAMPLERTYPE_COLOR, clamp=True), "", c, "Deck")
    conn(tex_obj(m, "Nonskid", "/Game/Carrier/Textures/T_Nonskid_N", -900, 0, ST.SAMPLERTYPE_NORMAL), "", c, "Nonskid")
    prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR); prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS); prop(c, "Nrm", unreal.MaterialProperty.MP_NORMAL)
    m.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)

    def simple(name, rgb, rough, metal=0.0, two=True, nanite_=True, skel=False):
        mm = new_material(name, MAT)
        mm.set_editor_property("two_sided", two)
        col = MEL.create_material_expression(mm, unreal.MaterialExpressionVectorParameter, -600, -100)
        col.set_editor_property("parameter_name", "Color"); col.set_editor_property("default_value", unreal.LinearColor(*rgb, 1))
        prop(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
        prop(scalar(mm, "Roughness", rough, -600, 50), "", unreal.MaterialProperty.MP_ROUGHNESS)
        prop(scalar(mm, "Metallic", metal, -600, 120), "", unreal.MaterialProperty.MP_METALLIC)
        mm.set_editor_property("used_with_nanite", nanite_)
        if skel:
            mm.set_editor_property("used_with_skeletal_mesh", True)
        MEL.recompile_material(mm); EAL.save_loaded_asset(mm)
        return mm

    simple("M_CarrierDark", (0.035, 0.037, 0.04), 0.6)
    simple("M_CarrierGlass", (0.01, 0.014, 0.018), 0.06, 0.4)
    simple("M_CarrierNet", (0.02, 0.02, 0.02), 0.95)
    simple("M_CarrierHangar", (0.30, 0.32, 0.33), 0.6)
    simple("M_CarrierHangarFloor", (0.05, 0.055, 0.06), 0.75)
    simple("M_CarrierJbd", (0.12, 0.125, 0.13), 0.55, 0.4)
    simple("M_Wire", (0.05, 0.05, 0.05), 0.35, 0.9, nanite_=False)
    simple("M_Hook", (0.09, 0.09, 0.085), 0.4, 0.6)
    simple("M_HelmetShell", (0.8, 0.8, 0.78), 0.45, skel=False, nanite_=False)
    simple("M_Visor", (0.01, 0.01, 0.012), 0.05, 0.6, nanite_=False)

    # lights (IFLOLS cells, datum, wave-off/cut, deck edge/centreline, drop line, hangar lamps): unlit emissive, colour x intensity
    m = new_material("M_CarrierLight", MAT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    col = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -600, -100)
    col.set_editor_property("parameter_name", "Color"); col.set_editor_property("default_value", unreal.LinearColor(1, 0.6, 0.1, 1))
    it = scalar(m, "Intensity", 20.0, -600, 50)
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 0)
    conn(col, "", mul, "A"); conn(it, "", mul, "B")
    prop(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    m.set_editor_property("used_with_nanite", False)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)

    # crew / pilot clothing on the UE mannequin: colour zones from the pre-skinned (reference-pose) position, facing +Y, cm
    m = new_material("M_CrewSuit", MAT)
    m.set_editor_property("used_with_skeletal_mesh", True)
    c = custom_node(m, """
float3 p = PS;
float ax = abs(p.x);
float3 col = Torso;
float r = 0.82;
if (p.z < 97.0) col = Pants;
if (p.z < 97.0 && p.z > 60.0 && GSuit > 0.5) col = lerp(Pants, float3(0.20, 0.19, 0.12), 0.8);
if (p.z < 12.0) { col = float3(0.03, 0.028, 0.025); r = 0.5; }
if (ax > 48.0 && p.z < 118.0 && p.z > 60.0) col = Gloves;
if (p.z > 150.0) { col = float3(0.035, 0.032, 0.03); }
if (Stripes > 0.5 && ((p.z > 126.0 && p.z < 129.5) || (p.z > 108.0 && p.z < 111.5)) && ax < 26.0) { col = float3(0.75, 0.75, 0.72); r = 0.35; }
if (Harness > 0.5 && p.z > 100.0 && p.z < 142.0 && ax < 23.0 && (abs(p.x - p.y * 0.0) < 6.0 || abs(abs(p.x) - 11.0) < 3.0)) col = float3(0.05, 0.05, 0.04);
Rough = r;
return col;
""", ["PS", "Torso", "Pants", "Gloves", "Stripes", "GSuit", "Harness"], [("Rough", CMOT.CMOT_FLOAT1)])
    ps = MEL.create_material_expression(m, unreal.MaterialExpressionPreSkinnedPosition, -1200, -200)
    vi = MEL.create_material_expression(m, unreal.MaterialExpressionVertexInterpolator, -1000, -200)   # pre-skinned position is vertex-only
    conn(ps, "", vi, "")
    conn(vi, "", c, "PS")
    for k, (pn, dv) in enumerate([("Torso", (0.8, 0.62, 0.05)), ("Pants", (0.03, 0.04, 0.08)), ("Gloves", (0.1, 0.08, 0.05))]):
        v = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, -100 + k * 80)
        v.set_editor_property("parameter_name", pn); v.set_editor_property("default_value", unreal.LinearColor(*dv, 1))
        conn(v, "", c, pn)
    for k, (pn, dv) in enumerate([("Stripes", 1.0), ("GSuit", 0.0), ("Harness", 0.0)]):
        conn(scalar(m, pn, dv, -900, 150 + k * 60), "", c, pn)
    prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR); prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)

    # wake / bow foam: deferred decal, foam from world-space noise, strength from the decal colour alpha (per segment MID)
    m = new_material("M_CarrierWake", MAT)
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    c = custom_node(m, """
float2 q = WP.xy / 100.0;
float n = 0;
float a = 1, f = 0.09, tot = 0;
for (int i = 0; i < 4; i++) { float2 pp = q * f + float2(T * 0.05 * (i + 1), -T * 0.03 * i); n += a * (sin(pp.x * 1.7 + sin(pp.y * 1.3)) * 0.5 + 0.5) * (sin(pp.y * 2.1 + sin(pp.x * 0.9 + 1.7)) * 0.5 + 0.5); tot += a; a *= 0.55; f *= 2.3; }
n /= tot;
float2 uv = DUV;
float lat = abs(uv.x - 0.5) * 2.0;
float edge = 1.0 - smoothstep(0.35 + Spread * 0.4, 1.0, lat);
float lon = 1.0 - smoothstep(0.75, 1.0, abs(uv.y - 0.5) * 2.0);
float foam = saturate((n - (0.62 - Strength * 0.35)) * 3.0) * edge * lon;
Op = saturate(foam * Strength * 1.6);
return float3(0.82, 0.86, 0.88);
""", ["WP", "DUV", "T", "Strength", "Spread"], [("Op", CMOT.CMOT_FLOAT1)])
    wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -200)
    duv = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, -100)
    tm = MEL.create_material_expression(m, unreal.MaterialExpressionTime, -900, 0)
    conn(wp, "", c, "WP"); conn(duv, "", c, "DUV"); conn(tm, "", c, "T")
    conn(scalar(m, "Strength", 1.0, -900, 80), "", c, "Strength"); conn(scalar(m, "Spread", 0.5, -900, 140), "", c, "Spread")
    prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR); prop(c, "Op", unreal.MaterialProperty.MP_OPACITY)
    prop(scalar(m, "Rough", 0.9, -600, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log("M3 MAT DONE")

# ---------------------------------------------------------------- carrier meshes
MAP = {"M_Hull": "M_CarrierHull", "M_Island": "MI_CarrierIsland", "M_Deck": "M_CarrierDeck", "M_Dark": "M_CarrierDark", "M_Glass": "M_CarrierGlass", "M_Net": "M_CarrierNet",
       "M_Hangar": "M_CarrierHangar", "M_HangarFloor": "M_CarrierHangarFloor", "M_Lamp": "M_CarrierLight", "M_Jbd": "M_CarrierJbd",
       "M_Yellow": "M_CarrierDeck", "M_HelmetShell": "M_HelmetShell", "M_Visor": "M_Visor", "M_Hook": "M_Hook"}


def assign(sm):
    for i, sn in enumerate(slot_names(sm)):
        base = sn.split(".")[0]
        tgt = MAP.get(base)
        if tgt and EAL.does_asset_exist(MAT + "/" + tgt):
            sm.set_material(i, unreal.load_asset(MAT + "/" + tgt))
    EAL.save_loaded_asset(sm)


if "carrier" in STEPS:
    for nm, nan in [("SM_CarrierHull", True), ("SM_CarrierDeck", True), ("SM_CarrierHangar", True), ("SM_CarrierElevator", False),
                    ("SM_CarrierJbd", False), ("SM_CarrierCollision", False), ("SM_Helmet", False)]:
        sm = import_mesh(os.path.join(CSRC, nm + ".glb"), "/Game/Carrier/Meshes", nm)
        if not sm:
            continue
        if nm == "SM_CarrierCollision":
            bs = sm.get_editor_property("body_setup")
            if bs:
                bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
                bs.set_editor_property("double_sided_geometry", True)   # the builder's mirrored winding: collide from both sides
        else:
            nanite(sm, nan)
            bs = sm.get_editor_property("body_setup")
            if bs:
                bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        assign(sm)
        bb = sm.get_bounding_box()
        log("mesh", nm, "slots", slot_names(sm), "bbox", bb.min, bb.max)
    log("M3 CARRIER DONE")

# ---------------------------------------------------------------- F-35C body, gear parts, hook (materials from the stage-2 f35c_static)
if "f35" in STEPS:
    ref = unreal.load_asset("/Game/F35C/f35c_static/StaticMeshes/f35c_static")
    refmats = {}
    if ref:
        for s in ref.get_editor_property("static_materials"):
            refmats[str(s.get_editor_property("material_slot_name")).split(".")[0]] = s.get_editor_property("material_interface")
    log("f35 ref slots", list(refmats.keys()))
    names = ["SM_F35Body"] + [f"SM_F35Gear{k}" for k in range(9)] + ["SM_F35Hook"]
    for nm in names:
        sm = import_mesh(os.path.join(FSRC, nm + ".glb"), "/Game/Carrier/F35", nm)
        if not sm:
            continue
        for i, sn in enumerate(slot_names(sm)):
            base = sn.split(".")[0]
            if base in refmats and refmats[base]:
                sm.set_material(i, refmats[base])
            elif base == "M_Hook":
                sm.set_material(i, unreal.load_asset(MAT + "/M_Hook"))
        nanite(sm, nm == "SM_F35Body")
        bs = sm.get_editor_property("body_setup")
        if bs:
            bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        EAL.save_loaded_asset(sm)
        bb = sm.get_bounding_box()
        log("f35", nm, "slots", slot_names(sm), "bbox", bb.min, bb.max)
    log("M3 F35 DONE")

# ---------------------------------------------------------------- LSO / deck audio (wav -> SoundWave)
if "audio" in STEPS and os.path.isdir(ASRC):
    tasks = []
    for f in sorted(glob.glob(os.path.join(ASRC, "*.wav"))):
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", f)
        t.set_editor_property("destination_path", "/Game/Carrier/Audio")
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    if tasks:
        AT.import_asset_tasks(tasks)
    log("audio assets", [p.split("/")[-1] for p in EAL.list_assets("/Game/Carrier/Audio", recursive=False)])
    log("M3 AUDIO DONE")

# mannequin: report the reference pose we rely on (head bone, hand positions) for the helmet offset + suit zones
sk = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
if sk:
    mats = [str(m.get_editor_property("material_slot_name")) for m in sk.get_editor_property("materials")]
    log("manny slots", mats)
log("M3 ALL DONE")
