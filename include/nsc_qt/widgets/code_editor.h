#pragma once

#include <QPlainTextEdit>

class QPaintEvent;
class QResizeEvent;

namespace nsc::qt {

class LineNumberArea;

// A QPlainTextEdit with a line-number gutter and current-line highlight --
// the standard Qt pattern (Editor::LineNumberArea from Qt's own examples),
// adapted to this app's dark/light theme. Drop-in replacement for
// QPlainTextEdit: toPlainText(), setPlainText(), setFont(), and
// setPlaceholderText() all still work unchanged.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int  lineNumberAreaWidth() const;

    void setDarkMode(bool dark);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

private:
    QWidget* line_number_area_ = nullptr;
    bool     dark_mode_        = false;
};

// The gutter widget itself. All painting is delegated back to CodeEditor,
// which is the only class that needs to know about text-block geometry.
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), code_editor_(editor) {}

    QSize sizeHint() const override { return QSize(code_editor_->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { code_editor_->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor* code_editor_;
};

}  // namespace nsc::qt