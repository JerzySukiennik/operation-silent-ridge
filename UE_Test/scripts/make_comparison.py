# Builds side-by-side Godot (left) vs UE 5.7 (right) images for the same 5 moments, plus one contact sheet.
# Usage: make_comparison.py <ue_run_dir> <out_dir> [label]
import sys, os
from PIL import Image, ImageDraw, ImageFont
GS = "/Users/jurek/Downloads/Claude/Projects/Operation Silent Ridge/Niepotrzebne/work"
ue, out = sys.argv[1], sys.argv[2]; label = sys.argv[3] if len(sys.argv) > 3 else "UE 5.7"
os.makedirs(out, exist_ok=True)
pairs = [("1_ocean_spawn", GS + "/hp_godot/spawn_ocean.png", "shot_03s.png", "ocean spawn (chase cam, 600 m)"),
         ("2_canyon_entrance", GS + "/hp_godot/canyon_00.png", "shot_13s.png", "canyon entrance (+5 s)"),
         ("3_canyon_inside_a", GS + "/hp_godot/canyon_03.png", "shot_28s.png", "inside canyon (+20 s)"),
         ("4_canyon_inside_b", GS + "/hp_godot/canyon_06.png", "shot_43s.png", "inside canyon (+35 s)"),
         ("5_overview", GS + "/agent-world/final_shots/overview_4000m.jpg", "shot_61s.png", "overview 4000 m")]
try:
    font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 34)
except Exception:
    font = ImageFont.load_default()
W, H = 960, 540
rows = []
for name, g, u, cap in pairs:
    up = os.path.join(ue, u)
    if not (os.path.exists(g) and os.path.exists(up)):
        print("missing", name, os.path.exists(g), os.path.exists(up)); continue
    a = Image.open(g).convert("RGB").resize((W, H), Image.LANCZOS); b = Image.open(up).convert("RGB").resize((W, H), Image.LANCZOS)
    im = Image.new("RGB", (W * 2 + 8, H + 50), (20, 20, 20)); im.paste(a, (0, 50)); im.paste(b, (W + 8, 50))
    d = ImageDraw.Draw(im)
    d.text((12, 8), "Godot 4.6 v1 - " + cap, fill=(255, 255, 255), font=font)
    d.text((W + 20, 8), label + " - " + cap, fill=(255, 255, 255), font=font)
    im.save(os.path.join(out, name + ".jpg"), quality=90); rows.append(im)
if rows:
    sheet = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)), (0, 0, 0))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y)); y += r.height
    sheet.save(os.path.join(out, "comparison_sheet.jpg"), quality=88)
    print("wrote", len(rows), "pairs + sheet to", out)
