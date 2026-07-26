param(
    [switch]$Render
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$localBin = Join-Path $env:LOCALAPPDATA "Programs\KiCad\10.0\bin"
$machineBin = Join-Path $env:ProgramFiles "KiCad\10.0\bin"
$kicadBin = if (Test-Path (Join-Path $localBin "kicad-cli.exe")) {
    $localBin
} elseif (Test-Path (Join-Path $machineBin "kicad-cli.exe")) {
    $machineBin
} else {
    throw "KiCad 10 was not found."
}

$python = Join-Path $kicadBin "python.exe"
$cli = Join-Path $kicadBin "kicad-cli.exe"
$board = Join-Path $PSScriptRoot "cdtosonos-heltec-carrier.kicad_pcb"
$schematic = Join-Path $PSScriptRoot "cdtosonos-heltec-carrier.kicad_sch"
$fabrication = Join-Path $PSScriptRoot "fabrication"
$previews = Join-Path $PSScriptRoot "previews"
$pdfOutput = Join-Path $repoRoot "output\pdf"

New-Item -ItemType Directory -Force -Path $fabrication, $previews, $pdfOutput | Out-Null

& $python -c "import kiutils" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "kiutils is missing. Run: & `"$python`" -m pip install kiutils==1.4.8"
}

& $python (Join-Path $PSScriptRoot "generate_kicad.py")
if ($LASTEXITCODE -ne 0) { throw "KiCad source generation failed." }

& $cli sch upgrade --force $schematic
if ($LASTEXITCODE -ne 0) { throw "Schematic upgrade failed." }

& $cli sch erc --severity-all --exit-code-violations --units mm `
    --output (Join-Path $PSScriptRoot "erc-report.txt") $schematic
if ($LASTEXITCODE -ne 0) { throw "ERC failed." }

& $cli pcb drc --refill-zones --save-board --schematic-parity `
    --severity-all --exit-code-violations --units mm `
    --output (Join-Path $PSScriptRoot "drc-report.txt") $board
if ($LASTEXITCODE -ne 0) { throw "DRC or schematic parity failed." }

& $cli pcb export gerbers --output $fabrication `
    --layers "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts" `
    --check-zones $board
if ($LASTEXITCODE -ne 0) { throw "Gerber export failed." }

& $cli pcb export drill --output $fabrication --format excellon `
    --excellon-units mm --excellon-separate-th --generate-map `
    --map-format pdf --generate-report `
    --report-path (Join-Path $fabrication "drill-report.txt") $board
if ($LASTEXITCODE -ne 0) { throw "Drill export failed." }

& $cli sch export pdf --black-and-white `
    --output (Join-Path $pdfOutput "cdtosonos-heltec-carrier-schematic.pdf") `
    $schematic
if ($LASTEXITCODE -ne 0) { throw "Schematic PDF export failed." }

& $cli pcb export pdf --mode-single `
    --output (Join-Path $pdfOutput "cdtosonos-heltec-carrier-1to1.pdf") `
    --layers "F.Cu,F.Silkscreen,Edge.Cuts" --black-and-white `
    --scale 1 --check-zones $board
if ($LASTEXITCODE -ne 0) { throw "1:1 PDF export failed." }

if ($Render) {
    & $cli pcb render `
        --output (Join-Path $previews "cdtosonos-heltec-carrier-top.png") `
        --width 1800 --height 1200 --side top --background opaque `
        --quality high --floor --perspective --rotate "35,0,-35" $board
    if ($LASTEXITCODE -ne 0) { throw "Top render failed." }

    & $cli pcb render `
        --output (Join-Path $previews "cdtosonos-heltec-carrier-bottom.png") `
        --width 1800 --height 1200 --side bottom --background opaque `
        --quality high --floor --perspective --rotate "35,0,35" $board
    if ($LASTEXITCODE -ne 0) { throw "Bottom render failed." }
}

Compress-Archive -Path (Join-Path $fabrication "*") `
    -DestinationPath (Join-Path $PSScriptRoot "cdtosonos-heltec-carrier-gerbers.zip") `
    -CompressionLevel Optimal -Force

Write-Host "Carrier build, checks and exports completed successfully."
