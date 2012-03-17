#!/bin/bash

function compile {
    echo "$1" | ./8cc > tmp.s || echo "Failed to compile $1"
    if [ $? -ne 0 ]; then
        echo "Failed to compile $1"
        exit
    fi
    gcc -o tmp.out tmp.s
    if [ $? -ne 0 ]; then
        echo "GCC failed: $1"
        exit
    fi
}

function assertequal {
    if [ "$1" != "$2" ]; then
        echo "Test failed: $2 expected but got $1"
        exit
    fi
}

function testastf {
    result="$(echo "$2" | ./8cc -a)"
    if [ $? -ne 0 ]; then
        echo "Failed to compile $1"
        exit
    fi
    assertequal "$result" "$1"
}

function testast {
    testastf "$1" "int f(){$2}"
}

function testm {
    compile "$2"
    assertequal "$(./tmp.out)" "$1"
}

function testfail {
    expr="int f(){$1}"
    echo "$expr" | ./8cc > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "Should fail to compile, but succeded: $expr"
        exit
    fi
}

# Parser
testast '(int)f(){1;}' '1;'
testast '(int)f(){(+ (- (+ 1 2) 3) 4);}' '1+2-3+4;'
testast '(int)f(){(+ (+ 1 (* 2 3)) 4);}' '1+2*3+4;'
testast '(int)f(){(+ (* 1 2) (* 3 4));}' '1*2+3*4;'
testast '(int)f(){(+ (/ 4 2) (/ 6 3));}' '4/2+6/3;'
testast '(int)f(){(/ (/ 24 2) 4);}' '24/2/4;'
testast '(int)f(){(decl int a 3);}' 'int a=3;'
testast "(int)f(){(decl char c 'a');}" "char c='a';"
testast '(int)f(){(decl *char s "abcd");}' 'char *s="abcd";'
testast '(int)f(){(decl [5]char s "asdf");}' 'char s[5]="asdf";'
testast '(int)f(){(decl [5]char s "asdf");}' 'char s[]="asdf";'
testast '(int)f(){(decl [3]int a {1,2,3});}' 'int a[3]={1,2,3};'
testast '(int)f(){(decl [3]int a {1,2,3});}' 'int a[]={1,2,3};'
testast '(int)f(){(decl [3][5]int a);}' 'int a[3][5];'
testast '(int)f(){(decl [5]*int a);}' 'int *a[5];'
testast '(int)f(){(decl int a 1);(decl int b 2);(= a (= b 3));}' 'int a=1;int b=2;a=b=3;'
testast '(int)f(){(decl int a 3);(addr a);}' 'int a=3;&a;'
testast '(int)f(){(decl int a 3);(deref (addr a));}' 'int a=3;*&a;'
testast '(int)f(){(decl int a 3);(decl *int b (addr a));(deref b);}' 'int a=3;int *b=&a;*b;'
testast '(int)f(){(if 1 {2;});}' 'if(1){2;}'
testast '(int)f(){(if 1 {2;} {3;});}' 'if(1){2;}else{3;}'
testast '(int)f(){(for (decl int a 1) 3 7 {5;});}' 'for(int a=1;3;7){5;}'
testast '(int)f(){"abcd";}' '"abcd";'
testast "(int)f(){'c';}" "'c';"
testast '(int)f(){(int)a();}' 'a();'
testast '(int)f(){(int)a(1,2,3,4,5,6);}' 'a(1,2,3,4,5,6);'
testast '(int)f(){(return 1);}' 'return 1;'
testast '(int)f(){(< 1 2);}' '1<2;'
testast '(int)f(){(> 1 2);}' '1>2;'
testast '(int)f(){(== 1 2);}' '1==2;'
testast '(int)f(){(deref (+ 1 2));}' '1[2];'
testast '(int)f(){(decl int a 1);(++ a);}' 'int a=1;a++;'
testast '(int)f(){(decl int a 1);(-- a);}' 'int a=1;a--;'
testast '(int)f(){(! 1);}' '!1;'
testast '(int)f(){(? 1 2 3);}' '1?2:3;'
testast '(int)f(){(and 1 2);}' '1&&2;'
testast '(int)f(){(or 1 2);}' '1||2;'
testast '(int)f(){(& 1 2);}' '1&2;'
testast '(int)f(){(| 1 2);}' '1|2;'
testast '(int)f(){1.200000;}' '1.2;'
testast '(int)f(){(+ 1.200000 1);}' '1.2+1;'

testastf '(int)f(int c){c;}' 'int f(int c){c;}'
testastf '(int)f(int c){c;}(int)g(int d){d;}' 'int f(int c){c;} int g(int d){d;}'
testastf '(decl int a 3)' 'int a=3;'

testastf '(decl (struct) a)' 'struct {} a;'
testastf '(decl (struct (int) (char)) a)' 'struct {int x; char y;} a;'
testastf '(decl (struct ([3]int)) a)' 'struct {int x[3];} a;'
testast '(int)f(){(decl (struct tag (int)) a);(decl *(struct tag (int)) p);(deref p).x;}' 'struct tag {int x;} a; struct tag *p; p->x;'
testast '(int)f(){(decl (struct (int)) a);a.x;}' 'struct {int x;} a; a.x;'

testfail '0abc;'
testfail '1+;'
testfail '1=2;'

# & is only applicable to an lvalue
testfail '&"a";'
testfail '&1;'
testfail '&a();'

echo "All tests passed"
