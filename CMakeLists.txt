cmake_minimum_required(VERSION 3.20)
project(number_system_converter CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(ftxui
        GIT_REPOSITORY "https://github.com/ArthurSonzogni/FTXUI.git"
        GIT_TAG "v7.0.0"
)

FetchContent_Declare(GSL
        GIT_REPOSITORY "https://github.com/microsoft/GSL"
        GIT_TAG "v4.2.0"
        GIT_SHALLOW ON
)

FetchContent_MakeAvailable(ftxui GSL)

# ---- Core: MIPS core
add_library(mips_core
        src/mips/decoder.cpp
        src/mips/alu.cpp
        src/mips/registers.cpp
        src/mips/memory.cpp
        src/mips/single_cycle_cpu.cpp
        src/mips/pipelined_cpu.cpp
        src/mips/program_loader.cpp   # ← NEW
        src/mips/disassembler.cpp     # ← NEW
)
target_include_directories(mips_core PUBLIC include)
target_link_libraries(mips_core PUBLIC Microsoft.GSL::GSL)
target_compile_features(mips_core PUBLIC cxx_std_20)

# ---- Core: pure conversion logic
add_library(nsc_core
        src/nsc/parse.cpp
        src/nsc/format.cpp
        src/nsc/converter.cpp
)
target_include_directories(nsc_core PUBLIC include)
target_link_libraries(nsc_core PUBLIC Microsoft.GSL::GSL)
target_compile_features(nsc_core PUBLIC cxx_std_20)

# ---- UI: FTXUI wiring
add_library(nsc_ui src/nsc/ui.cpp)
target_include_directories(nsc_ui PUBLIC include)
target_link_libraries(nsc_ui
        PUBLIC nsc_core mips_core Microsoft.GSL::GSL
        PRIVATE ftxui::screen ftxui::dom ftxui::component Threads::Threads
)

# ---- Executable
add_executable(number_system_converter src/nsc/main.cpp)
target_link_libraries(number_system_converter PRIVATE nsc_ui)

# ---- Qt6 GUI (optional) ----
option(BUILD_QT6_UI "Build GUI with Qt6" ON)

if(BUILD_QT6_UI)
    find_package(Qt6 6.4 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets QUIET)

    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "Qt6 not found. Install it with:\n"
            "  Fedora/RHEL: sudo dnf install qt6-qtbase-devel qt6-qtbase-gui\n"
            "  Ubuntu/Debian: sudo apt install qt6-base-dev\n"
            "  macOS: brew install qt@6\n"
            "Or re-run cmake with -DBUILD_QT6_UI=OFF to skip the GUI."
        )
    endif()

    add_library(nsc_qt_ui OBJECT
        src/nsc_qt/assembler.cpp
    )
    set_target_properties(nsc_qt_ui PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
    target_include_directories(nsc_qt_ui PUBLIC include)
    target_compile_options(nsc_qt_ui PRIVATE -Wall -Wextra)
    target_compile_features(nsc_qt_ui PUBLIC cxx_std_20)
    target_link_libraries(nsc_qt_ui
        PUBLIC  mips_core Microsoft.GSL::GSL Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    )

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
        AUTOMOC ON
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON
    )
    target_include_directories(clearCore-gui PRIVATE include)
    target_compile_options(clearCore-gui PRIVATE -Wall -Wextra)
    target_compile_features(clearCore-gui PUBLIC cxx_std_20)
    target_link_libraries(clearCore-gui PRIVATE mips_core Microsoft.GSL::GSL Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

    # Qt UI tests (Step 16)
    add_executable(qt_ui_test tests/qt_ui/qt_ui_test.cpp)
    set_target_properties(qt_ui_test PROPERTIES AUTOMOC ON)
    target_link_libraries(qt_ui_test PRIVATE nsc_qt_ui Qt6::Widgets)
    add_test(NAME qt_ui_test COMMAND qt_ui_test)
endif()

# ---- Tests
enable_testing()
add_executable(decoder_test tests/mips/decoder_test.cpp)
add_executable(cpu_test tests/mips/cpu_test.cpp)
add_executable(nsc_tests tests/nsc/convert_test.cpp)
target_link_libraries(decoder_test PRIVATE mips_core Microsoft.GSL::GSL)
target_link_libraries(cpu_test PRIVATE mips_core Microsoft.GSL::GSL)
target_link_libraries(nsc_tests PRIVATE nsc_core Microsoft.GSL::GSL)
add_test(NAME decoder_test COMMAND decoder_test)
add_test(NAME cpu_test COMMAND cpu_test)
add_test(NAME nsc_tests COMMAND nsc_tests)