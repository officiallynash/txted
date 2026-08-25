cmake_minimum_required(VERSION 3.20)
project(txted C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -O2 -fsanitize=leak)

file(GLOB_RECURSE SOURCES "src/*.c")

add_executable(txted ${SOURCES})

target_include_directories(txted PRIVATE include)

target_link_libraries(txted PRIVATE 
    raylib GL m pthread dl rt X11 
    tree-sitter tree-sitter-c 
)
