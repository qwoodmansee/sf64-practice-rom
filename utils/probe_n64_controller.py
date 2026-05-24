#!/usr/bin/env python3
"""Interactive N64 USB controller probe for mupen64plus.

Press each N64 button when prompted, then rotate the analog stick.
Prints an InputAutoCfg.ini block you can paste into
~/.config/mupen64plus/InputAutoCfg.ini.

Requires pygame: python3 -m pip install --user pygame
"""
import pygame, sys, time

pygame.init()
pygame.joystick.init()
if pygame.joystick.get_count() == 0:
    print("No joystick found. Plug in controller and try again.")
    sys.exit(1)
js = pygame.joystick.Joystick(0)
js.init()
print(f"Probing: {js.get_name()!r}")

pygame.event.pump()
time.sleep(0.2)
pygame.event.pump()
RESTING = [js.get_axis(a) for a in range(js.get_numaxes())]
print(f"Resting axis values: {[f'{v:+.2f}' for v in RESTING]}")
print()

PROMPTS = [
    ("A Button",     "Press A (the big blue button)"),
    ("B Button",     "Press B (the small green button)"),
    ("Start",        "Press Start"),
    ("Z Trig",       "Press Z (the trigger under the stick)"),
    ("L Trig",       "Press L (top-left shoulder)"),
    ("R Trig",       "Press R (top-right shoulder)"),
    ("C Button U",   "Press C-Up"),
    ("C Button D",   "Press C-Down"),
    ("C Button L",   "Press C-Left"),
    ("C Button R",   "Press C-Right"),
    ("DPad U",       "Press D-Pad Up"),
    ("DPad D",       "Press D-Pad Down"),
    ("DPad L",       "Press D-Pad Left"),
    ("DPad R",       "Press D-Pad Right"),
]

def neutral(js):
    """Axes near their resting baseline (not necessarily 0 - triggers rest at -1), no buttons held."""
    for a in range(js.get_numaxes()):
        if abs(js.get_axis(a) - RESTING[a]) > 0.3:
            return False
    for b in range(js.get_numbuttons()):
        if js.get_button(b):
            return False
    for h in range(js.get_numhats()):
        if js.get_hat(h) != (0, 0):
            return False
    return True

def what_is_not_neutral(js):
    """Return a list of (kind, index, value) for everything currently above threshold."""
    issues = []
    for a in range(js.get_numaxes()):
        v = js.get_axis(a)
        if abs(v - RESTING[a]) > 0.3:
            issues.append(("axis", a, f"{v:+.2f} (rest {RESTING[a]:+.2f})"))
    for b in range(js.get_numbuttons()):
        if js.get_button(b):
            issues.append(("button", b, "held"))
    for h in range(js.get_numhats()):
        v = js.get_hat(h)
        if v != (0, 0):
            issues.append(("hat", h, str(v)))
    return issues

def wait_until_neutral(js):
    """Drain events and block until controller is at rest, then quiet for 150ms."""
    stable_since = None
    last_complain = 0
    while True:
        pygame.event.pump()
        issues = what_is_not_neutral(js)
        if not issues:
            if stable_since is None:
                stable_since = time.time()
            elif time.time() - stable_since > 0.15:
                pygame.event.clear()
                return
        else:
            stable_since = None
            now = time.time()
            if now - last_complain > 1.0:
                desc = ", ".join(f"{k}{i}={v}" for k, i, v in issues)
                print(f"    (waiting for neutral; currently: {desc})", flush=True)
                last_complain = now
        time.sleep(0.01)

def rebaseline_all_axes(js):
    """Let everything settle, then capture current axis values as the new baseline.
    Handles digital-as-bipolar-axis inputs that only emit their resting value
    after their first event."""
    time.sleep(0.4)
    pygame.event.pump()
    for a in range(js.get_numaxes()):
        RESTING[a] = js.get_axis(a)

def wait_for_input(js, label):
    wait_until_neutral(js)
    print(f">>> {label}", flush=True)
    while True:
        for ev in pygame.event.get():
            if ev.type == pygame.JOYBUTTONDOWN:
                print(f"    -> button({ev.button})")
                rebaseline_all_axes(js)
                return f"button({ev.button})"
            if ev.type == pygame.JOYAXISMOTION:
                delta = ev.value - RESTING[ev.axis]
                if abs(delta) > 0.5:
                    sign = "+" if delta > 0 else "-"
                    print(f"    -> axis({ev.axis}{sign})")
                    rebaseline_all_axes(js)
                    return f"axis({ev.axis}{sign})"
            if ev.type == pygame.JOYHATMOTION and ev.value != (0, 0):
                # Mupen64Plus expects directional words, e.g. `hat(0 Up)`,
                # not a Python tuple like `hat(0 (0, 1))`.
                hat_dir = {
                    (0, 1):  "Up",
                    (0, -1): "Down",
                    (-1, 0): "Left",
                    (1, 0):  "Right",
                }.get(ev.value)
                if hat_dir is None:
                    # Diagonal — record the dominant axis so the binding still
                    # loads. Mupen64Plus does not accept diagonals directly.
                    dx, dy = ev.value
                    if abs(dx) >= abs(dy):
                        hat_dir = "Right" if dx > 0 else "Left"
                    else:
                        hat_dir = "Up" if dy > 0 else "Down"
                print(f"    -> hat({ev.hat} {hat_dir})")
                rebaseline_all_axes(js)
                return f"hat({ev.hat} {hat_dir})"
        time.sleep(0.01)

mapping = {}
for n64_label, prompt in PROMPTS:
    mapping[n64_label] = wait_for_input(js, prompt)

print()
print(">>> Now rotate the analog stick around once (release everything first)")
axes_seen = {}
deadline = time.time() + 5.0
while time.time() < deadline:
    for ev in pygame.event.get():
        if ev.type == pygame.JOYAXISMOTION and abs(ev.value) > 0.7:
            axes_seen.setdefault(ev.axis, []).append(ev.value)
    time.sleep(0.01)

ranked = sorted(axes_seen.items(), key=lambda kv: len(kv[1]), reverse=True)
if len(ranked) >= 2:
    x_axis, y_axis = ranked[0][0], ranked[1][0]
    print(f"    -> X Axis = axis({x_axis}-,{x_axis}+)")
    print(f"    -> Y Axis = axis({y_axis}-,{y_axis}+)")
    mapping["X Axis"] = f"axis({x_axis}-,{x_axis}+)"
    mapping["Y Axis"] = f"axis({y_axis}-,{y_axis}+)"
else:
    print("    !! couldn't detect stick axes — set X/Y Axis manually")

print()
print("===== InputAutoCfg.ini block =====")
print(f"[{js.get_name()}]")
print("plugged = True")
print("plugin = 2")
print("mouse = False")
print('AnalogDeadzone = "4096,4096"')
print('AnalogPeak = "32768,32768"')
for k, v in mapping.items():
    print(f"{k} = {v}")
print("Mempak switch =")
print("Rumblepak switch =")
