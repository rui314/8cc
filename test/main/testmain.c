// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void testmain(void);

// For test/extern.c
int externvar1 = 98;
int externvar2 = 99;

// For test/function.c
int booltest1(bool x) {
    return x;
}

void print(char *s) {
    printf("Testing %s ... ", s);
    fflush(stdout);
}

void printfail(void) {
    printf(isatty(fileno(stdout)) ? "\e[1;31mFailed\e[0m" : "Failed");
}

void fail(char *msg) {
    printfail();
    printf(": %s\n", msg);
    exit(1);
}

void expect(int a, int b) {
    if (!(a == b)) {
        printfail();
        printf("\n  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

void expect_string(char *a, char *b) {
    if (strcmp(a, b)) {
        printfail();
        printf("\n  \"%s\" expected, but got \"%s\"\n", a, b);
        exit(1);
    }
}

void expectf(float a, float b) {
    if (!(a == b)) {
        printfail();
        printf("\n  %f expected, but got %f\n", a, b);
        exit(1);
    }
}

void expectd(double a, double b) {
    if (!(a == b)) {
        printfail();
        printf("\n  %lf expected, but got %lf\n", a, b);
        exit(1);
    }
}

void expectl(long a, long b) {
    if (!(a == b)) {
        printfail();
        printf("\n  %ld expected, but got %ld\n", a, b);
        exit(1);
    }
}

void expectp(void *a, void *b) {
    if (!(a == b)) {
        printfail();
        printf("\n  %p expected, but got %p\n", a, b);
        exit(1);
    }
}

int main() {
    testmain();
    printf(isatty(fileno(stdout)) ? "\e[32mOK\e[0m\n" : "OK\n");
    return 0;
}
