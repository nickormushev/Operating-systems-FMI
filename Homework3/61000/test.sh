#!/bin/bash

./a.out 'mkdir' "+/test" 

for i in $(seq 1 4972) #180 for error
do
   ./a.out 'mkdir' "+/test/test${i}" 
done
exit 0
