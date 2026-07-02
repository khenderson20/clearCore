#pragma once

// ─── quick_simulator.h ────────────────────────────────────────────────────────
// QML bridge for the clearCore Qt Quick UI.
//
// Wraps the existing (framework-agnostic) nsc::qt::SimulatorController and
// exposes exactly what the QML layer needs: pipeline stage snapshots, a
// register model, memory dump rows, a bounded pipeline trace, statistics,
// and transport control (step / run / pause / reset / speed / model swap).
//
// Registered into the `ClearCore` QML module via QML_ELEMENT — instantiate
// it in QML as `Simulator {}`.

#include "mips/processor.h"
#include "nsc_qt/simulator_controller.h"

#include <QAbstractListModel>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

namespace nsc::quick {

// ─── RegisterModel ────────────────────────────────────────────────────────────
// 32 rows, one per architectural register. Refreshed after every cycle;
// dataChanged is emitted only for rows whose value actually changed, so the
// GridView repaints the minimum.
class RegisterModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NumberRole = Qt::UserRole + 1,  // int    — 0..31
        NameRole,                       // QString — "$t0", "$sp", …
        HexRole,                        // QString — "0x0000abcd"
        DecRole,                        // QString — signed decimal
        ChangedRole,                    // bool   — written on the last cycle
    };

    explicit RegisterModel(QObject* parent = nullptr);

    [[nodiscard]] int                    rowCount(const QModelIndex& = {}) const override;
    [[nodiscard]] QVariant               data(const QModelIndex& idx, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void refresh(const mips::RegisterFile& regs);
    void resetAll();

private:
    std::array<uint32_t, 32> values_{};
    int                      last_written_ = -1;
};

// ─── QuickSimulator ───────────────────────────────────────────────────────────
class QuickSimulator : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_NAMED_ELEMENT(Simulator)

    // Transport / status
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool programLoaded READ programLoaded NOTIFY programLoadedChanged)

    // Live pipeline state — list of 5 maps:
    //   { name, valid, stalled, flushed, pc (hex QString), text (disassembly) }
    Q_PROPERTY(QVariantList stages READ stages NOTIFY pipelineChanged)
    // Map of forwarding / hazard flags:
    //   { fwdExA, fwdExB, fwdMemA, fwdMemB, loadStall, branchFlush }
    Q_PROPERTY(QVariantMap hazards READ hazards NOTIFY pipelineChanged)

    // Statistics
    Q_PROPERTY(quint64 cycles READ cycles NOTIFY statsChanged)
    Q_PROPERTY(quint64 instructions READ instructions NOTIFY statsChanged)
    Q_PROPERTY(double cpi READ cpi NOTIFY statsChanged)
    Q_PROPERTY(quint64 dataHazards READ dataHazards NOTIFY statsChanged)
    Q_PROPERTY(quint64 controlHazards READ controlHazards NOTIFY statsChanged)
    Q_PROPERTY(quint64 forwards READ forwards NOTIFY statsChanged)
    Q_PROPERTY(quint64 stalls READ stalls NOTIFY statsChanged)
    Q_PROPERTY(quint64 flushes READ flushes NOTIFY statsChanged)

    // Pipeline trace — bounded instruction × cycle grid.
    // Each row: { label (QString), start (int, cycle of first cell),
    //             cells (QStringList of "IF","ID","EX","MEM","WB","--","**") }
    Q_PROPERTY(QVariantList traceRows READ traceRows NOTIFY traceChanged)

    // Register model (constant pointer; contents update in place)
    Q_PROPERTY(nsc::quick::RegisterModel* registers READ registers CONSTANT)

public:
    explicit QuickSimulator(QObject* parent = nullptr);

    // ── Transport ──────────────────────────────────────────────────────────────
    Q_INVOKABLE void step();
    Q_INVOKABLE void runPause();
    Q_INVOKABLE void reset();

    // Assemble `source`; on success load it and return "".
    // On failure return the assembler's "line N: message" string.
    Q_INVOKABLE QString assembleAndLoad(const QString& source);

    // ── Examples ───────────────────────────────────────────────────────────────
    Q_INVOKABLE QStringList exampleNames() const;
    Q_INVOKABLE QString     exampleSource(int index) const;

    // ── Memory ─────────────────────────────────────────────────────────────────
    // `rows` hex-dump lines of 16 bytes starting at `base` (aligned down to 16).
    // Format: "00400000  8c 09 00 04 …  |ascii………………|"
    Q_INVOKABLE QStringList memoryRows(quint32 base, int rows) const;

    // ── Breakpoints ────────────────────────────────────────────────────────────
    Q_INVOKABLE void toggleBreakpoint(quint32 pc);
    Q_INVOKABLE bool hasBreakpoint(quint32 pc) const;

    // ── Property getters ───────────────────────────────────────────────────────
    [[nodiscard]] bool    isRunning() const;
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] int     speed() const { return speed_; }
    [[nodiscard]] QString model() const { return model_; }
    [[nodiscard]] bool    programLoaded() const { return program_loaded_; }

    [[nodiscard]] QVariantList stages() const { return stages_; }
    [[nodiscard]] QVariantMap  hazards() const { return hazards_; }
    [[nodiscard]] QVariantList traceRows() const;

    [[nodiscard]] quint64 cycles() const { return stats_.cycles_executed; }
    [[nodiscard]] quint64 instructions() const { return stats_.instructions_retired; }
    [[nodiscard]] double  cpi() const { return stats_.cpi(); }
    [[nodiscard]] quint64 dataHazards() const { return stats_.data_hazards; }
    [[nodiscard]] quint64 controlHazards() const { return stats_.control_hazards; }
    [[nodiscard]] quint64 forwards() const { return stats_.forwarding_events; }
    [[nodiscard]] quint64 stalls() const { return stats_.stalls; }
    [[nodiscard]] quint64 flushes() const { return stats_.flushes; }

    [[nodiscard]] RegisterModel* registers() { return &reg_model_; }

    void setSpeed(int s);
    void setModel(const QString& m);  // "pipelined" | "single"

signals:
    void runningChanged();
    void statusChanged();
    void speedChanged();
    void modelChanged();
    void programLoadedChanged();
    void pipelineChanged();
    void statsChanged();
    void traceChanged();
    void breakpointsChanged();
    void assemblyError(QString message);

private:
    void rebuildController();
    void connectController();
    void onPipeline(const mips::PipelineState& ps);
    void setStatus(const QString& s);
    void appendTrace(const mips::PipelineState& ps);

    // One row of the trace grid, keyed by the instruction's IF entry.
    struct TraceRow {
        uint32_t    pc          = 0;
        quint64     start_cycle = 0;
        QStringList cells;  // per-cycle stage tags
        QString     label;  // "0x00400008  add $t2,$t0,$t1"
    };

    std::unique_ptr<nsc::qt::SimulatorController> ctl_;
    RegisterModel                                 reg_model_;

    QVariantList                 stages_;
    QVariantMap                  hazards_;
    nsc::qt::SimulatorStatistics stats_{};
    std::vector<TraceRow>        trace_;
    std::vector<uint32_t>        last_program_;

    QString status_         = QStringLiteral("Idle");
    QString model_          = QStringLiteral("pipelined");
    int     speed_          = 60;
    bool    program_loaded_ = false;

    static constexpr int kTraceMaxRows   = 256;
    static constexpr int kTraceMaxCycles = 512;
};

}  // namespace nsc::quick
