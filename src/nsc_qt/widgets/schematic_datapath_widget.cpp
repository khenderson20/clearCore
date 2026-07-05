#include "nsc_qt/widgets/schematic_datapath_widget.h"
#include "mips/processor.h"
#include "nsc_qt/instr_format.h"
#include "nsc_qt/ui_scale.h"

#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFocusEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QStringList>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nsc::qt {

namespace {

// ── Logical canvas ──────────────────────────────────────────────────────────
// All geometry is authored in this fixed coordinate space; QGraphicsView
// scales it to the widget. Chosen wide enough that the schematic reads at a
// glance, like Ripes' processor tab.
constexpr qreal kSceneW = 1290.0;
constexpr qreal kSceneH = 560.0;

// Stage column x-extents (tint bands + hit testing). Bars live between them.
struct ColumnSpan {
    qreal x0, x1;
};
constexpr ColumnSpan kColumns[5] = {
    {8, 270},      // IF
    {286, 555},    // ID
    {571, 845},    // EX
    {861, 1130},   // MEM
    {1146, 1276},  // WB
};

constexpr qreal kColTop = 44.0, kColBot = 500.0;

const char* const kStageNames[5]   = {"IF", "ID", "EX", "MEM", "WB"};
const char* const kPipeRegNames[4] = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"};

// Input-port scene positions for the four muxes, in select order. The select
// markers snap to these; they must match the wire endpoints in buildScene().
const QPointF kPcMuxPorts[2]   = {{16, 262}, {16, 298}};                // PC+4, branch target
const QPointF kFwdAMuxPorts[3] = {{600, 222}, {600, 238}, {600, 254}};  // reg, EX/MEM, MEM/WB
const QPointF kFwdBMuxPorts[3] = {{600, 292}, {600, 308}, {600, 324}};
const QPointF kWbMuxPorts[2]   = {{1180, 272}, {1180, 298}};  // mem data, ALU result

// Stage tint hues (shared with the old widget's palette).
const QColor kStageLight[5] = {QColor("#E3F2FD"), QColor("#E0F7FA"), QColor("#E8F5E9"),
                               QColor("#FFFDE7"), QColor("#FFEBEE")};
const QColor kStageDark[5]  = {QColor("#0D47A1"), QColor("#006064"), QColor("#1B5E20"),
                               QColor("#F57F17"), QColor("#B71C1C")};

// Theme palette for schematic chrome.
struct Theme {
    QColor bg, comp_fill, comp_border, wire, wire_dim, text, label, flush, stall, fwd_ex, fwd_mem,
        wb;
};
const Theme kLight = {
    QColor("#F5F5F5"), QColor("#FFFFFF"), QColor("#606060"), QColor("#707070"),
    QColor("#C4C4C4"), QColor("#1A1A1A"), QColor("#00529B"), QColor("#D32F2F"),
    QColor("#E65100"), QColor("#FF6F00"), QColor("#7B1FA2"), QColor("#2E7D32"),
};
const Theme kDark = {
    QColor("#1E1E1E"), QColor("#2D2D2D"), QColor("#9A9A9A"), QColor("#A0A0A0"),
    QColor("#4A4A4A"), QColor("#E0E0E0"), QColor("#9CDCFE"), QColor("#FF5252"),
    QColor("#FFB74D"), QColor("#FFA726"), QColor("#CE93D8"), QColor("#81C784"),
};
inline const Theme& theme(bool dark) {
    return dark ? kDark : kLight;
}

}  // anonymous namespace

// ── WireItem ────────────────────────────────────────────────────────────────
// An orthogonal signal wire: an arbitrary painter path plus arrowheads at the
// stated tips and junction dots where a bus splits. `setActive()` switches
// between the dim resting look and a bright highlighted one — that's how
// forwarding paths, the branch flush path, and the write-back loop light up.
class WireItem : public QGraphicsItem {
public:
    struct Tip {
        QPointF at;
        // Unit direction the arrow points: (1,0)=right, (0,1)=down, …
        QPointF dir;
    };

    WireItem(QPainterPath path, std::vector<Tip> tips, std::vector<QPointF> junctions,
             QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), path_(std::move(path)), tips_(std::move(tips)),
          junctions_(std::move(junctions)) {
        setZValue(-1);  // wires under components
    }

    void setColors(const QColor& rest, const QColor& lit) {
        rest_ = rest;
        lit_  = lit;
        update();
    }

    void setActive(bool on) {
        if (active_ == on) return;
        active_ = on;
        update();
    }

    // Thin dashed rendering for control-signal wires.
    void setControlStyle(bool on) {
        control_ = on;
        update();
    }

    QRectF boundingRect() const override { return path_.boundingRect().adjusted(-8, -8, 8, 8); }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        const QColor& c = active_ ? lit_ : rest_;
        const qreal   w = active_ ? 3.0 : (control_ ? 1.0 : 1.6);

        QPen pen(c, w);
        if (control_ && !active_) pen.setStyle(Qt::DashLine);
        pen.setJoinStyle(Qt::MiterJoin);
        p->setRenderHint(QPainter::Antialiasing);

        if (active_) {  // soft glow pass under the lit wire
            QPen glow(QColor(c.red(), c.green(), c.blue(), 70), w + 4, Qt::SolidLine, Qt::RoundCap);
            p->setPen(glow);
            p->setBrush(Qt::NoBrush);
            p->drawPath(path_);
        }
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPath(path_);

        p->setPen(Qt::NoPen);
        p->setBrush(c);
        for (const Tip& t : tips_) {
            const QPointF d = t.dir;
            const QPointF n(-d.y(), d.x());  // normal
            const qreal   len = 7.0, half = 4.0;
            const QPointF base   = t.at - d * len;
            const QPointF tri[3] = {t.at, base + n * half, base - n * half};
            p->drawPolygon(tri, 3);
        }
        for (const QPointF& j : junctions_)
            p->drawEllipse(j, 3.0, 3.0);
    }

private:
    QPainterPath         path_;
    std::vector<Tip>     tips_;
    std::vector<QPointF> junctions_;
    QColor               rest_ = Qt::gray, lit_ = Qt::red;
    bool                 active_ = false, control_ = false;
};

namespace {

// ── Hover-capable component shapes ───────────────────────────────────────────
// Thickens the border while hovered, giving components a tactile feel and
// signalling that the tooltip carries more detail (progressive disclosure).
// The highlight is drawn at paint time on top of the item's current pen —
// never by swapping the pen — so control-signal accents applied by
// applyState() mid-hover are not wiped when the cursor leaves.
template <typename Base> class HoverShape : public Base {
public:
    using Base::Base;

    QRectF boundingRect() const override {
        return Base::boundingRect().adjusted(-1.5, -1.5, 1.5, 1.5);  // room for the halo
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override {
        Base::paint(p, opt, w);
        if (!hovered_) return;
        QPen hp = this->pen();
        hp.setWidthF(hp.widthF() + 1.4);
        p->setPen(hp);
        p->setBrush(Qt::NoBrush);
        if constexpr (std::is_same_v<Base, QGraphicsRectItem>)
            p->drawRect(this->rect());
        else if constexpr (std::is_same_v<Base, QGraphicsEllipseItem>)
            p->drawEllipse(this->rect());
        else
            p->drawPolygon(this->polygon());
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* ev) override {
        hovered_ = true;
        this->update();
        Base::hoverEnterEvent(ev);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* ev) override {
        hovered_ = false;
        this->update();
        Base::hoverLeaveEvent(ev);
    }

private:
    bool hovered_ = false;
};

using HoverRect    = HoverShape<QGraphicsRectItem>;
using HoverEllipse = HoverShape<QGraphicsEllipseItem>;
using HoverPoly    = HoverShape<QGraphicsPolygonItem>;

// ── Component construction helpers ──────────────────────────────────────────
// Components are tagged with data(0)="comp" so the theme pass can restyle
// fill/border/text without keeping a pointer to each one.

void attachLabel(QAbstractGraphicsShapeItem* item, const QRectF& r, const QString& label,
                 const QFont& font, qreal cx_bias = 0.5) {
    auto* txt = new QGraphicsSimpleTextItem(label, item);
    txt->setData(0, QStringLiteral("comp-label"));
    txt->setFont(font);
    const QRectF tb = txt->boundingRect();
    txt->setPos(r.x() + r.width() * cx_bias - tb.width() / 2, r.center().y() - tb.height() / 2);
}

QGraphicsRectItem* addBox(QGraphicsScene* s, const QRectF& r, const QString& label,
                          const QString& tip, int font_px = scale::kFontSizeDense) {
    auto* box = new HoverRect(r);
    box->setData(0, QStringLiteral("comp"));
    box->setAcceptHoverEvents(true);
    box->setToolTip(tip);
    s->addItem(box);
    attachLabel(box, r, label, scale::monoFont(font_px));
    return box;
}

QGraphicsEllipseItem* addEllipse(QGraphicsScene* s, const QRectF& r, const QString& label,
                                 const QString& tip) {
    auto* el = new HoverEllipse(r);
    el->setData(0, QStringLiteral("comp"));
    el->setAcceptHoverEvents(true);
    el->setToolTip(tip);
    s->addItem(el);
    attachLabel(el, r, label, scale::monoFont(scale::kFontSizeDense - 2));
    return el;
}

// Classic ALU / adder pentagon with the input notch on the left.
QGraphicsPolygonItem* addAlu(QGraphicsScene* s, const QRectF& r, const QString& label,
                             const QString& tip) {
    const qreal x = r.x(), y = r.y(), w = r.width(), h = r.height();
    QPolygonF   poly;
    poly << QPointF(x, y) << QPointF(x + w, y + h * 0.30) << QPointF(x + w, y + h * 0.70)
         << QPointF(x, y + h) << QPointF(x, y + h * 0.62) << QPointF(x + w * 0.18, y + h * 0.50)
         << QPointF(x, y + h * 0.38);
    auto* item = new HoverPoly(poly);
    item->setData(0, QStringLiteral("comp"));
    item->setAcceptHoverEvents(true);
    item->setToolTip(tip);
    s->addItem(item);
    attachLabel(item, r, label, scale::monoFont(scale::kFontSizeDense, true), 0.52);
    return item;
}

// Vertical capsule mux.
QGraphicsRectItem* addMux(QGraphicsScene* s, const QRectF& r, const QString& tip) {
    auto* mux = new HoverRect(r);
    mux->setData(0, QStringLiteral("comp"));
    mux->setAcceptHoverEvents(true);
    mux->setToolTip(tip);
    s->addItem(mux);
    attachLabel(mux, r, QStringLiteral("M"), scale::monoFont(scale::kFontSizeDense - 2, true));
    return mux;
}

// Build an orthogonal path through the given points.
QPainterPath ortho(std::initializer_list<QPointF> pts) {
    QPainterPath p;
    bool         first = true;
    for (const QPointF& pt : pts) {
        if (first) {
            p.moveTo(pt);
            first = false;
        } else {
            p.lineTo(pt);
        }
    }
    return p;
}

}  // anonymous namespace

// ── SchematicDatapathWidget ─────────────────────────────────────────────────

SchematicDatapathWidget::SchematicDatapathWidget(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(0, 0, kSceneW, kSceneH, this);
    setScene(scene_);

    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(560, 260);

    // Zoom controls overlaid top-right of the viewport: a visible, clickable
    // alternative to Ctrl+wheel (which nothing on screen advertises).
    auto mk_btn = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFixedSize(26, 26);
        b->setFocusPolicy(Qt::NoFocus);  // keep keyboard focus on the schematic
        return b;
    };
    btn_zoom_in_  = mk_btn(QStringLiteral("+"), tr("Zoom in (Ctrl+Wheel)"));
    btn_zoom_out_ = mk_btn(QStringLiteral("−"), tr("Zoom out (Ctrl+Wheel)"));
    btn_zoom_fit_ = mk_btn(QStringLiteral("⤢"), tr("Zoom to fit"));
    connect(btn_zoom_in_, &QToolButton::clicked, this, [this] { zoomBy(1.2); });
    connect(btn_zoom_out_, &QToolButton::clicked, this, [this] { zoomBy(1.0 / 1.2); });
    connect(btn_zoom_fit_, &QToolButton::clicked, this, [this] {
        user_zoomed_ = false;
        fitSchematic();
    });

    buildScene();
    applyTheme();
    applyState();
}

void SchematicDatapathWidget::buildScene() {
    // ── Stage tint bands + mnemonic labels ──────────────────────────────────
    for (int i = 0; i < 5; ++i) {
        const auto& c = kColumns[i];
        stage_tints_[static_cast<std::size_t>(i)] =
            scene_->addRect(QRectF(c.x0, kColTop, c.x1 - c.x0, kColBot - kColTop));
        stage_tints_[static_cast<std::size_t>(i)]->setZValue(-10);
        stage_tints_[static_cast<std::size_t>(i)]->setPen(Qt::NoPen);

        auto* name = scene_->addSimpleText(QString::fromLatin1(kStageNames[i]),
                                           scale::monoFont(scale::kFontSizeBody, true));
        name->setData(0, QStringLiteral("stage-name"));
        // +14 keeps the row clear of the write-back wire's bottom run (y 510).
        name->setPos((c.x0 + c.x1) / 2 - name->boundingRect().width() / 2, kColBot + 14);

        stage_labels_[static_cast<std::size_t>(i)] =
            scene_->addSimpleText(QString(), scale::monoFont(scale::kFontSizeDense));
        stage_labels_[static_cast<std::size_t>(i)]->setPos((c.x0 + c.x1) / 2, 12);

        // PC of the instruction in this stage, dimmer, under the mnemonic —
        // lets users map schematic columns to Pipeline Trace rows at a glance.
        stage_pc_labels_[static_cast<std::size_t>(i)] =
            scene_->addSimpleText(QString(), scale::monoFont(scale::kFontSizeDense - 3));
        stage_pc_labels_[static_cast<std::size_t>(i)]->setPos((c.x0 + c.x1) / 2, 30);
    }

    // In-scene cycle counter (bottom-right, opposite the legend; the top row
    // belongs to the per-stage mnemonics and must stay clear).
    cycle_text_ = scene_->addSimpleText(QStringLiteral("Cycle 0"),
                                        scale::monoFont(scale::kFontSizeBody, true));
    cycle_text_->setPos(kSceneW - cycle_text_->boundingRect().width() - 20, kSceneH - 28);

    // Wire-colour legend along the bottom: the special wires communicate by
    // colour, so they need a permanent key (never rely on colour alone).
    {
        const char* names[4] = {QT_TR_NOOP("EX/MEM→EX forward"), QT_TR_NOOP("MEM/WB→EX forward"),
                                QT_TR_NOOP("branch flush"), QT_TR_NOOP("write-back")};
        qreal       x        = 20;
        for (const char* name : names) {
            auto* swatch = scene_->addLine(x, kSceneH - 18, x + 24, kSceneH - 18);
            legend_swatches_.push_back(swatch);
            auto* txt = scene_->addSimpleText(tr(name), scale::monoFont(scale::kFontSizeDense - 2));
            txt->setPos(x + 30, kSceneH - 18 - txt->boundingRect().height() / 2);
            legend_texts_.push_back(txt);
            x += 30 + txt->boundingRect().width() + 34;
        }
    }

    // ── Pipeline register bars ───────────────────────────────────────────────
    const qreal bar_xs[4] = {270, 555, 845, 1130};
    for (int i = 0; i < 4; ++i) {
        auto* bar = scene_->addRect(QRectF(bar_xs[i], 70, 16, 420));
        bar->setZValue(1);
        bar->setToolTip(tr("Pipeline register %1 — latches this stage's results on "
                           "every clock edge so the next stage can consume them.\n"
                           "Red = being flushed, amber = frozen by a stall.")
                            .arg(QString::fromLatin1(kPipeRegNames[i])));
        pipe_regs_[static_cast<std::size_t>(i)] = bar;
        auto* lbl = new QGraphicsSimpleTextItem(QString::fromLatin1(kPipeRegNames[i]), bar);
        lbl->setData(0, QStringLiteral("comp-label"));
        lbl->setFont(scale::monoFont(scale::kFontSizeDense - 3, true));
        lbl->setRotation(-90);
        lbl->setPos(bar_xs[i] + 13.5, 280 + lbl->boundingRect().width() / 2);
    }

    // ── IF stage ─────────────────────────────────────────────────────────────
    addMux(scene_, QRectF(16, 250, 20, 60),
           tr("PC-source mux — selects the next PC: PC+4 (sequential) or the "
              "branch/jump target when a branch is taken."));
    pc_box_ = addBox(scene_, QRectF(52, 255, 42, 50), QStringLiteral("PC"), QString());
    addAlu(scene_, QRectF(120, 100, 44, 56), QStringLiteral("+4"),
           tr("PC+4 adder — computes the next sequential instruction address "
              "(each MIPS instruction is 4 bytes)."));
    imem_box_ =
        addBox(scene_, QRectF(120, 235, 116, 105), QStringLiteral("Instr.\nmemory"), QString());

    auto addWire = [&](QPainterPath p, std::vector<WireItem::Tip> tips,
                       std::vector<QPointF> junctions = {}) -> WireItem* {
        auto* w = new WireItem(std::move(p), std::move(tips), std::move(junctions));
        scene_->addItem(w);
        wires_.push_back(w);
        return w;
    };

    // mux → PC → I-Mem, with a tap up to the +4 adder
    addWire(ortho({{36, 280}, {52, 280}}), {{{52, 280}, {1, 0}}});
    addWire(ortho({{94, 280}, {120, 280}}), {{{120, 280}, {1, 0}}});
    addWire(ortho({{104, 280}, {104, 120}, {120, 120}}), {{{120, 120}, {1, 0}}}, {{104, 280}});
    // +4 adder → over the top → PC mux input 0
    addWire(ortho({{164, 128}, {178, 128}, {178, 56}, {10, 56}, {10, 262}, {16, 262}}),
            {{{16, 262}, {1, 0}}});
    // I-Mem → IF/ID
    addWire(ortho({{236, 280}, {270, 280}}), {{{270, 280}, {1, 0}}});

    // ── ID stage ─────────────────────────────────────────────────────────────
    control_box_ = addBox(scene_, QRectF(320, 74, 96, 40), QStringLiteral("Control"), QString());
    regs_box_ = addBox(scene_, QRectF(320, 210, 130, 125), QStringLiteral("Registers"), QString());
    addEllipse(scene_, QRectF(340, 380, 84, 42), QStringLiteral("Sign-\nextend"),
               tr("Sign-extend — widens the 16-bit immediate field to 32 bits, "
                  "replicating the sign bit."));

    // Instruction bus out of IF/ID: vertical spine at x=305 feeding Control,
    // both register read ports, and sign-extend.
    {
        QPainterPath p = ortho({{286, 280}, {305, 280}});
        p.moveTo(305, 94);
        p.lineTo(305, 401);
        p.moveTo(305, 94);
        p.lineTo(320, 94);
        p.moveTo(305, 240);
        p.lineTo(320, 240);
        p.moveTo(305, 290);
        p.lineTo(320, 290);
        p.moveTo(305, 401);
        p.lineTo(340, 401);
        addWire(
            std::move(p),
            {{{320, 94}, {1, 0}}, {{320, 240}, {1, 0}}, {{320, 290}, {1, 0}}, {{340, 401}, {1, 0}}},
            {{305, 280}, {305, 240}, {305, 290}});
    }

    // Register file outputs / sign-extend / control word → ID/EX
    addWire(ortho({{450, 240}, {555, 240}}), {{{555, 240}, {1, 0}}});
    addWire(ortho({{450, 290}, {555, 290}}), {{{555, 290}, {1, 0}}});
    addWire(ortho({{424, 401}, {555, 401}}), {{{555, 401}, {1, 0}}});
    addWire(ortho({{416, 94}, {555, 94}}), {{{555, 94}, {1, 0}}})->setControlStyle(true);

    // ── EX stage ─────────────────────────────────────────────────────────────
    const QString fwd_mux_tip =
        tr("Forwarding mux — feeds the ALU with either the register value from "
           "ID/EX, the EX/MEM bypass (orange), or the MEM/WB bypass (purple). "
           "Forwarding resolves data hazards without stalling.");
    addMux(scene_, QRectF(600, 210, 20, 56), fwd_mux_tip);
    addMux(scene_, QRectF(600, 280, 20, 56), fwd_mux_tip);
    alu_item_ = addAlu(scene_, QRectF(680, 210, 84, 130), QStringLiteral("ALU"), QString());
    addAlu(scene_, QRectF(690, 92, 44, 56), QStringLiteral("+"),
           tr("Branch-target adder — PC+4 plus the sign-extended immediate "
              "shifted left 2 (word-aligned offset)."));

    // ID/EX → forwarding muxes → ALU
    addWire(ortho({{571, 240}, {585, 240}, {585, 222}, {600, 222}}), {{{600, 222}, {1, 0}}});
    addWire(ortho({{620, 238}, {650, 238}, {650, 242}, {680, 242}}), {{{680, 242}, {1, 0}}});
    addWire(ortho({{571, 290}, {585, 290}, {585, 292}, {600, 292}}), {{{600, 292}, {1, 0}}});
    addWire(ortho({{620, 308}, {680, 308}}), {{{680, 308}, {1, 0}}});
    // PC + shifted immediate → branch-target adder
    addWire(ortho({{571, 106}, {690, 106}}), {{{690, 106}, {1, 0}}});
    addWire(ortho({{571, 401}, {660, 401}, {660, 134}, {690, 134}}), {{{690, 134}, {1, 0}}});
    // Control word continues along the top
    addWire(ortho({{571, 94}, {680, 94}}), {})->setControlStyle(true);
    // ALU result / branch target → EX/MEM
    addWire(ortho({{764, 275}, {845, 275}}), {{{845, 275}, {1, 0}}});
    addWire(ortho({{734, 120}, {845, 120}}), {{{845, 120}, {1, 0}}});
    // Store-data pass-through: mux B output also runs to EX/MEM
    addWire(ortho({{650, 308}, {650, 370}, {845, 370}}), {{{845, 370}, {1, 0}}}, {{650, 308}});

    // Forwarding wires (always visible, lit when the hazard unit forwards)
    fwd_exmem_wire_ = addWire(ortho({{845, 420}, {590, 420}, {590, 238}, {600, 238}}),
                              {{{600, 238}, {1, 0}}}, {{590, 308}});
    {
        QPainterPath p = ortho({{590, 308}, {600, 308}});
        // stub drawn as part of the same wire so it lights together
        // (append to fwd wire path is not possible post-hoc; use second wire)
        auto* stub = addWire(std::move(p), {{{600, 308}, {1, 0}}});
        stub->setControlStyle(false);
        // Keep the stub in sync by treating it as part of the EX/MEM wire:
        // applyState() lights both via fwd_exmem_wire_ and this stub pointer.
        fwd_exmem_stub_ = stub;
    }
    fwd_memwb_wire_ = addWire(ortho({{1130, 455}, {578, 455}, {578, 254}, {600, 254}}),
                              {{{600, 254}, {1, 0}}}, {{578, 324}});
    {
        auto* stub      = addWire(ortho({{578, 324}, {600, 324}}), {{{600, 324}, {1, 0}}});
        fwd_memwb_stub_ = stub;
    }

    // ── MEM stage ────────────────────────────────────────────────────────────
    dmem_box_ =
        addBox(scene_, QRectF(910, 230, 125, 125), QStringLiteral("Data\nmemory"), QString());
    addBox(scene_, QRectF(910, 96, 80, 44), QStringLiteral("Branch"),
           tr("Branch decision — compares the operands and, on a taken branch, "
              "redirects the PC to the branch target (red wire) and flushes the "
              "younger instructions behind it."));

    addWire(ortho({{861, 275}, {910, 275}}), {{{910, 275}, {1, 0}}}, {{885, 275}});
    addWire(ortho({{861, 370}, {895, 370}, {895, 320}, {910, 320}}), {{{910, 320}, {1, 0}}});
    addWire(ortho({{861, 120}, {910, 120}}), {{{910, 120}, {1, 0}}});
    // ALU-result bypass to MEM/WB (non-load write-back)
    addWire(ortho({{885, 275}, {885, 415}, {1130, 415}}), {{{1130, 415}, {1, 0}}});
    // D-Mem read data → MEM/WB
    addWire(ortho({{1035, 275}, {1130, 275}}), {{{1130, 275}, {1, 0}}});
    // Branch decision → PC mux (the flush path; lights red on branch_flush)
    branch_wire_ =
        addWire(ortho({{990, 118}, {1056, 118}, {1056, 44}, {4, 44}, {4, 298}, {16, 298}}),
                {{{16, 298}, {1, 0}}});

    // ── WB stage ─────────────────────────────────────────────────────────────
    addMux(scene_, QRectF(1180, 250, 22, 70),
           tr("MemToReg mux — selects what is written back to the register "
              "file: the ALU result or the value loaded from data memory."));
    addWire(ortho({{1146, 275}, {1165, 275}, {1165, 272}, {1180, 272}}), {{{1180, 272}, {1, 0}}});
    addWire(ortho({{1146, 415}, {1172, 415}, {1172, 298}, {1180, 298}}), {{{1180, 298}, {1, 0}}});
    // Write-back loop to the register file write port
    writeback_wire_ =
        addWire(ortho({{1202, 285}, {1230, 285}, {1230, 510}, {300, 510}, {300, 325}, {320, 325}}),
                {{{320, 325}, {1, 0}}});

    // ── Breakpoint markers (bullseye per stage column) ───────────────────────
    for (int i = 0; i < 5; ++i) {
        const auto& c     = kColumns[i];
        auto*       outer = scene_->addEllipse(QRectF(c.x1 - 24, kColTop + 6, 14, 14),
                                               QPen(QColor("#AA0000"), 1), QColor("#E53935"));
        auto*       inner =
            scene_->addEllipse(QRectF(c.x1 - 20, kColTop + 10, 6, 6), Qt::NoPen, QColor("#FFFFFF"));
        outer->setZValue(5);
        inner->setZValue(6);
        bp_markers_[static_cast<std::size_t>(i)] = {outer, inner};
    }

    // ── Mux select-input markers ─────────────────────────────────────────────
    // A dot parked on the input port each mux is currently passing through.
    for (auto& marker : mux_markers_) {
        marker = scene_->addEllipse(QRectF(-4, -4, 8, 8));
        marker->setZValue(6);
        marker->setToolTip(tr("Selected mux input — the dot marks which input "
                              "this mux is passing through this cycle."));
    }

    // ── Pinned wire value labels ─────────────────────────────────────────────
    // Only values the snapshot actually carries: fetch PC, ID immediate, and
    // the WB result read back from the register file after the cycle.
    auto mk_val = [&]() {
        auto* v = scene_->addSimpleText(QString(), scale::monoFont(scale::kFontSizeDense - 3));
        v->setZValue(7);
        return v;
    };
    val_pc_  = mk_val();
    val_imm_ = mk_val();
    val_wb_  = mk_val();

    // ── Step-animation tokens ────────────────────────────────────────────────
    // Small pills that glide along the top of the stage bands on each clock
    // edge, one per instruction that advanced a stage.
    for (auto& tok : tokens_) {
        tok = scene_->addRect(QRectF(0, 0, 26, 12));
        tok->setZValue(9);
        tok->setVisible(false);
    }
    token_anim_ = new QVariantAnimation(this);
    token_anim_->setDuration(240);
    token_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(token_anim_, &QVariantAnimation::finished, this, [this] {
        for (auto* tok : tokens_)
            tok->setVisible(false);
    });

    // ── Hazard explainer chip ────────────────────────────────────────────────
    // Appears the moment a stall or flush happens and says why, in the
    // hazard's colour — turning "the pipeline did something weird" into a
    // teachable moment. Hidden when no hazard is active.
    hazard_chip_ = scene_->addRect(QRectF());
    hazard_chip_->setZValue(11);
    hazard_chip_->setVisible(false);
    hazard_text_ = scene_->addSimpleText(QString(), scale::monoFont(scale::kFontSizeDense, true));
    hazard_text_->setZValue(12);
    hazard_text_->setVisible(false);

    // ── Keyboard focus ring ──────────────────────────────────────────────────
    focus_ring_ = scene_->addRect(QRectF());
    focus_ring_->setZValue(10);
    focus_ring_->setBrush(Qt::NoBrush);
    focus_ring_->setVisible(false);
}

void SchematicDatapathWidget::applyTheme() {
    const Theme& t = theme(dark_mode_);
    scene_->setBackgroundBrush(t.bg);

    // Restyle every tagged component / label.
    const QList<QGraphicsItem*> items = scene_->items();
    for (QGraphicsItem* it : items) {
        const QString tag = it->data(0).toString();
        if (tag == QLatin1String("comp")) {
            if (auto* shape = dynamic_cast<QAbstractGraphicsShapeItem*>(it)) {
                shape->setBrush(t.comp_fill);
                shape->setPen(QPen(t.comp_border, 1.4));
            }
        } else if (tag == QLatin1String("comp-label")) {
            if (auto* txt = dynamic_cast<QGraphicsSimpleTextItem*>(it)) txt->setBrush(t.text);
        } else if (tag == QLatin1String("stage-name")) {
            if (auto* txt = dynamic_cast<QGraphicsSimpleTextItem*>(it)) txt->setBrush(t.label);
        }
    }

    for (WireItem* w : wires_)
        w->setColors(t.wire, t.label);
    // Special wires get their own lit colors.
    fwd_exmem_wire_->setColors(t.wire_dim, t.fwd_ex);
    fwd_exmem_stub_->setColors(t.wire_dim, t.fwd_ex);
    fwd_memwb_wire_->setColors(t.wire_dim, t.fwd_mem);
    fwd_memwb_stub_->setColors(t.wire_dim, t.fwd_mem);
    branch_wire_->setColors(t.wire_dim, t.flush);
    writeback_wire_->setColors(t.wire_dim, t.wb);

    focus_ring_->setPen(QPen(dark_mode_ ? QColor("#4FC3F7") : QColor("#0078D4"), 2, Qt::DashLine));

    // Legend swatches take the special-wire colours; text follows the theme.
    const QColor legend_colors[4] = {t.fwd_ex, t.fwd_mem, t.flush, t.wb};
    for (std::size_t i = 0; i < legend_swatches_.size() && i < 4; ++i) {
        legend_swatches_[i]->setPen(QPen(legend_colors[i], 3));
        legend_texts_[i]->setBrush(t.wire);
    }
    cycle_text_->setBrush(t.label);

    applyState();  // re-derives state-dependent tints for the new theme
}

void SchematicDatapathWidget::applyState() {
    const Theme& t = theme(dark_mode_);

    for (int i = 0; i < 5; ++i) {
        const auto&  snap = state_.stages[static_cast<std::size_t>(i)];
        const QColor tint = dark_mode_ ? kStageDark[i] : kStageLight[i];
        QColor       band = tint;
        // Dark tints are deep, saturated hues; at high alpha they overwhelm
        // the schematic (MEM turned solid brown). Keep them a faint wash.
        band.setAlpha(snap.valid ? (dark_mode_ ? 32 : 120) : (dark_mode_ ? 12 : 45));
        stage_tints_[static_cast<std::size_t>(i)]->setBrush(band);

        // Mnemonic label above the column (Ripes-style). Raw word 0 encodes
        // sll $zero,$zero,0 — the canonical MIPS nop — so print it as "nop".
        auto*   lbl = stage_labels_[static_cast<std::size_t>(i)];
        QString text;
        QColor  color = t.text;
        if (snap.flushed) {
            text  = QStringLiteral("nop (flush)");
            color = t.flush;
        } else if (snap.stalled) {
            text  = QStringLiteral("nop (stall)");
            color = t.stall;
        } else if (snap.valid) {
            text = snap.raw == 0 ? QStringLiteral("nop")
                                 : QString::fromStdString(format_instr(snap.raw));
        } else {
            text  = QStringLiteral("—");
            color = t.wire_dim;
        }
        lbl->setText(text);
        lbl->setBrush(color);
        const auto& c = kColumns[i];
        lbl->setPos((c.x0 + c.x1) / 2 - lbl->boundingRect().width() / 2, 12);

        // PC chip under the mnemonic, matching the Pipeline Trace address column.
        auto* pc_lbl = stage_pc_labels_[static_cast<std::size_t>(i)];
        pc_lbl->setText(snap.valid ? QStringLiteral("0x%1").arg(snap.pc, 8, 16, QChar('0'))
                                   : QString());
        pc_lbl->setBrush(t.wire);
        pc_lbl->setPos((c.x0 + c.x1) / 2 - pc_lbl->boundingRect().width() / 2, 30);

        // Breakpoint bullseye visibility.
        const bool bp = snap.valid && breakpoints_.count(snap.pc) > 0;
        bp_markers_[static_cast<std::size_t>(i)].first->setVisible(bp);
        bp_markers_[static_cast<std::size_t>(i)].second->setVisible(bp);
    }

    cycle_text_->setText(tr("Cycle %1").arg(static_cast<qulonglong>(state_.cycle)));
    cycle_text_->setPos(kSceneW - cycle_text_->boundingRect().width() - 20, kSceneH - 28);

    // Pipeline-register bars: red = flushing, amber = stalled, else neutral.
    for (int i = 0; i < 4; ++i) {
        const auto& snap = state_.stages[static_cast<std::size_t>(i + 1)];
        QColor      fill = t.comp_fill;
        if (snap.flushed)
            fill = t.flush;
        else if (snap.stalled)
            fill = t.stall;
        pipe_regs_[static_cast<std::size_t>(i)]->setBrush(fill);
        pipe_regs_[static_cast<std::size_t>(i)]->setPen(QPen(t.comp_border, 1.4));
    }

    fwd_exmem_wire_->setActive(state_.fwd_ex_to_ex_a || state_.fwd_ex_to_ex_b);
    fwd_exmem_stub_->setActive(state_.fwd_ex_to_ex_b);
    fwd_memwb_wire_->setActive(state_.fwd_mem_to_ex_a || state_.fwd_mem_to_ex_b);
    fwd_memwb_stub_->setActive(state_.fwd_mem_to_ex_b);
    branch_wire_->setActive(state_.branch_flush);

    // Control-signal accents: light the component that is actually working
    // this cycle, derived from the per-stage instruction's control word.
    auto control_for = [](const mips::StageSnapshot& snap) -> mips::Control {
        if (!snap.valid || snap.raw == 0) return {};
        const auto d = mips::Decoder::decode(snap.raw);
        return d ? mips::derive_control(*d) : mips::Control{};
    };
    const mips::Control mem_ctl = control_for(state_.stages[3]);
    const mips::Control wb_ctl  = control_for(state_.stages[4]);

    const QPen base_pen(t.comp_border, 1.4);
    dmem_box_->setPen((mem_ctl.mem_read || mem_ctl.mem_write) ? QPen(t.label, 2.4) : base_pen);
    regs_box_->setPen(wb_ctl.reg_write ? QPen(t.wb, 2.4) : base_pen);
    writeback_wire_->setActive(wb_ctl.reg_write);

    // ── Mux select markers ───────────────────────────────────────────────────
    // Selects are derived from the same signals that light the wires, so the
    // dot and the lit wire always agree.
    auto place_marker = [&](int idx, const QPointF& port, const QColor& color) {
        auto* m = mux_markers_[static_cast<std::size_t>(idx)];
        m->setPos(port);
        m->setBrush(color);
        m->setPen(QPen(t.comp_border, 0.8));
    };
    place_marker(0, kPcMuxPorts[state_.branch_flush ? 1 : 0],
                 state_.branch_flush ? t.flush : t.wire);
    const int sel_a = state_.fwd_ex_to_ex_a ? 1 : state_.fwd_mem_to_ex_a ? 2 : 0;
    const int sel_b = state_.fwd_ex_to_ex_b ? 1 : state_.fwd_mem_to_ex_b ? 2 : 0;
    place_marker(1, kFwdAMuxPorts[sel_a], sel_a == 1 ? t.fwd_ex : sel_a == 2 ? t.fwd_mem : t.wire);
    place_marker(2, kFwdBMuxPorts[sel_b], sel_b == 1 ? t.fwd_ex : sel_b == 2 ? t.fwd_mem : t.wire);
    place_marker(3, kWbMuxPorts[wb_ctl.mem_to_reg ? 0 : 1], wb_ctl.reg_write ? t.wb : t.wire);

    // ── Pinned wire values ───────────────────────────────────────────────────
    const auto& s_if = state_.stages[0];
    val_pc_->setText(s_if.valid ? QStringLiteral("0x%1").arg(s_if.pc, 8, 16, QChar('0'))
                                : QString());
    val_pc_->setBrush(t.label);
    val_pc_->setPos(73 - val_pc_->boundingRect().width() / 2, 312);

    const auto& s_id = state_.stages[1];
    QString     imm_text;
    if (s_id.valid && s_id.raw != 0) {
        if (const auto d = mips::Decoder::decode(s_id.raw)) {
            if (d->format == mips::InstrFormat::I) {
                const auto imm = static_cast<int16_t>(d->i().imm);
                imm_text       = QStringLiteral("imm=%1").arg(imm);
            }
        }
    }
    val_imm_->setText(imm_text);
    val_imm_->setBrush(t.label);
    val_imm_->setPos(490 - val_imm_->boundingRect().width() / 2, 384);

    const auto& s_wb = state_.stages[4];
    QString     wb_text;
    if (s_wb.valid && s_wb.raw != 0 && wb_ctl.reg_write) {
        if (const auto d = mips::Decoder::decode(s_wb.raw)) {
            uint8_t dest = 0;
            if (d->format == mips::InstrFormat::R)
                dest = d->r().rd;
            else if (d->format == mips::InstrFormat::I)
                dest = d->i().rt;
            else if (d->opcode == mips::Opcode::JAL)
                dest = 31;  // jal writes the return address into $ra
            if (dest != 0)
                wb_text =
                    QStringLiteral("$%1 = 0x%2")
                        .arg(QString::fromStdString(std::string(mips::register_abi_name(dest))))
                        .arg(reg_values_[dest], 8, 16, QChar('0'));
        }
    }
    val_wb_->setText(wb_text);
    val_wb_->setBrush(t.wb);
    val_wb_->setPos(700 - val_wb_->boundingRect().width() / 2, 486);

    // ── Hazard explainer chip ────────────────────────────────────────────────
    QString hz;
    QColor  hz_color;
    if (state_.branch_flush) {
        const auto& s_mem2 = state_.stages[3];
        hz                 = tr("Branch taken: %1 — PC redirected, younger instructions flushed")
                                 .arg(s_mem2.valid && s_mem2.raw != 0
                                          ? QString::fromStdString(format_instr(s_mem2.raw))
                                          : QStringLiteral("branch"));
        hz_color           = t.flush;
    } else if (state_.load_stall) {
        const auto& s_ex2 = state_.stages[2];
        hz =
            tr("Load-use hazard: %1 — its data arrives after MEM, so the "
               "dependent instruction waits one cycle")
                .arg(s_ex2.valid && s_ex2.raw != 0 ? QString::fromStdString(format_instr(s_ex2.raw))
                                                   : QStringLiteral("lw"));
        hz_color = t.stall;
    }
    if (hz.isEmpty()) {
        hazard_chip_->setVisible(false);
        hazard_text_->setVisible(false);
    } else {
        hazard_text_->setText(hz);
        hazard_text_->setBrush(hz_color);
        const QRectF tb = hazard_text_->boundingRect();
        const qreal  cx = kSceneW / 2;
        hazard_text_->setPos(cx - tb.width() / 2, 54);
        QColor bg = t.comp_fill;
        bg.setAlpha(235);
        hazard_chip_->setRect(cx - tb.width() / 2 - 10, 50, tb.width() + 20, tb.height() + 8);
        hazard_chip_->setBrush(bg);
        hazard_chip_->setPen(QPen(hz_color, 1.4));
        hazard_chip_->setVisible(true);
        hazard_text_->setVisible(true);
    }

    updateTooltips();
}

void SchematicDatapathWidget::updateTooltips() {
    // Each tooltip = what the unit does (static teaching text) + what it is
    // doing right now (live line derived from the stage snapshots).
    const auto& s_if  = state_.stages[0];
    const auto& s_id  = state_.stages[1];
    const auto& s_ex  = state_.stages[2];
    const auto& s_mem = state_.stages[3];

    auto instr_line = [](const mips::StageSnapshot& s) {
        if (!s.valid) return QString("—");
        if (s.raw == 0) return QStringLiteral("nop");
        return QString::fromStdString(format_instr(s.raw));
    };

    pc_box_->setToolTip(tr("Program Counter — holds the address of the instruction "
                           "being fetched.\nPC = 0x%1")
                            .arg(s_if.pc, 8, 16, QChar('0')));

    imem_box_->setToolTip(tr("Instruction memory — read-only program storage; outputs "
                             "the 32-bit word addressed by PC.\nFetching: %1")
                              .arg(instr_line(s_if)));

    // Control word of the instruction currently being decoded.
    QString asserted = tr("(none)");
    if (s_id.valid && s_id.raw != 0) {
        if (const auto d = mips::Decoder::decode(s_id.raw)) {
            const mips::Control c = mips::derive_control(*d);
            QStringList         sig;
            if (c.reg_write) sig << QStringLiteral("RegWrite");
            if (c.mem_read) sig << QStringLiteral("MemRead");
            if (c.mem_write) sig << QStringLiteral("MemWrite");
            if (c.mem_to_reg) sig << QStringLiteral("MemToReg");
            if (c.alu_src) sig << QStringLiteral("ALUSrc");
            if (c.reg_dst) sig << QStringLiteral("RegDst");
            if (c.branch) sig << QStringLiteral("Branch");
            if (c.jump) sig << QStringLiteral("Jump");
            if (!sig.isEmpty()) asserted = sig.join(QStringLiteral(", "));
        }
    }
    control_box_->setToolTip(tr("Control unit — decodes the ID-stage instruction into the "
                                "control signals that steer the datapath.\nID: %1\nAsserted: %2")
                                 .arg(instr_line(s_id), asserted));

    // Register file: show the ID instruction's source operands with live values.
    QString read_line = tr("(idle)");
    if (s_id.valid && s_id.raw != 0) {
        if (const auto d = mips::Decoder::decode(s_id.raw)) {
            uint8_t rs = 0, rt = 0;
            if (d->format == mips::InstrFormat::R) {
                rs = d->r().rs;
                rt = d->r().rt;
            } else if (d->format == mips::InstrFormat::I) {
                rs = d->i().rs;
                rt = d->i().rt;
            }
            read_line = QStringLiteral("$%1 = 0x%2   $%3 = 0x%4")
                            .arg(QString::fromStdString(std::string(mips::register_abi_name(rs))))
                            .arg(reg_values_[rs], 8, 16, QChar('0'))
                            .arg(QString::fromStdString(std::string(mips::register_abi_name(rt))))
                            .arg(reg_values_[rt], 8, 16, QChar('0'));
        }
    }
    regs_box_->setToolTip(tr("Register file — 32 × 32-bit registers with two read ports "
                             "(ID) and one write port (WB).\nID reads: %1")
                              .arg(read_line));

    alu_item_->setToolTip(tr("ALU — performs the arithmetic/logic operation for the "
                             "EX-stage instruction. Its inputs come through the forwarding "
                             "muxes on the left.\nEX: %1")
                              .arg(instr_line(s_ex)));

    const mips::Control mem_ctl = [&] {
        if (s_mem.valid && s_mem.raw != 0)
            if (const auto d = mips::Decoder::decode(s_mem.raw)) return mips::derive_control(*d);
        return mips::Control{};
    }();
    const QString mem_act = mem_ctl.mem_read    ? tr("reading (load)")
                            : mem_ctl.mem_write ? tr("writing (store)")
                                                : tr("idle");
    dmem_box_->setToolTip(tr("Data memory — the load/store port used in MEM. Only lw/sw "
                             "class instructions touch it.\nMEM: %1 — %2")
                              .arg(instr_line(s_mem), mem_act));
}

// ── Public API ──────────────────────────────────────────────────────────────

void SchematicDatapathWidget::setPipelineState(const mips::PipelineState& state) {
    const mips::PipelineState old = state_;
    state_                        = state;
    applyState();
    startTokenAnimation(old);
}

void SchematicDatapathWidget::setBreakpoints(const std::unordered_set<uint32_t>& bps) {
    breakpoints_ = bps;
    applyState();
}

void SchematicDatapathWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    applyTheme();
}

void SchematicDatapathWidget::setRegisterValues(const std::array<uint32_t, 32>& regs) {
    reg_values_ = regs;
    applyState();  // refreshes the WB value label and operand tooltips
}

void SchematicDatapathWidget::startTokenAnimation(const mips::PipelineState& old_state) {
    // Animate only across a single clock edge; resets and restores snap.
    if (state_.cycle != old_state.cycle + 1) {
        token_anim_->stop();
        for (auto* tok : tokens_)
            tok->setVisible(false);
        return;
    }
    token_anim_->stop();

    const Theme& t = theme(dark_mode_);
    struct Move {
        QGraphicsRectItem* tok;
        qreal              from_x, to_x;
    };
    std::vector<Move> moves;

    auto            col_center = [](int i) { return (kColumns[i].x0 + kColumns[i].x1) / 2 - 13; };
    constexpr qreal kTokenY    = 48;

    for (int i = 0; i < 5; ++i) {
        const auto& now = state_.stages[static_cast<std::size_t>(i)];
        if (!now.valid || now.stalled || now.flushed) continue;

        qreal from_x = 0;
        if (i == 0) {
            // Fresh fetch: slide in from the left edge of the IF column.
            const auto& before = old_state.stages[0];
            if (before.valid && before.pc == now.pc) continue;  // IF held (stall)
            from_x = kColumns[0].x0;
        } else {
            // Advanced: this instruction was in the previous column last cycle.
            const auto& prev = old_state.stages[static_cast<std::size_t>(i - 1)];
            if (!prev.valid || prev.pc != now.pc || prev.raw != now.raw) continue;
            from_x = col_center(i - 1);
        }

        auto* tok = tokens_[static_cast<std::size_t>(i)];
        tok->setBrush(dark_mode_ ? kStageDark[i] : kStageLight[i]);
        tok->setPen(QPen(t.comp_border, 1));
        tok->setPos(from_x, kTokenY);
        tok->setVisible(true);
        moves.push_back({tok, from_x, col_center(i)});
    }
    if (moves.empty()) return;

    disconnect(token_anim_, &QVariantAnimation::valueChanged, this, nullptr);
    token_anim_->setStartValue(0.0);
    token_anim_->setEndValue(1.0);
    connect(token_anim_, &QVariantAnimation::valueChanged, this, [this, moves](const QVariant& v) {
        const qreal p = v.toReal();
        for (const Move& m : moves)
            m.tok->setPos(m.from_x + (m.to_x - m.from_x) * p, 48);
    });
    token_anim_->start();
}

// ── Interaction ─────────────────────────────────────────────────────────────

int SchematicDatapathWidget::stageAtViewPos(const QPoint& pos) const {
    const QPointF sp = mapToScene(pos);
    if (sp.y() < kColTop || sp.y() > kColBot) return -1;
    for (int i = 0; i < 5; ++i)
        if (sp.x() >= kColumns[i].x0 && sp.x() <= kColumns[i].x1) return i;
    return -1;
}

void SchematicDatapathWidget::fitSchematic() {
    if (!user_zoomed_) fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void SchematicDatapathWidget::zoomBy(qreal factor) {
    user_zoomed_       = true;
    const qreal cur    = transform().m11();
    const qreal target = std::clamp(cur * factor, 0.3, 4.0);
    scale(target / cur, target / cur);
}

void SchematicDatapathWidget::wheelEvent(QWheelEvent* ev) {
    // Ctrl+wheel zooms around the cursor; plain wheel scrolls as usual.
    if (ev->modifiers() & Qt::ControlModifier) {
        zoomBy(std::pow(1.15, ev->angleDelta().y() / 120.0));
        ev->accept();
        return;
    }
    QGraphicsView::wheelEvent(ev);
}

void SchematicDatapathWidget::mousePressEvent(QMouseEvent* ev) {
    setFocus(Qt::MouseFocusReason);
    const int idx = stageAtViewPos(ev->pos());
    if (idx >= 0) {
        selected_stage_ = idx;
        if (hasFocus()) focusInEvent(nullptr);  // refresh ring position
    }
    QGraphicsView::mousePressEvent(ev);
}

void SchematicDatapathWidget::mouseDoubleClickEvent(QMouseEvent* ev) {
    const int idx = stageAtViewPos(ev->pos());
    if (idx < 0) return;
    const auto& snap = state_.stages[static_cast<std::size_t>(idx)];
    if (snap.valid) emit stageDetailRequested(idx, snap.pc, snap.raw);
}

void SchematicDatapathWidget::contextMenuEvent(QContextMenuEvent* ev) {
    QMenu menu(this);

    // Stage-specific actions only when the cursor is over a stage with a
    // real instruction; view actions are available everywhere.
    const int idx     = stageAtViewPos(ev->pos());
    QAction*  bp_act  = nullptr;
    QAction*  det_act = nullptr;
    if (idx >= 0 && state_.stages[static_cast<std::size_t>(idx)].valid) {
        const auto& snap   = state_.stages[static_cast<std::size_t>(idx)];
        const bool  has_bp = breakpoints_.count(snap.pc) > 0;
        bp_act             = menu.addAction(has_bp ? tr("Clear Breakpoint") : tr("Set Breakpoint"));
        det_act            = menu.addAction(tr("Stage Detail…"));
        menu.addSeparator();
    }
    QAction* fit_act    = menu.addAction(tr("Zoom to Fit"));
    QAction* export_act = menu.addAction(tr("Export as Image…"));

    QAction* chosen = menu.exec(ev->globalPos());
    if (chosen == nullptr) return;
    if (chosen == fit_act) {
        user_zoomed_ = false;
        fitSchematic();
    } else if (chosen == export_act) {
        exportImage();
    } else if (chosen == bp_act) {
        emit breakpointToggleRequested(state_.stages[static_cast<std::size_t>(idx)].pc);
    } else if (chosen == det_act) {
        const auto& snap = state_.stages[static_cast<std::size_t>(idx)];
        emit        stageDetailRequested(idx, snap.pc, snap.raw);
    }
}

void SchematicDatapathWidget::exportImage() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Schematic"),
        QStringLiteral("datapath-cycle-%1.png").arg(static_cast<qulonglong>(state_.cycle)),
        tr("PNG Images (*.png)"));
    if (path.isEmpty()) return;

    // 2x supersampling so wire and label detail survives in lab reports.
    QImage img(static_cast<int>(kSceneW) * 2, static_cast<int>(kSceneH) * 2,
               QImage::Format_ARGB32_Premultiplied);
    img.fill(theme(dark_mode_).bg);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    scene_->render(&p);
    p.end();

    if (!img.save(path))
        QMessageBox::warning(this, tr("Export Failed"),
                             tr("Could not write the image to:\n%1").arg(path));
}

void SchematicDatapathWidget::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
    case Qt::Key_Left:
        selected_stage_ = (selected_stage_ + 4) % 5;
        focusInEvent(nullptr);
        ev->accept();
        return;
    case Qt::Key_Right:
        selected_stage_ = (selected_stage_ + 1) % 5;
        focusInEvent(nullptr);
        ev->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const auto& snap = state_.stages[static_cast<std::size_t>(selected_stage_)];
        if (snap.valid) emit stageDetailRequested(selected_stage_, snap.pc, snap.raw);
        ev->accept();
        return;
    }
    case Qt::Key_Space:
    case Qt::Key_B: {
        const auto& snap = state_.stages[static_cast<std::size_t>(selected_stage_)];
        if (snap.valid) emit breakpointToggleRequested(snap.pc);
        ev->accept();
        return;
    }
    default:
        QGraphicsView::keyPressEvent(ev);
    }
}

void SchematicDatapathWidget::resizeEvent(QResizeEvent* ev) {
    QGraphicsView::resizeEvent(ev);
    fitSchematic();
    // Keep the zoom buttons pinned to the top-right of the viewport.
    const int x = viewport()->width() - 30;
    btn_zoom_in_->move(x, 6);
    btn_zoom_out_->move(x, 34);
    btn_zoom_fit_->move(x, 62);
}

void SchematicDatapathWidget::focusInEvent(QFocusEvent* ev) {
    if (ev) QGraphicsView::focusInEvent(ev);
    const auto& c = kColumns[selected_stage_];
    focus_ring_->setRect(QRectF(c.x0 - 3, kColTop - 3, (c.x1 - c.x0) + 6, (kColBot - kColTop) + 6));
    focus_ring_->setVisible(true);
}

void SchematicDatapathWidget::focusOutEvent(QFocusEvent* ev) {
    QGraphicsView::focusOutEvent(ev);
    focus_ring_->setVisible(false);
}

}  // namespace nsc::qt
