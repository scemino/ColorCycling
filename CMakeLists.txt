cmake_minimum_required(VERSION 3.16)
project(ColorCycling)

set(CMAKE_CXX_STANDARD 17)

find_package(GLEW REQUIRED)
find_package(SDL2 REQUIRED)
if (NOT WIN32)
    find_package(OpenGL REQUIRED)
endif ()

include_directories(${NGLIB_HEADERS_DIR} ${SDL2_INCLUDE_DIRS})
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES} GLEW::GLEW)
