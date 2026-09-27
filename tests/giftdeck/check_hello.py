"""
Validates the hello dumped by dump_hello.exe and cross-checks it against ConfigApp/Effects.cs (upstream's own
effect catalog with categories). Also writes commands.json next to the hello (static catalog for GiftDeck's pack).

    python check_hello.py <hello.json> <ConfigApp/Effects.cs>
"""

import collections
import json
import os
import re
import sys

hello_path, effects_cs = sys.argv[1], sys.argv[2]
hello = json.load(open(hello_path, encoding="utf-8"))

errors = []
for key, val in (("type", "hello"), ("game", "gta5"), ("mod", "chaosmod"), ("name", "Chaos Mod V")):
    if hello.get(key) != val:
        errors.append("hello.%s = %r" % (key, hello.get(key)))
if not isinstance(hello.get("version"), str) or not hello["version"]:
    errors.append("hello.version missing")

cmds = hello["commands"]
ids = [c["id"] for c in cmds]
if len(ids) != len(set(ids)):
    errors.append("duplicate ids")
for c in cmds:
    if set(c) != {"id", "name", "category", "description", "args"} or c["args"] != [] or not c["id"] or not c["name"]:
        errors.append("bad command %r" % c)

# upstream catalog: { "id", new EffectInfo("Name", EffectCategory.X, ...) }
cs = open(effects_cs, encoding="utf-8-sig").read()
upstream = {m[0]: (m[1], m[2]) for m in re.findall(r'\{\s*"([^"]+)",\s*new EffectInfo\("((?:[^"\\]|\\.)*)",\s*EffectCategory\.(\w+)', cs)}

mismatch_cat, mismatch_name = [], []
for c in cmds:
    if c["id"] in upstream:
        name, cat = upstream[c["id"]]
        if cat != c["category"]:
            mismatch_cat.append((c["id"], c["category"], cat))
        if name.replace('\\"', '"') != c["name"]:
            mismatch_name.append((c["id"], c["name"], name))
only_mod = sorted(set(ids) - set(upstream))
only_cfg = sorted(set(upstream) - set(ids))

by_cat = collections.Counter(c["category"] for c in cmds)
disabled = [c["id"] for c in cmds if "Disabled in the Chaos Mod config" in c["description"]]
forced = [c["id"] for c in cmds if "Disabled for random picks" in c["description"]]
timed = sum(1 for c in cmds if c["description"].startswith("Timed"))

print("hello version: %s" % hello["version"])
print("effects in hello: %d  (instant %d, timed %d, disabled in effects.json but GiftDeck can start: %d, "
      "can't be started: %d)" % (len(cmds), len(cmds) - timed, timed, len(forced), len(disabled)))
print("by category: " + ", ".join("%s %d" % kv for kv in by_cat.most_common()))
print("ConfigApp catalog: %d effects; matched %d; category mismatches %d; name mismatches %d"
      % (len(upstream), len(set(ids) & set(upstream)), len(mismatch_cat), len(mismatch_name)))
for m in mismatch_cat:
    print("  category differs: %s mod=%s configapp=%s" % m)
for m in mismatch_name[:20]:
    print("  name differs: %s mod=%r configapp=%r" % m)
if only_mod:
    print("  only in mod: " + ", ".join(only_mod))
if only_cfg:
    print("  only in ConfigApp: " + ", ".join(only_cfg))
if disabled:
    print("  can't be started (disabled): " + ", ".join(disabled))

out = os.path.join(os.path.dirname(hello_path), "commands.json")
json.dump(cmds, open(out, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
print("wrote " + out)

if len(cmds) < 300:
    errors.append("suspiciously few effects: %d" % len(cmds))
if errors:
    print("ERRORS:\n  " + "\n  ".join(errors[:30]))
    sys.exit(1)
print("hello OK")
