grep "time" "$1" | tr -s ' ' | cut -d ' ' -f 2 | sed 's/..$//' | awk '{sum += $1; count++} END {if (count > 0) print sum / count}'
