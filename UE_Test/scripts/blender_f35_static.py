# Blender headless: bake the F-35C glTF at gear-up (frame 0), drop animations/armatures, join into one mesh, export a static GLB for UE.
import bpy, sys
argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
sc = bpy.context.scene
print("INFO objects", len(bpy.data.objects), "actions", len(bpy.data.actions), "armatures", len([o for o in bpy.data.objects if o.type == 'ARMATURE']))
for o in bpy.data.objects:
    if o.animation_data and o.animation_data.action:
        pass
sc.frame_set(0)
bpy.context.view_layer.update()
# bake world matrices at gear-up
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
mats = {o.name: o.matrix_world.copy() for o in meshes}
for o in bpy.data.objects:
    o.animation_data_clear()
for o in meshes:
    for m in list(o.modifiers):
        if m.type == 'ARMATURE':
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.modifier_apply(modifier=m.name)
for o in meshes:
    o.parent = None
    o.matrix_world = mats[o.name]
for o in [o for o in bpy.data.objects if o.type != 'MESH']:
    bpy.data.objects.remove(o)
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.join()
ob = bpy.context.view_layer.objects.active
ob.name = "F35C"
import mathutils, math
# nose is +Y in Blender (glTF -Z); rotate -90 deg about Z so the nose is Blender/glTF +X -> UE +X
ob.data.transform(mathutils.Matrix.Rotation(math.radians(-90), 4, "Z")); ob.data.update()
bpy.context.view_layer.update()
bb = [ob.matrix_world @ mathutils.Vector(c) for c in ob.bound_box]
mn = [min(v[i] for v in bb) for i in range(3)]; mx = [max(v[i] for v in bb) for i in range(3)]
print("INFO tris", sum(len(p.vertices) - 2 for p in ob.data.polygons), "bbox", mn, mx, "mats", len(ob.data.materials))
bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB', export_animations=False, export_skins=False, export_apply=True)
print("INFO exported", dst)
