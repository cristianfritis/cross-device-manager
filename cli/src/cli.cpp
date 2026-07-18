#include "cli/src/cli.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <optional>
#include <ostream>

#include "devmgr/core/snapshot_json.hpp"
#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::cli {
namespace {

// The exact text coreErrorFor() (platform/linux dbus_contract.hpp) produces
// when the daemon is not registered on the bus. Matching it lets us return the
// dedicated "unreachable" exit code (4) while every other Io failure — a
// reachable daemon that could not complete the op — maps to "failed" (5).
constexpr const char* kHelperUnavailable = "helper devmgrd is not available";

constexpr const char* kUsageText =
    "usage: devmgr [--bus system|session] snapshot <command>\n"
    "  list [--json]            list snapshots (short id, date, trigger, reason)\n"
    "  create [--label <text>]  take a manual snapshot, print its id\n"
    "  restore <id>             restore a snapshot (id may be any unique prefix)\n"
    "  delete <id>              delete a snapshot (id may be any unique prefix)\n";

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

int doRestore(pal::IPrivilegedChannel& channel, const std::vector<std::string>& rest,
              std::ostream& out, std::ostream& err) {
    if (rest.size() != 1 || rest.front().empty()) {
        err << "usage: devmgr snapshot restore <id>\n";
        return kUsage;
    }
    int code = kOk;
    auto full = resolveId(channel, rest.front(), err, code);
    if (!full) return code;
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
    if (verb == "create") return doCreate(channel, rest, out, err);
    if (verb == "restore") return doRestore(channel, rest, out, err);
    if (verb == "delete") return doDelete(channel, rest, out, err);
    err << "devmgr: unknown snapshot command '" << verb << "'\n";
    return usage(err);
}

}  // namespace devmgr::cli
