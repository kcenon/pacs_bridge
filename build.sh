#!/bin/bash

# Build script for pacs_bridge
# Usage: ./build.sh [options]
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project root (directory where this script lives)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# Configuration defaults
PROJECT_NAME="pacs_bridge"
BUILD_DIR="build"
BUILD_TYPE="Release"
BUILD_TESTS="ON"
BUILD_EXAMPLES="OFF"
BUILD_BENCHMARKS="OFF"
BUILD_HL7="ON"
BUILD_FHIR="OFF"
ENABLE_TLS="ON"
VERBOSE="OFF"
CLEAN_BUILD="OFF"
RUN_TESTS="OFF"
CONFIGURE_ONLY="OFF"
TEST_FILTER=""
PARALLEL_JOBS=""

# Detect platform
case "$(uname -s)" in
    Darwin*)  PLATFORM="macOS" ;;
    Linux*)   PLATFORM="Linux" ;;
    *)        PLATFORM="Unknown" ;;
esac

# Detect CPU count for parallel builds
if [ "${PLATFORM}" = "macOS" ]; then
    CPU_COUNT=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
elif [ "${PLATFORM}" = "Linux" ]; then
    CPU_COUNT=$(nproc 2>/dev/null || echo 4)
else
    CPU_COUNT=4
fi

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            shift
            ;;
        --no-tests)
            BUILD_TESTS="OFF"
            shift
            ;;
        --examples)
            BUILD_EXAMPLES="ON"
            shift
            ;;
        --benchmarks)
            BUILD_BENCHMARKS="ON"
            shift
            ;;
        --fhir)
            BUILD_FHIR="ON"
            shift
            ;;
        --no-tls)
            ENABLE_TLS="OFF"
            shift
            ;;
        --clean)
            CLEAN_BUILD="ON"
            shift
            ;;
        --test)
            RUN_TESTS="ON"
            shift
            ;;
        --test-filter)
            RUN_TESTS="ON"
            TEST_FILTER="$2"
            shift 2
            ;;
        --configure)
            CONFIGURE_ONLY="ON"
            shift
            ;;
        --verbose)
            VERBOSE="ON"
            shift
            ;;
        --jobs|-j)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Build options:"
            echo "  --debug              Build in Debug mode"
            echo "  --release            Build in Release mode (default)"
            echo "  --relwithdebinfo     Build in RelWithDebInfo mode"
            echo "  --clean              Remove build directory before building"
            echo "  --configure          Configure only (no build)"
            echo "  --verbose            Enable verbose build output"
            echo "  --jobs, -j N         Number of parallel build jobs (default: ${CPU_COUNT})"
            echo "  --build-dir DIR      Set build directory (default: build)"
            echo ""
            echo "Module options:"
            echo "  --no-tests           Don't build tests"
            echo "  --examples           Build examples"
            echo "  --benchmarks         Build benchmarks"
            echo "  --fhir               Enable FHIR module"
            echo "  --no-tls             Disable TLS support"
            echo ""
            echo "Test options:"
            echo "  --test               Build and run tests"
            echo "  --test-filter NAME   Build and run specific test binary"
            echo "                       (e.g., --test-filter mpps_handler_test)"
            echo ""
            echo "  --help, -h           Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                   # Release build"
            echo "  $0 --clean --test    # Clean rebuild + run tests"
            echo "  $0 --debug --test    # Debug build + run tests"
            echo "  $0 --test-filter mpps_handler_test"
            echo "  $0 --configure       # Configure only"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Set parallel jobs
if [ -z "${PARALLEL_JOBS}" ]; then
    PARALLEL_JOBS="${CPU_COUNT}"
fi

# Print configuration
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Building ${PROJECT_NAME}${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Configuration:${NC}"
echo "  Platform:        ${PLATFORM}"
echo "  Build Type:      ${BUILD_TYPE}"
echo "  Build Directory: ${BUILD_DIR}"
echo "  Parallel Jobs:   ${PARALLEL_JOBS}"
echo "  Build Tests:     ${BUILD_TESTS}"
echo "  Build Examples:  ${BUILD_EXAMPLES}"
echo "  Build HL7:       ${BUILD_HL7}"
echo "  Build FHIR:      ${BUILD_FHIR}"
echo "  Enable TLS:      ${ENABLE_TLS}"
if [ "${RUN_TESTS}" = "ON" ]; then
    if [ -n "${TEST_FILTER}" ]; then
        echo "  Run Tests:       ${TEST_FILTER}"
    else
        echo "  Run Tests:       ALL"
    fi
fi
echo -e "${BLUE}========================================${NC}"

# Check for required tools
echo -e "${YELLOW}Checking prerequisites...${NC}"

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: cmake is not installed${NC}"
    echo "  Install: brew install cmake (macOS) or apt install cmake (Linux)"
    exit 1
fi

# Clean build directory if requested
if [ "${CLEAN_BUILD}" = "ON" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "${BUILD_DIR}"
fi

# Detect generator: reuse existing cache if present, otherwise auto-detect
CACHED_GENERATOR=""
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    CACHED_GENERATOR=$(grep '^CMAKE_GENERATOR:' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | cut -d= -f2)
fi

if [ -n "${CACHED_GENERATOR}" ]; then
    GENERATOR="${CACHED_GENERATOR}"
    echo -e "${GREEN}Reusing cached generator: ${GENERATOR}${NC}"
elif command -v ninja &> /dev/null; then
    GENERATOR="Ninja"
else
    echo -e "${YELLOW}Warning: ninja not found, using Unix Makefiles${NC}"
    GENERATOR="Unix Makefiles"
fi

# Fix stale FetchContent subbuild caches with mismatched generators
if [ -d "${BUILD_DIR}/_deps" ]; then
    for subbuild_cache in "${BUILD_DIR}"/_deps/*/CMakeCache.txt; do
        [ -f "${subbuild_cache}" ] || continue
        SUB_GEN=$(grep '^CMAKE_GENERATOR:' "${subbuild_cache}" 2>/dev/null | cut -d= -f2)
        if [ -n "${SUB_GEN}" ] && [ "${SUB_GEN}" != "${GENERATOR}" ]; then
            SUB_DIR=$(dirname "${subbuild_cache}")
            echo -e "${YELLOW}Cleaning stale subbuild cache: ${SUB_DIR} (${SUB_GEN} != ${GENERATOR})${NC}"
            rm -rf "${SUB_DIR}"
        fi
    done
fi

# Configure
echo -e "${YELLOW}Configuring...${NC}"

CMAKE_ARGS=(
    -B "${BUILD_DIR}"
    "-G${GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF"
    "-DBRIDGE_BUILD_TESTS=${BUILD_TESTS}"
    "-DBRIDGE_BUILD_EXAMPLES=${BUILD_EXAMPLES}"
    "-DBRIDGE_BUILD_BENCHMARKS=${BUILD_BENCHMARKS}"
    "-DBRIDGE_BUILD_HL7=${BUILD_HL7}"
    "-DBRIDGE_BUILD_FHIR=${BUILD_FHIR}"
    "-DBRIDGE_ENABLE_TLS=${ENABLE_TLS}"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
)

if [ "${VERBOSE}" = "ON" ]; then
    CMAKE_ARGS+=("-DCMAKE_VERBOSE_MAKEFILE=ON")
fi

cmake "${CMAKE_ARGS[@]}"

# Link compile_commands.json for IDE support
if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" compile_commands.json
fi

if [ "${CONFIGURE_ONLY}" = "ON" ]; then
    echo -e "${GREEN}Configuration complete.${NC}"
    exit 0
fi

# Build
echo -e "${YELLOW}Building (${PARALLEL_JOBS} jobs)...${NC}"

BUILD_ARGS=(
    --build "${BUILD_DIR}"
    --config "${BUILD_TYPE}"
    -j "${PARALLEL_JOBS}"
)

if [ "${VERBOSE}" = "ON" ]; then
    BUILD_ARGS+=(--verbose)
fi

cmake "${BUILD_ARGS[@]}"

echo -e "${GREEN}Build succeeded.${NC}"

# Run tests
if [ "${RUN_TESTS}" = "ON" ]; then
    echo ""
    echo -e "${YELLOW}Running tests...${NC}"

    if [ -n "${TEST_FILTER}" ]; then
        TEST_BIN="${BUILD_DIR}/bin/${TEST_FILTER}"
        if [ -f "${TEST_BIN}" ]; then
            "${TEST_BIN}"
        else
            echo -e "${RED}Error: test binary not found: ${TEST_BIN}${NC}"
            echo "Available test binaries:"
            ls "${BUILD_DIR}/bin/"*_test 2>/dev/null || echo "  (none found)"
            exit 1
        fi
    else
        # Exclude: performance/memory benchmarks, load stress tests,
        # and phase2 integration tests (require external kcenon dependencies)
        if ! ctest --test-dir "${BUILD_DIR}" --output-on-failure --timeout 600 -LE "performance|memory|load|phase2"; then
            echo ""
            echo -e "${YELLOW}Some tests failed. Review the output above for details.${NC}"
            echo -e "${YELLOW}Note: Use --test-filter NAME to run a specific test binary.${NC}"
            exit 1
        fi
    fi

    echo -e "${GREEN}Tests passed.${NC}"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Done.${NC}"
echo -e "${GREEN}========================================${NC}"
