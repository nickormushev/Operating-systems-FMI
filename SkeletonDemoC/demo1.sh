#!/bin/sh
#

a='Това е примерна програма на shell'

echo $a

b=$(date)

echo 'Сега е' $b

echo $(($1 + $2))

echo 'Име на програмата: ' $0

