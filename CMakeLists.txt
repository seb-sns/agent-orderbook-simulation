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

# Same sources compiled with ENABLE_VIZ: the recorder taps become real code
# and events are logged to simulation.mktviz for viz/viewer.html to replay.
# The plain 'simulation' target above is unaffected (taps compile to nothing).
add_library(core_viz STATIC ${SOURCES} src/VizRecorder.cpp)
target_compile_definitions(core_viz PRIVATE ENABLE_VIZ)
target_link_libraries(core_viz PRIVATE includes)

add_executable(simulation_viz src/main.cpp)
target_compile_definitions(simulation_viz PRIVATE ENABLE_VIZ)
target_link_libraries(simulation_viz PRIVATE core_viz includes)

add_subdirectory(benchmarks)
