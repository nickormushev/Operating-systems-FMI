cat province.csv \
    | cut -d ',' -f 1 \
    | xargs -n 1 -I currProv bash -c 'grep currProv province.csv \
    | sed '\''s/\(.*\),\(.*\)/\1 (\2):/'\'' \
    | tr '\''\n'\'' '\'' '\''; grep currProv city_province.csv \
    | cut -d '\'','\'' -f 1 \
    | xargs -n 1 -I city grep city spread.csv \
    | awk -F '\'','\'' '\''BEGIN{ deathSum=0; suspectSum=0} { deathSum+=$4; suspectSum+=$3 } END { printf "%d\n",deathSum/suspectSum*1000 }'\''' \
    | sort -n -r -k3 | head -n 10
