// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef FILE_H
#define FILE_H

typedef struct {
    FILE *file;
    char *name;
    int line;
    int column;
    bool autopop;
    union {
        // stream backed by FILE
        struct {
            int buf[2];
            int buflen;
        };
        // stream backed by string
        char *p;
    };
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
