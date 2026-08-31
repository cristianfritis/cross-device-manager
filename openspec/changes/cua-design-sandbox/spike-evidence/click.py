# Task 1.6 equivalent: can we drive by element via AT-SPI alone (no cua-driver)?
# Switch Devices -> Modules and confirm the toolbar recomposes.
import pyatspi, time
desktop = pyatspi.Registry.getDesktop(0)
app = next((desktop.getChildAtIndex(i) for i in range(desktop.childCount)
            if "devmgr-gui" in (desktop.getChildAtIndex(i).name or "")), None)

def find(node, role):
    if node.getRoleName() == role: return node
    for i in range(node.childCount):
        r = find(node.getChildAtIndex(i), role)
        if r: return r
    return None

def showing_verbs():
    tb = find(app, "tool bar")
    return [tb.getChildAtIndex(i).name for i in range(tb.childCount)
            if tb.getChildAtIndex(i).name
            and tb.getChildAtIndex(i).getState().contains(pyatspi.STATE_SHOWING)]

print("BEFORE (Devices):", showing_verbs())

tabs = find(app, "page tab list")
modules = next(tabs.getChildAtIndex(i) for i in range(tabs.childCount)
               if tabs.getChildAtIndex(i).name == "Modules")
action = modules.queryAction()
names = [action.getName(i) for i in range(action.nActions)]
print(f"\nModules tab actions available: {names}")
action.doAction(0)          # focus-free AT-SPI activation, no pointer, no uinput
time.sleep(2)

after = showing_verbs()
print("AFTER  (Modules):", after)
ok = "Load Module…" in after and "Refresh" not in after
print("\nRESULT:", "PASS - element action landed, toolbar recomposed" if ok
      else "FAIL - toolbar did not recompose")
raise SystemExit(0 if ok else 4)
