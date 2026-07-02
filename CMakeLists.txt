# ============================================================================
# clearCore — educational MIPS CPU emulator with pipeline visualization
# ============================================================================
#
# Targets:
#   number_system_converter  Terminal UI app (FTXUI): number converter + emulator
#   clearCore-gui            Qt6 Widgets desktop GUI          (optional)
#   clearCore-quick          Qt Quick / QML desktop GUI       (optional)
#   mips_core / nsc_core     Static libraries with the actual logic
#   *_test                   Unit tests, run with ctest
#
# Build (all builds land in build/<preset>, see CMakePresets.json):
#   cmake --preset debug            # or: release, asan, core-only
#   cmake --build --preset debug
#   ctest --preset debug
#
# Options (all default ON except sanitizers):
#   -DBUILD_NYXSTONE=ON|OFF      Nyxstone assembler/disassembler (needs LLVM 15+)
#   -DBUILD_QT6_UI=ON|OFF        Qt6 Widgets GUI
#   -DBUILD_QT6_QUICK_UI=ON|OFF  Qt Quick (QML) GUI
#   -DENABLE_SANITIZERS=ON|OFF   ASan + UBSan
#
# Dependencies (FTXUI, GSL, spdlog, Nyxstone are auto-downloaded; Qt/LLVM are not):
#   Fedora/RHEL:   sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel llvm-devel
#   Ubuntu/Debian: sudo apt install cmake build-essential qt6-base-dev qt6-declarative-dev llvm-dev
#   macOS:         brew install cmake qt@6 llvm@15
#
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(clearCore
        VERSION 0.1.0
        LANGUAGES CXX C  # C may be needed by some dependencies
        DESCRIPTION "Educational MIPS CPU emulator with pipeline visualization"
)

# ============================================================================
# GLOBAL SETTINGS
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
enable_testing()

# Every first-party target links this interface target; third-party
# FetchContent code does not, so dependency warnings never pollute our output.
add_library(clearcore_warnings INTERFACE)
target_compile_options(clearcore_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -pedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Sanitizers are applied globally (before FetchContent) so dependencies are
# instrumented consistently; mixing sanitized and unsanitized code breaks
# ASan interceptors.
option(ENABLE_SANITIZERS "Build with AddressSanitizer + UBSanitizer" OFF)
if(ENABLE_SANITIZERS)
    message(STATUS "Sanitizers enabled: address, undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# ============================================================================
# AUTO-DOWNLOADED DEPENDENCIES (FetchContent)
# ============================================================================

# FTXUI — terminal UI framework (MIT), https://github.com/ArthurSonzogni/FTXUI
FetchContent_Declare(ftxui
        GIT_REPOSITORY "https://github.com/ArthurSonzogni/FTXUI.git"
        GIT_TAG "v7.0.0"
        GIT_SHALLOW ON
)

# Microsoft GSL — gsl::span, gsl::not_null (MIT), https://github.com/microsoft/GSL
FetchContent_Declare(GSL
        GIT_REPOSITORY "https://github.com/microsoft/GSL"
        GIT_TAG "v4.2.0"
        GIT_SHALLOW ON
)

# spdlog — fast structured logging (MIT), https://github.com/gabime/spdlog
# Used for instruction tracing, pipeline/hazard events, and memory access logs.
FetchContent_Declare(spdlog
        GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
        GIT_TAG "v1.14.1"
        GIT_SHALLOW ON
)

FetchContent_MakeAvailable(ftxui GSL spdlog)

# ============================================================================
# NYXSTONE — LLVM-based assembler/disassembler (MIT)
# ============================================================================
#
# Assembles MIPS text to machine code at runtime and back (test generation,
# display, fuzzing). Chosen over Keystone for its MIT license, accuracy,
# label support, and active maintenance. https://github.com/emproof-com/nyxstone
#
# Requires LLVM 15+ headers/libraries on the system (llvm-devel / llvm-dev).
# Degrades gracefully: if LLVM is missing, Nyxstone is disabled with a warning.
#
option(BUILD_NYXSTONE "Enable Nyxstone assembler/disassembler (MIT License)" ON)

if(BUILD_NYXSTONE)
    message(STATUS "Configuring Nyxstone assembler/disassembler framework...")

    find_package(LLVM 15 QUIET CONFIG
            NAMES LLVM llvm
            HINTS ENV LLVM_DIR ENV NYXSTONE_LLVM_PREFIX
    )

    if(LLVM_FOUND)
        message(STATUS "  ✓ Found LLVM ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR} at ${LLVM_DIR}")

        FetchContent_Declare(nyxstone
                GIT_REPOSITORY "https://github.com/emproof-com/nyxstone.git"
                GIT_TAG "v0.1.8"  # Pinned for reproducible builds
                GIT_SHALLOW ON
        )
        FetchContent_MakeAvailable(nyxstone)

        if(TARGET nyxstone)
            message(STATUS "  ✓ Built Nyxstone from source (CMake target found)")
            set(NYXSTONE_TARGET nyxstone)
        else()
            message(WARNING "Nyxstone target not found. Build may have failed. Disabling.")
            set(BUILD_NYXSTONE OFF)
        endif()
    else()
        message(WARNING
                "LLVM 15+ not found; disabling Nyxstone. Install llvm-devel (Fedora), "
                "llvm-dev (Debian/Ubuntu), or brew install llvm@15 (macOS), "
                "or set -DLLVM_DIR=/path/to/llvm/cmake."
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

# ---- mips_core: MIPS CPU emulation (decode, execute, pipeline, memory) ----
add_library(mips_core STATIC
        src/mips/decoder.cpp          # Decode MIPS instructions to control signals
        src/mips/alu.cpp              # Arithmetic/logic unit
        src/mips/registers.cpp        # Register file (32 x 32-bit)
        src/mips/memory.cpp           # Memory subsystem
        src/mips/single_cycle_cpu.cpp # Simple 1-cycle CPU model
        src/mips/pipelined_cpu.cpp    # 5-stage pipelined CPU with hazard logic
        src/mips/program_loader.cpp   # Load programs into memory
        src/mips/disassembler.cpp     # Machine code back to assembly
)

target_include_directories(mips_core PUBLIC include)

target_link_libraries(mips_core
        PUBLIC
        Microsoft.GSL::GSL
        spdlog::spdlog
        PRIVATE
        clearcore_warnings
        Threads::Threads
)

target_compile_features(mips_core PUBLIC cxx_std_20)

# Code can check: #if CLEARCORE_NYXSTONE_ENABLED
if(BUILD_NYXSTONE AND NYXSTONE_TARGET)
    target_include_directories(mips_core PRIVATE ${LLVM_INCLUDE_DIRS})
    target_link_libraries(mips_core PRIVATE ${NYXSTONE_TARGET} ${LLVM_LIBRARIES})
    target_compile_definitions(mips_core PUBLIC CLEARCORE_NYXSTONE_ENABLED=1)
    message(STATUS "mips_core: Nyxstone assembler/disassembler support enabled")
else()
    target_compile_definitions(mips_core PUBLIC CLEARCORE_NYXSTONE_ENABLED=0)
    message(STATUS "mips_core: Nyxstone support disabled")
endif()

# ---- nsc_core: number system converter (parse/format/convert bases) ----
add_library(nsc_core STATIC
        src/nsc/parse.cpp
        src/nsc/format.cpp
        src/nsc/converter.cpp
)

target_include_directories(nsc_core PUBLIC include)
target_link_libraries(nsc_core
        PUBLIC Microsoft.GSL::GSL
        PRIVATE clearcore_warnings
)
target_compile_features(nsc_core PUBLIC cxx_std_20)

# ---- nsc_ui: FTXUI terminal UI on top of nsc_core + mips_core ----
add_library(nsc_ui STATIC
        src/nsc/ui.cpp
)

target_include_directories(nsc_ui PUBLIC include)
target_link_libraries(nsc_ui
        PUBLIC
        nsc_core
        mips_core
        Microsoft.GSL::GSL
        spdlog::spdlog
        PRIVATE
        clearcore_warnings
        ftxui::screen
        ftxui::dom
        ftxui::component
        Threads::Threads
)
target_compile_features(nsc_ui PUBLIC cxx_std_20)

# ============================================================================
# EXECUTABLES
# ============================================================================

# ---- number_system_converter: the main terminal UI application ----
add_executable(number_system_converter src/nsc/main.cpp)
target_link_libraries(number_system_converter PRIVATE nsc_ui)

# ---- clearCore-gui: Qt6 Widgets desktop GUI (optional) ----
option(BUILD_QT6_UI "Build Qt6 desktop GUI for processor simulation" ON)

if(BUILD_QT6_UI)
    find_package(Qt6 6.4
            COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets
            QUIET
    )
    if(NOT Qt6_FOUND)
        # Degrade gracefully so headless/CI machines can still build and test
        # the core libraries.
        message(WARNING
                "Qt6 not found (install qt6-qtbase-devel / qt6-base-dev / brew qt@6). "
                "Skipping clearCore-gui, or disable with -DBUILD_QT6_UI=OFF"
        )
        set(BUILD_QT6_UI OFF)
    endif()
endif()

if(BUILD_QT6_UI)
    message(STATUS "Qt6 found: Building desktop GUI")

    add_executable(clearCore-gui
            src/nsc_qt/main.cpp
            src/nsc_qt/assembler.cpp
            src/nsc_qt/examples.cpp
            src/nsc_qt/simulator_controller.cpp
            src/nsc_qt/main_window.cpp
            src/nsc_qt/preferences_dialog.cpp
            src/nsc_qt/widgets/code_editor.cpp
            src/nsc_qt/widgets/datapath_widget.cpp
            src/nsc_qt/widgets/register_widget.cpp
            src/nsc_qt/widgets/memory_widget.cpp
            src/nsc_qt/widgets/pipeline_trace_widget.cpp

            # Headers listed so AUTOMOC picks up Q_OBJECT classes
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

    set_target_properties(clearCore-gui PROPERTIES
            AUTOMOC ON                # Run moc on headers with Q_OBJECT
            AUTORCC ON                # Process .qrc resource files
            WIN32_EXECUTABLE ON       # Hide console window on Windows
            MACOSX_BUNDLE ON          # Create .app bundle on macOS
    )

    target_include_directories(clearCore-gui PRIVATE include)
    target_compile_features(clearCore-gui PUBLIC cxx_std_20)
    target_link_libraries(clearCore-gui
            PRIVATE
            mips_core
            Microsoft.GSL::GSL
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::OpenGL
            Qt6::OpenGLWidgets
            clearcore_warnings
    )
else()
    message(STATUS "Qt6 GUI disabled (set -DBUILD_QT6_UI=ON to enable)")
endif()

# ---- clearCore-quick: Qt Quick / QML desktop GUI (optional) ----
#
# Single-window QML front end; reuses mips_core plus the framework-agnostic
# pieces of the Widgets GUI (SimulatorController, assembler, examples).
# Needs qt6-qtdeclarative-devel (Fedora) / qt6-declarative-dev (Debian).
#
option(BUILD_QT6_QUICK_UI "Build Qt Quick (QML) desktop GUI" ON)

if(BUILD_QT6_QUICK_UI)
    find_package(Qt6 6.5
            COMPONENTS Core Gui Qml Quick QuickControls2
            QUIET
    )

    if(NOT Qt6Quick_FOUND)
        message(WARNING
                "Qt6 Quick not found (install qt6-qtdeclarative-devel / qt6-declarative-dev). "
                "Skipping clearCore-quick, or disable with -DBUILD_QT6_QUICK_UI=OFF"
        )
        set(BUILD_QT6_QUICK_UI OFF)
    else()
        message(STATUS "Qt6 Quick found: Building QML desktop GUI")

        qt_add_executable(clearCore-quick
                src/nsc_quick/main.cpp
                src/nsc_quick/quick_simulator.cpp           # QML bridge + models
                include/nsc_quick/quick_simulator.h         # (moc: Q_OBJECT + QML_ELEMENT)

                # Reused framework-agnostic pieces of the Widgets GUI
                src/nsc_qt/simulator_controller.cpp
                src/nsc_qt/assembler.cpp
                src/nsc_qt/examples.cpp
                include/nsc_qt/simulator_controller.h       # (moc)
        )

        # AUTOMOC must be ON *before* qt_add_qml_module: QML type registration
        # extracts metatypes from moc's JSON output at module-declaration time.
        set_target_properties(clearCore-quick PROPERTIES
                AUTOMOC ON
                WIN32_EXECUTABLE ON
                MACOSX_BUNDLE ON
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
                mips_core             # + GSL, spdlog via PUBLIC propagation
                Qt6::Core
                Qt6::Gui
                Qt6::Qml
                Qt6::Quick
                Qt6::QuickControls2
                clearcore_warnings
        )
    endif()
else()
    message(STATUS "Qt Quick GUI disabled (set -DBUILD_QT6_QUICK_UI=ON to enable)")
endif()

# ============================================================================
# UNIT TESTS  (run with: ctest --preset debug)
# ============================================================================

# All core tests follow the same pattern: one source file, one library.
function(clearcore_add_test name source lib)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${lib} Microsoft.GSL::GSL clearcore_warnings)
    target_compile_features(${name} PUBLIC cxx_std_20)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

clearcore_add_test(decoder_test   tests/mips/decoder_test.cpp   mips_core)  # Instruction decoding
clearcore_add_test(cpu_test       tests/mips/cpu_test.cpp       mips_core)  # CPU execution (both models)
clearcore_add_test(processor_test tests/mips/processor_test.cpp mips_core)  # IProcessor contract
clearcore_add_test(disasm_test    tests/mips/disasm_test.cpp    mips_core)  # Disassembler + hex loader
clearcore_add_test(nsc_tests      tests/nsc/convert_test.cpp    nsc_core)   # Number conversion

# ---- qt_ui_test: exercises assembler/controller/widgets (if Qt6 enabled) ----
if(BUILD_QT6_UI)
    # The units under test live in the clearCore-gui target, not a library,
    # so compile them into the test too.
    add_executable(qt_ui_test
            tests/qt_ui/qt_ui_test.cpp
            src/nsc_qt/assembler.cpp
            src/nsc_qt/simulator_controller.cpp
            src/nsc_qt/widgets/register_widget.cpp
            src/nsc_qt/widgets/memory_widget.cpp
            src/nsc_qt/widgets/pipeline_trace_widget.cpp
            include/nsc_qt/simulator_controller.h            # (moc)
            include/nsc_qt/widgets/register_widget.h         # (moc)
            include/nsc_qt/widgets/memory_widget.h           # (moc)
            include/nsc_qt/widgets/pipeline_trace_widget.h   # (moc)
    )
    set_target_properties(qt_ui_test PROPERTIES AUTOMOC ON)
    target_include_directories(qt_ui_test PRIVATE include)
    target_link_libraries(qt_ui_test PRIVATE
            Qt6::Widgets
            mips_core
            clearcore_warnings
    )
    target_compile_features(qt_ui_test PUBLIC cxx_std_20)
    add_test(NAME qt_ui_test COMMAND qt_ui_test)

    # QApplication needs a windowing platform; Qt's offscreen backend lets the
    # test run on headless machines and CI runners without a display.
    set_tests_properties(qt_ui_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

# ---- MARS golden tests: differential testing against a reference simulator ----
# Each corpus program in tests/golden/ is assembled and executed by MARS (the
# classroom-standard MIPS simulator, MIT licence) and by both clearCore CPU
# models; the register files must match exactly. Requires a Java runtime and
# Python 3 — quietly skipped when either is missing or the download fails, so
# offline and JRE-less builds still configure.
option(GOLDEN_TESTS "Differential tests against the MARS reference simulator" ON)
if(GOLDEN_TESTS)
    find_package(Java QUIET COMPONENTS Runtime)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Java_JAVA_EXECUTABLE AND Python3_EXECUTABLE)
        set(MARS_JAR "${CMAKE_BINARY_DIR}/mars/Mars4_5.jar")
        set(MARS_SHA256 ac340b676ba2b62246b9df77e62f81ad4447bcfd329ab539716bcd09950b7096)
        if(NOT EXISTS "${MARS_JAR}")
            message(STATUS "Downloading MARS 4.5.1 (reference MIPS simulator)")
            file(DOWNLOAD
                    https://github.com/dpetersanderson/MARS/releases/download/v.4.5.1/Mars4_5.jar
                    "${MARS_JAR}" STATUS _mars_status)
            list(GET _mars_status 0 _mars_rc)
            if(NOT _mars_rc EQUAL 0)
                file(REMOVE "${MARS_JAR}")
            endif()
        endif()
        set(_mars_hash "")
        if(EXISTS "${MARS_JAR}")
            file(SHA256 "${MARS_JAR}" _mars_hash)
        endif()
        if(_mars_hash STREQUAL MARS_SHA256)
            add_executable(golden_runner tests/golden/golden_runner.cpp)
            target_link_libraries(golden_runner PRIVATE mips_core Microsoft.GSL::GSL clearcore_warnings)
            target_compile_features(golden_runner PUBLIC cxx_std_20)
            foreach(prog arith logic shift compare fib memdata call)
                foreach(cpu single pipelined)
                    add_test(NAME golden_${prog}_${cpu}
                            COMMAND Python3::Interpreter
                                    "${CMAKE_SOURCE_DIR}/tests/golden/run_golden.py"
                                    --java "${Java_JAVA_EXECUTABLE}"
                                    --mars-jar "${MARS_JAR}"
                                    --runner $<TARGET_FILE:golden_runner>
                                    --cpu ${cpu}
                                    "${CMAKE_SOURCE_DIR}/tests/golden/${prog}.asm")
                endforeach()
            endforeach()
        else()
            message(STATUS "GOLDEN_TESTS: MARS jar unavailable or checksum mismatch — skipping")
        endif()
    else()
        message(STATUS "GOLDEN_TESTS: Java runtime or Python3 not found — skipping")
    endif()
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
message(STATUS "Run tests: ctest --preset <preset>")
message(STATUS "")
