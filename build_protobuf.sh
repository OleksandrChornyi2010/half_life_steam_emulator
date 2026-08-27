#!/bin/bash

# Exit immediately if any command fails
set -e

INSTALL_PREFIX="/opt/protobuf-32"

echo "[*] Step 1: Cleaning up previous build artifacts..."
rm -rf /tmp/protobuf

echo "[*] Step 2: Cloning Protobuf repository (v21.12)..."
git clone --depth 1 --branch v21.12 https://github.com/protocolbuffers/protobuf.git /tmp/protobuf
cd /tmp/protobuf

echo "[*] Step 3: Updating git submodules..."
git submodule update --init --recursive

echo "[*] Step 4: Patching source code to remove problematic tail-call attributes..."
# Physically remove the musttail attributes that break 32-bit GCC compilation
sed -i 's/\[\[gnu::musttail\]\]//g' src/google/protobuf/port_def.inc
sed -i 's/\[\[clang::musttail\]\]//g' src/google/protobuf/port_def.inc
# Also disable the internal macro flag just to be absolutely safe
sed -i 's/#define PROTOBUF_TAILCALL true/#define PROTOBUF_TAILCALL false/g' src/google/protobuf/port_def.inc

echo "[*] Step 5: Creating and entering build directory..."
mkdir -p build
cd build

echo "[*] Step 6: Configuring project with CMake..."
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_FLAGS="-m32" \
  -DCMAKE_CXX_FLAGS="-m32" \
  -DCMAKE_EXE_LINKER_FLAGS="-m32" \
  -DCMAKE_SHARED_LINKER_FLAGS="-m32" \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_SHARED_LIBS=OFF

echo "[*] Step 7: Compiling using all available CPU cores..."
make -j$(nproc)

echo "[*] Step 8: Installing the compiled binaries and libraries..."
sudo make install

echo "[*] Done! Protobuf is now installed in $INSTALL_PREFIX"