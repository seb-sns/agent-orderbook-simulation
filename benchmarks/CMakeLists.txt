find_package(benchmark REQUIRED)

add_executable(benchmark_simulation benchmark_Simulation.cpp)
target_link_libraries(benchmark_simulation
  PRIVATE
    core
    includes
    benchmark::benchmark
    pthread
)

add_executable(benchmark_orderlatency benchmark_OrderLatency.cpp)
target_link_libraries(benchmark_orderlatency
  PRIVATE
    core
    includes
)

add_executable(benchmark_agentlatency benchmark_AgentLatency.cpp)
target_link_libraries(benchmark_agentlatency
  PRIVATE
    core
    includes
)
