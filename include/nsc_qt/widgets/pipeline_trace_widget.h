#pragma once

#include "mips/processor.h"
#include <QWidget>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

class QTableWidget;

namespace nsc::qt {

class PipelineTraceWidget : public QWidget {
    Q_OBJECT

public:
    explicit PipelineTraceWidget(QWidget* parent = nullptr);

    void updateCycle(const mips::PipelineState& state);
    void clear();
    void setDarkMode(bool dark);

private:
    static constexpr int MAX_CYCLES = 20;

    void rebuildTable();

    // One row per instruction currently visible in the trace window (keyed by
    // PC). Each entry in `stages` is (absolute cycle number, stage name);
    // entries older than the visible window are pruned every updateCycle()
    // call, and the row itself is dropped once it has no visible entries
    // left. Storing the absolute cycle rather than a column offset means
    // entries stay correctly aligned with the header even as cycle_base_
    // advances.
    struct InstrRow {
        uint32_t                                     pc  = 0;
        uint32_t                                     raw = 0;
        std::deque<std::pair<uint64_t, std::string>> stages;
    };

    QTableWidget*         table_ = nullptr;
    std::vector<InstrRow> rows_{};
    uint64_t              cycle_base_    = 0;  // column 0 maps to this cycle + 1
    uint64_t              current_cycle_ = 0;
    bool                  dark_mode_     = false;
};

}  // namespace nsc::qt