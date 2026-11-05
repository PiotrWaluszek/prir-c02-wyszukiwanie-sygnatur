# Demo (Windows): powershell -ExecutionPolicy Bypass -File demo\demo.ps1 [-Size 128M] [-Threads N]
param(
    [string]$Size = "128M",
    [int]$Threads = [Environment]::ProcessorCount
)
$ErrorActionPreference = "Stop"
$Inv = [Globalization.CultureInfo]::InvariantCulture

$Root = Split-Path -Parent $PSScriptRoot
$NP = [Math]::Min(4, $Threads)
$Work = Join-Path $env:TEMP "sigscan-demo"
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# Visual Studio builds into build\Release
$Bin = $null
foreach ($d in @("build\Release", "build", "build\RelWithDebInfo", "build\Debug")) {
    if (Test-Path (Join-Path $Root "$d\sigscan-seq.exe")) { $Bin = Join-Path $Root $d; break }
}
if (-not $Bin) {
    Write-Host "Nie znaleziono programów w build\. Najpierw zbuduj projekt:"
    Write-Host "  cmake -B build"
    Write-Host "  cmake --build build --config Release"
    exit 1
}
function Exe($name) { Join-Path $Bin "$name.exe" }

function Invoke-Checked($exe, [string[]]$argv) {
    $tries = if ((Split-Path -Leaf $exe) -like "mpiexec*") { 5 } else { 1 }
    for ($t = 1; $t -le $tries; $t++) {
        if ($t -gt 1) { Start-Sleep -Seconds 2 }
        $out = & $exe @argv
        if ($LASTEXITCODE -eq 0) { return $out }
    }
    throw "program zakończył się błędem ($LASTEXITCODE): $exe $($argv -join ' ')"
}

# median of 3 runs; CSV columns: 20 = scan time, 25 = matches, 26 = checksum
function Measure-Version($label, $exe, [string[]]$argv) {
    $common = @("--text", "$Work\demo.bin", "--patterns", "$Work\demo.pat", "--csv", "--quiet", "--warmup", "1", "--reps", "3")
    try {
        $lines = @(Invoke-Checked $exe ($argv + $common) | Where-Object { $_ -match "," })
    } catch {
        Write-Host "   $_"
        return [pscustomobject]@{ Label = $label; Ms = $null; Matches = $null; Checksum = $null }
    }
    $times = $lines | ForEach-Object { [double]::Parse($_.Split(",")[19], $Inv) } | Sort-Object
    $last = $lines[-1].Split(",")
    [pscustomobject]@{ Label = $label; Ms = $times[1]; Matches = $last[24]; Checksum = $last[25] }
}

Write-Host "== 1. Generator danych: $Size losowych bajtów, 1000 wzorców 8-32 B, ziarno 2026"
Invoke-Checked (Exe "sigscan-gen") @("--out", "$Work\demo", "--size", $Size, "--npat", "1000", "--minlen", "8",
    "--maxlen", "32", "--hit-rate", "10", "--partial-rate", "100", "--seed", "2026") | Out-Null
$Truth = @(Get-Content "$Work\demo.truth.csv" | Select-Object -Skip 1)
Write-Host "   wstawione trafienia (znane pozycje): $($Truth.Count)"
Write-Host ""

Write-Host "== 2. Wyszukiwanie (mediana z 3 powtórzeń)"
$rows = @()
$rows += Measure-Version "sekwencyjna (Aho-Corasick)" (Exe "sigscan-seq") @()
$rows += Measure-Version "sekwencyjna, automat skompresowany" (Exe "sigscan-seq") @("--layout", "compressed")
$rows += Measure-Version "sekwencyjna, przeplot K=8 (1 rdzeń)" (Exe "sigscan-seq") @("--interleave", "8")
$hasOmp = Test-Path (Exe "sigscan-omp")
if ($hasOmp) {
    $rows += Measure-Version "OpenMP, $Threads wątków, bloki" (Exe "sigscan-omp") @("--threads", "$Threads", "--schedule", "blocks")
    $rows += Measure-Version "OpenMP, $Threads wątków, dynamic 1M" (Exe "sigscan-omp") @("--threads", "$Threads", "--schedule", "dynamic", "--chunk", "1M")
    $rows += Measure-Version "OpenMP, $Threads wątków, dynamic 4 KiB" (Exe "sigscan-omp") @("--threads", "$Threads", "--schedule", "dynamic", "--chunk", "4k")
    $rows += Measure-Version "OpenMP, $Threads wątków, przeplot K=8" (Exe "sigscan-omp") @("--threads", "$Threads", "--schedule", "dynamic", "--chunk", "1M", "--interleave", "8")
} else {
    Write-Host "   (brak sigscan-omp.exe - OpenMP nie został znaleziony przy budowaniu, pomijam)"
}
$mpiexec = Get-Command mpiexec -ErrorAction SilentlyContinue
if ((Test-Path (Exe "sigscan-mpi")) -and $mpiexec) {
    $rows += Measure-Version "MPI, $NP procesy" $mpiexec.Source @("-n", "$NP", (Exe "sigscan-mpi"))
} else {
    Write-Host "   (brak sigscan-mpi.exe albo mpiexec - pomijam MPI; MPI wymaga MS-MPI, patrz docs\URUCHOMIENIE.md)"
}

$bytes = (Get-Item "$Work\demo.bin").Length
$ref = $rows[0]
$ok = $true
Write-Host ""
Write-Host ("   {0,9} {1,7} {2,8} {3,9}  {4,-16}  {5}" -f "skan [ms]", "MB/s", "przysp.", "trafienia", "suma kontrolna", "wersja")
foreach ($r in $rows) {
    if ($null -eq $r.Ms) {
        Write-Host ("   {0,-62} {1}" -f "(błąd uruchomienia)", $r.Label)
        $ok = $false
        continue
    }
    if ($r.Matches -ne $ref.Matches -or $r.Checksum -ne $ref.Checksum) { $ok = $false }
    Write-Host ("   {0,9:F1} {1,7:F0} {2,7:F2}x {3,9}  {4,-16}  {5}" -f $r.Ms, ($bytes / $r.Ms / 1000), ($ref.Ms / $r.Ms),
        $r.Matches, $r.Checksum, $r.Label)
}
Write-Host "   przysp. = ile razy szybciej niż wersja sekwencyjna; kawałki 4 KiB to ponad 30 tys. granic,"
Write-Host "   na których reguła właściciela nie może zgubić ani zdublować trafienia"
Write-Host ""

if ($hasOmp) {
    Write-Host "== 3. Skalowanie OpenMP, jak na rys. 8 (dynamic 1M): przyspieszenie S(p) i efektywność E(p) = S(p)/p"
    $ps = @()
    for ($p = 1; $p -lt $Threads; $p *= 2) { $ps += $p }
    $ps += $Threads
    Write-Host ""
    Write-Host ("   {0,7} {1,9} {2,8} {3,6}" -f "wątki", "skan [ms]", "S(p)", "E(p)")
    foreach ($p in $ps) {
        $r = Measure-Version "p=$p" (Exe "sigscan-omp") @("--threads", "$p", "--schedule", "dynamic", "--chunk", "1M")
        if ($null -eq $r.Ms -or $r.Matches -ne $ref.Matches -or $r.Checksum -ne $ref.Checksum) {
            Write-Host "   błąd dla $p wątków"
            $ok = $false
            continue
        }
        Write-Host ("   {0,7} {1,9:F1} {2,8:F2} {3,6:F2}" -f $p, $r.Ms, ($ref.Ms / $r.Ms), ($ref.Ms / $r.Ms / $p))
    }
    Write-Host ""
}

function Sweep-Row($value, $exe, [string[]]$argv) {
    try {
        $lines = @(Invoke-Checked $exe ($argv + @("--csv", "--quiet", "--warmup", "1", "--reps", "3")) | Where-Object { $_ -match "," })
    } catch {
        Write-Host "   błąd dla $value"
        $script:ok = $false
        return
    }
    $times = $lines | ForEach-Object { [double]::Parse($_.Split(",")[19], $Inv) } | Sort-Object
    $last = $lines[-1].Split(",")
    $ms = $times[1]
    Write-Host ("   {0,10} {1,9:F1} {2,8:F2} {3,10:F1}" -f $value, $ms, ([double]$last[9] / $ms / 1e6), ([double]$last[13] / 1048576))
}
$D = @("--text", "$Work\demo.bin", "--patterns", "$Work\demo.pat")
$header = "   {0,10} {1,9} {2,8} {3,10}"

if ((Test-Path (Exe "sigscan-mpi")) -and $mpiexec) {
    Write-Host "== 4. Skalowanie MPI na jednym komputerze, jak na rys. 9"
    Write-Host ""
    Write-Host ($header -f "procesy", "skan [ms]", "GB/s", "automat MiB")
    foreach ($np in (@(1, 2, 4, $Threads) | Where-Object { $_ -le $Threads } | Select-Object -Unique)) {
        Sweep-Row $np $mpiexec.Source (@("-n", "$np", (Exe "sigscan-mpi")) + $D)
    }
    Write-Host ""
}

Write-Host "== 5. Przeplot K niezależnych przejść automatu w jednym wątku, jak na rys. 10"
Write-Host ""
Write-Host ($header -f "K", "skan [ms]", "GB/s", "automat MiB")
foreach ($k in 1, 2, 4, 8, 16) { Sweep-Row $k (Exe "sigscan-seq") ($D + @("--interleave", "$k")) }
Write-Host "   na M1 Pro (rys. 10) maksimum wypada przy K=8; liczby zależą od procesora i kompilatora"
Write-Host ""

if ($hasOmp) {
    Write-Host "== 6. Krzywa U: czas OpenMP ($Threads wątków, dynamic) w funkcji rozmiaru kawałka, jak na rys. 15"
    Write-Host ""
    Write-Host ($header -f "kawałek", "skan [ms]", "GB/s", "automat MiB")
    foreach ($c in "4k", "64k", "1M", "8M", "32M") {
        Sweep-Row $c (Exe "sigscan-omp") ($D + @("--threads", "$Threads", "--schedule", "dynamic", "--chunk", $c))
    }
    Write-Host "   przy dużych kawałkach (32M to tylko 4 kawałki) część wątków nie dostaje pracy i czas rośnie"
    Write-Host ""
}

Write-Host "== 7. Liczba wzorców: rozmiar automatu i przepustowość referencji, jak na rys. 4 (64 MiB tekstu)"
Write-Host ""
Write-Host ($header -f "wzorce", "skan [ms]", "GB/s", "automat MiB")
foreach ($k in 10, 100, 1000, 10000) {
    Invoke-Checked (Exe "sigscan-gen") @("--out", "$Work\np$k", "--size", "64M", "--npat", "$k", "--minlen", "8",
        "--maxlen", "32", "--seed", "2026") | Out-Null
    Sweep-Row $k (Exe "sigscan-seq") @("--text", "$Work\np$k.bin", "--patterns", "$Work\np$k.pat")
}
Write-Host "   im większy automat, tym mniej mieści się w pamięci podręcznej i tym wolniejszy skan"
Write-Host ""

Write-Host "== 8. Czy znaleziono każde trafienie wstawione przez generator (lista par wzorzec, pozycja)?"
Invoke-Checked (Exe "sigscan-seq") @("--text", "$Work\demo.bin", "--patterns", "$Work\demo.pat", "--mode", "list",
    "--out", "$Work\found.csv", "--quiet") | Out-Null
$found = New-Object 'System.Collections.Generic.HashSet[string]'
Get-Content "$Work\found.csv" | Select-Object -Skip 1 | ForEach-Object { [void]$found.Add($_) }
$missing = @($Truth | Where-Object { -not $found.Contains($_) }).Count
Write-Host "   brakujących: $missing z $($Truth.Count)"
Write-Host ""
if ($missing -ne 0) { $ok = $false }

if ($ok) {
    Write-Host "WYNIK: OK - wszystkie wersje znalazły te same $($ref.Matches) trafień z tą samą sumą kontrolną,"
    Write-Host "       w tym wszystkie $($Truth.Count) wstawione przez generator."
} else {
    Write-Host "WYNIK: BŁĄD - któraś wersja się nie uruchomiła, dała inny wynik albo pominęła wstawione trafienie."
    exit 1
}
