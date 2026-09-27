#include "nsc_qt/widgets/memory_widget.h"
#include "nsc_qt/ui_scale.h"

#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <QHexView/model/buffer/qhexbuffer.h>
#include <QHexView/model/qhexcursor.h>
#include <QHexView/model/qhexdocument.h>
#include <QHexView/qhexview.h>

#include <algorithm>
#include <cstring>

namespace nsc::qt {

// ─── MemorySnapshotBuffer ────────────────────────────────────────────────────
// The address space as the hex view sees it: a fixed-size copy the widget
// patches in place. QMemoryBuffer offers no such path — QHexDocument::setData()
// copies everything and resets the view (moving the cursor back to 0x0), and
// replace() records every write on an undo stack that grows without bound.
class MemorySnapshotBuffer final : public QHexBuffer {
public:
    explicit MemorySnapshotBuffer(QObject* parent = nullptr) : QHexBuffer(parent) {}

    [[nodiscard]] QByteArray& bytes() noexcept { return bytes_; }

    uchar at(qint64 idx) override {
        return static_cast<uchar>(bytes_.at(static_cast<qsizetype>(idx)));
    }
    [[nodiscard]] qint64 length() const override { return bytes_.size(); }
    // The view is read-only (QHexView::setReadOnly), so no edit ever arrives.
    void       insert(qint64 /*offset*/, const QByteArray& /*data*/) override {}
    void       remove(qint64 /*offset*/, int /*length*/) override {}
    QByteArray read(qint64 offset, int length) override {
        return bytes_.mid(static_cast<qsizetype>(offset), length);
    }
    bool read(QIODevice* device) override {
        bytes_ = device->readAll();
        return true;
    }
    void   write(QIODevice* device) override { device->write(bytes_); }
    qint64 indexOf(const QByteArray& ba, qint64 from) override {
        return bytes_.indexOf(ba, static_cast<qsizetype>(from));
    }
    qint64 lastIndexOf(const QByteArray& ba, qint64 from) override {
        return bytes_.lastIndexOf(ba, static_cast<qsizetype>(from));
    }

private:
    QByteArray bytes_;
};

// ─── MemoryWidget ────────────────────────────────────────────────────────────

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

void MemoryWidget::updateDisplay(const isa::Memory& mem) {
    const auto raw  = mem.raw();
    const auto size = static_cast<qsizetype>(raw.size());
    const auto now  = reinterpret_cast<const char*>(raw.data());

    const bool had_highlights = !changed_.empty();
    changed_.clear();

    if (snapshot_ == nullptr || snapshot_->bytes().size() != size) {
        // First refresh, or a different address space: take the whole image
        // once. Nothing is highlighted, since there is no earlier state.
        auto* buffer    = new MemorySnapshotBuffer;
        buffer->bytes() = QByteArray(now, size);
        auto* old_doc   = doc_;
        doc_            = QHexDocument::fromBuffer(buffer, this);  // takes the buffer
        snapshot_       = buffer;
        hex_view_->setDocument(doc_);
        if (old_doc != nullptr) old_doc->deleteLater();
        status_lbl_->setText(tr("%1 KiB RAM").arg(size / 1024));
        return;
    }

    // Compare a page at a time (memcmp is vectorised) and walk bytes only in a
    // page that differs; copy each changed run and record it for highlighting.
    constexpr qsizetype kPage   = 4096;
    char*               shown   = snapshot_->bytes().data();
    bool                changed = false;
    for (qsizetype page = 0; page < size; page += kPage) {
        const qsizetype end = std::min(page + kPage, size);
        if (std::memcmp(now + page, shown + page, static_cast<std::size_t>(end - page)) == 0)
            continue;
        for (qsizetype i = page; i < end;) {
            if (now[i] == shown[i]) {
                ++i;
                continue;
            }
            const qsizetype start = i;
            while (i < end && now[i] != shown[i])
                ++i;
            std::memcpy(shown + start, now + start, static_cast<std::size_t>(i - start));
            changed = true;
            if (!changed_.empty() && changed_.back().first + changed_.back().second == start)
                changed_.back().second += i - start;  // continues across a page boundary
            else if (changed_.size() < kMaxHighlightRuns)
                changed_.emplace_back(start, i - start);
        }
    }

    if (had_highlights || changed) applyHighlights();
    if (changed) hex_view_->viewport()->update();
}

QColor MemoryWidget::highlightColor() const {
    return dark_mode_ ? QColor(0x7A, 0x6E, 0x1F) : QColor(0xFF, 0xF9, 0xC4);
}

void MemoryWidget::applyHighlights() {
    hex_view_->clearMetadata();
    const QColor bg = highlightColor();
    for (const auto& [offset, length] : changed_)
        hex_view_->setBackgroundSize(offset, length, bg);
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
    applyHighlights();  // recolour the current highlights; no memory access needed
}

}  // namespace nsc::qt
