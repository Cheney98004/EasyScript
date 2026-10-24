<#
.SYNOPSIS
    EasyScript & EPM Automated Installer for Windows

.DESCRIPTION
    Compiles and installs EasyScript language runtime, EPM package manager,
    standard library plugins, and LSP schemas. Configures user environment variables.

.PARAMETER InstallDir
    Target installation directory. Defaults to "$HOME\.easyscript".

.PARAMETER BuildType
    CMake build configuration (Release or Debug). Defaults to "Release".

.PARAMETER SkipBuild
    Skip compilation step and install directly from existing build directory.

.PARAMETER Force
    Overwrite existing installation without prompting.
#>

[CmdletBinding()]
param (
    [string]$InstallDir = "$HOME\.easyscript",
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",
    [switch]$SkipBuild,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Err {
    param([string]$Message)
    Write-Host "[ERR] $Message" -ForegroundColor Red
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host @"
=============================================================
   ______                 _____           _       _   
  |  ____|               / ____|         (_)     | |  
  | |__   __ _ ___ _   _| (___   ___ _ __ _ _ __ | |_ 
  |  __| / _` / __| | | |\___ \ / __| '__| | '_ \| __|
  | |___| (_| \__ \ |_| |____) | (__| |  | | |_) | |_ 
  |______\__,_|___/\__, |_____/ \___|_|  |_| .__/ \__|
                    __/ |                  | |        
                   |___/                   |_|        
             EasyScript Installer for Windows
=============================================================
"@ -ForegroundColor Cyan

# 1. 檢查先決條件 (Prerequisites)
Write-Step "Checking prerequisites..."

$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakeCmd) {
    Write-Err "CMake was not found in PATH. Please install CMake 3.20+ from https://cmake.org/download/"
    exit 1
}
Write-Success "Found CMake ($($cmakeCmd.Source))"

# 2. 編譯 EasyScript 與 EPM
$BuildDir = Join-Path $ScriptDir "build"

if (-not $SkipBuild) {
    Write-Step "Configuring and building EasyScript & EPM ($BuildType)..."
    
    # 執行 CMake 配置
    & cmake -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType
    if ($LASTEXITCODE -ne 0) {
        Write-Err "CMake configuration failed."
        exit $LASTEXITCODE
    }

    # 執行編譯 (核心、EPM 與全部標準庫插件)
    & cmake --build $BuildDir --config $BuildType --target easyscript_test
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Build failed."
        exit $LASTEXITCODE
    }
    Write-Success "Build completed successfully."
} else {
    Write-Warn "Skipping build as requested."
}

# 3. 安裝至目標目錄
Write-Step "Installing to '$InstallDir'..."

$resolvedInstallDir = [System.IO.Path]::GetFullPath($InstallDir)

if (Test-Path $resolvedInstallDir) {
    if (-not $Force) {
        Write-Warn "Target directory '$resolvedInstallDir' already exists."
    }
} else {
    New-Item -ItemType Directory -Force -Path $resolvedInstallDir | Out-Null
}

# 執行 CMake Install 規則
& cmake --install $BuildDir --config $BuildType --prefix $resolvedInstallDir
if ($LASTEXITCODE -ne 0) {
    Write-Err "CMake install failed."
    exit $LASTEXITCODE
}

Write-Success "Files copied to $resolvedInstallDir"

# 4. 設定環境變數 (User Environment Variables)
Write-Step "Configuring Environment Variables..."

$binDir = Join-Path $resolvedInstallDir "bin"
$pluginsDir = Join-Path $resolvedInstallDir "plugins"

# (1) 更新 PATH
$currentUserPath = [System.Environment]::GetEnvironmentVariable("Path", [System.EnvironmentVariableTarget]::User)
if ($null -eq $currentUserPath) { $currentUserPath = "" }

$pathEntries = $currentUserPath.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
if ($pathEntries -notcontains $binDir) {
    $newPath = if ($currentUserPath.Trim().Length -gt 0) { "$currentUserPath;$binDir" } else { $binDir }
    [System.Environment]::SetEnvironmentVariable("Path", $newPath, [System.EnvironmentVariableTarget]::User)
    Write-Success "Added '$binDir' to User PATH"
} else {
    Write-Success "'$binDir' is already in User PATH"
}

# 同步更新當前 Session PATH
if ($env:Path.Split(';') -notcontains $binDir) {
    $env:Path = "$binDir;$env:Path"
}

# (2) 設定 EASYSCRIPT_PATH
$currentEasyPath = [System.Environment]::GetEnvironmentVariable("EASYSCRIPT_PATH", [System.EnvironmentVariableTarget]::User)
if ($currentEasyPath -ne $pluginsDir) {
    [System.Environment]::SetEnvironmentVariable("EASYSCRIPT_PATH", $pluginsDir, [System.EnvironmentVariableTarget]::User)
    Write-Success "Set User EASYSCRIPT_PATH to '$pluginsDir'"
} else {
    Write-Success "User EASYSCRIPT_PATH is already configured"
}
$env:EASYSCRIPT_PATH = $pluginsDir

# 5. 建立反安裝腳本 (uninstall.ps1)
$uninstallScript = Join-Path $resolvedInstallDir "uninstall.ps1"
$uninstallContent = @"
# EasyScript Uninstaller
Write-Host "Uninstalling EasyScript from '$resolvedInstallDir'..." -ForegroundColor Cyan

# 1. 移除 PATH
`$userPath = [System.Environment]::GetEnvironmentVariable("Path", [System.EnvironmentVariableTarget]::User)
if (`$userPath) {
    `$entries = `$userPath.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) | Where-Object { `$_ -ne '$binDir' }
    `$newPath = `$entries -join ';'
    [System.Environment]::SetEnvironmentVariable("Path", `$newPath, [System.EnvironmentVariableTarget]::User)
    Write-Host "[OK] Removed '$binDir' from User PATH" -ForegroundColor Green
}

# 2. 移除 EASYSCRIPT_PATH
[System.Environment]::SetEnvironmentVariable("EASYSCRIPT_PATH", `$null, [System.EnvironmentVariableTarget]::User)
Write-Host "[OK] Removed User EASYSCRIPT_PATH" -ForegroundColor Green

# 3. 刪除檔案 (需在新行程中或延遲刪除)
Write-Host "[OK] Uninstallation complete! You may now safely remove the folder: '$resolvedInstallDir'" -ForegroundColor Green
"@
Set-Content -Path $uninstallScript -Value $uninstallContent -Encoding UTF8
Write-Success "Generated uninstaller at: $uninstallScript"

# 6. 驗證安裝
Write-Step "Verifying installation..."

$easyExe = Join-Path $binDir "easyscript.exe"
$epmExe = Join-Path $binDir "epm.exe"

if ((Test-Path $easyExe) -and (Test-Path $epmExe)) {
    Write-Success "Verified easyscript.exe"
    Write-Success "Verified epm.exe"
} else {
    Write-Warn "One or more binaries could not be verified in $binDir"
}

Write-Host @"

=============================================================
             Installation Completed Successfully!
=============================================================
EasyScript runtime and EPM have been installed to:
  $resolvedInstallDir

Installed Components:
  - easyscript.exe   : EasyScript Runtime, Compiler, REPL & LSP
  - epm.exe          : EasyScript Package Manager
  - plugins/         : Standard Library (std_fs, std_io, std_math, std_os, std_path)
  - include/         : C++ Extension Headers (for native plugin development)

Getting Started:
  Restart your terminal or VS Code, then run:
    easyscript                 (Open interactive REPL)
    epm init -y                (Create a new project)
    epm --help                 (Show package manager options)

To uninstall:
  powershell -File "$uninstallScript"
=============================================================
"@ -ForegroundColor Green
