// Copyright 2012 Rui Ueyama. Released under the MIT license.

#define MACRO_1 "macro1"
#if __INCLUDE_LEVEL__ != 1
# error "include level"
#endif
