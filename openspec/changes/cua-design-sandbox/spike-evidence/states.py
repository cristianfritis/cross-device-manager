import pyatspi
desktop = pyatspi.Registry.getDesktop(0)
app = next((desktop.getChildAtIndex(i) for i in range(desktop.childCount)
            if "devmgr-gui" in (desktop.getChildAtIndex(i).name or "")), None)
if app is None:
    print("no app"); raise SystemExit(2)

def find(node, role):
    if node.getRoleName() == role: return node
    for i in range(node.childCount):
        r = find(node.getChildAtIndex(i), role)
        if r: return r
    return None

tb = find(app, "tool bar")
print(f"toolbar children={tb.childCount}\n")
print(f"{'name':<28} {'role':<12} SHOWING VISIBLE ENABLED")
for i in range(tb.childCount):
    c = tb.getChildAtIndex(i)
    st = c.getState()
    nm = (c.name or "")[:26]
    print(f"{nm:<28} {c.getRoleName():<12} "
          f"{str(st.contains(pyatspi.STATE_SHOWING)):<7} "
          f"{str(st.contains(pyatspi.STATE_VISIBLE)):<7} "
          f"{st.contains(pyatspi.STATE_ENABLED)}")

tabs = find(app, "page tab list")
if tabs:
    print(f"\npage tab list childCount={tabs.childCount}")
    for i in range(tabs.childCount):
        t = tabs.getChildAtIndex(i)
        print(f"  tab[{i}] name={t.name!r} showing={t.getState().contains(pyatspi.STATE_SHOWING)}")
