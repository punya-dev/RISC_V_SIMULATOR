#!/usr/bin/env bash
# Build the simulator and check every lab's tests. Run inside WSL: ./run.sh   From Windows: wsl bash run.sh
set -u
cd "$(dirname "$0")/code/riscv_sim_base" || exit 1

declare -A EXPECT=(
  ["micro.s --pipeline"]="15 6 0 0 0 0"
  ["micro.s --pipeline-forward"]="9 0 0 2 1 0"
  ["sample1_sum_loop.s --pipeline"]="108 40 11 0 0 0"
  ["sample1_sum_loop.s --pipeline-forward"]="68 0 11 13 11 0"
  ["sample2_mem_and_branches.s --pipeline"]="26 8 1 0 0 0"
  ["sample2_mem_and_branches.s --pipeline-forward"]="18 0 1 2 2 0"
  ["sample4_data_hazards.s --pipeline"]="69 35 1 0 0 0"
  ["sample4_data_hazards.s --pipeline-forward"]="36 2 1 9 7 2"
)

make -s clean >/dev/null
warn=$(make 2>&1 | grep -ci warning)
echo "build warnings: $warn"
[ -x rvsim ] || { echo "RESULT: FAIL (build)"; exit 1; }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
field() { sed -n "s/.* $1=\([0-9]*\).*/\1/p" "$2"; }
fail=0
for f in tests/*.s; do
  ./rvsim "$f" --batch > "$tmp/ref" 2>&1
  for m in --pipeline --pipeline-forward; do
    ./rvsim "$f" $m > "$tmp/got" 2>&1
    ok=1
    diff <(grep '^x' "$tmp/ref") <(grep '^x' "$tmp/got") >/dev/null || ok=0
    diff <(grep -vE '^(x|pc =|status|pipeline)' "$tmp/ref") <(grep -vE '^(x|pc =|status|pipeline)' "$tmp/got") >/dev/null || ok=0
    [ "$(grep -o 'status = [A-Z_]*' "$tmp/ref")" = "$(grep -o 'status = [A-Z_]*' "$tmp/got")" ] || ok=0
    c=$(field cycles "$tmp/got"); rt=$(field retired "$tmp/got"); s=$(field stalls "$tmp/got"); fl=$(field flushes "$tmp/got")
    [ "$c" = "$((rt + s + fl + 4))" ] || ok=0
    [ "$rt" = "$(sed -n 's/.*cycles = \([0-9]*\).*/\1/p' "$tmp/ref")" ] || ok=0
    got="$c $s $fl $(field fwd_ex "$tmp/got") $(field fwd_mem "$tmp/got") $(field load_use "$tmp/got")"
    key="$(basename "$f") $m"
    [ -z "${EXPECT[$key]:-}" ] || [ "${EXPECT[$key]}" = "$got" ] || ok=0
    [ $ok = 1 ] && r=PASS || { r=FAIL; fail=1; }
    printf '%-4s %-30s %-18s cyc/stall/flush/fwd_ex/fwd_mem/lu=%s  expect=%s\n' "$r" "$(basename "$f")" "$m" "$got" "${EXPECT[$key]:--}"
  done
done
[ "$warn" = 0 ] || fail=1
echo "RESULT: $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
