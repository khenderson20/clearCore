# PreferencesDialog

## 1. Class Overview

`PreferencesDialog` is a modal settings dialog for the clearCore Qt6 GUI, covering color scheme (light/dark), simulation execution speed, code editor font size, and whether register aliases (`$t0`, `$sp`, …) are shown in the Registers tab. `MainWindow` opens one instance from the View → Preferences… menu action each time it is needed rather than keeping one alive for the application's lifetime.

A developer reaches for this class only from `MainWindow::onShowPreferences()`; it is not meant to be embedded elsewhere.

## 2. Project Structure and Dependencies

Constructed on the stack in `MainWindow::onShowPreferences()` and shown with `exec()`.

Qt modules required:
- **Qt Widgets** — `QDialog` (base class), `QCheckBox`, `QDialogButtonBox`, `QFormLayout`, `QGroupBox`, `QHBoxLayout`, `QLabel`, `QRadioButton`, `QSlider`, `QSpinBox`, `QVBoxLayout`.
- **Qt Core** — `QSettings` for persistence.

Project-internal types: `nsc_qt::scale::monoFont()` / `kFontSizeBody` (`nsc_qt/ui_scale.h`) supplies the default font-size value shown in the spin box.

## 3. Class Hierarchy and Role

`PreferencesDialog` inherits [`QDialog`](https://doc.qt.io/qt-6/qdialog.html), which contributes modal/modeless dialog behavior — `exec()` blocks the calling thread's event loop nesting until the dialog is accepted or rejected. `QDialog` derives from [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) → [`QObject`](https://doc.qt.io/qt-6/qobject.html), giving it the usual paint/event/meta-object stack.

## 4. Signals

#### `colorSchemeChanged(bool dark)`
Emitted from `onAccept()` with the state of the Dark radio button. `MainWindow` connects this to `applyColorScheme()`.

#### `executionSpeedChanged(int speed)`
Emitted from `onAccept()` with the speed slider's value (0–100). `MainWindow` connects this directly to `SimulatorController::setExecutionSpeed()`.

#### `fontSizeChanged(int size)`
Emitted from `onAccept()` with the font-size spin box's value. `MainWindow` connects this to a lambda that updates the Code Editor tab's font.

#### `showRegisterAliasesChanged(bool show)`
Emitted from `onAccept()` with the alias checkbox's state. `MainWindow` connects this directly to `RegisterWidget::setShowAliases()`.

None of these signals fire until the user clicks OK — `Cancel` rejects the dialog without emitting anything.

## 5. Public Methods

#### `explicit PreferencesDialog(QWidget* parent = nullptr)`
Builds the dialog's layout (color scheme radio buttons, execution-speed slider, font-size spin box, alias checkbox, OK/Cancel buttons) and calls `loadSettings()` to populate initial values. `explicit` disables implicit `QWidget*` conversion.

#### `void loadSettings()`
Reads `QSettings("nsc-qt", "clearCore-gui")` and applies the stored `colorScheme`, `executionSpeed`, `fontSize`, and `showRegisterAliases` values to the dialog's controls, falling back to light mode, speed 100, `scale::kFontSizeBody`, and aliases-on respectively if unset. Called automatically by the constructor; safe to call again to discard unsaved edits.

#### `void saveSettings()`
Writes the current state of all four controls back to `QSettings("nsc-qt", "clearCore-gui")`. Called automatically by `onAccept()` — not normally called directly.

#### `bool isDarkMode() const noexcept`
Returns whether the Dark radio button is currently checked.

#### `int executionSpeed() const noexcept`
Returns the execution-speed slider's current value (0–100).

#### `int fontSize() const noexcept`
Returns the font-size spin box's current value.

#### `bool showRegisterAliases() const noexcept`
Returns whether the register-aliases checkbox is currently checked.

## 6. Protected / Private Slots

#### `void onAccept()` *(private slot)*
Connected to the dialog button box's `accepted()` signal. Calls `saveSettings()`, emits all four signals above with the dialog's current values, then calls `QDialog::accept()` to close the dialog with an accepted result.

## 7. Ownership and Lifecycle

`PreferencesDialog` is created as a local stack variable in `MainWindow::onShowPreferences()` (`PreferencesDialog dlg(this)`) and destroyed automatically when that method returns after `dlg.exec()` — it is never heap-allocated or kept alive beyond a single Preferences interaction. Passing `this` (the `MainWindow`) as `parent` makes it modal relative to the main window and ensures correct stacking/centering even though its lifetime is stack-managed, not parent-managed.

## 8. Thread Safety

**GUI-thread only**, as a `QDialog`/`QWidget` subclass.

## 9. Inter-Class Interactions

- **Emits to `MainWindow`**: all four signals, connected only while the dialog is open (the connections are made fresh in `onShowPreferences()` each time).
- **Reads/writes `QSettings("nsc-qt", "clearCore-gui")`** — the same settings group `MainWindow` reads once at startup for the initial color scheme and execution speed.

## 10. Usage Example

```cpp
nsc::qt::PreferencesDialog dlg(parentWindow);
QObject::connect(&dlg, &nsc::qt::PreferencesDialog::colorSchemeChanged,
                  parentWindow, &MainWindow::applyColorScheme);
dlg.exec();
```
