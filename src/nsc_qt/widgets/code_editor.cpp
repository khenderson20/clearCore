#include "nsc_qt/widgets/code_editor.h"

#include <QPaintEvent>
#include <QPainter>
#include <QTextBlock>

namespace nsc::qt {

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    line_number_area_ = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits    = 1;
    int max_block = qMax(1, blockCount());
    while (max_block >= 10) {
        max_block /= 10;
        ++digits;
    }
    // 6px padding on each side of the digits.
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth(int /*newBlockCount*/) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0)
        line_number_area_->scroll(0, dy);
    else
        line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());

    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    line_number_area_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extra_selections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        const QColor line_color = dark_mode_ ? QColor(0x2A, 0x2A, 0x2A) : QColor(0xEC, 0xEC, 0xEC);
        selection.format.setBackground(line_color);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extra_selections.append(selection);
    }

    setExtraSelections(extra_selections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter     painter(line_number_area_);
    const QColor gutter_bg  = dark_mode_ ? QColor(0x25, 0x25, 0x26) : QColor(0xF3, 0xF3, 0xF3);
    const QColor number_dim = dark_mode_ ? QColor(0x6E, 0x6E, 0x6E) : QColor(0x9A, 0x9A, 0x9A);
    const QColor number_cur = dark_mode_ ? QColor(0xCC, 0xCC, 0xCC) : QColor(0x33, 0x33, 0x33);

    painter.fillRect(event->rect(), gutter_bg);

    QTextBlock block        = firstVisibleBlock();
    int        block_number = block.blockNumber();
    int top    = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());

    const bool has_focus     = hasFocus();
    const int  current_block = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(block_number + 1);
            painter.setPen(has_focus && block_number == current_block ? number_cur : number_dim);
            painter.drawText(0, top, line_number_area_->width() - 8, fontMetrics().height(),
                             Qt::AlignRight, number);
        }
        block  = block.next();
        top    = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++block_number;
    }
}

void CodeEditor::setDarkMode(bool dark) {
    dark_mode_ = dark;
    highlightCurrentLine();
    line_number_area_->update();
}

}  // namespace nsc::qt