// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef __STDARG_H
#define __STDARG_H

/**
 * Refer this document for the x86-64 ABI.
 * http://www.x86-64.org/documentation/abi.pdf
 */

typedef struct {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} va_list[1];

#define va_start(ap, last) __builtin_va_start(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) 1
#define va_copy(dest, src) ((dest) = (src))

// Workaround to load stdio.h properly
#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif /* __STDARG_H */
