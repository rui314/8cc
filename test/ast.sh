#!/bin/bash
# Copyright 2012 Rui Ueyama. Released under the MIT license.

function fail {
    echo -n -e '\e[1;31m[ERROR]\e[0m '
    echo "$1"
    exit 1
}

function compile {
    echo "$1" | ./8cc -o tmp.s - || fail "Failed to compile $1"
    gcc -o tmp.out tmp.s
     [ $? -ne 0 ] && fail "GCC failed: $1"
}

function assertequal {
    [ "$1" != "$2" ] && fail "Test failed: $2 expected but got $1"
}

function testastf {
    result="$(echo "$2" | ./8cc -o - -fdump-ast -w -)"
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
    echo "$2" | ./8cc -o - -E $3 - > tmp.s || fail "Failed to compile $1"
    assertequal "$(cat tmp.s)" "$1"
}


function testfail {
    echo "$expr" | ./8cc -o /dev/null - 2> /dev/null
    expr="int f(){$1}"
    echo "$expr" | ./8cc -o /dev/null $OPTION - 2> /dev/null
    [ $? -eq 0 ] && fail "Should fail to compile, but succeded: $expr"
}

# Parser
testast '(()=>int)f(){1;}' '1;'
testast '(()=>int)f(){1L;}' '1L;'
# testast '(()=>int)f(){1152921504606846976L;}' '1152921504606846976;'
testast '(()=>int)f(){(+ (- (+ 1 2) 3) 4);}' '1+2-3+4;'
testast '(()=>int)f(){(+ (+ 1 (* 2 3)) 4);}' '1+2*3+4;'
testast '(()=>int)f(){(+ (* 1 2) (* 3 4));}' '1*2+3*4;'
testast '(()=>int)f(){(+ (/ 4 2) (/ 6 3));}' '4/2+6/3;'
testast '(()=>int)f(){(/ (/ 24 2) 4);}' '24/2/4;'
testast '(()=>int)f(){(decl int a 3@0);}' 'int a=3;'
testast "(()=>int)f(){(decl char c (conv 97=>char)@0);}" "char c='a';"
testast '(()=>int)f(){(decl *char s (conv "abcd"=>*char)@0);}' 'char *s="abcd";'
#testast "(()=>int)f(){(decl [5]char s 'a'@0 's'@1 'd'@2 'f'@3 '\0'@4);}" 'char s[5]="asdf";'
testast "(()=>int)f(){(decl [5]char s 'a'@0 's'@1 'd'@2 'f'@3 '\0'@4);}" 'char s[]="asdf";'
testast '(()=>int)f(){(decl [3]int a 1@0 2@4 3@8);}' 'int a[3]={1,2,3};'
testast '(()=>int)f(){(decl [3]int a 1@0 2@4 3@8);}' 'int a[]={1,2,3};'
testast '(()=>int)f(){(decl [3][5]int a);}' 'int a[3][5];'
testast '(()=>int)f(){(decl [5]*int a);}' 'int *a[5];'
testast '(()=>int)f(){(decl int a 1@0);(decl int b 2@0);(= lv=a (= lv=b 3));}' 'int a=1;int b=2;a=b=3;'
testast '(()=>int)f(){(decl int a 3@0);(addr lv=a);}' 'int a=3;&a;'
testast '(()=>int)f(){(decl int a 3@0);(deref (addr lv=a));}' 'int a=3;*&a;'
testast '(()=>int)f(){(decl int a 3@0);(decl *int b (addr lv=a)@0);(deref lv=b);}' 'int a=3;int *b=&a;*b;'
testast '(()=>int)f(){(if 1 {2;});}' 'if(1){2;}'
testast '(()=>int)f(){(if 1 {2;} {3;});}' 'if(1){2;}else{3;}'
testast '(()=>int)f(){{{(decl int a 1@0);};.L0:;(if 3 (nil) goto(.L2));{5;};.L1:;7;goto(.L0);.L2:;};}' 'for(int a=1;3;7){5;}'
testast '(()=>int)f(){"abcd";}' '"abcd";'
testast "(()=>int)f(){99;}" "'c';"
testast '(()=>int)f(){(int)a();}' 'a();'
testast '(()=>int)f(){(int)a(1,2,3,4,5,6);}' 'a(1,2,3,4,5,6);'
testast '(()=>int)f(){(return (conv 1=>int));}' 'return 1;'
testast '(()=>int)f(){(< 1 2);}' '1<2;'
testast '(()=>int)f(){(< 2 1);}' '1>2;'
testast '(()=>int)f(){(== 1 2);}' '1==2;'
# testast '(()=>int)f(){(deref (+ 1 2));}' '1[2];'
testast '(()=>int)f(){(decl int a 1@0);(post++ lv=a);}' 'int a=1;a++;'
testast '(()=>int)f(){(decl int a 1@0);(post-- lv=a);}' 'int a=1;a--;'
testast '(()=>int)f(){(! 1);}' '!1;'
testast '(()=>int)f(){(? 1 2 3);}' '1?2:3;'
testast '(()=>int)f(){(and 1 2);}' '1&&2;'
testast '(()=>int)f(){(or 1 2);}' '1||2;'
testast '(()=>int)f(){(& 1 2);}' '1&2;'
testast '(()=>int)f(){(| 1 2);}' '1|2;'
testast '(()=>int)f(){1.200000;}' '1.2;'
testast '(()=>int)f(){(+ 1.200000 (conv 1=>double));}' '1.2+1;'

testastf '((int)=>int)f(int lv=c){lv=c;}' 'int f(int c){c;}'
testastf '((int)=>int)f(int lv=c){lv=c;}((int)=>int)g(int lv=d){lv=d;}' 'int f(int c){c;} int g(int d){d;}'
testastf '(decl int a 3@0)' 'int a=3;'

testastf '(decl (struct) a)' 'struct {} a;'
testastf '(decl (struct (int) (char)) a)' 'struct {int x; char y;} a;'
testastf '(decl (struct ([3]int)) a)' 'struct {int x[3];} a;'
testast '(()=>int)f(){(decl (struct (int)) a);(decl *(struct (int)) p);(deref lv=p).x;}' 'struct tag {int x;} a; struct tag *p; p->x;'
testast '(()=>int)f(){(decl (struct (int)) a);lv=a.x;}' 'struct {int x;} a; a.x;'
testast '(()=>int)f(){(decl (struct (int:0:5) (int:5:13)) x);}' 'struct { int a:5; int b:8; } x;'

testfail '0abc;'
# testfail '1+;'
testfail '1=2;'

# & is only applicable to an lvalue
testfail '&"a";'
testfail '&1;'
testfail '&a();'

# -D command line options
testcpp '77' 'foo' '-Dfoo=77'

echo "All tests passed"
