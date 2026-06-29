# Phase 0 Execution Guide (read this first in a new session)

This is the **durable, committed resume state** for building the cross-device-manager.
The `.superpowers/sdd/` workspace (task briefs, reports, review diffs, progress ledger)
is git-ignored scratch and may be deleted between sessions — **this file is the source of
truth that survives cleanup.** Update the Task Status checklist below as tasks land, and
commit it alongside each task.

---

## TL;DR for a fresh session

1. Read this whole file.
2. Read the task-level plan: `docs/superpowers/plans/2026-06-28-phase0-foundations.md`.
3. Find the first unchecked task in **Task Status** below → that's your next task.
4. Follow **Per-Task Procedure**. One task per session is the intended cadence.
5. When the task is review-clean, hand the user the commit command (see **Commit Policy**),
   update **Task Status** here, and stop.

---

## What we're building

A cross-platform **Device Manager** in C++20 (TUI + GUI): real-time hotplug detection,
device enumeration + metadata, driver inspection/update, programmatic enable/disable, and
versioned backup of driver state with rollback. Built **Linux-first** behind a Platform
Abstraction Layer (PAL) so Windows can slot in later.

### Locked decisions (do not re-litigate)
- **Linux-first** — libudev / sysfs / libkmod / fwupd / polkit. macOS write-ops out of scope.
- **Real production tool** posture — real ops, guardrails, transactional rollback.
- **GUI = Qt 6; TUI = FTXUI.** Shared OS-agnostic core static lib `devmgr_core`; two thin
  frontend binaries. UI marshaling via `IUiDispatcher`.
- **Driver/firmware updates → fwupd/LVFS** + package-manager/DKMS for kernel modules.
- **Privilege separation → unprivileged frontends + polkit-gated D-Bus helper `devmgrd`** (sdbus-c++).
- **Backup/versioning → content-addressed immutable JSON snapshots + history graph** (no libgit2).
- **Testing isolation:** Docker shares the host kernel, so Docker is only safe for userspace
  logic + `umockdev` device mocking. Genuinely dangerous kernel-level ops are tested in a
  snapshotted disposable VM (QEMU/KVM).

Full architecture + 10-phase roadmap: `~/.claude/plans/you-can-use-context7-kind-gadget.md`
(machine-local plan-mode file) and project memory
`~/.claude/projects/-home-cfritis-Projects-Personal-cross-device-manager/memory/architecture-decisions.md`.

### Roadmap (each phase = its own plan → SDD execution cycle)
0. **Foundations** (current) — build system, core types, EventBus, TaskScheduler, logging, PAL
   interfaces + FakePal, Docker/VM/CI harness.
1. Read-only enumeration (libudev) + first FTXUI TUI.
2. Hotplug monitor + debouncing.
3. Qt GUI parity (validates the UI abstraction).
4. Enable/disable + polkit `devmgrd` helper + critical-device guard (VM-tested).
5. Driver/module management (libkmod, signatures/Secure Boot).
6. Driver/firmware updates (fwupd + package-manager/DKMS).
7. Backup & version control (snapshots, rollback, boot-success sentinel).
8. Hardening & packaging (deb/rpm, systemd unit, polkit policy).
9. (Future) Windows PAL; macOS read-only.

---

## Execution mode & constraints

- **Workflow:** Subagent-Driven Development (superpowers skill). One implementer subagent per
  task (TDD), one task-reviewer subagent (spec + quality), fix loop, then a final whole-branch
  review after all Phase 0 tasks. Use model `sonnet` for implementers/reviewers; `opus` for the
  final review.
- **Commit policy (IMPORTANT):** This environment **denies `git add` / `git commit`** for the
  agent. **Do not attempt to commit, and do not route git through wrappers to bypass it.**
  The **user commits every task themselves.** Implementer subagents must be told to **skip all
  commit steps** and leave changes uncommitted. When a task is review-clean, hand the user a
  ready-to-run `git add … && git commit …` command and wait.
- **Per-task review without commits:** because work stays uncommitted until the user commits,
  generate the review diff from the working tree (modifications vs HEAD + untracked files shown
  as additions). The user committing each task before the next keeps each task's working-tree
  diff clean.
- **Environment:** `VCPKG_ROOT=/home/cfritis/dev/vcpkg`. Tools: ninja, cmake 4.x, clang++ (LLVM 21),
  g++. First configure builds vcpkg deps from source (slow — allow long timeouts).

---

## Per-Task Procedure

1. Extract the brief:
   `<sdd>/scripts/task-brief docs/superpowers/plans/2026-06-28-phase0-foundations.md N`
   (`<sdd>` = `~/.claude/plugins/cache/claude-plugins-official/superpowers/<ver>/skills/subagent-driven-development`)
2. Dispatch an implementer subagent (sonnet, general-purpose) with: where the task fits, the
   brief path, and an explicit **"do NOT commit — leave changes uncommitted"** instruction.
3. Generate a working-tree review package (status + `git diff HEAD` + untracked files as
   additions, excluding `docs/` and `.superpowers/`).
4. Dispatch a task-reviewer subagent (sonnet) with the brief, the implementer's report, the diff
   file, and the Global Constraints from the plan.
5. Fix loop for any Critical/Important findings (fix subagent re-runs covering tests).
6. When review is clean: update **Task Status** below, hand the user the commit command, stop.

---

## Build & test commands

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

Dockerized unit run (added in Task 8): `docker compose -f test/docker-compose.yml run --rm unit`

### Formatting (keep the tree clang-format-clean)

`.clang-format` is tuned away from raw Google defaults so the plan's authored style is canonical
under clang-format 21.x: `SortIncludes: false` + `IncludeBlocks: Preserve` (keep author include
grouping) and `AllowShortFunctionsOnASingleLine: Inline` (in-class one-liners like FakePal
accessors stay one line; free functions and gtest `TEST(...)` bodies stay multi-line). Trade-off:
namespace-scope one-liners get expanded — write them multi-line (e.g. Task 3's `operator==`).

**After writing each task's code, before handoff, run:**

```bash
find core tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i
```

Task 9's CI runs `clang-format --dry-run --Werror` over `core`/`tests`, so an unformatted tree
fails CI. Keep it clean per-task.

---

## Safe cleanup between sessions

All of these are git-ignored and regenerated; safe to delete to clear stale cache:

```bash
rm -rf build/ vcpkg_installed/ .superpowers/sdd/   # caches + SDD scratch
# or the nuclear option (also removes the above):
git clean -fdx
```

After cleanup, `git status` should be clean (only tracked files remain). Re-run the build
commands above to regenerate. **Never** delete tracked sources or `docs/`.

---

## Task Status (Phase 0) — update + commit this with each task

- [x] **Task 1 — Build scaffold + version smoke test.** ✅ implemented, reviewed clean (1/1 test).
      *Awaiting user commit at time of writing.* Note: `tests/CMakeLists.txt` includes
      `${CMAKE_CURRENT_SOURCE_DIR}` — needed by Task 7's `fakes/` include, keep it.
- [x] **Task 2 — Result/Error type** (`core/include/devmgr/core/result.hpp`). ✅ implemented via
      TDD, 3/3 tests green. *Awaiting user commit.* (Folded in: clang-format baseline settled —
      see **Formatting** below.)
- [x] **Task 3 — Domain models + enum string helpers** (`models.hpp/.cpp`). ✅ implemented via
      TDD, 6/6 tests green, clang-format clean. *Awaiting user commit.*
- [ ] Task 4 — Event types + thread-safe EventBus (`events.hpp`, `runtime/event_bus.hpp`).
- [ ] Task 5 — Logging facade (`runtime/logging.hpp/.cpp`).
- [ ] Task 6 — Cancellation + TaskScheduler + progress (`runtime/`).
- [ ] Task 7 — PAL interfaces + in-memory FakePal (`pal/`, `tests/fakes/`).
- [ ] Task 8 — Docker dev/test image (`Dockerfile`, `test/docker-compose.yml`).
- [ ] Task 9 — Disposable VM scaffold + CI workflow (`test/vm/`, `.github/workflows/ci.yml`).

When all boxes are checked: run the final whole-branch review (opus), then use the
`finishing-a-development-branch` skill.
