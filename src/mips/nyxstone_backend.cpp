#include "mips/nyxstone_backend.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#if CLEARCORE_NYXSTONE_ENABLED

#include <nyxstone.h>

namespace mips {

namespace {

// LLVM triple for little-endian MIPS32. Matches the emulator's memory model
// (Memory stores/reads words little-endian) and the base MIPS32 ISA the
// Decoder/Alu implement.
constexpr const char* kTriple = "mipsel-linux-gnu";

}  // namespace

// Holds the LLVM-backed Nyxstone instance out of line so no LLVM/Nyxstone type
// appears in the public header.
struct NyxstoneBackend::Impl {
    std::unique_ptr<nyxstone::Nyxstone> nyx;
};

NyxstoneBackend::NyxstoneBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
NyxstoneBackend::NyxstoneBackend(NyxstoneBackend&&) noexcept            = default;
NyxstoneBackend& NyxstoneBackend::operator=(NyxstoneBackend&&) noexcept = default;
NyxstoneBackend::~NyxstoneBackend()                                     = default;

std::optional<NyxstoneBackend> NyxstoneBackend::create() {
    auto built = nyxstone::NyxstoneBuilder(std::string{kTriple}).build();
    if (!built) return std::nullopt;

    auto impl = std::make_unique<Impl>();
    impl->nyx = std::move(built.value());
    return NyxstoneBackend(std::move(impl));
}

NyxstoneBackend::Result<std::vector<uint32_t>> NyxstoneBackend::assemble(const std::string& text,
                                                                         uint32_t address) const {
    Result<std::vector<uint32_t>> out;

    // Assemble in no-reorder mode. clearCore's CPUs do not model MIPS branch
    // delay slots — jumps and branches take effect immediately — whereas LLVM's
    // default (.set reorder) auto-inserts a nop into the delay slot after every
    // control-transfer instruction. Prefixing the directive keeps LLVM's output
    // one-word-per-instruction, matching our execution model.
    const std::string src = ".set noreorder\n" + text;

    auto bytes = impl_->nyx->assemble(src, address, /*labels=*/{});
    if (!bytes) {
        out.error = bytes.error();
        return out;
    }

    const auto& raw = bytes.value();
    if (raw.size() % 4 != 0) {
        out.error = "assembled byte count (" + std::to_string(raw.size()) +
                    ") is not a multiple of 4; not whole MIPS32 words";
        return out;
    }

    out.value.reserve(raw.size() / 4);
    for (std::size_t i = 0; i < raw.size(); i += 4) {
        // mipsel: instruction bytes are little-endian in the object stream.
        out.value.push_back(
            static_cast<uint32_t>(raw[i]) | (static_cast<uint32_t>(raw[i + 1]) << 8) |
            (static_cast<uint32_t>(raw[i + 2]) << 16) | (static_cast<uint32_t>(raw[i + 3]) << 24));
    }
    return out;
}

NyxstoneBackend::Result<std::string>
NyxstoneBackend::disassemble(const std::vector<uint32_t>& words, uint32_t address) const {
    Result<std::string> out;

    std::vector<uint8_t> bytes;
    bytes.reserve(words.size() * 4);
    for (const uint32_t w : words) {
        bytes.push_back(static_cast<uint8_t>(w & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((w >> 8) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((w >> 16) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((w >> 24) & 0xFFu));
    }

    auto text = impl_->nyx->disassemble(bytes, address, /*count=*/0);
    if (!text) {
        out.error = text.error();
        return out;
    }
    out.value = text.value();
    return out;
}

}  // namespace mips

#endif  // CLEARCORE_NYXSTONE_ENABLED
