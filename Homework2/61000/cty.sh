#!/bin/bash

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

function search {

    #if testForType1 ${1}
    #then
    #    return 0
    #fi

    word=${1}
    fileData=$(cat ${file})

    for i in $(seq 1 ${#word}) 
    do
        currWord=$(echo "${word}" | cut -c 1-${i})
        fileData=$(echo "${fileData}" | egrep "${currWord}")

        #Checks for exact matches of the current string so far.  
        exactMatch=$(echo "${fileData}" \
            | egrep "[,=[[:space:]]${currWord}[[:space:],(\[;]") 

        #If exact match exist and there is only one then that is a candidate for the result 
        #as long as a longer match does not exist. If there are two it is
        if [ -n "${exactMatch}" ] && [ $(echo "${exactMatch}" | wc -l) -eq 1 ]
        then
            res=${exactMatch}
        fi
    done
    

    if ! [ -n "${res}" ]
    then
        return 1
    fi

    echo "${res}" 
}

function isFound {
    if [ "${1}" = "Not found" ]
    then
        echo "Not found"
        exit 4
    fi
}

function searchCountryRow {
    #Really ugly but does the job. Replaces [ and ] with \[ and \]
    res=$(echo "${1}" | sed -r "s%([][])%\\\\\1%g")

    #Prints all the lines from ${radioEntry} until it finds a line with :.
    #Tac reverses the file so the first row with a : sign is the one that has the country information
    cat "${file}" | tac | sed -n "\%${res}%,\%:% p" | grep ":"
}

function country {
    resCountry=$(search "${1}")
    isFound "${resCountry}"
    delimiter=","
    col=2

    if [ ! -z ${CTY_FORMAT} ] && [ ${CTY_FORMAT} = "DAT" ]
    then
        resCountry=$(searchCountryRow ${resCountry})
        delimiter=":"
        col=1
    else
        #Check to see if it is an exact match by seing if there is an = before the country
        #If that is not the case I want to see if we have a foreign country so I fgrep for a \
        if [ ! -n "$(echo ${resCountry} | grep "=${1}")" ] && echo "${1}" | fgrep '/' &>/dev/null
        then
            #I reverse the data and take the last column for the foreign country in case I have data like this R9JBN/4/M
            #where R9JBN/4 is an exact match and M is the foreign country
            foreignCountryID=$(echo "${1}" | rev | cut -d '/' -f 1 | rev)
            foreignCountry=$(search "${foreignCountryID}" | cut -d ',' -f 2)
            echo "$(echo "${resCountry}" | cut -d ',' -f 2) (${foreignCountry})"
            return 0
        fi
    fi

    echo "$(echo "${resCountry}" | cut -d "${delimiter}" -f "${col}")"
}

function zones {

    radioEntry=$(search ${1})
    arg=${1}
    isFound "${radioEntry}"

    if [ ! -z ${CTY_FORMAT} ] && [ ${CTY_FORMAT} = "DAT" ]
    then
        options="${radioEntry}"
        radioEntry=$(searchCountryRow ${radioEntry})

        ITU=$(echo "${radioEntry}" | cut -d ':' -f 3 | tr -d '[:space:]')
        WAZ=$(echo "${radioEntry}" | cut -d ':' -f 2 | tr -d '[:space:]')
    else
        options=$(echo "${radioEntry}" | cut -d ',' -f 10)
        ITU=$(echo "${radioEntry}" | cut -d ',' -f 6)
        WAZ=$(echo "${radioEntry}" | cut -d ',' -f 5)
    fi

    #If both priority WAZ and ITU entries are present
    if [ -n "$(echo ${options} | egrep "${arg}\([0-9]*\)\[")" ] 
    then
        read WAZ ITU < <(echo "${options}" | sed -r "s%.*${arg}\(([0-9]*)\)\[([0-9]*)\].*%\1 \2%")
        echo "$ITU $WAZ"
        return 0
    fi

    #If only priority ITU entry is present
    if [ -n "$(echo ${options} | egrep "${arg}\[[0-9]*")" ]
    then
        ITU=$(echo "${options}" | sed -r "s%.*${arg}\[([0-9]*)\].*%\1%")
        echo "$ITU $WAZ"
        return 0
    fi

    #If only priority WAZ entry is present
    if [ -n "$(echo "${options})" | egrep "${arg}\([0-9]*")" ]
    then
        WAZ=$(echo "${options}" | sed -r "s%.*${arg}\(([0-9]*)\).*%\1%")
        echo "$ITU $WAZ"
        return 0
    fi
	
    echo "$ITU $WAZ"
    #ZK1NJC
    #HK3JJH/0A
}

function deg_to_rad {
    pi=$(echo "scale=10; 4 * a(1)" | bc -l)

    point_lat=$(echo "${1} * ${pi}/180" | bc -l)
    point_long=$(echo "${2} * ${pi}/180" | bc -l)
    
    echo "${point_lat} ${point_long}"
}

function descartes_coords {
    rad=6371
    point_lat=${1}
    point_long=${2}

    point_x=$(echo "${rad} * c(${point_lat}) * c(${point_long})" | bc -l)
    point_y=$(echo "${rad} * c(${point_lat}) * s(${point_long})" | bc -l)
    point_z=$(echo "${rad} * s(${point_lat})" | bc -l)

    echo "${point_x} ${point_y} ${point_z}"
}

function distance {
    country1IdLine=$(search ${1})
    country2IdLine=$(search ${2})

    isFound ${country1IdLine}
    isFound ${country2IdLine}

    if [ ! -z ${CTY_FORMAT} ] && [ ${CTY_FORMAT} = "DAT" ]
    then
        point1CountryLine=$(searchCountryRow ${country1IdLine})
        point2CountryLine=$(searchCountryRow ${country2IdLine})
        isFound ${point1CountryLine}
        isFound ${point2CountryLine}

        read point1_lat point1_long < <(echo ${point1CountryLine} | cut -d ':' -f 5,6 | tr ':' ' ')
        read point2_lat point2_long < <(echo ${point2CountryLine} | cut -d ':' -f 5,6 | tr ':' ' ')
    else
        read point1_lat point1_long < <(echo ${country1IdLine} | cut -d ',' -f 7,8 | tr ',' ' ')
        read point2_lat point2_long < <(echo ${country2IdLine} | cut -d ',' -f 7,8 | tr ',' ' ')
    fi

    rad=6371

    #Convert To Rad
    read point1_lat point1_long < <(deg_to_rad ${point1_lat} ${point1_long})
    read point2_lat point2_long < <(deg_to_rad ${point2_lat} ${point2_long})

    #Conversion to descartes coordinates
    read point1_x point1_y point1_z < <(descartes_coords ${point1_lat} ${point1_long})
    read point2_x point2_y point2_z < <(descartes_coords ${point2_lat} ${point2_long})

    #Uses the triangle to calculate the angle
    distEuclid=$(echo "sqrt((${point1_x} - ${point2_x})^2 + (${point1_y} - ${point2_y})^2 + (${point1_z} - ${point2_z})^2)" | bc -l)
    height=$(echo "sqrt(${rad}^2 - (${distEuclid}/2)^2)" | bc -l)
    angle=$(echo "2 * a(${distEuclid}/(2 * ${height}))" | bc -l)

    #Calculates the distance between the points
    res=$(echo "${angle} * ${rad}" | bc -l)
    
    #Rounds the answer up or down
    if [ $(echo ${res} | cut -d '.' -f 2 | cut -c 1) -ge 5 ]
    then
        echo "$(echo "${res}" | cut -d '.' -f 1) + 1" | bc -l
        return 0
    fi

    echo "${res}" | cut -d '.' -f 1
}

case "${1}" in
    "country")
        shift
        country ${1}
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
