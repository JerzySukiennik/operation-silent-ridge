# Side-by-side of two UE runs at the same sequence moments: make_comparison_ue.py <left_run> <right_run> <out_dir> <left label> <right label>
import sys, os
from PIL import Image, ImageDraw, ImageFont
L, Rr, out, ll, rl = sys.argv[1:6]
os.makedirs(out, exist_ok=True)
moments = [("1_ocean_spawn", "shot_03s.png", "ocean spawn"), ("2_canyon_entrance", "shot_13s.png", "canyon entrance"),
           ("3_canyon_a", "shot_28s.png", "inside canyon +20 s"), ("4_canyon_b", "shot_43s.png", "inside canyon +35 s"),
           ("5_overview", "shot_61s.png", "overview 4000 m")]
try:
    font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 26)
except Exception:
    font = ImageFont.load_default()
W, H = 960, 540; rows = []
for name, f, cap in moments:
    a, b = os.path.join(L, f), os.path.join(Rr, f)
    if not (os.path.exists(a) and os.path.exists(b)):
        print("missing", name); continue
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    # older runner shots are DPI-unaware crops (top-left 1536x864 of the 1920x1080 frame): crop the other side to the same region
    cw, ch = min(ia.width, ib.width), min(ia.height, ib.height)
    ia, ib = ia.crop((0, 0, cw, ch)), ib.crop((0, 0, cw, ch))
    im = Image.new("RGB", (W * 2 + 8, H + 48), (20, 20, 20))
    im.paste(ia.resize((W, H), Image.LANCZOS), (0, 48))
    im.paste(ib.resize((W, H), Image.LANCZOS), (W + 8, 48))
    d = ImageDraw.Draw(im); d.text((12, 8), ll + " - " + cap, fill=(255, 255, 255), font=font); d.text((W + 20, 8), rl + " - " + cap, fill=(255, 255, 255), font=font)
    im.save(os.path.join(out, name + ".jpg"), quality=90); rows.append(im)
if rows:
    sheet = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y)); y += r.height
    sheet.save(os.path.join(out, "comparison_sheet.jpg"), quality=88); print("wrote", len(rows), "to", out)
