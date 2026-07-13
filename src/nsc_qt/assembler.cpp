#include "nsc_qt/assembler.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nsc::qt {

namespace {

// ── Register table ────────────────────────────────────────────────────────────

const std::unordered_map<std::string, uint8_t> kRegMap = {
    {"zero", 0}, {"at", 1},  {"v0", 2},  {"v1", 3},  {"a0", 4},  {"a1", 5},  {"a2", 6},  {"a3", 7},
    {"t0", 8},   {"t1", 9},  {"t2", 10}, {"t3", 11}, {"t4", 12}, {"t5", 13}, {"t6", 14}, {"t7", 15},
    {"s0", 16},  {"s1", 17}, {"s2", 18}, {"s3", 19}, {"s4", 20}, {"s5", 21}, {"s6", 22}, {"s7", 23},
    {"t8", 24},  {"t9", 25}, {"k0", 26}, {"k1", 27}, {"gp", 28}, {"sp", 29}, {"fp", 30}, {"ra", 31},
};

// Parses "$name" or "$N"; returns 0xFF on error.
static std::optional<uint8_t> parse_reg(std::string_view tok) {
    if (tok.empty() || tok[0] != '$') return std::nullopt;
    std::string name(tok.substr(1));
    // try numeric
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        int n          = 0;
        auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), n);
        if (ec == std::errc{} && ptr == name.data() + name.size() && n >= 0 && n < 32)
            return static_cast<uint8_t>(n);
        return std::nullopt;
    }
    auto it = kRegMap.find(name);
    if (it == kRegMap.end()) return std::nullopt;
    return it->second;
}

// Parses a decimal or 0x hex integer; returns nullopt on error.
static std::optional<int32_t> parse_imm(std::string_view tok) {
    if (tok.empty()) return std::nullopt;
    bool neg = (tok[0] == '-');
    if (neg) tok.remove_prefix(1);
    int32_t val = 0;
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        tok.remove_prefix(2);
        uint32_t u     = 0;
        auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), u, 16);
        if (ec != std::errc{} || ptr != tok.data() + tok.size()) return std::nullopt;
        val = static_cast<int32_t>(u);
    } else {
        auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), val);
        if (ec != std::errc{} || ptr != tok.data() + tok.size()) return std::nullopt;
    }
    return neg ? -val : val;
}

// Parses "imm($rs)" form; returns {imm, rs} or nullopt.
static std::optional<std::pair<int32_t, uint8_t>> parse_mem_operand(std::string_view tok) {
    auto lp = tok.find('(');
    auto rp = tok.find(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp < lp)
        return std::nullopt;
    auto imm_sv = tok.substr(0, lp);
    auto reg_sv = tok.substr(lp + 1, rp - lp - 1);
    auto imm    = parse_imm(imm_sv);
    auto reg    = parse_reg(reg_sv);
    if (!imm || !reg) return std::nullopt;
    return std::pair{*imm, *reg};
}

// ── Encoding helpers ──────────────────────────────────────────────────────────

static uint32_t enc_r(uint8_t rs, uint8_t rt, uint8_t rd, uint8_t shamt, uint8_t funct) {
    return (static_cast<uint32_t>(rs & 0x1F) << 21) | (static_cast<uint32_t>(rt & 0x1F) << 16) |
           (static_cast<uint32_t>(rd & 0x1F) << 11) | (static_cast<uint32_t>(shamt & 0x1F) << 6) |
           static_cast<uint32_t>(funct & 0x3F);
}

static uint32_t enc_i(uint8_t op, uint8_t rs, uint8_t rt, uint16_t imm) {
    return (static_cast<uint32_t>(op & 0x3F) << 26) | (static_cast<uint32_t>(rs & 0x1F) << 21) |
           (static_cast<uint32_t>(rt & 0x1F) << 16) | static_cast<uint32_t>(imm);
}

static uint32_t enc_j(uint8_t op, uint32_t target) {
    return (static_cast<uint32_t>(op & 0x3F) << 26) | (target & 0x03FFFFFF);
}

// ── Instruction encoding tables ───────────────────────────────────────────────
// Defined once at file scope so they are constructed once, not on every pass-2
// loop iteration (each unordered_map build is O(n) hash + allocation).

static const std::unordered_map<std::string, uint8_t> kR3Map = {
    {"add", 0x20}, {"addu", 0x21}, {"sub", 0x22}, {"subu", 0x23}, {"and", 0x24},
    {"or", 0x25},  {"xor", 0x26},  {"nor", 0x27}, {"slt", 0x2A},  {"sltu", 0x2B},
};
static const std::unordered_map<std::string, uint8_t> kShiftMap = {
    {"sll", 0x00},
    {"srl", 0x02},
    {"sra", 0x03},
};
static const std::unordered_map<std::string, uint8_t> kVShiftMap = {
    {"sllv", 0x04},
    {"srlv", 0x06},
};
static const std::unordered_map<std::string, uint8_t> kIArithMap = {
    {"addi", 0x08}, {"addiu", 0x09}, {"slti", 0x0A}, {"sltiu", 0x0B},
    {"andi", 0x0C}, {"ori", 0x0D},   {"xori", 0x0E},
};
static const std::unordered_map<std::string, uint8_t> kMemMap = {
    {"lw", 0x23},
    {"lbu", 0x24},
    {"lhu", 0x25},
    {"sw", 0x2B},
};
static const std::unordered_map<std::string, uint8_t> kBranchMap = {
    {"beq", 0x04},
    {"bne", 0x05},
};
static const std::unordered_map<std::string, uint8_t> kJumpMap = {
    {"j", 0x02},
    {"jal", 0x03},
};

// ── Tokeniser ─────────────────────────────────────────────────────────────────

// Strips leading/trailing whitespace in place.
static void trim(std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        s.clear();
        return;
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    s               = s.substr(first, last - first + 1);
}

// Splits `line` on whitespace and commas into tokens.
static std::vector<std::string> tokenise(const std::string& line) {
    std::vector<std::string> toks;
    std::string              cur;
    for (char c : line) {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

// ── Assembler ─────────────────────────────────────────────────────────────────

struct Line {
    int                      lineno = 0;
    std::string              mnemonic;
    std::vector<std::string> operands;
};

static std::string make_error(int lineno, const std::string& msg) {
    return "line " + std::to_string(lineno) + ": " + msg;
}

}  // anonymous namespace

AssemblerResult assemble(const std::string& source) {
    // ── Pass 1: collect labels and strip comments ──────────────────────────────
    std::unordered_map<std::string, uint32_t> labels;  // label → word index
    std::vector<Line>                         lines;

    std::istringstream ss(source);
    std::string        raw;
    int                lineno = 0;

    while (std::getline(ss, raw)) {
        ++lineno;
        // Strip comment
        const auto hash = raw.find('#');
        if (hash != std::string::npos) raw = raw.substr(0, hash);
        trim(raw);
        if (raw.empty()) continue;

        // Check for label
        const auto colon = raw.find(':');
        if (colon != std::string::npos) {
            std::string lbl = raw.substr(0, colon);
            trim(lbl);
            if (!lbl.empty()) labels[lbl] = static_cast<uint32_t>(lines.size());
            raw = raw.substr(colon + 1);
            trim(raw);
            if (raw.empty()) continue;
        }

        auto toks = tokenise(raw);
        if (toks.empty()) continue;

        Line ln;
        ln.lineno   = lineno;
        ln.mnemonic = toks[0];
        // Convert mnemonic to lower-case
        std::transform(ln.mnemonic.begin(), ln.mnemonic.end(), ln.mnemonic.begin(), ::tolower);
        for (std::size_t i = 1; i < toks.size(); ++i)
            ln.operands.push_back(toks[i]);

        lines.push_back(std::move(ln));
    }

    // ── Pass 2: encode ─────────────────────────────────────────────────────────
    AssemblerResult result;
    result.words.reserve(lines.size());

    for (std::size_t idx = 0; idx < lines.size(); ++idx) {
        const auto& ln    = lines[idx];
        const auto& mn    = ln.mnemonic;
        const auto& ops   = ln.operands;
        const int   ln_no = ln.lineno;

        // REMOVED: const uint32_t word_addr = static_cast<uint32_t>(idx * 4);

        auto err = [&](const std::string& msg) -> AssemblerResult {
            AssemblerResult e;
            e.error = make_error(ln_no, msg);
            return e;
        };

        auto need = [&](std::size_t n) -> bool { return ops.size() == n; };

        // ── nop ──
        if (mn == "nop") {
            result.words.push_back(0);
            continue;
        }

        // ── R-type: add addu sub subu and or xor nor slt sltu ─────────────────
        if (kR3Map.count(mn)) {
            if (!need(3)) return err("expected $rd, $rs, $rt");
            auto rd = parse_reg(ops[0]);
            auto rs = parse_reg(ops[1]);
            auto rt = parse_reg(ops[2]);
            if (!rd || !rs || !rt) return err("bad register");
            result.words.push_back(enc_r(*rs, *rt, *rd, 0, kR3Map.at(mn)));
            continue;
        }

        // ── R-type shifts: sll srl sra ($rd, $rt, shamt) ──────────────────────
        if (kShiftMap.count(mn)) {
            if (!need(3)) return err("expected $rd, $rt, shamt");
            auto rd  = parse_reg(ops[0]);
            auto rt  = parse_reg(ops[1]);
            auto imm = parse_imm(ops[2]);
            if (!rd || !rt || !imm) return err("bad operands");
            if (*imm < 0 || *imm > 31) return err("shift amount out of range");
            result.words.push_back(
                enc_r(0, *rt, *rd, static_cast<uint8_t>(*imm), kShiftMap.at(mn)));
            continue;
        }

        // ── R-type var shifts: sllv srlv ($rd, $rt, $rs) ──────────────────────
        if (kVShiftMap.count(mn)) {
            if (!need(3)) return err("expected $rd, $rt, $rs");
            auto rd = parse_reg(ops[0]);
            auto rt = parse_reg(ops[1]);
            auto rs = parse_reg(ops[2]);
            if (!rd || !rt || !rs) return err("bad register");
            result.words.push_back(enc_r(*rs, *rt, *rd, 0, kVShiftMap.at(mn)));
            continue;
        }

        // ── jr $rs ──
        if (mn == "jr") {
            if (!need(1)) return err("expected $rs");
            auto rs = parse_reg(ops[0]);
            if (!rs) return err("bad register");
            result.words.push_back(enc_r(*rs, 0, 0, 0, 0x08));
            continue;
        }

        // ── jalr $rd, $rs ──
        if (mn == "jalr") {
            if (!need(2)) return err("expected $rd, $rs");
            auto rd = parse_reg(ops[0]);
            auto rs = parse_reg(ops[1]);
            if (!rd || !rs) return err("bad register");
            result.words.push_back(enc_r(*rs, 0, *rd, 0, 0x09));
            continue;
        }

        // ── I-type arithmetic: addi addiu slti sltiu andi ori xori ──────────────
        if (kIArithMap.count(mn)) {
            if (!need(3)) return err("expected $rt, $rs, imm");
            auto rt  = parse_reg(ops[0]);
            auto rs  = parse_reg(ops[1]);
            auto imm = parse_imm(ops[2]);
            if (!rt || !rs || !imm) return err("bad operands");
            result.words.push_back(enc_i(kIArithMap.at(mn), *rs, *rt,
                                         static_cast<uint16_t>(static_cast<int16_t>(*imm))));
            continue;
        }

        // ── lui $rt, imm ──
        if (mn == "lui") {
            if (!need(2)) return err("expected $rt, imm");
            auto rt  = parse_reg(ops[0]);
            auto imm = parse_imm(ops[1]);
            if (!rt || !imm) return err("bad operands");
            result.words.push_back(
                enc_i(0x0F, 0, *rt, static_cast<uint16_t>(static_cast<int16_t>(*imm))));
            continue;
        }

        // ── Memory: lw lbu lhu sw ($rt, imm($rs)) ─────────────────────────────
        if (kMemMap.count(mn)) {
            if (!need(2)) return err("expected $rt, imm($rs)");
            auto rt = parse_reg(ops[0]);
            if (!rt) return err("bad register");
            auto mem_op = parse_mem_operand(ops[1]);
            if (!mem_op) return err("expected imm($rs)");
            auto [imm, rs] = *mem_op;
            result.words.push_back(
                enc_i(kMemMap.at(mn), rs, *rt, static_cast<uint16_t>(static_cast<int16_t>(imm))));
            continue;
        }

        // ── beq bne $rs, $rt, label_or_offset ─────────────────────────────────
        if (kBranchMap.count(mn)) {
            if (!need(3)) return err("expected $rs, $rt, label");
            auto rs = parse_reg(ops[0]);
            auto rt = parse_reg(ops[1]);
            if (!rs || !rt) return err("bad register");

            int32_t offset = 0;
            // Try numeric first, then label
            auto imm = parse_imm(ops[2]);
            if (imm) {
                offset = *imm;
            } else {
                auto it = labels.find(ops[2]);
                if (it == labels.end()) return err("undefined label '" + ops[2] + "'");
                // offset = (target_word_addr - (current_word_addr + 4)) / 4
                offset = static_cast<int32_t>(it->second) - static_cast<int32_t>(idx + 1);
            }
            result.words.push_back(enc_i(kBranchMap.at(mn), *rs, *rt,
                                         static_cast<uint16_t>(static_cast<int16_t>(offset))));
            continue;
        }

        // ── j jal target_or_label ─────────────────────────────────────────────
        if (kJumpMap.count(mn)) {
            if (!need(1)) return err("expected target");
            uint32_t target = 0;
            auto     imm    = parse_imm(ops[0]);
            if (imm) {
                target = static_cast<uint32_t>(*imm);
            } else {
                auto it = labels.find(ops[0]);
                if (it == labels.end()) return err("undefined label '" + ops[0] + "'");
                target = it->second;  // word index; IProcessor::load_program handles byte address
            }
            result.words.push_back(enc_j(kJumpMap.at(mn), target));
            continue;
        }

        return err("unknown instruction '" + mn + "'");
    }

    return result;
}

}  // namespace nsc::qt
