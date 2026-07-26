param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $here
try {
    & $Python .\generate_case.py
    if ($LASTEXITCODE -ne 0) {
        throw "Enclosure generation failed with exit code $LASTEXITCODE"
    }
    & $Python .\render_previews.py
    if ($LASTEXITCODE -ne 0) {
        throw "Enclosure preview validation failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
