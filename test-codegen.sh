#!/bin/bash

function fail {
    echo -n -e '\e[1;31m[ERROR]\e[0m '
    echo "$1"
    exit 1
}

function run {
    echo "$2" | ./8cc -S -fnewgen -o- - | cc -o a.out -x assembler -
    ./a.out
    c=$?
    [ $c -eq "$1" ] || fail "$1 expected, but got $c"
}

run 7 'int main() { int x = 2; return x + 5; }'
run 10 'int main() { int x = 2; int y = 3; return x + y + 5; }'
echo OK
exit 0
