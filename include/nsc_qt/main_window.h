#pragma once

#include "mips/processor.h"
#include "nsc_qt/simulator_controller.h"
#include <QByteArray>
#include <QMainWindow>
#include <memory>
#include <vector>

class QLabel;
class QComboBox;
class QAction;
class QToolBar;
class QMenu;
class QCloseEvent;

namespace ads {
class CDockManager;
}

namespace nsc::qt {

class SchematicDatapathWidget;
class RegisterWidget;
class MemoryWidget;
class PipelineTraceWidget;
class CodeEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    // Persists the dock layout so panel arrangements survive restarts.
    void closeEvent(QCloseEvent* event) override;

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

    // Restore the default (first-run) dock arrangement — backs View > Reset
    // Layout and the corrupt-state fallback in setupCentralWidget().
    void resetLayout();
    // True if at least one dock panel is open; a restored layout with none is
    // degenerate (an empty window) and must be discarded.
    bool hasOpenPanel() const;
    // Syncs the Run/Pause action text AND icon so every code path that changes
    // run state (Stop menu, Reset, Halt, Fault, Breakpoint) stays consistent.
    void setRunState(bool running);

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
    ads::CDockManager*       dock_manager_ = nullptr;
    QByteArray               default_layout_;             // snapshot of the first-run arrangement
    QMenu*                   panels_menu_     = nullptr;  // View > Panels toggle actions
    SchematicDatapathWidget* datapath_widget_ = nullptr;
    RegisterWidget*          register_widget_ = nullptr;
    MemoryWidget*            memory_widget_   = nullptr;
    PipelineTraceWidget*     trace_widget_    = nullptr;
    CodeEditor*              code_editor_     = nullptr;
    QComboBox*               examples_combo_  = nullptr;
    QLabel*                  asm_status_lbl_  = nullptr;

    // Statistics labels
    QLabel*  stat_cycles_lbl_   = nullptr;
    QLabel*  stat_instrs_lbl_   = nullptr;
    QLabel*  stat_cpi_lbl_      = nullptr;
    QLabel*  stat_data_haz_lbl_ = nullptr;
    QLabel*  stat_ctrl_haz_lbl_ = nullptr;
    QLabel*  stat_fwd_lbl_      = nullptr;
    QLabel*  stat_stalls_lbl_   = nullptr;
    QLabel*  stat_flushes_lbl_  = nullptr;
    QWidget* stat_cpi_card_     = nullptr;  // KPI card widget, re-colored by CPI health

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
