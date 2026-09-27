# nsc_qt reference documentation

Reference docs for `nsc_qt`, the Qt6 desktop GUI layer of clearCore (the `clearCore-gui` executable target, built when `BUILD_QT6_UI` is `ON`). Every class and header here depends only on `mips_core` through the `IProcessor` interface, via the `SimulatorController` bridge — never on `mips_core` internals directly.

Generated with the `qt-cpp-docs` skill. See also [docs/QT6_ARCHITECTURE.md](../../../docs/QT6_ARCHITECTURE.md) at the repository root for the higher-level design writeup, and [README.md](../../../README.md) for screenshots and build instructions.

## Application entry point

| File | Description |
|------|--------------|
| [main.md](main.md) | `main.cpp` — constructs `QApplication` and `MainWindow`, starts the event loop. |

## Core window and controller

| Class | Description |
|-------|--------------|
| [MainWindow.md](MainWindow.md) | The top-level application window; owns `SimulatorController` and every tab widget. |
| [SimulatorController.md](SimulatorController.md) | Bridges the UI-agnostic `mips::IProcessor` to Qt signals every widget consumes. |
| [PreferencesDialog.md](PreferencesDialog.md) | Modal settings dialog for color scheme, execution speed, font size, and register aliases. |

## Tab widgets

| Class | Description |
|-------|--------------|
| [DatapathWidget.md](DatapathWidget.md) | Live 5-stage pipeline diagram with breakpoints and keyboard-navigable stage detail. |
| [RegisterWidget.md](RegisterWidget.md) | 32-register grid with a wall-clock "just written" fade highlight. |
| [MemoryWidget.md](MemoryWidget.md) | Scrollable hex memory dump with a base-address jump control. |
| [PipelineTraceWidget.md](PipelineTraceWidget.md) | Instruction × cycle pipeline trace table, bounded to a 20-cycle window. |
| [CodeEditor.md](CodeEditor.md) | `QPlainTextEdit` subclass adding line numbers and current-line highlighting. |

*(The Code Editor tab's Assemble/Load button row and the Statistics tab are built inline in `MainWindow`, not as separate classes — see `MainWindow.md`.)*

## Utility headers

| File | Description |
|------|--------------|
| [assembler.md](assembler.md) | Two-pass MIPS assembler: assembly text → instruction words. |
| [examples.md](examples.md) | Curated catalog of example MIPS programs for the Code Editor's dropdown. |
| [ui_scale.md](ui_scale.md) | Shared 3-step typography scale used by every widget above. |
