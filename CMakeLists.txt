cmake_minimum_required(VERSION 3.20)
project(solaris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

add_executable(solaris main.cpp)
target_link_libraries(solaris PRIVATE glfw OpenGL::GL)
target_compile_options(solaris PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-O2>)
if (WIN32)
    target_link_libraries(solaris PRIVATE winmm)
endif()
