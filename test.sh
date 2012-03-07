#!/bin/bash

function compile {
  echo "$1" | ./8cc > tmp.s
  if [ $? -ne 0 ]; then
    echo "Failed to compile $1"
    exit
  fi
  gcc -o tmp.out driver.c tmp.s
  if [ $? -ne 0 ]; then
    echo "GCC failed"
    exit
  fi
}

function assertequal {
  if [ "$1" != "$2" ]; then
    echo "Test failed: $2 expected but got $1"
    exit
  fi
}

function testast {
  result="$(echo "$2" | ./8cc -a)"
  if [ $? -ne 0 ]; then
    echo "Failed to compile $1"
    exit
  fi
  assertequal "$result" "$1"
}

function test {
  compile "$2"
  assertequal "$(./tmp.out)" "$1"
}

function testfail {
  expr="$1"
  echo "$expr" | ./8cc > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    echo "Should fail to compile, but succeded: $expr"
    exit
  fi
}

make -s 8cc

# Parser
testast '{1;}' '1;'
testast '{(+ (- (+ 1 2) 3) 4);}' '1+2-3+4;'
testast '{(+ (+ 1 (* 2 3)) 4);}' '1+2*3+4;'
testast '{(+ (* 1 2) (* 3 4));}' '1*2+3*4;'
testast '{(+ (/ 4 2) (/ 6 3));}' '4/2+6/3;'
testast '{(/ (/ 24 2) 4);}' '24/2/4;'
testast '{(decl int a 3);}' 'int a=3;'
testast "{(decl char c 'a');}" "char c='a';"
testast '{(decl char* s "abc");}' 'char *s="abc";'
testast '{(decl char[4] s "abc");}' 'char s[4]="abc";'
testast '{(decl int[3] a {1,2,3});}' 'int a[3]={1,2,3};'
testast '{(decl int a 1);(decl int b 2);(= a (= b 3));}' 'int a=1;int b=2;a=b=3;'
testast '{(decl int a 3);(& a);}' 'int a=3;&a;'
testast '{(decl int a 3);(* (& a));}' 'int a=3;*&a;'
testast '{(decl int a 3);(decl int* b (& a));(* b);}' 'int a=3;int *b=&a;*b;'
testast '{(if 1 {2;});}' 'if(1){2;}'
testast '{(if 1 {2;} {3;});}' 'if(1){2;}else{3;}'

testast '{"abc";}' '"abc";'
testast "{'c';}" "'c';"

testast '{a();}' 'a();'
testast '{a(1,2,3,4,5,6);}' 'a(1,2,3,4,5,6);'

# Basic arithmetic
test 0 '0;'
test 3 '1+2;'
test 3 '1 + 2;'
test 10 '1+2+3+4;'
test 11 '1+2*3+4;'
test 14 '1*2+3*4;'
test 4 '4/2+6/3;'
test 3 '24/2/4;'
test 98 "'a'+1;"
test 2 '1;2;'

# Declaration
test 3 'int a=1;a+2;'
test 102 'int a=1;int b=48+2;int c=a+b;c*2;'
test 55 'int a[1]={55};int *b=a;*b;'
test 67 'int a[2]={55,67};int *b=a+1;*b;'
test 30 'int a[3]={20,30,40};int *b=a+1;*b;'

# Function call
test 25 'sum2(20, 5);'
test 15 'sum5(1, 2, 3, 4, 5);'
test a3 'printf("a");3;'
test abc5 'printf("%s", "abc");5;'
test b1 "printf(\"%c\", 'a'+1);1;"

# Pointer
test 61 'int a=61;int *b=&a;*b;'
test 97 'char *c="ab";*c;'
test 98 'char *c="ab"+1;*c;'
test 99 'char s[4]="abc";char *c=s+2;*c;'

# If statement
test 'a1' 'if(1){printf("a");}1;'
test '1' 'if(0){printf("a");}1;'
test 'x1' 'if(1){printf("x");}else{printf("y");}1;'
test 'y1' 'if(0){printf("x");}else{printf("y");}1;'

testfail '0abc;'
testfail '1+;'
testfail '1=2;'

# & is only applicable to an lvalue
testfail '&"a";'
testfail '&1;'
testfail '&a();'

echo "All tests passed"
