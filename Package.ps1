# Packages bin\ into releases\LibertyBounce-<version>.zip. Run Build.ps1 and
# build the importer (importer\BUILD.md) first.
param([string]$Version = '0.2.0')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$stage = Join-Path $root "build\package"
$zip = Join-Path $root "releases\LibertyBounce-$Version.zip"
foreach ($file in 'bin\LibertyBounce.asi', 'bin\LibertyBounceMusic.exe') {
    if (-not (Test-Path (Join-Path $root $file))) { throw "$file is missing; build it first" }
}
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\scripts", "$stage\source\importer", (Split-Path $zip) | Out-Null
Copy-Item "$root\bin\LibertyBounce.asi", "$root\bin\LibertyBounceMusic.exe", "$root\LibertyBounce.ini" "$stage\scripts"
Copy-Item "$root\README.md", "$root\LICENSE", "$root\LICENSE-ViceBounce.txt", "$root\Plugin-SDK-LICENSE.txt" $stage
Copy-Item "$root\ThirdParty" $stage -Recurse
Copy-Item "$root\importer\*.py", "$root\importer\BUILD.md", "$root\importer\requirements-build.txt" "$stage\source\importer"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Output "Packaged $zip"
