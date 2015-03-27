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

! #elif after #else
#if 1
#else
#elif 1
#end

! stray #endif
#endif

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
    tests = lex_tests + cpp_tests + encoding_tests
    p = Pool(None)
    for res in p.imap_unordered(run, parseTests(tests)):
        if res != None:
            print res
            exit(1)
    exit(0)
