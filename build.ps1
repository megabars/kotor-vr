param(
    [string]$ToolchainRoot = $env:VCToolsInstallDir,
    [string]$WindowsSdkRoot = $env:WindowsSdkDir,
    [string]$WindowsSdkVersion = $env:WindowsSDKVersion,
    [ValidateSet('Release','RelWithDebInfo','Debug')][string]$Configuration = 'Release',
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [switch]$ConfigureOnly,
    [switch]$ProbeOnly
)
$ErrorActionPreference = 'Stop'
if (!$BuildDirectory) { $BuildDirectory = "$PSScriptRoot\build" }
if (!$OutputDirectory) { $OutputDirectory = "$PSScriptRoot\bin" }
if (!$ToolchainRoot) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools: Desktop development with C++.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'MSVC not found. Supply -ToolchainRoot pointing to VC\Tools\MSVC\<version>.' }
    $ToolchainRoot = (Get-ChildItem -LiteralPath "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
}
if (!$WindowsSdkRoot) {
    $WindowsSdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
    if (!$WindowsSdkRoot) { $WindowsSdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10" }
}
if (!$WindowsSdkVersion) {
    $WindowsSdkVersion = (Get-ChildItem -LiteralPath "$WindowsSdkRoot\Include" -Directory | Where-Object Name -Match '^10\.' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
$WindowsSdkVersion = $WindowsSdkVersion.TrimEnd('\','/')
$compilerBin = Join-Path $ToolchainRoot 'bin\Hostx64\x86'
$sdkBin = Join-Path $WindowsSdkRoot "bin\$WindowsSdkVersion\x64"
foreach ($required in @("$compilerBin\cl.exe", "$compilerBin\nmake.exe", "$sdkBin\rc.exe", "$WindowsSdkRoot\Lib\$WindowsSdkVersion\um\x86\kernel32.lib")) {
    if (!(Test-Path -LiteralPath $required)) { throw "Required tool or library missing: $required" }
}
$saved = @{}
foreach ($key in @('PATH','INCLUDE','LIB','VSLANG')) { $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
try {
    $env:PATH = "$compilerBin;$sdkBin;$env:PATH"
    $env:INCLUDE = "$ToolchainRoot\include;$WindowsSdkRoot\Include\$WindowsSdkVersion\ucrt;$WindowsSdkRoot\Include\$WindowsSdkVersion\shared;$WindowsSdkRoot\Include\$WindowsSdkVersion\um;$WindowsSdkRoot\Include\$WindowsSdkVersion\winrt"
    $env:LIB = "$ToolchainRoot\lib\x86;$WindowsSdkRoot\Lib\$WindowsSdkVersion\ucrt\x86;$WindowsSdkRoot\Lib\$WindowsSdkVersion\um\x86"
    $env:VSLANG = '1033'
    $buildGame = if ($ProbeOnly) { 'OFF' } else { 'ON' }
    & cmake -S $PSScriptRoot -B $BuildDirectory -G 'NMake Makefiles' "-DCMAKE_BUILD_TYPE=$Configuration" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_RC_COMPILER=rc "-DKVR_BUILD_GAME=$buildGame" "-DKVR_OUTPUT_DIRECTORY=$OutputDirectory"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    if (!$ConfigureOnly) {
        & cmake --build $BuildDirectory --parallel
        if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
        & ctest --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Math/cursor tests failed.' }
    }
} finally {
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
