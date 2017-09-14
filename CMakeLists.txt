cmake_minimum_required(VERSION 3.7)
project(Quote-CTP)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)

include_directories(include)
find_library(
        CTPAPI
        NAMES thostmduserapi
        PATHS lib/ctpapi
)
find_library(
        ICONV
        NAMES iconv
        PATHS lib/iconv
)
find_library(
        RESTCLIENT
        NAMES restclient-cpp
        PATHS lib/restclient-cpp
)

aux_source_directory(src DIR_SRC)
add_executable(Quote-CTP ${DIR_SRC})
target_link_libraries(Quote-CTP uuid pthread ${CTPAPI} ${ICONV} ${RESTCLIENT})
