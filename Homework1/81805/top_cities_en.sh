#!bin/bash
join -t ',' <(sort -t ',' -k1 city.csv) <(sort -t ',' -k1 spread.csv) \
    | awk -F ',' '$4 > 100 { printf "%s (%s): %d\n",$1, $2, $5/$4 * 1000 }' \
    | sort -n -r -t ':' -k2 | head -n 10
