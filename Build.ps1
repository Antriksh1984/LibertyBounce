# Builds bin\LibertyBounce.asi. Needs VS 2022 Build Tools (C++) and plugin-sdk
# with Plugin_III.lib built (see README.md).
$ErrorActionPreference = 'Stop'
$sdk = if ($env:PLUGIN_SDK_DIR) { $env:PLUGIN_SDK_DIR } else { Join-Path $PSScriptRoot '..\_deps\plugin-sdk' | Resolve-Path }
$env:PLUGIN_SDK_DIR = $sdk
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found' }
Push-Location $PSScriptRoot
try {
    & (Join-Path $sdk 'tools\premake\premake5.exe') vs2022
    if ($LASTEXITCODE -ne 0) { throw 'Project generation failed' }
    & (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') 'build\LibertyBounce.vcxproj' /p:Configuration=Release /p:Platform=Win32 /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'ASI build failed' }
} finally { Pop-Location }
