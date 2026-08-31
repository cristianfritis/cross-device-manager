"""Capture what a user would actually see, from the running binaries.

Nothing here reads a ViewModel, an offscreen widget, or a fixed-width
ftxui::Screen. The GUI is read through its AT-SPI tree; the TUI is read as bytes
out of a fixed-size tmux pane.

The one rule that matters (D10): an element's presence in the accessibility tree
says nothing about whether a user can see it. Qt leaves hidden QActions in the
tree with SHOWING=False, so every on-screen claim filters on that state. A
harness that asserted on tree membership would pass "this verb is hidden, not
merely disabled" no matter what the code did -- an assertion that cannot fail.
"""
from __future__ import annotations

import json
import os
import shlex
import subprocess
import sys
import time

TABS = ["Devices", "Modules", "Updates", "Snapshots"]
GUI_BIN = "/src/build/linux-debug/gui/devmgr-gui"
TUI_BIN = "/src/build/linux-debug/tui/devmgr-tui"


def launch(binary):
    """Prefix set by session.sh -- normally `umockdev-run -d <fixture> --`, so the
    app enumerates the fixture device set instead of the host's real one."""
    return shlex.split(os.environ.get("LAUNCH_PREFIX", "")) + [binary]


# --------------------------------------------------------------------------
# GUI
# --------------------------------------------------------------------------
def _pyatspi():
    import pyatspi  # imported late: only the GUI path needs a session bus
    return pyatspi


def _find_app(timeout=25):
    pyatspi = _pyatspi()
    deadline = time.time() + timeout
    while time.time() < deadline:
        desktop = pyatspi.Registry.getDesktop(0)
        for i in range(desktop.childCount):
            try:
                app = desktop.getChildAtIndex(i)
                if app and "devmgr-gui" in (app.name or ""):
                    return app
            except Exception:
                pass
        time.sleep(0.5)
    return None


def _node(acc, pyatspi):
    st = acc.getState()
    try:
        comp = acc.queryComponent()
        ext = comp.getExtents(pyatspi.DESKTOP_COORDS)
        extents = [ext.x, ext.y, ext.width, ext.height]
    except Exception:
        extents = None
    return {
        "name": acc.name or "",
        "role": acc.getRoleName(),
        "showing": st.contains(pyatspi.STATE_SHOWING),
        "visible": st.contains(pyatspi.STATE_VISIBLE),
        "enabled": st.contains(pyatspi.STATE_ENABLED),
        "focusable": st.contains(pyatspi.STATE_FOCUSABLE),
        "extents": extents,
        "children": [],
    }


def _walk(acc, pyatspi, depth=0):
    node = _node(acc, pyatspi)
    if depth < 12:
        for i in range(acc.childCount):
            try:
                node["children"].append(_walk(acc.getChildAtIndex(i), pyatspi, depth + 1))
            except Exception:
                pass
    return node


def _find_role(node, role):
    if node["role"] == role:
        return node
    for c in node["children"]:
        r = _find_role(c, role)
        if r:
            return r
    return None


def _find_acc(acc, role):
    if acc.getRoleName() == role:
        return acc
    for i in range(acc.childCount):
        try:
            r = _find_acc(acc.getChildAtIndex(i), role)
            if r:
                return r
        except Exception:
            pass
    return None


def _select_long_row(app, pyatspi, outdir):
    """Focus the fixture's long-named row and dump what the detail pane shows.

    The row is located by the fixture's own value -- data the harness supplies
    to the backend -- never by app-authored wording, so D5 still holds.
    """
    marker = "Fixture Extremely Long"
    lists = []

    def collect(acc):
        try:
            if acc.getRoleName() in {"list", "table", "tree"}:
                lists.append(acc)
            for i in range(acc.childCount):
                collect(acc.getChildAtIndex(i))
        except Exception:
            pass

    collect(app)
    for lst in lists:
        for i in range(lst.childCount):
            try:
                item = lst.getChildAtIndex(i)
                if marker not in (item.name or ""):
                    continue
                try:
                    item.queryComponent().grabFocus()
                except Exception:
                    pass
                try:
                    sel = lst.querySelection()
                    sel.selectChild(i)
                except Exception:
                    pass
                time.sleep(1.5)
                json.dump(_walk(app, pyatspi),
                          open(f"{outdir}/gui-Devices-longrow.json", "w"), indent=1)
                _shot(f"{outdir}/gui-Devices-longrow.png")
                return
            except Exception:
                pass


def _shot(path):
    subprocess.run(["import", "-window", "root", "-display", os.environ["DISPLAY"], path],
                   check=False, capture_output=True)


def capture_gui(width, height, outdir):
    pyatspi = _pyatspi()
    os.makedirs(outdir, exist_ok=True)
    proc = subprocess.Popen(launch(GUI_BIN), stdout=open(f"{outdir}/gui.log", "w"),
                            stderr=subprocess.STDOUT)
    try:
        app = _find_app()
        if app is None:
            # A dead bridge must report as a harness fault, never as an app with
            # no controls -- otherwise a misconfigured run looks like a wall of
            # genuine design failures.
            json.dump({"harness_fault": "devmgr-gui never registered on the a11y bus"},
                      open(f"{outdir}/FAULT.json", "w"), indent=2)
            return 2

        # Size the window to the target. A resize, never an activate.
        subprocess.run(["xdotool", "search", "--name", "Device Manager",
                        "windowsize", str(width), str(height)],
                       check=False, capture_output=True)
        time.sleep(1.5)

        tabs_acc = _find_acc(app, "page tab list")
        for tab in TABS:
            target = None
            for i in range(tabs_acc.childCount):
                c = tabs_acc.getChildAtIndex(i)
                if (c.name or "") == tab:
                    target = c
            if target is None:
                continue
            # Focus-free AT-SPI activation: no pointer, no /dev/uinput, no
            # foreground steal.
            try:
                target.queryAction().doAction(0)
            except Exception:
                pass
            time.sleep(1.2)
            tree = _walk(app, pyatspi)
            json.dump(tree, open(f"{outdir}/gui-{tab}.json", "w"), indent=1)
            _shot(f"{outdir}/gui-{tab}.png")

            # docs/DESIGN.md 10.1: list rows elide long values, and the full
            # value is always reachable in the detail pane. Only checkable with
            # a device set that is guaranteed to contain a long value, which is
            # what the umockdev fixture provides (task 3.5).
            if tab == "Devices":
                _select_long_row(app, pyatspi, outdir)
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


# --------------------------------------------------------------------------
# TUI -- a fixed-size pane and capture-pane. No driver (D3).
# --------------------------------------------------------------------------
def capture_tui(width, height, outdir, no_color=False):
    os.makedirs(outdir, exist_ok=True)
    sess = f"dv{width}x{height}{'nc' if no_color else ''}"
    env = "NO_COLOR=1 " if no_color else ""
    subprocess.run(["tmux", "kill-session", "-t", sess], check=False, capture_output=True)
    cmd = " ".join(shlex.quote(a) for a in launch(TUI_BIN))
    subprocess.run(["tmux", "new-session", "-d", "-s", sess, "-x", str(width),
                    "-y", str(height), f"{env}{cmd}"], check=True)
    time.sleep(4)
    suffix = "-nocolor" if no_color else ""
    try:
        for tab in TABS:
            pane = subprocess.run(["tmux", "capture-pane", "-p", "-t", sess],
                                  capture_output=True, text=True).stdout
            with open(f"{outdir}/tui-{tab}{suffix}.txt", "w") as fh:
                fh.write(pane)
            # 'm' cycles tabs -- the single tab-entry path the app itself uses.
            subprocess.run(["tmux", "send-keys", "-t", sess, "m"], check=False)
            time.sleep(1.5)
    finally:
        subprocess.run(["tmux", "kill-session", "-t", sess], check=False,
                       capture_output=True)
    return 0


GUI_SIZES = [(1024, 640), (800, 520)]
TUI_SIZES = [(120, 32), (100, 28), (80, 24)]


def sweep(outdir):
    """Every view x every size for ONE posture, inside ONE session.

    One session per posture is deliberate. The backends are named owners on a
    shared system bus, so starting and tearing them down once per size raced:
    a session could find the previous one's name still held, abort its own
    double, and then verify a posture that was not the one it asked for.
    """
    rc = 0
    for (w, h) in GUI_SIZES:
        rc |= capture_gui(w, h, f"{outdir}/gui/{w}x{h}")
    for (w, h) in TUI_SIZES:
        capture_tui(w, h, f"{outdir}/tui/{w}x{h}")
        capture_tui(w, h, f"{outdir}/tui/{w}x{h}", no_color=True)
    return rc


if __name__ == "__main__":
    if sys.argv[1] == "sweep":
        sys.exit(sweep(sys.argv[2]))
    mode, w, h, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    if mode == "gui":
        sys.exit(capture_gui(w, h, out))
    nc = len(sys.argv) > 5 and sys.argv[5] == "nocolor"
    sys.exit(capture_tui(w, h, out, no_color=nc))
