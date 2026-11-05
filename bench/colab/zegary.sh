#!/usr/bin/env bash
# T4 clocks: kernel time vs text size with nvidia-smi samples, then a smaller grid and a locked clock.
# Run on Colab after building:  bench/colab/remote.sh c02 < bench/colab/zegary.sh  (output in /content/zegary)
set -e
cd /content/sigscan
B=build O=/content/zegary
mkdir -p $O /content/data
for s in 256M 512M 1G 2G; do
  [ -f /content/data/b-$s.bin ] || $B/sigscan-gen --out /content/data/b-$s --size $s --npat 1000 --minlen 8 \
    --maxlen 32 --hit-rate 10 --partial-rate 0 --prefix-share 0.3 --alphabet full --seed 2026 > /dev/null
done

# 1) kernel time per size, nvidia-smi in the background
Q=timestamp,clocks.sm,clocks.mem,power.draw,temperature.gpu,utilization.gpu,clocks_event_reasons.active
nvidia-smi --query-gpu=$Q --format=csv,noheader,nounits -lms 20 > $O/nvidia_smi.csv &
SMI=$!
sleep 3
rm -f $O/znaczniki_czasu.txt $O/kernel.csv
for s in 256M 512M 1G 2G; do
  echo "$s start $(date +%s.%N)" >> $O/znaczniki_czasu.txt
  $B/sigscan-cuda --text /content/data/b-$s.bin --patterns /content/data/b-$s.pat --kernel smem-root --block 256 \
    --reps 7 --warmup 1 --csv --csv-header --quiet >> $O/kernel.csv
  echo "$s end $(date +%s.%N)" >> $O/znaczniki_czasu.txt
  sleep 5
done
kill $SMI

# 2) default grid vs resident blocks only, then a locked clock
run() {
  $B/sigscan-cuda --text /content/data/b-$1.bin --patterns /content/data/b-$1.pat --kernel smem-root --block 256 \
    --reps 7 --warmup 1 --csv --quiet "${@:2}" |
    awk -F, -v s=$1 -v tag="$TAG" '$1=="cuda" && $16>=0 {print tag, s, $20}' | sort -k4 -n |
    awk '{a[NR]=$0} END {print a[int((NR+1)/2)]}'
}
{
  for gm in 32 4; do for s in 256M 2G; do TAG="unlocked gm=$gm" run $s --grid-mult $gm; done; done
  if nvidia-smi -lgc 1005,1005 > /dev/null 2>&1; then
    for s in 256M 512M 1G 2G; do TAG="locked1005 gm=32" run $s; done
    TAG="locked1005 gm=4" run 2G --grid-mult 4
    nvidia-smi -rgc > /dev/null 2>&1
  else
    echo "nvidia-smi -lgc not allowed on this VM"
  fi
} | tee $O/siatka_i_zablokowany_zegar.txt
