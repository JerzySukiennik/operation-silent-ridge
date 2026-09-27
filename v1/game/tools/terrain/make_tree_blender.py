# Blender (background) script: builds a low-poly Cascades conifer from branch cards, exports GLB and renders impostor views (albedo+AO, normals).
"""
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P game/tools/terrain/make_tree_blender.py -- <game dir>
Writes game/assets/world/trees/fir.glb and game/assets/world/src/tree/imp_*.png (composed into an atlas by make_impostor_atlas.py).
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

GAME = sys.argv[sys.argv.index("--") + 1]
SRC = os.path.join(GAME, "assets", "world", "src")
TREES = os.path.join(GAME, "assets", "world", "trees")
os.makedirs(TREES, exist_ok=True)
random.seed(11)

HEIGHT = 26.0
CROWN_BASE = 2.2
RMAX = 3.6


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def image(path):
    return bpy.data.images.load(path, check_existing=True)


def material(name, tex, alpha=None, normal=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = image(tex)
    nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    if alpha:
        nt.links.new(t.outputs["Alpha"], bsdf.inputs["Alpha"])
        m.blend_method = "CLIP" if hasattr(m, "blend_method") else None
    bsdf.inputs["Roughness"].default_value = 0.85
    return m


def crown_radius(z):
    t = (z - CROWN_BASE) / (HEIGHT - CROWN_BASE)
    return RMAX * max(0.0, 1.0 - t) ** 0.72 * (0.85 + 0.15 * math.sin(t * 9.0))


def build_tree():
    mesh = bpy.data.meshes.new("fir")
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    col = bm.loops.layers.color.new("Col")
    # trunk: 7-sided cone (material 1)
    sides = 7
    rings = [(0.0, 0.42), (HEIGHT * 0.5, 0.13), (HEIGHT - 1.2, 0.015)]
    rv = []
    for z, r in rings:
        ring = []
        for i in range(sides):
            a = i / sides * math.tau
            ring.append(bm.verts.new((math.cos(a) * r, math.sin(a) * r, z)))
        rv.append(ring)
    for k in range(len(rings) - 1):
        for i in range(sides):
            a, b = rv[k][i], rv[k][(i + 1) % sides]
            c, d = rv[k + 1][(i + 1) % sides], rv[k + 1][i]
            f = bm.faces.new((a, b, c, d))
            f.material_index = 1
            for loop, (u, v) in zip(f.loops, ((i / sides, rings[k][0] / 4), ((i + 1) / sides, rings[k][0] / 4),
                                              ((i + 1) / sides, rings[k + 1][0] / 4), (i / sides, rings[k + 1][0] / 4))):
                loop[uv].uv = (u, v)
                loop[col] = (0.5, 0.5, 0.5, 1.0)
    # branch cards (material 0): whorls of drooping, rolled quads with 2 segments
    z = CROWN_BASE
    whorl = 0
    while z < HEIGHT - 0.6:
        r = crown_radius(z)
        n = 4 if r < 1.0 else 6 if r < 2.2 else 7
        for i in range(n):
            yaw = (i + random.uniform(-0.3, 0.3)) / n * math.tau + whorl * 2.1
            taper = min(1.0, max(0.25, (HEIGHT - z) / 5.0))
            length = max(0.9 * taper, r * random.uniform(0.9, 1.2))
            width = max(0.85 * taper, length * 0.8)
            droop = math.radians(random.uniform(12, 28)) * (0.6 + 0.4 * (1 - z / HEIGHT))
            tip = math.radians(random.uniform(-8, 14))  # tips curl up a little
            roll = math.radians(random.choice((-1, 1)) * random.uniform(20, 55))
            if taper < 0.6:
                roll = math.radians(random.uniform(-15, 15))
                droop = -math.radians(40)  # topmost sprays point upwards into a spire
                tip = 0.0
            rot = Matrix.Rotation(yaw, 4, "Z")
            pts = []
            for j, s in enumerate((0.0, 0.55, 1.0)):
                ang = droop if j < 2 else droop - tip
                x = s * length
                zz = -math.tan(droop) * min(s, 0.55) * length - (math.tan(droop - tip) * (s - 0.55) * length if s > 0.55 else 0.0)
                w = width * (0.35 + 0.65 * min(1.0, s * 2.2)) * (1.0 - 0.25 * s)
                off = Vector((0.0, math.cos(roll) * w * 0.5, math.sin(roll) * w * 0.5))
                c = Vector((x, 0.0, zz))
                pts.append((rot @ (c - off), rot @ (c + off), s))
            verts = []
            for a, b, s in pts:
                verts.append((bm.verts.new(a + Vector((0, 0, z))), bm.verts.new(b + Vector((0, 0, z))), s))
            for j in range(2):
                a0, b0, s0 = verts[j]
                a1, b1, s1 = verts[j + 1]
                f = bm.faces.new((a0, a1, b1, b0))
                f.material_index = 0
                inner = 0.35 + 0.65 * s0
                for loop, (u, v, ao) in zip(f.loops, ((s0, 1.0, s0), (s1, 1.0, s1), (s1, 0.0, s1), (s0, 0.0, s0))):
                    loop[uv].uv = (0.02 + u * 0.96, v)
                    hz = z / HEIGHT
                    shade = (0.35 + 0.65 * (0.4 + 0.6 * ao)) * (0.55 + 0.45 * hz)
                    loop[col] = (shade, shade, shade, 1.0)
        z += random.uniform(0.75, 1.0) * (0.85 + 0.3 * (z / HEIGHT))
        whorl += 1
    # leader: two crossed small cards at the top
    for k in range(0):  # no leader cards: they read as crosses on the impostors
        a = k * math.pi / 2
        dx, dy = math.cos(a) * 0.22, math.sin(a) * 0.22
        vs = [bm.verts.new((-dx, -dy, HEIGHT - 2.4)), bm.verts.new((dx, dy, HEIGHT - 2.4)),
              bm.verts.new((dx * 0.2, dy * 0.2, HEIGHT + 0.3)), bm.verts.new((-dx * 0.2, -dy * 0.2, HEIGHT + 0.3))]
        f = bm.faces.new(vs)
        f.material_index = 0
        for loop, (u, v) in zip(f.loops, ((0.3, 0.0), (0.3, 1.0), (1.0, 1.0), (1.0, 0.0))):
            loop[uv].uv = (u, v)
            loop[col] = (1, 1, 1, 1)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("fir", mesh)
    bpy.context.scene.collection.objects.link(obj)
    mesh.materials.append(material("needles", os.path.join(SRC, "tree", "branch_albedo.png"), alpha=True))
    mesh.materials.append(material("bark", os.path.join(SRC, "polyhaven", "fir_tree_01", "fir_tree_01_bark_diff_2k.jpg")))
    # volumetric foliage normals: point away from the trunk axis and upwards
    mesh.shade_smooth() if hasattr(mesh, "shade_smooth") else None
    normals = []
    for loop in mesh.loops:
        p = mesh.vertices[loop.vertex_index].co
        poly_mat = None
        n = Vector((p.x, p.y, 0.0))
        if n.length < 1e-4:
            n = Vector((0, 0, 1))
        n = n.normalized() * 0.8 + Vector((0, 0, 0.6))
        normals.append(n.normalized())
    for poly in mesh.polygons:
        if poly.material_index == 1:
            for li in poly.loop_indices:
                p = mesh.vertices[mesh.loops[li].vertex_index].co
                normals[li] = Vector((p.x, p.y, 0.0)).normalized()
    mesh.normals_split_custom_set(normals)
    return obj


def export(obj):
    # the game shades the tree with its own shaders: export geometry + material slots only (no image references)
    for m in obj.data.materials:
        for nd in list(m.node_tree.nodes):
            if nd.type == "TEX_IMAGE":
                m.node_tree.nodes.remove(nd)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    kw = dict(filepath=os.path.join(TREES, "fir.glb"), use_selection=True, export_format="GLB", export_yup=True,
              export_normals=True, export_materials="EXPORT")
    try:
        bpy.ops.export_scene.gltf(export_vertex_color="ACTIVE", **kw)
    except TypeError:
        bpy.ops.export_scene.gltf(export_colors=True, **kw)
    print("exported fir.glb, tris:", sum(len(p.vertices) - 2 for p in obj.data.polygons))


def emission_override(obj, mode):
    """mode 'albedo': emit texture colour * vertex colour; 'normal': emit camera-space normal."""
    for m in obj.data.materials:
        nt = m.node_tree
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        tex = next((n for n in nt.nodes if n.type == "TEX_IMAGE"), None)
        for n in list(nt.nodes):
            if n.type in ("EMISSION", "MIX_SHADER", "BSDF_TRANSPARENT", "VECT_TRANSFORM", "VERTEX_COLOR", "MIX_RGB", "VECT_MATH", "ATTRIBUTE"):
                nt.nodes.remove(n)
        em = nt.nodes.new("ShaderNodeEmission")
        if mode == "albedo":
            vc = nt.nodes.new("ShaderNodeVertexColor")
            vc.layer_name = "Col"
            mul = nt.nodes.new("ShaderNodeMixRGB")
            mul.blend_type = "MULTIPLY"
            mul.inputs[0].default_value = 1.0
            nt.links.new(tex.outputs["Color"], mul.inputs[1])
            nt.links.new(vc.outputs["Color"], mul.inputs[2])
            nt.links.new(mul.outputs[0], em.inputs["Color"])
        else:
            geo = nt.nodes.new("ShaderNodeNewGeometry")
            vt = nt.nodes.new("ShaderNodeVectorTransform")
            vt.vector_type = "NORMAL"
            vt.convert_from = "WORLD"
            vt.convert_to = "CAMERA"
            sc = nt.nodes.new("ShaderNodeVectorMath")
            sc.operation = "MULTIPLY_ADD"
            sc.inputs[1].default_value = (0.5, 0.5, 0.5)
            sc.inputs[2].default_value = (0.5, 0.5, 0.5)
            nt.links.new(geo.outputs["Normal"], vt.inputs["Vector"])
            nt.links.new(vt.outputs["Vector"], sc.inputs[0])
            nt.links.new(sc.outputs["Vector"], em.inputs["Color"])
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        if m.name.startswith("needles"):
            nt.links.new(tex.outputs["Alpha"], mix.inputs[0])
        else:
            mix.inputs[0].default_value = 1.0
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(em.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], out.inputs["Surface"])


def render_views(obj):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 24
    sc.cycles.use_denoising = False
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.view_settings.view_transform = "Standard"
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0, 0, 0)
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    half_w = RMAX + 1.0
    out = os.path.join(SRC, "tree")
    for mode in ("albedo", "normal"):
        emission_override(obj, mode)
        # side views: 4 azimuths
        sc.render.resolution_x, sc.render.resolution_y = 384, 1024
        cam_data.ortho_scale = HEIGHT + 1.0 + 0.6  # fits height; width follows aspect 1:4
        for k in range(4):
            a = k * math.pi / 2 + 0.4
            d = Vector((math.cos(a), math.sin(a), 0.0))
            cam.location = d * 60 + Vector((0, 0, (HEIGHT + 0.6) / 2 - 0.3))
            cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
            sc.render.filepath = os.path.join(out, f"imp_{mode}_side{k}.png")
            bpy.ops.render.render(write_still=True)
        # top view
        sc.render.resolution_x, sc.render.resolution_y = 512, 512
        cam_data.ortho_scale = 2 * half_w
        cam.location = Vector((0, 0, 80))
        cam.rotation_euler = (0, 0, 0)
        sc.render.filepath = os.path.join(out, f"imp_{mode}_top.png")
        bpy.ops.render.render(write_still=True)
    print("impostor views rendered; side ortho height", HEIGHT + 1.6, "m, aspect 3:8")


clear()
tree = build_tree()
render_views(tree)
export(tree)
