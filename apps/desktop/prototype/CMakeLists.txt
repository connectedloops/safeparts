cmake_minimum_required(VERSION 3.21)
project(safeparts_ui_prototype LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets)

add_executable(safeparts-ui-prototype main.cpp)
target_link_libraries(safeparts-ui-prototype PRIVATE Qt6::Widgets)
