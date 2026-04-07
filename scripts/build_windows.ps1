# scripts\build_windows.ps1
# Run in an elevated PowerShell prompt when installing system packages
Set-StrictMode -Version Latest

Write-Host "=== Photon: Windows build helper ==="

# 1) Optional: install system tools with winget (uncomment/adjust IDs as needed)
# winget search Kitware.CMake
# winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
# winget install --id Ninja -e --accept-package-agreements --accept-source-agreements
# winget install --id LunarG.VulkanSDK -e --accept-package-agreements --accept-source-agreements

# 2) Bootstrap vcpkg (if missing)
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
$vcpkgRoot = Join-Path $repoRoot "vcpkg"
if (-not (Test-Path $vcpkgRoot)) {
    Write-Host "Cloning vcpkg..."
    git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
    & "$vcpkgRoot\bootstrap-vcpkg.bat"
}

# 3) Install libraries via vcpkg (x64)
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
& $vcpkgExe install glfw3:x64-windows glm:x64-windows --recurse

# 4) Configure & build with CMake + Ninja
$buildDir = Join-Path $repoRoot "build"
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
New-Item -ItemType Directory -Path $buildDir | Out-Null
Push-Location $buildDir
$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
# Enable Vulkan if SDK present
$useVulkan = Test-Path "$env:VULKAN_SDK"
$vkFlag = $useVulkan ? "-DUSE_VULKAN=ON" : "-DUSE_VULKAN=OFF"
cmake .. -G "Ninja" -DCMAKE_TOOLCHAIN_FILE="$toolchain" $vkFlag
ninja

# 5) Run headless smoke test
$exe = Join-Path $buildDir "photon.exe"
if (Test-Path $exe) {
    Write-Host "Running headless smoke test..."
    & $exe
} else {
    Write-Host "Build output not found: $exe"
}
Pop-Location
