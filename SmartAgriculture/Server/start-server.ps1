param([string]$Java = '')

$ErrorActionPreference = 'Stop'
$serverRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$jar = Join-Path $serverRoot 'target\smart-agriculture-server-1.0.0.jar'

if (-not $Java) {
    $found = Get-Command java -ErrorAction SilentlyContinue
    if ($found) { $Java = $found.Source }
}
if (-not $Java -or -not (Test-Path -LiteralPath $Java)) {
    throw 'Java not found. Install JDK 11+ or run: .\start-server.ps1 -Java C:\path\to\java.exe'
}
if (-not (Test-Path -LiteralPath $jar)) {
    throw "Server jar not found. Run Maven package first: $jar"
}

Set-Location -LiteralPath $serverRoot
& $Java -jar $jar
