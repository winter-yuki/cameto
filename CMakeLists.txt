cmake_minimum_required(VERSION 3.5)

project(hw1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hw1 main.cpp)

install(TARGETS hw1
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
