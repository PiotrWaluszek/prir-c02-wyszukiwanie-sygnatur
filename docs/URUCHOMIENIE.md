# Uruchomienie i sprawdzenie projektu (Docker, macOS albo Windows)

Instrukcja opisuje, jak zbudować projekt i sprawdzić jego działanie na własnym komputerze. Karta
graficzna nie jest potrzebna: wersje CPU (sekwencyjna, OpenMP, MPI) działają na każdym komputerze,
a wersja CUDA jest budowana tylko wtedy, gdy zainstalowany jest CUDA Toolkit.

Instalacja narzędzi zajmuje około 15 minut, samo demo trwa około minuty.

Demo wykonuje cztery kroki:
1. generuje 128 MiB losowych danych i 1000 wzorców, wstawiając trafienia w znane miejsca;
2. przeszukuje dane wersją sekwencyjną, OpenMP i MPI;
3. sprawdza, czy każda wersja dała tę samą liczbę trafień i tę samą sumę kontrolną oraz czy znaleziono
   każde trafienie wstawione przez generator;
4. wypisuje czasy, przepustowość i przyspieszenie, a na końcu `WYNIK: OK` albo `WYNIK: BŁĄD`.

Są dwie drogi:
- [**Docker**](#najprościej-docker-windows-macos-linux): jedno narzędzie do zainstalowania i dwa polecenia,
  tak samo na Windows, macOS i Linuksie. Najmniej pracy.
- **Budowanie bezpośrednio na systemie**: [macOS](#macos) albo [Windows](#windows). Wymaga kompilatora,
  CMake i MPI, ale programy działają wtedy natywnie na danym systemie.

---

## Najprościej: Docker (Windows, macOS, Linux)

Obraz zawiera system Ubuntu 24.04 z kompilatorem g++, CMake, OpenMP i Open MPI, więc niczego poza
Dockerem nie trzeba instalować.

1. Należy zainstalować **Docker Desktop**: https://www.docker.com/products/docker-desktop/
   Na Windows instalator sam włącza WSL 2 i może poprosić o ponowne uruchomienie komputera. Po instalacji
   trzeba uruchomić Docker Desktop i poczekać, aż pokaże, że działa.
2. Kod należy rozpakować z otrzymanego archiwum ZIP albo pobrać z GitHuba (**Code → Download ZIP**),
   a potem otworzyć terminal (na Windows zwykły PowerShell) w katalogu projektu.
3. Budowanie obrazu (pierwsze trwa kilka minut, bo pobiera system i kompilator):
   ```
   docker build -t c02 .
   ```
4. Demo:
   ```
   docker run --rm c02
   ```
5. Testy (opcjonalnie):
   ```
   docker run --rm c02 ctest --test-dir build --output-on-failure
   ```

Wynik demo jest taki sam jak przy budowaniu bezpośrednio na systemie (opis niżej, w sekcji
„Wynik demo”). Kontener korzysta ze wszystkich rdzeni, które Docker Desktop udostępnia maszynie
wirtualnej, więc czasy mogą być trochę inne niż poza Dockerem.

---

## macOS

Projekt działa na Macach z procesorem Apple Silicon (M1–M4) i Intel.

### 1. Narzędzia (jednorazowo)

Polecenia należy wpisać w aplikacji **Terminal**.

1. Narzędzia kompilatora Apple:
   ```bash
   xcode-select --install
   ```
   Komunikat, że narzędzia są już zainstalowane, oznacza, że ten krok można pominąć.
2. Homebrew, jeśli nie jest zainstalowany (czyli gdy polecenie `brew --version` nie działa):
   instrukcja na https://brew.sh
3. CMake, OpenMP i MPI:
   ```bash
   brew install cmake libomp open-mpi
   ```
   Apple clang nie zawiera OpenMP, dlatego potrzebny jest pakiet `libomp`. CMake wykrywa go automatycznie.

### 2. Kod

Kod można rozpakować z otrzymanego archiwum ZIP, pobrać z GitHuba (**Code → Download ZIP**) albo poleceniem:
```bash
git clone https://github.com/PiotrWaluszek/prir-c02-wyszukiwanie-sygnatur.git
cd prir-c02-wyszukiwanie-sygnatur
```

### 3. Budowanie

W katalogu projektu:
```bash
cmake -B build
cmake --build build -j
```
Pierwsze `cmake -B build` pobiera z internetu bibliotekę testową doctest. Na końcu konfiguracji pojawia się
linia `sigscan: OpenMP=TRUE MPI=TRUE CUDA=NOTFOUND`; brak CUDA na Macu jest normalny.

### 4. Demo

```bash
bash demo/demo.sh
```
Rozmiar danych i liczbę wątków można podać jako argumenty, np. `bash demo/demo.sh 512M 8`.

### 5. Testy (opcjonalnie)

```bash
ctest --test-dir build --output-on-failure
```
Uruchamiane są testy jednostkowe oraz test integracyjny, który porównuje z wersją sekwencyjną około 30
wariantów (różne liczby wątków i procesów, harmonogramy, układy automatu) na 6 rodzajach danych.
Trwa to około pół minuty.

---

## Windows

Projekt działa na 64-bitowym Windows 10 i 11 z kompilatorem Visual Studio (MSVC).

### 1. Narzędzia (jednorazowo)

1. **Visual Studio 2022 lub nowsze** (np. 2026): wystarczy darmowa wersja Community albo same „Build Tools”,
   do pobrania z https://visualstudio.microsoft.com/pl/downloads/. W instalatorze należy wybrać pakiet
   **„Programowanie aplikacji klasycznych w języku C++”** (ang. *Desktop development with C++*), który
   zawiera kompilator z OpenMP oraz CMake.
2. **MS-MPI**, potrzebne tylko do wersji MPI (bez niego demo pomija MPI):
   https://learn.microsoft.com/en-us/message-passing-interface/microsoft-mpi.
   Wymagane są oba pliki ze strony pobierania: `msmpisetup.exe` (środowisko uruchomieniowe z `mpiexec`)
   oraz `msmpisdk.msi` (pliki nagłówkowe i biblioteki dla kompilatora).
3. **Python 3**, opcjonalnie, tylko do testu integracyjnego: https://www.python.org/downloads/
   (z opcją „Add python.exe to PATH”).

Po instalacji trzeba otworzyć terminal na nowo, aby widział nowe programy.

### 2. Kod

Kod można rozpakować z otrzymanego archiwum ZIP, pobrać z GitHuba (**Code → Download ZIP**) albo przez
`git clone`, jak wyżej.

### 3. Budowanie

Budowanie odbywa się w terminalu **„Developer PowerShell for VS”** z numerem zainstalowanej wersji, np.
„Developer PowerShell for VS 2022” albo „for VS 2026” (dostępnym w menu Start; w zwykłym
PowerShellu polecenie `cmake` może nie być widoczne). W katalogu projektu:
```powershell
cmake -B build
cmake --build build --config Release
```
Na końcu konfiguracji powinna pojawić się linia `sigscan: OpenMP=TRUE MPI=TRUE CUDA=NOTFOUND`. Wartość
`MPI=FALSE` oznacza, że brakuje SDK MS-MPI albo terminal był otwarty jeszcze przed instalacją.

Programy są zapisywane w `build\Release\` (`sigscan-gen.exe`, `sigscan-seq.exe`, `sigscan-omp.exe`, `sigscan-mpi.exe`).

### 4. Demo

```powershell
powershell -ExecutionPolicy Bypass -File demo\demo.ps1
```
Opcja `-ExecutionPolicy Bypass` jest potrzebna, ponieważ Windows domyślnie blokuje skrypty `.ps1`.
Rozmiar danych i liczbę wątków można zmienić, np. `-Size 512M -Threads 8`.

### 5. Testy (opcjonalnie)

```powershell
ctest --test-dir build -C Release --output-on-failure
```

---

## Wynik demo

Przykładowy wynik z MacBooka Pro M1 Pro (10 rdzeni), 128 MiB danych:
```
== 1. Generator danych: 128M losowych bajtów, 1000 wzorców 8-32 B, ziarno 2026
   wstawione trafienia (znane pozycje): 1276

== 2. Wyszukiwanie (mediana z 3 powtórzeń)

   skan [ms]    MB/s  przysp. trafienia  suma kontrolna    wersja
       658.1     204    1.00x      1276  866ccea0c9671955  sekwencyjna (Aho-Corasick)
       526.3     255    1.25x      1276  866ccea0c9671955  sekwencyjna, automat skompresowany
       108.5    1237    6.07x      1276  866ccea0c9671955  sekwencyjna, przeplot K=8 (1 rdzeń)
        87.3    1537    7.54x      1276  866ccea0c9671955  OpenMP, 10 wątków, bloki
        77.5    1731    8.49x      1276  866ccea0c9671955  OpenMP, 10 wątków, dynamic 1M
        75.7    1773    8.70x      1276  866ccea0c9671955  OpenMP, 10 wątków, dynamic 4 KiB
        14.2    9461   46.39x      1276  866ccea0c9671955  OpenMP, 10 wątków, przeplot K=8
       164.7     815    4.00x      1276  866ccea0c9671955  MPI, 4 procesy
   przysp. = ile razy szybciej niż wersja sekwencyjna; kawałki 4 KiB to ponad 30 tys. granic,
   na których reguła właściciela nie może zgubić ani zdublować trafienia

== 3. Skalowanie OpenMP, jak na rys. 8 (dynamic 1M): przyspieszenie S(p) i efektywność E(p) = S(p)/p

     wątki skan [ms]     S(p)   E(p)
        1     640.4     1.03   1.03
        2     333.7     1.97   0.99
        4     165.8     3.97   0.99
        8      86.8     7.59   0.95
       10      76.8     8.57   0.86

== 4. Skalowanie MPI na jednym komputerze, jak na rys. 9

      procesy skan [ms]     GB/s automat MiB
            1     647.1     0.21       19.5
            2     337.5     0.40       19.5
            4     172.7     0.78       19.5
           10      88.7     1.51       19.5

== 5. Przeplot K niezależnych przejść automatu w jednym wątku, jak na rys. 10

            K skan [ms]     GB/s automat MiB
            1     638.6     0.21       19.5
            2     332.1     0.40       19.5
            4     174.5     0.77       19.5
            8     109.6     1.22       19.5
           16     239.1     0.56       19.5
   na M1 Pro (rys. 10) maksimum wypada przy K=8; liczby zależą od procesora i kompilatora

== 6. Krzywa U: czas OpenMP (10 wątków, dynamic) w funkcji rozmiaru kawałka, jak na rys. 15

      kawałek skan [ms]     GB/s automat MiB
           4k      76.5     1.75       19.5
          64k      74.3     1.81       19.5
           1M      76.8     1.75       19.5
           8M      88.3     1.52       19.5
          32M     165.2     0.81       19.5
   przy dużych kawałkach (32M to tylko 4 kawałki) część wątków nie dostaje pracy i czas rośnie

== 7. Liczba wzorców: rozmiar automatu i przepustowość referencji, jak na rys. 4 (64 MiB tekstu)

       wzorce skan [ms]     GB/s automat MiB
           10     147.7     0.45        0.2
          100     152.1     0.44        2.0
         1000     315.8     0.21       19.5
        10000     432.4     0.16      191.8
   im większy automat, tym mniej mieści się w pamięci podręcznej i tym wolniejszy skan

== 8. Czy znaleziono każde trafienie wstawione przez generator (lista par wzorzec, pozycja)?
   brakujących: 0 z 1276

WYNIK: OK - wszystkie wersje znalazły te same 1276 trafień z tą samą sumą kontrolną,
       w tym wszystkie 1276 wstawione przez generator.
```

- **skan [ms]**: czas samego przeszukiwania, bez wczytania pliku i budowy automatu (mediana z 3 powtórzeń).
- **przysp.**: przyspieszenie względem wersji sekwencyjnej; zależy od liczby rdzeni komputera.
- **przeplot K=8**: jeden wątek prowadzi osiem niezależnych przejść przez automat naraz, co daje
  kilkukrotne przyspieszenie już na jednym rdzeniu (sekcja 7.6 sprawozdania).
- **dynamic 4 KiB**: bardzo małe kawałki, czyli dziesiątki tysięcy granic między nimi; wynik musi być
  taki sam jak dla dużych kawałków.
- **sekcje 3–7**: krótkie wersje eksperymentów ze sprawozdania (skalowanie OpenMP i MPI, przeplot,
  krzywa U, liczba wzorców); przy każdej podany jest numer rysunku, z którym można je porównać.
- **trafienia i suma kontrolna**: muszą być identyczne w każdym wierszu. Suma kontrolna nie zależy od
  kolejności trafień, więc nie zmienia się przy innym podziale danych między wątki i procesy.
- **Wynik niezależny od komputera**: generator jest deterministyczny, więc dla domyślnych ustawień (128M,
  ziarno 2026) na każdym systemie wychodzi 1276 trafień i suma `866ccea0c9671955`. Różnią się tylko czasy.

## Typowe problemy

| Objaw | Przyczyna i rozwiązanie |
|---|---|
| macOS: `OpenMP=FALSE` | brak `libomp`; należy wykonać `brew install libomp`, usunąć katalog `build` (`rm -rf build`) i zbudować projekt ponownie |
| macOS: MPI zgłasza błąd ścieżki gniazda | Open MPI wymaga krótkiej ścieżki tymczasowej; `demo.sh` ustawia ją sam, przy ręcznym uruchomieniu wystarczy `export TMPDIR=/tmp` |
| Windows: `cmake` nie jest rozpoznawane | polecenia trzeba wpisywać w „Developer PowerShell for VS”, a nie w zwykłym PowerShellu; można też użyć Dockera |
| Windows: `MPI=FALSE` | należy doinstalować `msmpisdk.msi`, otworzyć terminal na nowo, usunąć katalog `build` (`Remove-Item -Recurse build`) i zbudować projekt ponownie |
| Windows: „uruchamianie skryptów jest wyłączone” | skrypt trzeba uruchomić z opcją `-ExecutionPolicy Bypass`, jak w punkcie 4 |
| Windows: pierwsze uruchomienie jest wolne | Windows Defender skanuje nowe pliki `.exe`; kolejne uruchomienia działają normalnie |
| brak internetu przy `cmake -B build` | testy wymagają pobrania doctest; same programy można zbudować z opcją `-DSIGSCAN_BUILD_TESTS=OFF` |

## GPU (opcjonalnie)

Wersja CUDA (`sigscan-cuda`) jest budowana automatycznie, jeśli CMake znajdzie CUDA Toolkit. Była
sprawdzana tylko na Linuksie (Google Colab, Tesla T4), skąd pochodzą wyniki GPU w raporcie. Sposób ich
odtworzenia opisuje sekcja „Odtworzenie pomiarów” w `README.md`.
