cmake_minimum_required(VERSION 3.30)
project(idk)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)


FetchContent_MakeAvailable(glfw)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(fmt REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Stb REQUIRED)

set(LIBS
        glfw
        Vulkan::Vulkan
        glm::glm
        GPUOpen::VulkanMemoryAllocator
        fmt::fmt
        imgui::imgui
        tinyobjloader::tinyobjloader
        fastgltf::fastgltf
)

set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)


add_executable(${PROJECT_NAME} src/main.cpp
        base/base.cpp
        base/base.h
        base/device.cpp
        base/device.h
        base/swapchain.cpp
        base/swapchain.h
        tools/utils.cpp
        tools/utils.h
        tools/types.h
        tools/debug.h
        tools/camera.cpp
        tools/camera.h
        src/mesh.cpp
        src/mesh.h

)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})
target_include_directories(${PROJECT_NAME} PRIVATE
        "${PROJECT_SOURCE_DIR}/external/tinygltf"
)

file(GLOB_RECURSE GLSL_SOURCE_FILES
        "${PROJECT_SOURCE_DIR}/shaders/*.frag"
        "${PROJECT_SOURCE_DIR}/shaders/*.vert"
        "${PROJECT_SOURCE_DIR}/shaders/*.glsl"
)

find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found!")
endif()

message(STATUS "GLSL_SOURCE_FILES: ${GLSL_SOURCE_FILES}")

foreach(GLSL ${GLSL_SOURCE_FILES})
    message(STATUS "BUILDING SHADER")
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
    message(STATUS ${GLSL})
    message(STATUS COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV})
    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
            DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
        Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_dependencies(${PROJECT_NAME} Shaders)

