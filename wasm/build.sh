#!/bin/bash

# ============================================================================
# OpticTrigeminal WASM Build Script
# Phase 1: Setup Emscripten and compile WASM module
# ============================================================================

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build"
DIST_DIR="$PROJECT_ROOT/dist/wasm"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ============================================================================
# Step 1: Check Emscripten Installation
# ============================================================================

echo -e "${GREEN}[1/4] Checking Emscripten SDK...${NC}"

if ! command -v emcc &> /dev/null; then
    echo -e "${YELLOW}Emscripten SDK not found in path. Checking for local installation...${NC}"
    
    if [ ! -d "$PROJECT_ROOT/emsdk" ]; then
        echo -e "${YELLOW}Cloning Emscripten SDK...${NC}"
        git clone https://github.com/emscripten-core/emsdk.git "$PROJECT_ROOT/emsdk"
        cd "$PROJECT_ROOT/emsdk"
        ./emsdk install latest
        ./emsdk activate latest
        cd "$SCRIPT_DIR"
    fi
    
    echo -e "${YELLOW}Sourcing emsdk_env.sh...${NC}"
    source "$PROJECT_ROOT/emsdk/emsdk_env.sh"
fi

if ! command -v emcc &> /dev/null; then
    echo -e "${RED}ERROR: Failed to setup Emscripten SDK${NC}"
    exit 1
fi

EMCC_VERSION=$(emcc --version | head -1)
echo -e "${GREEN}  ✓ Emscripten found: $EMCC_VERSION${NC}"

# ============================================================================
# Step 2: Create Build Directory
# ============================================================================

echo -e "${GREEN}[2/4] Setting up build directory...${NC}"

if [ -d "$BUILD_DIR" ]; then
    echo "  Removing old build..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$DIST_DIR"
echo -e "${GREEN}  ✓ Build directory ready: $BUILD_DIR${NC}"

# ============================================================================
# Step 3: Generate Build Configuration with Emscripten
# ============================================================================

echo -e "${GREEN}[3/4] Generating build configuration...${NC}"

cd "$BUILD_DIR"

# Use emcmake to wrap CMake with Emscripten configuration
emcmake cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    "$SCRIPT_DIR"

if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: CMake configuration failed${NC}"
    exit 1
fi

echo -e "${GREEN}  ✓ Build configured${NC}"

# ============================================================================
# Step 4: Compile WASM Module
# ============================================================================

echo -e "${GREEN}[4/4] Compiling WASM module...${NC}"

# Build with Emscripten
emmake make -j$(nproc) VERBOSE=1

if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: WASM compilation failed${NC}"
    exit 1
fi

# Copy output to dist directory
cp optic-trigeminal.wasm "$DIST_DIR/"
if [ -f optic-trigeminal.js ]; then
    cp optic-trigeminal.js "$DIST_DIR/"
fi

echo -e "${GREEN}  ✓ WASM module compiled${NC}"

# ============================================================================
# Verification & Output
# ============================================================================

echo ""
echo -e "${GREEN}=== WASM Build Complete ===${NC}"
echo ""
echo "Output files:"
echo "  - $DIST_DIR/optic-trigeminal.wasm (${RED}$(du -h "$DIST_DIR/optic-trigeminal.wasm" | cut -f1)${NC})"
if [ -f optic-trigeminal.js ]; then
    echo "  - $DIST_DIR/optic-trigeminal.js"
fi
echo ""
echo "Next steps:"
echo "  1. Run tests: cd $PROJECT_ROOT/tests && npm test"
echo "  2. Build frontend: cd $PROJECT_ROOT/web && npm run build"
echo "  3. Start server: cd $PROJECT_ROOT && ./build/optic-trigeminal"
echo ""

# ============================================================================
# Sanity Checks
# ============================================================================

echo -e "${YELLOW}Sanity Checks:${NC}"

# Check file size
WASM_SIZE=$(stat -f%z "$DIST_DIR/optic-trigeminal.wasm" 2>/dev/null || stat -c%s "$DIST_DIR/optic-trigeminal.wasm")
MAX_SIZE=$((5 * 1024 * 1024))  # 5MB max for clinical system

if [ "$WASM_SIZE" -gt "$MAX_SIZE" ]; then
    echo -e "${RED}  ⚠ WASM module is ${WASM_SIZE} bytes (> 5MB limit)${NC}"
    echo "    Consider enabling more optimization flags in CMakeLists.txt"
else
    echo -e "${GREEN}  ✓ WASM size OK: $(($WASM_SIZE / 1024))KB${NC}"
fi

# Check for exported symbols
echo -e "${YELLOW}Exported WASM functions:${NC}"
if command -v wasm-nm &> /dev/null; then
    wasm-nm "$DIST_DIR/optic-trigeminal.wasm" | grep -E "wasm_" || echo "  (Use wasm-nm to inspect)"
else
    echo "  (Install wasm-binutils to verify exports: brew install wabt)"
fi

echo ""
echo -e "${GREEN}Build succeeded! ✓${NC}"
