#include "cli/src/cli.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <functional>
#include <optional>
#include <ostream>
#include <string_view>

#include "devmgr/core/device_detail_fields.hpp"
#include "devmgr/core/device_json.hpp"
#include "devmgr/core/device_presentation.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/core/snapshot_history.hpp"
#include "devmgr/core/snapshot_json.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/core/snapshot_presentation.hpp"
#include "devmgr/pal/refusing_backends.hpp"

namespace devmgr::cli {
namespace {

// Markers (all lowercase) that identify a "daemon/helper is unreachable or
// could not be activated" failure — CLI exit 4 (kUnreachable) — as opposed to a
// reached daemon that failed the op (exit 5, kFailed). coreErrorFor()
// (platform/linux dbus_contract.hpp) prefers stable D-Bus/systemd error *names*,
// mapping the unreachable ones to Busy; this list is the message-text fallback
// for the cases that only reach us as a string (a name embedded in the message,
// or a transport error). Matched as a case-insensitive substring — never exact
// byte equality — so wording or capitalization drift does not misclassify.
constexpr std::array<std::string_view, 18> kUnreachableMarkers = {
    // Stable D-Bus / systemd error names (may arrive embedded in the message,
    // e.g. "org.freedesktop.systemd1.UnitMasked: Unit ... is masked.").
    "systemd1.unitmasked", "systemd1.nosuchunit", "systemd1.unitinactive",
    "dbus.error.serviceunknown", "dbus.error.namehasnoowner", "dbus.error.noreply",
    "dbus.error.spawn.",  // Spawn.ExecFailed / ServiceNotValid / FileNotFound
    "dbus.error.noserver", "dbus.error.disconnected",
    // Human-readable phrases the same failures surface as.
    "helper devmgrd is not available", "is masked", "devmgrd.service not found",
    "activation via systemd failed", "failed to activate service",
    "was not provided by any .service files", "disconnected from message bus without replying",
    "failed to connect to socket", "could not connect to bus"};

// True when the Error means the daemon/helper is unreachable or could not be
// activated (exit 4), not a reached daemon that failed the op (exit 5). Prefers
// the stable transport signal (Busy = no-reply/timeout, set by coreErrorFor);
// for Io it scans the message case-insensitively against kUnreachableMarkers.
// Any other code has its own exit code and is not this function's concern.
bool isDaemonUnavailableError(const core::Error& e) {
    if (e.code == core::Error::Code::Busy) return true;  // no-reply/timeout: not answering
    if (e.code != core::Error::Code::Io) return false;
    std::string lower = e.message;
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::ranges::any_of(kUnreachableMarkers, [&](std::string_view marker) {
        return lower.find(marker) != std::string::npos;
    });
}

// The inventory verbs are listed separately because they are a different
// contract: they read the platform directly, so they work with no helper
// running and on a platform that has none. They are appended to the usage text
// only when the running platform has a device enumerator (task 6.9) — usage
// must not advertise a verb the binary will refuse.
constexpr const char* kInventoryUsageText =
    "\n"
    "       devmgr devices <command>            (reads the system directly; no helper needed)\n"
    "  list [--json]              list present devices (name, bus, status)\n"
    "  show <id> [--json]         show one device's full record\n";

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

// Scoped usage: a user who typed `snapshot` and got the arguments wrong is
// asking about snapshots, so the snapshot table is the answer.
int usage(std::ostream& err) {
    err << kUsageText;
    return kUsage;
}

// Full usage, for the top level where the user has not named a family yet. The
// inventory table appears only when the running platform has a device
// enumerator, so usage never advertises a verb this binary would refuse
// (task 6.9).
int topLevelUsage(std::ostream& err, bool inventoryOffered) {
    err << kUsageText;
    if (inventoryOffered) err << kInventoryUsageText;
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
        case core::Error::Code::Io:
            // Busy (no-reply/timeout) and daemon-unavailable Io failures both mean
            // "could not reach or activate the daemon" → kUnreachable; any other Io
            // failure is a reached daemon that failed the op → kFailed.
            return isDaemonUnavailableError(e) ? kUnreachable : kFailed;
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

// ---- Inventory verbs (design D6) -------------------------------------------
//
// These read IDeviceEnumerator directly: no privileged channel, no daemon, no
// polkit. kUnreachable and kNotAuthorized are therefore structurally
// unreachable for them, which is the point — a user with a dead helper can
// still see what hardware is present.

// One list row: the canonical name and bus label the GUI and TUI show for the
// same device, byte for byte (cli-inventory: "Command-line output matches the
// other surfaces"). Composed here, but not SPELLED here — every user-visible
// word comes from a shared presentation helper.
std::string deviceRow(const core::Device& d) {
    return core::displayDeviceName(d) + "  [" + core::displayBus(d.bus) + "]  " +
           std::string(core::to_string(d.status)) + "  " + d.id.value;
}

// Enumeration failed: the caller must be able to tell that from "the query ran
// and found nothing" (task 6.6). A failure prints to stderr and returns
// kFailed; it never prints an empty list and returns success.
int reportEnumerationFailure(std::ostream& err, const core::Error& e) {
    err << "devmgr: cannot read devices: " << e.message << "\n";
    return kFailed;
}

int doDevicesList(pal::IDeviceEnumerator& enumerator, const std::vector<std::string>& rest,
                  std::ostream& out, std::ostream& err) {
    bool json = false;
    for (const auto& a : rest) {
        if (a == "--json") {
            json = true;
        } else {
            err << "devmgr: unexpected argument '" << a << "'\n";
            return kUsage;
        }
    }
    auto devices = enumerator.enumerate();
    if (!devices) return reportEnumerationFailure(err, devices.error());
    if (json) {
        out << core::deviceListToJson(*devices) << "\n";
        return kOk;
    }
    // The empty-result string asserts a completed query, which is exactly what
    // happened: the enumerator answered and reported nothing. The failure path
    // above never reaches here, so this string cannot stand in for an error.
    if (devices->empty()) {
        out << "(no devices)\n";
        return kOk;
    }
    for (const auto& d : *devices) out << deviceRow(d) << "\n";
    return kOk;
}

// The full record for one device, as label/value rows. The shared detail-field
// vocabulary supplies the rows that come from the property map, under the same
// labels and in the same order the GUI and TUI use (task 5a.5) — the CLI
// authors none of them and reads no raw platform key.
void writeDeviceDetail(std::ostream& out, const core::Device& d) {
    out << "Name:     " << core::displayDeviceName(d) << "\n";
    out << "Id:       " << d.id.value << "\n";
    out << "Bus:      " << core::displayBus(d.bus) << "\n";
    out << "Status:   " << core::to_string(d.status) << "\n";
    // Absent properties are omitted rather than blanked, the same rule the
    // detail panes follow (ui-accessibility).
    const auto row = [&out](const char* label, const std::string& value) {
        if (!value.empty()) out << label << value << "\n";
    };
    const auto fields = core::detailFields(d);
    const auto published = [&fields](core::DetailField field) {
        return std::ranges::any_of(fields, [field](const auto& f) { return f.field == field; });
    };
    if (!published(core::DetailField::DeviceInstanceId)) row("Identity: ", d.nativeId);
    if (!d.vendorId.empty() || !d.productId.empty())
        out << "VID:PID:  " << d.vendorId << ":" << d.productId << "\n";
    row("Serial:   ", d.serial);
    if (d.boundDriver.has_value() && !published(core::DetailField::Driver))
        out << "Driver:   " << *d.boundDriver << "\n";
    if (!published(core::DetailField::HardwareIds)) row("Hardware ID: ", d.hardwareId);
    if (d.parent.has_value()) out << "Parent:   " << d.parent->value << "\n";
    for (const auto& field : fields) out << field.label << ": " << field.value << "\n";
}

int doDevicesShow(pal::IDeviceEnumerator& enumerator, const std::vector<std::string>& rest,
                  std::ostream& out, std::ostream& err) {
    bool json = false;
    std::optional<std::string> id;
    for (const auto& a : rest) {
        if (a == "--json") {
            json = true;
        } else if (!id) {
            id = a;
        } else {
            err << "devmgr: unexpected argument '" << a << "'\n";
            return kUsage;
        }
    }
    if (!id) {
        err << "devmgr: devices show needs a device id\n";
        return kUsage;
    }
    auto devices = enumerator.enumerate();
    if (!devices) return reportEnumerationFailure(err, devices.error());
    const auto match =
        std::ranges::find_if(*devices, [&id](const core::Device& d) { return d.id.value == *id; });
    if (match == devices->end()) {
        err << "devmgr: no device matches id '" << *id << "'\n";
        return kNotFound;
    }
    if (json) {
        out << core::deviceToJson(*match) << "\n";
        return kOk;
    }
    writeDeviceDetail(out, *match);
    return kOk;
}

int runDevices(pal::IDeviceEnumerator& enumerator, const std::vector<std::string>& args,
               std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << "devmgr: devices needs a command (list|show)\n";
        return kUsage;
    }
    const std::string& verb = args.front();
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (verb == "list") return doDevicesList(enumerator, rest, out, err);
    if (verb == "show") return doDevicesShow(enumerator, rest, out, err);
    err << "devmgr: unknown devices command '" << verb << "'\n";
    return kUsage;
}

// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace

int runSnapshot(pal::IPrivilegedChannel& channel, const std::vector<std::string>& args,
                std::ostream& out, std::ostream& err) {
    if (args.empty()) return usage(err);
    const std::string& verb = args.front();
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (verb == "list") return doList(channel, rest, out, err);
    if (verb == "history") return doHistory(channel, rest, out, err);
    if (verb == "create") return doCreate(channel, rest, out, err);
    if (verb == "diff") return doDiff(channel, rest, out, err);
    if (verb == "restore") return doRestore(channel, rest, out, err);
    if (verb == "delete") return doDelete(channel, rest, out, err);
    err << "devmgr: unknown snapshot command '" << verb << "'\n";
    return usage(err);
}

int run(const Context& context, const std::vector<std::string>& args, std::ostream& out,
        std::ostream& err) {
    // Read once, from the descriptor: whether this platform offers the
    // inventory family at all is a build-and-machine fact, not something to
    // discover by calling the enumerator and failing (design D1).
    const bool inventory = context.capabilities.deviceEnumeration;
    if (args.empty()) return topLevelUsage(err, inventory);
    const std::string& family = args.front();
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    // `snapshot` reaches the channel; `devices` never does. Nothing above this
    // line has touched either backend, so the no-args, unknown-family and usage
    // paths make no connection attempt of any kind (task 6.2).
    if (family == "snapshot") return runSnapshot(context.channel, rest, out, err);
    if (family == "devices" && inventory) return runDevices(context.enumerator, rest, out, err);
    if (family == "devices") {
        // The platform has no enumerator, so this verb cannot exist here. Said
        // once, calmly, with no instruction to act on — there is nothing the
        // user can install or start that would change it.
        err << "devmgr: device inventory is not available on this platform\n";
        return topLevelUsage(err, /*inventoryOffered=*/false);
    }
    return topLevelUsage(err, inventory);
}

int run(pal::IPrivilegedChannel& channel, const std::vector<std::string>& args, std::ostream& out,
        std::ostream& err) {
    const Context context{
        .channel = channel, .enumerator = pal::refusingDeviceEnumerator(), .capabilities = {}};
    return run(context, args, out, err);
}

}  // namespace devmgr::cli
