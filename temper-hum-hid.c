/**
 * @author Oleg Stepura <oleg.stepura@gmail.com>
 * @copyright Copyright (c) Oleg Stepura
 * @version $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include "temper-hum-hid-api.h"
#include "temper-hum-hid-cmd.h"

struct gengetopt_args_info cmd_args;
FILE * log_file;

/**
 * Opens log file or reopens it if it's already opened
 */
void open_log_file(int exit_on_error)
{
	if (log_file) {
		fclose(log_file);
	}

	if (cmd_args.log_given) {
		log_file = fopen(cmd_args.log_arg, "a");
		if (!log_file) {
			temperhum_error(exit_on_error, "Cannot open log file '%s' for writing (a)", cmd_args.log_arg);
		}
	} else {
		openlog("temper-hum-hid", LOG_PID | LOG_CONS, LOG_USER);
	}
}

/**
 * Prints reading values from all found devices
 */
int temperhum_print_devices(temperhum_device * device)
{
	FILE * out_file;
	char full_report[4096] = {0x00};
	char log_report_data[512];
	int print_result = 1;

	time_t rawtime;
	struct tm * timeinfo;
	char time_string[24];

	while (device != NULL) {
		device->measurement_resolution_temperature = 14;
		device->measurement_resolution_humidity = 12;
		device->sensor_voltage = 3.5;

		char report[512] = {0x00};
		char report_line[128];

		int result = temperhum_fill(device);
		if (result < 0) {
			device = device->next;
			print_result = -1;
			continue;
		}
		
		if (!cmd_args.machine_given) {
			sprintf(report_line, "Temperhum device @ %03u:%03u:\n", device->bus_number, device->device_number);
			strcat(report, report_line);
			sprintf(report_line, "  Temperature: %.2f C\n", device->temperature);
			strcat(report, report_line);
			sprintf(report_line, "  Relative humidity: %.2f %%\n", device->humidity);
			strcat(report, report_line);
			sprintf(report_line, "  Dew point: %.2f C\n", device->dew_point);
			strcat(report, report_line);

			/**
			 * Calculate Human perception for this dew point according to Wikipedia table
			 * @see http://en.wikipedia.org/wiki/Dew_point
			 */
			char * perception;
			if (device->dew_point < 10) {
				perception = "A bit dry for some";
			} else if (10 <= device->dew_point && device->dew_point < 12.5) {
				perception = "Very comfortable";
			} else if (12.5 <= device->dew_point && device->dew_point < 16) {
				perception = "Comfortable";
			} else if (16 <= device->dew_point && device->dew_point < 18) {
				perception = "OK for most, but all perceive the humidity at upper edge";
			} else if (18 <= device->dew_point && device->dew_point < 21) {
				perception = "Somewhat uncomfortable for most people at upper edge";
			} else if (21 <= device->dew_point && device->dew_point < 24) {
				perception = "Very humid, quite uncomfortable";
			} else if (24 <= device->dew_point && device->dew_point < 26) {
				perception = "Extremely uncomfortable, fairly oppressive";
			} else {
				perception = "Severely high! Even deadly for asthma related illnesses";
			}
			sprintf(report_line, "  Human perception: %s\n", perception);
			strcat(report, report_line);

			if ((device->temperature - 2) < device->dew_point && device->dew_point < (device->temperature + 2)) {
				strcat(report, "\n  Warning! Dew point almost same as current temperature.\n  Humid air may condense into liquid water!\n");
			}
		} else {
			sprintf(report_line, "%03u-%03u-i%u-temp: %.2f\n", device->bus_number, device->device_number, device->interface_number, device->temperature);
			strcat(report, report_line);
			sprintf(report_line, "%03u-%03u-i%u-hum: %.2f\n", device->bus_number, device->device_number, device->interface_number, device->humidity);
			strcat(report, report_line);
			sprintf(report_line, "%03u-%03u-i%u-dew: %.2f\n", device->bus_number, device->device_number, device->interface_number, device->dew_point);
			strcat(report, report_line);
		}

		sprintf(
			log_report_data,
			"%03u:%03u-i%u/driver: %i; voltage: %.1f; temperature: %.2f (%i, {0x%02X, 0x%02X}) @ %ibit; humidity: %.2f (%i, {0x%02X, 0x%02X}) @ %ibit; dew point: %.2f",
			device->bus_number,
			device->device_number,
			device->interface_number,
			device->kernel_driver_detached,
			device->sensor_voltage,
			device->temperature,
			device->raw_temperature,
			device->raw_temperature_bytes[0] & 0xFF,
			device->raw_temperature_bytes[1] & 0xFF,
			device->measurement_resolution_temperature,
			device->humidity,
			device->raw_humidity,
			device->raw_humidity_bytes[0] & 0xFF,
			device->raw_humidity_bytes[1] & 0xFF,
			device->measurement_resolution_humidity,
			device->dew_point
		);

		if (log_file) {
			char log_report[512];

			time(&rawtime);
			timeinfo = localtime(&rawtime);
			strftime(time_string, 24, "%Y-%m-%d %H:%M:%S", timeinfo);
			
			sprintf(
				log_report,
				"[%s] TemperHum %s\n",
				time_string,
				log_report_data
			);
			int result = fputs(log_report, log_file);
			if (result < 0) {
				open_log_file(0);
			} else {
				fflush(log_file);
			}
		} else {
			syslog(LOG_INFO, log_report_data);	
		}

		if (cmd_args.repeat_arg) {
			strcat(report, "--------------------------------\n");
		}

		if (cmd_args.out_given) {
			strcat(full_report, report);
		} else {
			printf("%s", report);
		}

		device = device->next;
	}

	if (cmd_args.out_given) {
		out_file = fopen(cmd_args.out_arg, "w");
		if (!out_file) {
			temperhum_error(1, "Cannot open output file '%s' for writing (a)", cmd_args.out_arg);
		}
		fputs(full_report, out_file);
		fclose(out_file);
	}

	return print_result;
}

/**
 * Main logic
 */
int main(int argc, char *argv[])
{
	int result = cmdline_parser(argc, argv, &cmd_args);
	if (result != 0) {
		temperhum_init(0, 0, "\0");
		temperhum_error(1, "Cannot parse command line arguments, error %i", result);
	}

	temperhum_init(cmd_args.verbose_given, cmd_args.syslog_given, cmd_args.verbose_arg);

	//temperhum_reset_devices();

	temperhum_device * device;
	device = temperhum_find();
	open_log_file(1);

	int spent = 0;
	if (cmd_args.repeat_arg) {
		while (1) {
			// force a reset every hour
			if (spent >= 3600) {
				temperhum_debug("1 hour spent, forcing reinitialization of devices");
				temperhum_close();
				temperhum_init(cmd_args.verbose_given, cmd_args.syslog_given, cmd_args.verbose_arg);
				device = temperhum_find();
				spent = 0;
			}

			int result = temperhum_print_devices(device);
			if (result < 0) {
				temperhum_debug("Failures occured during reading, reinitialize devices");
				temperhum_close();
				temperhum_init(cmd_args.verbose_given, cmd_args.syslog_given, cmd_args.verbose_arg);
				device = temperhum_find();
			}

			sleep(cmd_args.repeat_arg);
			spent += cmd_args.repeat_arg;
		}
	} else {
		temperhum_print_devices(device);
	}

	if (log_file) {
		fclose(log_file);
	} else {
		closelog();
	}

	temperhum_close();
	return 0;
}
