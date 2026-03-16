cmake_minimum_required(VERSION 3.21)

project(openglStudy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FLOWCHART_EDITOR_DEFAULT_QT_PREFIX "D:/Qt/6.9.3/msvc2022_64")
set(
    FLOWCHART_EDITOR_QT_PREFIX
    "${FLOWCHART_EDITOR_DEFAULT_QT_PREFIX}"
    CACHE PATH
    "Qt installation prefix")

list(PREPEND CMAKE_PREFIX_PATH "${FLOWCHART_EDITOR_QT_PREFIX}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
qt_standard_project_setup()

set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

qt_add_executable(openglStudy
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.hpp
    src/app/RenderWidget.cpp
    src/app/RenderWidget.hpp
    src/app/SceneConfig.cpp
    src/app/SceneConfig.hpp
)

target_include_directories(openglStudy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(openglStudy PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

add_custom_command(TARGET openglStudy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ASSETS_DIR}
        $<TARGET_FILE_DIR:openglStudy>/assets
)

find_program(
    WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS "${FLOWCHART_EDITOR_QT_PREFIX}/bin")

if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET openglStudy POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --dir "$<TARGET_FILE_DIR:openglStudy>"
            "$<TARGET_FILE:openglStudy>"
    )
endif()
