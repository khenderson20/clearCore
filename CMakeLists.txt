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
#   -DBUILD_NYXSTONE=ON|OFF      Nyxstone assembler/disassembler (needs LLVM 15-20 + clang)
#   -DBUILD_QT6_UI=ON|OFF        Qt6 Widgets GUI
#   -DBUILD_QT6_QUICK_UI=ON|OFF  Qt Quick (QML) GUI
#   -DENABLE_SANITIZERS=ON|OFF   ASan + UBSan
#
# Dependencies (FTXUI, GSL, spdlog, Nyxstone are auto-downloaded; Qt/LLVM are not):
#   Fedora/RHEL:   sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel llvm19-devel clang19-devel
#   Ubuntu/Debian: sudo apt install cmake build-essential qt6-base-dev qt6-declarative-dev llvm-19-dev libclang-19-dev
#   macOS:         brew install cmake qt@6 llvm@19
#
# Nyxstone v0.1.8 supports LLVM 15-20 only. If your system default LLVM is newer
# (21+), install an in-range one and point Nyxstone at it, e.g.:
#   NYXSTONE_LLVM_PREFIX=/usr/lib64/llvm19 cmake --preset debug
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
if (ENABLE_SANITIZERS)
    message(STATUS "Sanitizers enabled: address, undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif ()

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
# Uses spdlog's bundled fmt copy to avoid cross-version API mismatches.
# The ClusterFuzzLite fuzz build overrides this with SPDLOG_USE_STD_FORMAT=ON
# so Clang 22+ uses C++20 <format> instead, sidestepping fmt entirely.
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
# Force the bundled fmt. We never provide an external fmt package, so if
# SPDLOG_FMT_EXTERNAL is ever left ON (e.g. a stale cache value or an IDE-set
# CMake option), spdlog's own CMakeLists runs find_package(fmt CONFIG REQUIRED)
# and configuration fails. FORCE overrides any such cached/command-line value.
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "" FORCE)

FetchContent_Declare(spdlog
        GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
        GIT_TAG "v1.14.1"
        GIT_SHALLOW ON
)

# These are private, statically-linked build dependencies — their headers,
# static libs, and CMake config files must not leak into our install tree or
# CPack packages. Disable each project's own install() rules before it is
# added, so `cmake --install` / CPack ship only clearCore's binaries and docs.
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(GSL_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

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

if (BUILD_NYXSTONE)
    message(STATUS "Configuring Nyxstone assembler/disassembler framework...")

    # Nyxstone v0.1.8 supports LLVM 15–20 only: it relies on LLVM's internal MC
    # headers, which shift every major version. Newer LLVM (21+) does not build.
    set(NYXSTONE_LLVM_MIN 15)
    set(NYXSTONE_LLVM_MAX 20)

    # Note: find_package(LLVM <version>) demands an *exact* major.minor match
    # (LLVM's config-version file is not "any newer"), so we must query
    # unversioned and range-check the major ourselves. Honour Nyxstone's own
    # NYXSTONE_LLVM_PREFIX convention so a pinned in-range install is found even
    # when a newer system LLVM (e.g. 22) is the default.
    set(_llvm_hints "")
    if (DEFINED ENV{NYXSTONE_LLVM_PREFIX})
        list(APPEND _llvm_hints
                "$ENV{NYXSTONE_LLVM_PREFIX}/lib/cmake/llvm"
                "$ENV{NYXSTONE_LLVM_PREFIX}/lib64/cmake/llvm")
    endif ()

    find_package(LLVM QUIET CONFIG
            NAMES LLVM llvm
            HINTS ${_llvm_hints} ENV LLVM_DIR
    )

    if (LLVM_FOUND AND LLVM_VERSION_MAJOR GREATER_EQUAL NYXSTONE_LLVM_MIN
                   AND LLVM_VERSION_MAJOR LESS_EQUAL NYXSTONE_LLVM_MAX)
        message(STATUS "  ✓ Found LLVM ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR} at ${LLVM_DIR}")

        FetchContent_Declare(nyxstone
                GIT_REPOSITORY "https://github.com/emproof-com/nyxstone.git"
                GIT_TAG "v0.1.8"  # Pinned for reproducible builds
                GIT_SHALLOW ON
        )
        FetchContent_MakeAvailable(nyxstone)

        if (TARGET nyxstone)
            # Nyxstone v0.1.8's CMake hardcodes ${CMAKE_SOURCE_DIR}/include for
            # its public headers, which only resolves correctly when Nyxstone is
            # the top-level project. Under FetchContent ${CMAKE_SOURCE_DIR} is
            # *our* tree, so nyxstone.h is not found. Re-point the target at the
            # fetched source's real include/vendor directories.
            target_include_directories(nyxstone PUBLIC
                    $<BUILD_INTERFACE:${nyxstone_SOURCE_DIR}/include>
                    $<BUILD_INTERFACE:${nyxstone_SOURCE_DIR}/vendor>)
            message(STATUS "  ✓ Built Nyxstone from source (CMake target found)")
            set(NYXSTONE_TARGET nyxstone)
        else ()
            message(WARNING "Nyxstone target not found. Build may have failed. Disabling.")
            set(BUILD_NYXSTONE OFF)
        endif ()
    elseif (LLVM_FOUND)
        message(WARNING
                "Found LLVM ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}, but Nyxstone v0.1.8 "
                "supports LLVM ${NYXSTONE_LLVM_MIN}-${NYXSTONE_LLVM_MAX} only. Disabling "
                "Nyxstone. Install an in-range LLVM (e.g. 'sudo dnf install llvm19-devel "
                "clang19-devel') and point Nyxstone at it with "
                "-DCMAKE_PREFIX_PATH or the NYXSTONE_LLVM_PREFIX env var "
                "(e.g. NYXSTONE_LLVM_PREFIX=/usr/lib64/llvm19)."
        )
        set(BUILD_NYXSTONE OFF)
    else ()
        message(WARNING
                "LLVM not found; disabling Nyxstone. Install an in-range LLVM "
                "(${NYXSTONE_LLVM_MIN}-${NYXSTONE_LLVM_MAX}): llvm19-devel + clang19-devel "
                "(Fedora), llvm-19-dev (Debian/Ubuntu), or brew install llvm@19 (macOS), "
                "then set NYXSTONE_LLVM_PREFIX or -DLLVM_DIR=/path/to/llvm/cmake."
        )
        set(BUILD_NYXSTONE OFF)
    endif ()
else ()
    message(STATUS "Nyxstone: disabled (set -DBUILD_NYXSTONE=ON to enable)")
    set(NYXSTONE_TARGET "")
endif ()

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
        src/mips/cp0.cpp              # Coprocessor 0: exception model (Status/Cause/EPC)
        src/mips/elf_loader.cpp       # MIPS ELF32 (mipsel) binary loader
        src/mips/trace.cpp            # Shared spdlog logger (instruction/hazard/exception tracing)
        src/mips/nyxstone_backend.cpp # Nyxstone (LLVM MC) assemble/disassemble wrapper (guarded)
)

# ── GDB RSP stub (optional — requires POSIX sockets; excluded on non-POSIX) ──
option(BUILD_GDB_STUB "Build the GDB Remote Serial Protocol stub (requires POSIX sockets)" ON)
if (BUILD_GDB_STUB)
    include(CheckIncludeFile)
    check_include_file("sys/socket.h" HAVE_SYS_SOCKET_H)
    if (HAVE_SYS_SOCKET_H)
        target_sources(mips_core PRIVATE src/mips/gdb_stub.cpp)
        target_compile_definitions(mips_core PUBLIC CLEARCORE_GDB_STUB_ENABLED=1)
        message(STATUS "GDB stub: enabled (POSIX sockets found)")
    else ()
        message(WARNING "sys/socket.h not found — GDB stub disabled; "
                        "use -DBUILD_GDB_STUB=OFF to suppress this warning")
        set(BUILD_GDB_STUB OFF)
        target_compile_definitions(mips_core PUBLIC CLEARCORE_GDB_STUB_ENABLED=0)
    endif ()
else ()
    target_compile_definitions(mips_core PUBLIC CLEARCORE_GDB_STUB_ENABLED=0)
    message(STATUS "GDB stub: disabled (set -DBUILD_GDB_STUB=ON to enable)")
endif ()

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
if (BUILD_NYXSTONE AND NYXSTONE_TARGET)
    target_include_directories(mips_core PRIVATE ${LLVM_INCLUDE_DIRS})
    target_link_libraries(mips_core PRIVATE ${NYXSTONE_TARGET} ${LLVM_LIBRARIES})
    target_compile_definitions(mips_core PUBLIC CLEARCORE_NYXSTONE_ENABLED=1)
    message(STATUS "mips_core: Nyxstone assembler/disassembler support enabled")
else ()
    target_compile_definitions(mips_core PUBLIC CLEARCORE_NYXSTONE_ENABLED=0)
    message(STATUS "mips_core: Nyxstone support disabled")
endif ()

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

if (BUILD_QT6_UI)
    find_package(Qt6 6.4
            COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets
            QUIET
    )
    if (NOT Qt6_FOUND)
        # Degrade gracefully so headless/CI machines can still build and test
        # the core libraries.
        message(WARNING
                "Qt6 not found (install qt6-qtbase-devel / qt6-base-dev / brew qt@6). "
                "Skipping clearCore-gui, or disable with -DBUILD_QT6_UI=OFF"
        )
        set(BUILD_QT6_UI OFF)
    endif ()
endif ()

if (BUILD_QT6_UI)
    message(STATUS "Qt6 found: Building desktop GUI")
    # Configures AUTOMOC/AUTORCC and, on Windows, sets DLL runtime output
    # directories so Qt6Core.dll is found by test runners in the build tree.
    qt_standard_project_setup()

    # Qt Advanced Docking System (LGPL-2.1) — IDE-style dockable panels for
    # the GUI. Built as a shared library (QADS default), which is what keeps
    # the LGPL obligations trivial for this MIT project.
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)  # QADS example apps
    FetchContent_Declare(qtads
            GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
            GIT_TAG 4.5.0
    )
    FetchContent_MakeAvailable(qtads)

    # QHexView (MIT) — the hex-editor widget behind the Memory panel.
    FetchContent_Declare(qhexview
            GIT_REPOSITORY https://github.com/Dax89/QHexView.git
            GIT_TAG v5.1.1
    )
    FetchContent_MakeAvailable(qhexview)

    add_executable(clearCore-gui
            src/nsc_qt/main.cpp
            src/nsc_qt/assembler.cpp
            src/nsc_qt/examples.cpp
            src/nsc_qt/simulator_controller.cpp
            src/nsc_qt/main_window.cpp
            src/nsc_qt/dock_panels.cpp
            src/nsc_qt/preferences_dialog.cpp
            src/nsc_qt/widgets/code_editor.cpp
            src/nsc_qt/widgets/datapath_widget.cpp
            src/nsc_qt/widgets/schematic_datapath_widget.cpp
            src/nsc_qt/widgets/register_widget.cpp
            src/nsc_qt/widgets/memory_widget.cpp
            src/nsc_qt/widgets/pipeline_trace_widget.cpp
            src/nsc_qt/widgets/pipeline_events_widget.cpp

            # Headers listed so AUTOMOC picks up Q_OBJECT classes
            include/nsc_qt/simulator_controller.h
            include/nsc_qt/main_window.h
            include/nsc_qt/preferences_dialog.h
            include/nsc_qt/ui_scale.h
            include/nsc_qt/examples.h
            include/nsc_qt/widgets/code_editor.h
            include/nsc_qt/widgets/datapath_widget.h
            include/nsc_qt/widgets/schematic_datapath_widget.h
            include/nsc_qt/widgets/register_widget.h
            include/nsc_qt/widgets/memory_widget.h
            include/nsc_qt/widgets/pipeline_trace_widget.h
            include/nsc_qt/widgets/pipeline_events_widget.h
    )

    set_target_properties(clearCore-gui PROPERTIES
            AUTOMOC ON                # Run moc on headers with Q_OBJECT
            AUTORCC ON                # Process .qrc resource files
            WIN32_EXECUTABLE ON       # Hide console window on Windows
            MACOSX_BUNDLE ON          # Create .app bundle on macOS
    )

    # The Qt Advanced Docking System is a shared library; the packaged
    # clearCore-gui needs a runtime search path to find it (and, on macOS, the
    # bundled Qt frameworks) — without one it fails to launch.
    if (UNIX AND NOT APPLE)
        # CPack installs QADS to lib/ next to the bin/ binary; point there so the
        # plain .tar.gz runs without LD_LIBRARY_PATH ("cannot open shared object
        # file"). lib64 is covered too for distros/GNUInstallDirs that use it.
        set_target_properties(clearCore-gui PROPERTIES
                INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN/../lib64"
        )
    elseif (APPLE)
        # macdeployqt bundles Qt and QADS into clearCore-gui.app/Contents/
        # Frameworks; @executable_path/../Frameworks is where the installed
        # binary resolves them at runtime (without it the app aborts with
        # "Library not loaded: @rpath/QtOpenGLWidgets.framework ... no LC_RPATH's
        # found").
        #
        # The trailing entry is what lets the bundling succeed in the first
        # place: clearCore-gui links QADS via @rpath/libqtadvanceddocking-qt6.4
        # .dylib, and macdeployqt resolves that dependency through the binary's
        # LC_RPATHs. Exposing QADS's build-tree directory gives macdeployqt a
        # path to the real dylib at deploy time — otherwise it errors ("Cannot
        # resolve rpath ...") and skips bundling QtOpenGLWidgets. That entry only
        # has to exist during `cmake --install`; it is a harmless dead path in
        # the shipped bundle. (QADS stays shared for LGPL compliance — see above
        # — so a static build is not an option here.)
        set_target_properties(clearCore-gui PROPERTIES
                BUILD_RPATH   "@executable_path/../Frameworks;@loader_path/../Frameworks"
                INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks;$<TARGET_FILE_DIR:qtadvanceddocking-qt6>"
        )
    endif ()

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
            ads::qtadvanceddocking-qt6
            QHexView
            clearcore_warnings
    )

    # Optional: KSyntaxHighlighting (MIT since KF 5.50) gives the Code Editor
    # real MIPS assembly highlighting using Kate's syntax definitions.
    # System package: kf6-syntax-highlighting-devel (Fedora) or
    # libkf6syntaxhighlighting-dev (Ubuntu >= 25.10). Without it the editor
    # is plain text, exactly as before.
    find_package(KF6SyntaxHighlighting QUIET)
    if (KF6SyntaxHighlighting_FOUND)
        message(STATUS "KSyntaxHighlighting found: Code Editor gets MIPS syntax highlighting")
        target_link_libraries(clearCore-gui PRIVATE KF6::SyntaxHighlighting)
        target_compile_definitions(clearCore-gui PRIVATE HAVE_KSYNTAXHIGHLIGHTING)
    else ()
        message(STATUS "KSyntaxHighlighting not found: Code Editor stays plain text")
    endif ()
else ()
    message(STATUS "Qt6 GUI disabled (set -DBUILD_QT6_UI=ON to enable)")
endif ()

# ---- clearCore-quick: Qt Quick / QML desktop GUI (optional) ----
#
# Single-window QML front end; reuses mips_core plus the framework-agnostic
# pieces of the Widgets GUI (SimulatorController, assembler, examples).
# Needs qt6-qtdeclarative-devel (Fedora) / qt6-declarative-dev (Debian).
#
option(BUILD_QT6_QUICK_UI "Build Qt Quick (QML) desktop GUI" ON)

if (BUILD_QT6_QUICK_UI)
    find_package(Qt6 6.5
            COMPONENTS Core Gui Qml Quick QuickControls2
            QUIET
    )

    if (NOT Qt6Quick_FOUND)
        message(WARNING
                "Qt6 Quick not found (install qt6-qtdeclarative-devel / qt6-declarative-dev). "
                "Skipping clearCore-quick, or disable with -DBUILD_QT6_QUICK_UI=OFF"
        )
        set(BUILD_QT6_QUICK_UI OFF)
    else ()
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
    endif ()
else ()
    message(STATUS "Qt Quick GUI disabled (set -DBUILD_QT6_QUICK_UI=ON to enable)")
endif ()

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

clearcore_add_test(decoder_test tests/mips/decoder_test.cpp mips_core)  # Instruction decoding
clearcore_add_test(cpu_test tests/mips/cpu_test.cpp mips_core)  # CPU execution (both models)
clearcore_add_test(processor_test tests/mips/processor_test.cpp mips_core)  # IProcessor contract
clearcore_add_test(disasm_test tests/mips/disasm_test.cpp mips_core)  # Disassembler + hex loader
clearcore_add_test(cp0_test tests/mips/cp0_test.cpp mips_core)  # CP0 exception model
clearcore_add_test(elf_loader_test tests/mips/elf_loader_test.cpp mips_core)  # ELF32 loader
clearcore_add_test(nsc_tests tests/nsc/convert_test.cpp nsc_core)   # Number conversion

# ---- nyxstone_test: differential Decoder/Disassembler vs LLVM (if enabled) ----
# Only meaningful when Nyxstone was actually built; mips_core carries the
# CLEARCORE_NYXSTONE_ENABLED definition either way, so the source also compiles
# to a passing no-op when disabled.
if (BUILD_NYXSTONE AND NYXSTONE_TARGET)
    clearcore_add_test(nyxstone_test tests/mips/nyxstone_test.cpp mips_core)
endif ()

# ---- qt_ui_test: exercises assembler/controller/widgets (if Qt6 enabled) ----
if (BUILD_QT6_UI)
    # The units under test live in the clearCore-gui target, not a library,
    # so compile them into the test too.
    add_executable(qt_ui_test
            tests/qt_ui/qt_ui_test.cpp
            src/nsc_qt/assembler.cpp
            src/nsc_qt/simulator_controller.cpp
            src/nsc_qt/dock_panels.cpp                       # dock-content invariant under test
            src/nsc_qt/widgets/register_widget.cpp
            src/nsc_qt/widgets/memory_widget.cpp
            src/nsc_qt/widgets/pipeline_trace_widget.cpp
            src/nsc_qt/widgets/pipeline_events_widget.cpp
            include/nsc_qt/simulator_controller.h            # (moc)
            include/nsc_qt/widgets/register_widget.h         # (moc)
            include/nsc_qt/widgets/memory_widget.h           # (moc)
            include/nsc_qt/widgets/pipeline_trace_widget.h   # (moc)
            include/nsc_qt/widgets/pipeline_events_widget.h  # (moc)
    )
    set_target_properties(qt_ui_test PROPERTIES AUTOMOC ON)
    target_include_directories(qt_ui_test PRIVATE include)
    target_link_libraries(qt_ui_test PRIVATE
            Qt6::Widgets
            mips_core
            QHexView  # memory_widget.cpp is compiled into the test
            ads::qtadvanceddocking-qt6  # dock_panels.cpp exercises the docking API
            clearcore_warnings
    )
    target_compile_features(qt_ui_test PUBLIC cxx_std_20)
    add_test(NAME qt_ui_test COMMAND qt_ui_test)

    # QApplication needs a windowing platform; Qt's offscreen backend lets the
    # test run on headless machines and CI runners without a display.
    # TIMEOUT: Qt plugin discovery failures on Windows produce a WER dialog
    # (no stdout, blocks forever) rather than a clean process exit — cap it.
    set_tests_properties(qt_ui_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 120
    )
endif ()

# ---- MARS golden tests: differential testing against a reference simulator ----
# Each corpus program in tests/golden/ is assembled and executed by MARS (the
# classroom-standard MIPS simulator, MIT licence) and by both clearCore CPU
# models; the register files must match exactly. Requires a Java runtime and
# Python 3 — quietly skipped when either is missing or the download fails, so
# offline and JRE-less builds still configure.
option(GOLDEN_TESTS "Differential tests against the MARS reference simulator" ON)
if (GOLDEN_TESTS)
    find_package(Java QUIET COMPONENTS Runtime)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if (Java_JAVA_EXECUTABLE AND Python3_EXECUTABLE)
        set(MARS_JAR "${CMAKE_BINARY_DIR}/mars/Mars4_5.jar")
        set(MARS_SHA256 ac340b676ba2b62246b9df77e62f81ad4447bcfd329ab539716bcd09950b7096)
        if (NOT EXISTS "${MARS_JAR}")
            message(STATUS "Downloading MARS 4.5.1 (reference MIPS simulator)")
            file(DOWNLOAD
                    https://github.com/dpetersanderson/MARS/releases/download/v.4.5.1/Mars4_5.jar
                    "${MARS_JAR}" STATUS _mars_status)
            list(GET _mars_status 0 _mars_rc)
            if (NOT _mars_rc EQUAL 0)
                file(REMOVE "${MARS_JAR}")
            endif ()
        endif ()
        set(_mars_hash "")
        if (EXISTS "${MARS_JAR}")
            file(SHA256 "${MARS_JAR}" _mars_hash)
        endif ()
        if (_mars_hash STREQUAL MARS_SHA256)
            add_executable(golden_runner tests/golden/golden_runner.cpp)
            target_link_libraries(golden_runner PRIVATE mips_core Microsoft.GSL::GSL clearcore_warnings)
            target_compile_features(golden_runner PUBLIC cxx_std_20)
            foreach (prog arith logic shift compare fib memdata call)
                foreach (cpu single pipelined)
                    add_test(NAME golden_${prog}_${cpu}
                            COMMAND Python3::Interpreter
                            "${CMAKE_SOURCE_DIR}/tests/golden/run_golden.py"
                            --java "${Java_JAVA_EXECUTABLE}"
                            --mars-jar "${MARS_JAR}"
                            --runner $<TARGET_FILE:golden_runner>
                            --cpu ${cpu}
                            "${CMAKE_SOURCE_DIR}/tests/golden/${prog}.asm")
                endforeach ()
            endforeach ()
        else ()
            message(STATUS "GOLDEN_TESTS: MARS jar unavailable or checksum mismatch — skipping")
        endif ()
    else ()
        message(STATUS "GOLDEN_TESTS: Java runtime or Python3 not found — skipping")
    endif ()
endif ()

# ============================================================================
# FUZZ TARGETS  (ClusterFuzzLite / libFuzzer)
# ============================================================================
#
# Pass -DFUZZING_ENGINE=<engine> at configure time to build fuzz harnesses.
# The ClusterFuzzLite build.sh sets this from the $LIB_FUZZING_ENGINE env var
# injected by the base-builder image (e.g. /usr/lib/libFuzzer.a or
# -fsanitize=fuzzer). Not built in normal developer or CI builds.
#
if (FUZZING_ENGINE)
    add_executable(fuzz_hex_loader tests/fuzz/fuzz_hex_loader.cpp)
    target_link_libraries(fuzz_hex_loader PRIVATE mips_core clearcore_warnings ${FUZZING_ENGINE})
    target_compile_features(fuzz_hex_loader PUBLIC cxx_std_20)
    message(STATUS "Fuzz targets enabled (engine: ${FUZZING_ENGINE})")
endif ()

# ============================================================================
# INSTALL RULES
# ============================================================================
#
# Define where `cmake --install` (and therefore CPack and AppImage tooling)
# places each artifact. GNUInstallDirs gives portable, platform-correct
# destinations (bin/, share/doc/, etc.). Only the runnable applications are
# installed — the static core libraries are internal build artifacts, not a
# public dev package.
#
include(GNUInstallDirs)

# The terminal UI is always built.
install(TARGETS number_system_converter
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE  DESTINATION .
)

if (BUILD_QT6_UI)
    install(TARGETS clearCore-gui
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            BUNDLE  DESTINATION .
    )

    # Desktop-entry and icon for the Widgets GUI. These give DEB/RPM packages a
    # menu entry, and are what linuxdeploy reads to build the AppImage (its
    # Exec= names the binary, Icon= names the 256x256 PNG below).
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/clearCore.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/clearCore.png"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    )
endif ()

if (BUILD_QT6_QUICK_UI)
    install(TARGETS clearCore-quick
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            BUNDLE  DESTINATION .
    )
endif ()

# Ship the license and top-level docs alongside the binaries.
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# ============================================================================
# SELF-CONTAINED BUNDLING (Windows / macOS installers)
# ============================================================================
# For standalone Windows/macOS installers the Qt runtime must ship inside the
# package so the app launches on a machine with no system Qt. OFF by default so
# Linux DEB/RPM (which declare Qt as a package dependency), the AppImage flow,
# and plain developer installs are unaffected; the cross-platform release
# workflow turns it ON.
option(CLEARCORE_BUNDLE_QT "Bundle the Qt runtime into the install tree / installers" OFF)

if (CLEARCORE_BUNDLE_QT AND (BUILD_QT6_UI OR BUILD_QT6_QUICK_UI))
    # On macOS the GUI apps must be .app bundles for macdeployqt and the .dmg.
    if (APPLE)
        foreach (gui_target clearCore-gui clearCore-quick)
            if (TARGET ${gui_target})
                set_target_properties(${gui_target} PROPERTIES MACOSX_BUNDLE ON)
            endif ()
        endforeach ()
    endif ()

    # On Windows, also drag in the MSVC runtime redistributable DLLs.
    if (WIN32)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
        include(InstallRequiredSystemLibraries)
    endif ()

    # qt_generate_deploy_app_script hooks windeployqt/macdeployqt into
    # `cmake --install`, so CPack automatically gathers Qt (and, for the QML
    # app, its QML imports) next to the executables.
    if (TARGET clearCore-gui)
        qt_generate_deploy_app_script(
                TARGET clearCore-gui
                OUTPUT_SCRIPT deploy_clearcore_gui
                NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT ${deploy_clearcore_gui})
    endif ()
    if (TARGET clearCore-quick)
        qt_generate_deploy_app_script(
                TARGET clearCore-quick
                OUTPUT_SCRIPT deploy_clearcore_quick
                NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT ${deploy_clearcore_quick})
    endif ()
endif ()

# ============================================================================
# PACKAGING (CPack)
# ============================================================================
#
# Build installable packages from the install tree:
#   cmake --build --preset release
#   cpack --config build/release/CPackConfig.cmake            # -> .tar.gz (default)
#   cpack --config build/release/CPackConfig.cmake -G DEB     # -> .deb   (needs dpkg)
#   cpack --config build/release/CPackConfig.cmake -G RPM     # -> .rpm   (needs rpmbuild)
#
# NOTE: these packages contain the clearCore binaries but NOT bundled Qt or the
# Qt Advanced Docking System shared library — DEB/RPM record those as
# dependencies (resolved by apt/dnf), while the plain .tar.gz assumes Qt6 is
# already present. A self-contained, dependency-free artifact (AppImage) is a
# separate deployment step layered on top of these install rules.
#
set(CPACK_PACKAGE_NAME "clearCore")
set(CPACK_PACKAGE_VENDOR "Kevin Henderson")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/khenderson20/clearCore")
set(CPACK_PACKAGE_CONTACT "Kevin Henderson <kevin.d.hende@gmail.com>")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Default generator per platform (overridable with `cpack -G`): a native
# installer on Windows/macOS, a portable archive elsewhere.
if (NOT CPACK_GENERATOR)
    if (WIN32)
        set(CPACK_GENERATOR "NSIS")
    elseif (APPLE)
        set(CPACK_GENERATOR "DragNDrop")
    else ()
        set(CPACK_GENERATOR "TGZ")
    endif ()
endif ()

# ── Windows NSIS installer ──────────────────────────────────────────────────
set(CPACK_PACKAGE_INSTALL_DIRECTORY "clearCore")
set(CPACK_NSIS_PACKAGE_NAME "clearCore")
set(CPACK_NSIS_DISPLAY_NAME "clearCore")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Start-menu / desktop shortcuts. Pairs of <exe-without-extension> ; <label>.
# The TUI is a console program, so it gets no GUI shortcut.
set(CPACK_PACKAGE_EXECUTABLES
        "clearCore-gui"   "clearCore (Qt Widgets)"
        "clearCore-quick" "clearCore (QML)"
)
set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\clearCore (Qt Widgets).lnk' '$INSTDIR\\\\${CMAKE_INSTALL_BINDIR}\\\\clearCore-gui.exe'"
)

# ── macOS drag-and-drop .dmg ────────────────────────────────────────────────
set(CPACK_DMG_VOLUME_NAME "clearCore ${PROJECT_VERSION}")

# Source packages (`cpack --config CPackSourceConfig.cmake`) exclude build
# output, VCS, and IDE metadata.
set(CPACK_SOURCE_GENERATOR "TGZ")
# Regex escapes need four backslashes here: CMake collapses "\\\\" to "\\" in
# this string, and CPack writes that value verbatim into CPackConfig.cmake, so
# the generated file carries a valid "\\." escape. Only "\\." (two backslashes)
# in source would land a lone "\." in the generated config, which newer CMake
# flags as an invalid escape sequence when cpack re-parses it.
set(CPACK_SOURCE_IGNORE_FILES
        "/\\\\.git/"
        "/build/"
        "/cmake-build-.*/"
        "/\\\\.idea/"
        "\\\\.user$"
)

# Debian-specific fields (used only with -G DEB). SHLIBDEPS auto-detects the
# Qt / QADS shared-library dependencies from the linked binaries.
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Kevin Henderson <kevin.d.hende@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# RPM-specific fields (used only with -G RPM). AUTOREQ auto-detects deps.
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

include(CPack)

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
message(STATUS "  • number_system_converter (TUI + MIPS simulator)")
if (BUILD_QT6_UI)
    message(STATUS "  • clearCore-gui (Qt6 Widgets desktop GUI)")
endif ()
if (BUILD_QT6_QUICK_UI)
    message(STATUS "  • clearCore-quick (Qt Quick / QML desktop GUI)")
endif ()
message(STATUS "")
message(STATUS "Run tests: ctest --preset <preset>")
message(STATUS "")
