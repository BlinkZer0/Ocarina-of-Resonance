param([int]$Port = 8080)

$ErrorActionPreference = 'Stop'
& node (Join-Path $PSScriptRoot 'web/server.mjs') $Port
exit $LASTEXITCODE
