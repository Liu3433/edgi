param(
    [string]$ArduinoData = "$env:LOCALAPPDATA/Arduino15",
    [string]$ArduinoCli = ''
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$avr = Join-Path $ArduinoData 'packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin'
$platform = Join-Path $ArduinoData 'packages/arduino/hardware/avr/1.8.7'
$core = Join-Path $platform 'cores/arduino'
$servo = Join-Path $ArduinoData 'libraries/Servo/src'
$softSerial = Join-Path $platform 'libraries/SoftwareSerial/src'
$output = Join-Path $project 'build/robot_uno_verify'
New-Item -ItemType Directory -Force -Path $output | Out-Null
# Prefer the same preprocessing/library pipeline used by Arduino IDE.
if (-not $ArduinoCli) {
    $cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cliCommand) { $ArduinoCli = $cliCommand.Source }
    else {
        foreach ($candidate in @(
            "$env:ProgramFiles/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe",
            "$env:LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe")) {
            if (Test-Path -LiteralPath $candidate) { $ArduinoCli = $candidate; break }
        }
    }
}
if ($ArduinoCli) {
    & $ArduinoCli compile --fqbn arduino:avr:uno --library (Join-Path $ArduinoData 'libraries/Servo') --build-path (Join-Path $project 'build/robot_uno_ide') --output-dir $output (Join-Path $project 'arduino/EdgiTalkUnoServo')
    if ($LASTEXITCODE -ne 0) { throw 'Arduino IDE/CLI pipeline compilation failed' }
    foreach ($extension in @('hex','elf')) {
        Copy-Item -LiteralPath (Join-Path $output "EdgiTalkUnoServo.ino.$extension") -Destination (Join-Path $output "EdgiTalkUnoServo.$extension")
    }
    return
}
Write-Output 'Arduino CLI unavailable; validating with AVR GCC directly (without IDE preprocessing).'
$common = @('-Os', '-g', '-ffunction-sections', '-fdata-sections', '-flto', '-mmcu=atmega328p',
    '-DF_CPU=16000000L', '-DARDUINO=10819', '-DARDUINO_AVR_UNO', '-DARDUINO_ARCH_AVR',
    "-I$core", "-I$platform/variants/standard", "-I$servo", "-I$softSerial")
$cppFlags = @('-std=gnu++11', '-fno-exceptions', '-fno-threadsafe-statics')
$objects = @()
$sources = @(Get-ChildItem -LiteralPath $core -File | Where-Object { $_.Extension -in '.c', '.cpp', '.S' })
$sources += Get-Item (Join-Path $servo 'avr/Servo.cpp'), (Join-Path $softSerial 'SoftwareSerial.cpp')
foreach ($source in $sources) {
    $object = Join-Path $output ($source.Name + '.o')
    if ($source.Extension -eq '.cpp') {
        & "$avr/avr-g++" @common @cppFlags -c $source.FullName -o $object
    } elseif ($source.Extension -eq '.S') {
        & "$avr/avr-gcc" @common -x assembler-with-cpp -c $source.FullName -o $object
    } else {
        & "$avr/avr-gcc" @common -std=gnu11 -c $source.FullName -o $object
    }
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($source.Name)" }
    $objects += $object
}
$sketch = Join-Path $project 'arduino/EdgiTalkUnoServo/EdgiTalkUnoServo.ino'
$sketchObject = Join-Path $output 'sketch.o'
& "$avr/avr-g++" @common @cppFlags -Wall -Wextra -include Arduino.h -x c++ -c $sketch -o $sketchObject
if ($LASTEXITCODE -ne 0) { throw 'UNO sketch compilation failed' }
$elf = Join-Path $output 'EdgiTalkUnoServo.elf'
& "$avr/avr-gcc" -Os -flto -fuse-linker-plugin '-Wl,--gc-sections' -mmcu=atmega328p -o $elf $sketchObject @objects -lm
if ($LASTEXITCODE -ne 0) { throw 'UNO linking failed' }
& "$avr/avr-objcopy" -O ihex -R .eeprom $elf (Join-Path $output 'EdgiTalkUnoServo.hex')
if ($LASTEXITCODE -ne 0) { throw 'UNO HEX export failed' }
& "$avr/avr-size" -C --mcu=atmega328p $elf
if ($LASTEXITCODE -ne 0) { throw 'UNO size check failed' }
