#include "nsc_qt/widgets/memory_widget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace nsc::qt {

MemoryWidget::MemoryWidget(QWidget* parent) : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);

    // ── Navigation bar ──────────────────────────────────────────────────────
    auto* nav = new QHBoxLayout;
    nav->addWidget(new QLabel("Base address:", this));

    addr_spin_ = new QSpinBox(this);
    addr_spin_->setRange(0, 0x7FFFFFFF);
    addr_spin_->setValue(static_cast<int>(DEF_BASE));
    addr_spin_->setDisplayIntegerBase(16);
    addr_spin_->setPrefix("0x");
    addr_spin_->setSingleStep(256);
    nav->addWidget(addr_spin_);

    status_lbl_ = new QLabel(this);
    nav->addWidget(status_lbl_);
    nav->addStretch();
    vl->addLayout(nav);

    buildTable();
    vl->addWidget(table_);

    connect(addr_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MemoryWidget::onAddressChanged);
}

void MemoryWidget::buildTable()
{
    // Columns: Address | B0..B15 | ASCII
    const int total_cols = 1 + COLS_HEX + 1;
    table_ = new QTableWidget(ROWS, total_cols, this);

    QStringList headers;
    headers << "Address";
    for (int i = 0; i < COLS_HEX; ++i)
        headers << QString("+%1").arg(i, 2, 16, QChar('0')).toUpper();
    headers << "ASCII";
    table_->setHorizontalHeaderLabels(headers);

    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setDefaultSectionSize(30);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(total_cols - 1, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->setFont(QFont("monospace", 9));
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(true);

    // Pre-populate with empty items
    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < total_cols; ++col) {
            auto* item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignCenter);
            table_->setItem(row, col, item);
        }
    }
}

void MemoryWidget::updateDisplay(const mips::Memory& mem)
{
    last_mem_ = &mem;
    refreshRows(mem);
}

void MemoryWidget::markWritten(uint32_t addr)
{
    written_addrs_.insert(addr);
}

void MemoryWidget::refreshRows(const mips::Memory& mem)
{
    const uint32_t base = base_addr_;
    for (int row = 0; row < ROWS; ++row) {
        const uint32_t row_addr = base + static_cast<uint32_t>(row * COLS_HEX);

        // Address column
        table_->item(row, 0)->setText(
            QString("0x%1").arg(row_addr, 8, 16, QChar('0')).toUpper());

        QString ascii_str;
        for (int col = 0; col < COLS_HEX; ++col) {
            const uint32_t byte_addr = row_addr + static_cast<uint32_t>(col);
            auto byte_val = mem.read_byte(byte_addr);
            auto* item = table_->item(row, 1 + col);

            if (byte_val) {
                item->setText(QString("%1").arg(*byte_val, 2, 16, QChar('0')).toUpper());
                ascii_str += (*byte_val >= 0x20 && *byte_val < 0x7F)
                             ? QChar(static_cast<char>(*byte_val)) : QChar('.');
                // Highlight recently written bytes
                if (written_addrs_.count(byte_addr))
                    item->setBackground(QColor("#FFF9C4"));
                else
                    item->setBackground(dark_mode_ ? QColor(0x2A,0x2A,0x2A) : Qt::white);
            } else {
                item->setText("--");
                item->setBackground(dark_mode_ ? QColor(0x1A,0x1A,0x1A) : QColor(0xEE,0xEE,0xEE));
                ascii_str += ' ';
            }
        }
        table_->item(row, 1 + COLS_HEX)->setText(ascii_str);
    }
    written_addrs_.clear();
}

void MemoryWidget::onAddressChanged(int value)
{
    base_addr_ = static_cast<uint32_t>(value) & ~0xFu; // align to 16
    if (last_mem_) refreshRows(*last_mem_);
}

void MemoryWidget::setDarkMode(bool dark)
{
    dark_mode_ = dark;
    if (last_mem_) refreshRows(*last_mem_);
}

} // namespace nsc::qt
