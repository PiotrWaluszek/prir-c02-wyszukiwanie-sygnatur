#!/usr/bin/env bash
# Runs a bash script from stdin on Colab:  bench/colab/remote.sh SESSION < script.sh
# Output is relayed line by line (plain subprocess output does not work in the Jupyter kernel).
# Hanging calls are killed after REMOTE_TIMEOUT s (perl alarm, macOS has no `timeout`).
set -euo pipefail
session=$1
colab=${COLAB:-colab}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/job.sh"
python3 - "$tmp/job.sh" "$tmp/job.py" <<'PY'
import json, sys
script = open(sys.argv[1]).read()
open(sys.argv[2], "w").write(f"""import subprocess
p = subprocess.Popen(["bash", "-c", {json.dumps(script)}], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                     text=True, bufsize=1)
for line in p.stdout:
    print(line, end="", flush=True)
rc = p.wait()
print("[remote exit]", rc, flush=True)
""")
PY
perl -e 'alarm shift; exec @ARGV' "${REMOTE_TIMEOUT:-900}" "$colab" exec -s "$session" -f "$tmp/job.py" 2>&1 | tee "$tmp/out" || true
rc=$(grep -a '^\[remote exit\]' "$tmp/out" | tail -1 | awk '{print $3}')
exit "${rc:-1}"
