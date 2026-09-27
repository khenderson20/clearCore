#pragma once

// ── ui_scale.h ──────────────────────────────────────────────────────────────
// Shared typography scale for the clearCore Qt GUI.
//
// Derived from a 16px desktop base using a Major Second (1.125) modular
// scale -- a good fit for a compact, data-heavy debugging UI. Every widget
// in nsc_qt should pull its font sizes from here instead of picking its own
// point size, so the whole app uses at most three type sizes:
//
//   kFontSizeDense  (14px) - dense/tabular content: memory hex dump, trace
//                            table, pipeline stage content
//   kFontSizeBody   (16px) - body text: register values, form labels,
//                            code editor
//   kFontSizeHeader (18px) - panel titles, section headers
//
// Sizes are pixel sizes (QFont::setPixelSize), not point sizes, so they
// render at a predictable physical size regardless of the platform's
// default point-to-pixel mapping.

#include <QFont>

namespace nsc::qt::scale {

constexpr int kFontSizeDense  = 14;
constexpr int kFontSizeBody   = 16;
constexpr int kFontSizeHeader = 18;

// Monospace font at a given scale step, optionally bold.
inline QFont monoFont(int pixelSize, bool bold = false) {
    QFont f("monospace");
    f.setPixelSize(pixelSize);
    if (bold) f.setBold(true);
    return f;
}

}  // namespace nsc::qt::scale