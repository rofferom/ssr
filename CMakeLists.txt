cmake_minimum_required(VERSION 2.6)
project(ssr)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O2 -g -std=c++11")

include_directories(libssr/include)

set(SYSTAT_CFILES
    libssr/src/ProcFsTools.cpp
    libssr/src/SystemMonitor.cpp
    libssr/src/StructDesc.cpp
    libssr/src/SystemRecorder.cpp
    libssr/src/SysStatsMonitor.cpp
    libssr/src/ProcessMonitor.cpp
    libssr/src/Log.cpp
    libssr/src/EventLoop.cpp
    libssr/src/Timer.cpp
    libssr/src/StructDescRegistry.cpp
    src/main.cpp)

add_executable(ssr ${SYSTAT_CFILES})
target_link_libraries(ssr -lrt)

add_executable(cpuload tests/cpuload.c)
target_link_libraries(cpuload -lrt -lm -lpthread)
