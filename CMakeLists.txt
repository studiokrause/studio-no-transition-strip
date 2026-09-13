cmake_minimum_required(VERSION 3.20)

project(studio-no-transition-strip VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(OBS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-sdk-installed")
set(QT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-07-11-x64")

list(APPEND CMAKE_PREFIX_PATH "${QT_DIR}")
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${OBS_SDK_DIR}/include"
)

# Generate plugin-support.c
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-support.c.in"
    "${CMAKE_CURRENT_BINARY_DIR}/plugin-support.c"
    @ONLY
)

add_library(${PROJECT_NAME} MODULE
    src/plugin-main.cpp
    src/studio-strip-manager.cpp
    src/studio-strip-manager.hpp
    src/plugin-support.h
    "${CMAKE_CURRENT_BINARY_DIR}/plugin-support.c"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    "${OBS_SDK_DIR}/lib/obs.lib"
    "${OBS_SDK_DIR}/lib/obs-frontend-api.lib"
)

# Avoid prefix 'lib' on Windows
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${PROJECT_NAME}"
)

# Copy plugin and locales to output dir after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/data/obs-plugins/${PROJECT_NAME}/locale"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data/locale"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/data/obs-plugins/${PROJECT_NAME}/locale"
)
