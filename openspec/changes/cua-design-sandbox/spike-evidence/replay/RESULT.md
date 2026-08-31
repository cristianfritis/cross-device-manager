# Task 7 replay — the harness is proven able to fail, but not on the targets D7 named

**Two results, and the first one corrects the plan.**

1. **D7's premise was wrong. F1, F2 and F3 were never a committed state**, so no
   commit can exhibit them and the replay as specified is unachievable.
2. **The harness is nonetheless proven able to fail.** Run against `dcb25d4` it
   produced **51 precise failures** on a real, committed, historically-recorded
   design defect that the automated tier of the day did not catch. Run against
   `HEAD` it passes.

## Why the premise was wrong

When the change was planned, `dcb25d4` (= `38eaa00^`) was verified to lack the
*fixes*: no `tui/src/views/legend.cpp`, no Devices banner in
`gui/src/main_window.hpp`, and `main_window.cpp:566` using `modulesVm_.banner()`.
That was read as the defects being live there.

**Absence of a fix is not presence of a defect** when the same commit introduced
the feature the defect belonged to. `git grep` settles it:

```
$ git grep -c "i=diagnostics" 38eaa00 -- tui/
38eaa00:tui/src/views/snapshots_view.cpp:1      <- the cause of F3
38eaa00:tui/src/views/legend.hpp:2              <- the fix for F3
```

`i=diagnostics` — the key whose addition pushed the Snapshots legend from 81 to
96 columns — **first appears in 38eaa00, the same commit as the legend fitting
that fixed it.** The same holds for the availability note: 38eaa00 both adds it
to the TUI Devices tab and gives the GUI its Devices banner.

The captures confirm it directly. At `dcb25d4`:

```
 Snapshots (/=filter  s=create…  r=restore  d=diff  h=history  x=delete  q=quit)
```

79 columns, `q=quit` present at 80x24, no `i=diagnostics`. And the Devices tab
shows no availability sentence on *either* surface, so F1's asymmetry does not
exist there either.

F1–F3 lived only in the working tree on 2026-07-27, between the feature landing
and the manual matrix catching it. They were fixed before commit. That is why
they were found by a human driving the app and by nothing else — and it is the
strongest possible argument for this harness, since a pre-commit working tree is
exactly what a developer would run it against.

## What the replay does prove

Against `dcb25d4` the harness emitted 51 failures, all of one kind:

```
[off-tab-verb] Devices: 'Install Update' belongs to Updates but is showing
[off-tab-verb] Devices: 'Load Module…' belongs to Modules but is showing
[off-tab-verb] Modules: 'Disable' belongs to Devices but is showing
[off-tab-verb] Snapshots: 'Refresh' belongs to Devices but is showing
… 47 more, across all four tabs
```

This is a genuine committed defect, and it is the one the 2026-07-27 manual note
recorded under *Observed, out of scope*:

> **The toolbar shows every tab's verbs at once** — on Devices it offers Create
> Snapshot, Install Update, Diff Snapshot. Live confirmation of the parked
> `tab-contextual-toolbar` change; not touched here.

It was live from before `dcb25d4` until `tab-contextual-toolbar` fixed it on
2026-08-08. No offscreen test caught it in that window. The harness catches it in
51 findings that each name the tab and the verb.

Critically, this check is only capable of failing because it filters on
`STATE_SHOWING` (D10) — the off-tab actions are present in the accessibility
tree in both the broken and the fixed build. A membership-based check would have
passed `dcb25d4` and `HEAD` alike.

Against `HEAD` the same harness reports `all checks passed`
(`failures-HEAD.json` is empty), so the failures are attributable to the tree
under test rather than to the harness.

## One check found its own bug

The first run against `HEAD` produced four `severity-role` failures claiming the
availability sentence had no `Details` disclosure. The disclosure exists; its
*visual* label is `Details ▾` but its **accessible name is `Backend
diagnostics`**. The check had an authored expected string in it — exactly what
D5 forbids — and it was rewritten to identify the control structurally, by role
and adjacency. Recorded because it is the same failure mode the harness exists
to catch, committed by the harness itself.

## Consequences for the change

- D7 is amended: the replay target is `dcb25d4`, and what it proves is the
  toolbar defect, not F1–F3.
- The spec scenario is reworded to require *a* known committed defect, not the
  three specific ones.
- F1–F3 remain the harness's design rationale; they are simply not replayable.
  The checks aimed at them (cross-surface parity, legend key preservation,
  severity role) are present and exercised on `HEAD`, but have not been observed
  to fail on a historical tree. That is stated rather than glossed.

## Reproducing

```bash
git worktree add --detach /tmp/replay dcb25d4
cp -r test/design /tmp/replay/test/design
docker build -t design-replay:latest -f /tmp/replay/Dockerfile.replay /tmp/replay
docker run --rm -v /tmp/out:/out design-replay:latest
```
