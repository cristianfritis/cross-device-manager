"""Assertions over captured surfaces.

Two rules govern everything here.

D5 -- assert COMPLETENESS, never absence of clipping. A clipped row and a row
that fits exactly are indistinguishable from the screen alone; the first attempt
at fixing F5 built a clipping detector and it immediately failed the healthy
80-column Snapshots legend, which fits exactly. So the legend check does not ask
"was this cut?", it asks "is every shortcut the widest render advertises still
here?" -- which the design's own stated degradation order makes decidable:
the legend gives up typography, never a shortcut.

D10 -- on screen means SHOWING. Tree membership is never evidence of visibility.
"""
from __future__ import annotations

import json
import os
import re
import sys

TABS = ["Devices", "Modules", "Updates", "Snapshots"]
LEGEND_TOKEN = re.compile(r"(?:(?<=[\s(])|^)([A-Za-z?/]{1,12})=([A-Za-z/-]+)")


class Failures(list):
    def add(self, check, detail):
        self.append({"check": check, "detail": detail})


# ---------------------------------------------------------------- helpers
def showing(node):
    """Every on-screen claim goes through here (D10)."""
    out = []
    if node.get("showing"):
        out.append(node)
    for c in node["children"]:
        out.extend(showing(c))
    return out


def find_role(node, role):
    if node["role"] == role:
        return node
    for c in node["children"]:
        r = find_role(c, role)
        if r:
            return r
    return None


def all_text(node):
    return " ".join(n["name"] for n in showing(node) if n["name"])


def legend_keys(pane_text):
    """The set of shortcut keys the legend advertises, from the real pane."""
    for line in reversed(pane_text.splitlines()):
        if line.count("=") >= 2:
            return {k for k, _ in LEGEND_TOKEN.findall(line)}, line.strip()
    return set(), ""


def load(path):
    with open(path) as fh:
        return json.load(fh)


# ---------------------------------------------------------------- checks
def check_accessible_names(gui_dir, fails):
    """docs/DESIGN.md 10.1: every focusable list, tree and filter carries an
    accessible name; toolbar actions carry their visible text as their name."""
    for tab in TABS:
        p = f"{gui_dir}/gui-{tab}.json"
        if not os.path.exists(p):
            continue
        tree = load(p)
        for n in showing(tree):
            needs_name = (
                n["role"] in {"list", "tree", "table", "text", "push button"}
                and n["focusable"]
            ) or n["role"] == "push button"
            if needs_name and not n["name"].strip():
                fails.add("accessible-name",
                          f"{tab}: showing {n['role']} has no accessible name")


def check_hidden_is_not_showing(gui_dir, fails):
    """tab-contextual-toolbar: an off-tab verb is hidden, not merely disabled.

    Only decidable because we filter on showing state -- the actions stay in the
    tree either way (D10)."""
    owner = {
        "Refresh": "Devices", "Disable": "Devices", "Enable": "Devices",
        "Bind driver…": "Devices", "Unbind driver (advanced)": "Devices",
        "Load Module…": "Modules", "Unload": "Modules",
        "Refresh Updates": "Updates", "Install Update": "Updates",
        "Dismiss Request": "Updates",
        "Create Snapshot…": "Snapshots", "Restore Snapshot…": "Snapshots",
        "Diff Snapshot": "Snapshots", "History": "Snapshots",
        "Delete Snapshot": "Snapshots",
    }
    for tab in TABS:
        p = f"{gui_dir}/gui-{tab}.json"
        if not os.path.exists(p):
            continue
        tree = load(p)
        bar = find_role(tree, "tool bar")
        if bar is None:
            fails.add("toolbar", f"{tab}: no toolbar in the tree")
            continue
        vis = {c["name"] for c in bar["children"]
               if c.get("showing") and c["name"].strip()}
        for name in vis:
            if owner.get(name) not in (None, tab):
                fails.add("off-tab-verb",
                          f"{tab}: '{name}' belongs to {owner[name]} but is showing")


def check_no_clipped_controls(gui_dir, fails, width, height):
    """10.1: primary controls can never be squeezed off-screen."""
    for tab in TABS:
        p = f"{gui_dir}/gui-{tab}.json"
        if not os.path.exists(p):
            continue
        tree = load(p)
        frame = find_role(tree, "frame")
        if frame is None or not frame["extents"]:
            continue
        fx, fy, fw, fh = frame["extents"]
        bar = find_role(tree, "tool bar")
        if bar is None:
            continue
        for c in bar["children"]:
            if not c.get("showing") or not c["name"].strip() or not c["extents"]:
                continue
            x, y, w, h = c["extents"]
            if x < fx or y < fy or x + w > fx + fw or y + h > fy + fh:
                fails.add("clipped-control",
                          f"{tab}@{width}x{height}: '{c['name']}' extends outside the window")


def check_legend_completeness(tui_root, sizes, fails):
    """F3. The design degrades the legend by giving up typography, NEVER a
    shortcut -- so the set of advertised keys must be identical at every width.
    At 80x24 the degraded Snapshots legend used to lose q=quit entirely."""
    for tab in TABS:
        widest = None
        for (w, h) in sizes:
            p = f"{tui_root}/{w}x{h}/tui-{tab}.txt"
            if not os.path.exists(p):
                continue
            keys, line = legend_keys(open(p).read())
            if not keys:
                continue
            if widest is None:
                widest = (w, h, keys, line)
                continue
            missing = widest[2] - keys
            if missing:
                fails.add("legend-completeness",
                          f"{tab}: keys {sorted(missing)} present at "
                          f"{widest[0]}x{widest[1]} but absent at {w}x{h} -- "
                          f"got '{line}'")


def check_quit_always_discoverable(tui_root, sizes, fails):
    """10: the TUI always provides a discoverable way to quit."""
    for tab in TABS:
        for (w, h) in sizes:
            p = f"{tui_root}/{w}x{h}/tui-{tab}.txt"
            if not os.path.exists(p):
                continue
            keys, line = legend_keys(open(p).read())
            if keys and "q" not in keys:
                fails.add("quit-discoverable",
                          f"{tab}@{w}x{h}: no q= shortcut in legend '{line}'")


def _without_focused_row(text):
    """Drop the focus-marked row before comparing two separate renders.

    FTXUI scrolls the FOCUSED row horizontally to reveal its tail, and how far
    it has scrolled when the pane is captured differs between launches -- the
    same row reads `> ad` in one run and `> tad` in the next for `acpi_tad`.
    That artifact was recorded on 2026-07-27 as pre-existing and out of scope.

    It carries no colour information, so excluding exactly that row keeps this
    check comparing what it is about -- whether the TEXT changes when colour is
    removed -- instead of failing on a horizontal scroll offset. Every other row,
    including all status and banner text, is still compared byte for byte.
    """
    return [ln for ln in text.splitlines() if not ln.lstrip("│").startswith(">")]


def check_color_independence(tui_root, sizes, fails):
    """10: never communicate state by colour alone. capture-pane strips SGR, so
    the coloured and NO_COLOR renders must carry identical text."""
    for tab in TABS:
        for (w, h) in sizes:
            a = f"{tui_root}/{w}x{h}/tui-{tab}.txt"
            b = f"{tui_root}/{w}x{h}/tui-{tab}-nocolor.txt"
            if not (os.path.exists(a) and os.path.exists(b)):
                continue
            if _without_focused_row(open(a).read()) != _without_focused_row(open(b).read()):
                fails.add("color-independence",
                          f"{tab}@{w}x{h}: text differs between coloured and NO_COLOR renders")


def check_cross_surface_parity(gui_dir, tui_dir, fails):
    """9 / F1. The availability sentence is taken from whichever surface renders
    it -- never authored here -- and must appear on the other one too."""
    for tab in TABS:
        tp = f"{tui_dir}/tui-{tab}.txt"
        gp = f"{gui_dir}/gui-{tab}.json"
        if not (os.path.exists(tp) and os.path.exists(gp)):
            continue
        tui_text = open(tp).read()
        sentence = None
        for line in tui_text.splitlines():
            if "unavailable" in line:
                sentence = line.strip().lstrip("?").strip()
                break
        if not sentence:
            continue
        core = sentence.split("—")[0].strip()
        if len(core) < 8:
            continue
        gui_text = all_text(load(gp))
        if core not in " ".join(gui_text.split()):
            fails.add("cross-surface-parity",
                      f"{tab}: TUI shows '{core}' but it is nowhere on the GUI surface")


def _parent_of(root, target):
    stack = [root]
    while stack:
        n = stack.pop()
        for c in n["children"]:
            if c is target:
                return n
            stack.append(c)
    return None


def check_severity_role(gui_dir, fails):
    """F2. A shared sentence keeps the severity role its accessor carries: the
    glyph, and a route to the diagnostic.

    Both halves are checked STRUCTURALLY. An earlier version of this check
    asserted a control literally named "Details" and produced four false
    failures -- the visual label is "Details", but the accessible name is
    "Backend diagnostics". Authoring an expected string here is precisely what
    D5 forbids, and the harness caught its own violation.
    """
    for tab in TABS:
        p = f"{gui_dir}/gui-{tab}.json"
        if not os.path.exists(p):
            continue
        tree = load(p)
        banner = None
        for n in showing(tree):
            if n["role"] == "label" and "unavailable" in n["name"]:
                banner = n
                break
        if banner is None:
            continue

        # The sentence must not arrive as a bare string: it carries a leading
        # severity glyph from its accessor.
        if banner["name"][:1].isalnum():
            fails.add("severity-role",
                      f"{tab}: availability sentence starts with no severity glyph "
                      f"-- {banner['name'][:40]!r}")

        # There must be a route to the diagnostic beside it. Identified by role
        # and focusability, never by an authored label.
        parent = _parent_of(tree, banner)
        siblings = parent["children"] if parent else []
        route = [s for s in siblings
                 if s is not banner and s.get("showing") and s["name"].strip()
                 and s["role"] in {"check box", "push button", "toggle button", "link"}]
        if not route:
            fails.add("severity-role",
                      f"{tab}: availability sentence shows with no adjacent control "
                      f"routing to its diagnostic")


def check_long_value_reachable_in_detail(gui_dir, fails):
    """docs/DESIGN.md 10.1: list rows elide long values (ElideRight, no wrap);
    the full value is always reachable in the detail pane.

    The expected value is the fixture's own device name -- input the harness
    fed the backend, not app-authored wording -- so this stays within D5.
    Before the umockdev fixture (task 3.5) this was unverifiable: whether any
    device name was long enough to elide depended on the host's hardware.
    """
    p = f"{gui_dir}/gui-Devices-longrow.json"
    if not os.path.exists(p):
        fails.add("long-value-detail",
                  "the fixture's long-named row was never selected -- "
                  "elision could not be checked")
        return
    tree = load(p)
    text = " ".join(all_text(tree).split())
    full = ("Fixture Extremely Long Device Name For Elision Testing "
            "Across Both Surfaces 0123456789")
    if full not in text:
        fails.add("long-value-detail",
                  "the long device name is elided in the list but is not present "
                  "in full anywhere on the surface (10.1)")


def check_devices_reach_both_surfaces(gui_dir, tui_dir, fails):
    """9: visible nouns match between surfaces. With a fixed device set the
    comparison is meaningful -- on a host device list it would differ per
    machine and could never be asserted."""
    tp = f"{tui_dir}/tui-Devices.txt"
    gp = f"{gui_dir}/gui-Devices.json"
    if not (os.path.exists(tp) and os.path.exists(gp)):
        return
    tui_text = " ".join(open(tp).read().split())
    gui_text = " ".join(all_text(load(gp)).split())
    # A short, stable prefix of each fixture row: long names elide on both
    # surfaces, so the whole string is not expected in a list row.
    for probe in ("Fixture Root Hub", "Fixture Deeply Nested",
                  "Fixture Deauthorized", "Fixture Display Controller"):
        on_gui = probe in gui_text
        on_tui = probe in tui_text
        if on_gui != on_tui:
            where = "GUI" if on_gui else "TUI"
            fails.add("device-parity",
                      f"'{probe}' is on the {where} Devices surface but not the other")


def check_posture_actually_differs(root, fails):
    """The two postures must produce different surfaces.

    Without this, a fwupd double that failed to claim its name would leave the
    healthy posture looking exactly like the degraded one, and every check would
    still pass -- the sweep would report on a posture it never reached. The
    probes are fixture values the harness fed the backend, not app wording.
    """
    deg = f"{root}/degraded/tui/120x32/tui-Updates.txt"
    hea = f"{root}/healthy/tui/120x32/tui-Updates.txt"
    if not (os.path.exists(deg) and os.path.exists(hea)):
        return
    degraded_text = open(deg).read()
    healthy_text = open(hea).read()

    if "Fixture System Firmware" not in healthy_text:
        fails.add("posture-not-exercised",
                  "healthy posture does not show the seeded fwupd update -- "
                  "the fwupd double did not serve this run")
    if "Fixture System Firmware" in degraded_text:
        fails.add("posture-leak",
                  "degraded posture shows the seeded fwupd update -- a previous "
                  "posture's daemon leaked into this one")
    if degraded_text == healthy_text:
        fails.add("posture-not-exercised",
                  "degraded and healthy Updates surfaces are identical")


# ---------------------------------------------------------------- main
def main(root):
    fails = Failures()
    gui_sizes = [(1024, 640), (800, 520)]
    tui_sizes = [(120, 32), (100, 28), (80, 24)]

    for posture in sorted(os.listdir(root)):
        pdir = os.path.join(root, posture)
        if not os.path.isdir(pdir):
            continue
        fault = None
        for w, h in gui_sizes:
            gdir = f"{pdir}/gui/{w}x{h}"
            if os.path.exists(f"{gdir}/FAULT.json"):
                fault = load(f"{gdir}/FAULT.json")["harness_fault"]
                fails.add("HARNESS-FAULT", f"{posture}@{w}x{h}: {fault}")
                continue
            if not os.path.isdir(gdir):
                continue
            check_accessible_names(gdir, fails)
            check_hidden_is_not_showing(gdir, fails)
            check_no_clipped_controls(gdir, fails, w, h)
            check_severity_role(gdir, fails)
            check_long_value_reachable_in_detail(gdir, fails)
        troot = f"{pdir}/tui"
        if os.path.isdir(troot):
            check_legend_completeness(troot, tui_sizes, fails)
            check_quit_always_discoverable(troot, tui_sizes, fails)
            check_color_independence(troot, tui_sizes, fails)
            if os.path.isdir(f"{pdir}/gui/1024x640"):
                check_cross_surface_parity(f"{pdir}/gui/1024x640",
                                           f"{troot}/120x32", fails)
                check_devices_reach_both_surfaces(f"{pdir}/gui/1024x640",
                                                  f"{troot}/120x32", fails)

    check_posture_actually_differs(root, fails)

    json.dump(fails, open(f"{root}/failures.json", "w"), indent=2)
    if fails:
        print(f"\nDESIGN VERIFICATION: {len(fails)} failure(s)\n")
        for f in fails:
            print(f"  [{f['check']}] {f['detail']}")
        return 1
    print("\nDESIGN VERIFICATION: all checks passed\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
