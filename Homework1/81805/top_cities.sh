#!/bin/bash
cat spread.csv | awk -F ',' '$3 > 100 { printf "%s: %d\n", $1, $4/$3 * 1000 }' | sort -r -n -k2 | head -n 10
