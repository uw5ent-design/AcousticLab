cmake_minimum_required(VERSION 3.22.1)
project("acousticlab")

add_library(
    acousticlab
    SHARED
    native-lib.cpp
)

find_library(
    log-lib
    log
)

target_link_libraries(
    acousticlab
    ${log-lib}
)
