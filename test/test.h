// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "stdio.h"

void exit(int);
size_t strlen(const char *);

extern int externvar1;
extern int externvar2;

extern void print(char *s);
extern void ffail(char *file, int line, char *msg);
extern void fexpect(char *file, int line, int a, int b);
extern void fexpect_string(char *file, int line, char *a, char *b);
extern void fexpectf(char *file, int line, float a, float b);
extern void fexpectd(char *file, int line, double a, double b);
extern void fexpectl(char *file, int line, long a, long b);

#define fail(msg) ffail(__FILE__, __LINE__, msg)
#define expect(a, b) fexpect(__FILE__, __LINE__, a, b);
#define expect_string(a, b) fexpect_string(__FILE__, __LINE__, a, b);
#define expectf(a, b) fexpectf(__FILE__, __LINE__, a, b);
#define expectd(a, b) fexpectd(__FILE__, __LINE__, a, b);
#define expectl(a, b) fexpectl(__FILE__, __LINE__, a, b);
