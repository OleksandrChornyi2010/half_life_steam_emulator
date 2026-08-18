# Stop execution on errors immediately
$ErrorActionPreference = "Stop"

$InstallPrefix = "C:\Libraries\protobuf-32"

Write-Host "[*] Step 1: Cleaning up previous build artifacts..."
$TempDir = "$env:TEMP\protobuf"
if (Test-Path $TempDir) {
    Remove-Item -Recurse -Force $TempDir
}

Write-Host "[*] Step 2: Cloning Protobuf repository (v21.12)..."
git clone --depth 1 --branch v21.12 https://github.com/protocolbuffers/protobuf.git $TempDir
Set-Location $TempDir

Write-Host "[*] Step 3: Updating git submodules..."
git submodule update --init --recursive

Write-Host "[*] Step 4: Patching source code to remove problematic tail-call attributes..."
# Using PowerShell string replacement instead of sed
$PortDefPath = "src\google\protobuf\port_def.inc"
$Content = Get-Content $PortDefPath -Raw
$Content = $Content -replace '\[\[gnu::musttail\]\]', ''
$Content = $Content -replace '\[\[clang::musttail\]\]', ''
$Content = $Content -replace '#define PROTOBUF_TAILCALL true', '#define PROTOBUF_TAILCALL false'
Set-Content -Path $PortDefPath -Value $Content

Write-Host "[*] Step 5: Creating and entering build directory..."
New-Item -ItemType Directory -Force -Path build | Out-Null
Set-Location build

Write-Host "[*] Step 6: Configuring project with CMake..."
# Using -A Win32 to enforce 32-bit architecture for MSVC instead of -m32 flags
cmake .. `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_INSTALL_PREFIX="$InstallPrefix" `
  -A Win32 `
  -Dprotobuf_BUILD_TESTS=OFF `
  -Dprotobuf_BUILD_SHARED_LIBS=OFF

Write-Host "[*] Step 7: Compiling using all available CPU cores..."
# Using CMake build wrapper instead of make
cmake --build . --config Release --parallel $env:NUMBER_OF_PROCESSORS

Write-Host "[*] Step 8: Installing the compiled binaries and libraries..."
# Using CMake install wrapper
cmake --install . --config Release

Write-Host "[*] Done! Protobuf is now installed in $InstallPrefix"