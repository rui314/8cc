#!/bin/bash

function fail {
    echo -n -e '\e[1;31m[ERROR]\e[0m '
    echo "$1"
    exit 1
}

function run {
    out=`echo "$1" | ./8cc -S -o- -fnewgen -`
    [ $out != "$2" ] && fail "\"$2\" expected but got $out"
}

run "int x(){}" "nop"
exit 0
