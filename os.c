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
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/time.h>

#include "tnylpo.h"


/*
 * memory layout of the emulated CP/M computer
 */
#define ALV_SIZE 64
#define ALV (MAGIC_ADDRESS - ALV_SIZE)
#define DPB_SIZE 15
#define DPB (ALV - DPB_SIZE)
#define BIOS_VECTOR (DPB - BIOS_VECTOR_COUNT * 3)
#define BIOS_START BIOS_VECTOR
#define BIOS_SIZE (MEMORY_SIZE - BIOS_START)
#define BDOS_SIZE 11
#define BDOS_START (BIOS_START - BDOS_SIZE)
#define SERIAL_NUMBER (BDOS_START - 6)
#define CCP_STACK_COUNT 8
#define CCP_STACK (SERIAL_NUMBER - CCP_STACK_COUNT * 2)
#define CCP_START CCP_STACK
#define CCP_SIZE (BDOS_START - CCP_START)
#define TPA_START 0x0100
#define BOOT 0x0000
#define IOBYTE 0x0003
#define DRVUSER 0x0004
#define BDOS_ENTRY 0x0005
#define DEFAULT_FCB_1 0x005c
#define DEFAULT_FCB_2 0x006c
#define DEFAULT_DMA 0x0080
#define DMA_SIZE 128


/*
 * current default drive (0=A, 1=B, ..., 15=P)
 */
static int current_drive = 0;


/*
 * current user number (0...15)
 */
static int current_user = 0;


/*
 * runtime read-only flags for the drives
 */
static int read_only[16];


/*
 * current DMA area
 */
static int current_dma = DEFAULT_DMA;


/*
 * OS serial number
 */
static const unsigned char serial_number[6] = {
	0x00, 0x16, 0x00, 0xc0, 0xff, 0xee
};


/*
 * helper function: get word from DE
 */
static inline int
get_de(void) { int de = reg_d; de <<= 8; de |= reg_e; return de; }


/*
 * helper function: get word from HL
 */
static inline int
get_hl(void) { int hl = reg_h; hl <<= 8; hl |= reg_l; return hl; }


/*
 * helper function: get word from BC
 */
static inline int
get_bc(void) { int bc = reg_b; bc <<= 8; bc |= reg_c; return bc; }


/*
 * logging functions and macros for the OS routines
 */
#define REGS_A 0x01
#define REGS_C 0x02
#define REGS_E 0x04
#define REGS_BC 0x08
#define REGS_DE 0x10
#define REGS_HL 0x20


static const char *
format_regs(int regs) {
	static char buffer[80];
	char *cp = buffer;
	if (! regs) {
		*cp = '\0';
	} else {
		cp += sprintf(cp, ":");
		if (regs & REGS_A) cp += sprintf(cp, " a=0x%02x", reg_a);
		if (regs & REGS_C) cp += sprintf(cp, " c=0x%02x", reg_c);
		if (regs & REGS_E) cp += sprintf(cp, " e=0x%02x", reg_e);
		if (regs & REGS_BC) cp += sprintf(cp, " bc=0x%04x", get_bc());
		if (regs & REGS_DE) cp += sprintf(cp, " de=0x%04x", get_de());
		if (regs & REGS_HL) cp += sprintf(cp, " hl=0x%04x", get_hl());
	}
	return buffer;
}


static void
sys_entry(const char *name, int regs) {
	plog("%s entry%s", name, format_regs(regs));
}


static void
sys_exit(const char *name, int regs) {
	plog("%s exit%s", name, format_regs(regs));
}


#define FDOS_ENTRY(name, regs) \
	if (log_level >= LL_FDOS) sys_entry(name, regs)
#define FDOS_EXIT(name, regs) \
	if (log_level >= LL_FDOS) sys_exit(name, regs)
#define SYS_ENTRY(name, regs) \
	if (log_level >= LL_SYSCALL) sys_entry(name, regs)
#define SYS_EXIT(name, regs) \
	if (log_level >= LL_SYSCALL) sys_exit(name, regs)


/*
 * checks if a base filename is "nice", i. e. acceptable both in CP/M and Unix
 */
static int
is_nice_filename(const char *fn) {
	const char *cp;
	size_t l;
	/*
	 * valid characters in filename and file name extension
	 */
	static const char valid[] = "#$-0123456789@abcdefghijklmnopqrstuvwxyz";
	/*
	 * assumption: no, the file name is not nice
	 */
	int rc = 0;
	/*
	 * is there an extension?
	 */
	cp = strchr(fn, '.');
	if (! cp) {
		/*
		 * no extension
		 */
		/*
		 * name must be 1 to 8 characters
		 */
		l = strlen(fn);
		if (l < 1 || l > 8) goto premature_exit;
		/*
		 * all characters must be valid
		 */
		for (cp = fn; *cp; cp++) {
			if (! strchr(valid, *cp)) goto premature_exit;
		}
	} else {
		/*
		 * extension present
		 */
		/*
		 * name must be 1 to 8 characters
		 */
		l = cp - fn;
		if (l < 1 || l > 8) goto premature_exit;
		/*
		 * extension must be 1 to 3 characters
		 */
		l = strlen(cp + 1);
		if (l < 1 || l > 3) goto premature_exit;
		/*
		 * all name characters must be valid
		 */
		for (cp = fn; *cp != '.'; cp++) {
			if (! strchr(valid, *cp)) goto premature_exit;
		}
		/*
		 * all extension characters must be valid
		 */
		for (cp++; *cp; cp++) {
			if (! strchr(valid, *cp)) goto premature_exit;
		}
	}
	/*
	 * file name is nice
	 */
	rc = 1;
premature_exit:
	return rc;
}


/*
 * helper function for os_init(): check file name of command file
 */
static int
check_command_name(const char *fn, int *add_com_p) {
	int rc = 0;
	const char *cp;
	/*
	 * base name must be "nice", i. e. CP/M & Unix conforming
	 */
	if (! is_nice_filename(fn)) {
		perr("command file name (%s) not valid", fn);
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * is there an extension?
	 */
	cp = strchr(fn, '.');
	if (cp) {
		/*
		 * yes: it must be .com
		 */
		if (strcmp(cp, ".com")) {
			perr("command file name must end in .com");
			rc = (-1);
			goto premature_exit;
		}
		*add_com_p = 0;
	} else {
		/*
		 * no: append .com
		 */
		*add_com_p = 1;
	}
premature_exit:
	return rc;
}


/*
 * helper function for handle_name_part(): check if a CP/M character
 * is valid in a file name
 */
static int
is_valid_in_cfn(unsigned char c) {
	if (c == 0x23 /* # */) return 1;
	if (c == 0x24 /* $ */) return 1;
	if (c == 0x2d /* - */) return 1;
	if (c == 0x3f /* ? */) return 1;
	if (c == 0x40 /* @ */) return 1;
	if (c >= 0x30 /* 0 */ && c <= 0x39 /* 9 */) return 1;
	if (c >= 0x41 /* A */ && c <= 0x5a /* Z */) return 1;
	return 0;
}


/*
 * helper function for setup_fcb(): handle file name or extension
 */
static const unsigned char *
handle_name_part(const unsigned char *sp, size_t length, unsigned char *dp) {
	size_t t;
	const unsigned char *cp;
	int star;
	/*
	 * skip over valid characters
	 */
	for (t = 0, cp = sp; is_valid_in_cfn(*cp); t++, cp++);
	/*
	 * ignore valid characters after the maximal length
	 */
	if (t > length) t = length;
	/*
	 * does the name part end in '*'?
	 */
	if (*cp == 0x2a /* * */) {
		/*
		 * yes: ignore further stars and valid characters
		 */
		while (*cp == 0x2a /* * */ || is_valid_in_cfn(*cp)) cp++;
		star = 1;
	} else {
		star = 0;
	}
	/*
	 * copy name part
	 */
	memcpy(dp, sp, t);
	/*
	 * if the name contained a '*', pad name part with '?'
	 */
	if (star) memset(dp + t, 0x3f /* ? */, length - t);
	return cp;
}


/*
 * set up the drive and name part of a FCB (i. e. the first twelve
 * bytes of an FCB) according to a (conforming) Unix file name
 */
static void
setup_fcb(const char *fn, unsigned char *fcb) {
	int t, l, i, j;
	wchar_t wc;
	unsigned char *cfn = NULL;
	const unsigned char *cp;
	/*
	 * initialize drive and name part of the fcb FCB
	 */
	fcb[0] = 0;
	memset(fcb + 1, 0x20 /* SPC */, 11);
	/*
	 * convert file name to upper case CP/M characters
	 */
	l = strlen(fn) + 1;
	cfn = alloc(l);
	mbtowc(NULL, NULL, 0);
	i = j = 0;
	do {
		t = mbtowc(&wc, fn + i, l);
		if (t == (-1)) goto premature_exit;
		i += t;
		l -= t;
		t = to_cpm(towupper(wc));
		if (t == (-1)) goto premature_exit;
		cfn[j++] = t;
	} while (t);
	cp = cfn;
	/*
	 * if the file name starts in a drive specification, set
	 * drive field in FCB, otherwise leave it 0
	 */
	if (cp[0] >= 0x41 /* A */ && cp[0] <= 0x50 /* P */ &&
	    cp[1] == 0x3a /* : */) {
		fcb[0] = cp[0] - 0x41 /* A */ + 1;
		cp += 2;
	}
	/*
	 * check the file name and copy it
	 */
	cp = handle_name_part(cp, 8, fcb + 1);
	/*
	 * check for '.' and skip it
	 */
	if (*cp != 0x2e /* . */) goto premature_exit;
	cp++;
	/*
	 * check the extension and copy it
	 */
	cp = handle_name_part(cp, 3, fcb + 9);
premature_exit:
	free(cfn);
	return;
}


/*
 * type of a file list
 */
struct file_list {
	/*
	 * size of file *in CP/M records of 128 bytes*
	 */
	off_t size;
	struct file_list *next_p;
	char *name;
};


/*
 * deallocate a file list
 */
static void
free_filelist(struct file_list *flp) {
	struct file_list *tp;
	while (flp) {
		tp = flp;
		flp = tp->next_p;
		free(tp->name);
		free(tp);
	}
}


/*
 * gets a listing of all possible CP/M files in a directory which
 * match a given, possibly ambigous file name (the pattern is expected
 * in Unix format)
 */
static struct file_list *
get_filelist(const char *directory, const char *name, const char *caller) {
	struct file_list *flp = NULL, *tp;
	DIR *dp = NULL;
	struct dirent *dep;
	int t;
	struct stat s;
	char *path = NULL;
	const char *pp, *np;
	dp = opendir(directory);
	if (! dp) {
		plog("%s: opendir(%s) failed: %s", caller,
		    directory, strerror(errno));
		goto premature_exit;
	}
	while ((dep = readdir(dp))) {
		/*
		 * skip CP/M incompatible names
		 */
		if (! is_nice_filename(dep->d_name)) continue;
		/*
		 * skip names which do not match
		 */
		pp = name;
		np = dep->d_name;
		while (*pp) {
			if (*pp == *np) {
				pp++;
				np++;
			} else if (*pp == '?') {
				pp++;
				if (*np && *np != '.') np++;
			} else {
				break;
			}
		}
		if (*pp || *np) continue;
		/*
		 * build path for file
		 */
		free(path);
		path = alloc(strlen(directory) + strlen(dep->d_name) + 2);
		sprintf(path, "%s/%s", directory, dep->d_name);
		/*
		 * get information for file, skip if unavailable
		 */
		t = lstat(path, &s);
		if (t == (-1)) {
			plog("%s: lstat(%s) failed: %s", caller, path,
			    strerror(errno));
			continue;
		}
		/*
		 * skip all but regular files
		 */
		if (! S_ISREG(s.st_mode)) continue;
		/*
		 * skip files greater than 8MB
		 */
		if (s.st_size > 8 * 1024 * 1024) continue;
		/*
		 * create list entry and append it to list
		 */
		tp = alloc(sizeof (struct file_list));
		tp->next_p = flp;
		tp->size = (s.st_size + 127) / 128;
		tp->name = alloc(strlen(dep->d_name) + 1);
		strcpy(tp->name, dep->d_name);
		flp = tp;
	}
premature_exit:
	free(path);
	if (dp) closedir(dp);
	return flp;
}


/*
 * file data flags
 */
#define FILE_RODISK 0x1 /* opened on a read only disk */
#define FILE_ROFILE 0x2 /* file was opened read only */
#define FILE_WRITTEN 0x4 /* file has been written to */

/*
 * xor value for file ID in FCB
 */
#define FILE_QUUX 0xafcb


/*
 * element of the file list
 */
struct file_data {
	struct file_data *next_p;
	char *path;
	int id;
	int flags;
	int fd;
};


/*
 * head of the file list
 */
static struct file_data *first_file_p = NULL;


/*
 * delete file list element
 */
static void
free_filedata(struct file_data *fdp) {
	if (fdp->fd != (-1)) {
		/*
		 * warn if a program didn't explicitly close an output file
		 */
		if (fdp->flags & FILE_WRITTEN) {
			plog("output file %s not explicitly closed by program",
			    fdp->path);
		}
		if (close(fdp->fd) == (-1)) {
			plog("cannot close %s: %s", fdp->path,
			    strerror(errno));
		}
	}
	free(fdp->path);
	free(fdp);
}


/*
 * create a new entry in the file data list, store its ID in the FCB
 */
static struct file_data *
create_filedata(int fcb, const char *caller) {
	/*
	 * current file ID generator; file ID are in the range 1...65535
	 */
	static int file_id = 1;
	int start_id, id;
	struct file_data **fdpp = NULL, *fdp = NULL;
	/*
	 * start with the current value of file_id
       	 */
	start_id = id = file_id++;
	file_id &= 0xffff;
	if (! file_id) file_id++;
	for (;;) {
		/*
		 * search if the ID is already in use
		 */
		for (fdpp = &first_file_p;
		    *fdpp && (*fdpp)->id < id;
		    fdpp = &(*fdpp)->next_p);
		/*
		 * no: use this ID
		 */
		if (! *fdpp || (*fdpp)->id > id) break;
		/*
		 * get next candidate
		 */
		id = file_id++;
		file_id &= 0xffff;
		if (! file_id) file_id++;
		/*
		 * if all possible file IDs are taken, despair
		 */
		if (id == start_id) {
			plog("%s (FCB 0x%04x): more than 65536 open files",
			    caller, fcb);
			terminate = 1;
			term_reason = ERR_LOGIC;
			goto premature_exit;
		}
	}
	/*
	 * allocate and initialize file data structure and enter
	 * it into the file list
	 */
	fdp = alloc(sizeof (struct file_data));
	fdp->next_p = *fdpp;
	fdp->path = NULL;
	fdp->id = id;
	fdp->flags = 0;
	fdp->fd = (-1);
	*fdpp = fdp;
	/*
	 * store file ID and file ID xor FILE_QUUX in the FCB
	 */
	memory[fcb + 16] = (id & 0xff);
	memory[fcb + 17] = ((id >> 8) & 0xff);
	id ^= FILE_QUUX;
	memory[fcb + 18] = (id & 0xff);
	memory[fcb + 19] = ((id >> 8) & 0xff);
premature_exit:
	return fdp;
}


/*
 * search existing file data structure; returns a pointer to a pointer
 * to allow removal of the entry
 */
static struct file_data **
get_filedata_pp(int fcb, const char *caller) {
	int id, t;
	struct file_data **fdpp = NULL;
	/*
	 * get and check file ID from FCB
	 */
	id = memory[fcb + 17];
	id <<= 8;
	id |= memory[fcb + 16];
	t = memory[fcb + 19];
	t <<= 8;
	t |= memory[fcb + 18];
	if ((id ^ t) != FILE_QUUX) {
		plog("%s (FCB 0x%04x): invalid file ID in FCB", caller, fcb);
		terminate = 1;
		term_reason = ERR_LOGIC;
		goto premature_exit;
	}
	/*
	 * search for file ID in the file list
	 */
	for (fdpp = &first_file_p;
	    *fdpp && (*fdpp)->id < id;
	    fdpp = &(*fdpp)->next_p);
	/*
	 * not found: file already closed
	 */
	if (! *fdpp || (*fdpp)->id != id) {
		plog("%s (FCB 0x%04x): stale file ID in FCB", caller, fcb);
		terminate = 1;
		term_reason = ERR_LOGIC;
		fdpp = NULL;
		goto premature_exit;
	}
premature_exit:
	return fdpp;
}


/*
 * reset disk subsystem: set current drive, reset read only vector,
 * reset DMA address
 */
static void
disk_reset(void) {
	/*
	 * set current drive from configuration
	 */
	current_drive = default_drive;
	memory[DRVUSER] = (default_drive | (current_user << 4));
	/*
	 * initialize readonly drives from configuration
	 */
	memcpy(read_only, conf_readonly, sizeof read_only);
	/*
	 * reset DMA address to default
	 */
	current_dma = DEFAULT_DMA;
}


/*
 * initialize the OS emulation; check command file name,
 * load command file, and set up the environment
 */
int
os_init(void) {
	int rc = 0, i, t, drive, add_com;
	size_t l, tpa_free, bfree, n;
	char *command_file = NULL;
	const char *fn, *cp;
	static const char valid_drive[] = "abcdefghijklmnop";
	FILE *fp = NULL;
	unsigned char *tpa_p;
	wchar_t buffer[DMA_SIZE], *bp;
	/*
	 * reset disk subsystem
	 */
	disk_reset();
	/*
	 * find and load executeable
	 */
	if (strchr(conf_command, '/')) {
		/*
		 * command name contains a slash --- assume Unix path
		 */
		fn = base_name(conf_command);
		/*
		 * check base name
		 */
		rc = check_command_name(fn, &add_com);
		if (rc) goto premature_exit;
		/*
		 * create command path
		 */
		l = strlen(conf_command) + 1;
		if (add_com) {
			command_file = alloc(l + 4);
			sprintf(command_file, "%s.com", conf_command);
		} else {
			command_file = alloc(l);
			strcpy(command_file, conf_command);
		}
	} else {
		/*
		 * assume CP/M style filename relative to virtual drive
		 */
		fn = conf_command;
		/*
		 * is the file name prefixed by a drive name?
		 */
		cp = strchr(valid_drive, fn[0]);
		if (cp && fn[1] == ':') {
			drive = cp - valid_drive;
			fn += 2;
		} else {
			drive = current_drive;
		}
		/*
		 * is the drive defined?
		 */
		if (! conf_drives[drive]) {
			perr("drive %c: not defined", valid_drive[drive]);
			rc = (-1);
			goto premature_exit;
		}
		/*
		 * check name
		 */
		rc = check_command_name(fn, &add_com);
		if (rc) goto premature_exit;
		/*
		 * create command path
		 */
		l = strlen(conf_drives[drive]) + 1 + strlen(fn) +
		    (add_com ? 4 : 0) + 1;
		command_file = alloc(l);
		sprintf(command_file, "%s/%s%s", conf_drives[drive],
		    fn, add_com ? ".com" : "");
	}
	/*
	 * load command file
	 */
	fp = fopen(command_file, "rb");
	if (! fp) {
		perr("cannot open command file %s. %s", command_file,
		    strerror(errno));
		rc = (-1);
		goto premature_exit;
	}
	tpa_p = memory + TPA_START;
	/*
	 * this is deliberately more than CCP_START - TPA_START to
	 * catch a command file which doesn't fit the TPA
	 */
	tpa_free = BDOS_START - TPA_START;
	while (tpa_free) {
		l = fread(tpa_p, 1, tpa_free, fp);
		if (! l) {
			if (feof(fp)) break;
			perr("read error on %s: %s", command_file,
			    strerror(errno));
			rc = (-1);
			goto premature_exit;
		}
		tpa_p += l;
		tpa_free -= l;
	}
	/*
	 * check for overrun
	 */
	if (tpa_free < BDOS_START - CCP_START) {
		perr("command file %s too large", command_file);
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * set up RET instructions in all magic addresses (to keep
	 * debuggers happy
	 */
	memset(memory + MAGIC_ADDRESS, 0xc9, MEMORY_SIZE - MAGIC_ADDRESS);
	/*
	 * set up CCP 8-level stack with a pushed return address to WBOOT
	 */
	reg_sp = SERIAL_NUMBER;
	memory[--reg_sp] = (((BIOS_VECTOR + 3) >> 8) & 0xff);
	memory[--reg_sp] = ((BIOS_VECTOR + 3) & 0xff);
	/*
	 * set up CP/M serial number
	 */
	memcpy(memory + SERIAL_NUMBER, serial_number, sizeof serial_number);
	/*
	 * set up BDOS (jp to MAGIC_ADDRESS + 0)
	 */
	memory[BDOS_START] = 0xc3 /* jp */;
	memory[BDOS_START + 1] = (MAGIC_ADDRESS & 0xff);
	memory[BDOS_START + 2] = ((MAGIC_ADDRESS >> 8) & 0xff);
	/*
	 * four dummy error vectors all point to WBOOT magic address
	 */
	memory[BDOS_START + 3] = memory[BDOS_START + 5] = 
	    memory[BDOS_START + 7] = memory[BDOS_START + 9] =
	       ((MAGIC_ADDRESS + 2) & 0xff);
	memory[BDOS_START + 4] = memory[BDOS_START + 6] = 
	    memory[BDOS_START + 8] = memory[BDOS_START + 10] =
	       (((MAGIC_ADDRESS + 2) >> 8) & 0xff);

	/*
	 * set up BIOS vector (jps to MAGIC_ADDRESS + 1 ... MAGIC_ADDRESS + 16)
	 */
	for (i = 0; i < BIOS_VECTOR_COUNT; i++) {
		t = MAGIC_ADDRESS + 1 + i;
		memory[BIOS_VECTOR + i * 3] = 0xc3 /* jp */;
		memory[BIOS_VECTOR + i * 3 + 1] = (t & 0xff);
		memory[BIOS_VECTOR + i * 3 + 2] = ((t >> 8) & 0xff);
	}
	/*
	 * set up fake DPB (used for all drives)
	 */
	/*
	 * SPT (sectors / track) 32 (randomly selected)
	 */
	memory[DPB] = (32 & 0xff);
	memory[DPB + 1] = ((32 >> 8) & 0xff);
	/*
	 * BSH (block shift) 7 (<-- 16K BLS)
	 */
	memory[DPB + 2] = 7;
	/*
	 * BLM (block mask) 127 (<-- 16K BLS)
	 */
	memory[DPB + 3] = 127;
	/*
	 * EXM (extent mask) 7 (<-- 16K BLS, DSM 511, i. e. 8MB drive)
	 */
	memory[DPB + 4] = 7;
	/*
	 * DSM (# data blocks - 1) 511 (<-- 16K BLS, 8MB drive)
	 */
	memory[DPB + 5] = (511 & 0xff);
	memory[DPB + 6] = ((511 >> 8) & 0xff);
	/*
	 * DSM (# directory entries - 1) 2047 (randomly selected)
	 */
	memory[DPB + 7] = (2047 & 0xff);
	memory[DPB + 8] = ((2047 >> 8) & 0xff);
	/*
	 * AL0, AL1 (directory block vector) 0xf0 0x00 (<-- 2047 DRM, 16K BLS)
	 */
	memory[DPB + 9] = 0xf0;
	memory[DPB + 10] = 0x00;
	/*
	 * CKS (directory check vector) 0 (fixed disk)
	 */
	memory[DPB + 11] = 0;
	memory[DPB + 12] = 0;
 	/*
	 * OFF (reserved tracks) 0 (none)
	 */
	memory[DPB + 13] = 0;
	memory[DPB + 14] = 0;
	/*
	 * set up fake ALV (used for all drives)
	 */
	memcpy(memory + ALV, memory + DPB + 9, 2);
	/*
	 * set up zero page
	 */
	/*
	 * set up WBOOT entry
	 */
	memory[BOOT] = 0xc3 /* jp */;
	memory[BOOT + 1] = ((BIOS_VECTOR + 3) & 0xff);
	memory[BOOT + 2] = (((BIOS_VECTOR + 3) >> 8) & 0xff);
	/*
	 * set up IOBYTE
	 */
	memory[IOBYTE] = 0x00;
	/*
	 * set up BDOS entry point
	 */
	memory[BDOS_ENTRY] = 0xc3 /* jp */;
	memory[BDOS_ENTRY + 1] = (BDOS_START & 0xff);
	memory[BDOS_ENTRY + 2] = ((BDOS_START >> 8) & 0xff);
	/*
	 * convert command line arguments to wchar_t and splice them into
	 * a DMA_SIZEd buffer
	 */
	buffer[0] = L'\0';
	bp = buffer;
	bfree = DMA_SIZE;
	for (i = 0; bfree && i < conf_argc; i++) {
		*bp++ = L' ';
		bfree--;
		n = mbstowcs(bp, conf_argv[i], bfree);
		if (n == (size_t) (-1)) {
			perr("invalid character in command line");
			rc = (-1);
			goto premature_exit;
		}
		bp += n;
		bfree -= n;
	}
	if (! bfree) {
		perr("to many command line arguments");
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * convert command line to CP/M character set and copy it to
	 * the default DMA area
	 */
	memory[DEFAULT_DMA] = bp - buffer;
	for (bp = buffer, i = DEFAULT_DMA + 1; *bp; bp++) {
		*bp = towupper(*bp);
		t = to_cpm(*bp);
		if (t == (-1)) {
			perr("invalid character in command line");
			rc = (-1);
			goto premature_exit;
		}
		memory[i++] = t;
	}
	/*
	 * set up the default FCBs at 0x005c and 0x006c
	 */
	setup_fcb(
	    conf_argc > 0 ? conf_argv[0] : "",
	    memory + DEFAULT_FCB_1);
	setup_fcb(
	    conf_argc > 1 ? conf_argv[1] : "",
	    memory + DEFAULT_FCB_2);
	/*
	 * point PC to the start of the TPA
	 */
	reg_pc = TPA_START;
	if (log_level > LL_ERRORS) {
		plog("starting execution of program %s", command_file);
	}
premature_exit:
	if (fp) fclose(fp);
	free(command_file);
	return rc;
}


/*
 * terminate program execution
 */
static void
bdos_system_reset(void) {
	static const char func[] = "system reset";
	SYS_ENTRY(func, 0);
	terminate = 1;
	term_reason = OK_TERM;
}


/*
 * column the BDOS thinks the cursor is in
 */
static int console_col = 0;


/*
 * output a newline to the console
 */
static void
put_crlf(void) {
	console_out(0x0d /* CR */);
	console_out(0x0a /* LF */);
	console_col = 0;
}


/*
 * output graphical character to the console; if the output device is the
 * VT52 emulation and the line end is reached, a line wrap is appended
 */
static void
put_graph(unsigned char c) {
	console_out(c);
	console_col++;
	if (conf_interactive && console_col == cols) put_crlf();
}


/*
 * output a character to the console, interpret BS, LF, HT, and CR, and
 * suppress all other control characters
 */
static void
put_char(unsigned char c) {
	int i;
	switch (c) {
	case 0x08 /* BS */:
		if (! console_col) return;
		console_out(c);
		console_col--;
		return;
	case 0x0a /* LF */:
		console_out(c);
		return;
	case 0x09 /* HT */:
		i = ((console_col / 8) + 1) * 8 - console_col;
		while (i) {
			put_graph(0x20 /* SPC */);
			i--;
		}
		return;
	case 0x0d /* CR */:
		console_out(c);
		console_col = 0;
		return;
	}
	/*
	 * suppress other control characters
	 */
	if (c < 0x20 /* SPC */ || c == 0x7f /* DEL */) return;
	put_graph(c);
}


/*
 * output control characters < SPC as ^ and an upper case letter
 */
static void
put_ctrl(unsigned char c) {
	if (c < 0x20 /* SPC */) {
		put_graph(0x5e /* ^ */);
		c += 0x40 /* @ */;
	}
	put_graph(c);
}


/*
 * get a byte from the console
 */
static unsigned char
get_char(void) {
	unsigned char c;
	c = console_in();
	put_char(c);
	return c;
}


/*
 * return a byte from the console in register A (waiting if none is ready),
 * echo characters, interpret BS, TAB, and CR, and LF
 */
static void
bdos_console_input(void) {
	static const char func[] = "console input";
	SYS_ENTRY(func, 0);
	reg_l = reg_a = get_char();
	reg_h = reg_b = 0;
	SYS_EXIT(func, REGS_A);
}


/*
 * send the byte in register E to the console
 */
static void
bdos_console_output(void) {
	static const char func[] = "console output";
	SYS_ENTRY(func, REGS_E);
	put_char(reg_e);
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_EXIT(func, 0);
}


/*
 * return a byte from the reader device in register A
 */
static void
bdos_reader_input(void) {
	static const char func[] = "reader input";
	SYS_ENTRY(func, 0);
	reg_l = reg_a = reader_in();
	reg_h = reg_b = 0;
	SYS_EXIT(func, REGS_A);
}


/*
 * send the byte in register E to the punch device
 */
static void
bdos_punch_output(void) {
	static const char func[] = "punch output";
	SYS_ENTRY(func, REGS_E);
	punch_out(reg_e);
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_EXIT(func, 0);
}


/*
 * send the byte in register E to the printer device
 */
static void
bdos_list_output(void) {
	static const char func[] = "list output";
	SYS_ENTRY(func, REGS_E);
	printer_out(reg_e);
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_EXIT(func, 0);
}


/*
 * If register E contains 0xff, a non-blocking read from the console
 * returns a character in register A or 0x00 if none is available;
 * otherwise, the character in register E is sent to the console.
 * No echoing or interpretation of control characters is performed
 * (calls to this function should not be mixed with the other
 * BDOS console I/O functions).
 */
static void
bdos_direct_console_io(void) {
	static const char func[] = "direct console io";
	SYS_ENTRY(func, REGS_E);
	if (reg_e == 0xff) {
		if (console_status()) {
			reg_l = reg_a = console_in();
		} else {
			reg_l = reg_a = 0x00;
		}
	} else {
		console_out(reg_e);
		reg_l = reg_a = 0;
	}
	reg_h = reg_b = 0;
	SYS_EXIT(func, REGS_A);
}


/*
 * return the value of the I/O byte (stored in location 0x0003) in
 * register A; the I/O byte functionality proper is not implemented.
 */
static void
bdos_get_io_byte(void) {
	static const char func[] = "get io byte";
	SYS_ENTRY(func, 0);
	reg_l = reg_a = memory[IOBYTE];
	reg_h = reg_b = 0;
	SYS_EXIT(func, REGS_A);
}


/*
 * set the value of the I/O byte from register E; see above.
 */
static void
bdos_set_io_byte(void) {
	static const char func[] = "set io byte";
	SYS_ENTRY(func, REGS_E);
	memory[IOBYTE] = reg_e;
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_EXIT(func, 0);
}


/*
 * output $-terminated text string from memory pointed to by DE
 */
static void
bdos_print_string(void) {
	int start, addr;
	addr = start = get_de();
	unsigned char byte;
	static const char func[] = "print string";
	SYS_ENTRY(func, REGS_DE);
	for (;;) {
		byte = memory[addr++];
		if (byte == 0x24 /* $ */) break;
		put_char(byte);
		if (addr == MEMORY_SIZE) {
			plog("print string: invalid string at 0x%04x", start);
			terminate = 1;
			term_reason = ERR_BDOSARG;
			break;
		}
	}
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_EXIT(func, 0);
}


/*
 * read data from console to a memory buffer pointed to by register DE
 */
static void
bdos_read_console_buffer(void) {
	int start_col, addr, size, curr, free, i;
	unsigned char c;
	static const char func[] = "read console buffer";
	SYS_ENTRY(func, REGS_DE);
	/*
	 * get and check buffer address
	 */
	addr = get_de();
	free = size = memory[addr];
	if (MEMORY_SIZE - addr < size + 2) {
		plog("read console buffer: invalid buffer 0x%04x", addr);
		terminate = 1;
		term_reason = ERR_BDOSARG;
		goto premature_exit;
	}
	curr = addr + 2;
	/*
	 * remember starting column (for retype or discard input)
	 */
	start_col = console_col;
	/*
	 * proceed as long there is room in the buffer
	 */
	while (free) {
		c =  console_in();
		/*
		 * at the start of a line, ^C terminates the program,
		 * otherwise it is an input character
		 */
		if (c == 0x03 /* ETX, ^C */) {
			if (free == size) {
				put_ctrl(c);
				put_crlf();
				terminate = 1;
				term_reason = OK_CTRLC;
				goto premature_exit;
			}
		}
		/*
		 * output physical end of line
		 */
		if (c == 0x05 /* ENQ, ^E */) {
			put_crlf();
			continue;
		}
		/*
		 * delete last character by overtyping
		 */
#if (0)
		if (c == 0x08 /* BS */) {
#else
		if (c == 0x08 /* BS */ || c == 0x7f /* DEL */) {
#endif
			if (free < size) {
				curr--;
				free++;
				put_char(0x08 /* BS */);
				put_graph(0x20 /* SPC */);
				put_char(0x08 /* BS */);
				if (memory[curr] < 0x20) {
					/*
					 * if the deleted character was
					 * a control character, overtype
					 * two characters
					 */
					put_char(0x08 /* BS */);
					put_graph(0x20 /* SPC */);
					put_char(0x08 /* BS */);
				}
			}
			continue;
		}
		/*
		 * regular end of input
		 */
		if (c == 0x0a /* LF */ || c == 0x0d /* CR */) break;
		/*
		 * retype the line
		 */
		if (c == 0x12 /* DC2, ^R */) {
			put_crlf();
			for (i = 0; i < start_col; i++) {
				put_graph(0x20 /* SPC */);
			}
			for (i = addr + 2; i < curr; i++) {
				put_ctrl(memory[i]);
			}
			continue;
		}
		/*
		 * discard all previous input
		 */
		if (c == 0x15 /* NAK, ^U */ || c == 0x18 /* CAN, ^X */) {
			put_crlf();
			for (i = 0; i < start_col; i++) {
				put_graph(0x20 /* SPC */);
			}
			curr = addr + 2;
			free = size;
			continue;
		}
#if (0)
		/*
		 * discard last character, echo deleted character
		 */
		if (c == 0x7f /* DEL */) {
			if (free < size) {
				curr--;
				free++;
				put_ctrl(memory[curr]);
			}
			continue;
		}
#endif
		/*
		 * echo character and store into buffer
		 */
		put_ctrl(c);
		memory[curr] = c;
		curr++;
		free--;
	}
	/*
	 * store number of bytes read to second byte of buffer
	 */
	memory[addr + 1] = size - free;
	/*
	 * emit a singe CR
	 */
	put_char(0x0d /* CR */);
premature_exit:
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_ENTRY(func, 0);
}


/*
 * return 0xff in register A if a character is ready from the console,
 * otherwise return 0x00
 */
static void
bdos_get_console_status(void) {
	static const char func[] = "get console status";
	SYS_ENTRY(func, 0);
	reg_l = reg_a = console_status() ? 0xff : 0x00;
	reg_h = reg_b = 0;
	SYS_ENTRY(func, REGS_A);
}


/*
 * return system version number in register A (we emulate CP/M 2.2, so
 * we have to return 0x22)
 */
static void
bdos_return_version_number(void) {
	static const char func[] = "return version number";
	SYS_ENTRY(func, 0);
	reg_l = reg_a = 0x22;
	reg_h = reg_b = 0;
	SYS_ENTRY(func, REGS_A);
}


/*
 * reset disk system: more or less a dummy, but resets the default drive to
 * the configuration default (as opposed to CP/M, which sets drive A) and the
 * DMA address to 0x0080
 */
static void
bdos_reset_disk_system(void) {
	static const char func[] = "reset disk system";
	FDOS_ENTRY(func, 0);
	disk_reset();
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, 0);
}


/*
 * set current drive to the value in register E
 */
static void
bdos_select_disk(void) {
	static const char func[] = "select disk";
	FDOS_ENTRY(func, REGS_E);
	/*
	 * drive number must be in the valid range (0...15)
	 */
	if (reg_e > 15) {
		plog("select disk: illegal disk %d", (int) reg_e);
		terminate = 1;
		term_reason = ERR_SELECT;
	} else {
		current_drive = reg_e;
		memory[DRVUSER] = (reg_e | (current_user << 4));
	}
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, 0);
}


#define L_UNIX_NAME (MB_LEN_MAX * 12 + 1)
/*
 * extract name from FCB and check for validity (ambiguity is o.k.),
 * and return it as Unix file name
 */
static int
get_unix_name(int fcb, char unix_name[L_UNIX_NAME], const char *caller) {
	int rc = (-1), i, e, t;
	unsigned char cpm_name[12 + 1 ], c, *cp;
	wint_t wc;
	char *up;
	cp = cpm_name;
	/*
	 * calculate length of filename
	 */
	i = fcb + 1;
	for (e = i + 8; e != i && memory[e - 1] == 0x20 /* SPC */; e--);
	/*
	 * filename must be at least one character long
	 */
	if (i == e) goto premature_exit;
	/*
	 * copy filename and check it for valid characters
	 */
	while (i != e) {
		c = memory[i++];
		if (! is_valid_in_cfn(c)) goto premature_exit;
		*cp++ = c;
	}
	/*
	 * calculate length of extension
	 */
	i = fcb + 9;
	for (e = i + 3; e != i && memory[e - 1] == 0x20 /* SPC */; e--);
	if (i != e) {
		/*
		 * if the extension is not empty, appemd a dot and
		 * the extension to the result
		 */
		*cp++ = 0x2e /* . */;
		while (i != e) {
			c = memory[i++];
			if (! is_valid_in_cfn(c)) goto premature_exit;
			*cp++ = c;
		}
	}
	/*
	 * terminate result
	 */
	*cp = 0x00 /* NUL */;
	/*
	 * convert to Unix charset and fold uppercase letters to
	 * lowercase letters
	 */
	wctomb(NULL, 0);
	for (up = unix_name, cp = cpm_name; *cp; cp++) {
		wc = from_cpm(*cp);
		if (wc == (-1)) goto premature_exit;
		wc = towlower(wc);
		t = wctomb(up, wc);
		if (t == (-1)) goto premature_exit;
		up += t;
	}
	*up = '\0';
	rc = 0;
premature_exit:
	if (rc) plog("%s (FCB 0x%04x): illegal file name", caller, fcb);
	return rc;
}


/*
 * check if filename is ambigous (i. e., contains question marks)
 */
static int
is_ambigous(const char *name) {
	return (strchr(name, '?') != NULL);
}


/*
 * get and check FCB address
 */
static int
get_fcb(int fcb_size, const char *caller) {
	int fcb = get_de();
	if (MEMORY_SIZE - fcb < fcb_size) {
		plog("%s (FCB 0x%04x): invalid address", caller, fcb);
		terminate = 1;
		term_reason = ERR_BDOSARG;
		fcb = (-1);
	} else {
		if (log_level >= LL_FCBS) {
			/*
			 * dump FCB
			 */
			plog("dump of FCB(0x%04x):", fcb);
			plog_dump(fcb, fcb_size);
		}
	}
	return fcb;
}


/*
 * get and check from fcb drive
 */
static int
get_drive(int fcb, const char *caller) {
	int drive;
	drive = memory[fcb];
	if (! drive) {
		drive = current_drive;
	} else {
		drive--;
	}
	if (drive > 15 || ! conf_drives[drive]) {
		plog("%s (FCB 0x%04x): illegal/unconfigured drive",
		    caller, fcb);
		terminate = 1;
		term_reason = ERR_SELECT;
		drive = (-1);
	}
	return drive;
}


/*
 * open FCB pointed to by register DE
 */
static void
bdos_open_file(void) {
	int fcb, extent, drive, fd = (-1), ambigous = 0, flags = 0;
	unsigned char temp_fcb[12];
	char unix_name[L_UNIX_NAME];
	struct file_list *flp = NULL, *tp;
	char *path = NULL;
	struct file_data *fdp;
	static const char func[] = "open file";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(33, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check ex (0...31), clear s2
	 */
	extent = memory[fcb + 12];
	if (extent > 31) {
		plog("%s (FCB 0x%04x): illegal extent number", func, fcb);
		goto premature_exit;
	}
	memory[fcb + 14]  = 0x00;
	/*
	 * get and check drive
	 */
	drive = get_drive(fcb, func);
	if (drive == (-1)) goto premature_exit;
	if (read_only[drive]) flags |= FILE_RODISK;
	/*
	 * extract name from FCB and check it
	 */
	if (get_unix_name(fcb, unix_name, func) == (-1)) goto premature_exit;
	/*
	 * file name may be ambigous
	 */
	ambigous = is_ambigous(unix_name);
	/*
	 * get a list of regular files matching the name in the FCB
	 * (usually, this will be a one-element list)
	 */
	flp = get_filelist(conf_drives[drive], unix_name, func);
	for (tp = flp; tp; tp = tp->next_p) {
		/*
		 * skip files to small for the given extent
		 */
		if (tp->size < ((off_t) extent) * 128) continue;
		/*
		 * matching file found: take name
		 */
		strcpy(unix_name, tp->name);
		break;
	}
	if (! tp) goto premature_exit;
	/*
	 * allocate and assemble file path
	 */
	path = alloc(strlen(conf_drives[drive]) + strlen(unix_name) + 2);
	sprintf(path, "%s/%s", conf_drives[drive], unix_name);
	/*
	 * open existing file
	 */
	if (flags) {
		/*
		 * disk r/o: file only can be read
		 */
		fd = open(path, O_RDONLY);
	} else {
		/*
		 * try to open r/w
		 */
		fd = open(path, O_RDWR);
		if (fd == (-1) && errno == EACCES) {
			/*
			 * if there is a problem with file access,
			 * assume that the file is r/o
			 */
			flags |= FILE_ROFILE;
			fd = open(path, O_RDONLY);
		}
	}
	if (fd == (-1)) {
		/*
		 * there is a file, but we cannot open it
		 */
		plog("%s (FCB 0x%04x): could not open %s: %s", func,
		    fcb, path, strerror(errno));
		terminate = 1;
		term_reason = ERR_HOST;
		goto premature_exit;
	}
	/*
	 * if the file name in the FCB was ambigous, update it
	 */
	if (ambigous) {
		setup_fcb(unix_name, temp_fcb);
		memcpy(memory + fcb + 1, temp_fcb + 1, 11);
	}
	/*
	 * create file structure
	 */
	fdp = create_filedata(fcb, func);
	if (! fdp) goto premature_exit;
	fdp->path = path;
	path = NULL;
	fdp->fd = fd;
	fd = (-1);
	fdp->flags = flags;
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	/*
	 * clean up
	 */
	if (fd != (-1)) close(fd);
	free_filelist(flp);
	free(path);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * close FCB pointed to by register DE
 */
static void
bdos_close_file(void) {
	int fcb;
	struct file_data **fdpp, *fdp;
	static const char func[] = "close file";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(33, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and verify FCB data structure
	 */
	fdpp = get_filedata_pp(fcb, func);
	if (! fdpp) goto premature_exit;
	fdp = *fdpp;
	/*
	 * some programs (e. g. dBase II) continue to use FCBs after
	 * a call to close; therefore, there is a option to support
	 * this
	 * */
	if (dont_close) {
		/*
		 * just mark the file as flushed
		 */
		fdp->flags &= ~FILE_WRITTEN;
		reg_a = 0x00;
		goto premature_exit;
	}
	/*
	 * remove file structure from the list
	 */
	*fdpp = fdp->next_p;
	/*
	 * remove the file reference from the FCB
	 */
	memory[fcb + 16] = memory[fcb + 17] =
	    memory[fcb + 18] = memory[fcb + 19] = 0x00;
	/*
	 * close the associated Unix file
	 */
	if (close(fdp->fd) == (-1)) {
		/*
		 * close failed: something is cleatly amiss
		 */
		plog("%s (FCB 0x%04x): close(%s) failed: %s", func, fcb,
		    fdp->path, strerror(errno));
		terminate = 1;
		term_reason = ERR_HOST;
	} else {
		/*
		 * success: always return directory code 0
		 */
		reg_a = 0x00;
	}
	/*
	 * mark the file descriptor as closed to avoid an error from
	 * free_fcb_data()
	 */
	fdp->fd = (-1);
	/*
	 * destroy file structure 
	 */
	free_filedata(fdp);
premature_exit:
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


static struct file_list *search_list_p = NULL;


/*
 * common end to bdos_search_for_first() and bdos_search_for_next():
 * return the first entry from the file list if there is one left
 */
static void
return_direntry(void) {
	unsigned char temp_fcb[12];
	struct file_list *flp;
	/*
	 * default: no more entries in the list
	 */
	reg_a = 0xff;
	/*
	 * get first entry from the list
	 */
	flp = search_list_p;
	if (! flp) goto premature_exit;
	/*
	 * construct a directory entry in the DMA area from the
	 * first entry in the list
	 */
	setup_fcb(flp->name, temp_fcb);
	memset(memory + current_dma, 0, 32);
	memset(memory + current_dma + 32, 0xe5, 96);
	memcpy(memory + current_dma + 1, temp_fcb + 1, 11);
	/*
	 * remove the first entry from the list
	 */
	search_list_p = flp->next_p;
	free(flp->name);
	free(flp);
	/*
	 * always return directory code 0, since the entry is
	 * always in the first 32 bytes of the DMA area
	 */
	reg_a = 0x00;
premature_exit:
	return;
}


/*
 * search for the first file matching the FCB pointed to by DE
 */
static void
bdos_search_for_first(void) {
	int fcb, drive;
	char unix_name[L_UNIX_NAME];
	static const char func[] = "search for first";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(32, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check drive
	 */
	drive = memory[fcb];
	if (drive == 0x3f /* ? */) {
		/*
		 * wildcard drive : since user areas are not supported,
		 * this simply specifies the current drive
		 */
		drive = current_drive;
	} else {
		drive = get_drive(fcb, func);
	}
	if (drive == (-1)) goto premature_exit;
	/*
	 * extract name from FCB and check it
	 */
	if (get_unix_name(fcb, unix_name, func) == (-1)) goto premature_exit;
	/*
	 * free old file list
	 */
	free_filelist(search_list_p);
	/*
	 * get a list of all matching file names
	 */
	search_list_p = get_filelist(conf_drives[drive], unix_name, func);
	/*
	 * return entry from the file list
	 */
	return_direntry();
premature_exit:
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


/*
 * return the next file name from the list established by
 * bdos_search_for_first()
 */
static void
bdos_search_for_next(void) {
	static const char func[] = "search for next";
	FDOS_ENTRY(func, 0);
	/*
	 * return entry from the file list
	 */
	return_direntry();
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


/*
 * delete files described by FCB pointed to by register DE
 */
static void
bdos_delete_file(void) {
	int fcb, drive, t;
	char unix_name[L_UNIX_NAME];
	struct file_list *flp = NULL, *tp;
	char *path = NULL;
	static const char func[] = "delete file";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(32, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check drive
	 */
	drive = get_drive(fcb, func);
	if (drive == (-1)) goto premature_exit;
	/*
	 * extract name from FCB and check it
	 */
	if (get_unix_name(fcb, unix_name, func) == (-1)) goto premature_exit;
	/*
	 * get a list of matching files
	 */
	flp = get_filelist(conf_drives[drive], unix_name, func);
	if (! flp) goto premature_exit;
	/*
	 * if the disk is write only, scream and die
	 */
	if (read_only[drive]) {
		plog("%s (FCB 0x%04x): write protected disk", func, fcb);
		terminate = 1;
		term_reason = ERR_RODISK;
		goto premature_exit;
	}
	/*
	 * traverse file list, deleting files
	 */
	for (tp = flp; tp; tp = tp->next_p) {
		/*
		 * build path
		 */
		path = alloc(sizeof conf_drives[drive] + strlen(tp->name) + 2);
		sprintf(path, "%s/%s", conf_drives[drive], tp->name);
		/*
		 * delete file
		 */
		t = unlink(path);
		if (t == (-1)) {
			/*
			 * failed: assume write protected file
			 */
			plog("%s (FCB 0x%04x): unlink(%s) failed: %s", func,
			    fcb, path, strerror(errno));
			terminate = 1;
			term_reason = ERR_ROFILE;
			goto premature_exit;
		}
		free(path);
		path = NULL;
	}
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	/*
	 * clean up
	 */
	free_filelist(flp);
	free(path);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * get and check FCB data structure: there must be a file data structure
 * referenced by the FCB address in DE
 */
static struct file_data *
file_fcb(int fcb, const char *caller) {
	struct file_data **fdpp = get_filedata_pp(fcb, caller);
	return fdpp ? *fdpp : NULL;
}


/*
 * get current sequential record offset from FCB
 */
static int
get_offset(int fcb, const char *caller) {
	unsigned char ex, s2, cr;
	int offset;
	/*
	 * get offset components
	 */
	s2 = memory[fcb + 14];
	ex = memory[fcb + 12];
	cr = memory[fcb + 32];
	/*
	 * offset must be in the range of 0 (start of file) to 65536
	 * (the record after the maximal record number)
	 */
	if (cr > 127 || ex > 31 || s2 > 16 || (s2 == 16 && (cr || ex))) {
		plog("%s (FCB 0x%04x): invalid file offset", caller, fcb);
		offset = (-1);
	} else {
		offset = s2;
		offset <<= 5;
		offset |= ex;
		offset <<= 7;
		offset |= cr;
	}
	return offset;
}


/*
 * store offset in FCB (offset is in the range 0...65536)
 */
static void
set_offset(int fcb, int offset) {
	memory[fcb + 32] = offset & 0x007f;
	memory[fcb + 12] = (offset >> 7) & 0x001f;
	memory[fcb + 14] = offset >> 12;
}


/*
 * read a 128 byte record from a file to the current DMA area;
 * return (-1) on EOF; incomplete records are filled with 0x1a (SUB/^Z)
 */
static int
read_record(int fcb, struct file_data *fdp, const char *caller) {
	unsigned char *bp = memory + current_dma;
	size_t n = 128;
	ssize_t t;
	while (n) {
		t = read(fdp->fd, bp, n);
		if (! t) break;
		if (t == (-1)) {
			plog("%s (FCB 0x%04x): read(%s) failed: %s", caller,
			    fcb, fdp->path, strerror(errno));
			terminate = 1;
			term_reason = ERR_HOST;
			break;
		}
		bp += t;
		n -=t;
	}
	if (n < 128) memset(bp, 0x1a /* SUB */, n);
	if (log_level >= LL_RECORDS && n < 128) plog_dump(current_dma, 128);
	return (n == 128) ? (-1) : 0;
}


/*
 * write a 128 byte record from the current DMA area to a file;
 * return (-1) on error
 */
static int
write_record(int fcb, struct file_data *fdp, const char *caller) {
	unsigned char *bp = memory + current_dma;
	size_t n = 128;
	ssize_t t;
	while (n) {
		t = write(fdp->fd, bp, n);
		if (t == (-1)) {
			plog("%s (FCB 0x%04x): write(%s) failed: %s", caller,
			    fcb, fdp->path, strerror(errno));
			terminate = 1;
			term_reason = ERR_HOST;
			break;
		}
		bp += t;
		n -=t;
	}
	fdp->flags |= FILE_WRITTEN;
	if (log_level >= LL_RECORDS) plog_dump(current_dma, 128);
	return n ? (-1) : 0;
}


/*
 * seek to a particular offset in a Unix file (the offset is given in
 * CP/M records of 128 bytes
 */
static int
seek(int fcb, struct file_data *fdp, int offset, const char *caller) {
	int rc = 0;
	off_t unix_offset;
	/*
	 * calculate unix file offset and set file position
	 */
	unix_offset = offset;
	unix_offset *= 128;
	if (lseek(fdp->fd, unix_offset, SEEK_SET) == (off_t) (-1)) {
		plog("%s (FCB 0x%04x): lseek(%s) failed: %s", caller, fcb,
		    fdp->path, strerror(errno));
		terminate = 1;
		term_reason = ERR_HOST;
		rc = (-1);
	}
	return rc;
}


/*
 * read next sequential record from the FCB pointed to by DE
 */
static void
bdos_read_sequential(void) {
	int fcb, offset;
	struct file_data *fdp = NULL;
	static const char func[] = "read sequential";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails (error code 0x01 is
	 * "reading unwritten data", i. e. EOF)
	 */
	reg_a = 0x01;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(33, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check corresponding FCB structure
	 */
	fdp = file_fcb(fcb, func);
	if (! fdp) goto premature_exit;
	/*
	 * get and check current file offset;
	 */
	offset = get_offset(fcb, func);
	if (offset == (-1) || offset == 65536) {
		/*
		 * error code 0x06 for "record out of range"
		 */
		reg_a = 0x06;
		goto premature_exit;
	}
	/*
	 * set file position
	 */
	if (seek(fcb, fdp, offset, func) == (-1)) goto premature_exit;
	/*
	 * read 128 bytes to the current DMA buffer
	 */
	if (read_record(fcb, fdp, func) == (-1)) goto premature_exit;
	/*
	 * advance offset in FCB
	 */
	set_offset(fcb, offset + 1);
	/*
	 * success
	 */
	reg_a = 0x00;
premature_exit:
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


/*
 * check if disk or file are writeable
 */
static int
check_writeable(int fcb, struct file_data *fdp, const char *caller) {
	int rc = (-1);
	/*
	 * disk read only?
	 */
	if (fdp->flags & FILE_RODISK) {
		plog("%s (FCB 0x%04x): %s: write protected disk", caller, fcb,
		    fdp->path);
		terminate = 1;
		term_reason = ERR_RODISK;
		goto premature_exit;
	}
	/*
	 * file read only?
	 */
	if (fdp->flags & FILE_ROFILE) {
		plog("%s (FCB 0x%04x): %s is write protected", caller, fcb,
		    fdp->path);
		terminate = 1;
		term_reason = ERR_ROFILE;
		goto premature_exit;
	}
	rc = 0;
premature_exit:
	return rc;
}


/*
 * write next sequential record to the FCB pointed to by DE
 */
static void
bdos_write_sequential(void) {
	int fcb, offset;
	struct file_data *fdp;
	static const char func[] = "write sequential";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails (0x02 means
	 * "no available data block", i. e. "disk full")
	 */
	reg_a = 0x02;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(33, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check corresponding FCB structure
	 */
	fdp = file_fcb(fcb, func);
	if (! fdp) goto premature_exit;
	/*
	 * check for write protection
	 */
	if (check_writeable(fcb, fdp, func) == (-1)) goto premature_exit;
	/*
	 * get and check current file offset;
	 */
	offset = get_offset(fcb, func);
	if (offset == (-1) || offset == 65536) {
		/*
		 * record out of range
		 */
		reg_a = 0x06;
		goto premature_exit;
	}
	/*
	 * set file position
	 */
	if (seek(fcb, fdp, offset, func) == (-1)) goto premature_exit;
	/*
	 * write 128 bytes from the current DMA buffer
	 */
	if (write_record(fcb, fdp, func) == (-1)) goto premature_exit;
	/*
	 * advance offset in FCB
	 */
	set_offset(fcb, offset + 1);
	/*
	 * success
	 */
	reg_a = 0;
premature_exit:
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


/*
 * create file described by FCB pointed to by register DE and open it
 */
static void
bdos_make_file(void) {
	int fcb, drive, fd = (-1);
	char unix_name[L_UNIX_NAME];
	char *path = NULL;
	struct file_data *fdp;
	static const char func[] = "make file";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(33, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * check ex (0...31), clear s2
	 */
	if (memory[fcb + 12] > 31) {
		plog("%s (FCB 0x%04x): illegal extent number", func, fcb);
		goto premature_exit;
	}
	memory[fcb + 14]  = 0x00;
	/*
	 * get and check drive
	 */
	drive = get_drive(fcb, func);
	if (drive == (-1)) goto premature_exit;
	/*
	 * drive must not be read only
	 */
	if (read_only[drive]) {
		plog("%s (FCB 0x%04x): disk write protected", func, fcb);
		terminate = 1;
		term_reason = ERR_RODISK;
		goto premature_exit;
	}
	/*
	 * extract name from FCB and check it
	 */
	if (get_unix_name(fcb, unix_name, func) == (-1)) goto premature_exit;
	/*
	 * file name may not be ambigous
	 */
	if (is_ambigous(unix_name)) {
		plog("%s (FCB 0x%04x): ambigous file name %s", func,
		    fcb, unix_name);
		goto premature_exit;
	}
	/*
	 * allocate and assemble file path
	 */
	path = alloc(strlen(conf_drives[drive]) + strlen(unix_name) + 2);
	sprintf(path, "%s/%s", conf_drives[drive], unix_name);
	/*
	 * create new file
	 */
	fd = open(path, O_CREAT|O_EXCL|O_RDWR, 0666);
	if (fd == (-1)) {
		plog("%s (FCB 0x%04x): could not create %s: %s", func, fcb,
		    path, strerror(errno));
		terminate = 1;
		term_reason = ERR_HOST;
		goto premature_exit;
	}
	/*
	 * create file structure
	 */
	fdp = create_filedata(fcb, func);
	fdp->path = path;
	path = NULL;
	fdp->fd = fd;
	fd = (-1);
	fdp->flags = 0;
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	/*
	 * clean up
	 */
	if (fd != (-1)) close(fd);
	free(path);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * rename file described by FCB pointed to by register DE
 */
static void
bdos_rename_file(void) {
	int fcb, drive;
	char unix_name_old[L_UNIX_NAME], unix_name_new[L_UNIX_NAME],
	    *path_old = NULL, *path_new = NULL;
	static const char func[] = "rename file";
	size_t l;
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(32, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check drive
	 */
	drive = get_drive(fcb, func);
	if (drive == (-1)) goto premature_exit;
	/*
	 * drive must not be read only
	 */
	if (read_only[drive]) {
		plog("%s (FCB 0x%04x): disk write protected", func, fcb);
		terminate = 1;
		term_reason = ERR_RODISK;
		goto premature_exit;
	}
	/*
	 * extract old and new names from FCB and check them
	 */
	if (get_unix_name(fcb, unix_name_old, func) == (-1)) {
		goto premature_exit;
	}
	if (get_unix_name(fcb + 16, unix_name_new, func) == (-1)) {
		goto premature_exit;
	}
	/*
	 * file names may not be ambigous
	 */
	if (is_ambigous(unix_name_old)) {
		plog("%s (FCB 0x%04x): ambigous old file name %s", func,
		    fcb, unix_name_old);
		goto premature_exit;
	}
	if (is_ambigous(unix_name_new)) {
		plog("%s (FCB 0x%04x): ambigous new file name %s", func,
		    fcb, unix_name_new);
		goto premature_exit;
	}
	/*
	 * allocate and assemble old and new file paths
	 */
	l = strlen(conf_drives[drive]);
	path_old = alloc(l + strlen(unix_name_old) + 2);
	sprintf(path_old, "%s/%s", conf_drives[drive], unix_name_old);
	path_new = alloc(l + strlen(unix_name_new) + 2);
	sprintf(path_new, "%s/%s", conf_drives[drive], unix_name_new);
	/*
	 * create new link
	 */
	if (link(path_old, path_new) == (-1)) {
		plog("%s (FCB 0x%04x): link(%s, %s) failed: %s", func,
		    fcb, path_old, path_new, strerror(errno));
		switch (errno) {
		case ENOENT:
		case EEXIST:
			break;
		case EACCES:
			terminate = 1;
			term_reason = ERR_ROFILE;
			break;
		default:
			terminate = 1;
			term_reason = ERR_HOST;
			break;
		}
		goto premature_exit;
	}
	/*
	 * delete old link
	 */
	if (unlink(path_old) == (-1)) {
		plog("%s (FCB 0x%04x): unlink(%s) failed: %s", func,
		    fcb, path_old, strerror(errno));
		terminate = 1;
		term_reason = ERR_HOST;
		unlink(path_new);
		goto premature_exit;
	}
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	/*
	 * clean up
	 */
	free(path_old);
	free(path_new);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * all configured drives are always logged in
 */
static void
bdos_return_log_in_vector(void) {
	static const char func[] = "return log in vector";
	int vector = 0, i;
	FDOS_ENTRY(func, 0);
	for (i = 15; i >= 0; i--) {
		vector <<= 1;
		vector |= (conf_drives[i] != NULL);
	}
	reg_l = reg_a = (vector & 0xff);
	reg_h = reg_b = ((vector >> 8) & 0xff);
	FDOS_EXIT(func, REGS_HL);
}


/*
 * return the current disk number
 */
static void
bdos_return_current_disk(void) {
	static const char func[] = "return current disk";
	FDOS_ENTRY(func, 0);
	reg_l = reg_a = current_drive;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * check and set new DMA address
 */
static void
bdos_set_dma_address(void) {
	static const char func[] = "set dma address";
	int addr = get_de();
	FDOS_ENTRY(func, REGS_DE);
	if (MEMORY_SIZE - addr < 128) {
		plog("set dma address: illegal address 0x%04x", addr);
		terminate = 1;
		term_reason = ERR_BDOSARG;
	} else {
		current_dma = addr;
	}
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, 0);
}


/*
 * the allocation vector is a dummy in this implementation; all drives
 * share the same allocation vector to save memory space
 */
static void
bdos_get_addr_alloc(void) {
	static const char func[] = "get addr alloc";
	FDOS_ENTRY(func, 0);
	reg_l = reg_a = (ALV & 0xff);
	reg_h = reg_b = ((ALV >> 8) & 0xff);
	FDOS_EXIT(func, REGS_HL);
}


/*
 * mark the current disk as write protected
 */
static void
bdos_write_protect_disk(void) {
	static const char func[] = "write protect disk";
	FDOS_ENTRY(func, 0);
	read_only[current_drive] = 1;
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, 0);
}


/*
 * return read-only array as bit vector
 */
static void
bdos_get_read_only_vector(void) {
	int vector = 0, i;
	static const char func[] = "get read only vector";
	FDOS_ENTRY(func, 0);
	for (i = 15; i >= 0; i--) {
		vector <<= 1;
		vector |= read_only[i];
	}
	reg_l = reg_a = (vector & 0xff);
	reg_h = reg_b = ((vector >> 8) & 0xff);
	FDOS_EXIT(func, REGS_HL);
}


/*
 * set file attributes is just a dummy, since file attributes are not
 * implemented; there is just some error checking
 */
static void
bdos_set_file_attributes(void) {
	int fcb, drive, i;
	char unix_name[L_UNIX_NAME], *path = NULL;
	static const char func[] = "set file attributes";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(32, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check drive
	 */
	drive = get_drive(fcb, func);
	if (drive == (-1)) goto premature_exit;
	/*
	 * drive must not be read only
	 */
	if (read_only[drive]) {
		plog("%s (FCB 0x%04x): disk write protected", func, fcb);
		terminate = 1;
		term_reason = ERR_RODISK;
		goto premature_exit;
	}
	/*
	 * silently remove the high bits from the file name
	 */
	for (i = 0; i < 11; i++) memory[fcb + 1 + i] &= 0x7f;
	if (get_unix_name(fcb, unix_name, func) == (-1)) goto premature_exit;
	/*
	 * file name may not be ambigous
	 */
	if (is_ambigous(unix_name)) {
		plog("%s (FCB 0x%04x): ambigous file name %s", func,
		    fcb, unix_name);
		goto premature_exit;
	}
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	/*
	 * clean up
	 */
	free(path);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * the disk parameters are dummy values (but consistent with the size
 * of the allocation vector); all drives share the same disk parameter block
 */
static void
bdos_get_addr_diskparams(void) {
	static const char func[] = "get addr diskparams";
	FDOS_ENTRY(func, 0);
	reg_l = reg_a = (DPB & 0xff);
	reg_h = reg_b = ((DPB >> 8) & 0xff);
	FDOS_EXIT(func, REGS_HL);
}


/*
 * user code manipulation is not supported, but tolerated
 */
static void
bdos_set_get_user_code(void) {
	static const char func[] = "get set user code";
	FDOS_ENTRY(func, REGS_E);
	if (reg_e == 0xff) {
		reg_l = reg_a = current_user;
	} else {
		current_user = (reg_e & 0x0f);
		reg_l = reg_a = 0;
	}
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * get random record number from FCB
 */
static int
get_random(int fcb, const char *caller) {
	unsigned char r0, r1, r2;
	int random;
	/*
	 * get random record number bytes
	 */
	r0 = memory[fcb + 33];
	r1 = memory[fcb + 34];
	r2 = memory[fcb + 35];
	/*
	 * random record number must be in the range of 0 (start of file)
	 * to 65536 (the record after the maximal record number)
	 */
	if (r2 > 1 || (r2 == 1 && (r0 || r1))) {
		plog("%s (FCB 0x%04x): invalid random record number",
		    caller, fcb);
		random = (-1);
	} else {
		random = r2;
		random <<= 8;
		random |= r1;
		random <<= 8;
		random |= r0;
	}
	return random;
}


/*
 * read random record from the FCB pointed to by DE
 */
static void
bdos_read_random(void) {
	int fcb, offset;
	struct file_data *fdp;
	static const char func[] = "read random";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails (0x01 is "reading unwritten data");
	 */
	reg_a = 0x01;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(36, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check corresponding file structure
	 */
	fdp = file_fcb(fcb, func);
	if (! fdp) goto premature_exit;
	/*
	 * get and check current random record number;
	 */
	offset = get_random(fcb, func);
	if (offset == (-1) || offset == 65536) {
		/*
		 * record out of range
		 */
		reg_a = 0x06;
		goto premature_exit;
	}
	/*
	 * set file position
	 */
	if (seek(fcb, fdp, offset, func) == (-1)) goto premature_exit;
	/*
	 * read 128 bytes to the current DMA buffer
	 */
	if (read_record(fcb, fdp, func) == (-1)) {
		/*
		 * report "reading unwritten data", i. e. EOF
		 */
		reg_a = 0x01;
		goto premature_exit;
	}
	/*
	 * set sequential offset in FCB
	 */
	set_offset(fcb, offset);
	/*
	 * report success
	 */
	reg_a = 0x00;
premature_exit:
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


/*
 * common functionality of bdos_write_random() and
 * bdos_write_random_with_zero_fill() (in fact, both functions are identical
 * but for their name, since unwritten records read as zero under Unix
 * anyway)
 */
static void
write_random(const char *caller) {
	int fcb, offset;
	struct file_data *fdp;
	/*
	 * assume the operation fails (0x05 means "no available directory
	 * space"; 0x02 "no available data block" would seem more adequate,
	 * but according to the CP/M documentation, it is not reported in
	 * random mode)
	 */
	reg_a = 0x05;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(36, caller);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check corresponding FCB structure
	 */
	fdp = file_fcb(fcb, caller);
	if (! fdp) goto premature_exit;
	/*
	 * check for write protection
	 */
	if (check_writeable(fcb, fdp, caller) == (-1)) goto premature_exit;
	/*
	 * get and check current random record number;
	 */
	offset = get_random(fcb, caller);
	if (offset == (-1) || offset == 65536) {
		/*
		 * record out of range
		 */
		reg_a = 0x06;
		goto premature_exit;
	}
	/*
	 * set file position
	 */
	if (seek(fcb, fdp, offset, caller) == (-1)) goto premature_exit;
	/*
	 * write 128 bytes from the current DMA buffer
	 */
	if (write_record(fcb, fdp, caller) == (-1)) goto premature_exit;
	/*
	 * set sequential offset in FCB
	 */
	set_offset(fcb, offset);
	/*
	 * report success
	 */
	reg_a = 0;
premature_exit:
	return;
}


/*
 * write random record from the FCB pointed to by DE
 */
static void
bdos_write_random(void) {
	static const char func[] = "write random";
	FDOS_ENTRY(func, REGS_DE);
	write_random(func);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * calculate file size and deposit it in the random record number of the
 * FCB pointed to by DE
 */
static void
bdos_compute_file_size(void) {
	int fcb, drive, t;
	off_t size;
	char unix_name[L_UNIX_NAME];
	char *path = NULL;
	struct stat s;
	static const char func[] = "compute file size";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(36, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get and check drive
	 */
	drive = get_drive(fcb, func);
	if (drive == (-1)) goto premature_exit;
	/*
	 * extract name from FCB and check it
	 */
	if (get_unix_name(fcb, unix_name, func) == (-1)) goto premature_exit;
	/*
	 * file name may not be ambigous
	 */
	if (is_ambigous(unix_name)) {
		plog("%s (FCB 0x%04x): ambigous file name %s", func,
		    fcb, unix_name);
		goto premature_exit;
	}
	/*
	 * allocate and assemble file path
	 */
	path = alloc(strlen(conf_drives[drive]) + strlen(unix_name) + 2);
	sprintf(path, "%s/%s", conf_drives[drive], unix_name);
	/*
	 * get size of file
	 */
	t = lstat(path, &s);
	if (t == (-1)) {
		plog("%s (FCB 0x%04x): lstat(%s) failed: %s", func, fcb,
		    path, strerror(errno));
		goto premature_exit;
	}
	if (! S_ISREG(s.st_mode)) {
		plog("%s (FCB 0x%04x): %s is no regular file", func, fcb,
		    path);
		goto premature_exit;
	}
	if (s.st_size > 8 * 1024 * 1024) {
		plog("%s (FCB 0x%04x): %s is larger than 8 MB", func, fcb,
		    path);
		goto premature_exit;
	}
	size = (s.st_size + 127) / 128;
	/*
	 * set random record number
	 */
	memory[fcb + 33] = (size & 0xff);
	memory[fcb + 34] = ((size >> 8) & 0xff);
	memory[fcb + 35] = ((size >> 16) & 0xff);
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	free(path);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


/*
 * set random record field from the current sequential offset
 */
static void
bdos_set_random_record(void) {
	int fcb, offset;
	static const char func[] = "set random record";
	FDOS_ENTRY(func, REGS_DE);
	/*
	 * assume the operation fails
	 */
	reg_a = 0xff;
	/*
	 * get and check FCB address
	 */
	fcb = get_fcb(36, func);
	if (fcb == (-1)) goto premature_exit;
	/*
	 * get current offset
	 */
	offset = get_offset(fcb, func);
	if (offset == (-1)) goto premature_exit;
	/*
	 * set random record number
	 */
	memory[fcb + 33] = (offset & 0xff);
	memory[fcb + 34] = ((offset >> 8) & 0xff);
	memory[fcb + 35] = ((offset >> 16) & 0xff);
	/*
	 * success: always return directory code 0
	 */
	reg_a = 0x00;
premature_exit:
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
	return;
}


/*
 * reset drives in vector in DE
 */
static void
bdos_reset_drive(void) {
	static const char func[] = "reset drive";
	FDOS_ENTRY(func, REGS_DE);
	int vector = get_de(), i;
	for (vector = get_de(), i = 0; i < 16; vector >>= 1, i++) {
		if (! (vector & 1)) continue;
		if (! conf_drives[i]) {
			plog("reset drive: illegal disk %d", i);
			terminate = 1;
			term_reason = ERR_SELECT;
			continue;
		}
		read_only[i] = conf_readonly[i];
	}
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, 0);
}


/*
 * unsupported BDOS call; these simply return zero
 */
static void
bdos_unsupported(void) {
	char func[64];
	sprintf(func, "unsupported BDOS function %d", (int) reg_c);
	SYS_ENTRY(func, REGS_DE);
	reg_l = reg_a = 0;
	reg_h = reg_b = 0;
	SYS_EXIT(func, REGS_HL);
}


/*
 * write random record from the FCB pointed to by DE, zeroing allocated but
 * unwritten records
 */
static void
bdos_write_random_with_zero_fill(void) {
	static const char func[] = "write random with zero fill";
	FDOS_ENTRY(func, REGS_DE);
	write_random(func);
	reg_l = reg_a;
	reg_h = reg_b = 0;
	FDOS_EXIT(func, REGS_A);
}


#define BDOS_COUNT 41

/*
 * function dispatcher table for the BDOS functions
 */
static void (*bdos_functions_p[BDOS_COUNT])(void) = {
/*0*/	bdos_system_reset,
/*1*/	bdos_console_input,
/*2*/	bdos_console_output,
/*3*/	bdos_reader_input,
/*4*/	bdos_punch_output,
/*5*/	bdos_list_output,
/*6*/	bdos_direct_console_io,
/*7*/	bdos_get_io_byte,
/*8*/	bdos_set_io_byte,
/*9*/	bdos_print_string,
/*10*/	bdos_read_console_buffer,
/*11*/	bdos_get_console_status,
/*12*/	bdos_return_version_number,
/*13*/	bdos_reset_disk_system,
/*14*/	bdos_select_disk,
/*15*/	bdos_open_file,
/*16*/	bdos_close_file,
/*17*/	bdos_search_for_first,
/*18*/	bdos_search_for_next,
/*19*/	bdos_delete_file,
/*20*/	bdos_read_sequential,
/*21*/	bdos_write_sequential,
/*22*/	bdos_make_file,
/*23*/	bdos_rename_file,
/*24*/	bdos_return_log_in_vector,
/*25*/	bdos_return_current_disk,
/*26*/	bdos_set_dma_address,
/*27*/	bdos_get_addr_alloc,
/*28*/	bdos_write_protect_disk,
/*29*/	bdos_get_read_only_vector,
/*30*/	bdos_set_file_attributes,
/*31*/	bdos_get_addr_diskparams,
/*32*/	bdos_set_get_user_code,
/*33*/	bdos_read_random,
/*34*/	bdos_write_random,
/*35*/	bdos_compute_file_size,
/*36*/  bdos_set_random_record,
/*37*/	bdos_reset_drive,
/*38*/	bdos_unsupported,
/*39*/	bdos_unsupported,
/*40*/	bdos_write_random_with_zero_fill
};


/*
 * BDOS call
 */
static void
magic_bdos(void) {
	if (reg_c < BDOS_COUNT) {
		(*bdos_functions_p[reg_c])();
	} else {
		bdos_unsupported();
	}
}


/*
 * BIOS BOOT
 */
static void
magic_boot(void) {
	static const char func[] = "bios boot";
	SYS_ENTRY(func, 0);
	/*
	 * no program should ever call BOOT
	 */
	perr("%s called by program", func);
	term_reason = ERR_BOOT;
	terminate = 1;
}


/*
 * BIOS WBOOT
 */
static void
magic_wboot(void) {
	static const char func[] = "bios wboot";
	SYS_ENTRY(func, 0);
	term_reason = OK_TERM;
	terminate = 1;
}


/*
 * BIOS CONST
 */
static void
magic_const(void) {
	static const char func[] = "bios const";
	SYS_ENTRY(func, 0);
	reg_a = console_status() ? 0xff : 0x00;
	SYS_EXIT(func, REGS_A);
}


/*
 * BIOS CONIN (high bit is not stripped!)
 */
static void
magic_conin(void) {
	static const char func[] = "bios conin";
	SYS_ENTRY(func, 0);
	reg_a = console_in();
	SYS_EXIT(func, REGS_A);
}


/*
 * BIOS CONOUT
 */
static void
magic_conout(void) {
	static const char func[] = "bios conout";
	SYS_ENTRY(func, REGS_C);
	console_out(reg_c);
	SYS_EXIT(func, 0);
}


/*
 * BIOS LIST
 */
static void
magic_list(void) {
	static const char func[] = "bios list";
	SYS_ENTRY(func, REGS_C);
	printer_out(reg_c);
	SYS_EXIT(func, 0);
}


/*
 * BIOS PUNCH
 */
static void
magic_punch(void) {
	static const char func[] = "bios punch";
	SYS_ENTRY(func, REGS_C);
	punch_out(reg_c);
	SYS_EXIT(func, 0);
}


/*
 * BIOS READER
 */
static void
magic_reader(void) {
	static const char func[] = "bios reader";
	SYS_ENTRY(func, 0);
	reg_a = reader_in();
	SYS_EXIT(func, REGS_A);
}


/*
 * BIOS HOME
 */
static void
magic_home(void) {
	static const char func[] = "bios home";
	SYS_ENTRY(func, 0);
	SYS_EXIT(func, 0);
}


/*
 * BIOS SELDSK
 */
static void
magic_seldsk(void) { 
	static const char func[] = "bios seldsk";
	SYS_ENTRY(func, REGS_C|REGS_E);
	/*
	 * report nonexisting drive
	 */
	reg_h = reg_l = 0x00;
	SYS_EXIT(func, REGS_HL);
}


/*
 * BIOS SETTRK
 */
static void
magic_settrk(void) {
	static const char func[] = "bios settrk";
	SYS_ENTRY(func, REGS_BC);
	SYS_EXIT(func, 0);
}


/*
 * BIOS SETSEC
 */
static void
magic_setsec(void) {
	static const char func[] = "bios setsec";
	SYS_ENTRY(func, REGS_BC);
	SYS_EXIT(func, 0);
}


/*
 * BIOS SETDMA
 */
static void
magic_setdma(void) {
	static const char func[] = "bios setdma";
	SYS_ENTRY(func, REGS_BC);
	SYS_EXIT(func, 0);
}


/*
 * BIOS READ
 */
static void
magic_read(void) { 
	static const char func[] = "bios read";
	SYS_ENTRY(func, 0);
	/*
	 * report an error
	 */
	reg_a = 1; 
	SYS_EXIT(func, REGS_A);
}


/*
 * BIOS WRITE
 */
static void
magic_write(void) { 
	static const char func[] = "bios write";
	SYS_ENTRY(func, REGS_C);
	/*
	 * report an error
	 */
	reg_a = 1; 
	SYS_EXIT(func, REGS_A);
}


/*
 * BIOS LISTST
 */
static void
magic_listst(void) {
	static const char func[] = "bios listst";
	SYS_ENTRY(func, 0);
	reg_a = printer_status() ? 0xff : 0x00;
	SYS_EXIT(func, REGS_A);
}


/*
 * BIOS SECTRAN
 */
static void
magic_sectran(void) {
	static const char func[] = "bios sectran";
	SYS_ENTRY(func, REGS_BC|REGS_DE);
	reg_l = reg_c;
	reg_h = reg_b;
	SYS_EXIT(func, REGS_HL);
}


/*
 * this routine is a tnylpo specific extension hiding as 18th BIOS routine:
 * it takes the value in register BC and delays program execution for this
 * many wall clock milliseconds.
 */
static void
magic_delay(void) {
	static const char func[] = "tnylpo delay";
	int delay;
	struct timeval end, t;
	SYS_ENTRY(func, REGS_BC);
	delay = get_bc();
	/*
	 * calculate absolute end time
	 */
	gettimeofday(&end, NULL);
	end.tv_sec += delay / 1000;
	end.tv_usec += (delay % 1000) * 1000;
	if (end.tv_usec >= 1000000L) {
		end.tv_sec++;
		end.tv_usec -= 1000000L;
	}
	for (;;) {
		/*
		 * calculate the difference between the current time
		 * and the end time
		 */
		gettimeofday(&t, NULL);
		t.tv_usec = end.tv_usec - t.tv_usec;
		t.tv_sec = end.tv_sec - t.tv_sec;
		if (t.tv_usec < 0) {
			t.tv_usec += 1000000L;
			t.tv_sec--;
		}
		/*
		 * if the difference is not positive, stop
		 */
		if (t.tv_sec < 0 || (t.tv_sec == 0 && t.tv_usec == 0)) break;
		/*
		 * calculate the minimum between the remaining time and
		 * the quarter of a second
		 */
		if (t.tv_sec > 0 || t.tv_usec > 250000L) {
			t.tv_sec = 0;
			t.tv_usec = 250000L;
		}
		/*
		 * wait this long
		 */ 
		select(0, NULL, NULL, NULL, &t);
		/*
		 * poll the console to keep window resizing etc. happy
		 */
		console_poll();
	}
	SYS_EXIT(func, 0);
}


/*
 * dispatcher table for the magic addresses
 */
static void (*magic_functions_p[1 + BIOS_VECTOR_COUNT])(void) = {
/* BDOS */	magic_bdos,
/* BOOT */	magic_boot,
/* WBOOT */	magic_wboot,
/* CONST */	magic_const,
/* CONIN */	magic_conin,
/* CONOUT */	magic_conout,
/* LIST */	magic_list,
/* PUNCH */	magic_punch,
/* READER */	magic_reader,
/* HOME */	magic_home,
/* SELDSK */	magic_seldsk,
/* SETTRK */	magic_settrk,
/* SETSEC */	magic_setsec,
/* SETDMA */	magic_setdma,
/* READ */	magic_read,
/* WRITE */	magic_write,
/* LISTST */	magic_listst,
/* SECTRAN */	magic_sectran,
/* delay */	magic_delay
};


/*
 * handle call to the OS: magic determines the call:
 * magic == 0: BDOS call, reg_c contains function number
 * magic == 1 ... 17: call to one of the 17 CP/M 2.2 BIOS entries
 * magic == 18: call to the emulator delay routine
 */
void
os_call(int magic) { (*magic_functions_p[magic])(); }


/*
 * finalize OS emulation
 */
int
os_exit(void) {
	int rc = 0;
	struct file_data *fdp;
	/*
	 * reset disk subsystem
	 */
	disk_reset();
	/*
	 * tear down file list
	 */
	while (first_file_p) {
		fdp = first_file_p;
		first_file_p = fdp->next_p;
		free_filedata(fdp);
	}
	return rc;
}
