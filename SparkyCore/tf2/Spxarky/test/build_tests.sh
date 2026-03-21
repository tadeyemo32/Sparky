#!/bin/bash
# build_tests.sh - Compile and run the Spxarky test suite
# Usage: ./build_tests.sh
# Requirements: cmake and a C++20 compiler

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "  Building Spxarky Test Suite (GTest)..."
echo "========================================"
echo ""

# Create build directory
mkdir -p build
cd build

# Generate and build
cmake ..
cmake --build . -j$(sysctl -n hw.ncpu)

echo "Build successful!"
echo ""

# Run the tests
./run_tests
EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo "All tests passed!"
else
    echo "Some tests failed! (exit code: $EXIT_CODE)"
fi

exit $EXIT_CODE
