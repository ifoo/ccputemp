/* ccputemp.c
 *
 * A Linux C port of Scott Williams' py-cputemp (http://sourceforge.net/projects/py-cputemp).
 *
 * Copyright 2012 Philip Pum <philippum@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <float.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>

// thanks to David Titarenco for typesafe min/max macros (http://stackoverflow.com/questions/3437404/min-and-max-in-c)
#define max(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })
#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })

enum temp_unit_t { CELSIUS, FAHRENHEIT, KELVIN};

static const enum temp_unit_t g_default_temp_unit = CELSIUS;
static const int g_default_runtime_secs = 5;
static const char * const g_default_log_file = "/var/log/cputemp.log";

static const char * const g_thermal_path_list[] = {
		"/sys/devices/LNXSYSTM:00/LNXTHERM:00/LNXTHERM:01/thermal_zone/temp",
		"/sys/bus/acpi/devices/LNXTHERM:00/thermal_zone/temp",
		"/proc/acpi/thermal_zone/THM0/temperature",
		"/proc/acpi/thermal_zone/THRM/temperature",
		"/proc/acpi/thermal_zone/THR1/temperature"
};

static const size_t g_thermal_path_list_len = sizeof(g_thermal_path_list);

// TODO: dont hardcode default seconds and default temperature unit.
// use g_default_runtime_secs and g_default_temp_unit instead.
static const char * const g_help_str =
"C port of cputemp (http://sourceforge.net/projects/py-cputemp)\n"\
"\n"\
"Usage:\n"\
" ccputemp [options]\n"\
"\n"\
"Options:\n"\
"-h, --help		display this help and exit\n"\
"-v, --version		output version information and exit\n"\
"-a, --average		display only the results (use with -s and [-F, -C or -K])\n"\
"-s, --seconds [s]	run ccputemp for specified number of seconds (default is 5)\n"\
"-C, --celsius		display temperature in degree Celsius (default)\n"\
"-F, --fahrenheit	display temperature in degree Fahrenheit\n"\
"-K, --kelvin		display temperature in degree Kelvin\n"\
"\n"\
"Visit http://github.com/ccputemp for more information.";

static const char * const g_version_str = "ccputemp v0.1 by Philip Pum (http://github.com/ccputemp)";

static int g_ctrl_c_signal = 0;

static float ccputemp_convert_unit_from_celsius(float val, enum temp_unit_t unit) {
	return (unit == CELSIUS) ? val : (unit == FAHRENHEIT) ? 1.8f * val + 32.0f : val + 273.15f;
}

static int ccputemp_get_thermal_value_from_file(const char * const src_file, float * const val) {
	FILE * file_handle = NULL;
	const size_t tmp_buf_len = 32;
	char tmp_buf[tmp_buf_len];

	if(!src_file || !val)
		return -1;

	file_handle = fopen(src_file, "r");
	if(!file_handle)
		return -1;

	memset(tmp_buf, 0, tmp_buf_len);
	if(fgets(tmp_buf, tmp_buf_len, file_handle) == NULL) {
		fclose(file_handle);
		return -1;
	}

	fclose(file_handle);

	*val = (float)(atoi(tmp_buf) / 1000);
	return 0;
}

static const char * const ccputemp_get_thermal_path(void) {
	struct stat fstat;
	size_t i=0;

	for(i=0; i<g_thermal_path_list_len; i++)
		if(stat(g_thermal_path_list[i], &fstat) == 0)
			return g_thermal_path_list[i];

	return NULL;
}

static int ccputemp_get_unit_temp_from_file(const char * const src_file, enum temp_unit_t unit, float * const val) {
	if(!src_file || !val)
		return -1;

	if(ccputemp_get_thermal_value_from_file(src_file, val) == -1)
		return -1;

	*val = ccputemp_convert_unit_from_celsius(*val, unit);

	return 0;
}

static void ccputemp_show_help() {
	printf("%s\n", g_help_str);
}

static void ccputemp_show_version() {
	printf("%s\n", g_version_str);
}

static void ccputemp_show_multiple_units() {
	fprintf(stderr, "Multiple temperature units specified. Use only one unit (-C, -F or -K).\nExiting...\n");
}

static const char * const ccputemp_temp_unit_t_to_str(enum temp_unit_t unit) {
	return (unit == CELSIUS) ? "Celsius" : (unit == FAHRENHEIT) ? "Fahrenheit" : "Kelvin";
}

static int ccputemp_do_avg(int opt_unit, enum temp_unit_t unit, int seconds) {
	float temp_sum = 0.0f;
	float cur_val = 0.0f;
	size_t i=0, j=0;
	const char * const src_path = ccputemp_get_thermal_path();

	ccputemp_show_version();

	if(!src_path) {
		fprintf(stderr, "Can not find a valid data source in /sys or /proc. Possible sources:\n");
		for(j=0; j<g_thermal_path_list_len; j++)
			fprintf(stderr, "\t%s\n", g_thermal_path_list[j]);
		return -1;
	}

	if(seconds < 1)
		seconds = g_default_runtime_secs;

	for(i=0; i<(size_t)(seconds); i++) {
		if(ccputemp_get_unit_temp_from_file(src_path, unit, &cur_val) == -1) {
			fprintf(stderr, "Error reading temperature data from '%s'. Exiting...\n", src_path);
			return -1;
		}

		temp_sum += cur_val;

		sleep(1);
	}

	printf("Average temperature was %.1f deegrees %s.\n", temp_sum / (float)(seconds), ccputemp_temp_unit_t_to_str(unit));
	return 0;
}

static void ccputemp_handle_signal(int s) {
	if(s == SIGINT)
		g_ctrl_c_signal = 1;
}

static void ccputemp_do_log(const char * const filename, float temp_avg, float temp_min, float temp_max, enum temp_unit_t unit, int secs) {
	FILE * file_handle = NULL;
	struct stat fstat;
	time_t t = time(NULL);
	struct tm * tm = localtime(&t);

	if(!filename)
		return;

	if(stat(filename, &fstat) != 0) {
		fprintf(stderr, "Could not locate log file '%s'.", filename);
		return;
	}

	file_handle = fopen(filename, "a");
	if(!file_handle) {
		fprintf(stderr, "Can not open log file '%s'.", filename);
	}

	const char * const unit_str = ccputemp_temp_unit_t_to_str(unit);

	fprintf(file_handle, "Session started at %d-%d-%d %d:%d:%d :\nccputime was run for %d seconds.\nHighest recorded temperature was %f degrees %s.\nLowest recorded temperature was %f degrees %s.\nAverage recorded temperature was %f degrees %s.\n---------------\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, secs, temp_max, unit_str, temp_min, unit_str, temp_avg, unit_str);
	fclose(file_handle);
	printf("Log has been updated.\n");
}

static int ccputemp_do_normal(int opt_unit, enum temp_unit_t unit, int seconds, int opt_seconds) {
	float temp_sum = 0.0f, temp_min = FLT_MAX, temp_max = FLT_MIN, cur_val = 0.0f;
	size_t i = 0;
	char input_c = '\0';
	struct sigaction sigIntHandler;
	const char * const src_path = ccputemp_get_thermal_path();

	ccputemp_show_version();

	if(!src_path) {
		fprintf(stderr, "Can not find a valid data source in /sys or /proc. Exiting...\n");
		return -1;
	}

	if(opt_unit == 0) {
		while(input_c != 'c' && input_c != 'f' && input_c != 'k') {
			printf("Set temperature unit: (c)elsius, (f)ahrenheit or (k)elvin: ");
			fscanf(stdin, "%c", &input_c);
			printf("\n");
			input_c = tolower(input_c);
		}

		unit = (input_c == 'c') ? CELSIUS : (input_c == 'f') ? FAHRENHEIT : KELVIN;
	}

	if(opt_seconds && seconds < 1)
		seconds = g_default_runtime_secs;

	// thanks to Gab Royer (http://stackoverflow.com/questions/1641182/how-can-i-catch-a-ctrl-c-event-c)
	sigIntHandler.sa_handler = ccputemp_handle_signal;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);

	const char * const unit_str = ccputemp_temp_unit_t_to_str(unit);

	while(g_ctrl_c_signal == 0) {
		if(opt_seconds && i >= seconds)
			break;
		if(ccputemp_get_unit_temp_from_file(src_path, unit, &cur_val) == -1) {
			fprintf(stderr, "Error reading temperature data from '%s'. Exiting...\n", src_path);
			return -1;
		}

		temp_sum += cur_val;
		temp_min = min(temp_min, cur_val);
		temp_max = max(temp_max, cur_val);

		printf("CPU Temperature: %f %s (Time running: %u secs)\n", temp_sum / (float)(i+1), unit_str, (unsigned int)i+1);
		i++;
		sleep(1);
	}

	if(i > 0) {
		printf("\nHighest recorded temperature was %f degrees %s.\nLowest recorded temperature was %f degrees %s.\nAverage recorded temperature was %f degrees %s.\n\n", temp_max, unit_str, temp_min, unit_str, temp_sum / (float)(i+1), unit_str);
		ccputemp_do_log(g_default_log_file, temp_sum / (float)(i+1), temp_min, temp_max, unit, i+1);
	}
	else
		fprintf(stderr, "Not enough measurements collected...\n");

	return 0;
}

int main(int argc, char ** argv) {
	int index = 0;
	int result = 0;

	int opt_help = 0;
	int opt_unit = 0;
	enum temp_unit_t opt_unit_val = g_default_temp_unit;
	int opt_version = 0;
	int opt_seconds = 0;
	int opt_seconds_val = g_default_runtime_secs;
	int opt_avg = 0;

	static struct option long_options[] = {
	            {"help",     	no_argument, 		0, 'h' },
	            {"celsius",  	no_argument, 		0, 'C' },
	            {"fahrenheit",  no_argument, 		0, 'F' },
	            {"kelvin", 		no_argument, 		0, 'K' },
	            {"version",  	no_argument, 		0, 'v' },
	            {"seconds",    	required_argument, 	0, 's' },
	            {"average",    	no_argument, 		0, 'a' },
	            {0,         	0,                 	0,  0  }
	        };

	while(optind < argc) {
		result = getopt_long(argc, argv, "aCFKs:hv", long_options, &index);
		if(result == -1) break;

		switch(result) {
		case 'h':
			opt_help = 1;
			break;

		case 'C':
			if(opt_unit == 1) {
				ccputemp_show_multiple_units();
				return -1;
			}
			opt_unit = 1;
			opt_unit_val = CELSIUS;
			break;

		case 'F':
			if(opt_unit == 1) {
				ccputemp_show_multiple_units();
				return -1;
			}
			opt_unit = 1;
			opt_unit_val = FAHRENHEIT;
			break;

		case 'K':
			if(opt_unit == 1) {
				ccputemp_show_multiple_units();
				return -1;
			}
			opt_unit = 1;
			opt_unit_val = KELVIN;
			break;

		case 'v':
			opt_version = 1;
			break;

		case 's':
			opt_seconds = 1;
			opt_seconds_val = atoi(optarg);
			if(opt_seconds_val < 1)
				opt_seconds_val = g_default_runtime_secs;
			break;

		case 'a':
			opt_avg = 1;
			break;

		default:
			ccputemp_show_help();
			return -1;
		}
	}

	if(opt_help == 1) {
		ccputemp_show_help();
		return 0;
	}

	if(opt_version == 1) {
		ccputemp_show_version();
		return 0;
	}

	return opt_avg == 1 ? ccputemp_do_avg(opt_unit, opt_unit_val, opt_seconds_val) : ccputemp_do_normal(opt_unit, opt_unit_val, opt_seconds_val, opt_seconds);
}
