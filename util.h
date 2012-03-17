#ifndef EIGHTCC_UTIL_H
#define EIGHTCC_UTIL_H

#define error(...)                              \
    errorf(__FILE__, __LINE__, __VA_ARGS__)

#define assert(expr)                                    \
    do {                                                \
        if (!(expr)) error("Assertion failed: " #expr); \
    } while (0)

extern void errorf(char *file, int line, char *fmt, ...) __attribute__((noreturn));
extern void warn(char *fmt, ...);
extern char *quote_cstring(char *p);

#endif /* EIGHTCC_UTIL_H */
