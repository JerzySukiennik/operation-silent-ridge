"""F-35C for carrier ops: splits the Godot GLB (game/assets/models/f35c/f35c.glb, baked gear animation on 9 rigid parts) into
a body mesh without the animated parts, one GLB per gear part (pivot = node origin at gear-up), a procedural tailhook, and a
generated C++ header with the sampled gear keyframes + wheel contact points (UE jet-local cm: X nose, Y right, Z up).
Usage: Blender -b --python blender_f35_parts.py -- <src.glb> <out_dir> <header.h>"""
import bpy, bmesh, sys, os, math
from mathutils import Matrix, Vector, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT, HDR = argv[0], argv[1], argv[2]
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
sc = bpy.context.scene

# assign each per-object action to its object (the importer only binds one)
for o in bpy.data.objects:
    a = bpy.data.actions.get(o.name)
    if a and o.type == "MESH":
        o.animation_data_create()
        o.animation_data.action = a
        try:
            if len(a.slots):
                o.animation_data.action_slot = a.slots[0]
        except Exception:
            pass
parts = [o for o in bpy.data.objects if o.type == "MESH" and o.animation_data and o.animation_data.action]
statics = [o for o in bpy.data.objects if o.type == "MESH" and o not in parts]
F0, F1 = 1, 175
print("INFO parts", [p.name for p in parts])

ROT = Matrix.Rotation(math.radians(-90), 4, "Z")        # same as blender_f35_static.py: nose -> +X (Blender), glTF/UE +X
# Blender (after ROT) -> UE: (x, -y, z) * 100
S = Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))


def ue_vec(v):
    return Vector((v.x * 100.0, -v.y * 100.0, v.z * 100.0))


def world(o, f):
    sc.frame_set(f)
    bpy.context.view_layer.update()
    return ROT @ o.matrix_world.copy()


# ---------------------------------------------------------------- sample keyframes
N = 25
rest = {p.name: world(p, F0) for p in parts}
keys = {p.name: [] for p in parts}
for i in range(N):
    f = F0 + (F1 - F0) * i / (N - 1)
    sc.frame_set(int(f), subframe=f - int(f))
    bpy.context.view_layer.update()
    for p in parts:
        W = ROT @ p.matrix_world.copy()
        D = W @ rest[p.name].inverted()                   # delta from gear-up, in the rotated Blender frame
        piv = rest[p.name].to_translation()
        loc = D @ piv                                     # where the pivot is now
        R = D.to_3x3()
        Rue = S.to_3x3() @ R @ S.to_3x3()                 # mirror conjugation -> UE frame rotation
        ax = [Rue.col[0], Rue.col[1], Rue.col[2]]
        keys[p.name].append((ue_vec(loc), ax))

# wheel contact points at gear-down (lowest vertices of the leg meshes, UE cm)
sc.frame_set(F1)
bpy.context.view_layer.update()
contacts = []
for p in parts:
    W = ROT @ p.matrix_world
    vs = [W @ v.co for v in p.data.vertices]
    lo = min(vs, key=lambda v: v.z)
    contacts.append((p.name, ue_vec(lo), len(vs)))
contacts.sort(key=lambda c: c[1].z)
print("INFO lowest points at gear-down", [(c[0], tuple(round(x, 1) for x in c[1]), c[2]) for c in contacts[:5]])

# ---------------------------------------------------------------- export parts (geometry at gear-up, pivot at the origin)
sc.frame_set(F0)
bpy.context.view_layer.update()


def export_obj(o, M, fname, pivot):
    me = o.data.copy()
    me.transform(Matrix.Translation(-pivot) @ M)
    ob = bpy.data.objects.new(fname, me)
    sc.collection.objects.link(ob)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, fname + ".glb"), export_format="GLB", use_selection=True,
                              export_animations=False, export_skins=False, export_apply=True, export_image_format="NONE")
    return ob


part_names = []
for k, p in enumerate(parts):
    M = rest[p.name]
    pv = M.to_translation()
    nm = f"SM_F35Gear{k}"
    export_obj(p, M, nm, pv)
    part_names.append((nm, p.name))

# body: every non-animated mesh joined at gear-up
meshes = []
for o in statics:
    me = o.data.copy()
    me.transform(ROT @ o.matrix_world)
    meshes.append(me)
bm = bmesh.new()
for me in meshes:
    bm.from_mesh(me)
body_me = bpy.data.meshes.new("SM_F35Body")
bm.to_mesh(body_me)
bm.free()
mats = []
for o in statics:
    for m in o.data.materials:
        if m not in mats:
            mats.append(m)
# rebuild material indices: from_mesh keeps per-mesh indices, so remap per source mesh
body_me = bpy.data.meshes.new("SM_F35Body")
bm = bmesh.new()
for o, me in zip(statics, meshes):
    tmp = bmesh.new()
    tmp.from_mesh(me)
    for f in tmp.faces:
        src = o.data.materials[f.material_index] if f.material_index < len(o.data.materials) else None
        f.material_index = mats.index(src) if src in mats else 0
    tmp.to_mesh(me)
    tmp.free()
    bm.from_mesh(me)
bm.to_mesh(body_me)
bm.free()
for m in mats:
    body_me.materials.append(m)
body = bpy.data.objects.new("SM_F35Body", body_me)
sc.collection.objects.link(body)
bpy.ops.object.select_all(action="DESELECT")
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "SM_F35Body.glb"), export_format="GLB", use_selection=True,
                          export_animations=False, export_skins=False, export_apply=True, export_image_format="NONE")
bvs = [v.co for v in body_me.vertices]
print("INFO body verts", len(bvs), "bbox", [round(min(v[i] for v in bvs), 2) for i in range(3)], [round(max(v[i] for v in bvs), 2) for i in range(3)])

# ---------------------------------------------------------------- tailhook (own work): pivot under the aft fuselage, arm points aft
# find the fuselage underside around x = -2.9 m (aft of the main gear, forward of the nozzle)
HX = -2.9
under = [v for v in bvs if abs(v.x - HX) < 0.25 and abs(v.y) < 0.35]
hz = min(v.z for v in under) + 0.06 if under else -0.75
print("INFO hook pivot z", round(hz, 3))
hk = bmesh.new()


def add_box(bmx, c, hx, hy, hz_, taper=1.0):
    x0, x1 = c[0] - hx, c[0] + hx
    v = []
    for (x, s) in ((x0, taper), (x1, 1.0)):
        for (y, z) in ((-hy * s, -hz_ * s), (hy * s, -hz_ * s), (hy * s, hz_ * s), (-hy * s, hz_ * s)):
            v.append(bmx.verts.new((x, c[1] + y, c[2] + z)))
    for f in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
        bmx.faces.new([v[i] for i in f])


ARM = 2.55
add_box(hk, (-ARM / 2, 0, 0), ARM / 2, 0.07, 0.07, 0.75)                 # shank (thin end aft)
add_box(hk, (-ARM - 0.12, 0, -0.07), 0.16, 0.11, 0.09)                    # hook point
add_box(hk, (-0.05, 0, 0.03), 0.18, 0.16, 0.08)                           # pivot fitting
hme = bpy.data.meshes.new("SM_F35Hook")
hk.to_mesh(hme)
hk.free()
hm = bpy.data.materials.new("M_Hook")
hm.use_nodes = True
b = next(n for n in hm.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
b.inputs["Base Color"].default_value = (0.18, 0.18, 0.17, 1)
b.inputs["Metallic"].default_value = 0.6
b.inputs["Roughness"].default_value = 0.4
hme.materials.append(hm)
hob = bpy.data.objects.new("SM_F35Hook", hme)
sc.collection.objects.link(hob)
bpy.ops.object.select_all(action="DESELECT")
hob.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "SM_F35Hook.glb"), export_format="GLB", use_selection=True, export_apply=True)
hook_pivot = ue_vec(Vector((HX, 0.0, hz)))

# ---------------------------------------------------------------- header
L = []
w = L.append
w("// Generated by UE_Test/scripts/carrier/blender_f35_parts.py - do not edit. F-35C gear animation sampled from the Godot GLB")
w("// (gear-up -> gear-down), rigid parts; UE jet-local cm (X nose, Y right, Z up), pivot position + rotated axes per sample.")
w("#pragma once")
w("")
w('#include "CoreMinimal.h"')
w("")
w("namespace CarrierF35Anim")
w("{")
w(f"\tconstexpr int32 NumParts = {len(parts)};")
w(f"\tconstexpr int32 NumSamples = {N};")
w("\tinline const TCHAR* PartMeshes[NumParts] = {" + ", ".join(f'TEXT("{n}")' for n, _ in part_names) + "};")
w("\tstruct FKey { float L[3]; float X[3]; float Y[3]; float Z[3]; };")
w("\tinline const FKey Keys[NumParts][NumSamples] = {")
for nm, src in part_names:
    rows = []
    for (loc, ax) in keys[src]:
        rows.append("{{%.2f, %.2f, %.2f}, {%.5f, %.5f, %.5f}, {%.5f, %.5f, %.5f}, {%.5f, %.5f, %.5f}}" % (
            loc.x, loc.y, loc.z, ax[0].x, ax[0].y, ax[0].z, ax[1].x, ax[1].y, ax[1].z, ax[2].x, ax[2].y, ax[2].z))
    w("\t\t{" + ", ".join(rows) + "},")
w("\t};")
lo = contacts[0][1]
# nose wheel = lowest point with the largest X among the lowest few; mains = the two lowest with |Y| > 50 cm
nose = max(contacts[:6], key=lambda c: c[1].x)[1]
mains = [c[1] for c in contacts if abs(c[1].y) > 50.0][:2]
w(f"\tinline const FVector NoseWheel({nose.x:.1f}, {nose.y:.1f}, {nose.z:.1f});")
if len(mains) == 2:
    w(f"\tinline const FVector MainWheelL({mains[0].x:.1f}, {-abs(mains[0].y):.1f}, {mains[0].z:.1f});")
    w(f"\tinline const FVector MainWheelR({mains[1].x:.1f}, {abs(mains[1].y):.1f}, {mains[1].z:.1f});")
w(f"\tinline const FVector HookPivot({hook_pivot.x:.1f}, {hook_pivot.y:.1f}, {hook_pivot.z:.1f});")
w(f"\tconstexpr double HookArm = {ARM * 100 + 25:.1f};   // pivot -> hook point, cm")
w("}")
open(HDR, "w").write("\n".join(L) + "\n")
print("INFO header", HDR, "contacts", [(c[0], tuple(round(x) for x in c[1])) for c in contacts[:8]])
