<#
skai t15 (docs/re/SKAI_T15_PLAN_RETEST.md section 3.5): a Q2 land skirmish trace with the read-only Frida hooks of
frida\skai_t15_hooks.js attached to StarWarsG.exe, so each Destroy_Structure plan end (PEXIT record of the Lua probe)
can be matched with the goal-system rule that ended it (running-plan re-test 0x6c47c0 false, Resolve_Category old-candidate
release, maintenance, or Purge_Goals).

  run_skai_t15_hooks.ps1 -Faction empire|rebel [run_trace.ps1 arguments]

Starts frida\frida_logger.py in the background (it waits for the game, attaches, logs), runs run_trace.ps1 (-Scenario
skirmish -NoDrive -LuaProbe -Ticks 36000 -TimeoutMin 30, menu clicks by hand as the .cmd says), then stops the logger and
moves its log into the run folder as frida_t15.jsonl. Read the result with analysis\plan_ends.py out\<run>.
#>
param(
    [ValidateSet('empire', 'rebel')][string]$Faction = 'empire',
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)
$ErrorActionPreference = 'Stop'
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $kit '..\..\..')
$py = Join-Path $root 'toolchain\re\venv\Scripts\python.exe'
if (-not (Test-Path $py)) { Write-Host "missing $py (toolchain\14_re_tools.cmd)"; exit 20 }
$out = Join-Path $kit 'out'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$log = Join-Path $out "frida_t15_$stamp.jsonl"
$stop = Join-Path $out "frida_t15_$stamp.stop"
$before = @(Get-ChildItem -LiteralPath $out -Directory | ForEach-Object Name)

$logger = Start-Process -FilePath $py -PassThru -WindowStyle Hidden -ArgumentList @('-I',
    ('"' + (Join-Path $kit 'frida\frida_logger.py') + '"'), '--out', ('"' + $log + '"'), '--stop-file', ('"' + $stop + '"'),
    '--minutes', '45')
Write-Host "frida logger started (pid $($logger.Id)), log $log"

if ($Faction -eq 'empire') {
    $label = 'skai t15 hooks: Utapau land, Underworld idle vs Empire Medium, advanced Default (by hand)'
} else {
    $label = 'skai t15 hooks: Utapau land, Empire idle vs Rebel Medium, advanced Default (by hand)'
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'run_trace.ps1') -Scenario skirmish -NoDrive -LuaProbe `
    -Ticks 36000 -TimeoutMin 30 -Label $label @Rest
$code = $LASTEXITCODE

New-Item -ItemType File -Force -Path $stop | Out-Null
if (-not $logger.WaitForExit(20000)) { Stop-Process -Id $logger.Id -Force -ErrorAction SilentlyContinue }
Remove-Item -LiteralPath $stop -Force -ErrorAction SilentlyContinue
$new = @(Get-ChildItem -LiteralPath $out -Directory | Where-Object { $before -notcontains $_.Name -and $_.Name -ne 'selftest' } |
         Sort-Object Name)
if ($new.Count -gt 0 -and (Test-Path $log)) {
    $dest = Join-Path $new[-1].FullName 'frida_t15.jsonl'
    Move-Item -LiteralPath $log -Destination $dest -Force
    Write-Host "frida log: $dest"
} else {
    Write-Host "frida log stays at $log (no new run folder)"
}
exit $code
