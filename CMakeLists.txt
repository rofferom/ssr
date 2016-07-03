cmake_minimum_required(VERSION 2.6)
project(sysstatsrec)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -std=c++11")

set(SYSTAT_CFILES src/main.cpp src/ProcFsTools.cpp src/SystemMonitor.cpp src/SystemRecorder.cpp)

add_executable(sysstatsrec ${SYSTAT_CFILES})
target_link_libraries(sysstatsrec -lrt)
