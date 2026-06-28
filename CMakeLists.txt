cmake_minimum_required(VERSION 4.2)
project(number_system_converter)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v7.0.0
)
FetchContent_MakeAvailable(ftxui)

add_executable(number_system_converter main.cpp)
target_link_libraries(number_system_converter
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
)