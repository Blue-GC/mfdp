param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
. "$PSScriptRoot\common.ps1"
Invoke-MfdpUnix -Name 'build' -ExtraArgs $Rest
