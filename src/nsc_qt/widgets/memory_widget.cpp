#include "nsc_qt/widgets/memory_widget.h"
#include "nsc_qt/ui_scale.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <QHexView/model/buffer/qmemorybuffer.h>
#include <QHexView/model/qhexcursor.h>
#include <QHexView/model/qhexdocument.h>
#include <QHexView/qhexview.h>

namespace nsc::qt {

MemoryWidget::MemoryWidget(QWidget* parent) : QWidget(parent) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);

    // ── Navigation bar ──────────────────────────────────────────────────────
    auto* nav = new QHBoxLayout;

    auto* jump_start_btn = new QPushButton(tr("Jump to 0x0"), this);
    jump_start_btn->setToolTip(tr("Jump to the start of memory"));
    jump_start_btn->setFont(scale::monoFont(scale::kFontSizeBody));
    connect(jump_start_btn, &QPushButton::clicked, this, [this] { addr_spin_->setValue(0); });
    nav->addWidget(jump_start_btn);

    nav->addStretch();

    auto* nav_lbl = new QLabel(tr("Go to address:"), this);
    nav_lbl->setFont(scale::monoFont(scale::kFontSizeBody));
    nav->addWidget(nav_lbl);

    addr_spin_ = new QSpinBox(this);
    addr_spin_->setFont(scale::monoFont(scale::kFontSizeBody));
    addr_spin_->setRange(0, 0x7FFFFFFF);
    addr_spin_->setValue(0);
    addr_spin_->setDisplayIntegerBase(16);
    addr_spin_->setPrefix("0x");
    addr_spin_->setSingleStep(16);
    nav->addWidget(addr_spin_);

    status_lbl_ = new QLabel(this);
    status_lbl_->setFont(scale::monoFont(scale::kFontSizeBody));
    nav->addWidget(status_lbl_);
    vl->addLayout(nav);

    hex_view_ = new QHexView(this);
    hex_view_->setReadOnly(true);
    hex_view_->setFont(scale::monoFont(scale::kFontSizeDense));

    // Label each section of the hex dump so new users immediately understand
    // what they are looking at. "Offset" = row start address, hex column keeps
    // its default byte-position numbers (00–0F), "ASCII" = text view.
    {
        auto opts          = hex_view_->options();
        opts.address_label = tr("Offset");
        opts.ascii_label   = QStringLiteral("ASCII");
        opts.flags |= QHexFlags::StyledHeader | QHexFlags::Separators;
        hex_view_->setOptions(opts);
    }

    vl->addWidget(hex_view_);

    connect(addr_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            &MemoryWidget::onAddressChanged);
}

void MemoryWidget::updateDisplay(const mips::Memory& mem) {
    last_mem_ = &mem;
    refreshView(mem);
}

void MemoryWidget::markWritten(uint32_t addr) {
    written_addrs_.insert(addr);
}

void MemoryWidget::refreshView(const mips::Memory& mem) {
    const auto       raw = mem.raw();
    const QByteArray ba(reinterpret_cast<const char*>(raw.data()),
                        static_cast<qsizetype>(raw.size()));

    if (doc_ == nullptr) {
        doc_ = QHexDocument::fromMemory<QMemoryBuffer>(ba, this);
        hex_view_->setDocument(doc_);
        status_lbl_->setText(tr("%1 KiB RAM").arg(raw.size() / 1024));
    } else {
        doc_->setData(ba);
    }

    // Repaint the "written last step" highlights.
    hex_view_->clearMetadata();
    const QColor written_bg = dark_mode_ ? QColor(0x7A, 0x6E, 0x1F) : QColor(0xFF, 0xF9, 0xC4);
    for (const uint32_t addr : written_addrs_)
        hex_view_->setBackgroundSize(addr, 1, written_bg);
    written_addrs_.clear();
}

void MemoryWidget::onAddressChanged(int value) {
    hex_view_->hexCursor()->move(static_cast<qint64>(value));
}

void MemoryWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    // Sync the QHexView header text colour to match the app's accent colour.
    auto         opts                    = hex_view_->options();
    const QColor hdr_fg                  = dark ? QColor("#9CDCFE") : QColor("#0078D4");
    opts.header_format.foreground        = hdr_fg;
    opts.addressheader_format.foreground = hdr_fg;
    opts.hexheader_format.foreground     = hdr_fg;
    opts.asciiheader_format.foreground   = hdr_fg;
    hex_view_->setOptions(opts);
    if (last_mem_) refreshView(*last_mem_);
}

}  // namespace nsc::qt
