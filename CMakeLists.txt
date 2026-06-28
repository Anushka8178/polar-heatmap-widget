cmake_minimum_required(VERSION 3.16)
project(RadarApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)
find_package(OpenGL REQUIRED)

include_directories(include)

add_executable(RadarApp
    main.cpp
    MainWindow.h
    MainWindow.cpp
    include/polarpywidget.h
    src/polarpywidget.cpp
    include/rasterstripwidget.h
    src/rasterstripwidget.cpp
    include/colormap.h
    src/colormap.cpp
)

target_link_libraries(RadarApp
    Qt5::Widgets
    Qt5::OpenGL
    OpenGL::GL
)
