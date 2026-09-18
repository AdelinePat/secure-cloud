#!/bin/bash

set -e  # Exit on error

# ==================== COLORS ====================
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
RESET='\033[0m'

title() {
  echo -e "${BLUE}===============================================${RESET}"
  echo -e "${BLUE}  $1${RESET}"
  echo -e "${BLUE}===============================================${RESET}"
  echo -e ""
}

success() {
  echo -e "${GREEN}$1${RESET}"
}

failure() {
  echo -e "${RED}$1${RESET}"
}

title "  Secure Cloud Client Build Script (Linux)"

# echo "========================================"
# echo "  Secure Cloud Client Build Script (Linux)"
# echo "========================================"


# Navigate to script's directory (client/)
cd "$(dirname "$0")"

# Check for required tools
echo "Checking dependencies..."
echo ""

if ! command -v cmake &> /dev/null; then
    failure "[ERROR] CMake not found!"
    echo ""
    echo "Install with:"
    echo "  sudo apt install cmake"
    exit 1
fi

if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    failure "[ERROR] Qt6 not found!"
    echo ""
    echo "Install with:"
    echo "  sudo apt install qt6-base-dev qt6-tools-dev"
    exit 1
fi

success "[OK] CMake found: $(cmake --version | head -n1)"

if command -v qmake6 &> /dev/null; then
    success "[OK] Qt6 found: $(qmake6 --version | grep 'Qt version')"
elif command -v qmake &> /dev/null; then
    success "[OK] Qt found: $(qmake --version | grep 'Qt version')"
fi

# echo "Checking OpenSSL development libraries..."

# if ! pkg-config --exists openssl 2>/dev/null; then
#     failure "[ERROR] OpenSSL development libraries not found!"
#     echo ""
#     echo "Install with:"
    
#     # Detect distro and suggest correct command
#     if [ -f /etc/debian_version ]; then
#         echo "  sudo apt install libssl-dev"
#     elif [ -f /etc/arch-release ]; then
#         echo "  sudo pacman -S openssl"
#     elif [ -f /etc/fedora-release ]; then
#         echo "  sudo dnf install openssl-devel"
#     else
#         echo "  (Please install OpenSSL development libraries for your distro)"
#     fi
#     exit 1
# fi

# OPENSSL_VERSION=$(pkg-config --modversion openssl)
# success "[OK] OpenSSL found: $OPENSSL_VERSION"

echo ""


# Verify CMakeLists.txt exists
if [ ! -f "CMakeLists.txt" ]; then
    failure "[ERROR] CMakeLists.txt not found!"
    echo "This script must be in the client/ directory."
    exit 1
fi

echo "[INFO] Building from: $(pwd)"
echo ""

# Clean build directory if it exists
if [ -d "build" ]; then
    echo "[INFO] Cleaning old build directory..."
    rm -rf build
fi

# Create fresh build directory
mkdir build
cd build

title "  Step 1: Configuring with CMake"

# Configure with CMake
# Let CMake auto-detect everything on Linux
cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo ""
    failure "[ERROR] CMake configuration failed!"
    echo ""
    echo "Troubleshooting:"
    echo "1. Make sure Qt6 is installed: sudo apt install qt6-base-dev"
    echo "2. Check that all dependencies are available"
    echo "3. Try: rm -rf build && ./linuxbuild.sh"
    exit 1
fi

echo ""
success "[OK] Configuration successful"
echo ""

title "  Step 2: Building Client"

# Build with all CPU cores
cmake --build . --config Release -j$(nproc)

if [ $? -ne 0 ]; then
    echo ""
    failure "[FAILED] Build failed!"
    echo "Check the errors above"
    exit 1
fi


echo ""
title "  Build Successful!"

# Find the executable
if [ -f "client" ]; then
    echo "Executable: $(pwd)/client"
    echo ""
    echo "To run:"
    echo "  cd build"
    echo "  ./client"
elif [ -f "Release/client" ]; then
    echo "Executable: $(pwd)/Release/client"
    echo ""
    echo "To run:"
    echo "  cd build/Release"
    echo "  ./client"
else
    echo "Executable built - check build/ directory"
    ls -lh client* 2>/dev/null || true
fi

echo ""
echo "Or run with: ./run.sh"
echo ""