cat province.csv \
    | cut -d ',' -f 1 \
    | xargs -n 1 -I currProv bash -c 'echo -n "currProv: "; grep currProv city_province.csv \
    | cut -d ',' -f 1 \
    | xargs -n 1 -I city grep city spread.csv \
    | awk -F ',' '\''BEGIN{ deathSum=0; suspectSum=0} { deathSum+=$4; suspectSum+=$3 } END { printf "%d\n",deathSum/suspectSum*1000 }'\''' \
    | sort -n -k2 -r | head -n 10
