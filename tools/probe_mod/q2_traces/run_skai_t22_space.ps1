<#
skai t22 (docs/re/SKAI_T22_GEONOSIS_SCOUT.md, "What is left"): a Q2 space skirmish trace of Geonosis WITHOUT the Lua probe
(no plan-script hooks: the 2026-11-16 hooked runs killed the space plan threads), with the read-only Frida hooks of
frida\skai_t22_space_hooks.js attached to StarWarsG.exe. They log every Resolve_Category of the Rebel AI's space goal
system (active goals and proposals with type, target cell, desire, effective desire; survivors and accepts), the
activation records, plan ends and goal releases, so the Rebel AI's scout / pad / build goal timeline can be compared with
the port without touching the Lua threads.

  run_skai_t22_space.ps1 [run_trace.ps1 arguments, e.g. -NonInteractive]

Starts frida\frida_logger.py in the background (it waits for the game, attaches, logs), runs run_trace.ps1 (-Scenario
skirmish, 5400 ticks, no -LuaProbe), then stops the logger and moves its log into the run folder as frida_t22.jsonl.
Read it with analysis\space_goals.py out\<run>.
#>
param(
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
$log = Join-Path $out "frida_t22_$stamp.jsonl"
$stop = Join-Path $out "frida_t22_$stamp.stop"
$before = @(Get-ChildItem -LiteralPath $out -Directory | ForEach-Object Name)

$logger = Start-Process -FilePath $py -PassThru -WindowStyle Hidden -ArgumentList @('-I',
    ('"' + (Join-Path $kit 'frida\frida_logger.py') + '"'), '--out', ('"' + $log + '"'),
    '--script', ('"' + (Join-Path $kit 'frida\skai_t22_space_hooks.js') + '"'), '--stop-file', ('"' + $stop + '"'),
    '--minutes', '20')
Write-Host "frida logger started (pid $($logger.Id)), log $log"

$label = 'skai t22 space hooks: Geonosis space, Empire idle vs Rebel Medium, advanced Default, no Lua probe'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'run_trace.ps1') -Scenario skirmish -Ticks 5400 `
    -Label $label @Rest
$code = $LASTEXITCODE

New-Item -ItemType File -Force -Path $stop | Out-Null
if (-not $logger.WaitForExit(20000)) { Stop-Process -Id $logger.Id -Force -ErrorAction SilentlyContinue }
Remove-Item -LiteralPath $stop -Force -ErrorAction SilentlyContinue
$new = @(Get-ChildItem -LiteralPath $out -Directory | Where-Object { $before -notcontains $_.Name -and $_.Name -ne 'selftest' } |
         Sort-Object Name)
if ($new.Count -gt 0 -and (Test-Path $log)) {
    $dest = Join-Path $new[-1].FullName 'frida_t22.jsonl'
    Move-Item -LiteralPath $log -Destination $dest -Force
    Write-Host "frida log: $dest"
} else {
    Write-Host "frida log stays at $log (no new run folder)"
}
exit $code
