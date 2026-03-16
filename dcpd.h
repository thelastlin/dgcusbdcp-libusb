#ifndef __DCPD_H
#define __DCPD_H

void pr_verbose(const char *fmt, ...);
void pr_err(const char *fmt, ...);
void set_verbose(int flags);

#define BIT(x)			(1U << (x))

enum {
    OPT_VERBOSE = BIT(0),
    OPT_FORCE   = BIT(1),
};

#define FLAG_SET(flags, f)	((flags) & (f))

#endif /* __DCPD_H */
