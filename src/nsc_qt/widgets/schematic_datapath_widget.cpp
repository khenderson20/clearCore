#include "nsc_qt/widgets/schematic_datapath_widget.h"
#include "nsc_qt/instr_format.h"
#include "nsc_qt/ui_scale.h"

#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nsc::qt {

namespace {

// ── Logical canvas ──────────────────────────────────────────────────────────
// All geometry is authored in this fixed coordinate space; QGraphicsView
// scales it to the widget. Chosen wide enough that the schematic reads at a
// glance, like Ripes' processor tab.
constexpr qreal kSceneW = 1400.0;
constexpr qreal kSceneH = 540.0;

// Stage column x-extents (tint bands + hit testing). Bars live between them.
struct ColumnSpan {
    qreal x0, x1;
};
constexpr ColumnSpan kColumns[5] = {
    {8, 270},      // IF
    {286, 555},    // ID
    {571, 845},    // EX
    {861, 1130},   // MEM
    {1146, 1392},  // WB
};

constexpr qreal kColTop = 44.0, kColBot = 500.0;

const char* const kStageNames[5]   = {"IF", "ID", "EX", "MEM", "WB"};
const char* const kPipeRegNames[4] = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"};

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

// ── Component construction helpers ──────────────────────────────────────────
// Components are plain QGraphicsItems tagged with data(0)="comp" so the theme
// pass can restyle fill/border/text without keeping a pointer to each one.

QGraphicsRectItem* addBox(QGraphicsScene* s, const QRectF& r, const QString& label,
                          int font_px = scale::kFontSizeDense) {
    auto* box = s->addRect(r);
    box->setData(0, QStringLiteral("comp"));
    auto* txt = new QGraphicsSimpleTextItem(label, box);
    txt->setData(0, QStringLiteral("comp-label"));
    txt->setFont(scale::monoFont(font_px));
    const QRectF tb = txt->boundingRect();
    txt->setPos(r.center().x() - tb.width() / 2, r.center().y() - tb.height() / 2);
    return box;
}

QGraphicsEllipseItem* addEllipse(QGraphicsScene* s, const QRectF& r, const QString& label) {
    auto* el = s->addEllipse(r);
    el->setData(0, QStringLiteral("comp"));
    auto* txt = new QGraphicsSimpleTextItem(label, el);
    txt->setData(0, QStringLiteral("comp-label"));
    txt->setFont(scale::monoFont(scale::kFontSizeDense - 2));
    const QRectF tb = txt->boundingRect();
    txt->setPos(r.center().x() - tb.width() / 2, r.center().y() - tb.height() / 2);
    return el;
}

// Classic ALU / adder pentagon with the input notch on the left.
QGraphicsPolygonItem* addAlu(QGraphicsScene* s, const QRectF& r, const QString& label) {
    const qreal x = r.x(), y = r.y(), w = r.width(), h = r.height();
    QPolygonF   poly;
    poly << QPointF(x, y) << QPointF(x + w, y + h * 0.30) << QPointF(x + w, y + h * 0.70)
         << QPointF(x, y + h) << QPointF(x, y + h * 0.62) << QPointF(x + w * 0.18, y + h * 0.50)
         << QPointF(x, y + h * 0.38);
    auto* item = s->addPolygon(poly);
    item->setData(0, QStringLiteral("comp"));
    auto* txt = new QGraphicsSimpleTextItem(label, item);
    txt->setData(0, QStringLiteral("comp-label"));
    txt->setFont(scale::monoFont(scale::kFontSizeDense, true));
    const QRectF tb = txt->boundingRect();
    txt->setPos(x + w * 0.52 - tb.width() / 2, y + h / 2 - tb.height() / 2);
    return item;
}

// Vertical capsule mux.
QGraphicsRectItem* addMux(QGraphicsScene* s, const QRectF& r) {
    auto* mux = s->addRect(r);  // rounded corners drawn via pen radius below
    mux->setData(0, QStringLiteral("comp"));
    auto* txt = new QGraphicsSimpleTextItem(QStringLiteral("M"), mux);
    txt->setData(0, QStringLiteral("comp-label"));
    txt->setFont(scale::monoFont(scale::kFontSizeDense - 2, true));
    const QRectF tb = txt->boundingRect();
    txt->setPos(r.center().x() - tb.width() / 2, r.center().y() - tb.height() / 2);
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
        name->setPos((c.x0 + c.x1) / 2 - name->boundingRect().width() / 2, kColBot + 8);

        stage_labels_[static_cast<std::size_t>(i)] =
            scene_->addSimpleText(QString(), scale::monoFont(scale::kFontSizeDense));
        stage_labels_[static_cast<std::size_t>(i)]->setPos((c.x0 + c.x1) / 2, 16);
    }

    // ── Pipeline register bars ───────────────────────────────────────────────
    const qreal bar_xs[4] = {270, 555, 845, 1130};
    for (int i = 0; i < 4; ++i) {
        auto* bar = scene_->addRect(QRectF(bar_xs[i], 70, 16, 420));
        bar->setZValue(1);
        pipe_regs_[static_cast<std::size_t>(i)] = bar;
        auto* lbl = new QGraphicsSimpleTextItem(QString::fromLatin1(kPipeRegNames[i]), bar);
        lbl->setData(0, QStringLiteral("comp-label"));
        lbl->setFont(scale::monoFont(scale::kFontSizeDense - 3, true));
        lbl->setRotation(-90);
        lbl->setPos(bar_xs[i] + 13.5, 280 + lbl->boundingRect().width() / 2);
    }

    // ── IF stage ─────────────────────────────────────────────────────────────
    addMux(scene_, QRectF(16, 250, 20, 60));  // PC-source mux
    addBox(scene_, QRectF(52, 255, 42, 50), QStringLiteral("PC"));
    addAlu(scene_, QRectF(120, 100, 44, 56), QStringLiteral("+4"));  // PC+4 adder
    addBox(scene_, QRectF(120, 235, 116, 105), QStringLiteral("Instr.\nmemory"));

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
    addBox(scene_, QRectF(320, 74, 96, 40), QStringLiteral("Control"));
    addBox(scene_, QRectF(320, 210, 130, 125), QStringLiteral("Registers"));
    addEllipse(scene_, QRectF(340, 380, 84, 42), QStringLiteral("Sign-\nextend"));

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
    addMux(scene_, QRectF(600, 210, 20, 56));  // forwarding mux A
    addMux(scene_, QRectF(600, 280, 20, 56));  // forwarding mux B
    addAlu(scene_, QRectF(680, 210, 84, 130), QStringLiteral("ALU"));
    addAlu(scene_, QRectF(690, 92, 44, 56), QStringLiteral("+"));  // branch-target adder

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
    addBox(scene_, QRectF(910, 230, 125, 125), QStringLiteral("Data\nmemory"));
    addBox(scene_, QRectF(910, 96, 80, 44), QStringLiteral("Branch"));

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
    addMux(scene_, QRectF(1180, 250, 22, 70));  // MemToReg mux
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

    applyState();  // re-derives state-dependent tints for the new theme
}

void SchematicDatapathWidget::applyState() {
    const Theme& t = theme(dark_mode_);

    for (int i = 0; i < 5; ++i) {
        const auto&  snap = state_.stages[static_cast<std::size_t>(i)];
        const QColor tint = dark_mode_ ? kStageDark[i] : kStageLight[i];
        QColor       band = tint;
        band.setAlpha(snap.valid ? (dark_mode_ ? 70 : 120) : (dark_mode_ ? 25 : 45));
        stage_tints_[static_cast<std::size_t>(i)]->setBrush(band);

        // Mnemonic label above the column (Ripes-style).
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
            text = QString::fromStdString(format_instr(snap.raw));
        } else {
            text  = QStringLiteral("—");
            color = t.wire_dim;
        }
        lbl->setText(text);
        lbl->setBrush(color);
        const auto& c = kColumns[i];
        lbl->setPos((c.x0 + c.x1) / 2 - lbl->boundingRect().width() / 2, 16);

        // Breakpoint bullseye visibility.
        const bool bp = snap.valid && breakpoints_.count(snap.pc) > 0;
        bp_markers_[static_cast<std::size_t>(i)].first->setVisible(bp);
        bp_markers_[static_cast<std::size_t>(i)].second->setVisible(bp);
    }

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
    writeback_wire_->setActive(state_.stages[4].valid);
}

// ── Public API ──────────────────────────────────────────────────────────────

void SchematicDatapathWidget::setPipelineState(const mips::PipelineState& state) {
    state_ = state;
    applyState();
}

void SchematicDatapathWidget::setBreakpoints(const std::unordered_set<uint32_t>& bps) {
    breakpoints_ = bps;
    applyState();
}

void SchematicDatapathWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    applyTheme();
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

void SchematicDatapathWidget::wheelEvent(QWheelEvent* ev) {
    // Ctrl+wheel zooms around the cursor; plain wheel scrolls as usual.
    if (ev->modifiers() & Qt::ControlModifier) {
        user_zoomed_       = true;
        const qreal step   = std::pow(1.15, ev->angleDelta().y() / 120.0);
        const qreal cur    = transform().m11();
        const qreal target = std::clamp(cur * step, 0.3, 4.0);
        scale(target / cur, target / cur);
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
    const int idx = stageAtViewPos(ev->pos());
    if (idx < 0) return;
    const auto& snap = state_.stages[static_cast<std::size_t>(idx)];
    if (!snap.valid) return;

    QMenu      menu(this);
    const bool has_bp = breakpoints_.count(snap.pc) > 0;
    QAction*   act    = menu.addAction(has_bp ? tr("Clear Breakpoint") : tr("Set Breakpoint"));
    QAction*   fit    = menu.addAction(tr("Zoom to Fit"));
    QAction*   chosen = menu.exec(ev->globalPos());
    if (chosen == act) emit breakpointToggleRequested(snap.pc);
    if (chosen == fit) {
        user_zoomed_ = false;
        fitSchematic();
    }
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
