# Task 1.4 gate probe: walk the AT-SPI tree of devmgr-gui and report whether it
# exposes NAMED, ADDRESSABLE child elements -- not just a top-level window.
import sys, pyatspi

TARGET = "devmgr-gui"
named, total, roles = 0, 0, {}
lines = []

def walk(node, depth=0):
    global named, total
    try:
        name = node.name or ""
        role = node.getRoleName()
    except Exception as e:
        return
    total += 1
    roles[role] = roles.get(role, 0) + 1
    if name.strip():
        named += 1
    if depth <= 3:
        lines.append("  " * depth + f"[{role}] name={name!r}")
    for i in range(node.childCount):
        try:
            walk(node.getChildAtIndex(i), depth + 1)
        except Exception:
            pass

desktop = pyatspi.Registry.getDesktop(0)
print(f"desktop children (registered apps): {desktop.childCount}")
app = None
for i in range(desktop.childCount):
    a = desktop.getChildAtIndex(i)
    print(f"  app[{i}] name={a.name!r} role={a.getRoleName()}")
    if a.name and TARGET in a.name:
        app = a

if app is None:
    print("GATE: FAIL - devmgr-gui not registered on the a11y bus")
    sys.exit(2)

walk(app)
print("\n--- tree (depth<=3) ---")
print("\n".join(lines[:60]))
print(f"\ntotal nodes={total} named={named}")
print("roles:", dict(sorted(roles.items(), key=lambda kv: -kv[1])[:12]))
if total > 5 and named > 3:
    print("GATE: PASS - named, addressable child elements exposed")
    sys.exit(0)
print("GATE: FAIL - tree is top-level only / unnamed")
sys.exit(3)
