# ============================================================================
# clearCore: Educational MIPS CPU Emulator with Terminal UI Visualization
# ============================================================================
#
# clearCore is a C++20 MIPS CPU emulator featuring:
#   • Pluggable processor models (single-cycle, pipelined with stages)
#   • Hazard detection and data forwarding simulation
#   • Terminal UI visualization (FTXUI) of pipeline datapath and signals
#   • Structured logging (spdlog) for instruction tracing and debugging
#   • Dynamic MIPS assembly support (Nyxstone) for test case generation
#   • Optional Qt6 desktop GUI for visual processor simulation
#
# Build Configuration:
#   cmake -B build -S .
#   cmake --build build
#
# Build Options:
#   -DBUILD_NYXSTONE=ON|OFF     : Enable/disable Nyxstone assembler (default: ON)
#   -DBUILD_QT6_UI=ON|OFF       : Build Qt6 Widgets desktop GUI (default: ON)
#   -DBUILD_QT6_QUICK_UI=ON|OFF : Build Qt Quick (QML) desktop GUI (default: ON)
#   -DENABLE_SANITIZERS=ON|OFF  : Build with ASan + UBSan (default: OFF)
#
# Presets (see CMakePresets.json):
#   cmake --preset debug|release|asan && cmake --build --preset <name>
#
# Install dependencies:
#   Fedora/RHEL:
#     sudo dnf install cmake gcc-c++ qt6-qtbase-devel llvm-devel
#   Ubuntu/Debian:
#     sudo apt install cmake build-essential qt6-base-dev llvm-dev
#   macOS:
#     brew install cmake qt@6 llvm@15
#
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(clearCore
        VERSION 0.1.0
        LANGUAGES CXX C  # C may be needed by some dependencies
        DESCRIPTION "Educational MIPS CPU emulator with pipeline visualization"
)

# ============================================================================
# CMAKE GLOBAL SETTINGS
# ============================================================================

# Enforce C++20 standard (required for structured bindings, spans, concepts)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Disable vendor-specific extensions (GNU, MSVC)

# FetchContent: Automatically download and integrate dependencies from GitHub
include(FetchContent)

# Enable CTest for running unit tests via `ctest`
enable_testing()

# ---- Warnings: shared interface target ----
# Every first-party target links this; third-party FetchContent code does not,
# so dependency warnings never pollute our build output.
add_library(clearcore_warnings INTERFACE)
target_compile_options(clearcore_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -pedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ---- Sanitizers: memory/UB error detection for Debug and CI builds ----
# ASan catches heap/stack overflows and use-after-free; UBSan catches signed
# overflow, misaligned access, and other undefined behavior — both essential
# for an emulator that does constant bit manipulation and memory indexing.
# Applied globally (before FetchContent) so dependencies are instrumented
# consistently; mixing sanitized and unsanitized code breaks ASan interceptors.
option(ENABLE_SANITIZERS "Build with AddressSanitizer + UBSanitizer" OFF)

if(ENABLE_SANITIZERS)
    message(STATUS "Sanitizers enabled: address, undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ============================================================================
# SYSTEM DEPENDENCIES
# ============================================================================

# Find system-installed libraries (threads for async logging)
find_package(Threads REQUIRED)

# ============================================================================
# EXTERNAL DEPENDENCIES (Auto-Downloaded from GitHub via FetchContent)
# ============================================================================

# ---- FTXUI: Terminal User Interface Framework ----
#
# Purpose: Render interactive console UI for pipeline visualization and controls
# Features: Canvas drawing, components (buttons, menus, inputs), responsive layout
# Homepage: https://github.com/ArthurSonzogni/FTXUI
# License: MIT
#
FetchContent_Declare(ftxui
        GIT_REPOSITORY "https://github.com/ArthurSonzogni/FTXUI.git"
        GIT_TAG "v7.0.0"
        GIT_SHALLOW ON
)

# ---- Microsoft GSL: C++ Core Guidelines Support Library ----
#
# Purpose: Safe wrapper types replacing unsafe raw pointers
# Key types: gsl::span (array view), gsl::not_null (non-null pointer)
# Homepage: https://github.com/microsoft/GSL
# License: MIT
#
# Usage example:
#   gsl::span<uint32_t> registers(reg_array, 32);  // Type-safe array view
#   gsl::not_null<CPU*> cpu_ptr{cpu};              // Asserts non-null
#
FetchContent_Declare(GSL
        GIT_REPOSITORY "https://github.com/microsoft/GSL"
        GIT_TAG "v4.2.0"
        GIT_SHALLOW ON
)

# ---- spdlog: Fast Structured C++ Logging Library ----
#
# Purpose: Provide thread-safe, leveled logging for debugging and tracing
#
# Use Cases in clearCore:
#   1. Instruction Tracing: Log PC, opcode, register state after each cycle
#      spdlog::debug("PC: 0x{:08x} | Instr: {} | $t0={}", pc, instr, reg[8]);
#
#   2. Pipeline Events: Log hazards, stalls, forwarding operations
#      spdlog::info("Hazard: RAW dependency detected on $t0");
#      spdlog::trace("Forwarding data from WB stage to EX stage");
#
#   3. Memory Access: Track load/store operations
#      spdlog::debug("Memory write: addr=0x{:08x}, data=0x{:08x}", addr, data);
#
#   4. Performance Analysis: Measure cycle counts and execution time
#      spdlog::stopwatch sw;
#      // ... simulation ...
#      spdlog::info("Execution completed in {}", sw);
#
# Configuration:
#   • Log levels: trace, debug, info, warn, error, critical
#   • Runtime level control: spdlog::set_level(spdlog::level::debug);
#   • Async logging: spdlog::set_async_mode(4096);  // Non-blocking queue
#   • File output: auto file_log = spdlog::basic_logger_mt("file", "log.txt");
#   • Colored console: auto console = spdlog::stdout_color_mt("console");
#
# Performance:
#   • Zero-cost when disabled (SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF)
#   • Async mode prevents blocking on I/O (write to queue, handle in background)
#   • Suitable for millions of log messages per simulation
#
# Homepage: https://github.com/gabime/spdlog
# License: MIT
# Version: 1.14.1 (stable v1.x branch)
#
FetchContent_Declare(spdlog
        GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
        GIT_TAG "v1.14.1"
        GIT_SHALLOW ON
)

# Download and make ftxui, GSL, spdlog available
FetchContent_MakeAvailable(ftxui GSL spdlog)

# ============================================================================
# NYXSTONE: LLVM-BASED ASSEMBLY/DISASSEMBLY FRAMEWORK (MIT LICENSE)
# ============================================================================
#
# Purpose: Dynamically assemble MIPS instructions to machine code at runtime
# and disassemble machine code back to assembly text.
#
# Why Nyxstone over Keystone:
#   • MIT License (vs Keystone's GPLv2) - no licensing complexity
#   • Superior accuracy - Keystone sometimes emitted incorrect bytes
#   • Label support - write realistic assembly programs with loops
#   • Strict error reporting - detailed error messages (vs Keystone's silent failures)
#   • Unified API - assembly + disassembly in one library (no Capstone needed)
#   • Actively maintained - Emproof team actively develops and fixes bugs
#   • All LLVM architectures - supports everything LLVM 15-18 supports
#
# Use Cases in clearCore:
#   1. Test Case Generation: Programmatically create MIPS test programs
#      nyx.assemble("addi $t0, $zero, 42")  →  structured instruction object
#
#   2. Disassembly: Convert machine code back to assembly for display/logging
#      nyx.disassemble({0x20080042}, 0x1000)  →  "addi $t0, $zero, 42"
#
#   3. Label-Based Programs: Write realistic assembly with loops
#      nyx.assemble("loop: addi $t0, $t0, 1; bne $t0, 10, loop")
#
#   4. Fuzzing: Generate random instruction sequences with correct encoding
#      for (int i = 0; i < 1000; i++) {
#          auto asm_code = generate_random_mips();
#          auto result = nyx.assemble(asm_code);  // Validated machine code
#      }
#
#   5. Educational Tools: Display equivalent machine code for assembly lines
#
# Requirements:
#   • LLVM 15, 16, 17, or 18 installed on system (headers + libraries)
#   • llvm-config in PATH or set $LLVM_DIR environment variable
#
# Installation:
#   Fedora/RHEL:  sudo dnf install llvm-devel
#   Ubuntu/Debian: sudo apt install llvm-dev (ensure version 15+)
#   macOS: brew install llvm@15
#
# Dependencies for Nyxstone:
#   • LLVM libraries (system dependency, not fetched)
#   • zlib-devel (compression, usually already installed)
#
# Licensing:
#   Nyxstone is MIT licensed. No reciprocal licensing requirements.
#   Can be used in any project type (open-source, closed-source, commercial).
#
# Homepage: https://github.com/emproof-com/nyxstone
# Maintained by: Emproof (https://emproof.com)
#
option(BUILD_NYXSTONE "Enable Nyxstone assembler/disassembler (MIT License)" ON)

if(BUILD_NYXSTONE)
    message(STATUS "Configuring Nyxstone assembler/disassembler framework...")

    # Step 1: Find LLVM 15 or later on the system
    # Nyxstone depends on LLVM libraries to function
    find_package(LLVM 15 QUIET CONFIG
            NAMES LLVM llvm
            HINTS ENV LLVM_DIR ENV NYXSTONE_LLVM_PREFIX
    )

    if(LLVM_FOUND)
        message(STATUS "  ✓ Found LLVM ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR} at ${LLVM_DIR}")

        # Step 2: Fetch Nyxstone from GitHub
        FetchContent_Declare(nyxstone
                GIT_REPOSITORY "https://github.com/emproof-com/nyxstone.git"
                GIT_TAG "v0.1.8"  # Pinned for reproducible builds
                GIT_SHALLOW ON
        )

        # Download and build Nyxstone
        FetchContent_MakeAvailable(nyxstone)

        # Verify Nyxstone built successfully
        if(TARGET nyxstone)
            message(STATUS "  ✓ Built Nyxstone from source (CMake target found)")
            set(NYXSTONE_TARGET nyxstone)
        else()
            message(WARNING "Nyxstone target not found. Build may have failed. Disabling.")
            set(BUILD_NYXSTONE OFF)
        endif()
    else()
        message(WARNING
                "LLVM 15+ not found. Nyxstone requires LLVM libraries.\n"
                "\n"
                "Install LLVM with:\n"
                "  Fedora/RHEL:  sudo dnf install llvm-devel\n"
                "  Ubuntu/Debian: sudo apt install llvm-dev\n"
                "  macOS: brew install llvm@15\n"
                "\n"
                "Or set -DLLVM_DIR=/path/to/llvm/cmake to use custom LLVM location.\n"
                "\n"
                "Disabling Nyxstone support."
        )
        set(BUILD_NYXSTONE OFF)
    endif()
else()
    message(STATUS "Nyxstone: disabled (set -DBUILD_NYXSTONE=ON to enable)")
    set(NYXSTONE_TARGET "")
endif()

# ============================================================================
# CORE LIBRARIES
# ============================================================================

# ---- Library: mips_core (MIPS CPU emulation logic) ----
#
# Responsibilities:
#   • CPU instruction decoding and execution
#   • Register file and memory management
#   • ALU operations and control logic
#   • Single-cycle and pipelined processor models
#   • Pipeline hazard detection and data forwarding
#   • Program loading and execution
#
add_library(mips_core STATIC
        src/mips/decoder.cpp          # Decode MIPS instructions to control signals
        src/mips/alu.cpp              # Arithmetic/logic unit implementation
        src/mips/registers.cpp        # Register file (32 registers x 32 bits)
        src/mips/memory.cpp           # Memory subsystem (data and instruction cache)
        src/mips/single_cycle_cpu.cpp # Simple 1-cycle CPU model
        src/mips/pipelined_cpu.cpp    # 5-stage pipelined CPU with hazard logic
        src/mips/program_loader.cpp   # Load ELF/binary programs into memory
        src/mips/disassembler.cpp     # Convert machine code back to assembly
)

target_include_directories(mips_core PUBLIC
        include                       # Public header files (e.g., include/mips/cpu.h)
)

# Link against required libraries
target_link_libraries(mips_core
        PUBLIC
        Microsoft.GSL::GSL        # Type-safe wrappers (gsl::span, gsl::not_null)
        spdlog::spdlog            # Structured logging
        PRIVATE
        clearcore_warnings        # -Wall -Wextra -pedantic
        Threads::Threads          # pthreads (if spdlog async enabled)
)

# Require C++20 features
target_compile_features(mips_core PUBLIC cxx_std_20)

# Add Nyxstone support if enabled
if(BUILD_NYXSTONE AND NYXSTONE_TARGET)
    # Include Nyxstone headers and LLVM headers
    target_include_directories(mips_core PRIVATE
            ${LLVM_INCLUDE_DIRS}
    )

    # Link against Nyxstone and LLVM libraries
    target_link_libraries(mips_core PRIVATE
            ${NYXSTONE_TARGET}
            ${LLVM_LIBRARIES}
    )

    # Define preprocessor flag so code can check: #ifdef CLEARCORE_NYXSTONE_ENABLED
    target_compile_definitions(mips_core PUBLIC CLEARCORE_NYXSTONE_ENABLED=1)

    message(STATUS "mips_core: Nyxstone assembler/disassembler support enabled")
else()
    target_compile_definitions(mips_core PUBLIC CLEARCORE_NYXSTONE_ENABLED=0)
    message(STATUS "mips_core: Nyxstone support disabled")
endif()

# ---- Library: nsc_core (Number System Converter logic) ----
#
# Responsibilities:
#   • Parse number strings in various bases (binary, octal, decimal, hex)
#   • Format numbers in various bases with customizable output
#   • Convert between bases without loss of precision
#
add_library(nsc_core STATIC
        src/nsc/parse.cpp             # Parse input strings
        src/nsc/format.cpp            # Format output with separators/padding
        src/nsc/converter.cpp         # Main conversion logic
)

target_include_directories(nsc_core PUBLIC include)

target_link_libraries(nsc_core
        PUBLIC Microsoft.GSL::GSL     # Type-safe wrappers
        PRIVATE clearcore_warnings    # -Wall -Wextra -pedantic
)

target_compile_features(nsc_core PUBLIC cxx_std_20)

# ---- Library: nsc_ui (Terminal UI components) ----
#
# Responsibilities:
#   • Assemble FTXUI components into interactive UI
#   • Handle user input (keyboard, mouse)
#   • Update display based on converter/emulator state
#   • Coordinate between logic and visualization
#
add_library(nsc_ui STATIC
        src/nsc/ui.cpp                # FTXUI component assembly
)

target_include_directories(nsc_ui PUBLIC include)

target_link_libraries(nsc_ui
        PUBLIC
        nsc_core                  # Number converter logic
        mips_core                 # MIPS emulator logic
        Microsoft.GSL::GSL        # Type-safe wrappers
        spdlog::spdlog            # Logging
        PRIVATE
        clearcore_warnings        # -Wall -Wextra -pedantic
        ftxui::screen             # FTXUI rendering to terminal
        ftxui::dom                # FTXUI declarative components
        ftxui::component          # FTXUI interactive elements
        Threads::Threads          # Multi-threading for async tasks
)

target_compile_features(nsc_ui PUBLIC cxx_std_20)

# ============================================================================
# EXECUTABLES
# ============================================================================

# ---- Executable: number_system_converter (Terminal UI Application) ----
#
# The main application: interactive number converter with MIPS emulator in TUI
#
add_executable(number_system_converter
        src/nsc/main.cpp              # Application entry point
)

target_link_libraries(number_system_converter PRIVATE nsc_ui)

# ---- Executable: clearCore-gui (Qt6 Desktop Application) ----
#
# Optional Qt6-based GUI with visual processor simulation
# Enable/disable with: -DBUILD_QT6_UI=ON|OFF
#
option(BUILD_QT6_UI "Build Qt6 desktop GUI for processor simulation" ON)

if(BUILD_QT6_UI)
    # Step 1: Locate Qt6 on the system
    find_package(Qt6 6.4
            COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets
            QUIET
    )

    if(NOT Qt6_FOUND)
        # Degrade gracefully (like the Quick UI below) so headless/CI machines
        # can still build and test the core libraries.
        message(WARNING
                "Qt6 not found. Install it with:\n"
                "  Fedora/RHEL:  sudo dnf install qt6-qtbase-devel qt6-qtbase-gui\n"
                "  Ubuntu/Debian: sudo apt install qt6-base-dev\n"
                "  macOS:        brew install qt@6\n"
                "Skipping clearCore-gui, or disable with -DBUILD_QT6_UI=OFF"
        )
        set(BUILD_QT6_UI OFF)
    endif()
endif()

if(BUILD_QT6_UI)
    message(STATUS "Qt6 found: Building desktop GUI")

    add_executable(clearCore-gui
            # Core source files
            src/nsc_qt/main.cpp                             # Application entry
            src/nsc_qt/assembler.cpp                        # Assembly UI widget
            src/nsc_qt/examples.cpp                         # Example programs
            src/nsc_qt/simulator_controller.cpp             # Emulation control
            src/nsc_qt/main_window.cpp                      # Main window layout
            src/nsc_qt/preferences_dialog.cpp               # Settings dialog

            # Widget components
            src/nsc_qt/widgets/code_editor.cpp              # Assembly code editor
            src/nsc_qt/widgets/datapath_widget.cpp          # Pipeline visualization
            src/nsc_qt/widgets/register_widget.cpp          # Register file viewer
            src/nsc_qt/widgets/memory_widget.cpp            # Memory inspector
            src/nsc_qt/widgets/pipeline_trace_widget.cpp    # Execution trace view

            # Headers (for moc generation)
            include/nsc_qt/simulator_controller.h
            include/nsc_qt/main_window.h
            include/nsc_qt/preferences_dialog.h
            include/nsc_qt/ui_scale.h
            include/nsc_qt/examples.h
            include/nsc_qt/widgets/code_editor.h
            include/nsc_qt/widgets/datapath_widget.h
            include/nsc_qt/widgets/register_widget.h
            include/nsc_qt/widgets/memory_widget.h
            include/nsc_qt/widgets/pipeline_trace_widget.h
    )

    # Configure Qt-specific properties
    set_target_properties(clearCore-gui PROPERTIES
            AUTOMOC ON                # Automatically run moc on headers with Q_OBJECT
            AUTORCC ON                # Automatically process .qrc resource files
            WIN32_EXECUTABLE ON       # Hide console window on Windows
            MACOSX_BUNDLE ON          # Create .app bundle on macOS
    )

    target_include_directories(clearCore-gui PRIVATE include)

    target_compile_features(clearCore-gui PUBLIC cxx_std_20)

    target_link_libraries(clearCore-gui
            PRIVATE
            mips_core             # MIPS emulation logic
            Microsoft.GSL::GSL    # Type-safe wrappers
            Qt6::Core             # Qt framework
            Qt6::Gui              # GUI components
            Qt6::Widgets          # Widget framework
            Qt6::OpenGL           # GPU rendering
            Qt6::OpenGLWidgets    # OpenGL integration
            clearcore_warnings    # -Wall -Wextra -pedantic
    )

else()
    message(STATUS "Qt6 GUI disabled (set -DBUILD_QT6_UI=ON to enable)")
endif()

# ---- Executable: clearCore-quick (Qt Quick / QML Desktop Application) ----
#
# Redesigned QML front end: single-window layout with the pipeline datapath
# permanently visible, editor alongside it, and Registers/Memory/Trace/Stats
# in a bottom inspector dock. Reuses mips_core plus the framework-agnostic
# pieces of the Widgets GUI (SimulatorController, assembler, examples).
#
# Requires qt6-qtdeclarative:
#   Fedora/RHEL:   sudo dnf install qt6-qtdeclarative-devel
#   Ubuntu/Debian: sudo apt install qt6-declarative-dev qml6-module-qtquick-controls
#   macOS:         brew install qt@6   (declarative included)
#
# Enable/disable with: -DBUILD_QT6_QUICK_UI=ON|OFF
#
option(BUILD_QT6_QUICK_UI "Build Qt Quick (QML) desktop GUI" ON)

if(BUILD_QT6_QUICK_UI)
    # Step 1: Locate the Qt Quick modules (needs more than qt6-qtbase)
    find_package(Qt6 6.5
            COMPONENTS Core Gui Qml Quick QuickControls2
            QUIET
    )

    if(NOT Qt6Quick_FOUND)
        message(WARNING
                "Qt6 Quick not found (qt6-qtdeclarative missing).\n"
                "  Fedora/RHEL:  sudo dnf install qt6-qtdeclarative-devel\n"
                "  Ubuntu/Debian: sudo apt install qt6-declarative-dev\n"
                "Skipping clearCore-quick, or disable with -DBUILD_QT6_QUICK_UI=OFF"
        )
        set(BUILD_QT6_QUICK_UI OFF)
    else()
        message(STATUS "Qt6 Quick found: Building QML desktop GUI")

        qt_add_executable(clearCore-quick
                # QML front end
                src/nsc_quick/main.cpp                      # Application entry
                src/nsc_quick/quick_simulator.cpp           # QML bridge + models
                include/nsc_quick/quick_simulator.h         # (moc: Q_OBJECT + QML_ELEMENT)

                # Reused framework-agnostic pieces of the Widgets GUI
                src/nsc_qt/simulator_controller.cpp         # Emulation control
                src/nsc_qt/assembler.cpp                    # MIPS assembler
                src/nsc_qt/examples.cpp                     # Example programs
                include/nsc_qt/simulator_controller.h       # (moc)
        )

        # AUTOMOC must be ON *before* qt_add_qml_module: QML type registration
        # extracts metatypes from moc's JSON output at module-declaration time.
        set_target_properties(clearCore-quick PROPERTIES
                AUTOMOC ON                # moc for Q_OBJECT / QML_ELEMENT headers
                WIN32_EXECUTABLE ON       # Hide console window on Windows
                MACOSX_BUNDLE ON          # Create .app bundle on macOS
        )

        # Theme.qml must be flagged as a singleton BEFORE qt_add_qml_module
        set_source_files_properties(qml/ClearCore/Theme.qml PROPERTIES
                QT_QML_SINGLETON_TYPE TRUE
        )

        qt_add_qml_module(clearCore-quick
                URI ClearCore
                RESOURCE_PREFIX /qt/qml
                QML_FILES
                qml/ClearCore/Main.qml
                qml/ClearCore/Theme.qml
                qml/ClearCore/CCButton.qml
                qml/ClearCore/TransportBar.qml
                qml/ClearCore/StatusPill.qml
                qml/ClearCore/DatapathStrip.qml
                qml/ClearCore/StageCard.qml
                qml/ClearCore/HazardBadge.qml
                qml/ClearCore/EditorPane.qml
                qml/ClearCore/RegistersPane.qml
                qml/ClearCore/MemoryPane.qml
                qml/ClearCore/TracePane.qml
                qml/ClearCore/StatsPane.qml
        )

        # qmltyperegistrar includes moc'd headers by basename only
        # (#include <quick_simulator.h>), so its directory must be on the path.
        target_include_directories(clearCore-quick PRIVATE
                include
                include/nsc_quick
        )

        target_compile_features(clearCore-quick PRIVATE cxx_std_20)

        target_link_libraries(clearCore-quick
                PRIVATE
                mips_core             # MIPS emulation logic (+ GSL, spdlog via PUBLIC)
                Qt6::Core             # Qt framework
                Qt6::Gui              # GUI base
                Qt6::Qml              # QML engine
                Qt6::Quick            # Qt Quick scene graph
                Qt6::QuickControls2   # QQuickStyle
                clearcore_warnings    # -Wall -Wextra -pedantic
        )
    endif()
else()
    message(STATUS "Qt Quick GUI disabled (set -DBUILD_QT6_QUICK_UI=ON to enable)")
endif()

# ============================================================================
# UNIT TESTS
# ============================================================================

# Tests validate correctness of core components before UI integration.
# Run with: ctest

# ---- Test: decoder_test ----
# Validates MIPS instruction decoding
add_executable(decoder_test tests/mips/decoder_test.cpp)
target_link_libraries(decoder_test PRIVATE mips_core Microsoft.GSL::GSL clearcore_warnings)
target_compile_features(decoder_test PUBLIC cxx_std_20)
add_test(NAME decoder_test COMMAND decoder_test)

# ---- Test: cpu_test ----
# Validates CPU execution (single-cycle and pipelined models)
add_executable(cpu_test tests/mips/cpu_test.cpp)
target_link_libraries(cpu_test PRIVATE mips_core Microsoft.GSL::GSL clearcore_warnings)
target_compile_features(cpu_test PUBLIC cxx_std_20)
add_test(NAME cpu_test COMMAND cpu_test)

# ---- Test: processor_test ----
# Validates the IProcessor contract against both concrete implementations
# (single-cycle and pipelined) using a shared correctness harness
add_executable(processor_test tests/mips/processor_test.cpp)
target_link_libraries(processor_test PRIVATE mips_core Microsoft.GSL::GSL clearcore_warnings)
target_compile_features(processor_test PUBLIC cxx_std_20)
add_test(NAME processor_test COMMAND processor_test)

# ---- Test: disasm_test ----
# Validates the disassembler and the hex program loader
add_executable(disasm_test tests/mips/disasm_test.cpp)
target_link_libraries(disasm_test PRIVATE mips_core Microsoft.GSL::GSL clearcore_warnings)
target_compile_features(disasm_test PUBLIC cxx_std_20)
add_test(NAME disasm_test COMMAND disasm_test)

# ---- Test: nsc_tests ----
# Validates number system converter logic
add_executable(nsc_tests tests/nsc/convert_test.cpp)
target_link_libraries(nsc_tests PRIVATE nsc_core Microsoft.GSL::GSL clearcore_warnings)
target_compile_features(nsc_tests PUBLIC cxx_std_20)
add_test(NAME nsc_tests COMMAND nsc_tests)

# ---- Test: qt_ui_test (if Qt6 enabled) ----
if(BUILD_QT6_UI)
    add_executable(qt_ui_test tests/qt_ui/qt_ui_test.cpp)
    set_target_properties(qt_ui_test PROPERTIES AUTOMOC ON)
    target_link_libraries(qt_ui_test PRIVATE Qt6::Widgets)
    add_test(NAME qt_ui_test COMMAND qt_ui_test)
endif()

# ============================================================================
# BUILD SUMMARY
# ============================================================================

message(STATUS "")
message(STATUS "=== clearCore Build Configuration ===")
message(STATUS "  C++ Standard:     C++20")
message(STATUS "  Threads:          ${CMAKE_THREAD_LIBS_INIT}")
message(STATUS "  Nyxstone:         ${BUILD_NYXSTONE}")
message(STATUS "  Qt6 GUI:          ${BUILD_QT6_UI}")
message(STATUS "  Qt Quick GUI:     ${BUILD_QT6_QUICK_UI}")
message(STATUS "")
message(STATUS "Executables:")
message(STATUS "  • number_system_converter (TUI + MIPS emulator)")
if(BUILD_QT6_UI)
    message(STATUS "  • clearCore-gui (Qt6 Widgets desktop GUI)")
endif()
if(BUILD_QT6_QUICK_UI)
    message(STATUS "  • clearCore-quick (Qt Quick / QML desktop GUI)")
endif()
message(STATUS "")
message(STATUS "Run tests: ctest --verbose")
message(STATUS "")