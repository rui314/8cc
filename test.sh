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

function testf {
    compile "int main(){printf(\"%d\",f());} $2"
    assertequal "$(./tmp.out)" "$1"
}

function testm {
    compile "$2"
    assertequal "$(./tmp.out)" "$1"
}

function test {
    testf "$1" "int f(){$2}"
}

function testfail {
    expr="int f(){$1}"
    echo "$expr" | ./8cc > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "Should fail to compile, but succeded: $expr"
        exit
    fi
}

make -s 8cc

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

# Basic arithmetic
test 0 '0;'
test 3 '1+2;'
test 3 '1 + 2;'
test 10 '1+2+3+4;'
test 11 '1+2*3+4;'
test 14 '1*2+3*4;'
test 4 '4/2+6/3;'
test 4 '24/2/3;'
test 98 "'a'+1;"
test 2 '1;2;'
test -1 'int a=0-1;a;'
test 0 'int a=0-1;1+a;'

# Floating point number
testm 0.5 'int main(){ float f = 0.5; printf("%.1f", f); }'
testm 1.5 'int main(){ float f = 1.0 + 0.5; printf("%.1f", f); }'

# Comparison
test 1 '1<2;'
test 0 '2<1;'
test 1 '1==1;'
test 0 '1==2;'

# Assignment
test '1 1 1 4' 'int a;int b;int c; a=b=c=1; printf("%d %d %d ",a,b,c); 4;'

# Declaration
test 3 'int a=1;a+2;'
test 102 'int a=1;int b=48+2;int c=a+b;c*2;'
test 55 'int a[]={55};int *b=a;*b;'
test 67 'int a[]={55,67};int *b=a+1;*b;'
test 30 'int a[]={20,30,40};int *b=a+1;*b;'
test 20 'int a[]={20,30,40};*a;'

# Function call
test a3 'printf("a");3;'
test xy5 'printf("%s", "xy");5;'
test b1 "printf(\"%c\", 'a'+1);1;"

# Pointer
test 61 'int a=61;int *b=&a;*b;'
test 97 'char *c="ab";*c;'
test 98 'char *c="ab"+1;*c;'
test 122 'char s[]="xyz";char *c=s+2;*c;'
test 65 'char s[]="xyz";*s=65;*s;'

# Array
test 1 'int a[2][3];int *p=a;*p=1;*p;'
test 32 'int a[2][3];int *p=a+1;*p=1;int *q=a;*p=32;*(q+3);'
test 62 'int a[4][5];int *p=a;*(*(a+1)+2)=62;*(p+7);'
test '1 2 3 0' 'int a[3]={1,2,3};printf("%d %d %d ",a[0],a[1],a[2]);0;'
test '1 2 0' 'int a[2][3];a[0][1]=1;a[1][1]=2;int *p=a;printf("%d %d ",p[1],p[4]);0;'
testf '65 1' 'int g(int x[][3]){printf("%d ",*(*(x+1)+1));} int f(){int a[2][3];int *p=a;*(p+4)=65;g(a);1;}'

# If statement
test 'a1' 'if(1){printf("a");}1;'
test '1' 'if(0){printf("a");}1;'
test 'x1' 'if(1){printf("x");}else{printf("y");}1;'
test 'y1' 'if(0){printf("x");}else{printf("y");}1;'
test 'a1' 'if(1)printf("a");1;'
test '1' 'if(0)printf("a");1;'
test 'x1' 'if(1)printf("x");else printf("y");1;'
test 'y1' 'if(0)printf("x");else printf("y");1;'

# For statement
test 012340 'for(int i=0; i<5; i=i+1){printf("%d",i);}0;'

# Return statement
test 33 'return 33; return 10;'

# Increment or decrement
test 15 'int a=15;a++;'
test 16 'int a=15;a++;a;'
test 15 'int a=15;a--;'
test 14 'int a=15;a--;a;'

# Boolean operators
test 0 '!1;'
test 1 '!0;'

# Global variable
testf 21 'int a=21;int f(){a;}'
testf 22 'int a;int f(){a=22;a;}'
testf 23 'int a[3];int f(){a[1]=23;a[1];}'
testf 25 'int a[3]={24,25,26};int f(){a[1];}'

# Block scope
test 31 'int a=31;{int a=64;}a;'
test 64 'int a=31;{int a=64;a;}'

# Function parameter
testf 77 'int g(){77;} int f(){g();}'
testf 79 'int g(int a){a;} int f(){g(79);}'
testf 21 'int g(int a,int b,int c,int d,int e,int f){a+b+c+d+e+f;} int f(){g(1,2,3,4,5,6);}'
testf 79 'int g(int a){a;} int f(){g(79);}'
testf 98 'int g(int *p){*p;} int f(){int a[]={98};g(a);}'
testf '99 98 97 1' 'int g(int *p){printf("%d ",*p);p=p+1;printf("%d ",*p);p=p+1;printf("%d ",*p);1;} int f(){int a[]={1,2,3};int *p=a;*p=99;p=p+1;*p=98;p=p+1;*p=97;g(a);}'
testf '99 98 97 1' 'int g(int *p){printf("%d ",*p);p=p+1;printf("%d ",*p);p=p+1;printf("%d ",*p);1;} int f(){int a[3];int *p=a;*p=99;p=p+1;*p=98;p=p+1;*p=97;g(a);}'

# Ternary operator
test 51 '(1+2)?51:52;'
test 52 '(1-1)?51:52;'

# Logical operators
test 1 '55 && 2;'
test 0 '55 && 0;'
test 0 '0 && 55;'

# Bit operators
test 3 '1|2;'
test 1 '1&3;'

# Struct
test 61 'struct {int a;} x; x.a = 61; x.a;'
test 63 'struct {int a; int b;} x; x.a = 61; x.b = 2; x.a + x.b;'
test 67 'struct {int a; struct {char b; int c;} y; } x; x.a = 61; x.y.b = 3; x.y.c = 3; x.a + x.y.b + x.y.c;'
test 67 'struct tag {int a; struct {char b; int c;} y; } x; struct tag s; s.a = 61; s.y.b = 3; s.y.c = 3; s.a + s.y.b + s.y.c;'
test 68 'struct tag {int a;} x; struct tag *p = &x; x.a = 68; (*p).a;'
test 69 'struct tag {int a;} x; struct tag *p = &x; (*p).a = 69; x.a;'
test 71 'struct tag {int a; int b;} x; struct tag *p = &x; x.b = 71; (*p).b;'
test 72 'struct tag {int a; int b;} x; struct tag *p = &x; (*p).b = 72; x.b;'
test 73 'struct tag {int a[3]; int b[3];} x; x.a[0] = 73; x.a[0];'
test 74 'struct tag {int a[3]; int b[3];} x; x.b[1] = 74; x.a[4];'
testf 77 'struct {int a; struct {char b; int c;} y; } x; int f() { x.a = 71; x.y.b = 3; x.y.c = 3; x.a + x.y.b + x.y.c;}'
testf 78 'struct tag {int a;} x; int f() { struct tag *p = &x; x.a = 78; (*p).a;}'
testf 79 'struct tag {int a;} x; int f() { struct tag *p = &x; (*p).a = 79; x.a;}'
testf 78 'struct tag {int a;} x; int f() { struct tag *p = &x; x.a = 78; p->a;}'
testf 79 'struct tag {int a;} x; int f() { struct tag *p = &x; p->a = 79; x.a;}'
testf 81 'struct tag {int a; int b;} x; int f() { struct tag *p = &x; x.b = 81; (*p).b;}'
testf 82 'struct tag {int a; int b;} x; int f() { struct tag *p = &x; (*p).b = 82; x.b;}'
testf 83 'struct tag {int a; int b;} x; int f() { struct tag a[3]; a[0].a = 83; a[0].a;}'
testf 84 'struct tag {int a; int b;} x; int f() { struct tag a[3]; a[1].b = 84; a[1].b;}'
testf 85 'struct tag {int a; int b;} x; int f() { struct tag a[3]; a[1].b = 85; int *p=a; p[3];}'

testfail '0abc;'
testfail '1+;'
testfail '1=2;'

# Union
test 90 'union {int a; int b;} x; x.a = 90; x.b;'
test 256 'union {char a[4]; int b;} x; x.b=0; x.a[1]=1; x.b;';
test 256 'union {char a[4]; int b;} x; x.a[0]=x.a[1]=x.a[2]=x.a[3]=0; x.a[1]=1; x.b;';

# & is only applicable to an lvalue
testfail '&"a";'
testfail '&1;'
testfail '&a();'

echo "All tests passed"
