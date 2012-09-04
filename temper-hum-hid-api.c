/**
 * @author Oleg Stepura <oleg.stepura@gmail.com>
 * @copyright Copyright (c) Oleg Stepura
 * @version $Id$
 */

#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include "temper-hum-hid-api.h"
#include <unistd.h>

#define VENDOR_ID  0x1130
#define PRODUCT_ID 0x660c
#define INTERFACE  1
#define DEFAULT_SENSOR_VOLTAGE 3.5
#define DEFAULT_MEASUREMENT_RESOLUTION_TEMPERATURE 14
#define DEFAULT_MEASUREMENT_RESOLUTION_HUMIDITY 12

static libusb_context *usb_context = NULL;

static struct temperhum_options temperhum_options;
static temperhum_device *temperhum_root_device = NULL;
static FILE * debug_output;

/**
 * Initialize syslog
 */
static void temperhum_init_syslog()
{
	openlog("temper-hum-hid", LOG_PID | LOG_CONS, LOG_USER);
	temperhum_options.syslog_initialized = 1;
}

/**
 * Write some debug data if any debug channel is active
 */
void temperhum_debug(const char* format, ...)
{
	if (!temperhum_options.debug && !temperhum_options.syslog) {
		return;
	}

	va_list args;
	char message[128];

    va_start(args, format);
    vsprintf(message, format, args);
    va_end(args);

	if (temperhum_options.debug) {
		fputs(message, debug_output);
		fputs("\n", debug_output);
		fflush(debug_output);
	}
	if (temperhum_options.syslog) {
		syslog(LOG_DEBUG, "%s", message);
	}
}

/**
 * Write down an error to stderr and syslog
 */
void temperhum_error(int exit_program, const char* format, ...)
{
	va_list args;
	char message[256];

    va_start(args, format);
    vsprintf(message, format, args);
    va_end(args);

	fprintf(stderr, "Error: %s\n", message);
	if (!temperhum_options.syslog_initialized) {
		temperhum_init_syslog();
	}
	syslog(LOG_ERR, "%s", message);

	if (exit_program) {
		temperhum_close();
		exit(-1);
	}
}

/**
 * Debug what bytes have been actually sent or recieved
 */
void temperhum_debug_bytes(unsigned char * data, int length)
{
	if (!temperhum_options.debug && !temperhum_options.syslog) {
		return;
	}

	int i;
	char byte_sequence[32] = {0};
	char current_byte[3];
	for (i = 0; i < length; i++) {
		if ((i % 8) == 0) {
			if (i > 0) {
				temperhum_debug("  0x%02X:%s", i - 8, byte_sequence);
				byte_sequence[0] = 0;
			}
		}
		// make data byte an unsigned int and then pass it to sprintf
		sprintf(current_byte, " %02X", data[i] & 0xFF);
		strcat(byte_sequence, current_byte);
	}
	temperhum_debug("  0x%02X:%s", i - 8, byte_sequence);
}

/**
 * Close temperhum root device
 */
void temperhum_close_devices()
{
	if (temperhum_root_device) {
		temperhum_device *d = temperhum_root_device;
		while (d) {
			temperhum_device *next = d->next;

			temperhum_debug("Releasing interface %u", d->interface_number);
			libusb_release_interface(d->handle, d->interface_number);
			if (d->kernel_driver_detached) {
				temperhum_debug("Attaching kernel driver back at interface %u", d->interface_number);
				libusb_attach_kernel_driver(d->handle, d->interface_number);
			}

			temperhum_debug("Closing usb device handle");
			libusb_close(d->handle);
			free(d);
			d = next;
		}
		temperhum_root_device = NULL;
	}
}

/**
 * Initialize temperhum
 */
void temperhum_init(int print_debug_messages, int send_debug_to_syslog, char * debug_filename)
{
	temperhum_options.debug = print_debug_messages;
	temperhum_options.syslog = send_debug_to_syslog;
	temperhum_options.syslog_initialized = 0;
	
	if (strlen(debug_filename)) {
		debug_output = fopen(debug_filename, "a");
	} else {
		debug_output = stdout;
	}

	if (temperhum_options.syslog) {
		temperhum_init_syslog();
	}

	if (!usb_context) {
		temperhum_debug("Init usb context");
		if (libusb_init(&usb_context)) {
			temperhum_error(1, "Cannot init libusb");
		}

		if (temperhum_options.debug) {
			libusb_set_debug(usb_context, 3);
		} else {
			libusb_set_debug(usb_context, 0);
		}
	}
}

/**
 * Close temperhum
 */
void temperhum_close()
{
	if (temperhum_options.syslog_initialized) {
		closelog();
	}

	temperhum_close_devices();
	
	if (usb_context) {
		temperhum_debug("Exit usb context");
		libusb_exit(usb_context);
		usb_context = NULL;
	}

	if (debug_output && debug_output != stdout) {
		fclose(debug_output);
	}
}

/**
 * Finds all matching temperhum devices
 */
temperhum_device * temperhum_find()
{
	if (temperhum_root_device) {
		return temperhum_root_device;
	}

	libusb_device **devs;
	libusb_device *dev;

	temperhum_device *current_device = NULL;

	int num_devs = libusb_get_device_list(usb_context, &devs);
	if (num_devs < 0) {
		return NULL;
	}

	temperhum_debug("Found %i usb devices", num_devs);

	int i = 0, j = 0, k = 0, res;
	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		struct libusb_config_descriptor *conf_desc = NULL;

		res = libusb_get_device_descriptor(dev, &desc);
		if (desc.idVendor != VENDOR_ID || desc.idProduct != PRODUCT_ID) {
			temperhum_debug("Skipping device %04x:%04x", desc.idVendor, desc.idProduct);
			continue;
		}
		
		uint8_t bus_number = libusb_get_bus_number(dev);
		uint8_t device_number = libusb_get_device_address(dev);
		temperhum_debug("Using device %04x:%04x @ %03u:%03u", desc.idVendor, desc.idProduct, bus_number, device_number);

		res = libusb_get_active_config_descriptor(dev, &conf_desc);
		if (res < 0) {
			libusb_get_config_descriptor(dev, 0, &conf_desc);
		}

		if (!conf_desc) {
			continue;
		}

		temperhum_debug("Using config %u", conf_desc->bConfigurationValue);

		for (j = 0; j < conf_desc->bNumInterfaces; j++) {
			const struct libusb_interface *intf = &conf_desc->interface[j];

			for (k = 0; k < intf->num_altsetting; k++) {
				const struct libusb_interface_descriptor *intf_desc = &intf->altsetting[k];

				if (intf_desc->bInterfaceNumber != INTERFACE) {
					temperhum_debug("Skipping interface %u", intf_desc->bInterfaceNumber);
					continue;
				}

				temperhum_device *tmp;
				tmp = calloc(1, sizeof(temperhum_device));
				tmp->next = NULL;
				tmp->device = dev;
				tmp->interface_number = intf_desc->bInterfaceNumber;
				tmp->kernel_driver_detached = 0;
				tmp->bus_number = bus_number;
				tmp->device_number = device_number;

				temperhum_debug("Using interface %u", tmp->interface_number);

				res = libusb_open(dev, &tmp->handle);
				if (res < 0) {
					temperhum_debug("Warning: cannot open usb device at interface %u", tmp->interface_number);
					continue;
				}

				temperhum_debug("Opened usb device");

				res = libusb_kernel_driver_active(tmp->handle, tmp->interface_number);
				if (res == 1) {
					temperhum_debug("Kernel has active driver on a device, detaching");
					res = libusb_detach_kernel_driver(tmp->handle, tmp->interface_number);
					if (res < 0) {
						temperhum_debug("Warning: cannot detach kernel driver at interface %u", tmp->interface_number);
						libusb_close(tmp->handle);
						continue;
					}
					tmp->kernel_driver_detached = 1;
				}

				res = libusb_claim_interface(tmp->handle, tmp->interface_number);
				if (res < 0) {
					temperhum_debug("Warning: cannot claim interface %u", tmp->interface_number);
					libusb_close(tmp->handle);
					continue;
				}

				temperhum_debug("Claimed interface %u", tmp->interface_number);

				if (current_device) {
					current_device->next = tmp;
				} else {
					temperhum_root_device = tmp;
				}
				current_device = tmp;
			}
		}

		libusb_free_config_descriptor(conf_desc);
	}

	libusb_free_device_list(devs, 1);
	temperhum_debug("Finished listing devices");

	return temperhum_root_device;
}

/**
 * Reset temperhum root device
 */
void temperhum_reset_devices()
{
	int res, devices_existed = 0;
	if (!temperhum_root_device) {
		temperhum_find();
	} else {
		devices_existed = 1;
	}

	if (temperhum_root_device) {
		temperhum_device *d = temperhum_root_device;
		while (d) {
			temperhum_device *next = d->next;

			temperhum_debug("Resetting device @ %03u:%03u", d->bus_number, d->device_number);
			libusb_release_interface(d->handle, d->interface_number);
			res = libusb_reset_device(d->handle);
			if (res < 0) {
				temperhum_debug("Warning: cannot reset device");
			}
			d = next;
		}
		temperhum_close_devices();
		if (devices_existed) {
			temperhum_find();
		}
	}

	// After powerup the device needs 11ms to reach its 
	// "sleep" state. No commands should be sent before that time. 
	usleep(20000);
}

/**
 * Send a command to temperhum device
 */
int temperhum_send(temperhum_device * device, unsigned char * request, int length)
{
	temperhum_debug("Sending %i bytes of data to interface %u of USB device at %03u:%03u:", length, device->interface_number, device->bus_number, device->device_number);
	temperhum_debug_bytes(request, length);
	
	int size = libusb_control_transfer(
		device->handle, 
		LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
		0x09, // HID Set_Report
		2 << 8, // HID output
		device->interface_number,
		request,
		length,
		1000
	);

	if (size <= 0) {
		temperhum_error(0, "Writing to temperhum @ %03u:%03u failed: %i", device->bus_number, device->device_number, size);

		return -1;
	} else if (size != length) {
		temperhum_error(0, "Written to temperhum only %i of %i bytes", size, length);

		return -1;
	}

	temperhum_debug("Written %i bytes", size);
	return size;
}

/**
 * Read data from temperhum device
 */
int temperhum_recieve(temperhum_device * device, unsigned char * response, int length)
{	
	int size = libusb_control_transfer(
		device->handle, 
		LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_IN,
		0x01, // HID Get_Report
		3 << 8, // HID input
		device->interface_number,
		response,
		length,
		1000
	);

	if (size < 0) {
		temperhum_error(0, "Read of data from the sensor failed at interafce %u: %i", device->interface_number, size);

		return size;
	} else if (size == 0) {
		temperhum_error(0, "No data was read from the sensor at interface %u (timeout)", device->interface_number);

		return -1;
	}

	if (size == length) {
		temperhum_debug("Warning: data buffer full, may have lost some data");
	}
	temperhum_debug("Read %i bytes of data:", size);
	temperhum_debug_bytes(response, size);

	return size;
}

/**
 * Issue a query to temperhum device sending a request command and reading response data
 */
int temperhum_request(temperhum_device * device, unsigned char * request, unsigned char * response, int request_length, int response_length)
{
	unsigned char command[] = {
		0x0A, 0x0B, 0x0C, 0x0D, 0x00, 0x00, 0x02, 0x00, // issue a command
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // request
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // padding to clear the i2c bus as per the Philips i2c spec, x7
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x0A, 0x0B, 0x0C, 0x0D, 0x00, 0x00, 0x01, 0x00  // query command
	};

	int i;
	for (i = 0; i < request_length; i++) {
		command[i + 8] = request[i];
	}

	int res;
	//unsigned char subcommand[8];
	//int size = sizeof(subcommand);
	//for (i = 0; i < 10; i++) {
	//	memcpy(subcommand, &command[i * size], size);
	//	res = temperhum_send(device, subcommand, size);
	//	if (res < 0) {
	//		return res;
	//	}
	//}

	res = temperhum_send(device, command, sizeof(command));
	if (res < 0) {
		return res;
	}
	
	/** 
	 * According to Sensirion datasheet for SHT1x the time for
	 * 8/12/14 bit measurements is 20/80/320 ms. Trial and error
	 * suggests that sleeping less that 400ms can produce spurious
	 * measurements
	 */
	usleep(400000);
	
	return temperhum_recieve(device, response, response_length);
}

/**
 * Fill temperature value in a temperhum device struct when raw data is read
 */
void temperhum_sht1x_fill_temperature(temperhum_device * device)
{
	/**
	 * This calculation is based on the FM75 datasheet, and converts
	 * from two separate data bytes to a single integer, which is
	 * needed for all currently supported temperature sensors.
	 * BUT it is clarified by masterb here: http://relavak.wordpress.com/2009/10/17/temper-temperature-sensor-linux-driver/
	 */
	int most_significant_byte = device->raw_temperature_bytes[0] << 8;
	int last_significant_byte = device->raw_temperature_bytes[1] & 0xFF; // makes it an unsigned int
	device->raw_temperature = most_significant_byte + last_significant_byte;
	temperhum_debug("Raw temperature read: %i, msb: %i, lsb: %i", device->raw_temperature, most_significant_byte, last_significant_byte);

	/**
	 * Datasheet SHT1x (SHT10, SHT11, SHT15)
	 * Humidity and Temperature Sensor IC:
	 * 
	 * The band-gap PTAT (Proportional To Absolute 
     * Temperature) temperature sensor is very linear by design. 
	 * Use the following formula to convert digital readout (SOT) 
	 * to temperature value, with coefficients given in Table 8.
	 *   T = D1 + D2 * SOT;
	 * Table 8.1:
	 * +---------+-------+-------+-------+-------+-------+
	 * | VDD ->  |    5V |    4V |  3.5V |    3V |  2.5V |
	 * +---------+-------+-------+-------+-------+-------+
	 * | D1 (°C) | -40.1 | -39.8 | -39.7 | -39.6 | -39.4 |
	 * +---------+-------+-------+-------+-------+-------+
	 * | D1 (°F) | -40.2 | -39.6 | -39.5 | -39.3 | -38.9 |
	 * +---------+-------+-------+-------+-------+-------+
	 * Table 8.2:
	 * +---------+-------+-------+
	 * | SOT ->  | 14bit | 12bit |
	 * +---------+-------+-------+
	 * | D2 (°C) |  0.01 |  0.04 |
	 * +---------+-------+-------+
	 * | D2 (°F) | 0.018 | 0.072 |
	 * +---------+-------+-------+
	 */
	float D1, D2;
	if (!device->sensor_voltage) {
		device->sensor_voltage = DEFAULT_SENSOR_VOLTAGE;
	}
	if (device->sensor_voltage == 2.5) {
		D1 = -39.4;
	} else if (device->sensor_voltage > 2.5 && device->sensor_voltage <= 3.0) {
		D1 = -39.6;
	} else if (device->sensor_voltage > 3.0 && device->sensor_voltage <= 3.5) {
		D1 = -39.7;
	} else if (device->sensor_voltage > 3.5 && device->sensor_voltage <= 4.0) {
		D1 = -39.8;
	} else if (device->sensor_voltage > 4.0 && device->sensor_voltage <= 5.0) {
		D1 = -40.1;
	} else {
		temperhum_error(1, "Wrong value for sensor voltage: %.1f", device->sensor_voltage);
	}

	if (!device->measurement_resolution_temperature) {
		device->measurement_resolution_temperature = DEFAULT_MEASUREMENT_RESOLUTION_TEMPERATURE;
	}
	if (device->measurement_resolution_temperature == 14) {
		D2 = 0.01;
	} else if (device->measurement_resolution_temperature == 12) {
		D2 = 0.04;
	} else {
		temperhum_error(1, "Wrong value of measurement resolution for temperature: %i", device->measurement_resolution_temperature);
	}

	device->temperature = D1 + D2 * device->raw_temperature;
	temperhum_debug("Compensated temperature: %.2f", device->temperature);
}

/**
 * Fill humidity value in a temperhum device struct when raw data is read
 */
void temperhum_sht1x_fill_humidity(temperhum_device * device)
{
	/**
	 * @see temperhum_sht1x_fill_temperature()
	 */
	int most_significant_byte = (device->raw_humidity_bytes[0] & 0xFF) << 8; // make it unsigned int and shift left 8 bits
	int last_significant_byte = device->raw_humidity_bytes[1] & 0xFF; // make it unsigned int
	device->raw_humidity = most_significant_byte + last_significant_byte;
	temperhum_debug("Raw humidity read: %i, msb: %i, lsb: %i", device->raw_humidity, most_significant_byte, last_significant_byte);

	/**
	 * Datasheet SHT1x (SHT10, SHT11, SHT15)
	 * Humidity and Temperature Sensor IC:
	 * 
	 * For compensating non-linearity of the humidity sensor
	 * and for obtaining the full accuracy of the 
	 * sensor it is recommended to convert the humidity readout
	 * (SORH) with the following formula with coefficients given in 
	 * Table 6.
	 *   RH_linear = C1 + C2 * SORH + C3 * (SORH ^ 2)
	 * Table 6:
	 * +---------+------------+------------+
	 * | SORH -> |     12 bit |      8 bit |
	 * +---------+------------+------------+
	 * | C1      |    -2.0468 |    -2.0468 |
	 * +---------+------------+------------+ 
	 * | C2      |     0.0367 |     0.5872 |
	 * +---------+------------+------------+
	 * | C3      | -1.5955E-6 | -4.0845E-4 |
	 * +---------+------------+------------+
	 */
	double C1, C2, C3;
	if (!device->measurement_resolution_humidity) {
		device->measurement_resolution_humidity = DEFAULT_MEASUREMENT_RESOLUTION_HUMIDITY;
	}
	C1 = -2.0468;
	if (device->measurement_resolution_humidity == 12) {
		C2 = 0.0367;
		C3 = -1.5955e-6;
	} else if (device->measurement_resolution_humidity == 8) {
		C2 = 0.5872;
		C3 = -4.0845e-4;
	} else {
		temperhum_error(1, "Wrong value of measurement resolution for humidity: %i", device->measurement_resolution_humidity);
	}

	double humidity_linear = C1 + C2 * device->raw_humidity + C3 * device->raw_humidity * device->raw_humidity;
	if (humidity_linear < 0) {
		humidity_linear = 0;
	}
	if (humidity_linear > 99) {
		humidity_linear = 100;
	}
	temperhum_debug("Linear humidity: %.4f", humidity_linear);

	/**
	 * Datasheet SHT1x (SHT10, SHT11, SHT15)
	 * Humidity and Temperature Sensor IC:
	 * 
	 * For temperatures significantly different from 25°C (~77°F) 
	 * the humidity signal requires temperature compensation.
	 * The temperature correction corresponds roughly to 
	 * 0.12%RH/°C @ 50%RH. Coefficcients for the temperature 
	 * compensation are given in Table 7. 
	 *   RH = (TempC - 25) * (T1 + T2 * SORH) + RH_linear
	 * +---------+---------+---------+
	 * | SORH -> |  12 bit |   8 bit |
	 * +---------+---------+---------+
	 * | T1      |    0.01 |    0.01 |
	 * +---------+---------+---------+ 
	 * | T2      | 0.00008 | 0.00128 |
	 * +---------+---------+---------+
	 */
	float T2, T1 = 0.01;
	if (device->measurement_resolution_humidity == 12) {
		T2 = 0.00008;
	} else if (device->measurement_resolution_humidity == 8) {
		T2 = 0.00128;
	} 
	device->humidity = (device->temperature - 25) * (T1 + T2 * device->raw_humidity) + humidity_linear;
	temperhum_debug("Compensated humidity: %.4f", device->humidity);
}

/**
 * Fill values in a temperhum device struct issuing a request command to read data from device
 */
int temperhum_fill(temperhum_device * device)
{
	unsigned char request[] = {0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned char response[512];
	unsigned char init_request[] = {0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned char init_response[512];
	
	time_t rawtime;
	struct tm * timeinfo;
	char time_string[24];

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(time_string, 24, "%Y-%m-%d %H:%M:%S", timeinfo);
	temperhum_debug("==== %s ====", time_string);

	bzero(device->raw_temperature_bytes, sizeof(device->raw_temperature_bytes));
	bzero(device->raw_humidity_bytes, sizeof(device->raw_humidity_bytes));

	int res = temperhum_request(device, init_request, init_response, sizeof(init_request), sizeof(init_response));
	if (res < 0) {
		return res;
	}

	res = temperhum_request(device, request, response, sizeof(request), sizeof(response));
	if (res < 0) {
		return res;
	}

	// If 5th - 8th bytes are FFs device reports bad data (found that trial and error)
	if (response[4] == 0xFF) {
		temperhum_error(0, "Returned data appears to be wrong");
	}

	// If only zeros returned that is an error
	if (response[0] == 0x00 && response[1] == 0x00 && response[2] == 0x00 && response[3] == 0x00) {
		temperhum_error(1, "Returned data appears to be wrong (only zeros returned)");
	}

	device->raw_temperature_bytes[0] = response[0];
	device->raw_temperature_bytes[1] = response[1];
	temperhum_debug("Raw temperature bytes: {0x%02X, 0x%02X}", device->raw_temperature_bytes[0] & 0xFF, device->raw_temperature_bytes[1] & 0xFF);
	temperhum_sht1x_fill_temperature(device);

	device->raw_humidity_bytes[0] = response[2];
	device->raw_humidity_bytes[1] = response[3];
	temperhum_debug("Raw humidity bytes: {0x%02X, 0x%02X}", device->raw_humidity_bytes[0] & 0xFF, device->raw_humidity_bytes[1] & 0xFF);
	temperhum_sht1x_fill_humidity(device);

	/**
	 * SHT1x is not measuring dew point directly, however dew 
	 * point can be derived from humidity and temperature 
	 * readings. Since humidity and temperature are both 
	 * measured on the same monolithic chip, the SHT1x allows 
	 * superb dew point measurements.  
	 * For dew point (Td) calculations there are various formulas 
	 * to be applied, most of them quite complicated. For  the 
	 * temperature range of -40 - 50°C the following 
	 * approximation provides good accuracy with parameters 
	 * given in Table 9:
	 * +-----------------------+---------+-------+
	 * | Temperature Range     | Tn (°C) |   m   |
	 * +-----------------------+---------+-------+
	 * | Above water, 0 - 50°C |  243.12 | 17.62 |
	 * +-----------------------+---------+-------+
	 * | Above ice, -40 - 0°C  |  272.62 | 22.46 |
	 * +-----------------------+---------+-------+
	 */
	double Tn = 243.12;
	double m = 17.62;
	if (device->temperature < 0) {
		Tn = 272.62;
		m = 22.46;
	}
	double gamma = log(device->humidity / 100) + m * device->temperature / (Tn + device->temperature);
	device->dew_point = Tn * gamma / (m - gamma);
	temperhum_debug("Calculated dew point: %.2f", device->dew_point);

	return 1;
}
