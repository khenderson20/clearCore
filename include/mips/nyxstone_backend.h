#pragma once

// ─── nyxstone_backend.h ──────────────────────────────────────────────────────
// Thin clearCore wrapper over Nyxstone (LLVM's MC layer) configured for
// little-endian MIPS32. It provides a *ground-truth* assembler/disassembler so
// the hand-written Decoder, Disassembler, and Qt assembler can be differentially
// validated against a production toolchain (see tests/mips/nyxstone_test.cpp).
//
// Compiled only when CLEARCORE_NYXSTONE_ENABLED — i.e. an in-range LLVM (15–20)
// was found at configure time. All LLVM/Nyxstone types are pimpl'd away, so
// consumers of this header never need LLVM include paths.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mips {

#if CLEARCORE_NYXSTONE_ENABLED

class NyxstoneBackend {
public:
    // Outcome of an assemble/disassemble call. On failure `error` is non-empty
    // and carries LLVM's diagnostic; `value` is then unspecified.
    template <typename T> struct Result {
        T                  value{};
        std::string        error;
        [[nodiscard]] bool ok() const noexcept { return error.empty(); }
        explicit           operator bool() const noexcept { return ok(); }
    };

    // Build a backend for little-endian MIPS32 ("mipsel"). Returns nullopt if
    // LLVM cannot construct the target machine (e.g. the Mips target was not
    // linked into the LLVM build).
    [[nodiscard]] static std::optional<NyxstoneBackend> create();

    NyxstoneBackend(NyxstoneBackend&&) noexcept;
    NyxstoneBackend& operator=(NyxstoneBackend&&) noexcept;
    ~NyxstoneBackend();
    NyxstoneBackend(const NyxstoneBackend&)            = delete;
    NyxstoneBackend& operator=(const NyxstoneBackend&) = delete;

    // Assemble MIPS assembly text into 32-bit instruction words. `address` is
    // the base virtual address (relevant to PC-relative operands). Labels and
    // comments are handled by LLVM's assembler.
    [[nodiscard]] Result<std::vector<uint32_t>> assemble(const std::string& text,
                                                         uint32_t           address = 0) const;

    // Disassemble instruction words into canonical MIPS assembly text.
    [[nodiscard]] Result<std::string> disassemble(const std::vector<uint32_t>& words,
                                                  uint32_t                     address = 0) const;

private:
    struct Impl;
    explicit NyxstoneBackend(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

#endif  // CLEARCORE_NYXSTONE_ENABLED

}  // namespace mips
