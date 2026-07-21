#include "cli/src/cli.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <functional>
#include <optional>
#include <ostream>

#include "devmgr/core/snapshot_history.hpp"
#include "devmgr/core/snapshot_json.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/core/snapshot_presentation.hpp"

namespace devmgr::cli {
namespace {

// The exact text coreErrorFor() (platform/linux dbus_contract.hpp) produces
// when the daemon is not registered on the bus. Matching it lets us return the
// dedicated "unreachable" exit code (4) while every other Io failure — a
// reachable daemon that could not complete the op — maps to "failed" (5).
constexpr const char* kHelperUnavailable = "helper devmgrd is not available";

constexpr const char* kUsageText =
    "usage: devmgr [--bus system|session] snapshot <command>\n"
    "  list [--json]              list snapshots (short id, date, trigger, reason)\n"
    "  history [--json]           list snapshots as a parent chain (HEAD, last good marked)\n"
    "  create [--label <text>]    take a manual snapshot, print its id\n"
    "  diff <a> [<b>] [--json]    show what changed between <a> and <b> (<b> omitted = live "
    "state)\n"
    "  restore [--preview] <id>   restore a snapshot, or --preview to show the change without "
    "restoring\n"
    "  delete <id>                delete a snapshot (id may be any unique prefix)\n";

int usage(std::ostream& err) {
    err << kUsageText;
    return kUsage;
}

int exitCodeFor(const core::Error& e) {
    switch (e.code) {
        case core::Error::Code::Permission:
            return kNotAuthorized;  // polkit refusal
        case core::Error::Code::NotFound:
            return kNotFound;  // no snapshot with that id
        case core::Error::Code::InvalidArgs:
            return kUsage;  // malformed argument — the caller's mistake, not the daemon's
        case core::Error::Code::Busy:
            return kUnreachable;  // no-reply / timeout: daemon not answering
        case core::Error::Code::Io:
            return e.message == kHelperUnavailable ? kUnreachable : kFailed;
        default:  // Unsupported (api too old), Conflict, Network → operation failed
            return kFailed;
    }
}

// One-line stderr error (spec: "Errors print to stderr, one line, no stack
// traces"), returning the mapped exit code.
int reportError(std::ostream& err, const core::Error& e) {
    err << "devmgr: " << e.message << "\n";
    return exitCodeFor(e);
}

// Local date-time cell, same shape as the VM row (snapshot-ui spec): the CLI
// prints daemon metadata directly rather than through the VM, so it re-derives
// the cell here instead of linking the app layer.
std::string localDateTime(std::int64_t utcSeconds) {
    const auto t = static_cast<std::time_t>(utcSeconds);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) return "?";
    static constexpr std::size_t kTimeBufferSize = 20;  // "YYYY-mm-dd HH:MM" + NUL
    std::array<char, kTimeBufferSize> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M", &local) == 0) return "?";
    return {buffer.data()};
}

std::string reasonCell(const core::SnapshotMeta& m) {
    if (m.reason.verb.empty()) return m.reason.subject;  // manual: the label
    if (m.reason.subject.empty()) return m.reason.verb;
    return m.reason.verb + " " + m.reason.subject;
}

std::string listRow(const core::SnapshotMeta& m) {
    std::string row = core::snapshotShortId(m.id) + "  " + localDateTime(m.createdAtUtc) + "  " +
                      to_string(m.trigger) + "  " + reasonCell(m);
    if (m.health != core::SnapshotHealth::Ok) row += "  (" + std::string(to_string(m.health)) + ")";
    return row;
}

// Resolves an id or unique-prefix to a full snapshot id. On no/ambiguous match
// or a failed list it writes the error, sets `code`, and returns nullopt (spec
// "Ambiguous prefix": list the matches and change nothing).
std::optional<std::string> resolveId(pal::IPrivilegedChannel& channel, const std::string& prefix,
                                     std::ostream& err, int& code) {
    auto metas = channel.snapshotList();
    if (!metas) {
        code = reportError(err, metas.error());
        return std::nullopt;
    }
    std::vector<std::string> matches;
    for (const auto& m : *metas)
        if (m.id.starts_with(prefix)) matches.push_back(m.id);
    if (matches.empty()) {
        err << "devmgr: no snapshot matches id '" << prefix << "'\n";
        code = kNotFound;
        return std::nullopt;
    }
    if (matches.size() > 1) {
        err << "devmgr: id '" << prefix << "' is ambiguous, matches:\n";
        for (const auto& id : matches) err << "  " << id << "\n";
        code = kNotFound;
        return std::nullopt;
    }
    return matches.front();
}

// The verb helpers below take (out, err) — stdout for results, stderr for
// errors. NOLINT: the two same-type stream refs are the two well-known standard
// streams (daemon/src convention), not an accidental-swap hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
int doList(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
           std::ostream& out, std::ostream& err) {
    bool json = false;
    for (const auto& a : rest) {
        if (a == "--json") {
            json = true;
        } else {
            err << "devmgr: unexpected argument '" << a << "'\n";
            return usage(err);
        }
    }
    auto metas = channel.snapshotList();
    if (!metas) return reportError(err, metas.error());
    if (json) {
        out << core::snapshotListToJson(*metas) << "\n";
        return kOk;
    }
    if (metas->empty()) {
        out << "(no snapshots)\n";
        return kOk;
    }
    for (const auto& m : *metas) out << listRow(m) << "\n";
    return kOk;
}

// Parent-chain listing (snapshot-history spec). Order and markers come from the
// same core::buildSnapshotChain the GUI/TUI use, so the three surfaces agree on
// chain order, HEAD, and last-good. Rows stay flat (no indentation): chain
// position is carried by the word markers, which read unambiguously in a
// pipe/redirect where indentation would not. `--json` yields the raw metadata
// array — id + parent are all a script needs to rebuild the chain itself
// (design decision 2: no new IPC, chain is derived).
int doHistory(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
              std::ostream& out, std::ostream& err) {
    bool json = false;
    for (const auto& a : rest) {
        if (a == "--json") {
            json = true;
        } else {
            err << "devmgr: unexpected argument '" << a << "'\n";
            return usage(err);
        }
    }
    auto metas = channel.snapshotList();
    if (!metas) return reportError(err, metas.error());
    if (json) {
        out << core::snapshotListToJson(*metas) << "\n";
        return kOk;
    }
    if (metas->empty()) {
        out << "(no snapshots)\n";
        return kOk;
    }
    for (const auto& row : core::buildSnapshotChain(*metas))
        out << listRow(row.meta) << core::chainMarkers(row) << "\n";
    return kOk;
}

int doCreate(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
             std::ostream& out, std::ostream& err) {
    std::string label;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == "--label") {
            if (i + 1 >= rest.size()) {
                err << "devmgr: --label needs a value\n";
                return usage(err);
            }
            label = rest[++i];
        } else {
            err << "devmgr: unexpected argument '" << rest[i] << "'\n";
            return usage(err);
        }
    }
    auto id = channel.snapshotCreate(label);
    if (!id) return reportError(err, id.error());
    out << *id << "\n";
    return kOk;
}

void printOutcome(std::ostream& out, const core::RestoreOutcome& o) {
    out << "restored " << core::snapshotShortId(o.snapshotId) << " (safety snapshot "
        << core::snapshotShortId(o.safetySnapshotId) << ")\n";
    if (o.items.empty()) {
        out << "  (state already matched; no changes needed)\n";
        return;
    }
    for (const auto& i : o.items) {
        out << "  " << i.status << "  " << i.action << "  " << i.subject;
        if (!i.detail.empty()) out << " — " << i.detail;
        out << "\n";
    }
}

// Splits `rest` into positional ids and a set of recognized flags. Ids are hex
// prefixes and never start with "--", so a leading-dash token that is not a
// known flag is a usage error rather than an id.
struct ParsedArgs {
    std::vector<std::string> positionals;
    bool ok = true;
};
ParsedArgs splitArgs(const std::vector<std::string>& rest, std::ostream& err,
                     const std::function<bool(const std::string&)>& takeFlag) {
    ParsedArgs parsed;
    for (const auto& a : rest) {
        if (takeFlag(a)) continue;
        if (a.starts_with("--")) {
            err << "devmgr: unexpected argument '" << a << "'\n";
            parsed.ok = false;
            return parsed;
        }
        parsed.positionals.push_back(a);
    }
    return parsed;
}

// diff <a> [<b>] [--json]: <b> omitted diffs <a> against live state. Read-only
// and polkit-free (parity with list). Identical payloads print the explicit
// "No differences." line core::diffLines emits — not an empty result.
int doDiff(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
           std::ostream& out, std::ostream& err) {
    bool json = false;
    auto args = splitArgs(rest, err, [&](const std::string& a) {
        if (a != "--json") return false;
        json = true;
        return true;
    });
    if (!args.ok) return usage(err);
    if (args.positionals.empty() || args.positionals.size() > 2) {
        err << "usage: devmgr snapshot diff <a> [<b>]\n";
        return kUsage;
    }
    int code = kOk;
    auto baseFull = resolveId(channel, args.positionals[0], err, code);
    if (!baseFull) return code;
    std::string targetFull;  // empty ⇒ diff against live state
    if (args.positionals.size() == 2) {
        auto t = resolveId(channel, args.positionals[1], err, code);
        if (!t) return code;
        targetFull = *t;
    }
    auto diff = channel.snapshotDiff(*baseFull, targetFull);
    if (!diff) return reportError(err, diff.error());
    if (json) {
        out << core::snapshotDiffToJson(*diff) << "\n";
        return kOk;
    }
    for (const auto& line : core::diffLines(*diff)) out << line << "\n";
    return kOk;
}

// restore --preview: the diff against live state plus the partial-convergence
// note, sharing core wording with the GUI/TUI preview. Restores nothing and
// exits 0, so it is safe to run before a scripted recovery.
int printRestorePreview(pal::IPrivilegedChannel& channel, const std::string& id, std::ostream& out,
                        std::ostream& err) {
    auto diff = channel.snapshotDiff(id, "");  // "" ⇒ live state
    if (!diff) return reportError(err, diff.error());
    out << "Restore preview for " << core::snapshotShortId(id) << " (nothing is restored):\n";
    for (const auto& line : core::restorePreviewChangeLines(*diff)) out << line << "\n";
    out << "\n" << core::restorePreviewConvergenceNote() << "\n";
    return kOk;
}

int doRestore(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
              std::ostream& out, std::ostream& err) {
    bool preview = false;
    auto args = splitArgs(rest, err, [&](const std::string& a) {
        if (a != "--preview") return false;
        preview = true;
        return true;
    });
    if (!args.ok) return usage(err);
    if (args.positionals.size() != 1 || args.positionals.front().empty()) {
        err << "usage: devmgr snapshot restore [--preview] <id>\n";
        return kUsage;
    }
    int code = kOk;
    auto full = resolveId(channel, args.positionals.front(), err, code);
    if (!full) return code;
    if (preview) return printRestorePreview(channel, *full, out, err);
    auto outcome = channel.snapshotRestore(*full);
    if (!outcome) return reportError(err, outcome.error());
    printOutcome(out, *outcome);
    return kOk;
}

int doDelete(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
             std::ostream& out, std::ostream& err) {
    if (rest.size() != 1 || rest.front().empty()) {
        err << "usage: devmgr snapshot delete <id>\n";
        return kUsage;
    }
    int code = kOk;
    auto full = resolveId(channel, rest.front(), err, code);
    if (!full) return code;
    auto r = channel.snapshotDelete(*full);
    if (!r) return reportError(err, r.error());
    out << "deleted " << core::snapshotShortId(*full) << "\n";
    return kOk;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — out/err are stdout/stderr
int run(pal::IPrivilegedChannel& channel, const std::vector<std::string>& args, std::ostream& out,
        std::ostream& err) {
    if (args.empty() || args.front() != "snapshot") return usage(err);
    if (args.size() < 2) return usage(err);
    const std::string& verb = args[1];
    const std::vector<std::string> rest(args.begin() + 2, args.end());
    if (verb == "list") return doList(channel, rest, out, err);
    if (verb == "history") return doHistory(channel, rest, out, err);
    if (verb == "create") return doCreate(channel, rest, out, err);
    if (verb == "diff") return doDiff(channel, rest, out, err);
    if (verb == "restore") return doRestore(channel, rest, out, err);
    if (verb == "delete") return doDelete(channel, rest, out, err);
    err << "devmgr: unknown snapshot command '" << verb << "'\n";
    return usage(err);
}

}  // namespace devmgr::cli
