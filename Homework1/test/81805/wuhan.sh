#!/bin/bash
echo $(cat spread.csv | grep $(cat city.csv | grep Wuhan | cut -d ',' -f 1) | cut -d ',' -f 4)
