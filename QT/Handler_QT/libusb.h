#pragma once

/*
 * Prefer libusb-1.0 header. If unavailable or mismatched, provide
 * a small libusb-1.0 API fallback for symbols used in this project.
 */
#if __has_include(<libusb-1.0/libusb.h>)
#include <libusb-1.0/libusb.h>
#elif defined(__GNUC__)
#include_next <libusb.h>
#else
#define LIBUSB_SHIM_NO_SYSTEM_HEADER 1
#endif

#if !defined(LIBUSB_API_VERSION)
#ifdef __cplusplus
extern "C" {
#endif

typedef struct libusb_context libusb_context;
typedef struct libusb_device_handle libusb_device_handle;

enum libusb_error
{
	LIBUSB_SUCCESS = 0,
	LIBUSB_ERROR_IO = -1,
	LIBUSB_ERROR_INVALID_PARAM = -2,
	LIBUSB_ERROR_ACCESS = -3,
	LIBUSB_ERROR_NO_DEVICE = -4,
	LIBUSB_ERROR_NOT_FOUND = -5,
	LIBUSB_ERROR_BUSY = -6,
	LIBUSB_ERROR_TIMEOUT = -7,
	LIBUSB_ERROR_OVERFLOW = -8,
	LIBUSB_ERROR_PIPE = -9,
	LIBUSB_ERROR_INTERRUPTED = -10,
	LIBUSB_ERROR_NO_MEM = -11,
	LIBUSB_ERROR_NOT_SUPPORTED = -12,
	LIBUSB_ERROR_OTHER = -99
};

int libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);
libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx, unsigned short vendor_id, unsigned short product_id);
int libusb_set_auto_detach_kernel_driver(libusb_device_handle *dev_handle, int enable);
int libusb_claim_interface(libusb_device_handle *dev_handle, int interface_number);
int libusb_release_interface(libusb_device_handle *dev_handle, int interface_number);
void libusb_close(libusb_device_handle *dev_handle);
int libusb_clear_halt(libusb_device_handle *dev_handle, unsigned char endpoint);
int libusb_control_transfer(libusb_device_handle *dev_handle,
							unsigned char bmRequestType,
							unsigned char bRequest,
							unsigned short wValue,
							unsigned short wIndex,
							unsigned char *data,
							unsigned short wLength,
							unsigned int timeout);
int libusb_bulk_transfer(libusb_device_handle *dev_handle,
						 unsigned char endpoint,
						 unsigned char *data,
						 int length,
						 int *transferred,
						 unsigned int timeout);
const char *libusb_error_name(int errcode);

#ifdef __cplusplus
}
#endif
#endif
