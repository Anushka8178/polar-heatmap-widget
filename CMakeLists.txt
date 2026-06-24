cmake_minimum_required(VERSION 3.16)
project(UnifiedWidget LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)
find_package(OpenGL REQUIRED)

set(POLAR_DIR "${CMAKE_SOURCE_DIR}/../newproject/repo")
set(RECT_DIR  "${CMAKE_SOURCE_DIR}/../Rectangular/files")

include_directories(
    ${POLAR_DIR}
    ${RECT_DIR}
)

add_executable(UnifiedWidget
    unified_main.cpp
    ${POLAR_DIR}/polarpywidget.cpp
    ${POLAR_DIR}/colormap.cpp
    ${RECT_DIR}/rasterstripwidget.cpp
)

target_link_libraries(UnifiedWidget
    Qt5::Widgets
    Qt5::OpenGL
    OpenGL::GL
)
