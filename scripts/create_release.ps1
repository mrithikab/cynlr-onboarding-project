# create_release.ps1
# Creates a release package for distribution

$ErrorActionPreference = "Stop"

$BuildConfig = "Release"
$BuildPlatform = "x64"
$SourceDir = "$PSScriptRoot\.."
$BuildDir = "$SourceDir\$BuildPlatform\$BuildConfig"
$ReleaseDir = "$SourceDir\release"

Write-Host "Creating release package..." -ForegroundColor Green

# Clean and create release directory
if (Test-Path $ReleaseDir) {
    Remove-Item -Recurse -Force $ReleaseDir
}
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null
New-Item -ItemType Directory -Path "$ReleaseDir\tests\unit" | Out-Null
New-Item -ItemType Directory -Path "$ReleaseDir\tests\integration" | Out-Null

# Copy main executable
Write-Host "Copying main executable..."
if (Test-Path "$BuildDir\cynlr-onboarding-project.exe") {
    Copy-Item "$BuildDir\cynlr-onboarding-project.exe" "$ReleaseDir\"
    Write-Host "  Copied cynlr-onboarding-project.exe" -ForegroundColor Gray
} else {
    Write-Error "Main executable not found at: $BuildDir\cynlr-onboarding-project.exe"
    exit 1
}

# Copy TestRunner from tests subdirectory
Write-Host "Copying TestRunner..."
if (Test-Path "$BuildDir\tests\TestRunner.exe") {
    Copy-Item "$BuildDir\tests\TestRunner.exe" "$ReleaseDir\"
    Write-Host "  Copied TestRunner.exe" -ForegroundColor Gray
} else {
    Write-Warning "TestRunner not found at: $BuildDir\tests\TestRunner.exe"
}

# Unit tests
Write-Host "Copying unit tests..."
$unitTests = @(
    "TestCsvStreamer.exe",
    "TestFilterBlock.exe",
    "TestFilterBlockCalc.exe",
    "TestDataGenerator.exe"
)

foreach ($test in $unitTests) {
    $src = "$BuildDir\tests\unit\$test"
    if (Test-Path $src) {
        Copy-Item $src "$ReleaseDir\tests\unit\"
        Write-Host "  Copied $test" -ForegroundColor Gray
    } else {
        Write-Warning "Unit test not found: $src"
    }
}

# Integration tests
Write-Host "Copying integration tests..."
$integrationTests = @(
    "IntegrationTestDataGenerator.exe",
    "TestCli.exe"
)

foreach ($test in $integrationTests) {
    $src = "$BuildDir\tests\integration\$test"
    if (Test-Path $src) {
        Copy-Item $src "$ReleaseDir\tests\integration\"
        Write-Host "  Copied $test" -ForegroundColor Gray
    } else {
        Write-Warning "Integration test not found: $src"
    }
}

# Copy test data files from repository root
Write-Host "Copying test data files..."
if (Test-Path "$SourceDir\test.csv") {
    Copy-Item "$SourceDir\test.csv" "$ReleaseDir\"
    Write-Host "  Copied test.csv" -ForegroundColor Gray
} else {
    Write-Warning "test.csv not found at: $SourceDir\test.csv"
}

# Copy any filter kernel test files if they exist
$filterFiles = Get-ChildItem -Path $SourceDir -Filter "*.txt" -File | Where-Object { $_.Name -like "*kernel*" -or $_.Name -like "*filter*" }
if ($filterFiles) {
    Write-Host "Copying filter files..."
    foreach ($file in $filterFiles) {
        Copy-Item $file.FullName "$ReleaseDir\"
        Write-Host "  Copied $($file.Name)" -ForegroundColor Gray
    }
}

# Create README
Write-Host "Creating README..."
$readmeContent = @"
# Cynlr Onboarding Project - Release Package

## Directory Structure

    cynlr-onboarding-project.exe    Main application
    TestRunner.exe                  Runs all tests
    test.csv                        Sample CSV input
    tests/unit/                     Unit test executables
    tests/integration/              Integration test executables

## Quick Start

1. Extract all files maintaining the directory structure
2. Run all tests: TestRunner.exe
3. Run main app: cynlr-onboarding-project.exe --mode=csv --csv=test.csv --threshold=100 --T_ns=1000

## Running the Main Application

CSV Mode:
    cynlr-onboarding-project.exe --mode=csv --csv=test.csv --threshold=100 --T_ns=1000

Random Mode (interactive, press Enter to stop):
    cynlr-onboarding-project.exe --mode=random --threshold=100 --T_ns=1000 --columns=1024

With Statistics (creates pair_metrics.csv):
    cynlr-onboarding-project.exe --mode=csv --csv=test.csv --threshold=100 --T_ns=1000 --stats

Quiet Mode (for scripting):
    cynlr-onboarding-project.exe --mode=csv --csv=test.csv --threshold=100 --T_ns=1000 --quiet

Custom Filter Kernel:
    cynlr-onboarding-project.exe --mode=csv --csv=test.csv --threshold=100 --T_ns=1000 --filter=file --filterfile=my_kernel.txt

## Running Tests

Run all tests:
    TestRunner.exe

Run individual unit tests:
    tests\unit\TestCsvStreamer.exe
    tests\unit\TestFilterBlock.exe
    tests\unit\TestFilterBlockCalc.exe
    tests\unit\TestDataGenerator.exe

Run individual integration tests:
    tests\integration\IntegrationTestDataGenerator.exe
    tests\integration\TestCli.exe

## Command-Line Options

    --mode=random|csv          Input mode (random generation or CSV file)
    --threshold=<number>       Filter threshold value (TV)
    --T_ns=<uint64>           Inter-pair timing in nanoseconds (>=500)
    --columns=<int>           Number of columns (auto-detected for CSV mode)
    --filter=default|file     Use default kernel or load from file
    --stats                   Enable CSV statistics output (pair_metrics.csv)
    --csv=<path>              CSV input file path (required for csv mode)
    --filterfile=<path>       Custom 9-tap filter kernel file
    --quiet                   Suppress all output except errors
    --help                    Display usage information

## Creating Custom CSV Input

Create a CSV file with comma-separated integer values (0-255).
Example: 10,20,30,40,50,60

The application will auto-detect columns, process values in pairs,
clamp to 0-255 range, and skip malformed values with warnings.

## Creating Custom Filter Kernels

Create a text file with 9 space-separated decimal numbers.
Example: 0.1 0.2 0.3 0.4 0.5 0.4 0.3 0.2 0.1

The filter is non-causal:
  - kernel[0] multiplies with oldest sample (most past)
  - kernel[4] multiplies with center (current) sample
  - kernel[8] multiplies with newest (future) sample

## Notes

- Tests create temporary files that are cleaned up automatically
- When using --stats, pair_metrics.csv is created in current directory
- Pipeline prints statistics unless --quiet is specified
- All executables must remain in this directory structure

## Troubleshooting

Executable not found error in tests:
  - Ensure files were extracted maintaining directory structure
  - Set CYNLR_EXE environment variable to cynlr-onboarding-project.exe path

Failed to read CSV error:
  - Check CSV file exists and is readable
  - Verify CSV contains numeric values separated by commas
  - Ensure at least 2 values (one pair) present

Tests fail with path errors:
  - Run TestRunner.exe from release package root directory
  - Do not move executables between directories

## Version Information

Build Configuration: Release x64
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Source: https://github.com/mrithikab/cynlr-onboarding-project

"@

$readmeContent | Out-File -FilePath "$ReleaseDir\README.txt" -Encoding UTF8

Write-Host "`nRelease package created successfully!" -ForegroundColor Green
Write-Host "Location: $ReleaseDir" -ForegroundColor Cyan

Write-Host "`nContents:" -ForegroundColor Yellow
Get-ChildItem -Recurse $ReleaseDir | Select-Object -ExpandProperty FullName | ForEach-Object {
    Write-Host "  $($_.Replace($ReleaseDir, ''))" -ForegroundColor Gray
}

Write-Host "`nNext steps:" -ForegroundColor Yellow
Write-Host "  1. Test locally: cd release && .\TestRunner.exe" -ForegroundColor White
Write-Host "  2. Create ZIP: Compress-Archive -Path release\* -DestinationPath cynlr-onboarding-project-v1.0.zip" -ForegroundColor White
Write-Host "  3. Upload to GitHub Releases tab" -ForegroundColor White
Write-Host "  4. Users extract and run TestRunner.exe" -ForegroundColor White