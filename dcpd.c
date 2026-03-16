/*
 * dcpd - Conexant USB Modem Diagnostic Channel Daemon
 *
 * User-space implementation of the dgcusbdcp kernel module.
 * Reads audio data from USB modem and outputs raw PCM stream to stdout.
 *
 * Example:
 *   sudo ./dcpd | aplay -f S16_LE -r 16000 -c 1
 */

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <libusb.h>

#include "include/cleanup.h"
#include "dcpd.h"

static const struct usb_device_id dcpd_ids[] = {
	/* Conexant USB ACF Modems, with signed NT drivers support */
	{USB_DEVICE(0x0572, 0x1328)},
	{USB_DEVICE(0x0572, 0x1329)},
	{USB_DEVICE(0x0572, 0x1348)},
	{USB_DEVICE(0x0572, 0x1349)},
	/* Dell RD02-D400?*/
	{USB_DEVICE(0x0572, 0x1324)},
	/* Zoom Inc, Model 3095 */
	{USB_DEVICE(0x0803, 0x3095)},

	/*
	 * Chipset defaults VID/PID pair.
	 * setting VID/PID/SN requires EEPROM connected, see datasheet
	 */
	{USB_DEVICE(0x0572, 0x1320)},
	{USB_DEVICE(0x0572, 0x1321)},
	{USB_DEVICE(0x0572, 0x1322)},
	{USB_DEVICE(0x0572, 0x1323)},
	/* CX93010-2x, Data/Fax/Voice */
	{USB_DEVICE(0x0572, 0x1340)},

};

/* USB endpoints */
#define EP_IN 0x83
#define EP_OUT 0x03
#define CONFIG_NUM 2
#define INTERFACE_NUM 2

/* DCP mode enable command */
static const uint8_t dcp_enable_cmd[] = {0x19, 0x01};

/* Buffer sizes */
#define RX_BUF_SIZE 256
#define DX_BUF_SIZE 256
#define USB_TIMEOUT_MS 2000

static int running = 1;

static void signal_handler(int sig)
{
    pr_verbose("Got Signal: %d", sig);

    running = 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-v] [-f]\n", prog);
    fprintf(stderr, "  -v    Verbose output to stderr\n");
    fprintf(stderr, "  -f    Ignore error when setting USB configuration\n");
    fprintf(stderr, "\nOutput: 16-bit signed LE PCM, 16000 Hz, mono\n");
    fprintf(stderr, "Example: sudo %s | aplay -f S16_LE -r 16000 -c 1\n", prog);
}

static const struct usb_device_id *find_device_id(uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < ARRAY_SIZE(dcpd_ids); i++) {
	if (dcpd_ids[i].idVendor == vid && dcpd_ids[i].idProduct == pid)
	    return &dcpd_ids[i];
    }
    return NULL;
}

static void do_usbdev_cleanup(libusb_device_handle *handle)
{
    libusb_release_interface(handle, INTERFACE_NUM);
    libusb_close(handle);
}

DEFINE_FREE(libusb_devhandle, libusb_device_handle *,
	    if (_T) do_usbdev_cleanup(_T));

static libusb_device_handle *find_and_open_device(libusb_context *ctx)
{
    libusb_device **devices;
    libusb_device *found = NULL;
    libusb_device_handle *handle = NULL;
    ssize_t count;
    int ret;

    count = libusb_get_device_list(ctx, &devices);
    if (count < 0) {
	pr_err("Error: Failed to get device list: %s\n",
	       libusb_error_name(count));
	return NULL;
    }

    /* Find first supported device */
    for (ssize_t i = 0; devices[i] != NULL; i++) {
	struct libusb_device_descriptor desc;
	ret = libusb_get_device_descriptor(devices[i], &desc);
	if (ret < 0) {
	    pr_err("Warning: Failed to get device descriptor: %s\n",
		   libusb_error_name(ret));
	    continue;
	}

	if (find_device_id(desc.idVendor, desc.idProduct)) {
	    found = devices[i];
	    pr_verbose("Found device: %04x:%04x\n", desc.idVendor,
		       desc.idProduct);
	    break;
	}
    }

    if (!found) {
	pr_err("Error: No supported Modem found\n");
	libusb_free_device_list(devices, 1);
	return NULL;
    }

    ret = libusb_open(found, &handle);
    libusb_free_device_list(devices, 1);

    if (ret < 0) {
	pr_err("Error: Failed to open device: %s\n", libusb_error_name(ret));
	return NULL;
    }

    return handle;
}

static int configure_device(libusb_device_handle *handle, unsigned int flags)
{
    struct libusb_device *dev;
    int ret;
    int current_config;

    dev = libusb_get_device(handle);

    /* Check device has 2 configurations */
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(dev, &desc);
    if (ret < 0) {
	pr_err("Error: Failed to get device descriptor: %s\n",
	       libusb_error_name(ret));
	return -1;
    }

    if (desc.bNumConfigurations < 2) {
	pr_err("Error: Device has only %d configurations (need 2)\n",
	       desc.bNumConfigurations);
	return -1;
    }

    /* Check current configuration */
    ret = libusb_get_configuration(handle, &current_config);
    if (ret < 0) {
	pr_err("Error: Failed to get current configuration: %s\n",
	       libusb_error_name(ret));
	return -1;
    }
    pr_verbose("Info: Current configuration was set to %d\n", current_config);

    /* Set configuration 2 if not already set */
    if (current_config != CONFIG_NUM) {
	ret = libusb_set_configuration(handle, CONFIG_NUM);
	if (ret < 0) {
	    pr_err("Error: Failed to set configuration %d: %s\n", CONFIG_NUM,
		   libusb_error_name(ret));
	    if (!FLAG_SET(flags, OPT_FORCE))
		return -1;
	}
	pr_verbose("Set configuration to %d (was %d)\n", CONFIG_NUM,
		   current_config);
    } else {
	pr_verbose("Configuration already set to %d\n", CONFIG_NUM);
    }

    /* Claim interface 2 */
    ret = libusb_claim_interface(handle, INTERFACE_NUM);
    if (ret < 0) {
	pr_err("Error: Failed to claim interface %d: %s\n", INTERFACE_NUM,
	       libusb_error_name(ret));
	return -1;
    }
    pr_verbose("Claimed interface %d\n", INTERFACE_NUM);

    return 0;
}

static int enable_dcp_mode(libusb_device_handle *handle)
{
    int transferred;
    int ret;

    ret = libusb_bulk_transfer(handle, EP_OUT, (void *)dcp_enable_cmd,
			       sizeof(dcp_enable_cmd), &transferred,
			       USB_TIMEOUT_MS);
    if (ret < 0) {
	pr_err("Error: Failed to enable DCP mode: %s\n",
	       libusb_error_name(ret));
	return -1;
    }
    if (transferred != sizeof(dcp_enable_cmd)) {
	pr_err("Error: Short write enabling DCP mode (%d/%zu)\n", transferred,
	       sizeof(dcp_enable_cmd));
	return -1;
    }

    pr_verbose("DCP mode enabled\n");
    return 0;
}

/* Returns number of bytes in output buffer */
static int process_data(const uint8_t *rx_buf, int rx_len, uint8_t *dx_buf,
			int *volume, int *volume_changed)
{
    const uint8_t *s = rx_buf;
    uint8_t *d = dx_buf;
    static int esc = 0;
    int new_volume = *volume;
    *volume_changed = 0;

    /* Escape with 0x19(EM). */
    for (int i = rx_len; i > 0; i--) {
	if (esc) {
	    if (*s == 0x19) {
		*d++ = *s;
	    } else if ((*s & 0x0f) == 1) {
		/* Note: volume was unused. */
		new_volume = (*s >> 4) & 7;
		if (new_volume != *volume) {
		    *volume = new_volume;
		    *volume_changed = 1;
		}
	    } else {
		pr_verbose("Unknown EM: 0x%02x\n", *s);
	    }
	    esc = 0;
	    s++;
	    continue;
	}

	if (*s == 0x19) {
	    esc = 1;
	    s++;
	    continue;
	}

	*d++ = *s++;
    }

    if (esc) {
	pr_verbose("Warning: Trailing escape character\n");
    }

    /* Got BE16 from modem, convert to LE16 */
    int out_len = d - dx_buf;
    uint16_t *w = (uint16_t *)dx_buf;
    for (int i = out_len / 2; i > 0; i--, w++) {
	*w = __builtin_bswap16(*w);
    }

    return out_len;
}

int do_pcm_rx(libusb_device_handle *handle)
{
    uint8_t rx_buf[RX_BUF_SIZE];
    uint8_t dx_buf[DX_BUF_SIZE];
    int volume = 0;
    int ret = 0;

    while (running) {
	int transferred;
	int volume_changed;
	int out_len;

	ret = libusb_bulk_transfer(handle, EP_IN, rx_buf, sizeof(rx_buf),
				   &transferred, USB_TIMEOUT_MS);

	if (!running)
	    break;

	if (ret == LIBUSB_ERROR_TIMEOUT) {
	    /* Timeout is normal, just retry */
	    continue;
	}

	if (ret < 0) {
	    pr_err("Error: USB read failed: %s\n", libusb_error_name(ret));
	    break;
	}

	if (transferred == 0)
	    continue;

	/* Process data */
	out_len = process_data(rx_buf, transferred, dx_buf, &volume,
			       &volume_changed);

	if (volume_changed) {
	    pr_verbose("Volume: %d\n", volume);
	}

	if (out_len > 0) {
	    ssize_t written = write(STDOUT_FILENO, dx_buf, out_len);
	    if (written < 0) {
		pr_err("Error: Write to stdout failed\n");
		break;
	    }
	}
    }
    return ret;
}

DEFINE_FREE(libusb_ctx, libusb_context *, if (_T) libusb_exit(_T));

int main(int argc, char *argv[])
{
    libusb_device_handle *handle __free(libusb_devhandle) = NULL;
    libusb_context *ctx __free(libusb_ctx) = NULL;
    unsigned int flags = 0;
    int opt;
    int ret;

    while ((opt = getopt(argc, argv, "vfh")) != -1) {
	switch (opt) {
	case 'v':
	    flags |= OPT_VERBOSE;
	    break;
	case 'f':
	    flags |= OPT_FORCE;
	    break;
	case 'h':
	    print_usage(argv[0]);
	    return 0;
	default:
	    print_usage(argv[0]);
	    return 1;
	}
    }
    set_verbose(flags);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ret = libusb_init(&ctx);
    if (ret < 0) {
	pr_err("Error: Failed to initialize libusb: %s\n",
	       libusb_error_name(ret));
	return 1;
    }

    handle = find_and_open_device(ctx);
    if (!handle)
	return 1;

    ret = configure_device(handle, flags);
    if (ret < 0)
	return ret;

    ret = enable_dcp_mode(handle);
    if (ret < 0)
	return ret;

    pr_verbose("Starting audio stream, ^C to quit\n");

    ret = do_pcm_rx(handle);

    return ret;
}
