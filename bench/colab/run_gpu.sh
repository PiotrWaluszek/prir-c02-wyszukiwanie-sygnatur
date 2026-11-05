#!/usr/bin/env bash
# GPU measurements on Colab:  bench/colab/run_gpu.sh [config ...]  (default: bench/configs/gpu/*.json)
# Uploads the committed code, builds, runs the tests, then each series; CSVs are downloaded one by one.
set -euo pipefail
cd "$(dirname "$0")/../.."
export COLAB=${COLAB:-$(command -v colab || echo ~/.colab-cli/bin/colab)}
S=${SESSION:-c02}
R=bench/colab/remote.sh
configs=("$@")
[[ ${#configs[@]} -eq 0 ]] && configs=(bench/configs/gpu/*.json)
commit=$(git rev-parse --short HEAD)
[[ -n $(git status --porcelain -- src include bench tests CMakeLists.txt) ]] && echo "warning: uncommitted changes are NOT uploaded"

if "$COLAB" status -s "$S" 2>&1 | grep -q "not found"; then
  "$COLAB" new -s "$S" --gpu T4
  SKIP_SETUP=0
fi

if [[ ${SKIP_SETUP:-0} != 1 ]]; then
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  git archive --format=tar.gz -o "$tmp/src.tgz" HEAD
  "$COLAB" upload -s "$S" "$tmp/src.tgz" /content/src.tgz
  $R "$S" <<REMOTE
set -e
mkdir -p /content/sigscan /content/results && cd /content/sigscan && tar xzf /content/src.tgz
cmake -B build -DCMAKE_CUDA_ARCHITECTURES=75 -DCMAKE_BUILD_TYPE=Release > /content/cmake.log 2>&1 || { tail -30 /content/cmake.log; exit 1; }
cmake --build build -j4 2>&1 | grep -E "error|warning: " || true
export OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
./build/tests/sigscan-tests | tail -2
python3 tests/integration.py --bin build --work /content/itest | tail -1 | tee /content/itest.txt
grep -q "ALL OK" /content/itest.txt
SIGSCAN_COMMIT=$commit bash bench/env_info.sh /content/env.txt
REMOTE
  "$COLAB" download -s "$S" /content/env.txt results/env/colab-t4.txt
fi

for cfg in "${configs[@]}"; do
  name=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["name"])' "$cfg")
  for attempt in 1 2 3; do
    REMOTE_TIMEOUT=120 $R "$S" <<REMOTE >/dev/null 2>&1 || true
cd /content/sigscan
# a previous attempt may have started it already
[ -s /content/results/$name.log ] && [ ! -f /content/results/$name.done ] && exit 0
rm -f /content/results/$name.done /content/results/$name.log
setsid nohup bash -c 'python3 bench/run.py --resume --cooldown 0.5 --out /content/results $cfg > /content/results/$name.log 2>&1; echo \$? > /content/results/$name.done' >/dev/null 2>&1 &
REMOTE
    sleep 5
    REMOTE_TIMEOUT=120 $R "$S" <<< "test -s /content/results/$name.log" >/dev/null 2>&1 && break
    echo "[$name] start attempt $attempt failed"
    [[ $attempt == 3 ]] && { echo "[$name] FAILED to start"; exit 1; }
  done
  echo "[$name] started"
  while :; do
    sleep "${POLL:-45}"
    st=$(REMOTE_TIMEOUT=180 $R "$S" <<REMOTE 2>/dev/null | grep -a '^STATE' || true
echo "STATE \$(cat /content/results/$name.done 2>/dev/null || echo running) \$(grep -a '^  \[' /content/results/$name.log | tail -1)"
REMOTE
)
    echo "  $st"
    case $st in
      "STATE 0"*) break ;;
      "STATE running"*|"") continue ;;
      *) echo "[$name] FAILED"; $R "$S" <<< "tail -20 /content/results/$name.log"; exit 1 ;;
    esac
  done
  for f in "$name.csv" "${name}_stats.csv"; do
    "$COLAB" download -s "$S" "/content/results/$f" "results/raw/$f" 2>/dev/null || true
  done
  echo "[$name] downloaded"
done
[[ ${KEEP:-0} == 1 ]] || "$COLAB" stop -s "$S"
