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
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>

#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>


#include "tnylpo.h"


/*
 * original state of the terminal device
 */
static struct termios old_termios;
/*
 * are the contents of old_termios valid?
 */
static int old_termios_valid = 0;
/*
 * stdin/stdout redirected?
 */
static int redirected = 0;
/*
 * for CR/LF --- LF conversion while console is redirected
 */
static int last_was_cr = 0;


/*
 * helper function for setting terminal attributes
 */
static int
set_term(int fd, struct termios *tp) {
	return tcsetattr(fd, TCSADRAIN, tp);
}


/*
 * reset stdin/stdout terminal attributes to values active at
 * program start
 */
static void
restore_terminal(void) {
	if (old_termios_valid) set_term(fileno(stdin), &old_termios);
}


/*
 * initialize console device
 * if the console is the VT52 emulation, crt_init() is called; otherwise,
 * if stdin/stdout are not redirected from the terminal, the parameters
 * of the underlying terminal device are modified to be suitable for
 * CP/M style console input and output 
 */
int
console_init(void) {
	int rc = 0, t;
	struct stat in_stat, out_stat;
	struct termios new_termios;
	/*
	 * the VT52 emulation uses curses and is handled separately
	 */
	if (conf_interactive) {
		rc = crt_init();
		goto premature_exit;
	}
	/*
	 * get stat of both stdin and stdout; if they do not refer to the
	 * same character device, assume there is a redirection
	 */
	t = fstat(fileno(stdin), &in_stat);
	if (t == (-1)) {
		perr("fstat(stdin) failed: %s", strerror(errno));
		rc = (-1);
		goto premature_exit;
	}
	t = fstat(fileno(stdout), &out_stat);
	if (t == (-1)) {
		perr("fstat(stdout) failed: %s", strerror(errno));
		rc = (-1);
		goto premature_exit;
	}
	redirected = (! S_ISCHR(in_stat.st_mode) ||
	    ! S_ISCHR(out_stat.st_mode) ||
	    in_stat.st_rdev != out_stat.st_rdev);
	if (redirected) goto premature_exit;
	/*
	 * get current terminal parameters of stdin
	 */
	t = tcgetattr(fileno(stdin), &old_termios);
	if (t == (-1)) {
		if (errno == ENOTTY) {
			/*
			 * stdin is no terminal device: redirected
			 */
			redirected = 1;
		} else {
			/*
			 * other errors are fatal
			 */
			perr("tcgetattr() failed: %s", strerror(errno));
			rc = (-1);
		}
		goto premature_exit;
	}
	/*
	 * old parameter values are valid
	 */
	old_termios_valid = 1;
	/*
	 * change input parameters: ignore breaks, ignore parity errors,
	 * don't strip the 8th bit, don't map or ignore CR and LF,
	 * don't echo input, don't generate interactive signals,
	 * switch off all canonical input processing; reads will
	 * wait indefinitely for data, but will return when there is
	 * at least one input byte
	 * change output parameters: turn off all output postprocessing
	 * like LF --- CR/LF conversion
	 */
	memcpy(&new_termios, &old_termios, sizeof new_termios);
	new_termios.c_iflag |= IGNBRK;
	new_termios.c_iflag |= IGNPAR;
	new_termios.c_iflag &= ~ISTRIP;
	new_termios.c_iflag &= ~INLCR;
	new_termios.c_iflag &= ~IGNCR;
	new_termios.c_iflag &= ~ICRNL;
	new_termios.c_lflag &= ~ECHO;
	new_termios.c_lflag &= ~ISIG;
	new_termios.c_lflag &= ~ICANON;
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;
	new_termios.c_oflag &= OPOST;
	if (set_term(fileno(stdin), &new_termios) == (-1)) {
		perr("tcsetattr() failed: %s", strerror(errno));
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * set stdin and stdout to unbuffered
	 */
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
premature_exit:
	/*
	 * in case of an error, restore the old terminal parameters
	 */
	if (rc && ! conf_interactive) restore_terminal();
	return rc;
}


/*
 * output a single character in CP/M representation to the console
 */
void
console_out(unsigned char c) {
	wint_t wc;
	/*
	 * the VT52 emulation is handled separately
	 */
	if (conf_interactive) {
		crt_out(c);
		goto premature_exit;
	}
	if (redirected) {
		/*
		 * change CR/LF to LF
		 */
		if (c != 0x0a /* LF */ && last_was_cr) putwchar(L'\r');
		if (c != 0x0d /* CR */) {
			/*
			 * convert to the Unix character set; unconvertible
			 * characters are silently ignored
			 */
			wc = from_cpm(c);
			if (wc != (-1)) putwchar(wc);
		}
		last_was_cr = (c == 0x0d /* CR */);
	} else {
		/*
		 * output to the terminal (no emulation): convert to
		 * Unix character set, ignore unconvertible characters
		 */
		wc = from_cpm(c);
		if (wc != (-1)) putwchar(wc);
	}
premature_exit:
	return;
}


/*
 * read a single character in CP/M representation from the console
 */
unsigned char
console_in(void) {
	unsigned char c;
	int t;
	wint_t wc;
	/*
	 * the VT52 emulation is handled separately
	 */
	if (conf_interactive) {
		c = crt_in();
		goto premature_exit;
	}
	if (redirected) {
		/*
		 * EOF is signalled CP/M style by the
		 * character SUB (^Z)
		 */
		if (feof(stdin) || ferror(stdin)) {
			c = 0x1a /* SUB */;
		} else {
			for (;;) {
				/*
				 * read a character from stdin
				 */
				wc = getwchar();
				/*
				 * on EOF (or on errors) return SUB (^Z)
				 */
				if (wc == WEOF) {
					c = 0x1a /* SUB */;
					break;
				}
				/*
				 * convert to the CP/M character set,
				 * ignore unconvertible characters
				 */
				t = to_cpm(wc);
				if (t != (-1)) {
					/*
					 * convert LF to CR
					 */
					if (t == 0x0a /* LF */) {
						t = 0x0d /* CR */;
					}
					c = (unsigned char) t;
					break;
				}
			}
		}
	} else {
		/*
		 * input from terminal (no emulation)
		 */
		for (;;) {
			/*
			 * read a character from stdin
			 */
			wc = getwchar();
			/*
			 * ignore errors (EOF proper should not be generated
			 * due to the terminal parameter settings)
			 */
			if (wc != WEOF) {
				/*
				 * convert to the CP/M character set,
				 * ignore unconvertible characters
				 */
				t = to_cpm(wc);
				if (t != (-1)) {
					c = (unsigned char) t;
					break;
				}
			}
		}
	}
premature_exit:
	return c;
}


/*
 * poll the console; this is only relevant for the VT52 emulation
 */
void
console_poll(void) {
	if (conf_interactive) crt_poll();
}


/*
 * console_status() returns true if there is a character ready from the
 * console, and false otherwise
 */
int
console_status(void) {
	int s = 0, t;
	fd_set in_set;
	struct timeval tv;
	if (conf_interactive) {
		/*
		 * the VT52 emulation is handled separately
		 */
		s = crt_status();
	} else {
		/*
		 * check for data availability by a nonblocking (zero-timeout)
		 * select() (this always returns true if stdin is redirected
		 * from a file)
		 */
		FD_ZERO(&in_set);
		FD_SET(fileno(stdin), &in_set);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		t = select(fileno(stdin) + 1, &in_set, NULL, NULL, &tv);
		s = (t != 0);
	}
	return s;
}


/*
 * clean up the console device
 */
int
console_exit(void) {
	/*
	 * the VT52 emulation (quite a lot mor work) is handled separately
	 */
	if (conf_interactive) {
		crt_exit();
		goto premature_exit;
	}
	/*
	 * output a pending CR (only happens if output is redirected to a file)
	 */
	if (last_was_cr) putwchar(L'\r');
	restore_terminal();
premature_exit:
	return 0;
}


/*
 * file pointers for printer, punch, and reader data files
 */
static FILE *printer_fp = NULL, *punch_fp = NULL, *reader_fp = NULL;
/*
 * errno describing the error which occurred while handling the
 * device data files (used for displaying appropriate error messages after
 * exiting interactive mode)
 */
static int printer_error = 0, punch_error = 0, reader_error = 0;
/*
 * state variables for LF --- CR/LF translation from and to the
 * printer, punch, and reader devices
 */
static int printer_cr = 0, punch_cr = 0, reader_lf = 0;


#define DEV_WCHAR(DEV) \
static void \
DEV##_wchar(wchar_t wc) { \
	wint_t t; \
	if (DEV##_error) goto premature_exit; \
	for (;;) { \
		t = fputwc(wc, DEV##_fp); \
		if (t != WEOF) break; \
		if (errno != EAGAIN) { \
			DEV##_error = errno; \
			goto premature_exit; \
		} \
	} \
premature_exit: \
	return; \
}


#define DEV_OUT(DEV) \
void \
DEV##_out(unsigned char c) { \
	size_t n; \
	wchar_t wc; \
	if (! conf_##DEV) goto premature_exit; \
	if (DEV##_error) goto premature_exit; \
	if (! DEV##_fp) { \
		DEV##_fp = fopen(conf_##DEV, conf_##DEV##_raw ? "ab" : "a"); \
		if (! DEV##_fp) { \
			DEV##_error = errno; \
			goto premature_exit; \
		} \
	} \
	if (conf_##DEV##_raw) { \
		for (;;) { \
			n = fwrite(&c, sizeof c, 1, DEV##_fp); \
			if (n == 1) break; \
			if (errno != EAGAIN) { \
				DEV##_error = errno; \
				goto premature_exit; \
			} \
		} \
	} else { \
		if (c != 0x0a /* LF */ && DEV##_cr) DEV##_wchar(L'\r'); \
		if (c != 0x0d /* CR */) { \
			wc = from_cpm(c); \
			if (wc != (-1)) DEV##_wchar(wc); \
		} \
		DEV##_cr = (c == 0x0d /* CR */); \
	} \
premature_exit: \
	return; \
}


/*
 * macro to define printer_close() and punch_close(), which have exactly
 * the same structure: appends pending CR, reports errors encountered
 * during device use, and closes the device file.
 */
#define DEV_CLOSE(DEV) \
static int \
DEV##_close(void) { \
	int rc = 0; \
	if (DEV##_cr) DEV##_wchar(L'\r'); \
	if (DEV##_error) { \
		perr("error on %s: %s", conf_##DEV, strerror(DEV##_error)); \
		rc = (-1); \
	} \
	if (DEV##_fp) { \
		if (fclose(DEV##_fp)) { \
			perr("cannot close %s: %s", conf_##DEV, \
			    strerror(errno)); \
			rc = (-1); \
		} \
	} \
	return rc; \
}


/*
 * printer_wchar(), helper for printer_out() and printer_close(), see above
 */
DEV_WCHAR(printer)


/*
 * printer_out(), see above
 */
DEV_OUT(printer)


/*
 * report printer status; as long as a printer has been configured and
 * no error occurred, the printer is always ready to receive data
 */
int
printer_status(void) {
	return (! conf_printer || printer_error) ? 0 : 1;
}


/*
 * printer_close(), helper for finalize_chario(), see above
 */
DEV_CLOSE(printer)


/*
 * punch_wchar(), helper for punch_out() and punch_close(), see above
 */
DEV_WCHAR(punch)


/*
 * punch_out(), see above
 */
DEV_OUT(punch)


/*
 * punch_close(), helper for finalize_chario(), see above
 */
DEV_CLOSE(punch)


/*
 * read a byte from the reader device
 */
unsigned char
reader_in(void) {
	/*
	 * default byte is ASCII SUB as EOF marker
	 */
	unsigned char c = 0x1a /* SUB */, tc;
	size_t n;
	wint_t wc;
	int t;
	/*
	 * no reader configured or previous reader error?
	 */
	if (! conf_reader || reader_error) goto premature_exit;
	/*
	 * open reader file if not already opened
	 */
	if (! reader_fp) { 
		reader_fp = fopen(conf_reader, conf_reader_raw ? "rb" : "r");
		if (! reader_fp) {
			reader_error = errno;
			goto premature_exit;
		}
	}
	/*
	 * raw binary bytes or text file?
	 */
	if (conf_reader_raw) {
		/*
		 * raw binary bytes
		 */
		for (;;) {
			/*
			 * try to read next byte
			 */
			n = fread(&tc, sizeof tc, 1, reader_fp);
			/*
			 * byte was available
			 */
			if (n == 1) {
				c = tc;
				break;
			}
			/*
			 * EOF reached
			 */
			if (feof(reader_fp)) break;;
			/*
			 * interrupted system call is o.k., otherwise
			 * mark reader as unavailable
			 */
			if (errno != EAGAIN) {
				reader_error = errno;
				break;;
			}
		}
	} else {
		/*
		 * text file
		 */
		if (reader_lf) {
			/*
			 * translate LF to CR/LF
			 */
			reader_lf = 0;
			c = 0x0a /* LF */;
		} else {
			for (;;) {
				/*
				 * get next (multibyte) character
				 */
				wc = fgetwc(reader_fp);
				if (wc == WEOF) {
					/*
					 * EOF reached
					 */
					if (feof(reader_fp)) break;
					/*
					 * retry on interrupted system call,
					 * otherwise mark reader as
					 * unavailable
					 */
					if (errno != EAGAIN) {
						reader_error = errno;
						break;
					}
				} else {
					/*
					 * convert to CP/M character set,
					 * skip unknown characters
					 */
					t = to_cpm(wc);
					if (t != (-1)) {
						c = (unsigned char) t;
						/*
						 * translate LF to CR/LF
						 */
						if (c == 0x0a /* LF */) {
							reader_lf = 1;
							c = 0x0d /* CR */;
						}
						break;
					}
				}
			}
		}
	} 
premature_exit:
	return c;
}


/*
 * shutdown reader device; helper for finalize_chario()
 */
static int
reader_close(void) {
	int rc = 0;
	/*
	 * report error which caused the reader to become unavailable
	 */
	if (reader_error) {
		perr("error on %s: %s", conf_reader, strerror(reader_error)); 
		rc = (-1);
	}
	if (reader_fp) {
		/*
		 * close reader file
		 */
		if (fclose(reader_fp)) {
			perr("cannot close %s: %s", conf_reader,
			    strerror(errno));
			rc = (-1);
		}
	}
	return rc;
}



/*
 * shutdown printer, punch, and reader devices, report errors
 */
int
finalize_chario(void) {
	int rc = 0;
	if (printer_close()) rc = (-1);
	if (punch_close()) rc = (-1);
	if (reader_close()) rc = (-1);
	return rc;
}
