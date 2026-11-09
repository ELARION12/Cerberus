#!/usr/bin/env bash
# Creates one virtual environment per framework under ~/bench-envs and installs it.
# Needs uv (pip install uv). Safe to re-run; existing environments are reused.
#
#   bash benchmarks/frameworks/setup_envs.sh            # all of them
#   bash benchmarks/frameworks/setup_envs.sh langchain  # just one

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENVS="${BENCH_ENVS:-$HOME/bench-envs}"
PY="${BENCH_PYTHON:-3.12}"

declare -A PACKAGES=(
  [plain]=""
  [cerberus]="$ROOT"
  [langchain]="langchain langchain-ollama langgraph"
  [llamaindex]="llama-index-core llama-index-llms-ollama"
  [smolagents]="smolagents[openai]"
)

want=("$@")
[ ${#want[@]} -eq 0 ] && want=(plain cerberus langchain llamaindex smolagents)

for name in "${want[@]}"; do
  dir="$ENVS/$name"
  echo "== $name -> $dir"
  [ -x "$dir/bin/python" ] || uv venv "$dir" --python "$PY" --managed-python -q
  if [ -n "${PACKAGES[$name]}" ]; then
    extra=""
    [ "$name" = "cerberus" ] && extra="--reinstall-package cerberus-agent"   # always rebuild from this checkout
    # shellcheck disable=SC2086
    uv pip install --python "$dir/bin/python" -q $extra ${PACKAGES[$name]}
  fi
  "$dir/bin/python" -c "import sys; print('   python', sys.version.split()[0])"
  uv pip freeze --python "$dir/bin/python" > "$dir/freeze.txt"
  echo "   $(wc -l < "$dir/freeze.txt") packages, $(du -sh "$dir" | cut -f1) on disk"
done
