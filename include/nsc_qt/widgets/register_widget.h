#pragma once

#include "mips/processor.h"
#include <QWidget>
#include <array>
#include <cstdint>

class QLabel;
class QGridLayout;
class QPaintEvent;
class QTimer;

namespace nsc::qt {

class RegisterWidget : public QWidget {
    Q_OBJECT

public:
    explicit RegisterWidget(QWidget* parent = nullptr);

    // Single per-cycle entry point: figures out which registers are read
    // this cycle, starts the wall-clock fade animation for any register
    // written this cycle, stores the fresh values, and refreshes every cell.
    void updateCycle(const mips::PipelineState& state, const std::array<uint32_t, 32>& vals);

    void setShowAliases(bool show);
    void setDarkMode(bool dark);

    // Reset all cells to zero and clear highlights.
    void clear();

    // Direct read-only access for tests.
    [[nodiscard]] uint32_t value(int idx) const noexcept { return values_[idx]; }

private:
    // ── Cell ─────────────────────────────────────────────────────────────────
    // A single register cell. Paints its own rounded background/border in
    // paintEvent() instead of going through QWidget::setStyleSheet(), which
    // reparses a full QSS string from scratch on every call.
    class Cell : public QWidget {
    public:
        explicit Cell(QWidget* parent = nullptr);

        void setNameText(const QString& text, const QColor& color);
        void setValueText(const QString& text, const QColor& color);
        void setBackground(const QColor& bg, const QColor& border);

    protected:
        void paintEvent(QPaintEvent* ev) override;

    private:
        QLabel* name_   = nullptr;
        QLabel* value_  = nullptr;
        QColor  bg_     = Qt::white;
        QColor  border_ = Qt::gray;
    };

    // "Just written" highlight duration, in wall-clock milliseconds --
    // deliberately NOT tied to simulator cycle count. The previous
    // implementation decayed the highlight by 1 unit per simulator step,
    // which meant it was invisible at high execution speed (multiple steps
    // per event-loop pass) and lasted seconds at low speed. A fixed
    // real-time duration keeps it meaningful regardless of run speed
    // (audit Warning #5).
    static constexpr int kFadeDurationMs = 250;

    struct CellState {
        Cell*  widget        = nullptr;
        qint64 fade_start_ms = -1;  // -1 = not fading
    };

    void buildGrid();
    void updateCell(int idx);
    void startFade(int idx);

    std::array<CellState, 32> cells_{};
    std::array<uint32_t, 32>  values_{};
    QGridLayout*              grid_         = nullptr;
    bool                      show_aliases_ = true;
    bool                      dark_mode_    = false;

    // Registers read by the current instruction in ID stage (highlight cyan).
    uint8_t read_rs_ = 0xFF;
    uint8_t read_rt_ = 0xFF;

    // Ticks while at least one cell is fading; stopped otherwise so the
    // widget is fully idle between register writes.
    QTimer* fade_timer_ = nullptr;
};

}  // namespace nsc::qt