cmake_minimum_required(VERSION 3.22.1)
project("acousticlab")

# 1. Подключаем модуль для скачивания библиотек
include(FetchContent)

# 2. Скачиваем libspecbleach прямо с GitHub во время сборки
FetchContent_Declare(
    specbleach
    GIT_REPOSITORY https://github.com/lucianodato/libspecbleach.git
    GIT_TAG main 
)
FetchContent_MakeAvailable(specbleach)

# 3. Собираем нашу основную библиотеку
add_library(
    acousticlab
    SHARED
    native-lib.cpp
)

find_library(
    log-lib
    log
)

# 4. Линкуем наш код с только что скачанной библиотекой шумодава
target_link_libraries(
    acousticlab
    specbleach
    ${log-lib}
)
