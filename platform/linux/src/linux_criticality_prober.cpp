#include "devmgr/platform/linux/linux_criticality_prober.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {

// Keyboard heuristic (same spirit as udev's input_id): the full QWERTY top
// row (KEY_Q=16 .. KEY_P=25) is present in capabilities/key.
constexpr unsigned kKeyQ = 16;
constexpr unsigned kKeyP = 25;
// capabilities files print hex words of unsigned long — 64-bit here.
constexpr unsigned kBitsPerWord = 64;

std::string readFirstLine(const fs::path& p) {
    std::ifstream in(p);
    std::string line;
    std::getline(in, line);
    return line;
}

// capabilities files are hex words, MOST significant first. Bit `bit` is
// counted from the LSB of the LAST word.
bool bitSet(const std::string& hexWords, unsigned bit) {
    std::vector<std::string> words;
    std::istringstream in(hexWords);
    for (std::string w; in >> w;) words.push_back(w);
    const std::size_t wordIdx = bit / kBitsPerWord;
    if (wordIdx >= words.size()) return false;
    const std::string& word = words[words.size() - 1 - wordIdx];
    std::uint64_t value = 0;
    std::istringstream(word) >> std::hex >> value;
    return ((value >> (bit % kBitsPerWord)) & 1U) != 0U;
}

bool isKeyboard(const std::string& keyBits) {
    if (keyBits.empty()) return false;
    for (unsigned code = kKeyQ; code <= kKeyP; ++code)
        if (!bitSet(keyBits, code)) return false;
    return true;
}

// Pointer: relative X and Y axes (REL_X=0, REL_Y=1).
bool isPointer(const std::string& relBits) {
    return !relBits.empty() && bitSet(relBits, 0) && bitSet(relBits, 1);
}

// Expand a block-device name to its physical leaves through slaves/
// (virtual dm/md devices sit under devices/virtual and would never prefix-
// match a real controller path — their slaves do).
void expandBlock(const fs::path& classBlock, const std::string& name,
                 std::vector<std::string>& out) {
    const fs::path dir = classBlock / name;
    std::error_code ec;
    const fs::path slaves = dir / "slaves";
    if (fs::is_directory(slaves, ec) && !fs::is_empty(slaves, ec)) {
        for (const auto& entry : fs::directory_iterator(slaves, ec))
            expandBlock(classBlock, entry.path().filename().string(), out);
        return;
    }
    const fs::path real = fs::canonical(dir, ec);
    if (!ec) out.push_back(real.string());
}

// One /proc/self/mounts pass: sources of "/" and "/boot" mounts, resolved to
// canonical sysfs block-device paths.
void collectStorageFacts(std::istream& mounts, const fs::path& classBlock,
                         pal::CriticalityFacts& facts) {
    for (std::string line; std::getline(mounts, line);) {
        std::istringstream fields(line);
        std::string source;
        std::string target;
        if (!(fields >> source >> target)) continue;
        std::vector<std::string>* bucket = nullptr;
        if (target == "/") bucket = &facts.rootBackingPaths;
        if (target == "/boot" || target == "/boot/efi") bucket = &facts.bootBackingPaths;
        if (bucket == nullptr || !source.starts_with('/')) continue;
        std::error_code ec;
        const fs::path node = fs::canonical(fs::path(source), ec);  // /dev/mapper/x → /dev/dm-0
        if (ec) continue;
        expandBlock(classBlock, node.filename().string(), *bucket);
    }
}

void collectInputFacts(const fs::path& classInput, pal::CriticalityFacts& facts) {
    std::error_code ec;
    if (!fs::is_directory(classInput, ec)) return;
    for (const auto& entry : fs::directory_iterator(classInput, ec)) {
        if (!entry.path().filename().string().starts_with("input")) continue;
        const fs::path real = fs::canonical(entry.path(), ec);
        if (ec) continue;
        const std::string keyBits = readFirstLine(real / "capabilities/key");
        const std::string relBits = readFirstLine(real / "capabilities/rel");
        if (isKeyboard(keyBits)) facts.keyboardPaths.push_back(real.string());
        if (isPointer(relBits)) facts.pointerPaths.push_back(real.string());
    }
}

void sortUnique(std::vector<std::string>& v) {
    std::ranges::sort(v);
    const auto dup = std::ranges::unique(v);
    v.erase(dup.begin(), dup.end());
}

}  // namespace

LinuxCriticalityProber::LinuxCriticalityProber(std::string sysfsRoot, std::string mountsPath)
    : sysfsRoot_(std::move(sysfsRoot)), mountsPath_(std::move(mountsPath)) {}

core::Result<pal::CriticalityFacts> LinuxCriticalityProber::probe() {
    std::ifstream mounts(mountsPath_);
    if (!mounts) return core::makeError(core::Error::Code::Io, "cannot read " + mountsPath_);

    pal::CriticalityFacts facts;
    collectStorageFacts(mounts, fs::path(sysfsRoot_) / "class/block", facts);
    collectInputFacts(fs::path(sysfsRoot_) / "class/input", facts);

    sortUnique(facts.rootBackingPaths);
    sortUnique(facts.bootBackingPaths);
    sortUnique(facts.keyboardPaths);
    sortUnique(facts.pointerPaths);
    return facts;
}

}  // namespace devmgr::platform_linux
