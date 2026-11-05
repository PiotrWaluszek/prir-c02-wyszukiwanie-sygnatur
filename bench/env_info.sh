#!/usr/bin/env bash
# Saves the hardware and software config of this machine (default: results/env/<hostname>.txt).
set -u
out=${1:-results/env/$(hostname -s).txt}
mkdir -p "$(dirname "$out")"

section() { printf '\n## %s\n' "$1"; }
try() { "$@" 2>/dev/null || echo "(unavailable: $*)"; }

{
  echo "# Environment - $(date -u '+%Y-%m-%d %H:%M UTC')"
  section "git"
  echo "${SIGSCAN_COMMIT:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}"
  git rev-parse HEAD >/dev/null 2>&1 && git status --short | head -20
  section "os"
  try uname -a
  if [[ $(uname) == Darwin ]]; then
    try sw_vers
    section "cpu"
    try sysctl -n machdep.cpu.brand_string
    echo "logical cpus:      $(sysctl -n hw.ncpu)"
    echo "performance cores: $(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null)"
    echo "efficiency cores:  $(sysctl -n hw.perflevel1.physicalcpu 2>/dev/null)"
    echo "L1d (P): $(sysctl -n hw.perflevel0.l1dcachesize 2>/dev/null)  L2 (P cluster): $(sysctl -n hw.perflevel0.l2cachesize 2>/dev/null)"
    echo "memory bytes: $(sysctl -n hw.memsize)"
    section "power"
    try pmset -g batt | head -2
    try pmset -g | grep -i lowpowermode
  else
    section "cpu"
    try lscpu | grep -v Vulnerab
    section "memory"
    try free -h
  fi
  section "compilers"
  try c++ --version | head -1
  try cmake --version | head -1
  [[ -d /opt/homebrew/opt/libomp ]] && echo "libomp: $(ls /opt/homebrew/Cellar/libomp 2>/dev/null)"
  try mpirun --version | head -1
  try nvcc --version | tail -2
  try python3 --version
  section "gpu"
  try nvidia-smi --query-gpu=name,driver_version,memory.total,clocks.max.sm,clocks.max.mem,pcie.link.gen.max,pcie.link.width.max --format=csv
} > "$out"
echo "wrote $out"
