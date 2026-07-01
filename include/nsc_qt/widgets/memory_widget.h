#pragma once

#include "mips/memory.h"
#include <QWidget>
#include <cstdint>
#include <unordered_set>

class QTableWidget;
class QSpinBox;
class QLabel;

namespace nsc::qt {

    class MemoryWidget : public QWidget {
        Q_OBJECT

    public:
        explicit MemoryWidget(QWidget* parent = nullptr);

        // Refresh display from `mem` starting at `base_addr_`.
        void updateDisplay(const mips::Memory& mem);

        // Highlight `addr` as recently written.
        void markWritten(uint32_t addr);

        void setDarkMode(bool dark);

    private slots:
        void onAddressChanged(int value);

    private:
        static constexpr int ROWS       = 16;
        static constexpr int COLS_HEX   = 16;  // 16 bytes per row
        static constexpr uint32_t DEF_BASE = 0x00000000;

        void buildTable();
        void refreshRows(const mips::Memory& mem);

        QTableWidget* table_      = nullptr;
        QSpinBox*     addr_spin_  = nullptr;
        QLabel*       status_lbl_ = nullptr;

        uint32_t base_addr_  = DEF_BASE;
        bool     dark_mode_  = false;

        // Set of byte addresses written in the last step.
        std::unordered_set<uint32_t> written_addrs_{};

        const mips::Memory* last_mem_ = nullptr;
    };

} // namespace nsc::qt