# ui_scale.h

## A. Overview

This header defines the shared typography scale used across every widget in the Qt6 GUI: three fixed pixel sizes derived from a 16px base using a Major Second (1.125) modular scale, chosen for a compact, data-heavy debugging UI. It is header-only (no corresponding `.cpp`). Every widget that previously picked its own ad hoc point size (values ranging from 7pt to 11pt across different tabs) now pulls from this single source instead, so the whole application is visually consistent and any future size adjustment happens in one place. A developer reaches for `nsc::qt::scale::monoFont()` any time a widget in `nsc_qt` needs a font — there should be no remaining call to the raw `QFont` constructor with a hardcoded size anywhere in `nsc_qt`.

## B. Namespaces

- **`nsc::qt::scale`** — a nested namespace specifically for typography constants and helpers, kept separate from `nsc::qt` itself so call sites read clearly as `scale::kFontSizeBody` / `scale::monoFont(...)` rather than colliding with unrelated `nsc::qt` names.

## C. Types and Type Aliases

None — this file declares only constants and one inline function.

## D. Constants

| Name | Type / Value | Description |
|------|--------------|--------------|
| `kFontSizeDense` | `constexpr int` = `14` | Dense/tabular content: the Memory tab's hex dump, the Pipeline Trace table, and pipeline stage content on the Datapath tab. |
| `kFontSizeBody` | `constexpr int` = `16` | Body text: register values, form labels, and the Code Editor. Also the default font size shown in `PreferencesDialog`'s font-size spin box. |
| `kFontSizeHeader` | `constexpr int` = `18` | Panel titles and section headers, e.g. the Registers tab's "Registers" header bar. |

## E. Functions

#### `QFont monoFont(int pixelSize, bool bold = false)`
Returns a `"monospace"` `QFont` with its pixel size set to `pixelSize` via `QFont::setPixelSize()` (not `QFont::setPointSize()`) and bold enabled if requested. Using pixel size rather than point size means the rendered size is predictable regardless of the platform's point-to-pixel mapping — callers should pass one of the three constants above rather than an arbitrary literal, to keep the whole application on the same three-size scale. Marked `inline` since it is defined in a header with no corresponding `.cpp`.

## F. Dependencies

- `<QFont>` — the return type of `monoFont()` and the type this whole file exists to configure consistently.

No other Qt or project-internal types are used; this file has no dependency on `mips_core`, `SimulatorController`, or any specific widget.

## G. Usage Example

```cpp
#include "nsc_qt/ui_scale.h"

label->setFont(nsc::qt::scale::monoFont(nsc::qt::scale::kFontSizeHeader, /*bold=*/true));
valueLabel->setFont(nsc::qt::scale::monoFont(nsc::qt::scale::kFontSizeBody));
```
