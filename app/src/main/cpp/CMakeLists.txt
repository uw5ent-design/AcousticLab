cmake_minimum_required(VERSION 3.22.1)
project("acousticlab")

# Ищем библиотеку Oboe
find_package(oboe REQUIRED CONFIG)

add_library(
    acousticlab
    SHARED
    native-lib.cpp
)

find_library(
    log-lib
    log
)

# Линкуем нашу библиотеку с Oboe и логами
target_link_libraries(
    acousticlab
    oboe::oboe
    ${log-lib}
)
