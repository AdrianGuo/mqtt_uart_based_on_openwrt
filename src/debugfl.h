//-为了方便调试定义了系列变量以便调试输出

#ifndef DEBUGFL_H
#define DEBUGFL_H

#include <stdio.h>

#if 1
#define DEBUG(...) do{ fprintf(stderr, "<debugfl> ");printf(__VA_ARGS__);} while(0)
#define API() fprintf(stderr, "<debugfl> api: %s.\n", __FUNCTION__)
#else
#define DEBUG(...)
#define API()
#endif

#endif /* DEBUGFL_H */
