cmake_minimum_required(VERSION 3.16)
project(PolarHeatmapWidget VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt5 / Qt6 compatibility
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGL)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGL)

# System OpenGL
find_package(OpenGL REQUIRED)

add_executable(PolarHeatmapWidget
    main.cpp
    myglwidget.h
    myglwidget.cpp
)

target_link_libraries(PolarHeatmapWidget
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGL
    OpenGL::GL
)
