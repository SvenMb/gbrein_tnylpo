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


#ifndef TNYLPO_H
#define TNYLPO_H

#include <stdlib.h>
#include <wchar.h>


/*
 * log level
 */
enum log_level {
	LL_UNSET = (-1) /* initial state */,
	LL_ERRORS = 0 /* report only errors */,
	LL_COUNTERS /* collect and output instruction counters */,
	LL_FDOS	/* trace FDOS functions */,
	LL_FCBS	/* dump FCBs in FDOS functions */,
	LL_RECORDS /* dumps data read and written */,
	LL_SYSCALL /* trace all OS functions */,
	LL_INVALID /* one log level too high... */
};
extern enum log_level log_level;


/*
 * error messages, memory (re-)allocation
 */
extern void perr(const char *format, ...);
extern void plog(const char *format, ...);
extern void plog_dump(int addr, int length);
extern void usage(void);


/*
 * size of the Z80 main memory; base of the magic addresses for OS calls
 * (the magic addresses are the last 19 addresses of the CP/M address
 * space [ 0xffed ... 0xffff ]; an instruction fetch from one of these
 * addresses causes a call to the emulated BDOS, to one of the emulated
 * 17 CP/M BIOS entries, or to tnylpos delay routine)
 */
#define MEMORY_SIZE (64 * 1024)
#define BIOS_VECTOR_COUNT 18
#define MAGIC_ADDRESS (MEMORY_SIZE - (1 + BIOS_VECTOR_COUNT))


/*
 * parts of the CPU emulation visible to the OS emulation (the CP/M
 * interface uses only 8080 compatible registers)
 */
extern unsigned char *memory;
extern int reg_sp;
extern int reg_pc;
extern unsigned char reg_a;
extern unsigned char reg_b;
extern unsigned char reg_c;
extern unsigned char reg_d;
extern unsigned char reg_e;
extern unsigned char reg_h;
extern unsigned char reg_l;


/*
 * flag for terminating emulator (set by an emulated OS function if
 * it wants to terminate the emulation)
 */
extern int terminate;
/*
 * reason of terminating emulator
 */
enum reason {
	OK_NOTRUN /* CPU emulation didn't rund due to earlier problems */,
	OK_TERM /* terminated by program (WBOOT, BDOS(0)) */,
	OK_CTRLC /* terminated by pressing ^C */,
	ERR_BOOT /* BOOT called (misbehaved program) */,
	ERR_BDOSARG /* illegal BDOS function parameter (misbehaved program) */,
	ERR_SELECT /* illegal/unconfigured disk drive accessed */,
	ERR_RODISK /* write access to R/O disk attempted */,
	ERR_ROFILE /* write access to R/O file attempted */,
	ERR_HOST /* host system call failed */,
	ERR_LOGIC  /* error in guest program logic */,
	ERR_SIGNAL /* caught a signal */
};
extern enum reason term_reason;


/*
 * CPU emulation functions
 */
extern int cpu_init(void);
extern void cpu_run(void);
extern int cpu_exit(void);


/*
 * OS emulation functions (in fact, OS emulation is part of the CPU
 * emulation, but separated to keep the source file size managable)
 */
extern int os_init(void);
extern void os_call(int magic);
extern int os_exit(void);



/*
 * configuration from the command line and from the configuration file
 */
extern int lines;
extern int cols;
extern int conf_interactive;
extern int altkeys;
extern int screen_delay;
extern wchar_t *conf_charset[256];
extern wchar_t *conf_alt_charset[256];
extern wchar_t *conf_unprintable;
extern char *conf_drives[16];
extern int conf_readonly[16];
extern char *conf_command;
extern int conf_argc;
extern char **conf_argv;
extern char *conf_printer;
extern int conf_printer_raw;
extern char *conf_punch;
extern int conf_punch_raw;
extern char *conf_reader;
extern int conf_reader_raw;
extern int charset;
extern char *conf_log;
extern int default_drive;
extern int dont_close;
extern int reverse_bs_del;
extern int delay_count;
extern int delay_nanoseconds;


/*
 * dump configuration
 */
enum dump {
	DUMP_NONE = 0x01,
	DUMP_STARTUP = 0x02,
	DUMP_EXIT = 0x04,
	DUMP_ERROR = 0x08,
	DUMP_SIGNAL = 0x10,
	DUMP_ALL = 0x20
};
extern enum dump conf_dump;


/*
 * read the optional configuration file
 */
extern int read_config(char *cfn);


/*
 * utility functions
 */
extern const char *base_name(const char *path);
extern void *alloc(size_t s);
extern void *resize(void *vp, size_t s);


/*
 * character conversion
 */
extern int to_cpm(wchar_t c);
extern wint_t from_cpm(unsigned char c);
extern wint_t from_graph(unsigned char c);


/*
 * character I/O emulation
 */
extern int console_init(void);
extern int console_exit(void);
extern unsigned char console_in(void);
extern void console_out(unsigned char c);
extern int console_status(void);
extern void console_poll(void);
extern void printer_out(unsigned char c);
extern int printer_status(void);
extern void punch_out(unsigned char c);
extern unsigned char reader_in(void);
extern int finalize_chario(void);


/*
 * maximum and minimum sizes of the VT52 emulation
 */
#define MIN_LINES 5
#define MAX_COLS 95
#define MIN_COLS 20
#define MAX_LINES 95


/*
 * VT52 emulation (part of the character I/O emulation, but separated to
 * keep source file sizes managable)
 */
extern int crt_init(void);
extern void crt_exit(void);
extern unsigned char crt_in(void);
extern void crt_out(unsigned char c);
extern int crt_status(void);
extern void crt_poll(void);


#endif
