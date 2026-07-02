#pragma once

#include "mips/processor.h"
#include "nsc_qt/simulator_controller.h"
#include <QMainWindow>
#include <memory>

class QTabWidget;
class QLabel;
class QComboBox;
class QAction;
class QToolBar;

namespace nsc::qt {

class DatapathWidget;
class RegisterWidget;
class MemoryWidget;
class PipelineTraceWidget;
class CodeEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onStep();
    void onRunPause();
    void onReset();
    void onOpenFile();
    void onSaveTrace();
    void onAssemble();
    void onLoad();
    void onShowPreferences();
    void onCycleExecuted(uint64_t count);
    void onPipelineStateChanged(mips::PipelineState state);
    void onStatisticsUpdated(nsc::qt::SimulatorStatistics stats);
    void onHalted();
    void onFaulted();
    void onBreakpointToggle(uint32_t pc);
    void onStageDetailRequested(int stage_index, uint32_t pc, uint32_t raw);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupConnections();
    void applyColorScheme(bool dark);

    QWidget* createCodeEditorTab();
    QWidget* createStatisticsTab();

    // Loads example[idx-1] from the catalog into code_editor_, confirming
    // first if the editor already has non-empty content to lose. idx is
    // 1-based because index 0 in the combo box is the "Load example…"
    // placeholder, not a real entry.
    void onExampleSelected(int idx);

    // Shows a temporary colored status-bar banner (green for success, red for
    // failure) so halt and fault are visually distinct instead of two
    // near-identical status-bar strings -- the "peak" moment of finishing a
    // program should feel different from crashing (audit Opportunity #7).
    void flashStatusBanner(bool success, const QString& text);

    // Controller
    std::unique_ptr<SimulatorController> controller_;

    // Widgets
    QTabWidget*          tabs_            = nullptr;
    DatapathWidget*      datapath_widget_ = nullptr;
    RegisterWidget*      register_widget_ = nullptr;
    MemoryWidget*        memory_widget_   = nullptr;
    PipelineTraceWidget* trace_widget_    = nullptr;
    CodeEditor*          code_editor_     = nullptr;
    QComboBox*           examples_combo_  = nullptr;
    QLabel*              asm_status_lbl_  = nullptr;

    // Statistics labels
    QLabel* stat_cycles_lbl_   = nullptr;
    QLabel* stat_instrs_lbl_   = nullptr;
    QLabel* stat_cpi_lbl_      = nullptr;
    QLabel* stat_data_haz_lbl_ = nullptr;
    QLabel* stat_ctrl_haz_lbl_ = nullptr;
    QLabel* stat_fwd_lbl_      = nullptr;
    QLabel* stat_stalls_lbl_   = nullptr;
    QLabel* stat_flushes_lbl_  = nullptr;

    // Status bar labels
    QLabel* status_cycles_lbl_ = nullptr;
    QLabel* status_instrs_lbl_ = nullptr;
    QLabel* status_cpi_lbl_    = nullptr;

    // Actions
    QAction* act_step_      = nullptr;
    QAction* act_run_pause_ = nullptr;
    QAction* act_reset_     = nullptr;
    QAction* act_open_      = nullptr;
    QAction* act_save_      = nullptr;

    bool dark_mode_ = false;

    // Assembled words waiting to be loaded
    std::vector<uint32_t> assembled_words_;
};

}  // namespace nsc::qt