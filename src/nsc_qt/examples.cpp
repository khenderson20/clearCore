#include "nsc_qt/examples.h"

namespace nsc::qt {

const std::vector<ExampleProgram>& exampleProgramCatalog() {
    static const std::vector<ExampleProgram> catalog = {
        {QStringLiteral("Hello registers"),
         QString(R"(# Hello registers -- basic arithmetic, no hazards
addi $t0, $zero, 5
addi $t1, $zero, 10
add  $t2, $t0, $t1
)")},
        {QStringLiteral("Data hazard (forwarding)"),
         QString(R"(# Data hazard demo -- each instruction needs the result
# of the one right before it. Watch the EX/MEM and MEM/WB
# forwarding arrows light up on the Datapath tab.
addi $t0, $zero, 1
add  $t1, $t0, $t0
add  $t2, $t1, $t0
add  $t3, $t2, $t1
)")},
        {QStringLiteral("Load-use stall"),
         QString(R"(# Load-use hazard demo -- a load's result isn't ready in
# time to forward to the very next instruction, so the
# pipeline has to stall for one cycle. Watch the Registers
# tab and the Statistics tab's stall counter.
addi $t0, $zero, 7
sw   $t0, 0($zero)
lw   $t1, 0($zero)
add  $t2, $t1, $t1
)")},
        {QStringLiteral("Branch flush (control hazard)"),
         QString(R"(# Control hazard demo -- the branch is taken, so the
# instruction fetched right behind it turns out to be wrong
# and gets flushed. Watch the FLUSH badge on the Datapath tab.
addi $t0, $zero, 3
addi $t1, $zero, 3
beq  $t0, $t1, skip
addi $t2, $zero, 99
skip:
addi $t3, $zero, 42
)")},
    };
    return catalog;
}

}  // namespace nsc::qt