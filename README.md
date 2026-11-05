# C02 - Wyszukiwanie sygnatur w strumieniu

Projekt z Programowania równoległego i rozproszonego (2026/2027). Program wyszukuje wiele sygnatur
(literalnych wzorców bajtowych) w dużym strumieniu bajtów i zwraca pary `(id wzorca, pozycja)`.
Projekt obejmuje cztery wersje: sekwencyjny Aho-Corasick (referencja), OpenMP, MPI i CUDA (PFAC).
Wszystkie dają bit w bit ten sam wynik.

| Program | Opis |
|---|---|
| `sigscan-gen` | generator danych: tekst, wzorce, znane pozycje trafień (ground truth) |
| `sigscan-seq` | referencja: Aho-Corasick, układ gęsty (DFA) albo skompresowany; `--interleave K` |
| `sigscan-omp` | OpenMP: bloki lub kawałki `static`/`dynamic`/`guided` z zakładką `Lmax − 1` |
| `sigscan-mpi` | MPI: Bcast wzorców, Scatterv tekstu, Alltoallv zakładek, Reduce/Gatherv wyników |
| `sigscan-cuda` | CUDA: PFAC, 5 wariantów kernela, tryb potokowy (`--streams`) |

Raport znajduje się w `docs/raport.pdf`.

## Szybkie uruchomienie (bez GPU)

Najprościej przez Docker (Windows, macOS, Linux), bez instalowania kompilatora i MPI. Po zainstalowaniu
Docker Desktop, w katalogu projektu:
```
docker build -t c02 .
docker run --rm c02                                              # demo
docker run --rm c02 ctest --test-dir build --output-on-failure   # testy
```

Poniżej budowanie bezpośrednio na macOS albo Windows. Karta graficzna nie jest potrzebna. Do
zbudowania i sprawdzenia projektu wystarczy kompilator C++ i CMake, pozostałe narzędzia są opcjonalne.
Szczegółowa instrukcja, razem z opisem typowych problemów, znajduje się w
[docs/URUCHOMIENIE.md](docs/URUCHOMIENIE.md).

### Wymagane narzędzia

| | macOS | Windows |
|---|---|---|
| **wymagane** | narzędzia Apple (`xcode-select --install`), Homebrew (https://brew.sh), `brew install cmake libomp` | Visual Studio 2022 lub nowsze (Community albo same „Build Tools”) z pakietem „Programowanie aplikacji klasycznych w języku C++” (zawiera kompilator, OpenMP i CMake) |
| opcjonalne: MPI | `brew install open-mpi` | MS-MPI: `msmpisetup.exe` oraz `msmpisdk.msi` |
| opcjonalne: Python 3 | zwykle jest już zainstalowany | python.org, z opcją „Add python.exe to PATH” |
| **niewymagane** | GPU, CUDA, Google Colab, LaTeX | GPU, CUDA, Google Colab, LaTeX, WSL |

Bez MPI budują się i działają wersje sekwencyjna i OpenMP, a demo pomija MPI. Bez Pythona nie
uruchamia się jedynie test integracyjny. Pierwsze wywołanie `cmake -B build` pobiera z internetu
bibliotekę testową doctest.

### Budowanie i demo

macOS (Terminal, w katalogu projektu):
```bash
cmake -B build && cmake --build build -j
bash demo/demo.sh
```

Windows („Developer PowerShell for VS” z menu Start, w katalogu projektu):
```powershell
cmake -B build
cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File demo\demo.ps1
```

Demo trwa około minuty. Generuje dane i przeszukuje je wersją sekwencyjną (oba układy automatu,
z przeplotem i bez), OpenMP (bloki, małe i duże kawałki, przeplot) i MPI, pokazuje skalowanie OpenMP
od jednego wątku do wszystkich rdzeni oraz krótkie wersje eksperymentów ze sprawozdania (MPI,
przeplot, krzywa U, liczba wzorców), a następnie sprawdza, czy wszystkie wersje dały tę samą liczbę
trafień i tę samą sumę kontrolną oraz czy znaleziono każde trafienie wstawione przez generator. Na
końcu wypisuje `WYNIK: OK`. Generator jest deterministyczny, więc dla domyślnych ustawień na każdym systemie
wychodzi 1276 trafień i suma kontrolna `866ccea0c9671955`.

Pełne testy uruchamia `ctest --test-dir build --output-on-failure` (na Windows z dodatkowym `-C Release`).
Projekt jest automatycznie budowany i testowany na macOS, Windows, Linuksie i w obrazie Dockera przy
każdej zmianie w repozytorium (`.github/workflows/build.yml`).

## Wymagania (pełna lista)

- CMake ≥ 3.24 i kompilator C++20: Apple clang 16, g++ 13 albo MSVC z Visual Studio 2022 lub nowsze
- OpenMP: na macOS `brew install libomp` (CMake wykrywa go automatycznie); MSVC i g++ mają go wbudowanego
- MPI: Open MPI (`brew install open-mpi` / `apt install libopenmpi-dev`), na Windows MS-MPI (runtime i SDK)
- CUDA (opcjonalnie): toolkit ≥ 12, GPU `sm_75` lub nowsze
- Python 3: test integracyjny i skrypty pomiarowe; wykresy wymagają `pip install -r scripts/plots/requirements.txt`

## Budowanie i testy

```bash
cmake -B build                          # Release; OpenMP, MPI i CUDA są wykrywane automatycznie
cmake --build build -j
ctest --test-dir build --output-on-failure   # testy jednostkowe i integracyjny (wszystkie wersje vs referencja)
```

Poszczególne technologie można wyłączyć opcjami `-DSIGSCAN_WITH_CUDA=OFF`, `-DSIGSCAN_WITH_MPI=OFF`
i `-DSIGSCAN_WITH_OMP=OFF`.

## Przykład użycia

```bash
B=build
$B/sigscan-gen --out data/demo --size 256M --npat 1000 --minlen 8 --maxlen 32 --hit-rate 10 --seed 2026
$B/sigscan-seq --text data/demo.bin --patterns data/demo.pat --mode list --out ref.csv
$B/sigscan-omp --text data/demo.bin --patterns data/demo.pat --threads 10 --schedule dynamic --chunk 1M --interleave 8
mpirun -np 8 $B/sigscan-mpi --text data/demo.bin --patterns data/demo.pat
$B/sigscan-cuda --text data/demo.bin --patterns data/demo.pat --kernel smem-root --verify
```

Każdy program wypisuje na stderr podsumowanie (czasy, MB/s, liczba trafień, suma kontrolna).
Z opcją `--csv` wypisuje jeden rekord CSV na każde powtórzenie (`--reps`, `--warmup`).

Formaty plików:
- `.bin` to surowe bajty;
- `.pat` zawiera jeden wzorzec na linię zapisany w hex (`#` rozpoczyna komentarz), a numer linii jest identyfikatorem wzorca;
- trafienia są zapisywane jako CSV `pattern_id,offset`.

## Odtworzenie pomiarów

```bash
bash bench/env_info.sh                                  # konfiguracja sprzętu → results/env/
python3 bench/run.py bench/configs/mac/*.json           # serie CPU (Mac) → results/raw/<eksperyment>.csv
bench/colab/run_gpu.sh                                  # serie GPU na Google Colab (T4) przez Colab CLI
python3 scripts/check_results.py                        # te same dane ⇒ ta sama liczba trafień i suma kontrolna
python3 scripts/datasets.py                             # results/raw/datasets.csv: parametry i ziarno każdego zbioru
python3 scripts/plots/make_all.py                       # wykresy (PDF) i tabele LaTeX → results/figures, results/tables
```

Działanie `run.py`:
- eksperymenty są opisane w `bench/configs/*.json`;
- zbiory danych są generowane raz i przechowywane w `data/` pod skrótem parametrów;
- przed serią powstają wszystkie jej zbiory, a kolejność wszystkich uruchomień jest losowana ze stałym
  ziarnem, także między zbiorami danych;
- `--resume` pomija już zmierzone punkty, a `--dry-run` tylko wypisuje polecenia.

`run_gpu.sh` wysyła kod z bieżącego commita, buduje go i uruchamia testy, a przy błędzie przerywa
pracę. Każda seria działa w tle na maszynie wirtualnej, a jej CSV jest pobierany zaraz po zakończeniu.
Skrypt wymaga zainstalowanego i zalogowanego [Colab CLI](https://github.com/googlecolab/google-colab-cli).

## Struktura repozytorium

```
include/sigscan/      automat (trie, DFA, skompresowany, PFAC), skan z regułą właściciela, CLI, CSV
src/common/           biblioteka sigscan_core
src/{gen,seq,omp,mpi,cuda}/   programy
tests/                testy jednostkowe (doctest) i integration.py (wszystkie wersje vs referencja)
demo/                 demo.sh (macOS/Linux) i demo.ps1 (Windows)
Dockerfile            obraz z kompilatorem, OpenMP i Open MPI (docker build / docker run)
bench/                run.py, configs/{mac,gpu}/*.json, env_info.sh, colab/ (sesja GPU)
results/raw/          surowe wyniki pomiarów (CSV, wszystkie powtórzenia) i logi serii
results/env/          konfiguracja sprzętu i oprogramowania każdej maszyny
scripts/              check_results.py, plots/ (styl wykresów, skrypt na eksperyment, make_all.py)
docs/raport.pdf       raport
docs/URUCHOMIENIE.md  instrukcja uruchomienia (Docker, macOS, Windows)
```

## Poprawność w skrócie

- **Reguła właściciela.** Fragment `[s, e)` jest skanowany w zakresie `[s, e + Lmax − 1)`, ale zgłasza tylko trafienia zaczynające się przed `e`, więc każde trafienie należy do dokładnie jednego fragmentu.
- **Tryb `count`.** Zwraca liczbę trafień i sumę kontrolną `Σ H(id)·(pos + K) mod 2^64`, która nie zależy od kolejności. Każda wersja na każdej maszynie musi dać tę samą parę.
- **Przenośny generator.** Ten sam `--seed` daje identyczne bajtowo dane na macOS, Linuksie i Windows.
