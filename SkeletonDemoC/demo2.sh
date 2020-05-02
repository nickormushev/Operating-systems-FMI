#!/bin/sh


a=2
b=$a

echo a=$a b=$b

if [ $a = $b ] 
 then echo equal
# else echo differ
fi

b=$(($a+1))

echo a=$a b=$b

if [ $a = $b ] 
 then echo equal; else echo differ
fi
