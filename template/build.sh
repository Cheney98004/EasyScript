#!/usr/bin/env bash
# ==============================================================================
# EasyScript C++ Native Extension Build & Test Script (Bash)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Release}"

echo -e "\033[36m==> Configuring and building EasyScript native module (${BUILD_TYPE})...\033[0m"

cmake -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build --config "${BUILD_TYPE}"

echo -e "\033[32m[OK] Build completed successfully.\033[0m"

if [[ "$*" == *"--test"* ]] || [[ "$*" == *"-t"* ]]; then
    echo -e "\033[36m\n==> Running module verification test...\033[0m"
    if [[ -f "build/easyscript" ]]; then
        ./build/easyscript test.easy
    elif command -v easyscript &> /dev/null; then
        easyscript test.easy
    elif [[ -f "../build/easyscript" ]]; then
        ../build/easyscript test.easy
    else
        echo -e "\033[33m[WARN] 'easyscript' binary not found. Skipping test.\033[0m"
    fi
fi
