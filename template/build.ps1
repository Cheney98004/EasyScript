param (
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",
    [string]$Generator = "",
    [switch]$RunTest
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "==> Configuring and building EasyScript native module ($BuildType)..." -ForegroundColor Cyan

# 1. 決定 CMake 生成器 (若有 GCC 且未手動指定，預設採用 MinGW Makefiles)
$cmakeArgs = @("-B", "build", "-DCMAKE_BUILD_TYPE=$BuildType")
if ($Generator) {
    $cmakeArgs += @("-G", $Generator)
} elseif (-not (Test-Path "build/CMakeCache.txt")) {
    $gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($null -ne $gccCmd) {
        $cmakeArgs += @("-G", "MinGW Makefiles")
    }
}

# 2. 執行 CMake 配置
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERR] CMake configuration failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

# 3. 執行編譯
& cmake --build build --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERR] Build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[OK] Build completed successfully." -ForegroundColor Green

# 4. 測試執行
if ($RunTest) {
    Write-Host "`n==> Running module verification test..." -ForegroundColor Cyan
    $easyCmd = Get-Command easyscript -ErrorAction SilentlyContinue
    $localEasy1 = Join-Path $ScriptDir "build/easyscript.exe"
    $localEasy2 = Join-Path $ScriptDir "build/$BuildType/easyscript.exe"
    $parentEasy = Join-Path (Split-Path $ScriptDir -Parent) "build/easyscript.exe"

    if (Test-Path $localEasy1) {
        & $localEasy1 test.easy
    } elseif (Test-Path $localEasy2) {
        & $localEasy2 test.easy
    } elseif ($null -ne $easyCmd) {
        & easyscript test.easy
    } elseif (Test-Path $parentEasy) {
        & $parentEasy test.easy
    } else {
        Write-Host "[WARN] 'easyscript' executable not found. Skipping test execution." -ForegroundColor Yellow
    }
}
