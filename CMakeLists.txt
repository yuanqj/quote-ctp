cmake_minimum_required(VERSION 3.7)
project(quote_ctp)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)

find_package(Boost REQUIRED COMPONENTS
        filesystem
)
include_directories(include)
aux_source_directory(src DIR_SRC)
add_executable(${PROJECT_NAME} ${DIR_SRC})
target_link_libraries(${PROJECT_NAME} uuid pthread ${Boost_LIBRARIES} thostmduserapi iconv)
