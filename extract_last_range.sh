#!/bin/bash
if [ -n "$1" ]; then
  file="$1"
else
  file=$(mktemp)
  cat > "$file"
  trap "rm -f '$file'" EXIT
fi

start=$(grep -n "RANGE START" "$file" | tail -1 | cut -d: -f1)
end=$(awk -v s="$start" 'NR > s && /RANGE END/ { print NR; exit }' "$file")
sed -n "$((start+1)),$((end-1))p" "$file"
