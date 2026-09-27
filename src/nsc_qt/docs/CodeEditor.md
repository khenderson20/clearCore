# CodeEditor

## 1. Class Overview

`CodeEditor` is a drop-in replacement for `QPlainTextEdit` that adds a line-number gutter and a current-line highlight — the standard editor affordances familiar from every code editor (Jakob's Law), used for the MIPS assembly source box on the Code Editor tab. It implements Qt's own canonical line-number-gutter pattern (the `Editor::LineNumberArea` example shipped with Qt), adapted to this application's dark/light theme rather than a fixed palette.

A developer reaches for this class anywhere a `QPlainTextEdit` would otherwise be used but line numbers are wanted — `toPlainText()`, `setPlainText()`, `setFont()`, and `setPlaceholderText()` all continue to work exactly as they do on the base class, so existing code that used `QPlainTextEdit` needs no changes beyond the type name.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::createCodeEditorTab()` as the Code Editor tab's text box.

Qt modules required:
- **Qt Widgets** — `QPlainTextEdit` (base class), `QWidget` (`LineNumberArea`'s base class).
- **Qt GUI** — `QPainter`, `QTextBlock`, `QTextFormat`, `QPaintEvent`, `QResizeEvent`.

No project-internal types are used directly in this file; `MainWindow` supplies fonts via `nsc_qt/ui_scale.h` after construction.

## 3. Class Hierarchy and Role

`CodeEditor` inherits [`QPlainTextEdit`](https://doc.qt.io/qt-6/qplaintextedit.html), a plain-text-optimized editor widget (lighter weight than `QTextEdit`, appropriate for source code). `QPlainTextEdit` derives from [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) → [`QObject`](https://doc.qt.io/qt-6/qobject.html). `Q_OBJECT` is required here for the three private slots connected to `QPlainTextEdit`'s own signals.

`LineNumberArea` (declared in the same header) inherits [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) directly. It has no `Q_OBJECT` macro — it is a thin painting surface with no signals or slots of its own, delegating all painting logic back to its owning `CodeEditor` via `lineNumberAreaPaintEvent()`.

## 4. Public Methods

#### `explicit CodeEditor(QWidget* parent = nullptr)`
Constructs the `line_number_area_` child widget, connects `QPlainTextEdit::blockCountChanged` to `updateLineNumberAreaWidth()`, `QPlainTextEdit::updateRequest` to `updateLineNumberArea()`, and `QPlainTextEdit::cursorPositionChanged` to `highlightCurrentLine()`, then calls both slots once to establish the initial gutter width and current-line highlight.

#### `void lineNumberAreaPaintEvent(QPaintEvent* event)`
Paints the gutter background and every visible line number, dimming all but the line the cursor is currently on (which is drawn in a brighter color only while the editor itself has focus). Called by `LineNumberArea::paintEvent()` — not intended to be called directly by other classes, despite being `public` (it needs to be reachable from `LineNumberArea`, which is a sibling class in the same header rather than a nested class).

#### `int lineNumberAreaWidth() const`
Computes the gutter's pixel width from the number of digits in the current block (line) count, plus fixed padding. Used both internally (`updateLineNumberAreaWidth()`, `resizeEvent()`) and by `LineNumberArea::sizeHint()`.

#### `void setDarkMode(bool dark)`
Switches the gutter and current-line-highlight colors and forces both to repaint immediately.

## 5. Protected Virtual Methods / Event Handlers

#### `void resizeEvent(QResizeEvent* event) override`
Base class: `QPlainTextEdit`. Calls the base implementation, then repositions `line_number_area_` to span the editor's left edge for its full height, at the width returned by `lineNumberAreaWidth()`.

## 6. Protected / Private Slots

#### `void updateLineNumberAreaWidth(int newBlockCount)` *(private slot)*
Connected to `blockCountChanged(int)`. Calls `setViewportMargins()` with the current `lineNumberAreaWidth()` as the left margin, making room for the gutter. The `newBlockCount` parameter is unused — the width is always recomputed from the editor's current state.

#### `void updateLineNumberArea(const QRect& rect, int dy)` *(private slot)*
Connected to `updateRequest(const QRect&, int)`. Scrolls the gutter to match if `dy` is non-zero (the editor scrolled vertically), otherwise repaints just the affected rectangle. If the updated region covers the whole viewport, also recomputes the gutter width in case the digit count changed.

#### `void highlightCurrentLine()` *(private slot)*
Connected to `cursorPositionChanged()`. Builds a single `QTextEdit::ExtraSelection` covering the full width of the cursor's current line (skipped entirely if the editor is read-only) and applies it via `setExtraSelections()`.

## 7. Ownership and Lifecycle

`CodeEditor` is constructed with `MainWindow`'s Code Editor tab widget as its `QObject` parent and is deleted automatically as part of the widget tree. `line_number_area_` is, in turn, constructed with the `CodeEditor` instance itself as its parent (`new LineNumberArea(this)`), so it requires no separate cleanup.

## 8. Thread Safety

**GUI-thread only**, as a `QPlainTextEdit`/`QWidget` subclass.

## 9. Inter-Class Interactions

- **Receives from `MainWindow`**: `setFont()` (initial setup and from the Preferences font-size signal), `setPlaceholderText()`, `setPlainText()` (from `onExampleSelected()`), `setDarkMode()` (from `applyColorScheme()`). Read via `toPlainText()` in `onAssemble()`.
- Does not use `QSettings` or any other global/shared state directly.
