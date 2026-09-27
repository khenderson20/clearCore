#pragma once

#include "mips/processor.h"
#include <QFont>
#include <QWidget>
#include <array>
#include <cstdint>
#include <vector>

class QShowEvent;
class QTableWidget;

namespace nsc::qt {

// Instruction × cycle pipeline diagram over the last kMaxCycles cycles.
//
// Columns are real simulator cycles (PipelineState::cycle), so a cycle the
// controller did not report stays blank instead of shifting later columns.
// Rows are dynamic instruction instances, not PCs: each instance is followed
// from stage to stage, so a short loop whose PC is in two stages at once gets
// one row per execution. The table is redrawn only while the widget is
// visible; a hidden widget keeps recording and redraws once when shown.
class PipelineTraceWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PipelineTraceWidget(QWidget* parent = nullptr);

    void updateCycle(const mips::PipelineState& state);
    void clear();
    void setDarkMode(bool dark);

protected:
    void showEvent(QShowEvent* ev) override;

private:
    static constexpr int kMaxCycles = 20;

    // One dynamic instance: the cycle in which it occupied each stage, 0 when
    // it was not seen there (a stalled or flushed slot is not recorded).
    struct InstrRow {
        uint32_t                pc  = 0;
        uint32_t                raw = 0;
        std::array<uint64_t, 5> cycle_in_stage{};

        [[nodiscard]] int      lastStage() const noexcept;  // -1 when empty
        [[nodiscard]] uint64_t lastCycle() const noexcept;
    };

    void rebuildTable();

    QTableWidget*         table_ = nullptr;
    std::vector<InstrRow> rows_{};
    uint64_t              last_cycle_ = 0;  // PipelineState::cycle of the last snapshot
    bool                  dirty_      = false;
    bool                  dark_mode_  = false;
    QFont                 label_font_;  // built once; constructing a QFont resolves the family
    QFont                 stage_font_;
};

}  // namespace nsc::qt
