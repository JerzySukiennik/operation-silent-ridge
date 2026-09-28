# World v2 build (editor Python, headless): imports the terrain v2 textures/tiles/rock scans/ocean grid, authors M_TerrainV2,
# M_OceanV2, M_RockScan (+ one instance per scan) and rebuilds /Game/Maps/L_SilentRidge with the v2 world.
# Steps via env OSR_V2 (comma list, default all): tex,tiles,rocks,mat,level,clean
import unreal, os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *

WORK = os.path.join(UET, "Niepotrzebne", "terrain_v2")
ASSETS = os.path.join(UET, "Niepotrzebne", "assets")
STEPS = os.environ.get("OSR_V2", "tex,tiles,rocks,mat,level,clean").split(",")
FORCE = os.environ.get("OSR_V2_FORCE", "") == "1"
ST = unreal.MaterialSamplerType
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
ROCKS = ["namaqualand_cliff_02", "rock_face_01", "rock_face_02", "coastal_cliff_02", "boulder_01", "rock_09"]


def nanite(mesh, on=True):
    ns = mesh.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", on)
    try:
        SMES.set_nanite_settings(mesh, ns, apply_changes=True)
    except Exception:
        mesh.set_editor_property("nanite_settings", ns)


def mesh_in(folder):
    for p in EAL.list_assets(folder, recursive=True, include_folder=False):
        a = unreal.load_asset(p.split(".")[0])
        if isinstance(a, unreal.StaticMesh):
            return a
    return None


# ---------------------------------------------------------------- textures
if "tex" in STEPS:
    T = os.path.join(WORK, "tex")
    for nm in ["T_mA_g", "T_mB_g", "T_mC_g", "T_mA_c", "T_mB_c", "T_mC_c", "T_mD_c", "T_bake_g", "T_ocean_g"]:
        import_texture(os.path.join(T, nm + ".png"), nm, "mask", force=True)
    for nm in ["T_rockface", "T_grav", "T_sand"]:
        import_texture(os.path.join(T, nm + "_A.png"), nm + "_A", "albedo", force=FORCE)
        import_texture(os.path.join(T, nm + "_N.png"), nm + "_N", "normal", force=FORCE)
    # trees read the bake by its stage-1 name: point it at the v2 bake
    import_texture(os.path.join(T, "T_bake_g.png"), "T_bake", "mask", force=True)
    log("V2 TEXTURES DONE")

# ---------------------------------------------------------------- rocks + ocean mesh
if "rocks" in STEPS:
    for rname in ROCKS:
        dest = "/Game/Rocks/" + rname
        if EAL.does_directory_exist(dest) and not FORCE:
            continue
        if EAL.does_directory_exist(dest):
            EAL.delete_directory(dest)
        import_file(os.path.join(ASSETS, rname, rname + "_2k.gltf"), dest)
        mesh = mesh_in(dest)
        if mesh:
            nanite(mesh, True); EAL.save_loaded_asset(mesh)
            bb = mesh.get_bounding_box(); log("rock", rname, "bbox", bb.min, bb.max)
    dest = "/Game/Ocean"
    if EAL.does_directory_exist(dest):
        EAL.delete_directory(dest)
    import_file(os.path.join(WORK, "SM_OceanGrid.glb"), dest)
    om = mesh_in(dest)
    om.set_material(0, unreal.load_asset("/Game/Materials/M_OceanV2") or unreal.load_asset("/Game/Materials/M_Ocean"))
    EAL.save_loaded_asset(om)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("V2 ROCKS DONE")

# ---------------------------------------------------------------- materials
if "mat" in STEPS:
    # terrain
    m = new_material("M_TerrainV2")
    m.set_editor_property("tangent_space_normal", False)
    texs = [("MAg", "T_mA_g", 1), ("MBg", "T_mB_g", 1), ("MCg", "T_mC_g", 1), ("MAc", "T_mA_c", 1), ("MBc", "T_mB_c", 1), ("MCc", "T_mC_c", 1),
            ("MDc", "T_mD_c", 1), ("Bake", "T_bake_g", 1),
            ("RockM", "T_rock_macro_A", 0), ("RockMN", "T_rock_macro_N", 2), ("RockF", "T_rockface_A", 0), ("RockFN", "T_rockface_N", 2),
            ("Rock", "T_rock_cliff_A", 0), ("RockN", "T_rock_cliff_N", 2), ("Grav", "T_grav_A", 0), ("GravN", "T_grav_N", 2),
            ("ScreeM", "T_scree_macro_A", 0), ("Snow", "T_snow_A", 0), ("SnowN", "T_snow_N", 2), ("SnowM", "T_snow_macro_A", 0),
            ("Ground", "T_ground_A", 0), ("GroundN", "T_ground_N", 2), ("Floor", "T_forest_floor_A", 0), ("Sand", "T_sand_A", 0),
            ("SandN", "T_sand_N", 2), ("Noise", "T_noise", 3)]
    stype = {0: ST.SAMPLERTYPE_COLOR, 1: ST.SAMPLERTYPE_LINEAR_COLOR, 2: ST.SAMPLERTYPE_NORMAL, 3: ST.SAMPLERTYPE_LINEAR_COLOR}
    code = open(os.path.join(HLSL, "terrain_v2.hlsl")).read()
    c = custom_node(m, code, ["P", "N", "Cam", "TreeFar", "BounceGain", "SunLux", "AOStrength"] + [t[0] for t in texs],
                    [("NormalWS", CMOT.CMOT_FLOAT3), ("Rough", CMOT.CMOT_FLOAT1), ("AOut", CMOT.CMOT_FLOAT1), ("Spec", CMOT.CMOT_FLOAT1), ("Emis", CMOT.CMOT_FLOAT3)])
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -400), "", c, "P")
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -900, -350), "", c, "N")
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, -900, -300), "", c, "Cam")
    for k, (pn, dv) in enumerate([("TreeFar", 2600.0), ("BounceGain", float(os.environ.get("OSR_BOUNCE", "1.0"))), ("SunLux", float(os.environ.get("OSR_SUN_LUX", "10"))),
                                  ("AOStrength", float(os.environ.get("OSR_AO", "1.0")))]):
        conn(scalar(m, pn, dv, -1100, -300 + k * 50), "", c, pn)
    for k, (pin, asset, kind) in enumerate(texs):
        conn(tex_obj(m, pin, "/Game/Textures/" + asset, -900, -200 + k * 60, stype[kind], clamp=(kind == 1)), "", c, pin)
    prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    prop(c, "NormalWS", unreal.MaterialProperty.MP_NORMAL)
    prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    prop(c, "AOut", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    prop(c, "Spec", unreal.MaterialProperty.MP_SPECULAR)
    prop(c, "Emis", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    m.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log("M_TerrainV2 done")

    # ocean: Gerstner WPO (vertex) + shading (pixel)
    m = new_material("M_OceanV2")
    m.set_editor_property("tangent_space_normal", False)
    otex = [("WaveA", "T_water_a", 2), ("WaveB", "T_water_b", 2), ("Noise", "T_noise", 3), ("Ocean", "T_ocean_g", 1)]
    c = custom_node(m, open(os.path.join(HLSL, "ocean_v2.hlsl")).read(), ["P", "Cam", "T"] + [t[0] for t in otex],
                    [("NormalWS", CMOT.CMOT_FLOAT3), ("Rough", CMOT.CMOT_FLOAT1), ("Spec", CMOT.CMOT_FLOAT1)])
    v = custom_node(m, open(os.path.join(HLSL, "ocean_wpo.hlsl")).read(), ["P", "Cam", "T", "Ocean"], [], -400, 600)
    wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -300)
    wpx = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 600)
    try:
        wpx.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    except Exception as ex:
        log("wpo worldpos flag", ex)
    cam = MEL.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, -900, -250)
    tm = MEL.create_material_expression(m, unreal.MaterialExpressionTime, -900, -200)
    conn(wp, "", c, "P"); conn(cam, "", c, "Cam"); conn(tm, "", c, "T")
    conn(wpx, "", v, "P"); conn(cam, "", v, "Cam"); conn(tm, "", v, "T")
    for k, (pin, asset, kind) in enumerate(otex):
        e = tex_obj(m, pin, "/Game/Textures/" + asset, -900, -100 + k * 60, stype[kind], clamp=(kind == 1))
        conn(e, "", c, pin)
        if pin == "Ocean":
            conn(e, "", v, "Ocean")
    prop(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    prop(c, "NormalWS", unreal.MaterialProperty.MP_NORMAL)
    prop(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    prop(c, "Spec", unreal.MaterialProperty.MP_SPECULAR)
    prop(v, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log("M_OceanV2 done")

    # rock scans: scan albedo turned into the terrain's grey rock (luminance x tint), scan normal map, baked sky visibility,
    # snow on up-facing surfaces where the terrain snow mask says so
    m = new_material("M_RockScan")
    alb = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    alb.set_editor_property("parameter_name", "Albedo"); alb.set_editor_property("texture", unreal.load_asset("/Game/Textures/T_rockface_A"))
    nrm = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 300)
    nrm.set_editor_property("parameter_name", "Normal"); nrm.set_editor_property("texture", unreal.load_asset("/Game/Textures/T_rockface_N"))
    nrm.set_editor_property("sampler_type", ST.SAMPLERTYPE_NORMAL)
    rcode = """
float3 wp = float3(P.x, P.z, P.y) * 0.01;
float2 uvg = (wp.xz + 40000.0) / 80000.0;
float2 uvc = (wp.xz - float2(-25000.0, -15000.0)) / float2(40000.0, 30000.0);
float cw = saturate(min(min(uvc.x, 1.0 - uvc.x) * 40000.0, min(uvc.y, 1.0 - uvc.y) * 30000.0) / 300.0);
float snowM = lerp(Texture2DSample(MAg, MAgSampler, uvg).r, Texture2DSample(MAc, MAcSampler, uvc).r, cw);
float skyv = lerp(Texture2DSample(Bake, BakeSampler, uvg).a, Texture2DSample(MCc, MCcSampler, uvc).a, cw);
float4 nz = Texture2DSample(Noise, NoiseSampler, wp.xz / 230.0 + 0.31);
float l = dot(A.rgb, float3(0.3, 0.59, 0.11));
float3 tint = lerp(float3(0.30, 0.30, 0.315), float3(0.33, 0.30, 0.27), smoothstep(0.45, 0.75, nz.g) * 0.8);
float3 col = lerp(tint * l * 3.1, A.rgb * 0.9, 0.18);
float up = saturate(Nv.z);
float sw = smoothstep(0.25, 0.75, saturate(snowM * 1.6) * smoothstep(0.55, 0.85, up));
col = lerp(col, float3(0.86, 0.88, 0.91), sw);
AOut = saturate(skyv * 1.05);
return col;
"""
    rc = custom_node(m, rcode, ["A", "P", "Nv", "MAg", "MAc", "MCc", "Bake", "Noise"], [("AOut", CMOT.CMOT_FLOAT1)], -500, 0)
    conn(alb, "RGBA", rc, "A")
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -200), "", rc, "P")
    conn(MEL.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -900, -150), "", rc, "Nv")
    for k, (pin, asset, kind) in enumerate([("MAg", "T_mA_g", 1), ("MAc", "T_mA_c", 1), ("MCc", "T_mC_c", 1), ("Bake", "T_bake_g", 1), ("Noise", "T_noise", 3)]):
        conn(tex_obj(m, pin, "/Game/Textures/" + asset, -1200, -100 + k * 60, stype[kind], clamp=(kind == 1)), "", rc, pin)
    prop(rc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    prop(rc, "AOut", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    prop(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    r = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -500, 300); r.set_editor_property("r", 0.85)
    prop(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("used_with_nanite", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log("M_RockScan done")
    # one instance per scan with its own textures
    for rname in ROCKS:
        folder = "/Game/Rocks/" + rname
        texs_ = [unreal.load_asset(p.split(".")[0]) for p in EAL.list_assets(folder, recursive=True, include_folder=False)]
        texs_ = [t for t in texs_ if isinstance(t, unreal.Texture2D)]
        d = next((t for t in texs_ if "diff" in t.get_name().lower() or "basecolor" in t.get_name().lower()), None)
        nm = next((t for t in texs_ if "nor" in t.get_name().lower()), None)
        mi_path = "/Game/Rocks/MI_" + rname
        mi = unreal.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else AT.create_asset("MI_" + rname, "/Game/Rocks", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, m)
        if d:
            MEL.set_material_instance_texture_parameter_value(mi, "Albedo", d)
        if nm:
            nm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP); nm.set_editor_property("srgb", False)
            EAL.save_loaded_asset(nm)
            MEL.set_material_instance_texture_parameter_value(mi, "Normal", nm)
        EAL.save_loaded_asset(mi)
        mesh = mesh_in(folder)
        if mesh:
            for i in range(len(mesh.static_materials)):
                mesh.set_material(i, mi)
            EAL.save_loaded_asset(mesh)
        log("rock material", rname, "albedo", d.get_name() if d else None, "normal", nm.get_name() if nm else None, "mesh", mesh.get_name() if mesh else None)
    # firs: stage-1 needles tint was lime-green; real conifer canopies are near-black green (albedo ~0.04-0.06)
    fm = unreal.load_asset("/Game/Materials/M_FirNeedles")
    mi_path = "/Game/Materials/MI_FirNeedlesDark"
    mi = unreal.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else AT.create_asset("MI_FirNeedlesDark", "/Game/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, fm)
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(float(os.environ.get("OSR_FIR_R", "0.25")), float(os.environ.get("OSR_FIR_G", "0.32")), float(os.environ.get("OSR_FIR_B", "0.26")), 1.0))
    EAL.save_loaded_asset(mi)
    fir = mesh_in("/Game/Trees")
    for i, sm in enumerate(fir.static_materials):
        if sm.material_interface and sm.material_interface.get_name() in ("M_FirNeedles", "MI_FirNeedlesDark"):
            fir.set_material(i, mi)
    EAL.save_loaded_asset(fir)
    om = mesh_in("/Game/Ocean")
    if om:
        om.set_material(0, unreal.load_asset("/Game/Materials/M_OceanV2")); EAL.save_loaded_asset(om)

# ---------------------------------------------------------------- tiles
if "tiles" in STEPS:
    man = json.load(open(os.path.join(WORK, "tiles", "manifest.json")))
    mat = unreal.load_asset("/Game/Materials/M_TerrainV2")
    if not mat:   # stub; the mat step rebuilds it in place (references stay valid)
        mat = new_material("M_TerrainV2"); mat.set_editor_property("used_with_nanite", True); EAL.save_loaded_asset(mat)
    for k, t in enumerate(man):
        dest = "/Game/TerrainV2/" + t["name"]
        if EAL.does_directory_exist(dest):
            if os.environ.get("OSR_V2_TILES_KEEP") == "1" and mesh_in(dest):
                continue
            EAL.delete_directory(dest)
        objs = import_file(os.path.join(WORK, "tiles", t["name"] + ".glb"), dest)
        mesh = next((o for o in objs if isinstance(o, unreal.StaticMesh)), None)
        if not mesh:
            log("tile import produced no mesh", t["name"]); continue
        mesh.set_material(0, mat)
        nanite(mesh, True)
        EAL.save_loaded_asset(mesh)
        if k % 20 == 0:
            log("tiles", k + 1, "/", len(man))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("V2 TILES DONE", len(man))

# ---------------------------------------------------------------- level
if "level" in STEPS:
    LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    SDS = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    SDL = unreal.SubobjectDataBlueprintFunctionLibrary
    MAP = "/Game/Maps/L_SilentRidge"
    if EAL.does_asset_exist(MAP):
        EAL.delete_asset(MAP)
    LES.new_level(MAP)

    def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0), label=None):
        a = EAS.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=rot[0], pitch=rot[1], yaw=rot[2]))
        if label:
            a.set_actor_label(label)
        return a

    man = json.load(open(os.path.join(WORK, "tiles", "manifest.json")))
    for t in man:
        mesh = mesh_in("/Game/TerrainV2/" + t["name"])
        a = spawn(unreal.StaticMeshActor, (t["x"], t["y"], 0.0), label=t["name"])
        c = a.static_mesh_component
        c.set_static_mesh(mesh); c.set_mobility(unreal.ComponentMobility.STATIC); c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        a.set_folder_path("Terrain")
    log("tiles placed", len(man))
    oc = spawn(unreal.StaticMeshActor, (0, 0, 0), label="Ocean")
    oc.static_mesh_component.set_static_mesh(mesh_in("/Game/Ocean"))
    oc.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    oc.static_mesh_component.set_editor_property("cast_shadow", False)
    oc.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    oc.static_mesh_component.set_editor_property("bounds_scale", 4.0)
    oc.tags = ["OsrOcean"]
    # sky, sun, fog, clouds, post (as stage 1/2)
    sun = spawn(unreal.DirectionalLight, (0, 0, 500000), (0, -40.0, -70.0), "Sun")
    lc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property("intensity", float(os.environ.get("OSR_SUN_LUX", "10")))
    lc.set_editor_property("atmosphere_sun_light", True)
    lc.set_editor_property("cast_shadows", True)
    try:
        lc.set_editor_property("cast_cloud_shadows", True)
        lc.set_editor_property("cloud_shadow_extent", 60.0)      # km (default 150): see DefaultEngine.ini cloud shadow notes
    except Exception as ex:
        log("cloud shadows prop", ex)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere")
    sky = spawn(unreal.SkyLight, (0, 0, 200000), label="SkyLight")
    slc = sky.get_component_by_class(unreal.SkyLightComponent)
    slc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    slc.set_editor_property("real_time_capture", True)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, 0), label="HeightFog")
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property("fog_density", float(os.environ.get("OSR_FOG", "0.006")))
    fc.set_editor_property("fog_height_falloff", 0.12)
    spawn(unreal.VolumetricCloud, (0, 0, 0), label="Clouds")
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), label="Post")
    ppv.set_editor_property("unbound", True)
    ps = ppv.get_editor_property("settings")
    for k, v in [("override_auto_exposure_bias", True), ("auto_exposure_bias", float(os.environ.get("OSR_EXPOSURE_BIAS", "0.0"))),
                 ("override_motion_blur_amount", True), ("motion_blur_amount", 0.35)]:
        try:
            ps.set_editor_property(k, v)
        except Exception as ex:
            log("pp prop", k, ex)
    ppv.set_editor_property("settings", ps)

    def ism_actor(label, mesh, xs, cull, folder):
        a = spawn(unreal.Actor, (0, 0, 0), label=label)
        handles = SDS.k2_gather_subobject_data_for_instance(a)
        p = unreal.AddNewSubobjectParams(parent_handle=handles[0] if handles else None, new_class=unreal.InstancedStaticMeshComponent)
        h, fail = SDS.add_new_subobject(p)
        comp = SDL.get_object(SDL.get_data(h))
        comp.set_static_mesh(mesh)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_cull_distances(cull[0], cull[1])
        comp.add_instances(xs, False, True)
        a.set_folder_path(folder)
        return a

    fir = mesh_in("/Game/Trees")
    cells = json.load(open(os.path.join(WORK, "trees.json")))
    tot = 0
    for key, inst in cells.items():
        xs = [unreal.Transform(unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw), unreal.Vector(s, s, s)) for x, y, z, yaw, s in inst]
        ism_actor("Firs_" + key, fir, xs, (float(os.environ.get("OSR_TREE_CULL0", "240000")), float(os.environ.get("OSR_TREE_CULL1", "280000"))), "Trees")
        tot += len(inst)
    log("trees placed", tot)
    rocks = json.load(open(os.path.join(WORK, "rocks.json")))
    MLIB = unreal.MathLibrary
    rtot = 0
    for rname, inst in rocks.items():
        mesh = mesh_in("/Game/Rocks/" + rname)
        if not mesh:
            log("missing rock mesh", rname); continue
        by = {}
        for r in inst:
            by.setdefault("%d_%d" % (int(r[0] // 400000), int(r[1] // 400000)), []).append(r)
        big = rname in ("namaqualand_cliff_02", "coastal_cliff_02", "rock_face_01", "rock_face_02")
        for key, rs in by.items():
            xs = []
            for r in rs:
                rot = MLIB.make_rotation_from_axes(unreal.Vector(*r[3:6]), unreal.Vector(*r[6:9]), unreal.Vector(*r[9:12]))
                xs.append(unreal.Transform(unreal.Vector(*r[0:3]), rot, unreal.Vector(r[12], r[12], r[12])))
            ism_actor("Rocks_%s_%s" % (rname, key), mesh, xs, (1200000.0, 1400000.0) if big else (500000.0, 600000.0), "Rocks")
            rtot += len(rs)
    log("rocks placed", rtot)
    spawn(unreal.PlayerStart, (-2900000, 950000, 100000), label="PlayerStart")
    LES.save_current_level()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("V2 LEVEL DONE")

# ---------------------------------------------------------------- remove stage-1 world assets no longer referenced (keeps the cook small)
if "clean" in STEPS:
    for p in ["/Game/Terrain", "/Game/Seq"]:
        if EAL.does_directory_exist(p):
            EAL.delete_directory(p); log("deleted", p)
    keep = set(t["name"] for t in json.load(open(os.path.join(WORK, "tiles", "manifest.json"))))
    for p in EAL.list_assets("/Game/TerrainV2", recursive=False, include_folder=True):
        nm = p.rstrip("/").split("/")[-1]
        if nm.startswith("SM_T2_") and nm not in keep:
            EAL.delete_directory("/Game/TerrainV2/" + nm); log("deleted orphan tile", nm)
    for a in ["/Game/Materials/M_Terrain", "/Game/Materials/M_Ocean", "/Game/Textures/T_masks_a", "/Game/Textures/T_masks_b", "/Game/Textures/T_ocean_depth"]:
        if EAL.does_asset_exist(a):
            EAL.delete_asset(a); log("deleted", a)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("V2 CLEAN DONE")
log("V2 ALL DONE", STEPS)
