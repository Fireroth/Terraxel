cmake_minimum_required(VERSION 3.10)
project(Terraxel)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/Debug)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/Release)

# GLAD
add_library(glad STATIC lib/glad/src/glad.c)
target_include_directories(glad PUBLIC lib/glad/include)

# GLFW
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
add_subdirectory(./lib/glfw)

# GLM
include_directories(${CMAKE_SOURCE_DIR}/lib/GLM)

# IMGUI
file(GLOB IMGUI_SOURCES
    lib/imgui/*.cpp
    lib/imgui/backends/imgui_impl_glfw.cpp
    lib/imgui/backends/imgui_impl_opengl3.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC lib/imgui lib/imgui/backends)

include_directories(
    lib/
    lib/glad/include
    lib/glfw/include
    lib/nlohmannJSON
)

file(GLOB_RECURSE SRC_FILES
    src/*.cpp
)

set(RESOURCE_FILES "")
if(WIN32)
    list(APPEND RESOURCE_FILES "${CMAKE_SOURCE_DIR}/App.rc")
endif()

add_executable(Terraxel ${SRC_FILES} ${RESOURCE_FILES})

target_link_libraries(Terraxel glad glfw imgui)

if (WIN32)
    target_link_libraries(Terraxel winmm)
endif()


file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/textures/*
    ${CMAKE_SOURCE_DIR}/saves/*
    ${CMAKE_SOURCE_DIR}/shaders/*
    ${CMAKE_SOURCE_DIR}/sounds/*
    ${CMAKE_SOURCE_DIR}/structures/*
    ${CMAKE_SOURCE_DIR}/models/*
    ${CMAKE_SOURCE_DIR}/blocks/*
    ${CMAKE_SOURCE_DIR}/biomes/*
    ${CMAKE_SOURCE_DIR}/options.txt
    ${CMAKE_SOURCE_DIR}/controls.txt
    ${CMAKE_SOURCE_DIR}/Font.ttf
)

set(OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
set(ASSET_STAMP ${CMAKE_BINARY_DIR}/assets.stamp)

add_custom_command(
    OUTPUT ${ASSET_STAMP}

    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/textures ${OUTPUT_DIR}/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/saves ${OUTPUT_DIR}/saves
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders ${OUTPUT_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/sounds ${OUTPUT_DIR}/sounds
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/structures ${OUTPUT_DIR}/structures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/models ${OUTPUT_DIR}/models
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/blocks ${OUTPUT_DIR}/blocks
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/biomes ${OUTPUT_DIR}/biomes
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/options.txt" "${OUTPUT_DIR}/options.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/controls.txt" "${OUTPUT_DIR}/controls.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Font.ttf" "${OUTPUT_DIR}/Font.ttf"

    COMMAND ${CMAKE_COMMAND} -E touch ${ASSET_STAMP}
    DEPENDS ${ASSET_FILES}
)

add_custom_target(copy_assets DEPENDS ${ASSET_STAMP})
add_dependencies(Terraxel copy_assets)