/*
 * Copyright (c) 2019 Georg Brein. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include <unistd.h>
#include <sys/time.h>

#include "tnylpo.h"


/*
 * program name for error messages
 */
static const char *prog_name = NULL;


/*
 * file pointer for the log file (NULL if none is configured)
 */
static FILE *log_fp = NULL;


/*
 * write message to the log file, if a log file is configured
 */
static void
plog_va(const char *format, va_list params) {
	struct timeval tv;
	struct tm *tm_p;
	if (log_fp) {
		gettimeofday(&tv, NULL);
		tm_p = localtime(&tv.tv_sec);
		fprintf(log_fp, "%04d-%02d-%02d %02d:%02d:%02d.%03ld: ",
		    tm_p->tm_year + 1900, tm_p->tm_mon + 1, tm_p->tm_mday,
		    tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec,
		    (long) tv.tv_usec / 1000);
		vfprintf(log_fp, format, params);
		fprintf(log_fp, "\n");
		fflush(log_fp);
	}
}


/*
 * write a message to the log file
 */
void
plog(const char *format, ...) {
	va_list params;
	va_start(params, format);
	plog_va(format, params);
	va_end(params);
}


#define DUMP_LINE 8

/*
 * dump a section of the Z80 memory to the log file; start + length
 * may overlap the end of memory (address 0x10000 wraps around to 0)
 */
void
plog_dump(int start, int length) {
	char buffer[DUMP_LINE * 4 + 10], *cp;
	int i, j, c;
	/*
	 * check if log file is open
	 */
	if (! log_fp) return;
	for (i = 0; i < length; i += DUMP_LINE) {
		cp = buffer;
		cp += sprintf(cp, "%04x:", (start + i) & 0xffff);
		for (j = 0; j < DUMP_LINE && i + j < length; j++) {
			c = memory[(start + i + j) & 0xffff];
			cp += sprintf(cp, " %02x", c);
		}
		for (; j < DUMP_LINE; j++) cp += sprintf(cp, "   ");
		cp += sprintf(cp, " |");
		for (j = 0; j < DUMP_LINE && i + j < length; j++) {
			c = memory[(start + i + j) & 0xffff];
			/*
			 * ASCII specific!
			 */
			if (c >= 0x21 /* ! */ && c <= 0x7e /* ~ */) {
				cp += sprintf(cp, "%c", c);
			} else {
				cp += sprintf(cp, ".");
			}
		}
		for (; j < DUMP_LINE; j++) cp += sprintf(cp, " ");
		cp += sprintf(cp, "|");
		plog("%s", buffer);
	}
}


/*
 * write a message both to stderr and to the log file
 */
void
perr(const char *format, ...) {
	va_list params;
	/*
	 * once for stderr...
	 */
	va_start(params, format);
	fprintf(stderr, "%s: ", prog_name);
	vfprintf(stderr, format, params);
	fprintf(stderr, "\n");
	va_end(params);
	/*
	 * ... and once for the log file
	 */
	va_start(params, format);
	plog_va(format, params);
	va_end(params);
}


/*
 * display a short usage summary
 */
void
usage(void) {
	perr("usage: %s [ <options> ] command [ <parameters> ... ]",
	    prog_name);
	perr("valid <options> are");
	perr("    -a            use alternate charset");
	perr("    -b            use line mode console");
	perr("    -c (<n> | @)  number of full screen mode columns *");
	perr("    -d <drive>    set default drive");
	perr("    -f <fn>       read configuration file");
	perr("    -l (<n> | @)  number of full screen mode lines *");
	perr("    -n            never actually close files");
	perr("    -r            reverse backspace and delete keys *");
	perr("    -s            use full screen mode console");
	perr("    -t (<n> | @)  delay before exiting full screen mode *");
	perr("    -v <level>    set log level");
	perr("    -w            use alternate function keys *");
	perr("options with an asterisk (*) apply only to full screen mode");
}


/*
 * print error message if a command line option has been defined more than once
 */
static void
only_once(int c) {
	perr("option -%c may be specified only once", c);
}


/*
 * parse a screen dimension from the command line (quite similar to
 * parse_dim() above); dimensions have to be decimal numbers in the
 * range min..max or the @ sign for the current screen size
 */
static int
parse_size(int c, int min, int max, const char *arg, int *size_p) {
	int rc = 0;
	unsigned long ul;
	char *cp;
	if (! strcmp(arg, "@")) {
		*size_p = (-1);
	} else {
		ul = strtoul(arg, &cp, 10);
		if (*cp || ul < min || ul > max) {
			perr("option -%c: argument out of range (%d...%d)",
			    c, min, max);
			rc = (-1);
		} else {
			*size_p = (int) ul;
		}
	}
	return rc;
}


/*
 * get the configuration
 *
 * After parsing the command line, further configuration values are
 * read from the optional configuration file
 */
static int
get_config(int argc, char **argv) {
	int rc = 0, opt, i;
	char *cfn = NULL, *cp;
	static const char valid_drives[] = "abcdefghijklmnop";
	size_t l;
	unsigned long ul;
	opterr = 0;
	while ((opt = getopt(argc, argv, "rbasc:l:f:d:v:wnt:")) != EOF) {
		switch (opt) {
		case 'a':
			/*
			 * use alternate character set
			 */
			if (charset) {
				only_once('a');
				rc = (-1);
			} else {
				charset = 1;
			}
			break;
		case 's':
		case 'b':
			/*
			 * use terminal emulation (-s) or the line
			 * orientated console interface (-b)
			 */
			if (conf_interactive != (-1)) {
				perr("options -b and -s may be specified "
				    "only once and are mutually exclusive");
				rc = (-1);
			} else {
				conf_interactive = (optopt == 's');
			}
			break;
		case 'f':
			/*
			 * configuration file
			 */
			if (cfn) {
				only_once('f');
				rc = (-1);
			}
			cfn = optarg;
			break;
		case 'l':
			/*
			 * number of lines of the VT52 emulation
			 */
			if (lines) {
				only_once('l');
				rc = (-1);
			}
			if (parse_size('l', MIN_LINES, MAX_LINES,
			    optarg, &lines)) {
				rc = (-1);
			}
			break;
		case 'c':
			/*
			 * number of columns of the VT52 emulation
			 */
			if (lines) {
				only_once('c');
				rc = (-1);
			}
			if (parse_size('c', MIN_COLS, MAX_COLS,
			    optarg, &cols)) {
				rc = (-1);
			}
			break;
		case 'd':
			/*
			 * initial default drive
			 */
			if (default_drive != (-1)) {
				only_once('d');
				rc = (-1);
			}
			l = strlen(optarg);
			cp = strchr(valid_drives, *optarg);
			if (l < 1 || l > 2 || (l == 2 && optarg[1] != ':')
			    || ! cp) {
				perr("invalid default drive");
				rc = (-1);
			} else {
				default_drive = cp - valid_drives;
			}
			break;
		case 'v':
			/*
			 * log level
			 */
			if (log_level != LL_UNSET) {
				only_once('v');
				rc = (-1);
			}
			ul = strtoul(optarg, &cp, 10);
			if (*cp || ul >= LL_INVALID) {
				perr("invalid log level");
				rc = (-1);
			} else {
				log_level = (enum log_level) ul;
			}
		       	break;
		case 'w':
			/*
			 * use WordStar cursor key sequences
			 */
			if (altkeys != (-1)) {
				only_once('w');
				rc = (-1);
			}
			altkeys = 1;
			break;
		case 'r':
			/*
			 * reverse backspace and delete keys
			 */
			if (reverse_bs_del != (-1)) {
				only_once('r');
				rc = (-1);
			}
			reverse_bs_del = 1;
			break;
		case 'n':
			/*
			 * don't actually close files closed
			 * by BDOS function 19
			 */
			if (dont_close != (-1)) {
				only_once('n');
				rc = (-1);
			}
			dont_close = 1;
			break;
		case 't':
			if (screen_delay != (-1)) {
				only_once('t');
				rc = (-1);
			}
			if (! strcmp(optarg, "@")) {
				screen_delay = (-2);
			} else {
				ul = strtoul(optarg, &cp, 10);
				if (*cp || ul > INT_MAX) {
					perr("invalid delay");
					rc = (-1);
				} else {
					screen_delay = (int) ul;
				}
			}
			break;
		case '?':
			perr("invalid option -%c", optopt);
			rc = (-1);
			break;
		}
	}
	/*
	 * there must be a command name on the command line; further
	 * parameters are passed to the CP/M emulator as CP/M command
	 * line parameters
	 */
	if (argc - optind) {
		conf_command = argv[optind];
		conf_argc = argc - optind - 1;
		conf_argv = argv + optind + 1;
	} else {
		perr("command name expected");
		rc = (-1);
	}
	/*
	 * command line error: print usage information and die
	 */
	if (rc) {
		usage();
		goto premature_exit;
	}
	/*
	 * read the optional configuration file
	 */
	rc = read_config(cfn);
	if (rc) goto premature_exit;
	/*
	 * default drive if none specified is A
	 */
	if (default_drive == (-1)) default_drive = 0;
	/*
	 * default screen size for the VT52 emulation
	 */
	if (! lines) lines = 24;
	if (! cols) cols = 80;
	/*
	 * massage screen delay value
	 */
	switch (screen_delay) {
	case (-2): screen_delay = (-1); break;
	case (-1): screen_delay = 0; break;
	}
	/*
	 * default log level is LL_ERRORS
	 */
	if (log_level == LL_UNSET) log_level = LL_ERRORS;
	/*
	 * character I/O is by default text
	 */
	if (conf_printer_raw == (-1)) conf_printer_raw = 0;
	if (conf_punch_raw == (-1)) conf_punch_raw = 0;
	if (conf_reader_raw == (-1)) conf_reader_raw = 0;
	/*
	 * if not a single drive is defined, define drive a: as
	 * the current working directory
	 */
	for (i = 0; i < 16 && ! conf_drives[i]; i++);
	if (i == 16) {
		conf_drives[0] = alloc(2);
		strcpy(conf_drives[0], ".");
	}
	/*
	 * default drive must be defined
	 */
	if (! conf_drives[default_drive]) {
		perr("default drive has no definition");
		rc = (-1);
	}
	/*
	 * close files by default
	 */
	if (dont_close == (-1)) dont_close = 0;
	/*
	 * use VT52 cursor keys by default
	 */
	if (altkeys == (-1)) altkeys = 0;
	/*
	 * don't reverse backspace and delete keys by default
	 */
	if (reverse_bs_del == (-1)) reverse_bs_del = 0;
	/*
	 * default mode is batch
	 */
	if (conf_interactive == (-1)) conf_interactive = 0;
premature_exit:
	return rc;
}


/*
 * For once, no comment.
 */
int
main(int argc, char **argv) {
	int rc = 0;
	prog_name = base_name(argv[0]);
	if (! setlocale(LC_CTYPE, "")) {
		perr("setlocale(LC_CTYPE) failed");
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * if the only parameter is -h, print the usage summary and exit
	 */
	if (argc == 2 && ! strcmp(argv[1], "-h")) {
		usage();
		goto premature_exit;
	}
	/*
	 * for security reasons, refuse to run as root user
	 */
	if (geteuid() == 0) {
		perr("I'm sorry, but I refuse to run as super user.");
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * parse command line and read configuration file
	 */
	if (get_config(argc, argv)) {
		perr("command line or configuration error");
		goto premature_exit;
	}
	/*
	 * open log file, if one is configured
	 */
	if (conf_log) {
		log_fp = fopen(conf_log, "a");
		if (! log_fp) {
			perr("cannot open log file %s: %s", conf_log,
			    strerror(errno));
			rc = (-1);
			goto premature_exit;
		}
		if (log_level > LL_ERRORS) plog("log opened");
	}
	/*
	 * initialize CPU and OS emulation
	 */
	rc = cpu_init();
	if (rc) goto premature_exit;
	/*
	 * initialize console emulation
	 */
	rc = console_init();
	if (! rc) {
		/*
		 * run program in emulated environment;
		 * errors are indicated by the return
		 * code of cpu_exit();
		 */
		cpu_run();
		/*
		 * clean up console
		 */
		if (console_exit()) rc = (-1);
	}
	/*
	 * clean up after emulation run
	 */
	if (cpu_exit()) rc = (-1);
	/*
	 * close character devices
	 */
	if (finalize_chario()) rc = (-1);
premature_exit:
	/*
	 * close log file, if one is open
	 */
	if (log_fp) {
		if (log_level > LL_ERRORS) plog("log closed");
		if (fclose(log_fp)) {
			perr("cannot close log file %s: %s", conf_log,
			    strerror(errno));
			rc = (-1);
		}
	}
	exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
	return 0;
}
