#!/bin/bash


echo "========================================"
echo "  Running Secure Cloud Client (Linux)"
echo "========================================"
echo ""

# Navigate to script's directory
cd "$(dirname "$0")"

# Clear Snap environment variables that can interfere with Qt
unset GTK_PATH
unset LD_LIBRARY_PATH

# Find and run the executable
if [ -f "build/client" ]; then
    echo "[OK] Starting client..."
    echo ""
    cd build
    ./client
elif [ -f "build/Release/client" ]; then
    echo "[OK] Starting client (Release)..."
    echo ""
    cd build/Release
    ./client
else
    echo "[ERROR] Client executable not found!"
    echo ""
    echo "Please build the client first using:"
    echo "  ./build.sh"
    echo ""
    exit 1
fi