// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdio.h>

void exit(int);
size_t strlen(const char *);

extern int externvar1;
extern int externvar2;

void print(char *s);
void fail(char *msg);
void expect(int a, int b);
void expect_string(char *a, char *b);
void expectf(float a, float b);
void expectd(double a, double b);
void expectl(long a, long b);
