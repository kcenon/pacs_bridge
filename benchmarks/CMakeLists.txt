# =============================================================================
# benchmarks/CMakeLists.txt
# Performance benchmark configuration for PACS Bridge
# =============================================================================
#
# Benchmarks are disabled by default. Enable with:
#   cmake -B build -DBRIDGE_BUILD_BENCHMARKS=ON
#
# Or via build.sh:
#   ./build.sh --benchmarks
#
# =============================================================================

# -----------------------------------------------------------------------------
# Helper macro: add_benchmark(<name> <source>)
# Creates an executable target linked to pacs_bridge and compile options.
# -----------------------------------------------------------------------------
macro(add_benchmark BENCHMARK_NAME BENCHMARK_SOURCE)
    add_executable(${BENCHMARK_NAME} ${BENCHMARK_SOURCE})

    target_link_libraries(${BENCHMARK_NAME}
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    target_include_directories(${BENCHMARK_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )
endmacro()

# -----------------------------------------------------------------------------
# Benchmark Targets
# -----------------------------------------------------------------------------

# Adapter performance benchmarks
# Measures throughput, latency, and scalability of each adapter implementation
add_benchmark(adapter_benchmark adapter_benchmark.cpp)

# Baseline comparison benchmarks
# Compares adapter overhead against direct implementation
add_benchmark(baseline_benchmark baseline_benchmark.cpp)

message(STATUS "Benchmarks: adapter_benchmark, baseline_benchmark")
