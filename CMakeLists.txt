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

# ---- Core: pure conversion logic, no UI dependencies (unit-testable) --------
add_library(nsc_core
        src/parse.cpp
        src/format.cpp
        src/converter.cpp
        src/parse.cpp
)
target_include_directories(nsc_core PUBLIC include)
target_compile_features(nsc_core PUBLIC cxx_std_20)

# ---- UI: FTXUI wiring, depends on the core ----------------------------------
add_library(nsc_ui src/ui.cpp)
target_include_directories(nsc_ui PUBLIC include)
target_link_libraries(nsc_ui
        PUBLIC nsc_core
        PRIVATE ftxui::screen
        PRIVATE ftxui::dom
        PRIVATE ftxui::component
)

# ---- Executable: thin entry point -------------------------------------------
add_executable(number_system_converter src/main.cpp)
target_link_libraries(number_system_converter PRIVATE nsc_ui)

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_executable(nsc_tests tests/convert_test.cpp)
target_link_libraries(nsc_tests PRIVATE nsc_core)
add_test(NAME nsc_tests COMMAND nsc_tests)