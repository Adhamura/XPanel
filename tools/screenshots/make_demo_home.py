# Demo HOME for screenshots: Steam-style library entries with generated (made-up) games and
# cover art, gallery pictures, and a background picture. Usage: make_demo_home.py <dir>
import os, sys, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter
home = sys.argv[1]; steam = f"{home}/.local/share/Steam"
os.makedirs(f"{steam}/steamapps", exist_ok=True)
games = [("Starfall Tactics", (255, 94, 58), (40, 10, 80)), ("Neon Drift", (0, 220, 255), (60, 0, 90)),
         ("Iron Hollow", (230, 180, 60), (30, 30, 30)), ("Pixel Harvest", (120, 220, 90), (20, 60, 40)),
         ("Abyss Runner", (90, 120, 255), (5, 10, 40)), ("Sky Forge", (255, 200, 120), (90, 30, 30)),
         ("Echo Arena", (255, 60, 140), (20, 20, 50)), ("Frostline", (180, 230, 255), (20, 40, 70))]
font = lambda s: ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", s)
def art(w, h, a, b, title, size, seed):
    random.seed(seed); im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h; d.line([(0, y), (w, y)], fill=tuple(int(a[i]*(1-t)*0.8 + b[i]*t) for i in range(3)))
    for _ in range(14):
        r = random.randint(w//12, w//3); x, y = random.randint(0, w), random.randint(0, h)
        d.ellipse([x-r, y-r, x+r, y+r], outline=tuple(min(255, c+60) for c in a), width=3)
    im = im.filter(ImageFilter.GaussianBlur(1.2)); d = ImageDraw.Draw(im)
    f = font(size); tw = d.textlength(title, font=f)
    d.text(((w-tw)/2, h*0.72), title, font=f, fill="white", stroke_width=3, stroke_fill=b)
    return im
for i, (name, a, b) in enumerate(games):
    gid = str(900100 + i)
    open(f"{steam}/steamapps/appmanifest_{gid}.acf", "w").write(
        f'"AppState"\n{{\n\t"appid"\t\t"{gid}"\n\t"name"\t\t"{name}"\n}}\n')
    os.makedirs(f"{steam}/appcache/librarycache/{gid}", exist_ok=True)
    art(600, 900, a, b, "", 46, i).save(f"{steam}/appcache/librarycache/{gid}/library_600x900.jpg", quality=90)
    art(1920, 620, a, b, "", 10, i+50).save(f"{steam}/appcache/librarycache/{gid}/library_hero.jpg", quality=88)
os.makedirs(f"{home}/Pictures/Screenshots", exist_ok=True); os.makedirs(f"{home}/Videos", exist_ok=True)
for i, (name, a, b) in enumerate(games[:6]):
    art(1920, 1080, a, b, name, 90, i+100).save(f"{home}/Pictures/Screenshots/{name.replace(' ', '_')}_{i+1:02d}.png")
art(1920, 1080, (0, 220, 255), (60, 0, 90), "", 10, 7).save(f"{home}/background.png")
