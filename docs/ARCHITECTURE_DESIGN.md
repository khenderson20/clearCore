
# 🧠 Architectural Deep Dive

This document provides a technical explanation of the core design choices in `number_system_converter`, focusing on how we map academic concepts (from Patterson & Hennessy, Harris & Harris) into executable C++20 components.

## 🏛️ Core Design Philosophy: Decoupling
The entire system is built upon two primary architectural pillars:

1.  **Separation of Concerns:** The Number System Converter (`nsc_core`) handles data representation and UI input/output, entirely separate from the CPU simulation (`mips_core`). This allows us to test conversion logic without needing a running emulator.
2.  **Polymorphic Backend:** The `IProcessor` interface is the single most important design feature. It acts as the abstract hardware-software contract. By designing against this interface, we adhere to the **Ripes/DrMIPS pattern**, ensuring that new processor backends (e.g., RISC-V) can be plugged into the existing UI and visualizer without any changes to the frontend code (`nsc_ui.cpp`).

## 📚 Reference Textbooks
The theoretical foundation of this project is built upon these industry and academic standards:

*   **Computer Organization and Design** (Patterson & Hennessy): Focuses on modern pipeline techniques, hazards, and overall CPU architecture.
    ![Cover for Patterson & Hennessy's Computer Organization and Design](assets/covers/patterson_hennessy.jpg)

*   **Digital Design and Computer Architecture** (Harris & Harris): Provides the fundamental building blocks, including single-cycle datapath design and basic digital logic.
    ![Cover for Harris & Harris's Digital Design and Computer Architecture](assets/covers/Harris_Harris.jpg)


## 🔄 MIPS Implementation Details
### Single-Cycle vs. Pipelined Models
*   **SingleCycleCpu:** This serves as a behavioral model, simulating all stages (Fetch $\to$ Decode $\to$ Execute $\to$ Memory $\to$ Writeback) within a single function call (`step()`). While computationally inefficient in reality, it provides the clearest initial demonstration of data path flow. It implements the core functionality outlined in **Harris & Harris** for basic instruction execution.
*   **PipelinedCpu:** This model implements concurrent operations using five dedicated pipeline registers (IF/ID, ID/EX, EX/MEM, MEM/WB). The design follows best practices from **Computer Organization and Design** concerning stage boundaries:
    *   **Hazard Detection Unit:** Responsible for identifying data hazards (e.g., Load-Use) by comparing register file read addresses (`rs`, `rt`) against the write address of subsequent stages. When a hazard is detected, the pipeline inserts stalls or flushes instructions.

### Control Path Implementation
The control logic (`derive_control`) is a centralized state machine mapping the combination of the opcode and function code to all necessary hardware signals (e.g., `mem_read`, `alu_src`). This centralization prevents duplicated switch tables between the single-cycle and pipelined implementations, enforcing modularity across both backends.

## 💡 Extensibility Roadmap
The architecture is designed for easy extensibility. The current use of `IProcessor` allows future implementation of more advanced models, such as those detailed in **Patterson & Hennessy's** work on speculative execution and branch prediction.