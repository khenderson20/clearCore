#pragma once

#include "mips/memory.h"
#include <QWidget>
#include <cstdint>
#include <unordered_set>

class QHexView;
class QHexDocument;
class QSpinBox;
class QLabel;

namespace nsc::qt {

// Hex view of the emulated RAM, built on QHexView (MIT). The whole address
// space is shown in one scrollable view; bytes stored by the last step are
// highlighted until the next refresh.
class MemoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit MemoryWidget(QWidget* parent = nullptr);

    // Refresh display from `mem`.
    void updateDisplay(const mips::Memory& mem);

    // Highlight `addr` as recently written.
    void markWritten(uint32_t addr);

    void setDarkMode(bool dark);

private slots:
    void onAddressChanged(int value);

private:
    void refreshView(const mips::Memory& mem);

    QHexView*     hex_view_   = nullptr;
    QHexDocument* doc_        = nullptr;
    QSpinBox*     addr_spin_  = nullptr;
    QLabel*       status_lbl_ = nullptr;

    bool dark_mode_ = false;

    // Set of byte addresses written in the last step.
    std::unordered_set<uint32_t> written_addrs_{};

    const mips::Memory* last_mem_ = nullptr;
};

}  // namespace nsc::qt
