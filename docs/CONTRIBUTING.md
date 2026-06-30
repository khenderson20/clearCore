
# 🤝 Contributing Guide

Thank you for considering contributing to `clearCore`! This project is a complex blend of low-level hardware simulation and modern C++ UI development, which requires strong adherence to design patterns and conventions.

If you'd like to contribute, please follow these guidelines:

## 🚀 Development Workflow
1.  **Fork the Repository:** Create your own fork of this repository.
2.  **Create a Branch:** Before making any changes, create a descriptive feature branch from `develop` (e.g., `git checkout -b fix/hazard-detection`).
3.  **Develop & Test:** Implement your feature or fix and ensure all existing tests pass before running the new ones. Use `ctest` to validate your code against our contract specifications.
4.  **Submit a Pull Request (PR):** Submit a PR targeting the `develop` branch.

## ✍️ Code Style and Conventions
Our codebase follows strict conventions designed for robustness and testability:
*   **Separation of Concerns:** The project is split into pure logic cores (`nsc_core`, `mips_core`) and the UI layer (`nsc_ui`). Core libraries must **never** include headers from the UI.
*   **Strong Typing:** Use strong enums (`enum class`) for hardware fields (e.g., ALU operations) rather than relying on bare integers.
*   **Fallible Operations:** Utilize `std::optional` and exceptions to handle fallible operations (such as instruction decoding or parsing), preventing silent data corruption.
*   **Const Correctness:** Apply `const` and `[[nodiscard]]` aggressively, especially on pure query methods in hardware models (`IProcessor`).

## 🧪 Testing Guidelines
We rely heavily on unit testing to guarantee the functional correctness of our core logic:
*   **Unit Tests:** Test small modules (e.g., `parseBase`, ALU functions) in isolation. These live under `/tests/nsc` and `/tests/mips`.
*   **Integration Tests:** Use polymorphic tests (`processor_test.cpp`) to ensure that both the single-cycle and pipelined backends adhere to the same abstract contract (`IProcessor`).

Please feel free to open an issue if you have questions about any of these conventions!
