# Downloads the CC0 Poly Haven rock scans (glTF, 2k textures) and extra CC0 terrain textures used by the v2 world into
# UE_Test/Niepotrzebne/assets/<id>/. Every asset is listed in UE_Test/CREDITS.md.
import json, os, sys, urllib.request
import tv2

OUT = os.path.join(tv2.ROOT, "UE_Test", "Niepotrzebne", "assets")
MODELS = ["namaqualand_cliff_02", "rock_face_01", "rock_face_02", "coastal_cliff_02", "boulder_01", "rock_09"]
TEXTURES = {"rock_face_03": "2k", "rocks_ground_06": "2k", "coast_sand_05": "2k"}
UA = {"User-Agent": "OperationSilentRidge-terrain-tool"}


def get(url, path):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA)) as r, open(path + ".part", "wb") as f:
        f.write(r.read())
    os.replace(path + ".part", path)
    print("got", os.path.relpath(path, OUT), flush=True)


def files(aid):
    with urllib.request.urlopen(urllib.request.Request("https://api.polyhaven.com/files/" + aid, headers=UA)) as r:
        return json.load(r)


for m in MODELS:
    g = files(m)["gltf"]["2k"]["gltf"]
    d = os.path.join(OUT, m)
    get(g["url"], os.path.join(d, os.path.basename(g["url"])))
    for rel, inc in g.get("include", {}).items():
        get(inc["url"], os.path.join(d, rel))
for t, res in TEXTURES.items():
    f = files(t)
    for kind in ["Diffuse", "nor_gl", "Rough", "AO", "Displacement"]:
        if kind in f and res in f[kind]:
            e = f[kind][res].get("png") or f[kind][res].get("jpg")
            get(e["url"], os.path.join(OUT, t, os.path.basename(e["url"])))
print("assets ok")
