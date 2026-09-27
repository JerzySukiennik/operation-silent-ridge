#!/usr/bin/env python3
# Writes Godot .import presets for the world textures (VRAM-compressed, mipmapped; BC5 normals, BC7 masks) so runtime-loaded textures import correctly.
import glob
import os

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))

TEMPLATE = """[remap]

importer="texture"
type="CompressedTexture2D"

[params]

compress/mode={mode}
compress/high_quality={hq}
compress/lossy_quality=0.7
compress/hdr_compression=1
compress/normal_map={normal}
compress/channel_pack=0
mipmaps/generate=true
mipmaps/limit=-1
roughness/mode=0
process/fix_alpha_border={fab}
process/premult_alpha=false
process/normal_map_invert_y=false
process/hdr_as_srgb=false
process/size_limit=0
detect_3d/compress_to=0
"""


def main():
    files = glob.glob(os.path.join(GAME, "assets", "world", "textures", "*.png"))
    files += glob.glob(os.path.join(GAME, "assets", "world", "terrain", "*.png"))
    files += glob.glob(os.path.join(GAME, "assets", "world", "trees", "*.png"))
    for f in files:
        b = os.path.basename(f)
        normal = "normal" in b
        mask = b.startswith("masks_") or b == "noise.png"
        billboard = "billboard" in b or "twig" in b
        # masks stay lossless (exact channels; CPU BC7 of 4096^2 takes ~10 min per import)
        txt = TEMPLATE.format(mode=0 if mask else 2, hq="false", normal=1 if normal else 2,
                              fab="true" if billboard else "false")
        imp = f + ".import"
        old = open(imp).read() if os.path.exists(imp) else ""
        keys = [l for l in txt.splitlines() if l.startswith(("compress/mode", "compress/high_quality", "compress/normal_map", "mipmaps/generate"))]
        if old and all(k in old.splitlines() for k in keys):
            continue
        open(imp, "w").write(txt)
        print("wrote", os.path.relpath(imp, GAME))


if __name__ == "__main__":
    main()
