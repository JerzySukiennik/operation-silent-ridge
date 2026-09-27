# Writes Content/Data/terrain.osrh for the C++ height lookup: header, 16x16 Nanite tile step map (0 = no tile / sea), raw 8192^2 R16 heightfield (h = -1024 + v/65535*5120).
import numpy as np, json, os, struct
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "game/assets/world/src/height.r16")
man = json.load(open(os.path.join(ROOT, "UE_Test/Niepotrzebne/tiles/manifest.json")))
steps = np.zeros((16, 16), np.uint8)
for t in man:
    i, j = int(t["name"][8:10]), int(t["name"][11:13])
    steps[j, i] = t["step"]
out = os.path.join(ROOT, "UE_Test/SilentRidgeUE/Content/Data/terrain.osrh")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "wb") as f:
    f.write(b"OSRH" + struct.pack("<IIff", 8192, 16, -1024.0, 5120.0))
    f.write(steps.tobytes())
    f.write(open(SRC, "rb").read())
print(out, os.path.getsize(out), "tiles", int((steps > 0).sum()))
