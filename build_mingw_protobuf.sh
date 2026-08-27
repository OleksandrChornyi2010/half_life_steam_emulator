#!/bin/bash

# Exit immediately if any command fails
set -e

# Define installation paths
LINUX_PREFIX="/opt/protobuf-32"
MINGW_PREFIX="/opt/protobuf-mingw32"

# Ensure the script is run with sudo
if [ "$EUID" -ne 0 ]; then
  echo "Error: Please run this script with sudo."
  exit 1
fi

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

# ==============================================================================
# BUILD 1: Linux 32-bit (Target: /opt/protobuf-32) - Includes protoc
# ==============================================================================
echo "[*] Step 5: Preparing Linux 32-bit build (for protoc)..."
mkdir -p build-linux
cd build-linux

echo "[*] Step 6: Configuring Linux 32-bit project with CMake..."
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$LINUX_PREFIX" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_FLAGS="-m32" \
  -DCMAKE_CXX_FLAGS="-m32" \
  -DCMAKE_EXE_LINKER_FLAGS="-m32" \
  -DCMAKE_SHARED_LINKER_FLAGS="-m32" \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_SHARED_LIBS=OFF

echo "[*] Step 7: Compiling Linux 32-bit build..."
make -j$(nproc)

echo "[*] Step 8: Installing Linux 32-bit Protobuf (including protoc)..."
make install

# Go back to the repository root
cd /tmp/protobuf

# ==============================================================================
# BUILD 2: Windows MinGW 32-bit (Target: /opt/protobuf-mingw32) - excludes protoc
# ==============================================================================
echo "[*] Step 9: Preparing MinGW 32-bit build (for Windows libraries)..."
mkdir -p build-mingw
cd build-mingw

echo "[*] Step 10: Configuring MinGW 32-bit project with CMake..."
cmake .. \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$MINGW_PREFIX" \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_SHARED_LIBS=OFF \
  -Dprotobuf_BUILD_PROTOC_BINARIES=OFF

echo "[*] Step 11: Compiling MinGW 32-bit build..."
make -j$(nproc)

echo "[*] Step 12: Installing MinGW 32-bit Protobuf libraries..."
make install

echo "[*] Done! Protobuf installed successfully:"
echo "    -> Linux protoc/libs: $LINUX_PREFIX"
echo "    -> MinGW libs:        $MINGW_PREFIX"