#include "nsc_qt/preferences_dialog.h"
#include "nsc_qt/ui_scale.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace nsc::qt {

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Preferences"));
    setMinimumWidth(360);

    auto* vl = new QVBoxLayout(this);

    // ── Color scheme ──────────────────────────────────────────────────────────
    auto* scheme_box = new QGroupBox(tr("Color Scheme"), this);
    auto* scheme_hl  = new QHBoxLayout(scheme_box);
    light_radio_     = new QRadioButton(tr("Light"), scheme_box);
    dark_radio_      = new QRadioButton(tr("Dark"), scheme_box);
    scheme_hl->addWidget(light_radio_);
    scheme_hl->addWidget(dark_radio_);
    vl->addWidget(scheme_box);

    // ── Execution speed ───────────────────────────────────────────────────────
    auto* speed_box = new QGroupBox(tr("Execution Speed"), this);
    auto* speed_hl  = new QHBoxLayout(speed_box);
    speed_hl->addWidget(new QLabel(tr("Slow"), speed_box));
    speed_slider_ = new QSlider(Qt::Horizontal, speed_box);
    speed_slider_->setRange(0, 100);
    speed_slider_->setValue(100);
    speed_slider_->setTickInterval(10);
    speed_slider_->setTickPosition(QSlider::TicksBelow);
    speed_hl->addWidget(speed_slider_);
    speed_hl->addWidget(new QLabel(tr("Fast"), speed_box));
    vl->addWidget(speed_box);

    // ── Font & display ────────────────────────────────────────────────────────
    auto* disp_box = new QGroupBox(tr("Display"), this);
    auto* fl       = new QFormLayout(disp_box);
    font_spin_     = new QSpinBox(disp_box);
    font_spin_->setRange(8, 24);
    font_spin_->setValue(scale::kFontSizeBody);
    fl->addRow(tr("Font size:"), font_spin_);

    alias_check_ = new QCheckBox(tr("Show register aliases ($t0, $sp, …)"), disp_box);
    alias_check_->setChecked(true);
    fl->addRow(alias_check_);
    vl->addWidget(disp_box);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vl->addWidget(btns);

    connect(btns, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadSettings();
}

void PreferencesDialog::loadSettings() {
    QSettings s("nsc-qt", "clearCore-gui");
    dark_radio_->setChecked(s.value("colorScheme", "light").toString() == "dark");
    light_radio_->setChecked(!dark_radio_->isChecked());
    speed_slider_->setValue(s.value("executionSpeed", 100).toInt());
    font_spin_->setValue(s.value("fontSize", scale::kFontSizeBody).toInt());
    alias_check_->setChecked(s.value("showRegisterAliases", true).toBool());
}

void PreferencesDialog::saveSettings() {
    QSettings s("nsc-qt", "clearCore-gui");
    s.setValue("colorScheme", dark_radio_->isChecked() ? "dark" : "light");
    s.setValue("executionSpeed", speed_slider_->value());
    s.setValue("fontSize", font_spin_->value());
    s.setValue("showRegisterAliases", alias_check_->isChecked());
}

void PreferencesDialog::onAccept() {
    saveSettings();
    emit colorSchemeChanged(dark_radio_->isChecked());
    emit executionSpeedChanged(speed_slider_->value());
    emit fontSizeChanged(font_spin_->value());
    emit showRegisterAliasesChanged(alias_check_->isChecked());
    accept();
}

bool PreferencesDialog::isDarkMode() const noexcept {
    return dark_radio_->isChecked();
}
int PreferencesDialog::executionSpeed() const noexcept {
    return speed_slider_->value();
}
int PreferencesDialog::fontSize() const noexcept {
    return font_spin_->value();
}
bool PreferencesDialog::showRegisterAliases() const noexcept {
    return alias_check_->isChecked();
}

}  // namespace nsc::qt