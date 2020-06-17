#!/bin/bash

function test {
    echo $#
    for i in $@ 
    do
        echo ${i}
    done
}

test $@

set $(ls ./)

shift 2

test $@

exit 0
