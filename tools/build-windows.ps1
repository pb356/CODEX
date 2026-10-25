$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
# Developer workstation only: VS 2022 C++ desktop tools, CMake >=3.20, NSIS 3.11.
# End users need none of these. MSVC runtime is linked statically (/MT).
function Checked([scriptblock]$Action) {
    & $Action
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Action" }
}
Checked { cmake -S . -B build/windows -A x64 }
Checked { cmake --build build/windows --config Release --parallel 4 }
Checked { ctest --test-dir build/windows -C Release --output-on-failure }
New-Item -ItemType Directory -Force dist | Out-Null
Copy-Item build/windows/Release/DesktopBoxes.exe build/DesktopBoxes.exe
Checked { makensis /V2 installer/setup.nsi }
Copy-Item build/DesktopBoxes.exe dist/DesktopBoxes.exe
Get-FileHash dist/DesktopBoxes.exe, dist/DesktopBoxes-0.1.0-x64-Setup.exe -Algorithm SHA256 |
    ForEach-Object { '{0}  {1}' -f $_.Hash.ToLowerInvariant(), (Split-Path $_.Path -Leaf) } |
    Set-Content -Encoding ASCII dist/SHA256SUMS.txt
