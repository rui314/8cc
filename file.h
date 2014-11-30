// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef FILE_H
#define FILE_H

typedef struct {
    FILE *file;  // stream backed by FILE *
    char *p;     // stream backed by string
    char *name;
    int line;
    int column;
    int ntok;  // number of tokens in the file (except space and newline)
    bool autopop;
    int last;
    int buf[2];
    int buflen;
} File;

extern int readc(void);
extern void unreadc(int c);
extern File *current_file(void);
extern void insert_stream(FILE *file, char *name);
extern void push_stream(FILE *file, char *name);
extern void push_stream_string(char *s);
extern void pop_stream(void);
extern int stream_depth(void);

#endif
