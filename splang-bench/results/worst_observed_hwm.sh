grep "footprint" "$1" | tr -s ' ' | cut -d ' ' -f 2 | sort -n --reverse | head -n 1
