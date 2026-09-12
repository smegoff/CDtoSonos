param(
    [string]$Device = 'http://cdtosonos.local',
    [ValidateRange(1, 300)][int]$Duration = 30
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$python = Join-Path $project '.venv-integrity\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    Write-Host 'From the project directory, set up the PC-only test environment:'
    Write-Host '  python -m venv .venv-integrity'
    Write-Host '  .\.venv-integrity\Scripts\python.exe -m pip install -r scripts\requirements-integrity.txt'
    exit 2
}
$report = Join-Path $project ('output\diagnostics\heltec-integrity-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
& $python (Join-Path $PSScriptRoot 'heltec_integrity.py') $Device --duration $Duration --output $report
exit $LASTEXITCODE
