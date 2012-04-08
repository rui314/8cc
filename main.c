// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

static char *file;
static bool wantast;
static bool cpponly;

static void usage(void) {
    fprintf(stderr,
            "Usage: 8cc [ -E ][ -a ] [ -h ] <file>\n\n"
            "  -E    print preprocessed source code\n"
            "  -a    print AST\n"
            "  -h    print this help\n\n");
    exit(1);
}

static void parseopt(int argc, char **argv) {
    int i;
    for (i = 1; argv[i]; i++) {
        if (!strcmp(argv[i], "-a"))
            wantast = true;
        else if (!strcmp(argv[i], "-E"))
            cpponly = true;
        else if (!strcmp(argv[i], "-h"))
            usage();
        else break;
    }
    if (i != argc - 1)
        usage();
    file = argv[i];
}

static void preprocess(void) {
    for (;;) {
        Token *tok = read_token();
        if (!tok)
            break;
        if (tok->bol)
            printf("\n");
        if (tok->nspace)
            printf("%*c", tok->nspace, ' ');
        printf("%s", t2s(tok));
    }
    printf("\n");
    exit(0);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    parseopt(argc, argv);

    cpp_init();
    lex_init(file);

    if (cpponly)
        preprocess();

    if (wantast)
        suppress_warning = true;
    List *toplevels = read_toplevels();
    if (!wantast)
        emit_data_section();
    for (Iter *i = list_iter(toplevels); !iter_end(i);) {
        Node *v = iter_next(i);
        if (wantast)
            printf("%s", a2s(v));
        else
            emit_toplevel(v);
    }
    return 0;
}
