function Invoke-MfdpUnix {
  param([string]$Name, [string[]]$ExtraArgs = @())
  $root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
  if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Write-Error 'The MFDP C++ core currently builds on Linux and macOS only. Install WSL (wsl --install) and run this command again, or use a Linux or macOS machine.'
    exit 1
  }
  & wsl.exe --cd $root bash "scripts/unix/$Name.sh" @ExtraArgs
  exit $LASTEXITCODE
}
