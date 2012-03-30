#include <stdio.h>

void exit(int);
int strlen(char *);

extern int externvar1;
extern int externvar2;

void print(char *s);
void fail(char *msg);
void expect(int a, int b);
void expect_string(char *a, char *b);
void expectf(float a, float b);
void expectd(double a, double b);
