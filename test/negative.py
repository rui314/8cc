#!/usr/bin/python

# This file contains negative tests.
# All tests in this file are not valid C code and thus cause
# the compiler to print out error messages.
# We verify that the compiler actually detects these errors and
# prints out correct error messages.

from multiprocessing import Pool
from subprocess import Popen, PIPE, STDOUT
import re

# These are lists of expected error messages and compiler inputs.
lex_tests = r"""
! \x is not followed by a hexadecimal character: z
int i = '\xz'

! invalid universal character: @
char *p = "\u123@";

! invalid universal character: \u0097
char *p = "\u0097";

! unknown escape character: \y
char *p = "\y";

! unterminated char
char x = '

! unterminated string
char *p = "

! premature end of block comment
/*
"""

cpp_tests = r"""
! unterminated macro argument list
#define x()
x(

! macro argument number does not match
#define x(_)
x(1, 2)

! ',' expected, but got 'b'
#define x(a b)

! missing ')' in macro parameter list
#define x(a,

! identifier expected, but got '123'
#define x(123)

! ) expected, but got ,
#define x(x, ..., y)

! '##' cannot appear at start of macro expansion
#define x ##

! '##' cannot appear at start of macro expansion
#define x(y) ## x

! '##' cannot appear at end of macro expansion
#define x(y) x ##

! identifier expected, but got "abc"
#if defined("abc")

! identifier expected, but got "abc"
#ifdef "abc"

! identifier expected, but got "abc"
#ifndef "abc"

! stray #else
#else

! #else appears in #else
#if 0
#else
#else
#endif

! stray #elif
#elif

! #elif after #else
#if 1
#else
#elif 1
#end

! stray #endif
#endif

! #warning: foobar
#warning foobar

! #error: foobar
#error foobar

! expected file name, but got (newline)
#include

! '<' expected, but got *
#define x *foo*
#include x

! premature end of header name
#define x <foo
#include x

! cannot find header file: /no-such-file
#include </no-such-file>

! unknown #pragma: foo
#pragma foo

! number expected after #line, but got "abc"
#line "abc" "def"

! newline or a source name are expected, but got 456
#line 123 456

! line number expected, but got 123.4
# 123.4

! file name expected, but got 2
# 1 2

! unsupported preprocessor directive: foo
#foo

! _Pragma takes a string literal, but got 1
_Pragma(1)
"""

encoding_tests = r"""
! unsupported non-standard concatenation of string literals: L"bar"
u"foo" L"bar";

! invalid UTF-8 sequence
void *p = U"\xff";

! invalid UTF-8 continuation byte
void *p = U"\xE3\x94";

! invalid UCS character: \U10000000
void *p = "\U10000000";
"""

parser_tests = r"""
! lvalue expected, but got 1
int f() { 1++; }

! lvalue expected, but got 1
int f() { &1; }

! lvalue expected, but got 1
int f() { 1 = 3; }

! integer type expected, but got 1.5
int f() { 1.5 << 2; }

! integer type expected, but got 3.0
int f() { 1 << 3.0; }

! integer type expected, but got 4.0
int f() { switch (4.0); }

! void is not allowed
struct { void i; }

! void is not allowed
int f(void v);

! void is not allowed
void x;

! '(' expected, but got long
int x = _Alignof long;

! ',' expected, but got foo
int x;
int f() { _Static_assert(_Generic(x foo)) }

! type name expected, but got foo
int x;
int f() { _Static_assert(_Generic(x, foo)) }

! default expression specified twice
int x;
int f() { _Static_assert(_Generic(x, default: x, default: x)) }

! no matching generic selection
int x;
struct { _Static_assert(_Generic(x, float: x)) };

! invalid character 'x': 0x1x
int x = 0x1x;

! invalid character 'x': 0.2x
int x = 0.2x;

! undefined variable: x
int f() { return x; }

! function expected, but got 1
int f() { 1(2); }

! pointer type expected, but got int 1
int f() { 1->x; }

! label name expected after &&, but got 1
int f() { &&1; }

! pointer type expected, but got 1
int f() { *1; }

! invalid use of ~: 3.0
int f() { ~3.0; }

! expression expected
int f() { switch(); }

! struct expected, but got 97
int f() { 'a'.x; }

! pointer type expected, but got
int f() { f->x; }

! struct has no such field: z
struct { int x; } y;
int f() { y.z; }

! non-integer type cannot be a bitfield: float
struct { float x:3; };

! invalid bitfield size for char: 9
struct { char x:9; };

! invalid bitfield size for int: 33
struct { int x:33; };

! zero-width bitfield needs to be unnamed: x
struct { int x:0; };

! missing ';' at the end of field list
struct { int x };

! flexible member may only appear as the last member
struct { int x[]; int y; };

! flexible member with no other fields
struct { int x[]; };

! declarations of T does not match
struct T { int y; };
union T x;

! declarations of T does not match
struct T { int y; };
enum T x;

! enum tag T is not defined
enum T x;

! identifier expected, but got int
enum { int };

! ',' or '}' expected, but got y
enum { x y };

! excessive initializer: 4
int x[3] = { 1, 2, 3, 4 };

! malformed desginated initializer: 'a'
struct { int x; } x = { .'a' = 3 };

! field does not exist: y
struct { int x; } x = { .y = 3 };

! array designator exceeds array bounds: 4
int x[3] = { [4] = 1; };

! at least one parameter is required before "..."
int f(...);

! comma expected, but got y
int f(int x y);

! identifier expected, but got 1
int f(x, 1) {}

! comma expected, but got 1
int f(x 1) {}

! at least one parameter is required before "..."
int f(...) {}

! array of functions
int x[3]();

! function returning a function
typedef T(void);
T f();

! function returning an array
typedef T[3];
T f();

! identifier is not expected, but got y
int x = sizeof(int y);

! identifier, ( or * are expected, but got 1
int 1;

! premature end of input
int x = sizeof(int

! negative alignment: -1
int _Alignas(-1) x;

! alignment must be power of 2, but got 3
int _Alignas(3) x;

! type mismatch: double
int double x;

! type mismatch: double
struct { int x; } double y;

! type specifier missing, assuming int
f();

! invalid function definition
int f(x, y) z {}

! missing parameter: z
int f(x, y) int z; {}

! premature end of input
int f(

! stray goto: x
int f() { goto x; }

! stray unary &&: x
int f() { &&x; }

! 'while' is expected, but got 1
int f() { do; 1; }

! duplicate case value: 1
int f() { switch(1) { case 1: case 1:; }; }

! duplicate case value: 3 ... 5
int f() { switch(1) { case 1 ... 4: case 3 ... 5:; }; }

! case region is not in correct order: 5 ... 3
int f() { switch(1) { case 5 ... 3:; }; }

! duplicate default
int f() { switch(1) { default: default:; }; }

! stray break statement
int f() { break; }

! stray continue statement
int f() { continue; }

! pointer expected for computed goto, but got 1
int f() { goto *1; }

! identifier expected, but got 1
int f() { goto 1; }

! duplicate label: x
int f() { x: x:; }

! stray character in program: '\'
\ x
"""

def run(args):
    expect, code = args
    p = Popen(["./8cc", "-c", "-o", "/dev/null", "-"], stdin=PIPE, stdout=PIPE, stderr=STDOUT)
    out, err = p.communicate(code)
    if out == None:
        return "expected error, but it didn't fail: %s" % expect
    if out.find(expect) == -1:
        return "expected: %s\ngot: %s" % (expect, out.rstrip("\n"))
    return None

def parseTests(s):
    return map(lambda t: t.split("\n", 1), re.split("\n! ", s)[1:])

if __name__ == '__main__':
    # Run tests in parallel using process pool
    tests = lex_tests + cpp_tests + encoding_tests + parser_tests
    p = Pool(None)
    for res in p.imap_unordered(run, parseTests(tests)):
        if res != None:
            print res
            exit(1)
    exit(0)
