param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64', 'x86', 'All')]
    [string]$Platform = 'All'
)

# Builds WinSap with MSBuild from Visual Studio. -Platform All (the default)
# builds both bin\x64\<Configuration>\winsap.exe and bin\x86\<Configuration>\winsap32.exe.
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) { throw 'Visual Studio with the C++ desktop workload (MSVC x86/x64) is required.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw "MSBuild was not found: $msbuild" }
$solution = Join-Path $PSScriptRoot 'src\winsap.sln'
$platforms = if ($Platform -eq 'All') { @('x64', 'x86') } else { @($Platform) }

foreach ($target in $platforms) {
    & $msbuild $solution /m /nologo /v:minimal "/p:Configuration=$Configuration" "/p:Platform=$target"
    if ($LASTEXITCODE -ne 0) { throw "WinSap $Configuration $target build failed." }
    $name = if ($target -eq 'x64') { 'winsap.exe' } else { 'winsap32.exe' }
    $executable = Join-Path $PSScriptRoot "bin\$target\$Configuration\$name"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "The build did not produce $executable" }
    Write-Output "Executable: $executable"
}
