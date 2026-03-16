#ifndef __DCPD_H
#define __DCPD_H

#include <stdint.h>
void pr_verbose(const char *fmt, ...);
void pr_err(const char *fmt, ...);
void set_verbose(int flags);

#define BIT(x)			(1U << (x))

enum {
    OPT_VERBOSE = BIT(0),
    OPT_FORCE   = BIT(1),
};

#define FLAG_SET(flags, f)	((flags) & (f))

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))

#define USB_DEVICE(vend, prod) \
    .idVendor = (vend), \
    .idProduct = (prod)

struct usb_device_id {
    uint16_t idVendor;
    uint16_t idProduct;
};

#endif /* __DCPD_H */
