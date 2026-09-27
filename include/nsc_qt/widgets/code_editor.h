#pragma once

#include <QPlainTextEdit>

class QPaintEvent;
class QResizeEvent;

#ifdef HAVE_KSYNTAXHIGHLIGHTING
namespace KSyntaxHighlighting {
class SyntaxHighlighter;
}
#endif

namespace nsc::qt {

class LineNumberArea;

// A QPlainTextEdit with a line-number gutter and current-line highlight --
// the standard Qt pattern (Editor::LineNumberArea from Qt's own examples),
// adapted to this app's dark/light theme. Drop-in replacement for
// QPlainTextEdit: toPlainText(), setPlainText(), setFont(), and
// setPlaceholderText() all still work unchanged.
class CodeEditor final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void              lineNumberAreaPaintEvent(QPaintEvent* event);
    [[nodiscard]] int lineNumberAreaWidth() const;

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

#ifdef HAVE_KSYNTAXHIGHLIGHTING
    // MIPS assembly highlighting via KSyntaxHighlighting (optional dependency;
    // without it the editor is plain text). Owned by the document.
    KSyntaxHighlighting::SyntaxHighlighter* highlighter_ = nullptr;
#endif
};

// The gutter widget itself. All painting is delegated back to CodeEditor,
// which is the only class that needs to know about text-block geometry.
class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), code_editor_(editor) {}

    [[nodiscard]] QSize sizeHint() const override {
        return {code_editor_->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent* event) override { code_editor_->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor* code_editor_;
};

}  // namespace nsc::qt