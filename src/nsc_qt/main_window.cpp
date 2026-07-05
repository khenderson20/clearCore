#include "nsc_qt/main_window.h"
#include "nsc_qt/ui_scale.h"

#include "nsc_qt/assembler.h"
#include "nsc_qt/dock_panels.h"
#include "nsc_qt/examples.h"
#include "nsc_qt/preferences_dialog.h"
#include "nsc_qt/widgets/code_editor.h"
#include "nsc_qt/widgets/memory_widget.h"
#include "nsc_qt/widgets/pipeline_trace_widget.h"
#include "nsc_qt/widgets/register_widget.h"
#include "nsc_qt/widgets/schematic_datapath_widget.h"

#include "mips/decoder.h"
#include "mips/pipelined_cpu.h"
#include "mips/program_loader.h"
#include "mips/registers.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
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
#include <QPushButton>
#include <QSettings>
#include <QSize>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <array>
#include <string>
#include <utility>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

namespace nsc::qt {

namespace {
// Schema version for the persisted dock layout. Bump whenever the default
// arrangement changes — ADS restoreState() rejects a saved state with a
// different version, so stale/incompatible layouts are cleanly discarded.
// v1: all panels in one central tabbed area.
// v2: 2-column split — Code Editor left, Datapath center-top, inspector tabs bottom.
constexpr int kLayoutVersion = 2;
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      controller_(std::make_unique<SimulatorController>(std::make_unique<mips::PipelinedCpu>())) {
    setWindowTitle(tr("clearCore — MIPS Simulator"));
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

void MainWindow::setupMenuBar() {
    auto* mb = menuBar();

    // File
    auto* file_menu = mb->addMenu(tr("&File"));
    act_open_       = file_menu->addAction(tr("&Open Program…"), QKeySequence("Ctrl+O"), this,
                                           &MainWindow::onOpenFile);
    act_open_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    act_save_ = file_menu->addAction(tr("&Save Trace…"), QKeySequence("Ctrl+S"), this,
                                     &MainWindow::onSaveTrace);
    act_save_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    file_menu->addSeparator();
    file_menu->addAction(tr("E&xit"), qApp, &QApplication::quit);

    // Simulation
    auto* sim_menu = mb->addMenu(tr("&Simulation"));
    act_step_ = sim_menu->addAction(tr("&Step"), QKeySequence("F10"), this, &MainWindow::onStep);
    act_step_->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    act_step_->setToolTip(tr("Step one pipeline cycle (F10)"));
    act_run_pause_ =
        sim_menu->addAction(tr("&Run"), QKeySequence("F5"), this, &MainWindow::onRunPause);
    act_run_pause_->setToolTip(tr("Run / pause simulation (F5)"));
    sim_menu->addAction(tr("Sto&p"), QKeySequence("Shift+F5"), this, [this] {
        controller_->stop();
        setRunState(false);
    });
    act_reset_ =
        sim_menu->addAction(tr("&Reset"), QKeySequence("Ctrl+R"), this, &MainWindow::onReset);
    act_reset_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    act_reset_->setToolTip(tr("Reset simulation to start (Ctrl+R)"));

    // View
    auto* view_menu = mb->addMenu(tr("&View"));
    // Filled with per-dock toggle actions once the docks exist
    // (setupCentralWidget runs after this).
    panels_menu_ = view_menu->addMenu(tr("P&anels"));
    view_menu->addAction(tr("&Reset Layout"), this, &MainWindow::resetLayout);
    view_menu->addSeparator();
    view_menu->addAction(tr("&Preferences…"), QKeySequence("Ctrl+,"), this,
                         &MainWindow::onShowPreferences);
    view_menu->addAction(tr("Keyboard &Shortcuts"), QKeySequence("Ctrl+?"), this, [this] {
        const QString text = tr("F10               – Step one pipeline cycle\n"
                                "F5                – Run / Pause\n"
                                "Shift+F5          – Stop\n"
                                "Ctrl+R            – Reset simulation\n"
                                "Ctrl+O            – Open hex program file\n"
                                "Ctrl+S            – Save trace to CSV\n"
                                "Ctrl+,            – Preferences\n"
                                "\n"
                                "Code Editor:\n"
                                "Ctrl+Enter        – Assemble\n"
                                "Ctrl+Shift+Enter  – Load (after assembling)\n"
                                "\n"
                                "Datapath panel (click or Tab to focus):\n"
                                "Left/Right  – Select a pipeline stage\n"
                                "Enter       – Show stage detail\n"
                                "Space       – Toggle breakpoint on selected stage");
        QMessageBox::information(this, tr("Keyboard Shortcuts"), text);
    });
}

// ── Tool bar ──────────────────────────────────────────────────────────────────

void MainWindow::setupToolBar() {
    auto* tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(16, 16));
    setRunState(false);  // seeds Run icon before toolbar renders
    tb->addAction(act_step_);
    tb->addAction(act_run_pause_);
    tb->addAction(act_reset_);
    tb->addSeparator();
    tb->addAction(act_open_);
}

// ── Central widget ────────────────────────────────────────────────────────────

void MainWindow::setupCentralWidget() {
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    dock_manager_ = new ads::CDockManager(this);

    datapath_widget_ = new SchematicDatapathWidget(this);
    register_widget_ = new RegisterWidget(this);
    memory_widget_   = new MemoryWidget(this);
    trace_widget_    = new PipelineTraceWidget(this);

    // Helper: create a named dock and register its View > Panels toggle.
    auto make_dock = [&](const QString& title, QWidget* contents) -> ads::CDockWidget* {
        auto* dock = new ads::CDockWidget(dock_manager_, title);
        dock->setObjectName(title);
        dock->setWidget(contents);
        panels_menu_->addAction(dock->toggleViewAction());
        return dock;
    };

    // ── 2-column IDE layout ───────────────────────────────────────────────────
    //
    //  ┌─ Code Editor ─┬─────── Datapath ──────────┐
    //  │  (edit code,  │  (pipeline visualization,  │
    //  │  assemble,    │  stage highlights,          │
    //  │  load)        │  breakpoints)               │
    //  │               ├── Registers ─┬─ Memory ────┤
    //  │               │  (live vals) │ (hex dump)  │
    //  └───────────────┴─────────────┴─────────────-┘
    //                                 Pipeline Trace / Statistics also tabbed bottom-right.
    //
    // Panels can be dragged, split, floated, or hidden via View > Panels.
    // The arrangement is saved on close and restored defensively below.

    // Code editor — left column (narrower; user writes and loads programs here).
    auto* code_dock = make_dock(tr("Code Editor"), createCodeEditorTab());
    auto* left_area = dock_manager_->addDockWidget(ads::LeftDockWidgetArea, code_dock);
    (void)left_area;

    // Datapath — center, largest panel. Shown first so new users see the
    // pipeline visualization immediately after loading and running a program.
    auto* dp_dock     = make_dock(tr("Datapath"), datapath_widget_);
    auto* center_area = dock_manager_->addDockWidget(ads::CenterDockWidgetArea, dp_dock);

    // Inspector row — tabbed below the Datapath. Registers is the default tab
    // because it updates every cycle and gives the most direct feedback.
    auto* reg_dock = make_dock(tr("Registers"), register_widget_);
    auto* bottom_area =
        dock_manager_->addDockWidget(ads::BottomDockWidgetArea, reg_dock, center_area);
    auto* mem_dock = make_dock(tr("Memory"), memory_widget_);
    dock_manager_->addDockWidgetTabToArea(mem_dock, bottom_area);
    auto* trace_dock = make_dock(tr("Pipeline Trace"), trace_widget_);
    dock_manager_->addDockWidgetTabToArea(trace_dock, bottom_area);
    auto* stats_dock = make_dock(tr("Statistics"), createStatisticsTab());
    dock_manager_->addDockWidgetTabToArea(stats_dock, bottom_area);
    bottom_area->setCurrentIndex(0);  // Registers in front

    // Snapshot the default arrangement so resetLayout() and the fallback
    // restore can reinstate it without rebuilding the docks from scratch.
    default_layout_ = dock_manager_->saveState(kLayoutVersion);

    // Restore the previous session's layout defensively. ADS rejects layouts
    // tagged with a different kLayoutVersion, so schema changes cleanly
    // discard stale states instead of rebuilding a broken window. If the
    // restore leaves every dock closed (degenerate empty window), fall back.
    const QSettings s("nsc-qt", "clearCore-gui");
    const auto      saved = s.value("dockLayout").toByteArray();
    if (!saved.isEmpty()) {
        if (!dock_manager_->restoreState(saved, kLayoutVersion) || !hasOpenPanel())
            dock_manager_->restoreState(default_layout_, kLayoutVersion);
    } else {
        // Fresh install — pre-load "Hello registers" so the editor isn't blank
        // on first launch and new users can immediately click Assemble → Load → Step.
        const auto& catalog = exampleProgramCatalog();
        if (!catalog.empty()) code_editor_->setPlainText(catalog[0].source);
    }

    // Status bar
    status_cycles_lbl_ = new QLabel(tr("Cycles: 0"));
    status_instrs_lbl_ = new QLabel(tr("Instructions: 0"));
    status_cpi_lbl_    = new QLabel(tr("CPI: —"));
    statusBar()->addPermanentWidget(status_cycles_lbl_);
    statusBar()->addPermanentWidget(new QLabel("|"));
    statusBar()->addPermanentWidget(status_instrs_lbl_);
    statusBar()->addPermanentWidget(new QLabel("|"));
    statusBar()->addPermanentWidget(status_cpi_lbl_);
    statusBar()->showMessage(tr("Ready — load a program or click Assemble to get started"), 5000);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s("nsc-qt", "clearCore-gui");
    s.setValue("dockLayout", dock_manager_->saveState(kLayoutVersion));
    QMainWindow::closeEvent(event);
}

// View > Reset Layout: restore the default arrangement captured on first build.
void MainWindow::resetLayout() {
    if (!default_layout_.isEmpty()) dock_manager_->restoreState(default_layout_, kLayoutVersion);
}

void MainWindow::setRunState(bool running) {
    if (running) {
        act_run_pause_->setText(tr("&Pause"));
        act_run_pause_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    } else {
        act_run_pause_->setText(tr("&Run"));
        act_run_pause_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    }
}

bool MainWindow::hasOpenPanel() const {
    const auto docks = dock_manager_->dockWidgetsMap();
    for (auto* dock : docks)
        if (dock != nullptr && !dock->isClosed()) return true;
    return false;
}

QWidget* MainWindow::createCodeEditorTab() {
    auto* w  = new QWidget;
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    code_editor_ = new CodeEditor(w);
    code_editor_->setFont(scale::monoFont(scale::kFontSizeBody));
    code_editor_->setPlaceholderText(tr("# MIPS assembly\n"
                                        "# Example:\n"
                                        "#   addi $t0, $zero, 5\n"
                                        "#   addi $t1, $zero, 10\n"
                                        "#   add  $t2, $t0, $t1\n"));
    vl->addWidget(code_editor_);

    // Button toolbar row — given a named container so the QSS can set its
    // background independently of the ADS dock area that hosts this panel.
    auto* btn_bar = new QWidget(w);
    btn_bar->setObjectName("codeEditorBtnBar");
    btn_bar->setAutoFillBackground(true);
    auto* btn_row = new QHBoxLayout(btn_bar);
    btn_row->setContentsMargins(8, 6, 8, 6);
    btn_row->setSpacing(8);
    vl->addWidget(btn_bar);

    auto* asm_btn = new QPushButton(tr("Assemble"), w);
    asm_btn->setObjectName("primaryButton");
    asm_btn->setToolTip(tr("Assemble source and prepare machine code (does not load)"));
    asm_btn->setShortcut(QKeySequence("Ctrl+Return"));

    auto* load_btn = new QPushButton(tr("Load"), w);
    load_btn->setToolTip(tr("Load assembled program into simulator and reset (Ctrl+Shift+Return)"));
    load_btn->setShortcut(QKeySequence("Ctrl+Shift+Return"));

    // Examples dropdown -- a short, curated list (Hick's Law) chosen to
    // demonstrate the pipeline behavior this simulator actually visualizes,
    // rather than generic assembly samples. Index 0 is a non-selectable
    // placeholder; connecting to activated() rather than currentIndexChanged()
    // means programmatic setCurrentIndex(0) resets below don't re-trigger it.
    examples_combo_ = new QComboBox(w);
    examples_combo_->setFont(scale::monoFont(scale::kFontSizeBody));
    examples_combo_->addItem(tr("Load example…"));
    for (const auto& ex : exampleProgramCatalog())
        examples_combo_->addItem(ex.name);
    examples_combo_->setToolTip(tr("Load a curated example program into the editor"));
    connect(examples_combo_, qOverload<int>(&QComboBox::activated), this,
            &MainWindow::onExampleSelected);

    asm_status_lbl_ = new QLabel(btn_bar);
    asm_status_lbl_->setFont(scale::monoFont(scale::kFontSizeBody));
    btn_row->addWidget(asm_btn);
    btn_row->addWidget(load_btn);
    btn_row->addSpacing(12);
    btn_row->addWidget(examples_combo_);
    btn_row->addSpacing(12);
    btn_row->addWidget(asm_status_lbl_);
    btn_row->addStretch();

    connect(asm_btn, &QPushButton::clicked, this, &MainWindow::onAssemble);
    connect(load_btn, &QPushButton::clicked, this, &MainWindow::onLoad);
    return w;
}

void MainWindow::onExampleSelected(int idx) {
    if (idx <= 0) return;  // "Load example…" placeholder, not a real entry

    const auto& catalog = exampleProgramCatalog();
    const auto& ex      = catalog[static_cast<std::size_t>(idx - 1)];

    if (!code_editor_->toPlainText().trimmed().isEmpty()) {
        const auto reply = QMessageBox::question(
            this, tr("Replace Current Code?"),
            tr("Loading \"%1\" will replace the code currently in the editor. Continue?")
                .arg(ex.name),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) {
            examples_combo_->setCurrentIndex(0);
            return;
        }
    }

    code_editor_->setPlainText(ex.source);
    assembled_words_.clear();
    asm_status_lbl_->setStyleSheet("");
    asm_status_lbl_->setText(tr("Loaded example: %1").arg(ex.name));
    examples_combo_->setCurrentIndex(0);  // reset so the same example can be re-picked later
}

QWidget* MainWindow::createStatisticsTab() {
    auto* w = new QWidget;
    w->setAutoFillBackground(true);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(12);

    // ── KPI card row ──────────────────────────────────────────────────────────
    // Three headline metrics in large-number "cards" for immediate readability.
    // The CPI card background is color-coded (green/orange/red) in onStatisticsUpdated()
    // and stored in stat_cpi_card_ for later updates. Values are mouse-selectable
    // so students can copy numbers into lab reports without retyping.
    auto mkCard = [&](const QString& label, QLabel*& val_ptr) -> QFrame* {
        auto* card = new QFrame(w);
        card->setFrameShape(QFrame::StyledPanel);
        card->setObjectName("statCard");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 10, 12, 10);
        cl->setSpacing(4);

        val_ptr = new QLabel("—", card);
        val_ptr->setAlignment(Qt::AlignCenter);
        val_ptr->setFont(scale::monoFont(26, true));
        val_ptr->setTextInteractionFlags(Qt::TextSelectableByMouse);

        auto* name_lbl = new QLabel(label, card);
        name_lbl->setAlignment(Qt::AlignCenter);
        name_lbl->setObjectName("statCardLabel");
        name_lbl->setFont(scale::monoFont(scale::kFontSizeDense));

        cl->addWidget(val_ptr);
        cl->addWidget(name_lbl);
        return card;
    };

    auto* kpi_row = new QHBoxLayout;
    kpi_row->setSpacing(8);
    kpi_row->addWidget(mkCard(tr("Cycles"), stat_cycles_lbl_), 1);
    kpi_row->addWidget(mkCard(tr("Instructions"), stat_instrs_lbl_), 1);
    auto* cpi_card = mkCard(tr("CPI"), stat_cpi_lbl_);
    cpi_card->setToolTip(tr("Cycles Per Instruction — lower is better\n"
                            "1.0 = ideal (no stalls)   >2.0 = heavy pipeline overhead"));
    stat_cpi_card_ = cpi_card;
    kpi_row->addWidget(cpi_card, 1);
    vl->addLayout(kpi_row);

    // ── Pipeline events ────────────────────────────────────────────────────────
    auto* pipe_box = new QGroupBox(tr("Pipeline Events"), w);
    auto* pipe_fl  = new QFormLayout(pipe_box);
    pipe_fl->setContentsMargins(12, 8, 12, 12);
    pipe_fl->setSpacing(8);
    pipe_fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto mkRow = [&](const QString& name, QLabel*& ptr, const QString& tip) {
        ptr = new QLabel("—", pipe_box);
        ptr->setFont(scale::monoFont(scale::kFontSizeBody));
        ptr->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ptr->setToolTip(tip);
        pipe_fl->addRow(name, ptr);
    };
    mkRow(tr("Data hazards:"), stat_data_haz_lbl_,
          tr("RAW (Read-After-Write) hazards detected in the Decode stage"));
    mkRow(tr("Control hazards:"), stat_ctrl_haz_lbl_,
          tr("Hazards from branches and jumps that disrupted the pipeline"));
    mkRow(tr("Forwarding events:"), stat_fwd_lbl_,
          tr("Times the forwarding unit bypassed the register file to resolve a data hazard"));
    mkRow(tr("Stalls:"), stat_stalls_lbl_,
          tr("Bubble cycles inserted for hazards that could not be resolved by forwarding"));
    mkRow(tr("Flushes:"), stat_flushes_lbl_,
          tr("Pipeline flushes caused by branch mispredictions or control hazards"));
    vl->addWidget(pipe_box);

    vl->addStretch();
    return w;
}

// ── Connections ───────────────────────────────────────────────────────────────

void MainWindow::setupConnections() {
    connect(controller_.get(), &SimulatorController::cycleExecuted, this,
            &MainWindow::onCycleExecuted);
    connect(controller_.get(), &SimulatorController::pipelineStateChanged, this,
            &MainWindow::onPipelineStateChanged);
    connect(controller_.get(), &SimulatorController::statisticsUpdated, this,
            &MainWindow::onStatisticsUpdated);
    connect(controller_.get(), &SimulatorController::halted, this, &MainWindow::onHalted);
    connect(controller_.get(), &SimulatorController::faulted, this, &MainWindow::onFaulted);
    connect(controller_.get(), &SimulatorController::breakpointHit, this, [this](uint32_t pc) {
        statusBar()->showMessage(tr("Breakpoint hit at 0x%1").arg(pc, 8, 16, QChar('0')), 5000);
        setRunState(false);
    });

    connect(datapath_widget_, &SchematicDatapathWidget::breakpointToggleRequested, this,
            &MainWindow::onBreakpointToggle);
    connect(datapath_widget_, &SchematicDatapathWidget::stageDetailRequested, this,
            &MainWindow::onStageDetailRequested);
}

// ── Slot implementations ──────────────────────────────────────────────────────

void MainWindow::onStep() {
    controller_->stop();
    setRunState(false);
    controller_->stepCycle();
}

void MainWindow::onRunPause() {
    if (controller_->isRunning()) {
        controller_->stop();
        setRunState(false);
    } else {
        controller_->run();
        setRunState(true);
    }
}

void MainWindow::onReset() {
    controller_->stop();
    setRunState(false);
    controller_->reset();
    register_widget_->clear();
    trace_widget_->clear();
    statusBar()->showMessage(tr("Reset."), 2000);
}

void MainWindow::onOpenFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Hex Program"), {}, tr("Hex Programs (*.hex *.txt);;All Files (*)"));
    if (path.isEmpty()) return;

    auto prog = mips::load_hex_file(path.toStdString());
    if (!prog) {
        QMessageBox::critical(this, tr("Load Error"),
                              QString::fromStdString(prog.error.value_or("unknown error")));
        return;
    }

    // Confirm before discarding an in-progress run -- only asked once we
    // have a validated replacement program ready, not before the file
    // picker (audit Critical #2: Load/Open silently wiped simulation state).
    if (controller_->cycleCount() > 0) {
        const auto reply =
            QMessageBox::question(this, tr("Discard Current Run?"),
                                  tr("Loading this program will discard the current simulation "
                                     "progress (%1 cycles executed). Continue?")
                                      .arg(static_cast<qulonglong>(controller_->cycleCount())),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) return;
    }

    onReset();
    if (!controller_->loadProgram(prog.words)) {
        QMessageBox::critical(this, tr("Load Error"), tr("Program too large for memory."));
        return;
    }
    statusBar()->showMessage(tr("Loaded %1 instructions from %2").arg(prog.words.size()).arg(path),
                             4000);
}

void MainWindow::onSaveTrace() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Pipeline Trace"),
                                                      tr("trace.csv"), tr("CSV Files (*.csv)"));
    if (path.isEmpty()) return;

    QMessageBox::information(this, tr("Save Trace"),
                             tr("Trace export not yet implemented for this build."));
}

void MainWindow::onAssemble() {
    const std::string src = code_editor_->toPlainText().toStdString();
    if (src.empty()) {
        asm_status_lbl_->setText(tr("No source to assemble."));
        return;
    }
    auto result = assemble(src);
    if (!result) {
        asm_status_lbl_->setStyleSheet("color: red;");
        // Assembler diagnostics ("line N: message") come from plain-C++
        // parsing code, not Qt UI copy -- not routed through tr().
        asm_status_lbl_->setText(QString::fromStdString(result.error.value_or("error")));
        assembled_words_.clear();
        return;
    }
    assembled_words_ = std::move(result.words);
    asm_status_lbl_->setStyleSheet("color: green;");
    asm_status_lbl_->setText(tr("✓ %1 instructions assembled").arg(assembled_words_.size()));
}

void MainWindow::onLoad() {
    if (assembled_words_.empty()) {
        asm_status_lbl_->setStyleSheet("color: orange;");
        asm_status_lbl_->setText(tr("Assemble first."));
        return;
    }

    // Same discard confirmation as onOpenFile() -- see audit Critical #2.
    if (controller_->cycleCount() > 0) {
        const auto reply =
            QMessageBox::question(this, tr("Discard Current Run?"),
                                  tr("Loading this program will discard the current simulation "
                                     "progress (%1 cycles executed). Continue?")
                                      .arg(static_cast<qulonglong>(controller_->cycleCount())),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) return;
    }

    onReset();
    if (!controller_->loadProgram(assembled_words_)) {
        asm_status_lbl_->setStyleSheet("color: red;");
        asm_status_lbl_->setText(tr("Program too large for memory."));
        return;
    }
    asm_status_lbl_->setStyleSheet("color: green;");
    asm_status_lbl_->setText(tr("✓ %1 instructions loaded").arg(assembled_words_.size()));
}

void MainWindow::onShowPreferences() {
    PreferencesDialog dlg(this);
    connect(&dlg, &PreferencesDialog::colorSchemeChanged, this, &MainWindow::applyColorScheme);
    connect(&dlg, &PreferencesDialog::executionSpeedChanged, controller_.get(),
            &SimulatorController::setExecutionSpeed);
    connect(&dlg, &PreferencesDialog::showRegisterAliasesChanged, register_widget_,
            &RegisterWidget::setShowAliases);
    connect(&dlg, &PreferencesDialog::fontSizeChanged, this, [this](int sz) {
        if (code_editor_) code_editor_->setFont(scale::monoFont(sz));
    });
    dlg.exec();
}

void MainWindow::onCycleExecuted(uint64_t count) {
    status_cycles_lbl_->setText(tr("Cycles: %1").arg(count));
}

void MainWindow::onPipelineStateChanged(mips::PipelineState state) {
    datapath_widget_->setPipelineState(state);
    trace_widget_->updateCycle(state);

    // Gather register values once, then push state + values to RegisterWidget
    // together so it refreshes each of its 32 cells exactly once per cycle.
    std::array<uint32_t, 32> reg_vals{};
    for (int i = 0; i < 32; ++i)
        reg_vals[i] = controller_->registerValue(static_cast<uint8_t>(i));
    register_widget_->updateCycle(state, reg_vals);

    // Refresh memory
    memory_widget_->updateDisplay(controller_->memory());
}

void MainWindow::onStatisticsUpdated(nsc::qt::SimulatorStatistics stats) {
    status_instrs_lbl_->setText(
        tr("Instructions: %1").arg(static_cast<qulonglong>(stats.instructions_retired)));
    const double cpi = stats.cpi();
    status_cpi_lbl_->setText(cpi > 0 ? tr("CPI: %1").arg(cpi, 0, 'f', 2) : tr("CPI: —"));

    stat_cycles_lbl_->setText(QString::number(stats.cycles_executed));
    stat_instrs_lbl_->setText(QString::number(stats.instructions_retired));

    if (cpi > 0) {
        stat_cpi_lbl_->setText(QString::number(cpi, 'f', 2));
        const QColor text_color = cpi >= 2.0   ? QColor("#F44336")
                                  : cpi >= 1.5 ? QColor("#FF9800")
                                               : QColor("#4CAF50");
        stat_cpi_lbl_->setStyleSheet(
            QString("color: %1; font-weight: bold;").arg(text_color.name()));
        // Color the card background to give a glanceable health signal even
        // when the user isn't reading the exact number.
        if (stat_cpi_card_) {
            const QColor bg  = dark_mode_ ? (cpi >= 2.0   ? QColor("#3D1515")
                                             : cpi >= 1.5 ? QColor("#3D2D10")
                                                          : QColor("#152D15"))
                                          : (cpi >= 2.0   ? QColor("#FFEBEE")
                                             : cpi >= 1.5 ? QColor("#FFF3E0")
                                                          : QColor("#E8F5E9"));
            const QColor bdr = dark_mode_ ? (cpi >= 2.0   ? QColor("#7C2020")
                                             : cpi >= 1.5 ? QColor("#7C5810")
                                                          : QColor("#1E7C1E"))
                                          : (cpi >= 2.0   ? QColor("#EF9A9A")
                                             : cpi >= 1.5 ? QColor("#FFCC80")
                                                          : QColor("#A5D6A7"));
            stat_cpi_card_->setStyleSheet(
                QString(
                    "QFrame#statCard { background: %1; border: 1px solid %2; border-radius: 6px; }")
                    .arg(bg.name(), bdr.name()));
        }
    } else {
        stat_cpi_lbl_->setText("—");
        stat_cpi_lbl_->setStyleSheet("");
        if (stat_cpi_card_) stat_cpi_card_->setStyleSheet("");
    }

    stat_data_haz_lbl_->setText(QString::number(stats.data_hazards));
    stat_ctrl_haz_lbl_->setText(QString::number(stats.control_hazards));
    stat_fwd_lbl_->setText(QString::number(stats.forwarding_events));
    stat_stalls_lbl_->setText(QString::number(stats.stalls));
    stat_flushes_lbl_->setText(QString::number(stats.flushes));
}

void MainWindow::onHalted() {
    controller_->stop();
    setRunState(false);
    flashStatusBanner(true, tr("✓ Program halted (spin-loop detected)."));
}

void MainWindow::onFaulted() {
    controller_->stop();
    setRunState(false);
    flashStatusBanner(false, tr("✗ Processor fault — check your program."));
}

void MainWindow::flashStatusBanner(bool success, const QString& text) {
    // Halt (success) and fault used to be delivered identically -- a plain
    // status-bar string either way. Give them visually distinct treatment
    // so finishing a program actually feels different from crashing one
    // (audit Opportunity #7).
    const QString bg = success ? "#2E7D32" : "#C62828";  // green / red
    statusBar()->setStyleSheet(
        QString(
            "QStatusBar { background: %1; } QStatusBar QLabel { color: white; font-weight: bold; }")
            .arg(bg));
    statusBar()->showMessage(text, 4000);
    QTimer::singleShot(4000, this, [this] { statusBar()->setStyleSheet(""); });
}

void MainWindow::onBreakpointToggle(uint32_t pc) {
    if (controller_->hasBreakpoint(pc)) {
        controller_->clearBreakpoint(pc);
        statusBar()->showMessage(tr("Breakpoint cleared at 0x%1").arg(pc, 8, 16, QChar('0')), 2000);
    } else {
        controller_->setBreakpoint(pc);
        statusBar()->showMessage(tr("Breakpoint set at 0x%1").arg(pc, 8, 16, QChar('0')), 2000);
    }
    datapath_widget_->setBreakpoints(controller_->breakpoints());
}

void MainWindow::onStageDetailRequested(int stage_index, uint32_t pc, uint32_t raw) {
    using namespace mips;
    auto decoded = Decoder::decode(raw);

    static constexpr const char* STAGES[] = {"IF", "ID", "EX", "MEM", "WB"};
    QDialog                      dlg(this);
    dlg.setWindowTitle(
        tr("Stage Detail — %1").arg(stage_index < 5 ? QString(STAGES[stage_index]) : "?"));
    dlg.setMinimumWidth(340);

    auto* fl = new QFormLayout(&dlg);
    fl->setContentsMargins(16, 16, 16, 16);
    fl->setSpacing(6);

    auto add_row = [&](const QString& key, const QString& val) {
        auto* lbl = new QLabel(val);
        lbl->setFont(scale::monoFont(scale::kFontSizeDense));
        fl->addRow(key, lbl);
    };

    add_row(tr("PC:"), QString("0x%1").arg(pc, 8, 16, QChar('0')).toUpper());
    add_row(tr("Raw word:"), QString("0x%1").arg(raw, 8, 16, QChar('0')).toUpper());

    if (decoded) {
        add_row(tr("Mnemonic:"), QString::fromStdString(std::string(Decoder::mnemonic(*decoded))));
        if (decoded->format == InstrFormat::R) {
            const auto& r = decoded->r();
            add_row(tr("$rs:"),
                    QString("$%1 (%2) = 0x%3")
                        .arg(r.rs)
                        .arg(QString::fromStdString(std::string(register_abi_name(r.rs))))
                        .arg(controller_->registerValue(r.rs), 8, 16, QChar('0')));
            add_row(tr("$rt:"),
                    QString("$%1 (%2) = 0x%3")
                        .arg(r.rt)
                        .arg(QString::fromStdString(std::string(register_abi_name(r.rt))))
                        .arg(controller_->registerValue(r.rt), 8, 16, QChar('0')));
            add_row(tr("$rd:"),
                    QString("$%1 (%2) = 0x%3")
                        .arg(r.rd)
                        .arg(QString::fromStdString(std::string(register_abi_name(r.rd))))
                        .arg(controller_->registerValue(r.rd), 8, 16, QChar('0')));
        } else if (decoded->format == InstrFormat::I) {
            const auto& i = decoded->i();
            add_row(tr("$rs:"),
                    QString("$%1 (%2) = 0x%3")
                        .arg(i.rs)
                        .arg(QString::fromStdString(std::string(register_abi_name(i.rs))))
                        .arg(controller_->registerValue(i.rs), 8, 16, QChar('0')));
            add_row(tr("imm:"), QString("0x%1 (%2)")
                                    .arg(i.imm, 4, 16, QChar('0'))
                                    .arg(static_cast<int16_t>(i.imm)));
        }
    } else {
        add_row(tr("Decode:"), tr("(unknown instruction)"));
    }

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok);
    fl->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::applyColorScheme(bool dark) {
    dark_mode_ = dark;

    // ── Palette (non-styled widgets, tooltips, system colours) ────────────────
    QPalette pal;
    if (dark) {
        pal.setColor(QPalette::Window, QColor(0x1E, 0x1E, 0x1E));
        pal.setColor(QPalette::WindowText, Qt::white);
        pal.setColor(QPalette::Base, QColor(0x2A, 0x2A, 0x2A));
        pal.setColor(QPalette::AlternateBase, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ToolTipBase, QColor(0x25, 0x25, 0x25));
        pal.setColor(QPalette::ToolTipText, Qt::white);
        pal.setColor(QPalette::Text, Qt::white);
        pal.setColor(QPalette::Button, QColor(0x3C, 0x3C, 0x3C));
        pal.setColor(QPalette::ButtonText, Qt::white);
        pal.setColor(QPalette::BrightText, Qt::red);
        // 3D/bevel roles — set explicitly so ADS's palette(light/dark/mid) selectors
        // resolve to our dark-theme values rather than the system palette's defaults.
        pal.setColor(QPalette::Light, QColor(0x3C, 0x3C, 0x3C));
        pal.setColor(QPalette::Midlight, QColor(0x2D, 0x2D, 0x2D));
        pal.setColor(QPalette::Dark, QColor(0x14, 0x14, 0x14));
        pal.setColor(QPalette::Mid, QColor(0x22, 0x22, 0x22));
        pal.setColor(QPalette::Shadow, Qt::black);
        pal.setColor(QPalette::PlaceholderText, QColor(0x66, 0x66, 0x66));
        pal.setColor(QPalette::Highlight, QColor(0x26, 0x4F, 0x78));
        pal.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        pal.setColor(QPalette::Window, QColor(0xF5, 0xF5, 0xF5));
        pal.setColor(QPalette::WindowText, Qt::black);
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xF5, 0xF5, 0xF5));
        pal.setColor(QPalette::ToolTipBase, Qt::white);
        pal.setColor(QPalette::ToolTipText, Qt::black);
        pal.setColor(QPalette::Text, Qt::black);
        pal.setColor(QPalette::Button, QColor(0xEE, 0xEE, 0xEE));
        pal.setColor(QPalette::ButtonText, Qt::black);
        // 3D/bevel roles — critical: ADS uses palette(light) as CDockWidget background.
        // Setting Light=white here means that background resolves to white in light mode
        // rather than inheriting whatever the system's dark theme has for QPalette::Light.
        pal.setColor(QPalette::Light, Qt::white);
        pal.setColor(QPalette::Midlight, QColor(0xF8, 0xF8, 0xF8));
        pal.setColor(QPalette::Dark, QColor(0xBE, 0xBE, 0xBE));
        pal.setColor(QPalette::Mid, QColor(0xD0, 0xD0, 0xD0));
        pal.setColor(QPalette::Shadow, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::PlaceholderText, QColor(0x99, 0x99, 0x99));
        pal.setColor(QPalette::Highlight, QColor(0x00, 0x78, 0xD4));
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

QComboBox {
    background: #3C3C3C;
    color: #CCCCCC;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 4px 8px 4px 10px;
    min-width: 80px;
}
QComboBox:hover { border-color: #777777; }
QComboBox QAbstractItemView {
    background: #252526;
    color: #CCCCCC;
    border: 1px solid #454545;
    selection-background-color: #094771;
    selection-color: white;
}

QWidget#codeEditorBtnBar {
    background: #252526;
    border-top: 1px solid #3C3C3C;
}

QFrame#statCard {
    background: #2D2D2D;
    border: 1px solid #3C3C3C;
    border-radius: 6px;
}
QFrame#statCard QLabel { background: transparent; }
QLabel#statCardLabel { color: #888888; }
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

QComboBox {
    background: white;
    color: #333333;
    border: 1px solid #CCCCCC;
    border-radius: 3px;
    padding: 4px 8px 4px 10px;
    min-width: 80px;
}
QComboBox:hover { border-color: #AAAAAA; }
QComboBox QAbstractItemView {
    background: white;
    color: #333333;
    border: 1px solid #CCCCCC;
    selection-background-color: #0078D4;
    selection-color: white;
}

QWidget#codeEditorBtnBar {
    background: #F0F0F0;
    border-top: 1px solid #DDDDDD;
}

QFrame#statCard {
    background: white;
    border: 1px solid #E0E0E0;
    border-radius: 6px;
}
QFrame#statCard QLabel { background: transparent; }
QLabel#statCardLabel { color: #777777; }
)";
    qApp->setStyleSheet(qss);

    // ADS calls dock_manager_->setStyleSheet(its_built_in_css) during
    // construction, which takes precedence over qApp->setStyleSheet() for
    // all ads--* selectors. The only way to theme ADS elements is to append
    // our overrides onto its own stylesheet. We cache the ADS base CSS once
    // on the first call (after dock_manager_ is created) so subsequent
    // scheme switches always start from the unmodified original.
    if (dock_manager_) {
        static const QString ads_base  = dock_manager_->styleSheet();
        const QString        ads_theme = dark ? R"(
ads--CDockContainerWidget { background: #1E1E1E; }
ads--CDockContainerWidget ads--CDockSplitter::handle { background: #3C3C3C; }
ads--CDockAreaWidget      { background: #1E1E1E; }
ads--CDockAreaTitleBar    { background: #252526; border-bottom: 1px solid #3C3C3C; }
ads--CDockWidgetTab {
    background: #2D2D2D;
    border-color: #3C3C3C;
    padding: 4px 16px;
}
ads--CDockWidgetTab:hover { background: #383838; }
ads--CDockWidgetTab[activeTab="true"] {
    background: #1E1E1E;
    border-top: 2px solid #007ACC;
}
ads--CDockWidgetTab QLabel           { color: #888888; }
ads--CDockWidgetTab[activeTab="true"] QLabel { color: #FFFFFF; }
ads--CDockWidget { background: #1E1E1E; border: none; }
QScrollArea#dockWidgetScrollArea { background: transparent; border: none; }
)"
                                              : R"(
ads--CDockContainerWidget { background: #F5F5F5; }
ads--CDockContainerWidget ads--CDockSplitter::handle { background: #D5D5D5; }
ads--CDockAreaWidget      { background: #F5F5F5; }
ads--CDockAreaTitleBar    { background: #E8E8E8; border-bottom: 1px solid #CCCCCC; }
ads--CDockWidgetTab {
    background: #E8E8E8;
    border-color: #D0D0D0;
    padding: 4px 16px;
}
ads--CDockWidgetTab:hover { background: #DCDCDC; }
ads--CDockWidgetTab[activeTab="true"] {
    background: white;
    border-top: 2px solid #0078D4;
}
ads--CDockWidgetTab QLabel           { color: #555555; }
ads--CDockWidgetTab[activeTab="true"] QLabel { color: #1A1A1A; }
ads--CDockWidget { background: white; border: none; }
QScrollArea#dockWidgetScrollArea { background: transparent; border: none; }
)";
        dock_manager_->setStyleSheet(ads_base + ads_theme);
    }

    // Reset the CPI card to the QSS default (color re-applied by onStatisticsUpdated).
    if (stat_cpi_card_) stat_cpi_card_->setStyleSheet("");

    datapath_widget_->setDarkMode(dark);
    register_widget_->setDarkMode(dark);
    memory_widget_->setDarkMode(dark);
    trace_widget_->setDarkMode(dark);
    code_editor_->setDarkMode(dark);
}

}  // namespace nsc::qt
