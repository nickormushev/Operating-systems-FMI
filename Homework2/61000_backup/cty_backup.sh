#!/bin/bash
set -u

if [ $# -lt 1 ]
then
    echo "Not enough arguments"
    exit 1
fi

if [ $# -gt 4 ] 
then
    echo "Too many arguments"
    exit 2
fi

if ! [ -r ${1} ]
then
    echo "Given file not readable"
    exit 3
fi

file=${1}
shift

#Checks for entries starting with =
function testForType1 {
    country=$(cat ${file} | egrep "[,[:space:]]=${1}[[:space:](\[;]")

    if [ -n "${country}" ]
    then
        echo "${country}"
        return 0
    fi

    return 1
}

function country {

    if testForType1 ${1}
    then
        return 0
    fi

    word=${1}
    fileData=$(cat ${file})

    for i in $(seq 1 ${#word}) 
    do
        fileData=$(echo "${fileData}" | egrep "[,[:space:]]$(echo "${word}" | cut -c 1-${i})")

        #Checks for exact matches of the current string so far.  
        exactMatch=$(echo "${fileData}" \
            | egrep "[,[[:space:]]$(echo "${word}" | cut -c 1-${i})[[:space:];]") 

        #If exact match exist and there is only one then that is a candidate for the result 
        #as long as a longer match does not exist. If there are two it is
        if [ -n "${exactMatch}" ] && [ $(echo "${exactMatch}" | wc -l) -eq 1 ]
        then
            res=${exactMatch}
        fi
    done

    echo "${res}" 
}

#function single_bracket_zone_num {
#    radioEntry=${1}
#    arg=${2}
#    bracket="${3}"
#
#    if [ -n "$(echo ${radioEntry} | grep "${arg}${bracket}")" ]
#    then
#        bracket="\\${3}"
#        echo "${radioEntry}" | cut -d ',' -f 10 | sed -r "s/.*${arg}${bracket}([0-9]*)${bracket}.*/\1/"
#    fi
#}

function zones {
	arg=$(echo "${1}" | tr '/' ':')
    radioEntry=$(country ${1} | tr '/' ':')

    ITU=$(echo "${radioEntry}" | cut -d ',' -f 6)
    WAZ=$(echo "${radioEntry}" | cut -d ',' -f 5)

    #If both priority WAZ and ITU entries are present
    if [ -n "$(echo ${radioEntry} | egrep "${arg}\([0-9]*\)\[")" ] 
    then
        read WAZ ITU < <(echo "${radioEntry}" | cut -d ',' -f 10 | sed -r "s/.*${arg}\(([0-9]*)\)\[([0-9]*)\].*/\1 \2/")
        echo "$ITU $WAZ"
        return 0
    fi

    #If only priority ITU entry is present
    if [ -n "$(echo ${radioEntry} | egrep "${arg}\[[0-9]*")" ]
    then
        ITU=$(echo "${radioEntry}" | cut -d ',' -f 10 | sed -r "s/.*${arg}\[([0-9]*)\].*/\1/")
        echo "$ITU $WAZ"
        return 0
    fi

    #If only priority WAZ entry is present
    if [ -n "$(echo "${radioEntry})" | egrep "${arg}\([0-9]*")" ]
    then
        WAZ=$(echo "${radioEntry}" | cut -d ',' -f 10 | sed -r "s/.*${arg}\(([0-9]*)\).*/\1/")
        echo "$ITU $WAZ"
        return 0
    fi
    
    #tempWAZ=$single_bracket_zone_num "${radioEntry}" "${arg}" "("
    
	
    echo "$ITU $WAZ"
    #ZK1NJC
    #HK3JJH/0A
}


function distance {
    read point1_lat point1_long < <(country ${1} | cut -d ',' -f 7,8 | tr ',' ' ')
    read point2_lat point2_long < <(country ${2} | cut -d ',' -f 7,8 | tr ',' ' ')

    pi=$(echo "scale=10; 4 * a(1)" | bc -l)
    rad=6371

    #Convert To Rad
    point1_lat=$(echo "${point1_lat} * ${pi}/180" | bc -l)
    point1_long=$(echo "-1 * ${point1_long} * ${pi}/180" | bc -l)

    point2_lat=$(echo "${point2_lat} * ${pi}/180" | bc -l)
    point2_long=$(echo "-1 * ${point2_long} * ${pi}/180" | bc -l)

    #Descartes coordinates
    point1_x=$(echo "${rad} * c(${point1_lat}) * c(${point1_long})" | bc -l)
    point1_y=$(echo "${rad} * c(${point1_lat}) * s(${point1_long})" | bc -l)
    point1_z=$(echo "${rad} * s(${point1_lat})" | bc -l)

    point2_x=$(echo "${rad} * c(${point2_lat}) * c(${point2_long})" | bc -l)
    point2_y=$(echo "${rad} * c(${point2_lat}) * s(${point2_long})" | bc -l)
    point2_z=$(echo "${rad} * s(${point2_lat})" | bc -l)

    distEuclid=$(echo "sqrt((${point1_x} - ${point2_x})^2 + (${point1_y} - ${point2_y})^2 + (${point1_z} - ${point2_z})^2)" | bc -l)
    height=$(echo "sqrt(${rad}^2 - (${distEuclid}/2)^2)" | bc -l)
    angle=$(echo "2 * a(${distEuclid}/(2 * ${height}))" | bc -l)

    echo "${angle} * ${rad}" | bc -l
}

case "${1}" in
    "country")
        shift
        country ${1} | cut -d ',' -f 2
        ;;
    "zones")
        shift
        zones ${1}
        ;;
    "distance")
        shift
        distance ${1} ${2}
        ;;
    *)
        echo "Wrong option"
        ;;
esac
