#pragma once

#include "isa/memory.h"
#include <QColor>
#include <QWidget>
#include <cstdint>
#include <utility>
#include <vector>

class QHexDocument;
class QHexView;
class QSpinBox;
class QLabel;

namespace nsc::qt {

class MemorySnapshotBuffer;

// Hex view of the emulated RAM, built on QHexView (MIT). The whole address
// space is shown in one scrollable view. Bytes that changed since the previous
// refresh are highlighted until the next one — with a refresh every cycle,
// that is exactly what the last step stored.
//
// The widget keeps its own copy of memory and patches only the bytes that
// changed, so a refresh never re-uploads the whole address space and never
// holds a reference to the controller's memory between calls.
class MemoryWidget final : public QWidget {
    Q_OBJECT

public:
    explicit MemoryWidget(QWidget* parent = nullptr);

    // Show `mem`. Only the bytes that differ from the previous refresh are
    // copied, and those are the ones highlighted.
    void updateDisplay(const isa::Memory& mem);

    void setDarkMode(bool dark);

    // For tests: the highlighted (changed) byte ranges as {offset, length}.
    [[nodiscard]] const std::vector<std::pair<qint64, qint64>>& changedRanges() const noexcept {
        return changed_;
    }

private slots:
    void onAddressChanged(int value);

private:
    // Caps the per-refresh highlight work when a program rewrites a large
    // region between two refreshes (e.g. a memset loop at full speed).
    static constexpr std::size_t kMaxHighlightRuns = 256;

    void                 applyHighlights();
    [[nodiscard]] QColor highlightColor() const;

    QHexView*             hex_view_   = nullptr;
    QHexDocument*         doc_        = nullptr;
    MemorySnapshotBuffer* snapshot_   = nullptr;  // owned by doc_
    QSpinBox*             addr_spin_  = nullptr;
    QLabel*               status_lbl_ = nullptr;

    bool dark_mode_ = false;

    // Byte ranges that changed in the last refresh: {offset, length}; at most
    // kMaxHighlightRuns of them are recorded.
    std::vector<std::pair<qint64, qint64>> changed_;
};

}  // namespace nsc::qt
