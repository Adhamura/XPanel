# Storyboard of the README demo (tools/screenshots/demo.sh): scripted gamepad input sent through
# the fake daemon, which passes it on to XPanel like xpanel-daemon does.
#   demo_input.py <daemon socket> <XPanel binary>
import json, math, socket, subprocess, sys, time

sock_path, xpanel = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX)
s.connect(sock_path)


def send(**m):
    s.sendall(json.dumps(m).encode() + b"\n")


def nav(name, wait=0.45):
    send(event="nav", name=name)
    time.sleep(wait)


def press(name):
    send(event="press", name=name)


def release(name):
    send(event="release", name=name)


def axes(**v):
    full = {"lx": 0, "ly": 0, "rx": 0, "ry": 0, "lt": 0, "rt": 0}
    full.update(v)
    send(event="axes", **full)


def run(*args):
    subprocess.Popen([xpanel, *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# 1. Quick Panel over the game, moved with the D-pad
run("--open"); time.sleep(2.5)
for d in ["right", "right", "down", "left", "left", "down"]:
    nav(d)
time.sleep(1.0)
run("--close"); time.sleep(0.3)

# 2. Control Mode key map: test mode follows the sticks and buttons
run("--app", "control"); time.sleep(2.0)
press("A"); time.sleep(0.35)
t0 = time.time()
while time.time() - t0 < 2.6:
    t = time.time() - t0
    axes(lx=0.75 * math.cos(t * 3.2), ly=0.75 * math.sin(t * 3.2), rx=0.6 * math.sin(t * 2.4), ry=0.6 * math.cos(t * 2.4))
    if 0.9 < t < 0.95: press("X")
    if 1.7 < t < 1.75: release("A")
    time.sleep(1 / 30)
release("X"); axes()
time.sleep(0.4)
# triggers: the drawing flips to the top edge
press("RT"); t0 = time.time()
while time.time() - t0 < 2.2:
    t = time.time() - t0
    axes(rt=min(1, t / 1.2), lt=max(0, min(1, (t - 0.5) / 1.2)))
    time.sleep(1 / 30)
release("RT"); axes(); time.sleep(0.4)
# rear paddle: flips to the back
press("Right paddle"); time.sleep(2.2); release("Right paddle"); time.sleep(0.4)
press("Y"); time.sleep(1.4); release("Y")
time.sleep(0.5)

# 3. Performance settings and the Game Library
run("--app", "settings/performance"); time.sleep(2.6)
run("--app", "library"); time.sleep(2.8)
