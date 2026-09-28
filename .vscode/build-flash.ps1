$ErrorActionPreference = "Stop"

# ---------------------------------------------------------
# Paths
# ---------------------------------------------------------

$workspace = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir  = Join-Path $workspace "build"

$cmakeCommand = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else {
    Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
}
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake niet gevonden. Installeer CMake en voeg de bin-map toe aan PATH."
}

$dfuCommand = Get-Command "dfu-util.exe" -ErrorAction SilentlyContinue

if (-not $dfuCommand) {
    $dfuCommand = Get-Command "dfu-util" -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------
# BUILD
# ---------------------------------------------------------

Write-Host ""
Write-Host "========================================"
Write-Host " OpenPixelOSD - Configure"
Write-Host "========================================"
Write-Host ""

& $cmake `
    -S $workspace `
    -B $buildDir `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DTARGET_MCU=STM32G431

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure is mislukt."
}

Write-Host ""
Write-Host "========================================"
Write-Host " OpenPixelOSD - Build"
Write-Host "========================================"
Write-Host ""

& $cmake `
    --build $buildDir `
    --parallel

if ($LASTEXITCODE -ne 0) {
    throw "Build is mislukt."
}

# ---------------------------------------------------------
# Firmware zoeken
# ---------------------------------------------------------

$firmware = Join-Path $buildDir "OpenPixelOSD_STM32G431.bin"

if (-not (Test-Path $firmware)) {
    throw "Firmware niet gevonden: $firmware"
}

$file = Get-Item $firmware

Write-Host ""
Write-Host "========================================"
Write-Host " BUILD SUCCESS"
Write-Host "========================================"
Write-Host ""
Write-Host "Firmware:"
Write-Host "  $firmware"
Write-Host ""
Write-Host "Grootte:"
Write-Host "  $($file.Length) bytes"
Write-Host ""

# ---------------------------------------------------------
# Vragen of we moeten uploaden
# ---------------------------------------------------------

$upload = Read-Host "Wil je de firmware nu uploaden via USB DFU? [j/N]"

if ($upload -notmatch '^(j|J|y|Y)$') {
    Write-Host ""
    Write-Host "Upload overgeslagen."
    Write-Host "Build is wel succesvol afgerond."
    exit 0
}

# ---------------------------------------------------------
# dfu-util controleren
# ---------------------------------------------------------

if (-not $dfuCommand) {
    throw "dfu-util is niet gevonden in PATH."
}

$dfu = $dfuCommand.Source

# ---------------------------------------------------------
# STM32 DFU targets detecteren
# ---------------------------------------------------------

function Get-Stm32DfuTargets {

    $lines = & $dfu --list 2>&1

    $targets = @()

    foreach ($line in $lines) {

        if (
            $line -match
            'Found DFU:\s+\[(?<vid>[0-9A-Fa-f]{4}):(?<pid>[0-9A-Fa-f]{4})\].*path="(?<path>[^"]+)".*alt=(?<alt>\d+).*name="(?<name>[^"]*)".*serial="(?<serial>[^"]*)"'
        ) {

            $usbVid = $Matches["vid"].ToLower()
            $usbPid = $Matches["pid"].ToLower()
            $usbPath = $Matches["path"]
            $alt = [int]$Matches["alt"]
            $memoryName = $Matches["name"]
            $serial = $Matches["serial"]

            # Alleen STM32 ROM DFU bootloader
            # Alleen alt 0 = Internal Flash
            if (
                $usbVid -eq "0483" -and
                $usbPid -eq "df11" -and
                $alt -eq 0
            ) {

                $targets += [PSCustomObject]@{
                    VidPid = "${usbVid}:${usbPid}"
                    Serial = $serial
                    Path   = $usbPath
                    Alt    = $alt
                    Name   = $memoryName
                }
            }
        }
    }

    return @($targets)
}

# ---------------------------------------------------------
# Wachten/detecteren
# ---------------------------------------------------------

while ($true) {

    Write-Host ""
    Write-Host "Zoeken naar STM32 DFU targets..."
    Write-Host ""

    $targets = @(Get-Stm32DfuTargets)

    if ($targets.Count -gt 0) {
        break
    }

    Write-Host "Geen STM32 in DFU-mode gevonden." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Zet het board in DFU-mode:"
    Write-Host ""
    Write-Host "  1. BOOT0 ingedrukt houden"
    Write-Host "  2. NRST indrukken"
    Write-Host "  3. NRST loslaten"
    Write-Host "  4. BOOT0 loslaten"
    Write-Host ""

    $retry = Read-Host "Druk ENTER om opnieuw te zoeken, of typ q om te stoppen"

    if ($retry -match '^(q|Q)$') {
        Write-Host "Upload geannuleerd."
        exit 0
    }
}

# ---------------------------------------------------------
# Target kiezen
# ---------------------------------------------------------

Write-Host ""
Write-Host "========================================"
Write-Host " Gedetecteerde STM32 targets"
Write-Host "========================================"
Write-Host ""

for ($i = 0; $i -lt $targets.Count; $i++) {

    $target = $targets[$i]

    Write-Host "[$($i + 1)] STM32 DFU"
    Write-Host "    VID:PID : $($target.VidPid)"
    Write-Host "    Serial  : $($target.Serial)"
    Write-Host "    USB path: $($target.Path)"
    Write-Host "    Memory  : $($target.Name)"
    Write-Host ""
}

if ($targets.Count -eq 1) {

    $target = $targets[0]

    $confirm = Read-Host "Upload naar serial $($target.Serial)? [J/n]"

    if ($confirm -match '^(n|N)$') {
        Write-Host "Upload geannuleerd."
        exit 0
    }

}
else {

    while ($true) {

        $choice = Read-Host "Kies target [1-$($targets.Count)]"

        $number = 0

        if (
            [int]::TryParse($choice, [ref]$number) -and
            $number -ge 1 -and
            $number -le $targets.Count
        ) {
            $target = $targets[$number - 1]
            break
        }

        Write-Host "Ongeldige keuze." -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------
# Flash
# ---------------------------------------------------------

Write-Host ""
Write-Host "========================================"
Write-Host " FLASH STM32G431"
Write-Host "========================================"
Write-Host ""
Write-Host "Target serial : $($target.Serial)"
Write-Host "USB path      : $($target.Path)"
Write-Host "Firmware      : $firmware"
Write-Host ""

& $dfu `
    -d "0483:df11" `
    -S $target.Serial `
    -a 0 `
    -s "0x08000000:leave" `
    -D $firmware

$flashExitCode = $LASTEXITCODE

if ($flashExitCode -ne 0) {

    Write-Host ""
    Write-Host "dfu-util eindigde met exitcode $flashExitCode." -ForegroundColor Red
    Write-Host ""
    throw "Flashen mislukt."
}

Write-Host ""
Write-Host "========================================"
Write-Host " FLASH SUCCESS"
Write-Host "========================================"
Write-Host ""