param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
if (!$OutputDirectory) { $OutputDirectory = "$PSScriptRoot\dist" }
$version = (Get-Content -LiteralPath "$PSScriptRoot\VERSION" -Raw).Trim()
$stage = Join-Path $OutputDirectory "stage-$version"
if (Test-Path -LiteralPath $stage) { throw "Staging directory already exists: $stage. Use a fresh -OutputDirectory." }
$package = Join-Path $stage 'kotor-vr'
New-Item -ItemType Directory -Path "$package\bin" -Force | Out-Null
$binaries = @('kotor_vr.dll','kotor-vr-launcher.exe','kotor-vr-probe.exe','kotor-vr-math-test.exe')
foreach ($name in $binaries) {
    $source = "$PSScriptRoot\bin\$name"
    if ((Get-Item -LiteralPath $source).VersionInfo.FileVersion -ne $version) { throw "Wrong version or missing resource: $name" }
    Copy-Item -LiteralPath $source -Destination "$package\bin\$name"
}
foreach ($name in @('README.md','README.ru.md','VERSION','LICENSE','THIRD_PARTY_NOTICES.md','install.ps1','restore-settings.ps1','docs','scripts','licenses')) {
    Copy-Item -LiteralPath "$PSScriptRoot\$name" -Destination $package -Recurse
}
$binarySums = foreach ($name in $binaries) { '{0}  bin/{1}' -f (Get-FileHash -LiteralPath "$package\bin\$name" -Algorithm SHA256).Hash.ToLowerInvariant(), $name }
[IO.File]::WriteAllLines("$package\SHA256SUMS.txt", $binarySums, [Text.UTF8Encoding]::new($false))
$zip = Join-Path $OutputDirectory "kotor-vr-$version-windows-x86.zip"
if (Test-Path -LiteralPath $zip) { throw "Archive exists: $zip" }
Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
$sum = '{0}  {1}' -f (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $zip -Leaf)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'SHA256SUMS.txt'), "$sum`n", [Text.UTF8Encoding]::new($false))
Write-Host "Created $zip"
