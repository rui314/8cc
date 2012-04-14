// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "8cc.h"

static char *file;
static bool wantast;
static bool cpponly;
static String *cppdefs;

static void usage(void) {
    fprintf(stderr,
            "Usage: 8cc [ -E ][ -a ] [ -h ] <file>\n\n"
            "  -E                print preprocessed source code\n"
            "  -D name           Predefine name as a macro\n"
            "  -D name=def\n"
            "  -U name           Undefine name\n"
            "  -a                print AST\n"
            "  -d cpp            print tokens for debugging\n"
            "  -h                print this help\n\n");
    exit(1);
}

static void parse_debug_arg(char *s) {
    char *tok, *save;
    while ((tok = strtok_r(s, ",", &save)) != NULL) {
        s = NULL;
        if (!strcmp(tok, "cpp"))
            debug_cpp = true;
        else
            error("Unknown debug parameter: %s", tok);
    }
}

static void parseopt(int argc, char **argv) {
    cppdefs = make_string();
    for (;;) {
        int opt = getopt(argc, argv, "ED:U:ad:h");
        if (opt == -1)
            break;
        switch (opt) {
        case 'E':
            cpponly = true;
            break;
        case 'D': {
            char *p = strchr(optarg, '=');
            if (p)
                *p = ' ';
            string_appendf(cppdefs, "#define %s\n", optarg);
            break;
        }
        case 'U':
            string_appendf(cppdefs, "#undef %s\n", optarg);
            break;
        case 'a':
            wantast = true;
            break;
        case 'd':
            parse_debug_arg(optarg);
            break;
        case 'h':
        default:
            usage();
        }
    }
    if (optind != argc - 1)
        usage();
    file = argv[optind];
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
    parse_init();
    if (string_len(cppdefs) > 0)
        cpp_eval(get_cstring(cppdefs));
    lex_init(file);

    if (cpponly)
        preprocess();

    if (wantast)
        suppress_warning = true;
    List *toplevels = read_toplevels();
    for (Iter *i = list_iter(toplevels); !iter_end(i);) {
        Node *v = iter_next(i);
        if (wantast)
            printf("%s", a2s(v));
        else
            emit_toplevel(v);
    }
    return 0;
}
