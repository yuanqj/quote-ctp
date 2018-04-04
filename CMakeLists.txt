cmake_minimum_required(VERSION 3.7)
project(Quote-CTP)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)

include_directories(include)

aux_source_directory(src DIR_SRC)
add_executable(Quote-CTP ${DIR_SRC})
target_link_libraries(Quote-CTP uuid pthread thostmduserapi iconv restclient-cpp)
