cmake_minimum_required(VERSION 3.16)
project(markov LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(markov main.cpp)

if(MSVC)
    target_compile_options(markov PRIVATE /W4 /O2)
else()
    target_compile_options(markov PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

