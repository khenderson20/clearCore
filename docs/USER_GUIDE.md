
# 📚 User Guide: Learning Computer Architecture with NSC Emulator

This project is not just a calculator; it's an interactive lab designed to teach you how modern CPUs operate. Use the Number System Converter as a gateway into MIPS architecture concepts.

## 🗺️ Getting Started (The Basics)

1.  **Number Conversion:** Before diving into the CPU, use the converter tab. Input numbers in different bases (e.g., Decimal `255` $\to$ Hex `FF`). This establishes your understanding of how data is represented at the lowest level (`uint64_t`).
2.  **CPU Mode Selection:** Choose **Single-Cycle** first. This provides a simple, predictable model to see fundamental instruction flow (Fetch $\to$ Decode $\to$ Execute...).
3.  **Instruction Entry:** Use the input field to enter raw 32-bit instructions (e.g., `0x00401000`). Watch the MIPS Decoder panel instantly break this binary value down into its components (Opcode, Register fields, etc.).

## 🔬 Learning Pipeline Concepts (The Advanced View)

Switching to **Pipelined Mode** is where the true learning begins. The visualization allows you to observe timing and complexity that single-cycle models hide.

### Understanding Hazards
Watch for these specific events in your cycle visualizations:
*   **Data Hazard:** Occurs when one instruction tries to use the result of a previous instruction before that result has been written back (e.g., Instruction B needs the output of ALU from Instruction A). The system detects this and inserts a **stall**, effectively pausing the pipeline for one cycle, as detailed in *Computer Organization and Design*.
*   **Control Hazard:** Occurs due to branches or jumps. Since the CPU doesn't know if a branch will be taken until late in the pipeline (the EX stage), it must guess. If it guesses wrong, the instructions already loaded into the stages following the branch must be **flushed**, wasting cycles.

### Tracing Data Flow
Use the visualization to trace operands:
*   When an instruction executes, follow its data path through the IF $\to$ ID $\to$ EX $\to$ MEM $\to$ WB registers (the pipeline state).
*   Observe when the Forwarding Unit intercepts a result and sends it *backwards* into the ALU input lines from a later stage—this is how the CPU avoids unnecessary stalls.

## 📖 Reference Materials
This project implements concepts detailed in:
*   **Textbooks:** For fundamental principles of digital design, see **Harris & Harris**, *Digital Design and Computer Architecture*. For CPU implementation details (pipelining, hazards), reference **Patterson & Hennessy**, *Computer Organization and Design*.
*   **FTXUI Documentation:** Learn how the TUI renders reactively by reviewing the [official FTXUI web documentation](https://github.com/ArthurSonzogni/FTXUI).