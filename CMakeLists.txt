cmake_minimum_required(VERSION 3.20)
project(number_system_converter CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG v7.0.0
)
FetchContent_MakeAvailable(ftxui)

# ---- Core: MIPS core
add_library(mips_core
        src/mips/decoder.cpp
        src/mips/alu.cpp
        src/mips/registers.cpp
        src/mips/memory.cpp
        src/mips/cpu.cpp
)
target_include_directories(mips_core PUBLIC include)
target_compile_features(mips_core PUBLIC cxx_std_20)

# ---- Core: pure conversion logic, no UI dependencies (unit-testable) --------
add_library(nsc_core
        src/nsc/parse.cpp
        src/nsc/format.cpp
        src/nsc/converter.cpp
)
target_include_directories(nsc_core PUBLIC include)
target_compile_features(nsc_core PUBLIC cxx_std_20)

# ---- UI: FTXUI wiring, depends on the core ----------------------------------
add_library(nsc_ui src/nsc/ui.cpp)
target_include_directories(nsc_ui PUBLIC include)
target_link_libraries(nsc_ui
        PUBLIC nsc_core
        PUBLIC mips_core
        PRIVATE ftxui::screen
        PRIVATE ftxui::dom
        PRIVATE ftxui::component
)

# ---- Executable: thin entry point -------------------------------------------
add_executable(number_system_converter src/nsc/main.cpp)
target_link_libraries(number_system_converter PRIVATE nsc_ui)

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_executable(decoder_test tests/mips/decoder_test.cpp)
add_executable(cpu_test     tests/mips/cpu_test.cpp)
add_executable(nsc_tests    tests/nsc/convert_test.cpp)
target_link_libraries(nsc_tests   PRIVATE nsc_core)
target_link_libraries(decoder_test PRIVATE mips_core)
target_link_libraries(cpu_test     PRIVATE mips_core)
add_test(NAME nsc_tests    COMMAND nsc_tests)
add_test(NAME decoder_test COMMAND decoder_test)
add_test(NAME cpu_test     COMMAND cpu_test)