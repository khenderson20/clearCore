#pragma once

// ── schematic_datapath_widget.h ─────────────────────────────────────────────
// Ripes-style circuit-schematic view of the 5-stage MIPS pipeline, built on
// QGraphicsView/QGraphicsScene (the same architecture Ripes' VSRTL renderer
// uses). Functional units (PC, memories, register file, ALU, muxes, adders)
// are QGraphicsItems laid out in a fixed logical coordinate space; wires are
// orthogonal painter paths with arrowheads and junction dots.
//
// Live state binding (setPipelineState):
//   • a mnemonic label above each stage column (like Ripes' top labels)
//   • pipeline-register bars tint red on flush / amber on stall
//   • forwarding wires light up when the fwd_* flags are set
//   • the branch-target wire back to the PC mux lights red on branch_flush
//
// Public API and signals mirror DatapathWidget exactly so MainWindow can swap
// between the two with a one-line change.

#include "mips/processor.h"

#include <QGraphicsView>
#include <array>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

class QAbstractGraphicsShapeItem;
class QGraphicsScene;
class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QVariantAnimation;

namespace nsc::qt {

class WireItem;

class SchematicDatapathWidget : public QGraphicsView {
    Q_OBJECT

public:
    explicit SchematicDatapathWidget(QWidget* parent = nullptr);

    void setPipelineState(const mips::PipelineState& state);
    void setBreakpoints(const std::unordered_set<uint32_t>& bps);
    void setDarkMode(bool dark);
    // Live register-file contents, used to enrich component tooltips with the
    // actual operand values of the instructions in flight.
    void setRegisterValues(const std::array<uint32_t, 32>& regs);

signals:
    void breakpointToggleRequested(uint32_t pc);
    void stageDetailRequested(int stage_index, uint32_t pc, uint32_t raw_instr);

protected:
    void wheelEvent(QWheelEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void focusInEvent(QFocusEvent* ev) override;
    void focusOutEvent(QFocusEvent* ev) override;

private:
    // Scene construction (runs once; items are retained and re-themed).
    void buildScene();
    // Push state_ / dark_mode_ / breakpoints_ into the retained items.
    void applyState();
    void applyTheme();

    // Stage column index (0–4) at a view position, or -1.
    int stageAtViewPos(const QPoint& pos) const;
    // Zoom-to-fit while the user hasn't zoomed manually.
    void fitSchematic();
    // Refresh the live parts of the educational component tooltips.
    void updateTooltips();
    // Render the scene to a 2x PNG chosen via a save dialog (lab reports).
    void exportImage();
    // Slide a token from each stage column to the next for instructions that
    // advanced between `old_state` and the current state_ (one clock edge).
    void startTokenAnimation(const mips::PipelineState& old_state);

    QGraphicsScene* scene_ = nullptr;

    // Retained items updated per cycle / theme change.
    std::array<QGraphicsSimpleTextItem*, 5> stage_labels_{};  // mnemonics above columns
    std::array<QGraphicsRectItem*, 5>       stage_tints_{};   // translucent column bands
    std::array<QGraphicsRectItem*, 4>       pipe_regs_{};     // IF/ID … MEM/WB bars
    std::vector<WireItem*>                  wires_;           // all wires (for re-theme)
    WireItem*                               fwd_exmem_wire_ = nullptr;
    WireItem*          fwd_exmem_stub_ = nullptr;  // mux-B branch of the fwd bus
    WireItem*          fwd_memwb_wire_ = nullptr;
    WireItem*          fwd_memwb_stub_ = nullptr;
    WireItem*          branch_wire_    = nullptr;
    WireItem*          writeback_wire_ = nullptr;
    QGraphicsRectItem* focus_ring_     = nullptr;
    // Per-stage breakpoint bullseye (outer ring, punched-out centre).
    std::array<std::pair<QGraphicsEllipseItem*, QGraphicsEllipseItem*>, 5> bp_markers_{};

    // Components with live tooltips / control-signal accents.
    QAbstractGraphicsShapeItem* pc_box_      = nullptr;
    QAbstractGraphicsShapeItem* imem_box_    = nullptr;
    QAbstractGraphicsShapeItem* control_box_ = nullptr;
    QAbstractGraphicsShapeItem* regs_box_    = nullptr;
    QAbstractGraphicsShapeItem* alu_item_    = nullptr;
    QAbstractGraphicsShapeItem* dmem_box_    = nullptr;

    QGraphicsSimpleTextItem*                cycle_text_ = nullptr;  // in-scene cycle counter
    std::array<QGraphicsSimpleTextItem*, 5> stage_pc_labels_{};     // PCs under the mnemonics
    std::vector<QGraphicsLineItem*>         legend_swatches_;       // wire-colour legend
    std::vector<QGraphicsSimpleTextItem*>   legend_texts_;

    // Mux select-input markers: a dot on the input port each mux is passing
    // through this cycle. Index: 0 = PC-source, 1 = fwd A, 2 = fwd B, 3 = WB.
    std::array<QGraphicsEllipseItem*, 4> mux_markers_{};

    // Live value labels pinned to wires whose values are known exactly:
    // the fetch PC, the ID-stage immediate, and the WB write-back result.
    QGraphicsSimpleTextItem* val_pc_  = nullptr;
    QGraphicsSimpleTextItem* val_imm_ = nullptr;
    QGraphicsSimpleTextItem* val_wb_  = nullptr;

    // Step-animation tokens gliding between stage columns on each clock edge.
    std::array<QGraphicsRectItem*, 5> tokens_{};
    QVariantAnimation*                token_anim_ = nullptr;

    mips::PipelineState          state_{};
    std::unordered_set<uint32_t> breakpoints_{};
    std::array<uint32_t, 32>     reg_values_{};
    bool                         dark_mode_      = false;
    bool                         user_zoomed_    = false;
    int                          selected_stage_ = 0;
};

}  // namespace nsc::qt
