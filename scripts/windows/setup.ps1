$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
node scripts/lib/setup-env.js
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
New-Item -ItemType Directory -Force -Path (Join-Path $root 'artifacts') | Out-Null
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
  Write-Error 'npm not found; install Node.js 18+ and run: npm install'
  exit 1
}
npm install
exit $LASTEXITCODE
