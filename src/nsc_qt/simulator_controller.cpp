#include "nsc_qt/simulator_controller.h"
#include <QMutexLocker>

namespace nsc::qt {

SimulatorController::SimulatorController(std::unique_ptr<mips::IProcessor> processor,
                                         QObject* parent)
    : QObject(parent)
    , processor_(std::move(processor))
{
    run_timer_ = new QTimer(this);
    run_timer_->setInterval(0);
    connect(run_timer_, &QTimer::timeout, this, &SimulatorController::onRunTimer);
}

bool SimulatorController::loadProgram(const std::vector<uint32_t>& words, uint32_t addr)
{
    bool ok = false;
    mips::PipelineState ps;
    {
        QMutexLocker lock(&mutex_);
        run_timer_->stop();
        ok = processor_->load_program(words, addr);
        if (ok) {
            stats_ = {};
            ps = processor_->pipeline_state();
        }
    }
    if (ok) {
        emit programLoaded(static_cast<int>(words.size()));
        emit pipelineStateChanged(ps);
    }
    return ok;
}

void SimulatorController::reset()
{
    mips::PipelineState ps;
    SimulatorStatistics st;
    {
        QMutexLocker lock(&mutex_);
        run_timer_->stop();
        processor_->reset();
        stats_ = {};
        ps = processor_->pipeline_state();
        st = stats_;
    }
    emit pipelineStateChanged(ps);
    emit statisticsUpdated(st);
}

void SimulatorController::doStep()
{
    mips::StepResult    result;
    mips::PipelineState ps;
    uint64_t            cycle;
    uint32_t            next_pc;
    SimulatorStatistics stats_copy;
    bool                bp_hit = false;

    {
        QMutexLocker lock(&mutex_);
        result   = processor_->step();
        ps       = processor_->pipeline_state();
        accumulateStats(ps);
        cycle    = static_cast<uint64_t>(processor_->cycle_count());
        next_pc  = processor_->pc();
        stats_copy = stats_;

        if (result != mips::StepResult::Ok) {
            run_timer_->stop();
        } else if (breakpoints_.count(next_pc)) {
            run_timer_->stop();
            bp_hit = true;
        }
    }

    // High Performance Throttling:
    // Prevent UI event flood if running continuously at Max Speed (Interval == 0).
    // Only applies while the run timer is driving execution — a manual
    // stepCycle() must always emit, or the UI silently shows stale state
    // (QTimer's default interval is 0 even when it has never been started).
    bool throttle = run_timer_->isActive()
                 && (run_timer_->interval() == 0) && (cycle % 5000 != 0);

    // Only emit standard update signals if we aren't throttling, OR if we halted/breaked
    if (!throttle || result != mips::StepResult::Ok || bp_hit) {
        emit cycleExecuted(cycle);
        emit pipelineStateChanged(ps);
        emit statisticsUpdated(stats_copy);
    }

    if (result == mips::StepResult::Halt)       emit halted();
    else if (result == mips::StepResult::Fault) emit faulted();
    else if (bp_hit)                             emit breakpointHit(next_pc);
}

void SimulatorController::stepCycle()
{
    doStep();
}

void SimulatorController::run()
{
    run_timer_->start();
}

void SimulatorController::stop()
{
    run_timer_->stop();
}

bool SimulatorController::isRunning() const noexcept
{
    return run_timer_->isActive();
}

void SimulatorController::onRunTimer()
{
    doStep();
}

uint64_t SimulatorController::cycleCount() const noexcept
{
    QMutexLocker lock(&mutex_);
    return static_cast<uint64_t>(processor_->cycle_count());
}

uint32_t SimulatorController::registerValue(uint8_t idx) const noexcept
{
    QMutexLocker lock(&mutex_);
    return processor_->regs().read(idx);
}

std::optional<uint32_t> SimulatorController::memoryWord(uint32_t addr) const noexcept
{
    QMutexLocker lock(&mutex_);
    return processor_->mem().read_word(addr);
}

mips::PipelineState SimulatorController::pipelineState() const noexcept
{
    QMutexLocker lock(&mutex_);
    return processor_->pipeline_state();
}

SimulatorStatistics SimulatorController::statistics() const noexcept
{
    QMutexLocker lock(&mutex_);
    return stats_;
}

const mips::Memory& SimulatorController::memory() const noexcept
{
    return processor_->mem();
}

const mips::RegisterFile& SimulatorController::registers() const noexcept
{
    return processor_->regs();
}

void SimulatorController::setBreakpoint(uint32_t pc)
{
    QMutexLocker lock(&mutex_);
    breakpoints_.insert(pc);
}

void SimulatorController::clearBreakpoint(uint32_t pc)
{
    QMutexLocker lock(&mutex_);
    breakpoints_.erase(pc);
}

bool SimulatorController::hasBreakpoint(uint32_t pc) const noexcept
{
    QMutexLocker lock(&mutex_);
    return breakpoints_.count(pc) > 0;
}

const std::unordered_set<uint32_t>& SimulatorController::breakpoints() const noexcept
{
    return breakpoints_;
}

void SimulatorController::setExecutionSpeed(int speed)
{
    const int interval = (100 - std::clamp(speed, 0, 100)) * 5;
    run_timer_->setInterval(interval);
}

void SimulatorController::accumulateStats(const mips::PipelineState& ps)
{
    ++stats_.cycles_executed;

    const auto& wb = ps.stages[4];
    if (wb.valid && !wb.stalled && !wb.flushed)
        ++stats_.instructions_retired;

    if (ps.load_stall) {
        ++stats_.data_hazards;
        ++stats_.stalls;
    }
    if (ps.branch_flush) {
        ++stats_.control_hazards;
        ++stats_.flushes;
    }
    if (ps.fwd_ex_to_ex_a || ps.fwd_ex_to_ex_b ||
        ps.fwd_mem_to_ex_a || ps.fwd_mem_to_ex_b)
        ++stats_.forwarding_events;
}

} // namespace nsc::qt