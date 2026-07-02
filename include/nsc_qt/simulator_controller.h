#pragma once

#include "mips/processor.h"
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace nsc::qt {

struct SimulatorStatistics {
    uint64_t cycles_executed      = 0;
    uint64_t instructions_retired = 0;
    uint64_t data_hazards         = 0;
    uint64_t control_hazards      = 0;
    uint64_t forwarding_events    = 0;
    uint64_t stalls               = 0;
    uint64_t flushes              = 0;

    [[nodiscard]] double cpi() const noexcept {
        if (instructions_retired == 0) return 0.0;
        return static_cast<double>(cycles_executed) / static_cast<double>(instructions_retired);
    }
};

class SimulatorController : public QObject {
    Q_OBJECT

public:
    explicit SimulatorController(std::unique_ptr<mips::IProcessor> processor,
                                 QObject*                          parent = nullptr);

    bool               loadProgram(const std::vector<uint32_t>& words, uint32_t addr = 0);
    void               reset();
    void               stepCycle();
    void               run();
    void               stop();
    [[nodiscard]] bool isRunning() const noexcept;

    [[nodiscard]] uint64_t                  cycleCount() const noexcept;
    [[nodiscard]] uint32_t                  registerValue(uint8_t idx) const noexcept;
    [[nodiscard]] std::optional<uint32_t>   memoryWord(uint32_t addr) const noexcept;
    [[nodiscard]] mips::PipelineState       pipelineState() const noexcept;
    [[nodiscard]] SimulatorStatistics       statistics() const noexcept;
    [[nodiscard]] const mips::Memory&       memory() const noexcept;
    [[nodiscard]] const mips::RegisterFile& registers() const noexcept;

    void                                              setBreakpoint(uint32_t pc);
    void                                              clearBreakpoint(uint32_t pc);
    [[nodiscard]] bool                                hasBreakpoint(uint32_t pc) const noexcept;
    [[nodiscard]] const std::unordered_set<uint32_t>& breakpoints() const noexcept;

    // Execution speed 0–100; 100 = as fast as possible, 0 = 500 ms/cycle.
    void setExecutionSpeed(int speed);

signals:
    void cycleExecuted(uint64_t count);
    void pipelineStateChanged(mips::PipelineState state);
    void breakpointHit(uint32_t pc);
    void statisticsUpdated(nsc::qt::SimulatorStatistics stats);
    void programLoaded(int instructionCount);
    void halted();
    void faulted();

private slots:
    void onRunTimer();

private:
    void accumulateStats(const mips::PipelineState& ps);
    void doStep();  // executes one step and emits signals; caller must NOT hold mutex_

    mutable QMutex                    mutex_;
    std::unique_ptr<mips::IProcessor> processor_;
    std::unordered_set<uint32_t>      breakpoints_;
    SimulatorStatistics               stats_{};
    QTimer*                           run_timer_ = nullptr;
};

}  // namespace nsc::qt
