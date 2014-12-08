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

run 7 'int main() { int x=2; return x+5; }'
run 10 'int main() { int x=2,y=3; return x+y+5; }'
run 30 'int main() { int a=1,b=2,c=3,d=4,e=5; return a+b+c+d+e+a+b+c+d+e; }'
run 3 'int main() { char a=1; return a+2; }'
run 3 'int main() { int a; a=1; return a+2; }'

run 3 'int main() { int a[3]; a[0] = 1; return a[0]+2; }'
run 3 'int main() { int a[3]; a[0] = 1; a[1] = 2; return a[0]+a[1]; }'

# make enough number of intermediate results to cause spill and load
run 11 'int main() { int a=1; return a+(a+(a+(a+(a+(a+(a+(a+(a+(a+a))))))))); }'

echo OK
exit 0
