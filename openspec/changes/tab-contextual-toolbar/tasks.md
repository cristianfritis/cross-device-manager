## 1. Toolbar structure

- [x] 1.1 Keep a `QToolBar* toolbar_` member, set `Qt::ToolButtonTextBesideIcon`, and add a small ctor-local helper that creates an action, tags it with its owning tab via `setData(tabIndex)`, and appends it to the toolbar (design D1).
- [x] 1.2 Reorder action construction so the toolbar's single linear order satisfies every tab's §5.3 order: Devices (`Refresh`, enable/disable, `Bind driver…`, `Unbind driver (advanced)`), Modules (`Load Module…`, `Unload`), Updates (`Refresh Updates`, `Install Update`, `Dismiss Request`), Snapshots (`Create Snapshot…`, `Restore Snapshot…`, `Diff Snapshot`, `History`, `Delete Snapshot`). Move each connect-lambda with its action, changing nothing inside them (design D2).
- [x] 1.3 Insert group separators between the §5.3 groups within each tab's run, and between tabs' runs.
- [x] 1.4 Attach theme icons with `QStyle` fallbacks to the five commands with a standard freedesktop name (`view-refresh` ×2, `system-software-update`, `edit-delete`, `window-close`); leave every other action text-only (design D6).

## 2. One presentation function

- [x] 2.1 Rename `updateActionEnablement()` to `updateActionPresentation()` and make it the single owner of visibility, enablement, dynamic text and tooltip; update every call site (tab change, selection change, model reset, availability change, operation start/completion).
- [x] 2.2 Add a visibility pass that walks `toolbar_->actions()` and sets each non-separator action visible iff `data().toInt()` equals the active tab index; keep it free of sysfs/libkmod/D-Bus/filesystem work.
- [x] 2.3 Hide `Dismiss Request` unless the Updates tab is active *and* `updatesVm_.requestBanner()` is non-empty (design D4).
- [x] 2.4 Add the separator pass: hide every separator, then reveal only those standing between two visible actions, so no leading, trailing, or doubled separator survives a hidden group (design D3).
- [x] 2.5 Gate `Refresh` to the Devices tab in the enablement half (it is currently always enabled), leaving `Refresh Updates` as the Updates tab's own refresh; change no other enablement predicate, wording or shortcut.
- [x] 2.6 Keep function complexity under the clang-tidy threshold by leaving `updateDeviceVerbEnablement()`/`gateOnDaemon()` as they are and splitting the new passes into their own small private methods.

## 3. GUI tests

- [x] 3.1 Add a per-tab visibility matrix test asserting the exact visible action set for each of the four tabs, and that every action owned by another tab is invisible.
- [x] 3.2 Add an order test asserting the visible actions of each tab appear in the specified order.
- [x] 3.3 Add a separator-hygiene test: for each tab, the visible toolbar entries neither start nor end with a separator and contain no two adjacent separators.
- [x] 3.4 Add an off-tab shortcut test: shortcuts remain bound after switching tabs, and an off-tab verb is both invisible and disabled so its shortcut cannot fire.
- [x] 3.5 Add a daemon-down test: on each tab, that tab's daemon-backed verbs stay visible, are disabled, and carry a non-empty tooltip carrying the shared sentence.
- [x] 3.6 Add `Dismiss Request` visibility tests for both the no-request and request-present cases.
- [x] 3.7 Extend existing `isEnabled()`-only assertions in `gui/tests/test_main_window.cpp` to assert `isVisible()` as well.

## 4. TUI legend

- [x] 4.1 Gate the `d=dismiss` legend entry in `renderUpdatesView` on a non-empty `requestBanner`, mirroring the existing `i=diagnostics` gate; leave the `d` key handler in `tui_app.cpp` unchanged (design D7).
- [x] 4.2 Add render tests in `tui/tests/test_updates_view_render.cpp` asserting the legend omits `d=dismiss` with no request and includes it with one, at 120x32, 100x28 and 80x24, with `q=quit` present and no row exceeding screen width at every size.

## 5. Verification

- [x] 5.1 Build and run the focused GUI and TUI test targets; all green. Host: 95/95 GUI, 13/13 UpdatesViewRender, full host ctest 759/759.
- [x] 5.2 Run `scripts/check-format.sh` (host, then `--container` if pushing) and the clang-tidy gate over the changed files. All gates green in the CI-matched container 2026-07-28: image rebuilt (`build unit` — no volume mount), `ctest` **760/760** (one by-design root skip), CI clang-tidy command **exit 0** with zero user diagnostics, `check-format.sh --container` **OK, 251 files clean** under `/usr/bin/clang-format-18`. Host runners agreed (clang-format 22 clean; zero tidy diagnostics in the changed `.cpp` files).
- [x] 5.3 Run `openspec validate tab-contextual-toolbar --strict`.
