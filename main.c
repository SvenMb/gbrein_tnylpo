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
#include <ctype.h>
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
 * log level
 */
enum log_level log_level = LL_UNSET; 


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
 * allocate memory, scream and die if there is no more memory available
 */
void *
alloc(size_t s) {
	void *vp = malloc(s);
	if (! vp) {
		perr("out of memory");
		exit(EXIT_FAILURE);
	}
	return vp;
}


/*
 * same, but reallocates memory
 */
void *resize(void *vp, size_t s) {
	void *tp = realloc(vp, s);
	if (! tp) {
		perr("out of memory");
		exit(EXIT_FAILURE);
	}
	return tp;
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
	 * parse command line and read log file
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
