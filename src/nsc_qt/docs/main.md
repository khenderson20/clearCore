# main.cpp

## A. Overview

This is the entry point for the `clearCore-gui` executable — the Qt6 desktop GUI build of clearCore, as distinct from `number_system_converter`, the FTXUI terminal build. Its job is small and standard: construct the `QApplication`, apply a few identifying attributes, configure settings storage format, construct and show the single `MainWindow`, and hand control to Qt's event loop.

## B. Qt Application Setup

Constructs a [`QApplication`](https://doc.qt.io/qt-6/qapplication.html) (the Qt Widgets application object; required because this is a widget-based, not QML-based, GUI) from `argc`/`argv`. Sets three identifying attributes used by `QSettings` and shown in OS-level application metadata:
- `setApplicationName(QStringLiteral("clearCore-gui"))`
- `setOrganizationName(QStringLiteral("nsc-qt"))`
- `setApplicationVersion(QStringLiteral("1.1.0"))`

`QSettings::setDefaultFormat(QSettings::IniFormat)` is called next, forcing `QSettings` to use INI-file storage on every platform (rather than the platform default, e.g. the Windows registry) — this is what `MainWindow` and `PreferencesDialog` rely on implicitly when they construct `QSettings("nsc-qt", "clearCore-gui")` without specifying a format themselves.

## C. Command-Line Handling

None. `argc`/`argv` are passed straight to the `QApplication` constructor for Qt's own built-in argument handling (platform/style options like `-style`); no `QCommandLineParser` or custom option parsing is present in this file.

## D. Top-Level Object Creation

One application-level object and one window are created, in this order:

1. `QApplication app(argc, argv)` — must exist before any `QWidget` is constructed.
2. `nsc::qt::MainWindow window` — the sole top-level window. Its constructor builds the entire widget tree (menu bar, toolbar, all six tabs) and the one `SimulatorController` instance the whole application shares. See `MainWindow.md`.

## E. Wiring and Connections

None performed directly in `main()` — all signal/slot wiring happens inside `MainWindow`'s constructor. `main.cpp`'s only responsibility after constructing `window` is calling `window.show()` to make it visible before the event loop starts.

## F. Event Loop

Started via `return app.exec();` — the function's return value is `app.exec()`'s own return value, which becomes the process exit code (0 on a normal quit, initiated by closing the main window).

## G. Dependencies

- `nsc_qt/main_window.h` — `MainWindow`, the application's single top-level window.
- `<QApplication>` — the Qt Widgets application object and event loop.
- `<QSettings>` — used here only to force INI-format storage; the actual settings keys are read/written by `MainWindow` and `PreferencesDialog`.
- `<QStringLiteral>` — compile-time `QString` construction for the three identifying attribute values, avoiding a runtime UTF-8-to-UTF-16 conversion for string literals that never change.
