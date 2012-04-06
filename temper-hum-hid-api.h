/**
 * @author Oleg Stepura <oleg.stepura@gmail.com>
 * @copyright Copyright (c) Oleg Stepura
 * @version $Id$
 */

#ifndef TEMPER_HUM_HID_API
#define TEMPER_HUM_HID_API

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sys/types.h>
#include <libusb.h>

struct temperhum_options {
	int debug; /** print debug messages to screen */
	int syslog; /** send debug messages to syslog */
	int syslog_initialized;
};

struct temperhum_device {
	libusb_device *device;
	libusb_device_handle *handle;
	uint8_t bus_number;
	uint8_t device_number;
	uint8_t interface_number;
	double sensor_voltage;
	int measurement_resolution_temperature;
	int measurement_resolution_humidity;
	char raw_temperature_bytes[2];
	char raw_humidity_bytes[2];
	int raw_temperature;
	int raw_humidity;
	double temperature;
	double humidity;
	double dew_point;
	int kernel_driver_detached;
	struct temperhum_device *next; /** Pointer to the next device */
};

typedef struct temperhum_device temperhum_device;

void temperhum_debug(const char* format, ...);
void temperhum_error(int exit_program, const char* format, ...);
void temperhum_debug_bytes(unsigned char * data, int length);
void temperhum_init(int print_debug_messages, int send_debug_to_syslog, char * debug_filename);
void temperhum_close();
void temperhum_reset_devices();
temperhum_device * temperhum_find();
int temperhum_fill(temperhum_device * device);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* TEMPER_HUM_HID_API */
