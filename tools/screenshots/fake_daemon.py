# Stand-in for xpanel-daemon for screenshots: serves a fixed Ally X state on the given socket.
import json, os, socket, sys, threading
path = sys.argv[1]
state = {"opmode": "turbo", "tdp": 25, "tdpSlow": 0, "tdpFast": 0, "fan": "auto", "charge": 80,
         "eco": False, "gpuClock": 0, "lighting": "static", "lightColor": "#ff2d55",
         "lightBrightness": 3, "lightSpeed": "medium", "bootSound": True, "mcuPowersave": False,
         "ac": False, "spl": 25, "sppt": 30, "fppt": 35, "profile": "performance",
         "tdpMin": 5, "tdpMax": 35, "gpuMin": 800, "gpuMax": 2900,
         "inputLayer": "active", "inputPad": "event5", "inputGrab": False,
         "serial": "DEMO000000", "model": "ROG Ally X RC72LA", "bios": "RC72LA.312"}
# XPANEL_FAKE_STATE='{"fan": "custom", ...}' overrides values (to screenshot other states)
state.update(json.loads(os.environ.get("XPANEL_FAKE_STATE", "{}")))
if os.path.exists(path): os.unlink(path)
srv = socket.socket(socket.AF_UNIX); srv.bind(path); srv.listen(8)
clients = []  # a request {"event": "nav", "name": "down"} is passed on to the other clients (scripted gamepad input)
def line(): return (json.dumps({"state": state}) + "\n").encode()
def serve(c):
    f = c.makefile("rb")
    for raw in f:
        try: req = json.loads(raw)
        except Exception: continue
        if "event" in req:
            for o in list(clients):
                if o is not c:
                    try: o.sendall(json.dumps({"event": req["event"], "name": req.get("name", "")}).encode() + b"\n")
                    except Exception: pass
            continue
        state.update({k: v for k, v in req.get("set", {}).items() if k != "control"})
        try: c.sendall(line())
        except Exception: return
while True:
    c, _ = srv.accept(); clients.append(c); c.sendall(line())
    threading.Thread(target=serve, args=(c,), daemon=True).start()
