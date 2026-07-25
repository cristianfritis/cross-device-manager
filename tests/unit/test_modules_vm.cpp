#include <gtest/gtest.h>

#include <chrono>  // FIX ROUND 1 (i-1): wait_for in the ready-future test
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using devmgr::app::ApplicationFacade;
using devmgr::app::ModulesVM;

namespace {

// FIX ROUND 1 (i-2 test): a dispatcher that queues closures instead of
// running them inline, so posted work outlives the VM that posted it — the
// shape T11 (FTXUI) and T12 (Qt) actually have, unlike InlineUiDispatcher.
class DeferringUiDispatcher final : public devmgr::app::IUiDispatcher {
   public:
    void post(std::function<void()> fn) override { queued.push_back(std::move(fn)); }
    std::vector<std::function<void()>> queued;
};

}  // namespace

class ModulesVMTest : public ::testing::Test {
   protected:
    devmgr::runtime::EventBus bus_;
    devmgr::runtime::TaskScheduler scheduler_;
    devmgr::test::FakePal pal_;
    devmgr::app::DeviceService service_{bus_};
    devmgr::test::InlineUiDispatcher dispatcher_;
    ApplicationFacade facade_{pal_, scheduler_, bus_, service_, nullptr, nullptr, &pal_, &pal_};
    // ModulesVM holds references + a Subscription: construct in place per test
    // (it is neither copyable nor movable).

    void seed(const std::string& name, long refs) {
        devmgr::core::LoadedModule m;
        m.name = name;
        m.sizeBytes = 4096;
        m.refCount = refs;
        pal_.seedLoadedModule(m);
    }
};

TEST_F(ModulesVMTest, RebuildListsModulesWithPlaceholderSignature) {
    seed("dummy", 0);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_NE(v.rowsRef()[0].find("dummy"), std::string::npos);
    EXPECT_NE(v.rowsRef()[0].find("…"), std::string::npos);  // async fill pending
    EXPECT_EQ(v.selectedModule(), "dummy");
}

TEST_F(ModulesVMTest, FilterNarrowsRows) {
    seed("dummy", 0);
    seed("usbhid", 2);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.setFilter("usb");
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_NE(v.rowsRef()[0].find("usbhid"), std::string::npos);
}

TEST_F(ModulesVMTest, SignatureFillReplacesPlaceholder) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.isSigned = true;
    info.signer = "Build key";
    pal_.seedDriver("/anywhere", info);  // moduleInfo() finds by name
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();  // the ModulesChangedEvent normally triggers this via dispatcher
    EXPECT_NE(v.rowsRef()[0].find("yes (Build key)"), std::string::npos);
}

TEST_F(ModulesVMTest, BannerReportsSecureBootAndLockdown) {
    pal_.info.secureBoot = true;
    pal_.info.lockdownMode = "integrity";
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    EXPECT_EQ(v.banner(),
              "Secure Boot: ON · Lockdown: integrity — unsigned modules will be rejected");
    pal_.info.secureBoot = false;
    pal_.info.lockdownMode = "none";
    EXPECT_EQ(v.banner(), "Secure Boot: off · Lockdown: none");
}

// D6: text and valence leave the VM together, so a surface never has to
// reverse-engineer one from the other. banner() stays the text half.
TEST_F(ModulesVMTest, BannerCarriesItsOwnSeverity) {
    pal_.info.secureBoot = true;
    pal_.info.lockdownMode = "integrity";
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    auto line = v.bannerLine();
    EXPECT_EQ(line.text, v.banner());  // one source; banner() is the text half of it
    EXPECT_EQ(line.severity, devmgr::app::StatusSeverity::Warning);

    pal_.info.secureBoot = false;
    pal_.info.lockdownMode = "none";
    line = v.bannerLine();
    EXPECT_EQ(line.text, v.banner());
    EXPECT_EQ(line.severity,
              devmgr::app::StatusSeverity::Info);  // steady posture: not an error (§5.5)
}

// The seam's whole point: the severity follows the SYSTEM STATE, not the words.
// Two postures that produce different banner strings but the same rejection
// state carry the same severity — under the retired substring match this held
// only by luck of the phrase surviving in every wording.
TEST_F(ModulesVMTest, BannerSeverityTracksStateNotWording) {
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);

    pal_.info.secureBoot = true;
    pal_.info.lockdownMode = "integrity";
    const auto a = v.bannerLine();
    pal_.info.secureBoot = false;
    pal_.info.lockdownMode = "confidentiality";
    const auto b = v.bannerLine();
    EXPECT_NE(a.text, b.text);          // different words…
    EXPECT_EQ(a.severity, b.severity);  // …same state, so the same valence
    EXPECT_EQ(a.severity, devmgr::app::StatusSeverity::Warning);
}

TEST_F(ModulesVMTest, DetailLinesIncludeModprobeInfo) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.version = "1.0";
    info.path = "/lib/modules/x/dummy.ko";
    pal_.seedDriver("/anywhere", info);
    pal_.modprobeResult =
        devmgr::core::ModprobeInfo{.options = "numdummies=2", .blacklisted = true};
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    const auto lines = v.detailLines();
    const std::string all = [&] {
        std::string s;
        for (const auto& l : lines) s += l + "\n";
        return s;
    }();
    EXPECT_NE(all.find("numdummies=2"), std::string::npos);
    EXPECT_NE(all.find("blacklisted"), std::string::npos);
}

// FIX ROUND 1 (i-1): fillSignatures() must never return an invalid
// shared_future. Before the fix, `if (names.empty()) return sigFill_;`
// returned the default-constructed sigFill_ verbatim whenever there was
// nothing left to fill AND no fill had ever run yet — a caller doing
// .wait()/.get() on that invalid handle is UB. With zero modules loaded,
// "names" is vacuously empty on the very first-ever call, which is the only
// way to observe sigFill_ still at its default (never-assigned) state
// through the public API — so this is the exact repro, not merely a
// plausible one. ASSERT_TRUE(valid()) runs before any wait so a pre-fix
// failure is a clean assertion failure, not a std::future_error crash.
TEST_F(ModulesVMTest, FillSignaturesWithNothingToFillReturnsReadyFuture) {
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();  // zero modules: snapshot_ empty, sigFill_ never assigned

    auto firstEver = v.fillSignatures();
    ASSERT_TRUE(firstEver.valid());
    EXPECT_EQ(firstEver.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    firstEver.wait();  // must not hang / must not be UB

    // Also cover the brief's original scenario (spec §7.1 perf case):
    // re-entering the view after a real fill has already cached everything.
    seed("dummy", 0);
    ModulesVM v2(facade_, bus_, scheduler_, dispatcher_);
    v2.rebuild();
    v2.fillSignatures().wait();
    v2.rebuild();  // now every name is cached

    auto again = v2.fillSignatures();
    ASSERT_TRUE(again.valid());
    EXPECT_EQ(again.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    again.wait();  // must not hang / must not be UB
}

// FIX ROUND 1 (i-2): the destructor's "no publish into a dead VM" guarantee
// held only for an inline dispatcher (post() runs synchronously). With a real
// queuing dispatcher (T11 FTXUI / T12 Qt), post() returns before the UI
// thread drains the queue, so a closure posted just before destruction could
// still run after the VM is gone. Reproduces that ordering with a
// test-local deferring dispatcher and asserts running the orphaned closure
// afterward is a safe no-op (a clean use-after-free repro under ASan before
// the alive-token fix).
TEST_F(ModulesVMTest, SignedForRowClassifiesSignatureColumn) {
    using devmgr::app::ModuleSignature;
    seed("signedmod", 0);
    seed("unsignedmod", 0);
    seed("unknownmod", 0);  // no seeded driver → moduleDetail() fails → "?" cell
    devmgr::core::Driver s;
    s.name = "signedmod";
    s.isSigned = true;
    s.signer = "Build key";
    pal_.seedDriver("/a", s);
    devmgr::core::Driver u;
    u.name = "unsignedmod";
    u.isSigned = false;
    pal_.seedDriver("/b", u);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();

    // Before the async fill every cell is "…" (pending) → Undetermined.
    for (int i = 0; std::cmp_less(i, v.rowsRef().size()); ++i)
        EXPECT_EQ(v.signedForRow(i), ModuleSignature::Undetermined);

    v.fillSignatures().wait();
    v.rebuild();

    auto sigOfNamed = [&](const char* needle) -> std::optional<ModuleSignature> {
        for (int i = 0; std::cmp_less(i, v.rowsRef().size()); ++i)
            if (v.rowsRef()[i].find(needle) != std::string::npos) return v.signedForRow(i);
        return std::nullopt;
    };
    EXPECT_EQ(sigOfNamed("signedmod"), ModuleSignature::Signed);
    EXPECT_EQ(sigOfNamed("unsignedmod"), ModuleSignature::Unsigned);
    EXPECT_EQ(sigOfNamed("unknownmod"), ModuleSignature::Undetermined);

    EXPECT_FALSE(v.signedForRow(-1).has_value());
    EXPECT_FALSE(v.signedForRow(9999).has_value());
}

TEST_F(ModulesVMTest, SignedForRowNulloptOnPlaceholderRow) {
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();  // no modules → "(no modules)" placeholder, not a module row
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_FALSE(v.signedForRow(0).has_value());
}

TEST_F(ModulesVMTest, PostedClosureAfterDestructionIsDropped) {
    seed("dummy", 0);
    DeferringUiDispatcher deferring;
    {
        ModulesVM v(facade_, bus_, scheduler_, deferring);
        v.rebuild();
        v.fillSignatures().wait();  // worker completes and posts the merge closure
    }  // VM destroyed here; posted closure(s) still queued
    for (auto& fn : deferring.queued) fn();  // must not crash; must be a no-op
}

// ---------------------------------------------------------------------------
// B5 (task 8.5): column order is Name, Signed, Ref, Size, Used-by, and the
// parenthesised signer appears only when there is one to name.
// ---------------------------------------------------------------------------

TEST_F(ModulesVMTest, RowColumnOrderIsNameSignedRefSizeUsedBy) {
    devmgr::core::LoadedModule m;
    m.name = "nvidia";
    m.sizeBytes = 4096 * 1024;  // 4096K
    m.refCount = 3;
    m.holders = {"nvidia_drm"};
    pal_.seedLoadedModule(m);
    devmgr::core::Driver info;
    info.name = "nvidia";
    info.isSigned = false;
    pal_.seedDriver("/anywhere", info);

    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();
    ASSERT_EQ(v.rowsRef().size(), 1U);
    const std::string& row = v.rowsRef()[0];

    const auto name = row.find("nvidia");
    const auto signature = row.find("NO");
    const auto refs = row.find('3');
    const auto size = row.find("4096K");
    const auto usedBy = row.find("nvidia_drm");
    ASSERT_NE(name, std::string::npos);
    ASSERT_NE(signature, std::string::npos);
    ASSERT_NE(refs, std::string::npos);
    ASSERT_NE(size, std::string::npos);
    ASSERT_NE(usedBy, std::string::npos);
    EXPECT_LT(name, signature) << row;
    EXPECT_LT(signature, refs) << row;
    EXPECT_LT(refs, size) << row;
    EXPECT_LT(size, usedBy) << row;
}

// The 80-column drop order: the list pane is 72 wide, so a row is clipped from
// the right. Name/Signed/Ref/Size must all still be inside the pane; Used-by is
// the cell that goes. This is the whole point of moving Signed to column two.
TEST_F(ModulesVMTest, SignedSurvivesTheEightyColumnClip) {
    devmgr::core::LoadedModule m;
    m.name = "snd_hda_codec_realtek";
    m.sizeBytes = 180224 * 1024;
    m.refCount = 1;
    m.holders = {"snd_hda_intel", "snd_hda_codec_generic", "snd_soc_core"};
    pal_.seedLoadedModule(m);
    devmgr::core::Driver info;
    info.name = "snd_hda_codec_realtek";
    info.isSigned = true;
    info.signer = "Build time autogenerated kernel key";  // longer than the column
    pal_.seedDriver("/anywhere", info);

    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();
    const std::string& row = v.rowsRef()[0];

    // Usable width inside the 72-column list pane after the border, the "> "
    // selection marker and the scroll affordance.
    static constexpr std::size_t kUsableCols = 67;
    const auto signature = row.find("yes (");
    const auto size = row.find("180224K");
    ASSERT_NE(signature, std::string::npos) << row;
    ASSERT_NE(size, std::string::npos) << row;
    EXPECT_LT(size + std::string("180224K").size(), kUsableCols) << row;
    // The over-long signer is ellipsis-truncated rather than pushing the numeric
    // columns to the right.
    EXPECT_NE(row.find("…"), std::string::npos) << row;
}

TEST_F(ModulesVMTest, SignerParenthesesOmittedWhenTheSignerIsEmpty) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.isSigned = true;
    info.signer = "";  // present but empty — must not render as "yes ()"
    pal_.seedDriver("/anywhere", info);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();
    EXPECT_EQ(v.rowsRef()[0].find("yes ()"), std::string::npos) << v.rowsRef()[0];
    EXPECT_NE(v.rowsRef()[0].find("yes"), std::string::npos) << v.rowsRef()[0];
}

// The async-pending "…" cell is one column wide but three bytes: it must be
// padded by display width, or every column to its right jumps two places when
// the real signature lands.
TEST_F(ModulesVMTest, PendingSignatureCellKeepsTheColumnsInPlace) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.isSigned = false;
    pal_.seedDriver("/anywhere", info);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    const std::string pending = v.rowsRef()[0];
    v.fillSignatures().wait();
    v.rebuild();
    const std::string filled = v.rowsRef()[0];
    // Compare COLUMNS, not bytes: "…" is three bytes wide and one column wide,
    // which is exactly the trap the by-hand padding exists to avoid.
    auto columnOf = [](const std::string& row, const std::string& needle) {
        const auto at = row.find(needle);
        EXPECT_NE(at, std::string::npos) << row;
        std::size_t cols = 0;
        for (std::size_t i = 0; i < at; ++i) {
            if ((static_cast<unsigned char>(row[i]) & 0xC0U) != 0x80U) ++cols;
        }
        return cols;
    };
    EXPECT_EQ(columnOf(pending, "4K"), columnOf(filled, "4K")) << pending << "\n" << filled;
}

// R4 (task 10.3): the row carries a criticality MARKER; the detail pane names
// the level and the risk in words, so the marker never carries its meaning
// alone (docs/DESIGN.md §10).
TEST_F(ModulesVMTest, EssentialModuleDetailNamesTheRisk) {
    devmgr::core::LoadedModule m;
    m.name = "amdgpu";  // curated essential list: blanks the session at refcount 0
    m.sizeBytes = 4096;
    m.refCount = 0;
    pal_.seedLoadedModule(m);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    const auto lines = v.detailLines();
    bool named = false;
    for (const auto& line : lines) {
        if (line.find("essential") != std::string::npos &&
            line.find("system unusable") != std::string::npos)
            named = true;
    }
    EXPECT_TRUE(named) << lines.front();
    EXPECT_EQ(v.criticalityForRow(0), devmgr::core::Criticality::Essential);
}

TEST_F(ModulesVMTest, OrdinaryModuleDetailHasNoRiskLine) {
    seed("some_leaf_module", 0);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    for (const auto& line : v.detailLines()) {
        EXPECT_EQ(line.find("Risk:"), std::string::npos) << line;
    }
}

// R5 (task 10.4): the Modules header is built from formatModuleRow's own
// widths, so header and rows cannot drift out of alignment.
TEST_F(ModulesVMTest, ColumnHeaderAlignsWithTheRows) {
    devmgr::core::LoadedModule m;
    m.name = "nvidia";
    m.sizeBytes = 4096;
    m.refCount = 0;
    m.holders = {"nvidia_drm"};
    pal_.seedLoadedModule(m);
    devmgr::core::Driver info;
    info.name = "nvidia";
    info.isSigned = false;
    pal_.seedDriver("/anywhere", info);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();

    const std::string header = v.columnHeader();
    const std::string& row = v.rowsRef()[0];
    EXPECT_EQ(header.find("Name"), row.find("nvidia"));
    EXPECT_EQ(header.find("Signed"), row.find("NO"));
    EXPECT_EQ(header.find("Used-by"), row.find("nvidia_drm"));
    // Right-aligned numeric columns end where their values end.
    EXPECT_EQ(header.find("Ref") + 3, row.find('0') + 1);
    EXPECT_EQ(header.find("Size") + 4, row.find("4K") + 2);
}
