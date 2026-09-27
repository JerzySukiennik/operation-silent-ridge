# Builds /Game/Maps/L_SilentRidge: Nanite terrain tiles, ocean, sky/sun/fog/clouds, fir ISMs, and the auto-playing LS_Flight sequence (spawnable F-35C + 3 CineCameras).
import unreal, os, sys, json, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_common import *
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MAP = "/Game/Maps/L_SilentRidge"
if EAL.does_asset_exist(MAP):
    EAL.delete_asset(MAP)
ok = LES.new_level(MAP)
log("new_level", ok)
world = unreal.EditorLevelLibrary.get_editor_world()

def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0), label=None):
    a = EAS.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=rot[0], pitch=rot[1], yaw=rot[2]))
    if label:
        a.set_actor_label(label)
    return a

# ---------------------------------------------------------------- terrain tiles
man = json.load(open(os.path.join(UET, "Niepotrzebne", "tiles", "manifest.json")))
n = 0
for t in man:
    mesh = unreal.load_asset("/Game/Terrain/" + t["name"] + "/" + t["name"])
    if not mesh:
        log("missing tile", t["name"]); continue
    a = spawn(unreal.StaticMeshActor, (t["x"], t["y"], 0.0), label=t["name"])
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_folder_path("Terrain")
    n += 1
log("tiles placed", n)

# ---------------------------------------------------------------- ocean
plane = unreal.load_asset("/Engine/BasicShapes/Plane")
oc = spawn(unreal.StaticMeshActor, (0, 0, 0), label="Ocean")
oc.static_mesh_component.set_static_mesh(plane)
oc.set_actor_scale3d(unreal.Vector(6000, 6000, 1))   # 600 km
oc.static_mesh_component.set_material(0, unreal.load_asset("/Game/Materials/M_Ocean"))
oc.static_mesh_component.set_editor_property("cast_shadow", False)
oc.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

# ---------------------------------------------------------------- sky, sun, fog, clouds, post
# Godot TO_SUN (-0.262, 0.643, 0.720) -> UE light forward = -(x, z, y) = (0.262, -0.720, -0.643): pitch -40, yaw -70
sun = spawn(unreal.DirectionalLight, (0, 0, 500000), (0, -40.0, -70.0), "Sun")
lc = sun.get_component_by_class(unreal.DirectionalLightComponent)
lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
lc.set_editor_property("intensity", float(os.environ.get("OSR_SUN_LUX", "10")))
lc.set_editor_property("atmosphere_sun_light", True)
lc.set_editor_property("cast_shadows", True)
try:
    lc.set_editor_property("cast_cloud_shadows", True)
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
cl = spawn(unreal.VolumetricCloud, (0, 0, 0), label="Clouds")
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

# ---------------------------------------------------------------- trees (Nanite ISM per 2 km cell)
fir = [unreal.load_asset(p) for p in EAL.list_assets("/Game/Trees", recursive=True)]
fir = [a for a in fir if isinstance(a, unreal.StaticMesh)][0]
cells = json.load(open(os.path.join(SRC, "trees.json")))
SDS = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
SDL = unreal.SubobjectDataBlueprintFunctionLibrary
total = 0
for key, inst in cells.items():
    try:
        a = spawn(unreal.Actor, (0, 0, 0), label="Firs_" + key)
        handles = SDS.k2_gather_subobject_data_for_instance(a)
        root = handles[0] if handles else None
        # actor spawned from Actor has no root: add a scene root first, then the ISM
        p = unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.InstancedStaticMeshComponent)
        h, fail = SDS.add_new_subobject(p)
        comp = SDL.get_object(SDL.get_data(h))
        comp.set_static_mesh(fir)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_cull_distances(float(os.environ.get("OSR_TREE_CULL0", "240000")), float(os.environ.get("OSR_TREE_CULL1", "280000")))
        xs = [unreal.Transform(unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw), unreal.Vector(s, s, s)) for x, y, z, yaw, s in inst]
        comp.add_instances(xs, False, True)
        a.set_folder_path("Trees")
        total += len(inst)
    except Exception as ex:
        log("tree cell failed", key, ex)
log("trees placed", total)

# ---------------------------------------------------------------- flight sequence
K = json.load(open(os.path.join(SRC, "flight_keys.json")))
FPS = K["fps"]
SEQ = "/Game/Seq/LS_Flight"
if EAL.does_asset_exist(SEQ):
    EAL.delete_asset(SEQ)
seq = AT.create_asset("LS_Flight", "/Game/Seq", unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
seq.set_display_rate(unreal.FrameRate(FPS, 1))
seq.set_playback_start(0)
seq.set_playback_end(int(round(K["total"] * FPS)))
LIN = unreal.MovieSceneKeyInterpolation.LINEAR
CONST = unreal.MovieSceneKeyInterpolation.CONSTANT

def transform_keys(binding, keysets):
    """keysets: list of (start_frame, locs, rots) ; rot = [roll, pitch, yaw] degrees."""
    tr = binding.add_track(unreal.MovieScene3DTransformTrack)
    sec = tr.add_section()
    sec.set_range(0, int(round(K["total"] * FPS)) + 1)
    ch = {str(c.channel_name): c for c in sec.get_all_channels()}
    names = ["Location.X", "Location.Y", "Location.Z", "Rotation.X", "Rotation.Y", "Rotation.Z"]
    miss = [x for x in names if x not in ch]
    if miss:
        log("channel names", list(ch.keys()))
        chans = list(sec.get_all_channels())[:6]
    else:
        chans = [ch[x] for x in names]
    for si, (f0, locs, rots) in enumerate(keysets):
        last = len(locs) - 1
        for i in range(len(locs)):
            interp = CONST if (i == last and si < len(keysets) - 1) else LIN
            vals = list(locs[i]) + list(rots[i])
            for c, v in zip(chans, vals):
                c.add_key(unreal.FrameNumber(f0 + i), float(v), 0.0, unreal.MovieSceneTimeUnit.DISPLAY_RATE, interp)
    return sec

def make_cam(label):
    c = spawn(unreal.CineCameraActor, (0, 0, 100000), label=label)
    cc = c.get_cine_camera_component()
    fb = cc.get_editor_property("filmback")
    fb.set_editor_property("sensor_width", 36.0); fb.set_editor_property("sensor_height", 20.25)
    cc.set_editor_property("filmback", fb)
    cc.set_editor_property("current_focal_length", 15.3)   # 16:9, vertical FOV ~67 deg like the Godot chase cam
    fs = cc.get_editor_property("focus_settings")
    fs.set_editor_property("focus_method", unreal.CameraFocusMethod.DISABLE)
    cc.set_editor_property("focus_settings", fs)
    cc.set_editor_property("current_aperture", 22.0)
    return c

jet = spawn(unreal.StaticMeshActor, (0, 0, 100000), label="F35C")
f35 = [unreal.load_asset(p) for p in EAL.list_assets("/Game/F35C", recursive=True)]
f35 = [a for a in f35 if isinstance(a, unreal.StaticMesh)][0]
jet.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
jet.static_mesh_component.set_static_mesh(f35)
jet.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

def add_bind(actor):
    try:
        b = seq.add_spawnable_from_instance(actor)
    except Exception as ex:
        log("add_spawnable failed, possessable", ex)
        b = seq.add_possessable(actor)
    return b

jb = add_bind(jet)
shots = K["shots"]
f_of = lambda t: int(round(t * FPS))
transform_keys(jb, [(f_of(s["start"]), s["jet_loc"], s["jet_rot"]) for s in shots if s["jet_loc"]])
cut = seq.add_track(unreal.MovieSceneCameraCutTrack)
cams = []
for s in shots:
    cam = make_cam("Cam_" + s["name"])
    cb = add_bind(cam)
    transform_keys(cb, [(f_of(s["start"]), s["cam_loc"], s["cam_rot"])])
    sec = cut.add_section()
    sec.set_range(f_of(s["start"]), f_of(s["start"] + s["dur"]))
    bid = None
    for fn in (lambda: seq.get_binding_id(cb), lambda: unreal.MovieSceneSequenceExtensions.get_portable_binding_id(seq, seq, cb), lambda: cb.get_binding_id() if hasattr(cb, "get_binding_id") else None):
        try:
            bid = fn()
            if bid is not None:
                break
        except Exception as ex:
            log("binding id attempt failed", ex)
    if bid is None:
        bid = unreal.MovieSceneObjectBindingID()
        bid.set_editor_property("guid", cb.get_id())
    try:
        sec.set_camera_binding_id(bid)
    except Exception as ex:
        log("camera binding failed", ex)
    cams.append(cam)
# spawnables keep their own templates: remove the level copies
for a in [jet] + cams:
    if seq.find_binding_by_name(a.get_actor_label()) is not None or True:
        pass
spawned_ok = all(b is not None for b in [jb])
EAL.save_loaded_asset(seq)
for a in [jet] + cams:
    try:
        EAS.destroy_actor(a)
    except Exception as ex:
        log("destroy", ex)
lsa = spawn(unreal.LevelSequenceActor, (0, 0, 0), label="FlightSequence")
lsa.set_sequence(seq)
pb = lsa.get_editor_property("playback_settings")
for k, v in [("auto_play", True), ("pause_at_end", True)]:
    try:
        pb.set_editor_property(k, v)
    except Exception as ex:
        log("playback prop", k, ex)
        try:
            lsa.set_editor_property(k, v)
        except Exception as ex2:
            log("actor prop", k, ex2)
try:
    lc_ = pb.get_editor_property("loop_count"); lc_.set_editor_property("value", 0); pb.set_editor_property("loop_count", lc_)
except Exception as ex:
    log("loop_count", ex)
lsa.set_editor_property("playback_settings", pb)
log("playback settings", pb)
ps2 = spawn(unreal.PlayerStart, (-2900000, 950000, 100000), label="PlayerStart")
LES.save_current_level()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("LEVEL DONE bindings", [b.get_display_name() for b in seq.get_bindings()], "tracks", len(seq.get_tracks()) if hasattr(seq, "get_tracks") else "?")
