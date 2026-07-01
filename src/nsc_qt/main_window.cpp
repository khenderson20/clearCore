#include "nsc_qt/main_window.h"

#include "nsc_qt/assembler.h"
#include "nsc_qt/preferences_dialog.h"
#include "nsc_qt/widgets/datapath_widget.h"
#include "nsc_qt/widgets/memory_widget.h"
#include "nsc_qt/widgets/pipeline_trace_widget.h"
#include "nsc_qt/widgets/register_widget.h"

#include "mips/decoder.h"
#include "mips/pipelined_cpu.h"
#include "mips/program_loader.h"
#include "mips/registers.h"

#include <array>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace nsc::qt {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , controller_(std::make_unique<SimulatorController>(
          std::make_unique<mips::PipelinedCpu>()))
{
    setWindowTitle("clearCore — MIPS Simulator");
    resize(1200, 760);

    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupConnections();

    // Restore preferences
    QSettings s("nsc-qt", "clearCore-gui");
    applyColorScheme(s.value("colorScheme", "light").toString() == "dark");
    controller_->setExecutionSpeed(s.value("executionSpeed", 100).toInt());
}

// ── Menu bar ──────────────────────────────────────────────────────────────────

void MainWindow::setupMenuBar()
{
    auto* mb = menuBar();

    // File
    auto* file_menu = mb->addMenu("&File");
    act_open_ = file_menu->addAction("&Open Program…", QKeySequence("Ctrl+O"),
                                      this, &MainWindow::onOpenFile);
    act_save_ = file_menu->addAction("&Save Trace…", QKeySequence("Ctrl+S"),
                                      this, &MainWindow::onSaveTrace);
    file_menu->addSeparator();
    file_menu->addAction("E&xit", qApp, &QApplication::quit);

    // Simulation
    auto* sim_menu = mb->addMenu("&Simulation");
    act_step_ = sim_menu->addAction("&Step", QKeySequence("F10"),
                                     this, &MainWindow::onStep);
    act_run_pause_ = sim_menu->addAction("&Run", QKeySequence("F5"),
                                          this, &MainWindow::onRunPause);
    sim_menu->addAction("Sto&p", QKeySequence("Shift+F5"),
                        this, [this]{ controller_->stop(); act_run_pause_->setText("&Run"); });
    act_reset_ = sim_menu->addAction("&Reset", QKeySequence("Ctrl+R"),
                                      this, &MainWindow::onReset);

    // View
    auto* view_menu = mb->addMenu("&View");
    view_menu->addAction("&Preferences…", QKeySequence("Ctrl+,"),
                          this, &MainWindow::onShowPreferences);
    view_menu->addAction("Keyboard &Shortcuts", QKeySequence("Ctrl+?"),
                          this, [this] {
        const QString text =
            "F10         – Step one cycle\n"
            "F5          – Run\n"
            "Shift+F5    – Stop\n"
            "Ctrl+R      – Reset\n"
            "Ctrl+O      – Open program file\n"
            "Ctrl+S      – Save trace to CSV\n"
            "Ctrl+,      – Preferences";
        QMessageBox::information(this, "Keyboard Shortcuts", text);
    });
}

// ── Tool bar ──────────────────────────────────────────────────────────────────

void MainWindow::setupToolBar()
{
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->addAction(act_step_);
    tb->addAction(act_run_pause_);
    tb->addAction(act_reset_);
    tb->addSeparator();
    tb->addAction(act_open_);
}

// ── Central widget ────────────────────────────────────────────────────────────

void MainWindow::setupCentralWidget()
{
    tabs_ = new QTabWidget(this);
    setCentralWidget(tabs_);

    datapath_widget_  = new DatapathWidget(this);
    register_widget_  = new RegisterWidget(this);
    memory_widget_    = new MemoryWidget(this);
    trace_widget_     = new PipelineTraceWidget(this);

    // Tab 0: Datapath
    tabs_->addTab(datapath_widget_,   "Datapath");

    // Tab 1: Registers
    tabs_->addTab(register_widget_,   "Registers");

    // Tab 2: Memory
    tabs_->addTab(memory_widget_,     "Memory");

    // Tab 3: Pipeline Trace
    tabs_->addTab(trace_widget_,      "Pipeline Trace");

    // Tab 4: Code Editor
    tabs_->addTab(createCodeEditorTab(), "Code Editor");

    // Tab 5: Statistics
    tabs_->addTab(createStatisticsTab(), "Statistics");

    // Status bar
    status_cycles_lbl_ = new QLabel("Cycles: 0");
    status_instrs_lbl_ = new QLabel("Instructions: 0");
    status_cpi_lbl_    = new QLabel("CPI: —");
    statusBar()->addPermanentWidget(status_cycles_lbl_);
    statusBar()->addPermanentWidget(new QLabel("|"));
    statusBar()->addPermanentWidget(status_instrs_lbl_);
    statusBar()->addPermanentWidget(new QLabel("|"));
    statusBar()->addPermanentWidget(status_cpi_lbl_);
}

QWidget* MainWindow::createCodeEditorTab()
{
    auto* w  = new QWidget;
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    code_editor_ = new QPlainTextEdit(w);
    code_editor_->setFont(QFont("monospace", 10));
    code_editor_->setPlaceholderText(
        "# MIPS assembly\n"
        "# Example:\n"
        "#   addi $t0, $zero, 5\n"
        "#   addi $t1, $zero, 10\n"
        "#   add  $t2, $t0, $t1\n");
    vl->addWidget(code_editor_);

    // Separator line above button row
    auto* sep = new QFrame(w);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vl->addWidget(sep);

    auto* btn_row  = new QHBoxLayout;
    btn_row->setContentsMargins(8, 6, 8, 6);
    btn_row->setSpacing(8);

    auto* asm_btn  = new QPushButton("Assemble", w);
    asm_btn->setObjectName("primaryButton");
    asm_btn->setToolTip("Assemble source and prepare machine code (does not load)");

    auto* load_btn = new QPushButton("Load", w);
    load_btn->setToolTip("Load assembled program into simulator and reset");

    asm_status_lbl_ = new QLabel(w);
    btn_row->addWidget(asm_btn);
    btn_row->addWidget(load_btn);
    btn_row->addSpacing(12);
    btn_row->addWidget(asm_status_lbl_);
    btn_row->addStretch();
    vl->addLayout(btn_row);

    connect(asm_btn,  &QPushButton::clicked, this, &MainWindow::onAssemble);
    connect(load_btn, &QPushButton::clicked, this, &MainWindow::onLoad);
    return w;
}

QWidget* MainWindow::createStatisticsTab()
{
    auto* w  = new QWidget;
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    auto mkLabel = [&](QFormLayout* fl, const char* name, QLabel*& ptr) {
        ptr = new QLabel("—", w);
        ptr->setFont(QFont("monospace", 11));
        fl->addRow(name, ptr);
    };

    // ── Performance group ──────────────────────────────────────────────────────
    auto* perf_box = new QGroupBox("Performance", w);
    auto* perf_fl  = new QFormLayout(perf_box);
    perf_fl->setContentsMargins(12, 8, 12, 8);
    perf_fl->setSpacing(8);
    perf_fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mkLabel(perf_fl, "Cycles executed:",      stat_cycles_lbl_);
    mkLabel(perf_fl, "Instructions retired:", stat_instrs_lbl_);
    mkLabel(perf_fl, "CPI:",                  stat_cpi_lbl_);
    vl->addWidget(perf_box);

    // ── Pipeline events group ──────────────────────────────────────────────────
    auto* pipe_box = new QGroupBox("Pipeline Events", w);
    auto* pipe_fl  = new QFormLayout(pipe_box);
    pipe_fl->setContentsMargins(12, 8, 12, 8);
    pipe_fl->setSpacing(8);
    pipe_fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mkLabel(pipe_fl, "Data hazards:",      stat_data_haz_lbl_);
    mkLabel(pipe_fl, "Control hazards:",   stat_ctrl_haz_lbl_);
    mkLabel(pipe_fl, "Forwarding events:", stat_fwd_lbl_);
    mkLabel(pipe_fl, "Stalls:",            stat_stalls_lbl_);
    mkLabel(pipe_fl, "Flushes:",           stat_flushes_lbl_);
    vl->addWidget(pipe_box);

    vl->addStretch();
    return w;
}

// ── Connections ───────────────────────────────────────────────────────────────

void MainWindow::setupConnections()
{
    connect(controller_.get(), &SimulatorController::cycleExecuted,
            this, &MainWindow::onCycleExecuted);
    connect(controller_.get(), &SimulatorController::pipelineStateChanged,
            this, &MainWindow::onPipelineStateChanged);
    connect(controller_.get(), &SimulatorController::statisticsUpdated,
            this, &MainWindow::onStatisticsUpdated);
    connect(controller_.get(), &SimulatorController::halted,
            this, &MainWindow::onHalted);
    connect(controller_.get(), &SimulatorController::faulted,
            this, &MainWindow::onFaulted);
    connect(controller_.get(), &SimulatorController::breakpointHit,
            this, [this](uint32_t pc) {
                statusBar()->showMessage(
                    QString("Breakpoint hit at 0x%1").arg(pc, 8, 16, QChar('0')), 5000);
                act_run_pause_->setText("&Run");
            });

    connect(datapath_widget_, &DatapathWidget::breakpointToggleRequested,
            this, &MainWindow::onBreakpointToggle);
    connect(datapath_widget_, &DatapathWidget::stageDetailRequested,
            this, &MainWindow::onStageDetailRequested);
}

// ── Slot implementations ──────────────────────────────────────────────────────

void MainWindow::onStep()
{
    controller_->stop();
    act_run_pause_->setText("&Run");
    controller_->stepCycle();
}

void MainWindow::onRunPause()
{
    if (controller_->isRunning()) {
        controller_->stop();
        act_run_pause_->setText("&Run");
    } else {
        controller_->run();
        act_run_pause_->setText("&Pause");
    }
}

void MainWindow::onReset()
{
    controller_->stop();
    act_run_pause_->setText("&Run");
    controller_->reset();
    register_widget_->clear();
    trace_widget_->clear();
    statusBar()->showMessage("Reset.", 2000);
}

void MainWindow::onOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Hex Program", {}, "Hex Programs (*.hex *.txt);;All Files (*)");
    if (path.isEmpty()) return;

    auto prog = mips::load_hex_file(path.toStdString());
    if (!prog) {
        QMessageBox::critical(this, "Load Error",
                              QString::fromStdString(prog.error.value_or("unknown error")));
        return;
    }
    onReset();
    if (!controller_->loadProgram(prog.words)) {
        QMessageBox::critical(this, "Load Error", "Program too large for memory.");
        return;
    }
    statusBar()->showMessage(
        QString("Loaded %1 instructions from %2").arg(prog.words.size()).arg(path), 4000);
}

void MainWindow::onSaveTrace()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Pipeline Trace", "trace.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QMessageBox::information(this, "Save Trace",
                             "Trace export not yet implemented for this build.");
}

void MainWindow::onAssemble()
{
    const std::string src = code_editor_->toPlainText().toStdString();
    if (src.empty()) {
        asm_status_lbl_->setText("No source to assemble.");
        return;
    }
    auto result = assemble(src);
    if (!result) {
        asm_status_lbl_->setStyleSheet("color: red;");
        asm_status_lbl_->setText(QString::fromStdString(result.error.value_or("error")));
        assembled_words_.clear();
        return;
    }
    assembled_words_ = std::move(result.words);
    asm_status_lbl_->setStyleSheet("color: green;");
    asm_status_lbl_->setText(
        QString("✓ %1 instructions assembled").arg(assembled_words_.size()));
}

void MainWindow::onLoad()
{
    if (assembled_words_.empty()) {
        asm_status_lbl_->setStyleSheet("color: orange;");
        asm_status_lbl_->setText("Assemble first.");
        return;
    }
    onReset();
    if (!controller_->loadProgram(assembled_words_)) {
        asm_status_lbl_->setStyleSheet("color: red;");
        asm_status_lbl_->setText("Program too large for memory.");
        return;
    }
    asm_status_lbl_->setStyleSheet("color: green;");
    asm_status_lbl_->setText(
        QString("✓ %1 instructions loaded").arg(assembled_words_.size()));
}

void MainWindow::onShowPreferences()
{
    PreferencesDialog dlg(this);
    connect(&dlg, &PreferencesDialog::colorSchemeChanged,  this, &MainWindow::applyColorScheme);
    connect(&dlg, &PreferencesDialog::executionSpeedChanged, controller_.get(),
            &SimulatorController::setExecutionSpeed);
    connect(&dlg, &PreferencesDialog::showRegisterAliasesChanged,
            register_widget_, &RegisterWidget::setShowAliases);
    connect(&dlg, &PreferencesDialog::fontSizeChanged, this, [this](int sz) {
        QFont f("monospace", sz);
        if (code_editor_) code_editor_->setFont(f);
    });
    dlg.exec();
}

void MainWindow::onCycleExecuted(uint64_t count)
{
    status_cycles_lbl_->setText(QString("Cycles: %1").arg(count));
}

void MainWindow::onPipelineStateChanged(mips::PipelineState state)
{
    datapath_widget_->setPipelineState(state);
    register_widget_->setPipelineState(state);
    trace_widget_->updateCycle(state);

    // Refresh register values
    std::array<uint32_t, 32> reg_vals{};
    for (int i = 0; i < 32; ++i)
        reg_vals[i] = controller_->registerValue(static_cast<uint8_t>(i));
    register_widget_->updateValues(reg_vals);

    // Refresh memory
    memory_widget_->updateDisplay(controller_->memory());
}

void MainWindow::onStatisticsUpdated(nsc::qt::SimulatorStatistics stats)
{
    status_instrs_lbl_->setText(
        QString("Instructions: %1").arg(static_cast<qulonglong>(stats.instructions_retired)));
    const double cpi = stats.cpi();
    status_cpi_lbl_->setText(cpi > 0 ? QString("CPI: %1").arg(cpi, 0, 'f', 2) : "CPI: —");

    stat_cycles_lbl_->setText(   QString::number(stats.cycles_executed));
    stat_instrs_lbl_->setText(   QString::number(stats.instructions_retired));

    if (cpi > 0) {
        stat_cpi_lbl_->setText(QString::number(cpi, 'f', 2));
        const QColor cpi_color = cpi >= 2.0 ? QColor("#F44336")
                               : cpi >= 1.5 ? QColor("#FF9800")
                                            : QColor("#4CAF50");
        stat_cpi_lbl_->setStyleSheet(
            QString("color: %1; font-weight: bold;").arg(cpi_color.name()));
    } else {
        stat_cpi_lbl_->setText("—");
        stat_cpi_lbl_->setStyleSheet("");
    }

    stat_data_haz_lbl_->setText( QString::number(stats.data_hazards));
    stat_ctrl_haz_lbl_->setText( QString::number(stats.control_hazards));
    stat_fwd_lbl_->setText(      QString::number(stats.forwarding_events));
    stat_stalls_lbl_->setText(   QString::number(stats.stalls));
    stat_flushes_lbl_->setText(  QString::number(stats.flushes));
}

void MainWindow::onHalted()
{
    controller_->stop();
    act_run_pause_->setText("&Run");
    statusBar()->showMessage("Program halted (spin-loop detected).", 4000);
}

void MainWindow::onFaulted()
{
    controller_->stop();
    act_run_pause_->setText("&Run");
    statusBar()->showMessage("Processor fault — check your program.", 6000);
}

void MainWindow::onBreakpointToggle(uint32_t pc)
{
    if (controller_->hasBreakpoint(pc)) {
        controller_->clearBreakpoint(pc);
        statusBar()->showMessage(
            QString("Breakpoint cleared at 0x%1").arg(pc, 8, 16, QChar('0')), 2000);
    } else {
        controller_->setBreakpoint(pc);
        statusBar()->showMessage(
            QString("Breakpoint set at 0x%1").arg(pc, 8, 16, QChar('0')), 2000);
    }
    datapath_widget_->setBreakpoints(controller_->breakpoints());
}

void MainWindow::onStageDetailRequested(int stage_index, uint32_t pc, uint32_t raw)
{
    using namespace mips;
    auto decoded = Decoder::decode(raw);

    static constexpr const char* STAGES[] = {"IF", "ID", "EX", "MEM", "WB"};
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Stage Detail — %1").arg(
        stage_index < 5 ? QString(STAGES[stage_index]) : "?"));
    dlg.setMinimumWidth(340);

    auto* fl = new QFormLayout(&dlg);
    fl->setContentsMargins(16, 16, 16, 16);
    fl->setSpacing(6);

    auto add_row = [&](const QString& key, const QString& val) {
        auto* lbl = new QLabel(val);
        lbl->setFont(QFont("monospace", 9));
        fl->addRow(key, lbl);
    };

    add_row("PC:",          QString("0x%1").arg(pc, 8, 16, QChar('0')).toUpper());
    add_row("Raw word:",    QString("0x%1").arg(raw, 8, 16, QChar('0')).toUpper());

    if (decoded) {
        add_row("Mnemonic:", QString::fromStdString(std::string(Decoder::mnemonic(*decoded))));
        if (decoded->format == InstrFormat::R) {
            const auto& r = decoded->r();
            add_row("$rs:", QString("$%1 (%2) = 0x%3")
                        .arg(r.rs)
                        .arg(QString::fromStdString(std::string(register_abi_name(r.rs))))
                        .arg(controller_->registerValue(r.rs), 8, 16, QChar('0')));
            add_row("$rt:", QString("$%1 (%2) = 0x%3")
                        .arg(r.rt)
                        .arg(QString::fromStdString(std::string(register_abi_name(r.rt))))
                        .arg(controller_->registerValue(r.rt), 8, 16, QChar('0')));
            add_row("$rd:", QString("$%1 (%2) = 0x%3")
                        .arg(r.rd)
                        .arg(QString::fromStdString(std::string(register_abi_name(r.rd))))
                        .arg(controller_->registerValue(r.rd), 8, 16, QChar('0')));
        } else if (decoded->format == InstrFormat::I) {
            const auto& i = decoded->i();
            add_row("$rs:", QString("$%1 (%2) = 0x%3")
                        .arg(i.rs)
                        .arg(QString::fromStdString(std::string(register_abi_name(i.rs))))
                        .arg(controller_->registerValue(i.rs), 8, 16, QChar('0')));
            add_row("imm:", QString("0x%1 (%2)").arg(i.imm, 4, 16, QChar('0'))
                                                .arg(static_cast<int16_t>(i.imm)));
        }
    } else {
        add_row("Decode:", "(unknown instruction)");
    }

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok);
    fl->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::applyColorScheme(bool dark)
{
    dark_mode_ = dark;

    // ── Palette (non-styled widgets, tooltips, system colours) ────────────────
    QPalette pal;
    if (dark) {
        pal.setColor(QPalette::Window,          QColor(0x1E,0x1E,0x1E));
        pal.setColor(QPalette::WindowText,      Qt::white);
        pal.setColor(QPalette::Base,            QColor(0x2A,0x2A,0x2A));
        pal.setColor(QPalette::AlternateBase,   QColor(0x35,0x35,0x35));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x25,0x25,0x25));
        pal.setColor(QPalette::ToolTipText,     Qt::white);
        pal.setColor(QPalette::Text,            Qt::white);
        pal.setColor(QPalette::Button,          QColor(0x3C,0x3C,0x3C));
        pal.setColor(QPalette::ButtonText,      Qt::white);
        pal.setColor(QPalette::BrightText,      Qt::red);
        pal.setColor(QPalette::Highlight,       QColor(0x26,0x4F,0x78));
        pal.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        pal.setColor(QPalette::Window,          QColor(0xF5,0xF5,0xF5));
        pal.setColor(QPalette::WindowText,      Qt::black);
        pal.setColor(QPalette::Base,            Qt::white);
        pal.setColor(QPalette::AlternateBase,   QColor(0xF5,0xF5,0xF5));
        pal.setColor(QPalette::ToolTipBase,     Qt::white);
        pal.setColor(QPalette::ToolTipText,     Qt::black);
        pal.setColor(QPalette::Text,            Qt::black);
        pal.setColor(QPalette::Button,          QColor(0xEE,0xEE,0xEE));
        pal.setColor(QPalette::ButtonText,      Qt::black);
        pal.setColor(QPalette::Highlight,       QColor(0x00,0x78,0xD4));
        pal.setColor(QPalette::HighlightedText, Qt::white);
    }
    qApp->setPalette(pal);

    // ── Global stylesheet ─────────────────────────────────────────────────────
    const QString qss = dark ? R"(
QMainWindow, QDialog { background: #1E1E1E; }

QMenuBar { background: #2D2D2D; color: #CCCCCC; border-bottom: 1px solid #444444; }
QMenuBar::item:selected { background: #094771; color: white; }
QMenu { background: #252526; color: #CCCCCC; border: 1px solid #454545; }
QMenu::item:selected { background: #094771; }
QMenu::separator { height: 1px; background: #454545; margin: 2px 0; }

QToolBar {
    background: #2D2D2D;
    border-bottom: 1px solid #444444;
    padding: 3px 4px;
    spacing: 2px;
}
QToolBar::separator { width: 1px; background: #555555; margin: 4px 3px; }
QToolButton {
    color: #CCCCCC;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px 10px;
    min-width: 32px;
}
QToolButton:hover  { background: #3C3C3C; border-color: #555555; }
QToolButton:pressed { background: #094771; border-color: #007ACC; }

QTabWidget::pane { border: none; border-top: 1px solid #333333; }
QTabBar { background: #252526; }
QTabBar::tab {
    background: #2D2D2D;
    color: #888888;
    padding: 6px 20px;
    border: none;
    border-bottom: 2px solid transparent;
    min-width: 72px;
}
QTabBar::tab:selected   { background: #1E1E1E; color: #FFFFFF; border-bottom-color: #007ACC; }
QTabBar::tab:hover:!selected { background: #383838; color: #CCCCCC; }

QStatusBar { background: #252526; color: #CCCCCC; border-top: 1px solid #444444; }
QStatusBar QLabel { color: #CCCCCC; padding: 0px 5px; }

QGroupBox {
    color: #9CDCFE;
    border: 1px solid #3C3C3C;
    border-radius: 5px;
    margin-top: 12px;
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0px 5px;
}

QPushButton {
    background: #3C3C3C;
    color: #CCCCCC;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 5px 14px;
    min-width: 64px;
}
QPushButton:hover  { background: #4C4C4C; border-color: #777777; }
QPushButton:pressed { background: #094771; border-color: #007ACC; }
QPushButton#primaryButton {
    background: #0E639C;
    color: white;
    border-color: #1177BB;
}
QPushButton#primaryButton:hover  { background: #1177BB; }
QPushButton#primaryButton:pressed { background: #094771; }

QTableWidget {
    gridline-color: #3A3A3A;
    background: #1E1E1E;
    alternate-background-color: #252526;
    color: #CCCCCC;
    border: none;
}
QHeaderView::section {
    background: #2D2D2D;
    color: #9CDCFE;
    border: none;
    border-right: 1px solid #444444;
    border-bottom: 1px solid #444444;
    padding: 3px 8px;
    font-weight: bold;
}
QTableWidget::item:selected { background: #264F78; color: white; }

QPlainTextEdit {
    background: #1E1E1E;
    color: #D4D4D4;
    border: none;
    selection-background-color: #264F78;
}

QSpinBox {
    background: #3C3C3C;
    color: #CCCCCC;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 2px 6px;
}
QSpinBox::up-button, QSpinBox::down-button { background: #3C3C3C; border: none; width: 16px; }
QSpinBox::up-arrow   { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 4px solid #888; }
QSpinBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top:    4px solid #888; }

QScrollBar:vertical   { background: #1E1E1E; width: 10px; border: none; }
QScrollBar:horizontal { background: #1E1E1E; height: 10px; border: none; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: #4A4A4A; border-radius: 5px; min-height: 20px; min-width: 20px;
}
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #6A6A6A; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }

QFrame[frameShape="4"] { color: #444444; }
QFrame[frameShape="5"] { color: #444444; }
QWidget#regHeader { background: #252526; border-bottom: 1px solid #3C3C3C; }
QWidget#regHeader QLabel { color: #9CDCFE; background: transparent; border: none; }
)"
    : R"(
QMainWindow, QDialog { background: #F5F5F5; }

QMenuBar { background: #F0F0F0; color: #333333; border-bottom: 1px solid #DDDDDD; }
QMenuBar::item:selected { background: #0078D4; color: white; }
QMenu { background: white; color: #333333; border: 1px solid #CCCCCC; }
QMenu::item:selected { background: #0078D4; color: white; }
QMenu::separator { height: 1px; background: #DDDDDD; margin: 2px 0; }

QToolBar {
    background: #F3F3F3;
    border-bottom: 1px solid #E0E0E0;
    padding: 3px 4px;
    spacing: 2px;
}
QToolBar::separator { width: 1px; background: #CCCCCC; margin: 4px 3px; }
QToolButton {
    color: #333333;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px 10px;
    min-width: 32px;
}
QToolButton:hover   { background: #E8E8E8; border-color: #CCCCCC; }
QToolButton:pressed { background: #0078D4; color: white; border-color: #0065B4; }

QTabWidget::pane { border: none; border-top: 1px solid #E0E0E0; background: white; }
QTabBar { background: #EBEBEB; }
QTabBar::tab {
    background: #EBEBEB;
    color: #666666;
    padding: 6px 20px;
    border: none;
    border-bottom: 2px solid transparent;
    min-width: 72px;
}
QTabBar::tab:selected   { background: white; color: #1A1A1A; border-bottom-color: #0078D4; }
QTabBar::tab:hover:!selected { background: #E0E0E0; color: #333333; }

QStatusBar { background: #F3F3F3; color: #555555; border-top: 1px solid #E0E0E0; }
QStatusBar QLabel { color: #555555; padding: 0px 5px; }

QGroupBox {
    color: #0078D4;
    border: 1px solid #DDDDDD;
    border-radius: 5px;
    margin-top: 12px;
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0px 5px;
}

QPushButton {
    background: #EEEEEE;
    color: #333333;
    border: 1px solid #CCCCCC;
    border-radius: 3px;
    padding: 5px 14px;
    min-width: 64px;
}
QPushButton:hover   { background: #E0E0E0; border-color: #AAAAAA; }
QPushButton:pressed { background: #CCE4FF; border-color: #0078D4; }
QPushButton#primaryButton {
    background: #0078D4;
    color: white;
    border-color: #006CC1;
}
QPushButton#primaryButton:hover   { background: #106EBE; }
QPushButton#primaryButton:pressed { background: #005499; }

QTableWidget {
    gridline-color: #E0E0E0;
    background: white;
    alternate-background-color: #F7F7F7;
    color: #333333;
    border: none;
}
QHeaderView::section {
    background: #F3F3F3;
    color: #0078D4;
    border: none;
    border-right: 1px solid #E0E0E0;
    border-bottom: 1px solid #E0E0E0;
    padding: 3px 8px;
    font-weight: bold;
}
QTableWidget::item:selected { background: #CCE4FF; color: #003366; }

QPlainTextEdit {
    background: white;
    color: #1A1A1A;
    border: none;
    selection-background-color: #CCE4FF;
}

QSpinBox {
    background: white;
    color: #333333;
    border: 1px solid #CCCCCC;
    border-radius: 3px;
    padding: 2px 6px;
}
QSpinBox::up-button, QSpinBox::down-button { background: #EEEEEE; border: none; width: 16px; }
QSpinBox::up-arrow   { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 4px solid #666; }
QSpinBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top:    4px solid #666; }

QScrollBar:vertical   { background: #F5F5F5; width: 10px; border: none; }
QScrollBar:horizontal { background: #F5F5F5; height: 10px; border: none; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: #BBBBBB; border-radius: 5px; min-height: 20px; min-width: 20px;
}
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #999999; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }

QFrame[frameShape="4"] { color: #DDDDDD; }
QFrame[frameShape="5"] { color: #DDDDDD; }
QWidget#regHeader { background: #EBEBEB; border-bottom: 1px solid #DDDDDD; }
QWidget#regHeader QLabel { color: #0078D4; background: transparent; border: none; }
)";
    qApp->setStyleSheet(qss);

    datapath_widget_->setDarkMode(dark);
    register_widget_->setDarkMode(dark);
    memory_widget_->setDarkMode(dark);
    trace_widget_->setDarkMode(dark);
}

} // namespace nsc::qt
