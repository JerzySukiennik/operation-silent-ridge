# Old vs new world side by side at the same autotest moments (stage 2 run on the left; the stage-1 flythrough supplies the old
# overview, since the stage-2 autotest had none): make_comparison_v2.py <old run> <old overview png> <new run> <out dir>
import sys, os
from PIL import Image, ImageDraw, ImageFont
old, old_over, new, out = sys.argv[1:5]
os.makedirs(out, exist_ok=True)
moments = [("0_ocean_low", "ocean_sun_low.png", "ocean, 150 m, into the sun"), ("1_ocean_spawn", "spawn_ocean.png", "ocean spawn 600 m"), ("2_canyon_entrance", "canyon_00.png", "canyon entrance"),
           ("3_canyon_a", "canyon_02.png", "inside canyon +10 s"), ("4_canyon_b", "canyon_05.png", "inside canyon +25 s"),
           ("5_overview", "overview.png", "overview 4000 m")]
try:
    font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 26)
except Exception:
    font = ImageFont.load_default()
W, H = 960, 540; rows = []
for name, f, cap in moments:
    a = old_over if name == "5_overview" else os.path.join(old, "spawn_ocean.png" if name == "0_ocean_low" else f)
    b = os.path.join(new, f)
    if not (os.path.exists(a) and os.path.exists(b)):
        print("missing", name, a, b); continue
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    im = Image.new("RGB", (W * 2 + 8, H + 48), (20, 20, 20))
    im.paste(ia.resize((W, H), Image.LANCZOS), (0, 48)); im.paste(ib.resize((W, H), Image.LANCZOS), (W + 8, 48))
    d = ImageDraw.Draw(im)
    d.text((12, 8), "BEFORE (stage 2) - " + ("ocean spawn 600 m" if name == "0_ocean_low" else cap), fill=(255, 255, 255), font=font)
    d.text((W + 20, 8), "AFTER (world v2) - " + cap, fill=(255, 255, 255), font=font)
    im.save(os.path.join(out, name + ".jpg"), quality=90); rows.append(im)
if rows:
    sheet = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y)); y += r.height
    sheet.save(os.path.join(out, "map_before_after.jpg"), quality=86); print("wrote", len(rows), "rows to", out)
