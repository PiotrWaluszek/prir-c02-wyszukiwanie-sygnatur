#!/usr/bin/env bash
# Demo (macOS / Linux): bash demo/demo.sh [size, default 128M] [threads, default all]
set -euo pipefail

SIZE=${1:-128M}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CORES=$( (sysctl -n hw.ncpu 2>/dev/null || nproc) | tr -d ' ')
THREADS=${2:-$CORES}
NP=$(( THREADS < 4 ? THREADS : 4 ))
WORK=${TMPDIR:-/tmp}/sigscan-demo
mkdir -p "$WORK"

BIN=""
for d in "$ROOT/build" "$ROOT/build/Release"; do
    [[ -x "$d/sigscan-seq" ]] && BIN=$d && break
done
if [[ -z $BIN ]]; then
    echo "Nie znaleziono programów w build/. Najpierw zbuduj projekt:"
    echo "  cmake -B build && cmake --build build -j"
    exit 1
fi

# median of 3 runs; CSV columns: 20 = scan time, 25 = matches, 26 = checksum
measure() {
    local label=$1 out ms last; shift
    if ! out=$("$@" --text "$WORK/demo.bin" --patterns "$WORK/demo.pat" --csv --quiet --warmup 1 --reps 3); then
        echo "$label;;;"  # empty result = error
        return
    fi
    ms=$(cut -d, -f20 <<< "$out" | sort -g | sed -n 2p)
    last=$(tail -n 1 <<< "$out")
    echo "$label;$ms;$(cut -d, -f25 <<< "$last");$(cut -d, -f26 <<< "$last")"
}

echo "== 1. Generator danych: $SIZE losowych bajtów, 1000 wzorców 8-32 B, ziarno 2026"
"$BIN/sigscan-gen" --out "$WORK/demo" --size "$SIZE" --npat 1000 --minlen 8 --maxlen 32 \
    --hit-rate 10 --partial-rate 100 --seed 2026
TRUTH=$(( $(wc -l < "$WORK/demo.truth.csv") - 1 ))
echo "   wstawione trafienia (znane pozycje): $TRUTH"
echo

echo "== 2. Wyszukiwanie (mediana z 3 powtórzeń)"
ROWS=()
ROWS+=("$(measure "sekwencyjna (Aho-Corasick)" "$BIN/sigscan-seq")")
ROWS+=("$(measure "sekwencyjna, automat skompresowany" "$BIN/sigscan-seq" --layout compressed)")
ROWS+=("$(measure "sekwencyjna, przeplot K=8 (1 rdzeń)" "$BIN/sigscan-seq" --interleave 8)")
if [[ -x "$BIN/sigscan-omp" ]]; then
    ROWS+=("$(measure "OpenMP, $THREADS wątków, bloki" "$BIN/sigscan-omp" --threads "$THREADS" --schedule blocks)")
    ROWS+=("$(measure "OpenMP, $THREADS wątków, dynamic 1M" "$BIN/sigscan-omp" --threads "$THREADS" --schedule dynamic --chunk 1M)")
    ROWS+=("$(measure "OpenMP, $THREADS wątków, dynamic 4 KiB" "$BIN/sigscan-omp" --threads "$THREADS" --schedule dynamic --chunk 4k)")
    ROWS+=("$(measure "OpenMP, $THREADS wątków, przeplot K=8" "$BIN/sigscan-omp" --threads "$THREADS" --schedule dynamic --chunk 1M --interleave 8)")
else
    echo "   (brak sigscan-omp - OpenMP nie został znaleziony przy budowaniu, pomijam)"
fi
if [[ -x "$BIN/sigscan-mpi" ]] && command -v mpirun > /dev/null; then
    MPIRUN=(mpirun -np "$NP")
    mpirun --version 2>&1 | grep -qi "hydra" || MPIRUN+=(--oversubscribe)
    ROWS+=("$(TMPDIR=/tmp measure "MPI, $NP procesy" "${MPIRUN[@]}" "$BIN/sigscan-mpi")")
else
    echo "   (brak sigscan-mpi albo mpirun - pomijam MPI)"
fi

BYTES=$(wc -c < "$WORK/demo.bin" | tr -d ' ')
printf '\n   %9s %7s %8s %9s  %-16s  %s\n' "skan [ms]" "MB/s" "przysp." "trafienia" "suma kontrolna" "wersja"
REF_MS="" REF_M="" REF_C="" OK=1
for row in "${ROWS[@]}"; do
    IFS=';' read -r label ms m c <<< "$row"
    if [[ -z $ms ]]; then
        echo "   (błąd uruchomienia)                                         $label"
        OK=0
        continue
    fi
    [[ -z $REF_MS ]] && REF_MS=$ms REF_M=$m REF_C=$c
    [[ $m == "$REF_M" && $c == "$REF_C" ]] || OK=0
    awk -v l="$label" -v ms="$ms" -v r="$REF_MS" -v b="$BYTES" -v m="$m" -v c="$c" \
        'BEGIN { printf "   %9.1f %7.0f %7.2fx %9s  %-16s  %s\n", ms, b / ms / 1000, r / ms, m, c, l }'
done
echo "   przysp. = ile razy szybciej niż wersja sekwencyjna; kawałki 4 KiB to ponad 30 tys. granic,"
echo "   na których reguła właściciela nie może zgubić ani zdublować trafienia"
echo

if [[ -x "$BIN/sigscan-omp" ]]; then
    echo "== 3. Skalowanie OpenMP, jak na rys. 8 (dynamic 1M): przyspieszenie S(p) i efektywność E(p) = S(p)/p"
    PS=()
    for (( p = 1; p < THREADS; p *= 2 )); do PS+=("$p"); done
    PS+=("$THREADS")
    printf '\n   %8s %9s %8s %6s\n' "wątki" "skan [ms]" "S(p)" "E(p)"
    for p in "${PS[@]}"; do
        IFS=';' read -r _ ms m c <<< "$(measure "p=$p" "$BIN/sigscan-omp" --threads "$p" --schedule dynamic --chunk 1M)"
        if [[ -z $ms || $m != "$REF_M" || $c != "$REF_C" ]]; then OK=0; echo "   błąd dla $p wątków"; continue; fi
        awk -v p="$p" -v ms="$ms" -v r="$REF_MS" 'BEGIN { printf "   %6d %9.1f %8.2f %6.2f\n", p, ms, r / ms, r / ms / p }'
    done
    echo
fi

# one row per value: prints "value ms GB/s automaton_MiB"; args: value, command...
sweep_row() {
    local v=$1 out ms last; shift
    if ! out=$("$@" --csv --quiet --warmup 1 --reps 3); then OK=0; echo "   błąd dla $v"; return; fi
    ms=$(cut -d, -f20 <<< "$out" | sort -g | sed -n 2p)
    last=$(tail -n 1 <<< "$out")
    awk -v v="$v" -v ms="$ms" -v b="$(cut -d, -f10 <<< "$last")" -v a="$(cut -d, -f14 <<< "$last")" \
        'BEGIN { printf "   %10s %9.1f %8.2f %10.1f\n", v, ms, b / ms / 1e6, a / 1048576 }'
}
D=(--text "$WORK/demo.bin" --patterns "$WORK/demo.pat")

if [[ -x "$BIN/sigscan-mpi" ]] && command -v mpirun > /dev/null; then
    echo "== 4. Skalowanie MPI na jednym komputerze, jak na rys. 9"
    printf '\n   %10s %9s %8s %10s\n' "procesy" "skan [ms]" "GB/s" "automat MiB"
    for np in 1 2 4 "$THREADS"; do
        (( np > THREADS )) && continue
        MR=(mpirun -np "$np"); mpirun --version 2>&1 | grep -qi "hydra" || MR+=(--oversubscribe)
        TMPDIR=/tmp sweep_row "$np" "${MR[@]}" "$BIN/sigscan-mpi" "${D[@]}"
        [[ $np == "$THREADS" ]] && break
    done
    echo
fi

echo "== 5. Przeplot K niezależnych przejść automatu w jednym wątku, jak na rys. 10"
printf '\n   %10s %9s %8s %10s\n' "K" "skan [ms]" "GB/s" "automat MiB"
for k in 1 2 4 8 16; do sweep_row "$k" "$BIN/sigscan-seq" "${D[@]}" --interleave "$k"; done
echo "   na M1 Pro (rys. 10) maksimum wypada przy K=8; liczby zależą od procesora i kompilatora"
echo

if [[ -x "$BIN/sigscan-omp" ]]; then
    echo "== 6. Krzywa U: czas OpenMP ($THREADS wątków, dynamic) w funkcji rozmiaru kawałka, jak na rys. 15"
    printf '\n   %11s %9s %8s %10s\n' "kawałek" "skan [ms]" "GB/s" "automat MiB"
    for c in 4k 64k 1M 8M 32M; do
        sweep_row "$c" "$BIN/sigscan-omp" "${D[@]}" --threads "$THREADS" --schedule dynamic --chunk "$c"
    done
    echo "   przy dużych kawałkach (32M to tylko 4 kawałki) część wątków nie dostaje pracy i czas rośnie"
    echo
fi

echo "== 7. Liczba wzorców: rozmiar automatu i przepustowość referencji, jak na rys. 4 (64 MiB tekstu)"
printf '\n   %10s %9s %8s %10s\n' "wzorce" "skan [ms]" "GB/s" "automat MiB"
for k in 10 100 1000 10000; do
    "$BIN/sigscan-gen" --out "$WORK/np$k" --size 64M --npat "$k" --minlen 8 --maxlen 32 --seed 2026 2> /dev/null
    sweep_row "$k" "$BIN/sigscan-seq" --text "$WORK/np$k.bin" --patterns "$WORK/np$k.pat"
done
echo "   im większy automat, tym mniej mieści się w pamięci podręcznej i tym wolniejszy skan"
echo

echo "== 8. Czy znaleziono każde trafienie wstawione przez generator (lista par wzorzec, pozycja)?"
"$BIN/sigscan-seq" --text "$WORK/demo.bin" --patterns "$WORK/demo.pat" --mode list --out "$WORK/found.csv" --quiet
MISSING=$(comm -13 <(sort "$WORK/found.csv") <(sort "$WORK/demo.truth.csv") | wc -l | tr -d ' ')
echo "   brakujących: $MISSING z $TRUTH"
echo
(( MISSING == 0 )) || OK=0
if (( OK )); then
    echo "WYNIK: OK - wszystkie wersje znalazły te same $REF_M trafień z tą samą sumą kontrolną,"
    echo "       w tym wszystkie $TRUTH wstawione przez generator."
else
    echo "WYNIK: BŁĄD - któraś wersja się nie uruchomiła, dała inny wynik albo pominęła wstawione trafienie."
    exit 1
fi
