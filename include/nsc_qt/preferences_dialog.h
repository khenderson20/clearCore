#pragma once

#include <QDialog>

class QRadioButton;
class QSlider;
class QSpinBox;
class QCheckBox;

namespace nsc::qt {

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();

    [[nodiscard]] bool isDarkMode() const noexcept;
    [[nodiscard]] int  executionSpeed() const noexcept;
    [[nodiscard]] int  fontSize() const noexcept;
    [[nodiscard]] bool showRegisterAliases() const noexcept;

signals:
    void colorSchemeChanged(bool dark);
    void executionSpeedChanged(int speed);
    void fontSizeChanged(int size);
    void showRegisterAliasesChanged(bool show);

private slots:
    void onAccept();

private:
    QRadioButton* light_radio_  = nullptr;
    QRadioButton* dark_radio_   = nullptr;
    QSlider*      speed_slider_ = nullptr;
    QSpinBox*     font_spin_    = nullptr;
    QCheckBox*    alias_check_  = nullptr;
};

}  // namespace nsc::qt
