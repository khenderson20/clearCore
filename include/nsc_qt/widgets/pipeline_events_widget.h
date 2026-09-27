#pragma once

// ── pipeline_events_widget.h ────────────────────────────────────────────────
// Scrolling, colour-coded log of pipeline events: every forward, stall, and
// flush as it happens, plus program-level milestones (load, reset, halt,
// fault, breakpoints). Complements the schematic's in-place highlights with
// a persistent history a student can read back after a run — "what happened
// while I wasn't looking" — and pairs with the Statistics counters by
// showing the individual events behind each total.

#include "mips/processor.h"
#include <QColor>
#include <QHash>
#include <QString>
#include <QWidget>
#include <cstdint>

class QListWidget;
class QLabel;

namespace nsc::qt {

class PipelineEventsWidget final : public QWidget {
    Q_OBJECT

public:
    // Event categories, colour-coded to match the schematic's wire palette
    // (forwarding orange/purple, stall amber, flush red).
    enum class Kind : uint8_t { FwdExMem, FwdMemWb, Stall, Flush, Info, Success, Error };

    explicit PipelineEventsWidget(QWidget* parent = nullptr);

    // Derives and logs the hazard events visible in `state` (forwards,
    // load-use stall, branch flush). Consecutive-cycle repeats of the same
    // event are suppressed so a multi-cycle stall reads as one entry. The log
    // is history, so it records whether or not the panel is visible; a
    // suppressed repeat costs one hash lookup and no string work.
    void updateCycle(const mips::PipelineState& state);

    // Appends one entry. `cycle` prefixes the message; pass the current
    // cycle count for program-level events.
    void logEvent(Kind kind, uint64_t cycle, const QString& text);

    void clear();
    void setDarkMode(bool dark);

    // Number of entries currently in the log (for tests).
    [[nodiscard]] int eventCount() const;

private:
    [[nodiscard]] QColor kindColor(Kind kind) const;

    static constexpr int kMaxEntries = 500;

    QListWidget* list_      = nullptr;
    QLabel*      count_lbl_ = nullptr;
    bool         dark_mode_ = false;

    // Rising-edge suppression: (event index << 32 | instruction word) → the
    // cycle it was last seen in.
    QHash<quint64, quint64> recent_;
};

}  // namespace nsc::qt
