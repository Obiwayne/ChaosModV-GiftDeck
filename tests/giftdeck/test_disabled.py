"""
GiftDeckRunDisabledEffects test: loads the built ChaosMod.asi offline (dump_hello.exe) with a test effects.json that
disables some effects, once with the option on (default) and once off, and checks the hello descriptions and the
mod's real trigger checks (GiftDeckCheckTrigger).

    python test_disabled.py <dump_hello.exe> <ChaosMod.asi> <scratch dir>
"""

import json
import os
import shutil
import subprocess
import sys

exe, asi, work = sys.argv[1:4]
failed = []


def check(name, cond, detail=""):
    if not cond:
        failed.append(name)
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + detail) if detail else ""), flush=True)


EFFECTS = {
    "player_heal": {"enabled": False},                                  # instant, disabled
    "player_neverwanted": {"enabled": False},                           # timed, disabled
    "player_invincible": {"enabled": False, "permanent": True},         # disabled + permanent: never forced on
    "player_suicide": {"enabled": True, "shortcutKeycode": 0},          # enabled
}
IDS = ["player_heal", "player_neverwanted", "player_invincible", "player_suicide", "not_an_effect"]


def run(option):
    d = os.path.join(work, "disabled-%s" % ("default" if option is None else str(option).lower()))
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "chaosmod", "configs"))
    json.dump(EFFECTS, open(os.path.join(d, "chaosmod", "configs", "effects.json"), "w"))
    if option is not None:
        json.dump({"GiftDeckRunDisabledEffects": option},
                  open(os.path.join(d, "chaosmod", "configs", "config.json"), "w"))
    out = os.path.join(d, "hello.json")
    p = subprocess.run([exe, asi, d, out] + IDS, capture_output=True, text=True, encoding="utf-8")
    checks = {}
    for line in p.stdout.splitlines():
        if line.startswith("CHECK "):
            _, eid, ok, msg = line.split(" ", 3)
            checks[eid] = (ok == "1", msg)
    hello = json.load(open(out, encoding="utf-8"))
    desc = {c["id"]: c["description"] for c in hello["commands"]}
    return p.returncode, hello, desc, checks


for label, option in (("option on (default, no key)", None), ("option on (explicit true)", True)):
    rc, hello, desc, checks = run(option)
    print("--- " + label)
    check("dump ran", rc == 0 and len(hello["commands"]) == 369, "%d effects" % len(hello["commands"]))
    check("disabled effect listed", "player_heal" in desc)
    check("disabled effect described as disabled for random picks",
          "Disabled for random picks" in desc["player_heal"], desc["player_heal"])
    check("disabled instant effect triggers ok", checks["player_heal"][0], checks["player_heal"][1])
    check("disabled timed effect triggers ok", checks["player_neverwanted"][0], checks["player_neverwanted"][1])
    check("disabled + permanent stays refused", not checks["player_invincible"][0]
          and "disabled" in checks["player_invincible"][1], checks["player_invincible"][1])
    check("enabled effect triggers ok, no 'disabled' in description",
          checks["player_suicide"][0] and "Disabled" not in desc["player_suicide"], desc["player_suicide"])
    check("unknown id refused", not checks["not_an_effect"][0] and "Unknown" in checks["not_an_effect"][1])

rc, hello, desc, checks = run(False)
print("--- option off")
check("dump ran", rc == 0 and len(hello["commands"]) == 369)
check("disabled effect still listed", "player_heal" in desc)
check("described as disabled in config", desc["player_heal"].endswith("Disabled in the Chaos Mod config"),
      desc["player_heal"])
check("disabled effect refused with the old message",
      not checks["player_heal"][0] and checks["player_heal"][1] == 'Effect "player_heal" is disabled in the Chaos Mod config',
      checks["player_heal"][1])
check("enabled effect still triggers ok", checks["player_suicide"][0])

print("\n%s" % ("disabled-effects test: ALL PASSED" if not failed else "disabled-effects test: %d FAILED" % len(failed)))
sys.exit(1 if failed else 0)
