cmake_minimum_required(VERSION 3.14)
project(AgentOrderbookSimulation)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(includes INTERFACE)
target_include_directories(includes INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

set(SOURCES
    src/Orderbook.cpp
    src/MatchingEngine.cpp
    src/TradeDispatcher.cpp
    src/Agent.cpp
    src/AgentManager.cpp
    src/AgentStrategy.cpp
)

add_library(core STATIC ${SOURCES})
target_link_libraries(core PRIVATE includes)

add_executable(simulation src/main.cpp)
target_link_libraries(simulation PRIVATE core includes)

add_subdirectory(benchmarks)
