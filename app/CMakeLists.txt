cmake_minimum_required(VERSION 3.16)
project(robot-core-controller VERSION 1.0 LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(robot-core-controller src/main.c)
target_compile_options(robot-core-controller PRIVATE -Wall -Wextra)

install(TARGETS robot-core-controller RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
