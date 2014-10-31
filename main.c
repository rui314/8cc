// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "8cc.h"

static char *inputfile;
static char *outputfile;
static bool dumpast;
static bool cpponly;
static bool dumpasm;
static bool dontlink;
static Buffer *cppdefs;
static Vector *tmpfiles = &EMPTY_VECTOR;

static void usage(void) {
    fprintf(stderr,
            "Usage: 8cc [ -E ][ -a ] [ -h ] <file>\n\n"
            "\n"
            "  -I<path>          add to include path\n"
            "  -E                print preprocessed source code\n"
            "  -D name           Predefine name as a macro\n"
            "  -D name=def\n"
            "  -S                Stop before assembly (default)\n"
            "  -c                Do not run linker (default)\n"
            "  -U name           Undefine name\n"
            "  -fdump-ast        print AST\n"
            "  -fdump-stack      Print stacktrace\n"
            "  -fno-dump-source  Do not emit source code as assembly comment\n"
            "  -d cpp            print tokens for debugging\n"
            "  -o filename       Output to the specified file\n"
            "  -g                Do nothing at this moment\n"
            "  -Wall             Enable all warnings\n"
            "  -Werror           Make all warnings into errors\n"
            "  -O<number>        Does nothing at this moment\n"
            "  -m64              Output 64-bit code (default)\n"
            "  -w                Disable all warnings\n"
            "  -h                print this help\n"
            "\n"
            "One of -a, -c, -E or -S must be specified.\n\n");
    exit(1);
}

static void delete_temp_files(void) {
    for (int i = 0; i < vec_len(tmpfiles); i++)
        unlink(vec_get(tmpfiles, i));
}

static char *replace_suffix(char *filename, char suffix) {
    char *r = format("%s", filename);
    char *p = r + strlen(r) - 1;
    if (*p != 'c')
        error("filename suffix is not .c");
    *p = suffix;
    return r;
}

static FILE *open_output_file(void) {
    if (!outputfile) {
        if (dumpasm) {
            outputfile = replace_suffix(inputfile, 's');
        } else {
            outputfile = format("/tmp/8ccXXXXXX.s");
            if (!mkstemps(outputfile, 2))
                perror("mkstemps");
            vec_push(tmpfiles, outputfile);
        }
    }
    if (!strcmp(outputfile, "-"))
        return stdout;
    FILE *fp = fopen(outputfile, "w");
    if (!fp)
        perror("fopen");
    return fp;
}

static void parse_warnings_arg(char *s) {
    if (!strcmp(s, "error"))
        warning_is_error = true;
    else if (strcmp(s, "all"))
        error("unknown -W option: %s", s);
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

static void parse_f_arg(char *s) {
    if (!strcmp(s, "dump-ast"))
        dumpast = true;
    else if (!strcmp(s, "dump-stack"))
        dumpstack = true;
    else if (!strcmp(s, "no-dump-source"))
        dumpsource = false;
    else
        usage();
}

static void parse_m_arg(char *s) {
    if (strcmp(s, "64"))
        error("Only 64 is allowed for -m, but got %s", s);
}

static void parseopt(int argc, char **argv) {
    cppdefs = make_buffer();
    for (;;) {
        int opt = getopt(argc, argv, "I:ED:O:SU:W:acd:f:gm:o:hw");
        if (opt == -1)
            break;
        switch (opt) {
        case 'I':
            add_include_path(optarg);
            break;
        case 'E':
            cpponly = true;
            break;
        case 'D': {
            char *p = strchr(optarg, '=');
            if (p)
                *p = ' ';
            buf_printf(cppdefs, "#define %s\n", optarg);
            break;
        }
        case 'O':
            break;
        case 'S':
            dumpasm = true;
            break;
        case 'U':
            buf_printf(cppdefs, "#undef %s\n", optarg);
            break;
        case 'W':
            parse_warnings_arg(optarg);
            break;
        case 'a':
            dumpast = true;
            break;
        case 'c':
            dontlink = true;
            break;
        case 'd':
            parse_debug_arg(optarg);
            break;
        case 'f':
            parse_f_arg(optarg);
            break;
        case 'm':
            parse_m_arg(optarg);
            break;
        case 'g':
            break;
        case 'o':
            outputfile = optarg;
            break;
        case 'w':
            enable_warning = false;
            break;
        case 'h':
        default:
            usage();
        }
    }
    if (optind != argc - 1)
        usage();

    if (!dumpast && !cpponly && !dumpasm && !dontlink)
        error("One of -a, -c, -E or -S must be specified");
    inputfile = argv[optind];
}

static void preprocess(void) {
    for (;;) {
        Token *tok = read_token();
        if (!tok)
            break;
        if (tok->bol)
            printf("\n");
        if (tok->space)
            printf(" ");
        printf("%s", t2s(tok));
    }
    printf("\n");
    exit(0);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (atexit(delete_temp_files))
        perror("atexit");
    cpp_init();
    parse_init();
    parseopt(argc, argv);
    if (buf_len(cppdefs) > 0)
        cpp_eval(buf_body(cppdefs));
    lex_init(inputfile);
    set_output_file(open_output_file());

    if (cpponly)
        preprocess();

    Vector *toplevels = read_toplevels();
    for (int i = 0; i < vec_len(toplevels); i++) {
        Node *v = vec_get(toplevels, i);
        if (dumpast)
            printf("%s", a2s(v));
        else
            emit_toplevel(v);
    }

    close_output_file();

    if (!dumpast && !dumpasm) {
        char *objfile = replace_suffix(inputfile, 'o');
        pid_t pid = fork();
        if (pid < 0) perror("fork");
        if (pid == 0) {
            execlp("as", "as", "-o", objfile, "-c", outputfile, (char *)NULL);
            perror("execl failed");
        }
        int status;
        waitpid(pid, &status, 0);
        if (status < 0)
            error("as failed");
    }
    return 0;
}
