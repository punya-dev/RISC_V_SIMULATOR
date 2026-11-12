#!/bin/bash
# usage: ./run_all.sh   (run from riscv_sim_base/)
pass=0; fail=0
check() { # name ref_status pipe_status
  :; }
for f in tests/*.s; do
  ./rvsim "$f" --batch    > /tmp/ref.txt 2>&1
  ./rvsim "$f" --pipeline > /tmp/pipe.txt 2>&1
  name=$(basename "$f")
  ok=1
  diff <(grep '^x' /tmp/ref.txt) <(grep '^x' /tmp/pipe.txt) >/dev/null || { echo "  REG MISMATCH: $name"; ok=0; }
  [ "$(grep '^status' /tmp/ref.txt | sed 's/ *cycles.*//')" = "$(grep '^status' /tmp/pipe.txt | sed 's/ *cycles.*//')" ] || { echo "  STATUS MISMATCH: $name"; ok=0; }
  diff <(grep -v -E '^(x|pc|status|pipeline|assembled|-|  ->|$)' /tmp/ref.txt) <(grep -v -E '^(x|pc|status|pipeline|assembled|-|  ->|$)' /tmp/pipe.txt) >/dev/null || { echo "  OUTPUT MISMATCH: $name"; ok=0; }
  line=$(grep '^pipeline:' /tmp/pipe.txt)
  c=$(sed -E 's/.*cycles=([0-9]+).*/\1/' <<<"$line"); r=$(sed -E 's/.*retired=([0-9]+).*/\1/' <<<"$line")
  s=$(sed -E 's/.*stalls=([0-9]+).*/\1/' <<<"$line"); fl=$(sed -E 's/.*flushes=([0-9]+).*/\1/' <<<"$line")
  single=$(grep '^status' /tmp/ref.txt | sed -E 's/.*cycles = ([0-9]+).*/\1/')
  st=$(grep '^status' /tmp/ref.txt | awk '{print $3}')
  [ "$r" = "$single" ] || { if [ "$st" = "ECALL_EXIT" ]; then echo "  RETIRED != SINGLE-CYCLE COUNT: $name ($r vs $single)"; ok=0; fi; }
  if [ "$st" = "ECALL_EXIT" ] && [ "$c" != "$((r+s+fl+4))" ]; then echo "  IDENTITY BROKEN: $name"; ok=0; fi
  if [ $ok = 1 ]; then pass=$((pass+1)); echo "PASS $name  cycles=$c retired=$r stalls=$s flushes=$fl"; else fail=$((fail+1)); echo "FAIL $name"; fi
done
echo "passed=$pass failed=$fail"; [ $fail = 0 ]

