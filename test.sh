#!/bin/bash

function fail {
    echo -n -e '\e[1;31m[ERROR]\e[0m '
    echo "$1"
    exit 1
}

function compile {
    echo "$1" | ./8cc - > tmp.s || fail "Failed to compile $1"
    gcc -o tmp.out tmp.s
     [ $? -ne 0 ] && fail "GCC failed: $1"
}

function assertequal {
    [ "$1" != "$2" ] && fail "Test failed: $2 expected but got $1"
}

function testastf {
    result="$(echo "$2" | ./8cc -a -)"
    [ $? -ne 0 ] && fail "Failed to compile $2"
    assertequal "$result" "$1"
}

function testast {
    testastf "$1" "int f(){$2}"
}

function testm {
    compile "$2"
    assertequal "$(./tmp.out)" "$1"
}

function testcpp {
    echo "$2" | ./8cc -E $3 - > tmp.s || fail "Failed to compile $1"
    assertequal "$(cat tmp.s)" "$1"
}


function testfail {
    echo "$expr" | ./8cc - > /dev/null 2>&1
    expr="int f(){$1}"
    echo "$expr" | ./8cc $OPTION - > /dev/null 2>&1
    [ $? -eq 0 ] && fail "Should fail to compile, but succeded: $expr"
}

# Parser
testast '(() -> int)f(){1;}' '1;'
testast '(() -> int)f(){1L;}' '1L;'
# testast '(() -> int)f(){1152921504606846976L;}' '1152921504606846976;'
testast '(() -> int)f(){(+ (- (+ 1 2) 3) 4);}' '1+2-3+4;'
testast '(() -> int)f(){(+ (+ 1 (* 2 3)) 4);}' '1+2*3+4;'
testast '(() -> int)f(){(+ (* 1 2) (* 3 4));}' '1*2+3*4;'
testast '(() -> int)f(){(+ (/ 4 2) (/ 6 3));}' '4/2+6/3;'
testast '(() -> int)f(){(/ (/ 24 2) 4);}' '24/2/4;'
testast '(() -> int)f(){(decl int a 3@0);}' 'int a=3;'
testast "(() -> int)f(){(decl char c 'a'@0);}" "char c='a';"
testast '(() -> int)f(){(decl *char s "abcd"@0);}' 'char *s="abcd";'
#testast "(() -> int)f(){(decl [5]char s 'a'@0 's'@1 'd'@2 'f'@3 '\0'@4);}" 'char s[5]="asdf";'
testast "(() -> int)f(){(decl [5]char s 'a'@0 's'@1 'd'@2 'f'@3 '\0'@4);}" 'char s[]="asdf";'
testast '(() -> int)f(){(decl [3]int a 1@0 2@4 3@8);}' 'int a[3]={1,2,3};'
testast '(() -> int)f(){(decl [3]int a 1@0 2@4 3@8);}' 'int a[]={1,2,3};'
testast '(() -> int)f(){(decl [3][5]int a);}' 'int a[3][5];'
testast '(() -> int)f(){(decl [5]*int a);}' 'int *a[5];'
testast '(() -> int)f(){(decl int a 1@0);(decl int b 2@0);(= a (= b 3));}' 'int a=1;int b=2;a=b=3;'
testast '(() -> int)f(){(decl int a 3@0);(addr a);}' 'int a=3;&a;'
testast '(() -> int)f(){(decl int a 3@0);(deref (addr a));}' 'int a=3;*&a;'
testast '(() -> int)f(){(decl int a 3@0);(decl *int b (addr a)@0);(deref b);}' 'int a=3;int *b=&a;*b;'
testast '(() -> int)f(){(if 1 {2;});}' 'if(1){2;}'
testast '(() -> int)f(){(if 1 {2;} {3;});}' 'if(1){2;}else{3;}'
testast '(() -> int)f(){(for (decl int a 1@0) 3 7 {5;});}' 'for(int a=1;3;7){5;}'
testast '(() -> int)f(){"abcd";}' '"abcd";'
testast "(() -> int)f(){'c';}" "'c';"
testast '(() -> int)f(){(int)a();}' 'a();'
testast '(() -> int)f(){(int)a(1,2,3,4,5,6);}' 'a(1,2,3,4,5,6);'
testast '(() -> int)f(){(return 1);}' 'return 1;'
testast '(() -> int)f(){(< 1 2);}' '1<2;'
testast '(() -> int)f(){(> 1 2);}' '1>2;'
testast '(() -> int)f(){(== 1 2);}' '1==2;'
# testast '(() -> int)f(){(deref (+ 1 2));}' '1[2];'
testast '(() -> int)f(){(decl int a 1@0);(post++ a);}' 'int a=1;a++;'
testast '(() -> int)f(){(decl int a 1@0);(post-- a);}' 'int a=1;a--;'
testast '(() -> int)f(){(! 1);}' '!1;'
testast '(() -> int)f(){(? 1 2 3);}' '1?2:3;'
testast '(() -> int)f(){(and 1 2);}' '1&&2;'
testast '(() -> int)f(){(or 1 2);}' '1||2;'
testast '(() -> int)f(){(& 1 2);}' '1&2;'
testast '(() -> int)f(){(| 1 2);}' '1|2;'
testast '(() -> int)f(){1.200000;}' '1.2;'
testast '(() -> int)f(){(+ 1.200000 (conv 1 -> double));}' '1.2+1;'

testastf '((int) -> int)f(int c){c;}' 'int f(int c){c;}'
testastf '((int) -> int)f(int c){c;}((int) -> int)g(int d){d;}' 'int f(int c){c;} int g(int d){d;}'
testastf '(decl int a 3@0)' 'int a=3;'

testastf '(decl (struct) a)' 'struct {} a;'
testastf '(decl (struct (int) (char)) a)' 'struct {int x; char y;} a;'
testastf '(decl (struct ([3]int)) a)' 'struct {int x[3];} a;'
testast '(() -> int)f(){(decl (struct (int)) a);(decl *(struct (int)) p);(deref p).x;}' 'struct tag {int x;} a; struct tag *p; p->x;'
testast '(() -> int)f(){(decl (struct (int)) a);a.x;}' 'struct {int x;} a; a.x;'

testfail '0abc;'
# testfail '1+;'
testfail '1=2;'

# & is only applicable to an lvalue
testfail '&"a";'
testfail '&1;'
testfail '&a();'

# -D command line options
testcpp ' 77' 'foo' '-Dfoo=77'

echo "All tests passed"
