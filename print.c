#include <stdarg.h>
#include <stdio.h>
#include "dcpd.h"

static int opt_verbose = 0;

void pr_verbose(const char *fmt, ...)
{
    va_list args;

    if (!opt_verbose)
	return;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void pr_err(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void set_verbose(int flags)
{
    opt_verbose = flags;
}
