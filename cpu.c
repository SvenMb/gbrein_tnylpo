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
#include <wchar.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>

#include <sys/time.h>
#include <unistd.h>

#include "tnylpo.h"


unsigned char *memory = NULL;


/*
 * CPU status
 */
static int flag_i = 0;


/*
 * CPU registers and flags
 */
int reg_sp = 0;
int reg_pc = 0;
unsigned char reg_a = 0;
unsigned char reg_b = 0;
unsigned char reg_c = 0;
unsigned char reg_d = 0;
unsigned char reg_e = 0;
unsigned char reg_h = 0;
unsigned char reg_l = 0;
static unsigned char alt_reg_a = 0;
static unsigned char alt_reg_b = 0;
static unsigned char alt_reg_c = 0;
static unsigned char alt_reg_d = 0;
static unsigned char alt_reg_e = 0;
static unsigned char alt_reg_h = 0;
static unsigned char alt_reg_l = 0;
static unsigned char reg_ixh = 0;
static unsigned char reg_ixl = 0;
static unsigned char reg_iyh = 0;
static unsigned char reg_iyl = 0;
static unsigned char reg_r = 0;
static unsigned char reg_i = 0;
static int flag_s = 0;
static int flag_z = 0;
static int flag_y = 0;
static int flag_h = 0;
static int flag_x = 0;
static int flag_p = 0;
#define flag_v flag_p
static int flag_n = 0;
static int flag_c = 0;
static int alt_flag_s = 0;
static int alt_flag_z = 0;
static int alt_flag_y = 0;
static int alt_flag_h = 0;
static int alt_flag_x = 0;
static int alt_flag_p = 0;
static int alt_flag_n = 0;
static int alt_flag_c = 0;


/*
 * termination flag
 */
int terminate = 0;


/*
 * dump flag
 */
static sig_atomic_t dump = 0;


/*
 * reason for termination
 */
enum reason term_reason = OK_NOTRUN;


/*
 * start of current instruction including prefixes
 */
static int current_instruction = (-1);
/*
 * parts of the current instruction
 */
static int prefix, opcode, opcode2, op_low, op_high, disp;
/*
 * the mysterious internal register which is sometimes visible via X3, X5
 */
static int internal = 0;



/*
 * get word from memory
 */
static inline int
get_word(int address) {
	int word = memory[(address + 1) & 0xffff];
	word <<= 8;
	word |= memory[address];
	return word;
}


/*
 * store word to memory
 */
static inline void
set_word(int address, int word) {
	memory[address] = (word & 0xff);
	memory[(address + 1) & 0xffff] = ((word >> 8) & 0xff);
}


static inline int
get_bc(void) { int bc = reg_b; bc <<= 8; bc |= reg_c; return bc; }

static inline void
set_bc(int bc) { reg_c = bc & 0xff; reg_b = (bc >> 8) & 0xff; }

static inline int
get_de(void) { int de = reg_d; de <<= 8; de |= reg_e; return de; }

static inline void
set_de(int de) { reg_e = de & 0xff; reg_d = (de >> 8) & 0xff; }

static inline int
get_hl(void) { int hl = reg_h; hl <<= 8; hl |= reg_l; return hl; }

static inline void
set_hl(int hl) { reg_l = hl & 0xff; reg_h = (hl >> 8) & 0xff; }

static inline int
get_ix(void) { int ix = reg_ixh; ix <<= 8; ix |= reg_ixl; return ix; }

static inline void
set_ix(int ix) { reg_ixl = ix & 0xff; reg_ixh = (ix >> 8) & 0xff; }

static inline int
get_iy(void) { int iy = reg_iyh; iy <<= 8; iy |= reg_iyl; return iy; }

static inline void
set_iy(int iy) { reg_iyl = iy & 0xff; reg_iyh = (iy >> 8) & 0xff; }


/*
 * dump registers and memory to log file
 */
static void
dump_machine(const char *label) {
	plog("start of %s machine dump", label);
	plog("a=%02x f=%c%c%c%c%c%c%c%c bc=%04x de=%04x hl=%04x",
	    reg_a, flag_s ? 's' : '-', flag_z ? 'z' : '-',
	    flag_y ? 'y' : '-', flag_h ? 'h' : '-', flag_x ? 'x' : '-',
	    flag_p ? 'p' : '-', flag_n ? 'n' : '-', flag_c ? 'c' : '-',
	    get_bc(), get_de(), get_hl());
	plog("a\'=%02x f\'=%c%c%c%c%c%c%c%c bc\'=%04x de\'=%04x hl\'=%04x",
	    alt_reg_a, alt_flag_s ? 's' : '-',
	    alt_flag_z ? 'z' : '-', alt_flag_y ? 'y' : '-',
	    alt_flag_h ? 'h' : '-', alt_flag_x ? 'x' : '-',
	    alt_flag_p ? 'p' : '-', alt_flag_n ? 'n' : '-',
	    alt_flag_c ? 'c' : '-', (alt_reg_b << 8) | alt_reg_c,
	    (alt_reg_d << 8) | alt_reg_e, (alt_reg_h << 8) | alt_reg_l);
	plog("ix=%04x iy=%04x sp=%04x pc=%04x, r=%02x i=%02x",
	    get_ix(), get_iy(), reg_sp, reg_pc, reg_r, reg_i);
	plog("interrupts %s", flag_i ? "enabled" : "disabled");
	plog_dump(0, MEMORY_SIZE);
	plog("end of %s machine dump", label);
}


/*
 * initialize the CPU emulator: allocate main memory, initialize OS emulation
 */
int
cpu_init(void) {
	int rc = 0;
	struct timeval tv;
	/*
	 * allocate and clear Z80 memory
	 */
	memory = alloc(MEMORY_SIZE);
	memset(memory, 0, MEMORY_SIZE);
	/*
	 * set register R to some random value; programs (e. g. Turbo Pascal)
	 * use R for generating random numbers
	 *
	 * No, this random number is not suitable for
	 * cryptographical purposes.
	 */
	gettimeofday(&tv, NULL);
	srand((unsigned) tv.tv_usec);
	reg_r = (rand() & 0x7f);
	/*
	 * initialize OS emulation
	 */
	rc = os_init();
	if (rc) goto premature_exit;
	/*
	 * perform startup dump
	 */
	if (conf_dump & DUMP_STARTUP) dump_machine("startup");
premature_exit:
	if (rc) free(memory);
	return rc;
}


/*
 * get word from stack
 */
static int
pop(void) {
	int word;
	word = memory[reg_sp];
	reg_sp = ((reg_sp + 1) & 0xffff);
	word |= (((int) memory[reg_sp]) << 8);
	reg_sp = ((reg_sp + 1) & 0xffff);
	return word;
}


/*
 * put word on stack
 */
static void
push(int word) {
	reg_sp = ((reg_sp + 0xffff) & 0xffff);
	memory[reg_sp] = ((word >> 8) & 0xff);
	reg_sp = ((reg_sp + 0xffff) & 0xffff);
	memory[reg_sp] = word & 0xff;
}


/*
 * do nothing
 */
static void
inst_nop(void) { }


/*
 * load immediate extended
 */
static void
inst_lxi(void) {
	switch (opcode & 0x30) {
	case 0x00:
		reg_c = op_low;
		reg_b = op_high;
		break;
	case 0x10:
		reg_e = op_low;
		reg_d = op_high;
		break;
	case 0x20:
		if (prefix == 0xdd) {
			reg_ixl = op_low;
			reg_ixh = op_high;
		} else if (prefix == 0xfd) {
			reg_iyl = op_low;
			reg_iyh = op_high;
		} else {
			reg_l = op_low;
			reg_h = op_high;
		}
		break;
	case 0x30:
		reg_sp = op_high;
		reg_sp <<= 8;
		reg_sp |= op_low;
		break;
	}
}


/*
 * store A extended
 */
static void
inst_stax(void) {
	memory[(opcode & 0x10) ? get_de() : get_bc()] = reg_a;
}


/*
 * load A extended
 */
static void
inst_ldax(void) {
	reg_a = memory[(opcode & 0x10) ? get_de() : get_bc()];
}


/*
 * store A
 */
static void
inst_sta(void) {
	int addr;
	addr = op_high;
	addr <<= 8;
	addr |= op_low;
	memory[addr] = reg_a;
}


/*
 * load A
 */
static void
inst_lda(void) {
	int addr;
	addr = op_high;
	addr <<= 8;
	addr |= op_low;
	reg_a = memory[addr];
}


/*
 * store HL/IX/IY
 */
static void
inst_shld(void) {
	int addr;
	unsigned char *lp, *hp;
	addr = op_high;
	addr <<= 8;
	addr |= op_low;
	lp = memory + addr;
	hp = memory + ((addr + 1) & 0xffff);
	switch (prefix) {
	case 0x00:
		*lp = reg_l;
		*hp = reg_h;
		break;
	case 0xdd:
		*lp = reg_ixl;
		*hp = reg_ixh;
		break;
	case 0xfd:
		*lp = reg_iyl;
		*hp = reg_iyh;
		break;
	}
}


/*
 * load HL/IX/IY
 */
static void
inst_lhld(void) {
	int addr;
	unsigned char *lp, *hp;
	addr = op_high;
	addr <<= 8;
	addr |= op_low;
	lp = memory + addr;
	hp = memory + ((addr + 1) & 0xffff);
	switch (prefix) {
	case 0x00:
		reg_l = *lp;
		reg_h = *hp;
		break;
	case 0xdd:
		reg_ixl = *lp;
		reg_ixh = *hp;
		break;
	case 0xfd:
		reg_iyl = *lp;
		reg_iyh = *hp;
		break;
	}
}


/*
 * relative jump
 */
static void
inst_jr(void) {
	internal = op_low;
	if (internal & 0x80) internal |= 0xff00;
	internal = (internal + reg_pc) & 0xffff;
	reg_pc = internal;
}


/*
 * conditional relative jump
 */
static void
inst_jrcc(void) {
	switch (opcode & 0x18) {
	case 0x00: if (! flag_z) inst_jr(); break;
	case 0x08: if (flag_z) inst_jr(); break;
	case 0x10: if (! flag_c) inst_jr(); break;
	case 0x18: if (flag_c) inst_jr(); break;
	}
}


/*
 * DJNZ
 */
static void
inst_djnz(void) {
	reg_b = reg_b ? reg_b - 1 : 0xff;
	if (reg_b) inst_jr();
}


/*
 * EX AF,AF'
 */
static void
inst_exaf(void) {
	unsigned char uc;
	int i;
	uc = reg_a; reg_a = alt_reg_a; alt_reg_a = uc;
	i = flag_c; flag_c = alt_flag_c; alt_flag_c = i;
	i = flag_n; flag_n = alt_flag_n; alt_flag_n = i;
	i = flag_p; flag_v = alt_flag_p; alt_flag_p = i;
	i = flag_x; flag_x = alt_flag_x; alt_flag_x = i;
	i = flag_h; flag_h = alt_flag_h; alt_flag_h = i;
	i = flag_y; flag_y = alt_flag_y; alt_flag_y = i;
	i = flag_z; flag_z = alt_flag_z; alt_flag_z = i;
	i = flag_s; flag_s = alt_flag_s; alt_flag_s = i;
}


/*
 * set carry flag
 */
static void
inst_scf(void) {
	flag_y = ((reg_a & 0x20) != 0);
	flag_h = 0;
	flag_x = ((reg_a & 0x08) != 0);
	flag_n = 0;
	flag_c = 1;
}


/*
 * complement carry flag
 */
static void
inst_ccf(void) {
	flag_y = ((reg_a & 0x20) != 0);
	flag_h = flag_c;
	flag_x = ((reg_a & 0x08) != 0);
	flag_n = 0;
	flag_c = (flag_c == 0);
}


/*
 * halt CPU (dummy; just works as if an NMI had occurred)
 */
static void
inst_halt(void) {
	plog("0x%04x: HALT executed", current_instruction);
}


/*
 * complement A
 */
static void
inst_cpl(void) {
	reg_a ^= 0xff;
	flag_y = ((reg_a & 0x20) != 0);
	flag_h = 1;
	flag_x = ((reg_a & 0x08) != 0);
	flag_n = 1;
}


/*
 * helper function for inst_rla(), inst_rlca(), inst_rra(), and inst_rrca()
 */
static void
rot_flags(void) {
	flag_y = ((reg_a & 0x20) != 0);
	flag_h = 0;
	flag_x = ((reg_a & 0x08) != 0);
	flag_n = 0;
}


/*
 * 9-Bit rotation of A to the left
 */
static void
inst_rla(void) {
	unsigned t;
	t = reg_a;
	t <<= 1;
	t |= flag_c;
	flag_c = ((t & 0x100) == 0x100);
	reg_a = (unsigned char) (t & 0xff);
	rot_flags();
}


/*
 * 8-Bit rotation of A to the left
 */
static void
inst_rlca(void) {
	flag_c = ((reg_a & 0x80) == 0x80);
	reg_a <<= 1;
	reg_a &= 0xff;
	reg_a |= flag_c;
	rot_flags();
}


/*
 * 9-Bit rotation of A to the right
 */
static void
inst_rra(void) {
	unsigned t;
	t = reg_a;
	if (flag_c) t |= 0x100;
	flag_c = ((t & 0x01) == 0x01);
	t >>= 1;
	reg_a = (unsigned char) (t & 0xff);
	rot_flags();
}


/*
 * 8-Bit rotation of A to the right
 */
static void
inst_rrca(void) {
	flag_c = ((reg_a & 0x01) == 0x01);
	reg_a >>= 1;
	if (flag_c) reg_a |= 0x80;
	rot_flags();
}


/*
 * return address of 8-bit register/memory operand
 * n is the 3-bit field from the opcode describing the register/memory operand:
 * (0=b, 1=c, 2=d, 3=e, 4=h/ixh/iyh, 5=l/ixl/iyl, 6=(hl)/(ix+d)/(iy+d), 7=a)
 * a is the second 3-bit operand field from the same opcode (or zero if
 * there is none)
 */
static unsigned char *
operand8(int n, int a) {
	switch (n) {
	case 0: return &reg_b;
	case 1: return &reg_c;
	case 2: return &reg_d;
	case 3: return &reg_e;
	case 4:
		if (a == 6) return &reg_h;
		switch (prefix) {
		case 0xdd: return &reg_ixh;
		case 0xfd: return &reg_iyh;
		default: return &reg_h;
		}
	case 5:
		if (a == 6) return &reg_l;
		switch (prefix) {
		case 0xdd: return &reg_ixl;
		case 0xfd: return &reg_iyl;
		default: return &reg_l;
		}
	case 6:
		switch (prefix) {
		case 0xdd:
			internal = disp;
			if (internal & 0x80) internal |= 0xff00;
			internal = (get_ix() + internal) & 0xffff;
			return memory + internal;
		case 0xfd:
			internal = disp;
			if (internal & 0x80) internal |= 0xff00;
			internal = (get_iy() + internal) & 0xffff;
			return memory + internal;
		default:
			return memory + get_hl();
		}
	default: return &reg_a;
	}

}


/*
 * move 8-bit data
 */
static void
inst_mov(void) {
	int d, s;
	unsigned char *dp, *sp;
	d = ((opcode >> 3) & 0x07);
	s = (opcode & 0x07);
	dp = operand8(d, s);
	sp = operand8(s, d);
	*dp = *sp;
}


/*
 * move 8-bit immediate
 */
static void
inst_mvi(void) {
	*operand8((opcode >> 3) & 0x07, 0) = op_low;
}


/*
 * returns sumand1 + summand2 + carry, sets flags
 */
static unsigned char
add8(unsigned char summand1, unsigned char summand2, int carry) {
	int c6;
	unsigned s1 = summand1, s2 = summand2, su, cy = carry ? 1 : 0;
	/*
	 * unrolled loop
	 */
	su = (s1 ^ s2 ^ cy) & 0x01;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x01;
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x02;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x02;
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x04;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x04;
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x08;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x08;
	flag_h = (cy != 0);
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x10;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x10;
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x20;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x20;
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x40;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x40;
	c6 = (cy != 0);
	cy <<= 1;
	su |= (s1 ^ s2 ^ cy) & 0x80;
	cy = ((s2 & cy) | (s1 & (s2 | cy))) & 0x80;
	flag_c = (cy != 0);
	flag_n = 0;
	flag_v = flag_c ^ c6;
	flag_x = ((su & 0x08) != 0);
	flag_y = ((su & 0x20) != 0);
	flag_z = (su == 0);
	flag_s = ((su & 0x80) != 0);
	return (unsigned char) su;
}


/*
 * returns minuend - subtrahend - carry, sets flags
 */
static unsigned char
sub8(unsigned char minuend, unsigned char subtrahend, int carry) {
	int c6;
	unsigned mi = minuend, sb = subtrahend, df, cy = carry ? 1 : 0;
	/*
	 * unrolled loop
	 */
	df = (mi ^ sb ^ cy) & 0x01;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x01;
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x02;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x02;
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x04;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x04;
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x08;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x08;
	flag_h = (cy != 0);
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x10;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x10;
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x20;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x20;
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x40;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x40;
	c6 = (cy != 0);
	cy <<= 1;
	df |= (mi ^ sb ^ cy) & 0x80;
	cy = ((sb & cy) | (~mi & (sb | cy))) & 0x80;
	flag_c = (cy != 0);
	flag_n = 1;
	flag_v = flag_c ^ c6;
	flag_x = ((df & 0x08) != 0);
	flag_y = ((df & 0x20) != 0);
	flag_z = (df == 0);
	flag_s = ((df & 0x80) != 0);
	return (unsigned char) df;
}


/*
 * returns s1 + s2 + carry, sets flags
 */
static unsigned
add16(unsigned s1, unsigned s2, int carry) {
	int c14 = 0, i;
	unsigned su = 0, cy = carry ? 1 : 0, ma = 1;
	for (i = 0; i < 16; i++) {
		su |= (s1 ^ s2 ^ cy) & ma;
		cy = ((s2 & cy) | (s1 & (s2 | cy))) & ma;
		if (i == 11) flag_h = (cy != 0);
		if (i == 14) c14 = (cy != 0);
		if (i == 15) flag_c = (cy != 0);
		cy <<= 1;
		ma <<= 1;
	}
	flag_n = 0;
	flag_v = flag_c ^ c14;
	flag_x = ((su & 0x0800) != 0);
	flag_y = ((su & 0x2000) != 0);
	flag_z = (su == 0);
	flag_s = ((su & 0x8000) != 0);
	return su;
}


/*
 * returns s1 - s2 - carry, sets flags
 */
static unsigned
sub16(unsigned mi, unsigned sb, int carry) {
	int c14 = 0, i;
	unsigned df = 0, cy = carry ? 1 : 0, ma = 1;
	for (i = 0; i < 16; i++) {
		df |= (mi ^ sb ^ cy) & ma;
		cy = ((sb & cy) | (~mi & (sb | cy))) & ma;
		if (i == 11) flag_h = (cy != 0);
		if (i == 14) c14 = (cy != 0);
		if (i == 15) flag_c = (cy != 0);
		cy <<= 1;
		ma <<= 1;
	}
	flag_n = 1;
	flag_v = flag_c ^ c14;
	flag_x = ((df & 0x0800) != 0);
	flag_y = ((df & 0x2000) != 0);
	flag_z = (df == 0);
	flag_s = ((df & 0x8000) != 0);
	return df;
}


/*
 * 8-bit increment
 */
static void
inst_inr(void) {
	unsigned char *dp;
	/*
	 * doesn't affect carry flag
	 */
	int old_c = flag_c;
	dp = operand8((opcode >> 3) & 0x07, 0);
	*dp = add8(*dp, 1, 0);
	flag_c = old_c;
}


/*
 * 16-bit increment
 */
static void
inst_inx(void) {
	switch (opcode & 0x30) {
	case 0x00: set_bc((get_bc() + 1) & 0xffff); break;
	case 0x10: set_de((get_de() + 1) & 0xffff); break;
	case 0x20:
		switch (prefix) {
		case 0x00: set_hl((get_hl() + 1) & 0xffff); break;
		case 0xdd: set_ix((get_ix() + 1) & 0xffff); break;
		case 0xfd: set_iy((get_iy() + 1) & 0xffff); break;
		}
		break;
	case 0x30: reg_sp = ((reg_sp + 1) & 0xffff); break;
	}
}


/*
 * 8-bit decrement
 */
static void
inst_dcr(void) {
	unsigned char *dp;
	/*
	 * doesn't affect carry flag
	 */
	int old_c = flag_c;
	dp = operand8((opcode >> 3) & 0x07, 0);
	*dp = sub8(*dp, 1, 0);
	flag_c = old_c;
}


/*
 * 16-bit decrement
 */
static void
inst_dcx(void) {
	switch (opcode & 0x30) {
	case 0x00: set_bc((get_bc() + 0xffff) & 0xffff); break;
	case 0x10: set_de((get_de() + 0xffff) & 0xffff); break;
	case 0x20:
		switch (prefix) {
		case 0x00: set_hl((get_hl() + 0xffff) & 0xffff); break;
		case 0xdd: set_ix((get_ix() + 0xffff) & 0xffff); break;
		case 0xfd: set_iy((get_iy() + 0xffff) & 0xffff); break;
		}
		break;
	case 0x30: reg_sp = ((reg_sp + 0xffff) & 0xffff); break;
	}
}


/*
 * 16-bit addition without carry
 */
static void
inst_dad(void) {
	/*
	 * flags S, Z, and P are not modified
	 */
	int old_s = flag_s, old_z = flag_z, old_p = flag_p;
	unsigned s = 0;
	switch (opcode & 0x30) {
	case 0x00: s = get_bc(); break;
	case 0x10: s = get_de(); break;
	case 0x20:
		switch (prefix) {
		case 0x00: s = get_hl(); break;
		case 0xdd: s = get_ix(); break;
		case 0xfd: s = get_iy(); break;
		}
		break;
	case 0x30: s = reg_sp; break;
	}
	switch (prefix) {
	case 0x00:
		internal = get_hl();
		set_hl(add16(internal, s, 0));
		break;
	case 0xdd:
		internal = get_ix();
		set_ix(add16(internal, s, 0));
		break;
	case 0xfd:
		internal = get_iy();
		set_iy(add16(internal, s, 0));
		break;
	}
	flag_s = old_s;
	flag_z = old_z;
	flag_p = old_p;
}


/*
 * calculate parity of a byte (even number of bits: 1, odd number of bits: 0)
 */
static int
parity(unsigned char byte) {
	int p = 1;
	while (byte) {
		p ^= (byte & 0x01);
		byte >>= 1;
	}
	return p;
}


/*
 * adjust A for BCD arithmetic after addition or subtraction
 */
static void
inst_daa(void) {
	int high = (reg_a >> 4) & 0x0f, low = reg_a & 0x0f, new_c, new_h;
	unsigned char diff;
	/*
	 * calculate adjustment byte for A
	 */
	if (flag_c) {
		if (low < 0xa) {
			diff = flag_h ?  0x66 : 0x60;
		} else {
			diff = 0x66;
		}
	} else {
		if (low < 0xa) {
			if (high < 0xa) {
				diff = flag_h ? 0x06 : 0x00;
			} else {
				diff = flag_h ? 0x66 : 0x60;
			}
		} else {
			diff = (high < 0x9) ? 0x06 : 0x66;
		}
	}
	/*
	 * calculate new C flag
	 */
	if (flag_c) {
		new_c = 1;
	} else {
		if (low < 0xa) {
			new_c = (high < 0xa) ? 0 : 1;
		} else {
			new_c = (high < 0x9) ? 0 : 1;
		}
	}
	/*
	 * calculate new H flag
	 */
	if (flag_n) {
		if (flag_h) {
			new_h = (low < 0x6) ? 1 : 0;
		} else {
			new_h = 0;
		}
	} else {
		new_h = (low < 0xa) ? 0 : 1;
	}
	/*
	 * adjust A, set flags S, Z, Y, X, and N
	 */
	reg_a = flag_n ? sub8(reg_a, diff, 0) : add8(reg_a, diff, 0);
	/*
	 * set P/V flag from parity of A
	 */
	flag_p = parity(reg_a);
	/*
	 * set flags C and H calculated above
	 */
	flag_c = new_c;
	flag_h = new_h;
}


/*
 * 8-bit add to A
 */
static void
inst_add(void) { reg_a = add8(reg_a, *operand8(opcode & 0x07, 0), 0); }


/*
 * 8-bit add to A immediate
 */
static void
inst_adi(void) { reg_a = add8(reg_a, op_low, 0); }


/*
 * 8-bit add with carry to A
 */
static void
inst_adc(void) { reg_a = add8(reg_a, *operand8(opcode & 0x07, 0), flag_c); }


/*
 * 8-bit add with carry to A immediate
 */
static void
inst_aci(void) { reg_a = add8(reg_a, op_low, flag_c); }


/*
 * 8-bit subtract from A
 */
static void
inst_sub(void) { reg_a = sub8(reg_a, *operand8(opcode & 0x07, 0), 0); }


/*
 * 8-bit subtract from A immediate
 */
static void
inst_sui(void) { reg_a = sub8(reg_a, op_low, 0); }


/*
 * 8-bit subtract with borrow from A
 */
static void
inst_sbca(void) { reg_a = sub8(reg_a, *operand8(opcode & 0x07, 0), flag_c); }


/*
 * 8-bit subtract with borrow from A immediate
 */
static void
inst_sbi(void) { reg_a = sub8(reg_a, op_low, flag_c); }


/*
 * 8-bit compare to A
 */
static void
inst_cmp(void) {
	unsigned char op = *operand8(opcode & 0x07, 0);
	sub8(reg_a, op, 0);
	flag_x = ((op & 0x08) == 0x08);
	flag_y = ((op & 0x20) == 0x20);
}


/*
 * 8-bit compare to A immediate
 */
static void
inst_cmpi(void) {
	sub8(reg_a, op_low, 0);
	flag_x = ((op_low & 0x08) == 0x08);
	flag_y = ((op_low & 0x20) == 0x20);
}


/*
 * set flags after inst_and/ani/or/ori/xor/xri()
 */
static void
log_flags(void) {
	flag_s = ((reg_a & 0x80) == 0x80);
	flag_z = reg_a ? 0 : 1;
	flag_y = ((reg_a & 0x20) == 0x20);
	flag_x = ((reg_a & 0x08) == 0x08);
	flag_p = parity(reg_a);
	flag_n = 0;
	flag_c = 0;
}


/*
 * logical and with A
 */
static void
inst_and(void) {
	reg_a &= *operand8(opcode & 0x07, 0);
	flag_h = 1;
	log_flags();
}


/*
 * logical and with A immediate
 */
static void
inst_ani(void) {
	reg_a &= op_low;
	flag_h = 1;
	log_flags();
}


/*
 * logical or with A
 */
static void
inst_or(void) {
	reg_a |= *operand8(opcode & 0x07, 0);
	flag_h = 0;
	log_flags();
}


/*
 * logical or with A immediate
 */
static void
inst_ori(void) {
	reg_a |= op_low;
	flag_h = 0;
	log_flags();
}


/*
 * logical xor with A
 */
static void
inst_xor(void) {
	reg_a ^= *operand8(opcode & 0x07, 0);
	flag_h = 0;
	log_flags();
}


/*
 * logical xor with A immediate
 */
static void
inst_xri(void) {
	reg_a ^= op_low;
	flag_h = 0;
	log_flags();
}


/*
 * unconditional jump
 */
static void
inst_jp(void) {
	reg_pc = op_high;
	reg_pc <<= 8;
	reg_pc |= op_low;
}


/*
 * common part of ret, ret cc, and retn
 */
static void
do_ret(void) {
	reg_pc = pop();
}


/*
 * unconditional return
 */
static void
inst_ret(void) {
	/*
	 * handle magic addresses for BDOS and BIOS calls
	 */
	if (current_instruction >= MAGIC_ADDRESS) {
		os_call(current_instruction - MAGIC_ADDRESS);
	}
	do_ret();
}


/*
 * unconditional call
 */
static void
inst_call(void) {
	push(reg_pc);
	inst_jp();
}


/*
 * restart
 */
static void
inst_rst(void) {
	push(reg_pc);
	reg_pc = (opcode & 0x38);
}


/*
 * return true if condition from jp cc, call cc, or ret cc is met
 */
static int
condition_met(void) {
	switch (opcode & 0x38) {
	case 0x00: return (! flag_z);
	case 0x08: return flag_z;
	case 0x10: return (! flag_c);
	case 0x18: return flag_c;
	case 0x20: return (! flag_p);
	case 0x28: return flag_p;
	case 0x30: return (! flag_s);
	default /* case 0x38 */: return flag_s;
	}
}


/*
 * conditional jump
 */
static void
inst_jpcc(void) { if (condition_met()) inst_jp(); }


/*
 * conditional return
 */
static void
inst_retcc(void) { if (condition_met()) do_ret(); }


/*
 * conditional call
 */
static void
inst_callcc(void) { if (condition_met()) inst_call(); }


/*
 * push word register to stack
 */
static void
inst_push(void) {
	int word;
	switch (opcode & 0x30) {
		/*
		 * BC
		 */
	case 0x00: word = get_bc(); break;
		/*
		 * DE
		 */
	case 0x10: word = get_de(); break;
	case 0x20:
		switch (prefix) {
		/*
		 * IX
		 */
		case 0xdd: word = get_ix(); break;
		/*
		 * IY
		 */
		case 0xfd: word = get_iy(); break;
		/*
		 * HL
		 */
		default: word = get_hl(); break;
		}
		break;
	default:
		/*
		 * AF
		 */
		word = reg_a;
		word <<= 8;
		if (flag_s) word |= 0x80;
		if (flag_z) word |= 0x40;
		if (flag_y) word |= 0x20;
		if (flag_h) word |= 0x10;
		if (flag_x) word |= 0x08;
		if (flag_p) word |= 0x04;
		if (flag_n) word |= 0x02;
		if (flag_c) word |= 0x01;
		break;
	}
	push(word);
}


/*
 * pop word register from stack
 */
static void
inst_pop(void) {
	int word = pop();
	switch (opcode & 0x30) {
		/*
		 * BC
		 */
	case 0x00: set_bc(word); break;
		/*
		 * DE
		 */
	case 0x10: set_de(word); break;
	case 0x20:
		switch (prefix) {
		/*
		 * IX
		 */
		case 0xdd: set_ix(word); break;
		/*
		 * IY
		 */
		case 0xfd: set_iy(word); break;
		/*
		 * HL
		 */
		default: set_hl(word); break;
		}
		break;
	default:
		/*
		 * AF
		 */
		reg_a = ((word >> 8) & 0xff);
		flag_s = ((word & 0x80) == 0x80);
		flag_z = ((word & 0x40) == 0x40);
		flag_y = ((word & 0x20) == 0x20);
		flag_h = ((word & 0x10) == 0x10);
		flag_x = ((word & 0x08) == 0x08);
		flag_p = ((word & 0x04) == 0x04);
		flag_n = ((word & 0x02) == 0x02);
		flag_c = ((word & 0x01) == 0x01);
		break;
	}

}


/*
 * exchange word registers
 */
static void
inst_exx(void) {
	unsigned char uc;
	uc = reg_b; reg_b = alt_reg_b; alt_reg_b = uc;
	uc = reg_c; reg_c = alt_reg_c; alt_reg_c = uc;
	uc = reg_d; reg_d = alt_reg_d; alt_reg_d = uc;
	uc = reg_e; reg_e = alt_reg_e; alt_reg_e = uc;
	uc = reg_h; reg_h = alt_reg_h; alt_reg_h = uc;
	uc = reg_l; reg_l = alt_reg_l; alt_reg_l = uc;
}


/*
 * exchange DE and HL
 */
static void
inst_xchg(void) {
	unsigned char uc;
	uc = reg_h; reg_h = reg_d; reg_d = uc;
	uc = reg_l; reg_l = reg_e; reg_e = uc;
}


/*
 * exchange HL, IX, or IY with top of stack
 */
static void
inst_xthl(void) {
	unsigned char *rlp, *rhp, *slp, *shp, uc;
	switch (prefix) {
	case 0xdd: rhp = &reg_ixh; rlp = &reg_ixl; break;
	case 0xfd: rhp = &reg_iyh; rlp = &reg_iyl; break;
	default: rhp = &reg_h; rlp = &reg_l; break;
	}
	slp = memory + reg_sp;
	shp = memory + ((reg_sp + 1) & 0xffff);
	uc = *slp; *slp = *rlp; *rlp = uc;
	uc = *shp; *shp = *rhp; *rhp = uc;
}


/*
 * jump to address in HL, IX, or IY
 */
static void
inst_pchl(void) {
	switch (prefix) {
	case 0xdd: reg_pc = get_ix(); break;
	case 0xfd: reg_pc = get_iy(); break;
	default: reg_pc = get_hl(); break;
	}
}


/*
 * set SP to address in HL, IX, or IY
 */
static void
inst_sphl(void) {
	switch (prefix) {
	case 0xdd: reg_sp = get_ix(); break;
	case 0xfd: reg_sp = get_iy(); break;
	default: reg_sp = get_hl(); break;
	}
}


/*
 * get byte from port to A
 */
static void
inst_ina(void) {
	reg_a = 0x00;
}


/*
 * put byte in A to port
 */
static void
inst_outa(void) { }


/*
 * enable interrupts
 */
static void
inst_ei(void) {
	flag_i = 1;
}


/*
 * disable interrupts
 */
static void
inst_di(void) {
	flag_i = 0;
}


/*
 * same as operand8(), but for I/O instructions: no modification by
 * prefixes, and code 6 doesn't reference (HL)
 */
static unsigned char *
io_operand(int n) {
	switch (n) {
	case 0: return &reg_b;
	case 1: return &reg_c;
	case 2: return &reg_d;
	case 3: return &reg_e;
	case 4: return &reg_h;
	case 5: return &reg_l;
	case 6: return NULL;
	default: return &reg_a;
	}
}


/*
 * IN r,(C) (dummy, always reads 0)
 */
static void
inst_inc(void) {
	unsigned char *dp = io_operand((opcode2 >> 3) & 0x07);
	if (dp) *dp = 0;
	flag_s = 0; /* result 0 */
	flag_z = 1; /* result 0 */
	flag_y = 0; /* result 0 */
	flag_h = 0;
	flag_x = 0; /* result 0 */
	flag_p = 0; /* result 0 */
	flag_n = 0;
}


/*
 * OUT (C), r (dummy, doesn't do anything)
 */
static void
inst_outc(void) { }


/*
 * decrease PC by two (used for LDIR/LDDR/CPIR/CPDR/INIR/INDR/OTIR/OTDR)
 */
static void
repeat_block(void) {
	reg_pc = ((reg_pc + 0xfffe) & 0xffff);
}


/*
 * OUTI (dummy apart from its side effects, which are strange)
 */
static void
inst_outi(void) {
	int hl = get_hl(), k = memory[hl], new_n, new_c, new_h, new_p;
	set_hl((hl + 1) & 0xffff);
	new_n = ((k & 0x80) == 0x80);
	k += reg_l;
	new_c = new_h = (k > 255);
	new_p = parity((k & 7) ^ reg_b);
	reg_b = sub8(reg_b, 1, 0);
	flag_c = new_c;
	flag_n = new_n;
	flag_p = new_p;
	flag_h = new_h;
}


/*
 * OTIR (like OUTI, but decrease PC by two if B != 0
 */
static void
inst_otir(void) {
	inst_outi();
	if (reg_b) repeat_block();
}


/*
 * OUTD (dummy apart from its side effects, which are strange)
 */
static void
inst_outd(void) {
	int hl = get_hl(), k = memory[hl], new_n, new_c, new_h, new_p;
	set_hl((hl + 0xffff) & 0xffff);
	new_n = ((k & 0x80) == 0x80);
	k += reg_l;
	new_c = new_h = (k > 255);
	new_p = parity((k & 7) ^ reg_b);
	reg_b = sub8(reg_b, 1, 0);
	flag_c = new_c;
	flag_n = new_n;
	flag_p = new_p;
	flag_h = new_h;
}


/*
 * OTDR (like OUTD, but decrease PC by two if B != 0
 */
static void
inst_otdr(void) {
	inst_outd();
	if (reg_b) repeat_block();
}


/*
 * INI (dummy apart from its side effects, which are strange)
 */
static void
inst_ini(void) {
	int hl = get_hl(), k = 0, new_n, new_c, new_h, new_p;
	memory[hl] = k;
	set_hl((hl + 1) & 0xffff);
	new_n = ((k & 0x80) == 0x80);
	k += (reg_c + 1) & 0xff;
	new_c = new_h = (k > 255);
	new_p = parity((k & 7) ^ reg_b);
	reg_b = sub8(reg_b, 1, 0);
	flag_c = new_c;
	flag_n = new_n;
	flag_p = new_p;
	flag_h = new_h;
}


/*
 * INIR (like INI, but decrease PC by two if B != 0
 */
static void
inst_inir(void) {
	inst_ini();
	if (reg_b) repeat_block();
}


/*
 * IND (dummy apart from its side effects, which are strange)
 */
static void
inst_ind(void) {
	int hl = get_hl(), k = 0, new_n, new_c, new_h, new_p;
	memory[hl] = k;
	set_hl((hl + 0xffff) & 0xffff);
	new_n = ((k & 0x80) == 0x80);
	k += reg_c ? 0xff : reg_c - 1;
	new_c = new_h = (k > 255);
	new_p = parity((k & 7) ^ reg_b);
	reg_b = sub8(reg_b, 1, 0);
	flag_c = new_c;
	flag_n = new_n;
	flag_p = new_p;
	flag_h = new_h;
}


/*
 * INDR (like IND, but decrease PC by two if B != 0
 */
static void
inst_indr(void) {
	inst_ind();
	if (reg_b) repeat_block();
}


/*
 * NEG
 */
static void
inst_neg(void) {
	reg_a = sub8(0, reg_a, 0);
}


/*
 * RETN and RETI do the same (which is in this implementation the same
 * as a simple RET, since there is no NMI and no IFF2
 */
static void
inst_retn(void) {
	do_ret();
}


/*
 * IM 0/1/2 are dummies, since there are no interrupts in this
 * implementation
 */
static void
inst_im0(void) { }


static void
inst_im1(void) { }


static void
inst_im2(void) { }


/*
 * set flags for LD A,I and LD A,R
 */
static void
ldair_flags(void) {
	flag_s = ((reg_a & 0x80) == 0x80);
	flag_z = (reg_a == 0);
	flag_y = ((reg_a & 0x20) == 0x20);
	flag_h = 0;
	flag_x = ((reg_a & 0x08) == 0x08);
	flag_p = flag_i;
	flag_n = 0;
}


/*
 * LD A,I
 */
static void
inst_ldai(void) {
	reg_a = reg_i;
	ldair_flags();
}


/*
 * LD I,A
 */
static void
inst_ldia(void) {
	reg_i = reg_a;
}


/*
 * LD A,R
 */
static void
inst_ldar(void) {
	reg_a = reg_r;
	ldair_flags();
}


/*
 * LD R,A
 */
static void
inst_ldra(void) {
	reg_r = reg_a;
}


/*
 * ADC HL,rr
 */
static void
inst_adchl(void) {
	unsigned value;
	internal = get_hl();
	switch (opcode2 & 0x30) {
	case 0x00: value = get_bc(); break;
	case 0x10: value = get_de(); break;
	case 0x20: value = internal; break;
	default: value = reg_sp; break;
	}
	set_hl(add16(internal, value, flag_c));
}


/*
 * SBC HL,rr
 */
static void
inst_sbchl(void) {
	unsigned value;
	internal = get_hl();
	switch (opcode2 & 0x30) {
	case 0x00: value = get_bc(); break;
	case 0x10: value = get_de(); break;
	case 0x20: value = internal; break;
	default: value = reg_sp; break;
	}
	set_hl(sub16(internal, value, flag_c));
}


/*
 * common part of LDI and LDD
 */
static void
ldx(int up) {
	int hl, de, bc, t;
	bc = get_bc();
	de = get_de();
	hl = get_hl();
	t = memory[de] = memory[hl];
	t += reg_a;
	if (up) {
		hl = ((hl + 1) & 0xffff);
		de = ((de + 1) & 0xffff);
	} else {
		hl = ((hl + 0xffff) & 0xffff);
		de = ((de + 0xffff) & 0xffff);
	}
	bc = ((bc + 0xffff) & 0xffff);
	set_bc(bc);
	set_de(de);
	set_hl(hl);
	flag_y = (t & 0x02) == 0x02;
	flag_h = 0;
	flag_x = (t & 0x08) == 0x08;
	flag_p = (bc != 0);
	flag_n = 0;
}


/*
 * LDI
 */
static void
inst_ldi(void) { ldx(1); }


/*
 * LDIR (repeats LDI until P is clear, i. e. BC == 0)
 */
static void
inst_ldir(void) {
	inst_ldi();
	if (flag_p) repeat_block();
}


/*
 * LDD
 */
static void
inst_ldd(void) { ldx(0); }


/*
 * LDDR (repeats LDD until P is clear, i. e. BC == 0)
 */
static void
inst_lddr(void) {
	inst_ldd();
	if (flag_p) repeat_block();
}


/*
 * common part of CPI and CPD
 */
static void
cpx(int up) {
	int hl, bc, t, old_c;
	old_c = flag_c;
	bc = get_bc();
	hl = get_hl();
	t = sub8(reg_a, memory[hl], 0);
	t += flag_h;
	hl = up ? ((hl + 1) & 0xffff) : ((hl + 0xffff) & 0xffff);
	bc = ((bc + 0xffff) & 0xffff);
	set_bc(bc);
	set_hl(hl);
	flag_y = ((t & 0x02) == 0x02);
	flag_x = ((t & 0x08) == 0x08);
	flag_p = (bc != 0);
	flag_c = old_c;
}


/*
 * CPI
 */
static void
inst_cpi(void) { cpx(1); }


/*
 * CPIR (repeats CPI until P is clear, i. e. BC == 0)
 */
static void
inst_cpir(void) {
	inst_cpi();
	if (flag_p && ! flag_z) repeat_block();
}


/*
 * CPD
 */
static void
inst_cpd(void) { cpx(0); }


/*
 * CPDR (repeats CPD until P is clear, i. e. BC == 0)
 */
static void
inst_cpdr(void) {
	inst_cpd();
	if (flag_p && ! flag_z) repeat_block();
}


/*
 * LD rr,(nn)
 */
static void
inst_lrrd(void) {
	int t;
	t = op_high;
	t <<= 8;
	t |= op_low;
	t = get_word(t);
	switch (opcode2 & 0x30) {
	case 0x00: set_bc(t); break;
	case 0x10: set_de(t); break;
	case 0x20: set_hl(t); break;
	default: reg_sp = t; break;
	}
}


/*
 * LD (nn),rr
 */
static void
inst_srrd(void) {
	int t;
	t = op_high;
	t <<= 8;
	t |= op_low;
	switch (opcode2 & 0x30) {
	case 0x00: set_word(t, get_bc()); break;
	case 0x10: set_word(t, get_de()); break;
	case 0x20: set_word(t, get_hl()); break;
	default: set_word(t, reg_sp); break;
	}
}


/*
 * helper function to set the flags after Z80 (as opposed to
 * 8080) specific shift and rotate operations
 */
static void
shift_flags(unsigned char data) {
	flag_s = ((data & 0x80) == 0x80);
	flag_z = (data == 0x00);
	flag_y = ((data & 0x20) == 0x20);
	flag_h = 0;
	flag_x = ((data & 0x08) == 0x08);
	flag_p = parity(data);
	flag_n = 0;
}


/*
 * RLD
 */
static void
inst_rld(void) {
	int t, hl = get_hl();
	t = memory[hl];
	memory[hl] = ((t << 4) & 0xf0) | (reg_a & 0x0f);
	reg_a = (reg_a & 0xf0) | ((t >> 4) & 0xf);
	shift_flags(reg_a);
}


/*
 * RRD
 */
static void
inst_rrd(void) {
	int t, hl = get_hl();
	t = memory[hl];
	memory[hl] = ((t >> 4) & 0x0f) | ((reg_a << 4) & 0x0f);
	reg_a = (reg_a & 0xf0) | (t & 0xf);
	shift_flags(reg_a);
}


/*
 * RLC/RRC/RL/RR/SLA/SRA/SLL/SRL/BIT/RES/SET
 *
 * two-byte opcodes starting in 0xcb; there are only 11 separate
 * instructions with a very similar structure, so this doesn't merit
 * a separate dispatch table.
 */
static void
inst_cb(void) {
	int r = (opcode2 & 0x07), temp;
	unsigned char *op1, *op2, byte;
	if (prefix) {
		/*
		 * if there is a prefix, the source and one destination
		 * operand is always an indexed memory location
		 */
		op1 = operand8(6, 0);
		/*
		 * only a register can be the other destination operand
		 * and it is never i[xy][lh].
		 */
		op2 = (r == 6) ? NULL : operand8(r, 6);
	} else {
		/*
		 * without a prefix there is only one operand (both
		 * source and destination)
		 */
		op1 = operand8(r, 0);
		op2 = NULL;
	}
	byte = *op1;
	switch (opcode2 & 0xc0) {
	case 0x00:
		/*
		 * shift and rotate instructions
		 */
		switch (opcode2 & 0x38) {
		case 0x00:
			/*
			 * RLC: 8-bit rotate left
			 */
			flag_c = ((byte & 0x80) == 0x80);
			byte = (((byte << 1) | flag_c) & 0xff);
			break;
		case 0x08:
			/*
			 * RRC: 8-bit rotate right
			 */
			flag_c = ((byte & 0x01) == 0x01);
			byte = (((byte >> 1) | (flag_c ? 0x80 : 0x00)) & 0xff);
			break;
		case 0x10:
			/*
			 * RL: 9-bit rotate left
			 */
			temp = ((byte & 0x80) == 0x80);
			byte = (((byte << 1) | flag_c) & 0xff);
			flag_c = temp;
			break;
		case 0x18:
			/*
			 * RR: 9-bit rotate right
			 */
			temp = ((byte & 0x01) == 0x01);
			byte = (((byte >> 1) | (flag_c ? 0x80 : 0x00)) & 0xff);
			flag_c = temp;
			break;
		case 0x20:
			/*
			 * SLA: arithmetical (and logical) left shift
			 */
			flag_c = ((byte & 0x80) == 0x80);
			byte = ((byte << 1) & 0xfe);
			break;
		case 0x28:
			/*
			 * SRA: arithmetical right shift
			 */
			temp = (byte & 0x80);
			flag_c = ((byte & 0x01) == 0x01);
			byte = (((byte >> 1) | temp)) & 0xff;
			break;
		case 0x30:
			/*
			 * SLL: like SLA, but shifts in 1
			 */
			flag_c = ((byte & 0x80) == 0x80);
			byte = (((byte << 1) | 0x01) & 0xff);
			break;
		case 0x38:
			/*
			 * SRL: logical right shift
			 */
			flag_c = ((byte & 0x01) == 0x01);
			byte = ((byte >> 1) & 0x7f);
			break;
		}
		shift_flags(byte);
		break;
	case 0x40:
		/*
		 * BIT
		 */
		byte &= (1 << ((opcode2 >> 3) & 0x07));
		flag_n = 0;
		flag_p = flag_z = (byte == 0);
		flag_h = 1;
		flag_s = ((byte & 0x80) == 0x80);
		/*
		 * flags X and Y have complicated rules in this instructon
		 */
		if (r == 6) {
			flag_x = ((internal & 0x0800) == 0x0800);
			flag_y = ((internal & 0x2000) == 0x2000);
		} else {
			flag_x = ((byte & 0x08) == 0x08);
			flag_y = ((byte & 0x20) == 0x20);
		}
		/*
		 * no operand is modified by BIT
		 */
		goto no_save;
	case 0x80:
		/*
		 * RES
		 */
		byte &= ~(1 << ((opcode2 >> 3) & 0x07));
		break;
	case 0xc0:
		/*
		 * SET
		 */
		byte |= 1 << ((opcode2 >> 3) & 0x07);
		break;
	}
	*op1 = byte;
	if (op2) *op2 = byte;
no_save:
	return;
}


/*
 * operand/displacement fetch
 */
static inline int
fetch(void) {
	int byte;
	byte = memory[reg_pc];
	reg_pc = ((reg_pc + 1) & 0xffff);
	return byte;
}


/*
 * opcode/prefix fetch: increase R
 */
static inline int
fetch_m1(void) {
	int t = reg_r;
	int opcode = fetch();
	/*
	 * increase the lower 7 bits of R by 1, leave bit 7 unchanged
	 */
	reg_r = (t & 0x80) | ((t + 1) & 0x7f);
	return opcode;
}


/*
 * flags and structures of the instruction dispatch tables
 */
enum flags {
	OP_0 = 0,
	OP_INDEXED = 1, /* indexed addressing */
	OP_ARG8 = 2, /* has an 8-bit displacement/operand */
	OP_ARG16 = 4 /* has a 16-bit address/operand */
};

struct instruction {
	void (*handler_p)(void);
	enum flags flags;
};


/*
 * dispatcher table for instructions staring in 0xed
 */
static const struct instruction ed_plane[256] = {
/*00*/	{ inst_nop,	OP_0 },
/*01*/	{ inst_nop,	OP_0 },
/*02*/	{ inst_nop,	OP_0 },
/*03*/	{ inst_nop,	OP_0 },
/*04*/	{ inst_nop,	OP_0 },
/*05*/	{ inst_nop,	OP_0 },
/*06*/	{ inst_nop,	OP_0 },
/*07*/	{ inst_nop,	OP_0 },
/*08*/	{ inst_nop,	OP_0 },
/*09*/	{ inst_nop,	OP_0 },
/*0a*/	{ inst_nop,	OP_0 },
/*0b*/	{ inst_nop,	OP_0 },
/*0c*/	{ inst_nop,	OP_0 },
/*0d*/	{ inst_nop,	OP_0 },
/*0e*/	{ inst_nop,	OP_0 },
/*0f*/	{ inst_nop,	OP_0 },
/*10*/	{ inst_nop,	OP_0 },
/*11*/	{ inst_nop,	OP_0 },
/*12*/	{ inst_nop,	OP_0 },
/*13*/	{ inst_nop,	OP_0 },
/*14*/	{ inst_nop,	OP_0 },
/*15*/	{ inst_nop,	OP_0 },
/*16*/	{ inst_nop,	OP_0 },
/*17*/	{ inst_nop,	OP_0 },
/*18*/	{ inst_nop,	OP_0 },
/*19*/	{ inst_nop,	OP_0 },
/*1a*/	{ inst_nop,	OP_0 },
/*1b*/	{ inst_nop,	OP_0 },
/*1c*/	{ inst_nop,	OP_0 },
/*1d*/	{ inst_nop,	OP_0 },
/*1e*/	{ inst_nop,	OP_0 },
/*1f*/	{ inst_nop,	OP_0 },
/*20*/	{ inst_nop,	OP_0 },
/*21*/	{ inst_nop,	OP_0 },
/*22*/	{ inst_nop,	OP_0 },
/*23*/	{ inst_nop,	OP_0 },
/*24*/	{ inst_nop,	OP_0 },
/*25*/	{ inst_nop,	OP_0 },
/*26*/	{ inst_nop,	OP_0 },
/*27*/	{ inst_nop,	OP_0 },
/*28*/	{ inst_nop,	OP_0 },
/*29*/	{ inst_nop,	OP_0 },
/*2a*/	{ inst_nop,	OP_0 },
/*2b*/	{ inst_nop,	OP_0 },
/*2c*/	{ inst_nop,	OP_0 },
/*2d*/	{ inst_nop,	OP_0 },
/*2e*/	{ inst_nop,	OP_0 },
/*2f*/	{ inst_nop,	OP_0 },
/*30*/	{ inst_nop,	OP_0 },
/*31*/	{ inst_nop,	OP_0 },
/*32*/	{ inst_nop,	OP_0 },
/*33*/	{ inst_nop,	OP_0 },
/*34*/	{ inst_nop,	OP_0 },
/*35*/	{ inst_nop,	OP_0 },
/*36*/	{ inst_nop,	OP_0 },
/*37*/	{ inst_nop,	OP_0 },
/*38*/	{ inst_nop,	OP_0 },
/*39*/	{ inst_nop,	OP_0 },
/*3a*/	{ inst_nop,	OP_0 },
/*3b*/	{ inst_nop,	OP_0 },
/*3c*/	{ inst_nop,	OP_0 },
/*3d*/	{ inst_nop,	OP_0 },
/*3e*/	{ inst_nop,	OP_0 },
/*3f*/	{ inst_nop,	OP_0 },
/*40*/	{ inst_inc,	OP_0 },
/*41*/	{ inst_outc,	OP_0 },
/*42*/	{ inst_sbchl,	OP_0 },
/*43*/	{ inst_srrd,	OP_ARG16 },
/*44*/	{ inst_neg,	OP_0 },
/*45*/	{ inst_retn,	OP_0 },
/*46*/	{ inst_im0,	OP_0 },
/*47*/	{ inst_ldia,	OP_0 },
/*48*/	{ inst_inc,	OP_0 },
/*49*/	{ inst_outc,	OP_0 },
/*4a*/	{ inst_adchl,	OP_0 },
/*4b*/	{ inst_lrrd,	OP_ARG16 },
/*4c*/	{ inst_neg,	OP_0 },
/*4d*/	{ inst_retn,	OP_0 },	/* reti */
/*4e*/	{ inst_im0,	OP_0 },
/*4f*/	{ inst_ldra,	OP_0 },
/*50*/	{ inst_inc,	OP_0 },
/*51*/	{ inst_outc,	OP_0 },
/*52*/	{ inst_sbchl,	OP_0 },
/*53*/	{ inst_srrd,	OP_ARG16 },
/*54*/	{ inst_neg,	OP_0 },
/*55*/	{ inst_retn,	OP_0 },
/*56*/	{ inst_im1,	OP_0 },
/*57*/	{ inst_ldai,	OP_0 },
/*58*/	{ inst_inc,	OP_0 },
/*59*/	{ inst_outc,	OP_0 },
/*5a*/	{ inst_adchl,	OP_0 },
/*5b*/	{ inst_lrrd,	OP_ARG16 },
/*5c*/	{ inst_neg,	OP_0 },
/*5d*/	{ inst_retn,	OP_0 },
/*5e*/	{ inst_im2,	OP_0 },
/*5f*/	{ inst_ldar,	OP_0 },
/*60*/	{ inst_inc,	OP_0 },
/*61*/	{ inst_outc,	OP_0 },
/*62*/	{ inst_sbchl,	OP_0 },
/*63*/	{ inst_srrd,	OP_ARG16 },
/*64*/	{ inst_neg,	OP_0 },
/*65*/	{ inst_retn,	OP_0 },
/*66*/	{ inst_im0,	OP_0 },
/*67*/	{ inst_rrd,	OP_0 },
/*68*/	{ inst_inc,	OP_0 },
/*69*/	{ inst_outc,	OP_0 },
/*6a*/	{ inst_adchl,	OP_0 },
/*6b*/	{ inst_lrrd,	OP_ARG16 },
/*6c*/	{ inst_neg,	OP_0 },
/*6d*/	{ inst_retn,	OP_0 },
/*6e*/	{ inst_im0,	OP_0 },
/*6f*/	{ inst_rld,	OP_0 },
/*70*/	{ inst_inc,	OP_0 },
/*71*/	{ inst_outc,	OP_0 },
/*72*/	{ inst_sbchl,	OP_0 },
/*73*/	{ inst_srrd,	OP_ARG16 },
/*74*/	{ inst_neg,	OP_0 },
/*75*/	{ inst_retn,	OP_0 },
/*76*/	{ inst_im1,	OP_0 },
/*77*/	{ inst_nop,	OP_0 },
/*78*/	{ inst_inc,	OP_0 },
/*79*/	{ inst_outc,	OP_0 },
/*7a*/	{ inst_adchl,	OP_0 },
/*7b*/	{ inst_lrrd,	OP_ARG16 },
/*7c*/	{ inst_neg,	OP_0 },
/*7d*/	{ inst_retn,	OP_0 },
/*7e*/	{ inst_im2,	OP_0 },
/*7f*/	{ inst_nop,	OP_0 },
/*80*/	{ inst_nop,	OP_0 },
/*81*/	{ inst_nop,	OP_0 },
/*82*/	{ inst_nop,	OP_0 },
/*83*/	{ inst_nop,	OP_0 },
/*84*/	{ inst_nop,	OP_0 },
/*85*/	{ inst_nop,	OP_0 },
/*86*/	{ inst_nop,	OP_0 },
/*87*/	{ inst_nop,	OP_0 },
/*88*/	{ inst_nop,	OP_0 },
/*89*/	{ inst_nop,	OP_0 },
/*8a*/	{ inst_nop,	OP_0 },
/*8b*/	{ inst_nop,	OP_0 },
/*8c*/	{ inst_nop,	OP_0 },
/*8d*/	{ inst_nop,	OP_0 },
/*8e*/	{ inst_nop,	OP_0 },
/*8f*/	{ inst_nop,	OP_0 },
/*90*/	{ inst_nop,	OP_0 },
/*91*/	{ inst_nop,	OP_0 },
/*92*/	{ inst_nop,	OP_0 },
/*93*/	{ inst_nop,	OP_0 },
/*94*/	{ inst_nop,	OP_0 },
/*95*/	{ inst_nop,	OP_0 },
/*96*/	{ inst_nop,	OP_0 },
/*97*/	{ inst_nop,	OP_0 },
/*98*/	{ inst_nop,	OP_0 },
/*99*/	{ inst_nop,	OP_0 },
/*9a*/	{ inst_nop,	OP_0 },
/*9b*/	{ inst_nop,	OP_0 },
/*9c*/	{ inst_nop,	OP_0 },
/*9d*/	{ inst_nop,	OP_0 },
/*9e*/	{ inst_nop,	OP_0 },
/*9f*/	{ inst_nop,	OP_0 },
/*a0*/	{ inst_ldi,	OP_0 },
/*a1*/	{ inst_cpi,	OP_0 },
/*a2*/	{ inst_ini,	OP_0 },
/*a3*/	{ inst_outi,	OP_0 },
/*a4*/	{ inst_nop,	OP_0 },
/*a5*/	{ inst_nop,	OP_0 },
/*a6*/	{ inst_nop,	OP_0 },
/*a7*/	{ inst_nop,	OP_0 },
/*a8*/	{ inst_ldd,	OP_0 },
/*a9*/	{ inst_cpd,	OP_0 },
/*aa*/	{ inst_ind,	OP_0 },
/*ab*/	{ inst_outd,	OP_0 },
/*ac*/	{ inst_nop,	OP_0 },
/*ad*/	{ inst_nop,	OP_0 },
/*ae*/	{ inst_nop,	OP_0 },
/*af*/	{ inst_nop,	OP_0 },
/*b0*/	{ inst_ldir,	OP_0 },
/*b1*/	{ inst_cpir,	OP_0 },
/*b2*/	{ inst_inir,	OP_0 },
/*b3*/	{ inst_otir,	OP_0 },
/*b4*/	{ inst_nop,	OP_0 },
/*b5*/	{ inst_nop,	OP_0 },
/*b6*/	{ inst_nop,	OP_0 },
/*b7*/	{ inst_nop,	OP_0 },
/*b8*/	{ inst_lddr,	OP_0 },
/*b9*/	{ inst_cpdr,	OP_0 },
/*ba*/	{ inst_indr,	OP_0 },
/*bb*/	{ inst_otdr,	OP_0 },
/*bc*/	{ inst_nop,	OP_0 },
/*bd*/	{ inst_nop,	OP_0 },
/*be*/	{ inst_nop,	OP_0 },
/*bf*/	{ inst_nop,	OP_0 },
/*c0*/	{ inst_nop,	OP_0 },
/*c1*/	{ inst_nop,	OP_0 },
/*c2*/	{ inst_nop,	OP_0 },
/*c3*/	{ inst_nop,	OP_0 },
/*c4*/	{ inst_nop,	OP_0 },
/*c5*/	{ inst_nop,	OP_0 },
/*c6*/	{ inst_nop,	OP_0 },
/*c7*/	{ inst_nop,	OP_0 },
/*c8*/	{ inst_nop,	OP_0 },
/*c9*/	{ inst_nop,	OP_0 },
/*ca*/	{ inst_nop,	OP_0 },
/*cb*/	{ inst_nop,	OP_0 },
/*cc*/	{ inst_nop,	OP_0 },
/*cd*/	{ inst_nop,	OP_0 },
/*ce*/	{ inst_nop,	OP_0 },
/*cf*/	{ inst_nop,	OP_0 },
/*d0*/	{ inst_nop,	OP_0 },
/*d1*/	{ inst_nop,	OP_0 },
/*d2*/	{ inst_nop,	OP_0 },
/*d3*/	{ inst_nop,	OP_0 },
/*d4*/	{ inst_nop,	OP_0 },
/*d5*/	{ inst_nop,	OP_0 },
/*d6*/	{ inst_nop,	OP_0 },
/*d7*/	{ inst_nop,	OP_0 },
/*d8*/	{ inst_nop,	OP_0 },
/*d9*/	{ inst_nop,	OP_0 },
/*da*/	{ inst_nop,	OP_0 },
/*db*/	{ inst_nop,	OP_0 },
/*dc*/	{ inst_nop,	OP_0 },
/*dd*/	{ inst_nop,	OP_0 },
/*de*/	{ inst_nop,	OP_0 },
/*df*/	{ inst_nop,	OP_0 },
/*e0*/	{ inst_nop,	OP_0 },
/*e1*/	{ inst_nop,	OP_0 },
/*e2*/	{ inst_nop,	OP_0 },
/*e3*/	{ inst_nop,	OP_0 },
/*e4*/	{ inst_nop,	OP_0 },
/*e5*/	{ inst_nop,	OP_0 },
/*e6*/	{ inst_nop,	OP_0 },
/*e7*/	{ inst_nop,	OP_0 },
/*e8*/	{ inst_nop,	OP_0 },
/*e9*/	{ inst_nop,	OP_0 },
/*ea*/	{ inst_nop,	OP_0 },
/*eb*/	{ inst_nop,	OP_0 },
/*ec*/	{ inst_nop,	OP_0 },
/*ed*/	{ inst_nop,	OP_0 },
/*ee*/	{ inst_nop,	OP_0 },
/*ef*/	{ inst_nop,	OP_0 },
/*f0*/	{ inst_nop,	OP_0 },
/*f1*/	{ inst_nop,	OP_0 },
/*f2*/	{ inst_nop,	OP_0 },
/*f3*/	{ inst_nop,	OP_0 },
/*f4*/	{ inst_nop,	OP_0 },
/*f5*/	{ inst_nop,	OP_0 },
/*f6*/	{ inst_nop,	OP_0 },
/*f7*/	{ inst_nop,	OP_0 },
/*f8*/	{ inst_nop,	OP_0 },
/*f9*/	{ inst_nop,	OP_0 },
/*fa*/	{ inst_nop,	OP_0 },
/*fb*/	{ inst_nop,	OP_0 },
/*fc*/	{ inst_nop,	OP_0 },
/*fd*/	{ inst_nop,	OP_0 },
/*fe*/	{ inst_nop,	OP_0 },
/*ff*/	{ inst_nop,	OP_0 }
};


/*
 * base plane function dispatcher table
 * (contains more or less the 8080-compatible instructions)
 */
static const struct instruction base_plane[256] = {
/*00*/	{ inst_nop,	OP_0 },
/*01*/	{ inst_lxi,	OP_ARG16 },
/*02*/	{ inst_stax,	OP_0 },
/*03*/	{ inst_inx,	OP_0 },
/*04*/	{ inst_inr,	OP_0 },
/*05*/	{ inst_dcr,	OP_0 },
/*06*/	{ inst_mvi,	OP_ARG8 },
/*07*/	{ inst_rlca,	OP_0 },
/*08*/	{ inst_exaf,	OP_0 },
/*09*/	{ inst_dad,	OP_0 },
/*0a*/	{ inst_ldax,	OP_0 },
/*0b*/	{ inst_dcx,	OP_0 },
/*0c*/	{ inst_inr,	OP_0 },
/*0d*/	{ inst_dcr,	OP_0 },
/*0e*/	{ inst_mvi,	OP_ARG8 },
/*0f*/	{ inst_rrca,	OP_0 },
/*10*/	{ inst_djnz,	OP_ARG8 },
/*11*/	{ inst_lxi,	OP_ARG16 },
/*12*/	{ inst_stax,	OP_0 },
/*13*/	{ inst_inx,	OP_0 },
/*14*/	{ inst_inr,	OP_0 },
/*15*/	{ inst_dcr,	OP_0 },
/*16*/	{ inst_mvi,	OP_ARG8 },
/*17*/	{ inst_rla,	OP_0 },
/*18*/	{ inst_jr,	OP_ARG8 },
/*19*/	{ inst_dad,	OP_0 },
/*1a*/	{ inst_ldax,	OP_0 },
/*1b*/	{ inst_dcx,	OP_0 },
/*1c*/	{ inst_inr,	OP_0 },
/*1d*/	{ inst_dcr,	OP_0 },
/*1e*/	{ inst_mvi,	OP_ARG8 },
/*1f*/	{ inst_rra,	OP_0 },
/*20*/	{ inst_jrcc,	OP_ARG8 },
/*21*/	{ inst_lxi,	OP_ARG16 },
/*22*/	{ inst_shld,	OP_ARG16 },
/*23*/	{ inst_inx,	OP_0 },
/*24*/	{ inst_inr,	OP_0 },
/*25*/	{ inst_dcr,	OP_0 },
/*26*/	{ inst_mvi,	OP_ARG8 },
/*27*/	{ inst_daa,	OP_0 },
/*28*/	{ inst_jrcc,	OP_ARG8 },
/*29*/	{ inst_dad,	OP_0 },
/*2a*/	{ inst_lhld,	OP_ARG16 },
/*2b*/	{ inst_dcx,	OP_0 },
/*2c*/	{ inst_inr,	OP_0 },
/*2d*/	{ inst_dcr,	OP_0 },
/*2e*/	{ inst_mvi,	OP_ARG8 },
/*2f*/	{ inst_cpl,	OP_0 },
/*30*/	{ inst_jrcc,	OP_ARG8 },
/*31*/	{ inst_lxi,	OP_ARG16 },
/*32*/	{ inst_sta,	OP_ARG16 },
/*33*/	{ inst_inx,	OP_0 },
/*34*/	{ inst_inr,	OP_INDEXED },
/*35*/	{ inst_dcr,	OP_INDEXED },
/*36*/	{ inst_mvi,	OP_INDEXED|OP_ARG8 },
/*37*/	{ inst_scf,	OP_0 },
/*38*/	{ inst_jrcc,	OP_ARG8 },
/*39*/	{ inst_dad,	OP_0 },
/*3a*/	{ inst_lda,	OP_ARG16 },
/*3b*/	{ inst_dcx,	OP_0 },
/*3c*/	{ inst_inr,	OP_0 },
/*3d*/	{ inst_dcr,	OP_0 },
/*3e*/	{ inst_mvi,	OP_ARG8 },
/*3f*/	{ inst_ccf,	OP_0 },
/*40*/	{ inst_mov,	OP_0 },
/*41*/	{ inst_mov,	OP_0 },
/*42*/	{ inst_mov,	OP_0 },
/*43*/	{ inst_mov,	OP_0 },
/*44*/	{ inst_mov,	OP_0 },
/*45*/	{ inst_mov,	OP_0 },
/*46*/	{ inst_mov,	OP_INDEXED },
/*47*/	{ inst_mov,	OP_0 },
/*48*/	{ inst_mov,	OP_0 },
/*49*/	{ inst_mov,	OP_0 },
/*4a*/	{ inst_mov,	OP_0 },
/*4b*/	{ inst_mov,	OP_0 },
/*4c*/	{ inst_mov,	OP_0 },
/*4d*/	{ inst_mov,	OP_0 },
/*4e*/	{ inst_mov,	OP_INDEXED },
/*4f*/	{ inst_mov,	OP_0 },
/*50*/	{ inst_mov,	OP_0 },
/*51*/	{ inst_mov,	OP_0 },
/*52*/	{ inst_mov,	OP_0 },
/*53*/	{ inst_mov,	OP_0 },
/*54*/	{ inst_mov,	OP_0 },
/*55*/	{ inst_mov,	OP_0 },
/*56*/	{ inst_mov,	OP_INDEXED },
/*57*/	{ inst_mov,	OP_0 },
/*58*/	{ inst_mov,	OP_0 },
/*59*/	{ inst_mov,	OP_0 },
/*5a*/	{ inst_mov,	OP_0 },
/*5b*/	{ inst_mov,	OP_0 },
/*5c*/	{ inst_mov,	OP_0 },
/*5d*/	{ inst_mov,	OP_0 },
/*5e*/	{ inst_mov,	OP_INDEXED },
/*5f*/	{ inst_mov,	OP_0 },
/*60*/	{ inst_mov,	OP_0 },
/*61*/	{ inst_mov,	OP_0 },
/*62*/	{ inst_mov,	OP_0 },
/*63*/	{ inst_mov,	OP_0 },
/*64*/	{ inst_mov,	OP_0 },
/*65*/	{ inst_mov,	OP_0 },
/*66*/	{ inst_mov,	OP_INDEXED },
/*67*/	{ inst_mov,	OP_0 },
/*68*/	{ inst_mov,	OP_0 },
/*69*/	{ inst_mov,	OP_0 },
/*6a*/	{ inst_mov,	OP_0 },
/*6b*/	{ inst_mov,	OP_0 },
/*6c*/	{ inst_mov,	OP_0 },
/*6d*/	{ inst_mov,	OP_0 },
/*6e*/	{ inst_mov,	OP_INDEXED },
/*6f*/	{ inst_mov,	OP_0 },
/*70*/	{ inst_mov,	OP_INDEXED },
/*71*/	{ inst_mov,	OP_INDEXED },
/*72*/	{ inst_mov,	OP_INDEXED },
/*73*/	{ inst_mov,	OP_INDEXED },
/*74*/	{ inst_mov,	OP_INDEXED },
/*75*/	{ inst_mov,	OP_INDEXED },
/*76*/	{ inst_halt,	OP_0 },
/*77*/	{ inst_mov,	OP_INDEXED },
/*78*/	{ inst_mov,	OP_0 },
/*79*/	{ inst_mov,	OP_0 },
/*7a*/	{ inst_mov,	OP_0 },
/*7b*/	{ inst_mov,	OP_0 },
/*7c*/	{ inst_mov,	OP_0 },
/*7d*/	{ inst_mov,	OP_0 },
/*7e*/	{ inst_mov,	OP_INDEXED },
/*7f*/	{ inst_mov,	OP_0 },
/*80*/	{ inst_add,	OP_0 },
/*81*/	{ inst_add,	OP_0 },
/*82*/	{ inst_add,	OP_0 },
/*83*/	{ inst_add,	OP_0 },
/*84*/	{ inst_add,	OP_0 },
/*85*/	{ inst_add,	OP_0 },
/*86*/	{ inst_add,	OP_INDEXED },
/*87*/	{ inst_add,	OP_0 },
/*88*/	{ inst_adc,	OP_0 },
/*89*/	{ inst_adc,	OP_0 },
/*8a*/	{ inst_adc,	OP_0 },
/*8b*/	{ inst_adc,	OP_0 },
/*8c*/	{ inst_adc,	OP_0 },
/*8d*/	{ inst_adc,	OP_0 },
/*8e*/	{ inst_adc,	OP_INDEXED },
/*8f*/	{ inst_adc,	OP_0 },
/*90*/	{ inst_sub,	OP_0 },
/*91*/	{ inst_sub,	OP_0 },
/*92*/	{ inst_sub,	OP_0 },
/*93*/	{ inst_sub,	OP_0 },
/*94*/	{ inst_sub,	OP_0 },
/*95*/	{ inst_sub,	OP_0 },
/*96*/	{ inst_sub,	OP_INDEXED },
/*97*/	{ inst_sub,	OP_0 },
/*98*/	{ inst_sbca,	OP_0 },
/*99*/	{ inst_sbca,	OP_0 },
/*9a*/	{ inst_sbca,	OP_0 },
/*9b*/	{ inst_sbca,	OP_0 },
/*9c*/	{ inst_sbca,	OP_0 },
/*9d*/	{ inst_sbca,	OP_0 },
/*9e*/	{ inst_sbca,	OP_INDEXED },
/*9f*/	{ inst_sbca,	OP_0 },
/*a0*/	{ inst_and,	OP_0 },
/*a1*/	{ inst_and,	OP_0 },
/*a2*/	{ inst_and,	OP_0 },
/*a3*/	{ inst_and,	OP_0 },
/*a4*/	{ inst_and,	OP_0 },
/*a5*/	{ inst_and,	OP_0 },
/*a6*/	{ inst_and,	OP_INDEXED },
/*a7*/	{ inst_and,	OP_0 },
/*a8*/	{ inst_xor,	OP_0 },
/*a9*/	{ inst_xor,	OP_0 },
/*aa*/	{ inst_xor,	OP_0 },
/*ab*/	{ inst_xor,	OP_0 },
/*ac*/	{ inst_xor,	OP_0 },
/*ad*/	{ inst_xor,	OP_0 },
/*ae*/	{ inst_xor,	OP_INDEXED },
/*af*/	{ inst_xor,	OP_0 },
/*b0*/	{ inst_or,	OP_0 },
/*b1*/	{ inst_or,	OP_0 },
/*b2*/	{ inst_or,	OP_0 },
/*b3*/	{ inst_or,	OP_0 },
/*b4*/	{ inst_or,	OP_0 },
/*b5*/	{ inst_or,	OP_0 },
/*b6*/	{ inst_or,	OP_INDEXED },
/*b7*/	{ inst_or,	OP_0 },
/*b8*/	{ inst_cmp,	OP_0 },
/*b9*/	{ inst_cmp,	OP_0 },
/*ba*/	{ inst_cmp,	OP_0 },
/*bb*/	{ inst_cmp,	OP_0 },
/*bc*/	{ inst_cmp,	OP_0 },
/*bd*/	{ inst_cmp,	OP_0 },
/*be*/	{ inst_cmp,	OP_INDEXED },
/*bf*/	{ inst_cmp,	OP_0 },
/*c0*/	{ inst_retcc,	OP_0 },
/*c1*/	{ inst_pop,	OP_0 },
/*c2*/	{ inst_jpcc,	OP_ARG16 },
/*c3*/	{ inst_jp,	OP_ARG16 },
/*c4*/	{ inst_callcc,	OP_ARG16 },
/*c5*/	{ inst_push,	OP_0 },
/*c6*/	{ inst_adi,	OP_ARG8 },
/*c7*/	{ inst_rst,	OP_0 },
/*c8*/	{ inst_retcc,	OP_0 },
/*c9*/	{ inst_ret,	OP_0 },
/*ca*/	{ inst_jpcc,	OP_ARG16 },
/*cb*/	{ inst_cb,	OP_INDEXED },
/*cc*/	{ inst_callcc,	OP_ARG16 },
/*cd*/	{ inst_call,	OP_ARG16 },
/*ce*/	{ inst_aci,	OP_ARG8 },
/*cf*/	{ inst_rst,	OP_0 },
/*d0*/	{ inst_retcc,	OP_0 },
/*d1*/	{ inst_pop,	OP_0 },
/*d2*/	{ inst_jpcc,	OP_ARG16 },
/*d3*/	{ inst_outa,	OP_ARG8 },
/*d4*/	{ inst_callcc,	OP_ARG16 },
/*d5*/	{ inst_push,	OP_0 },
/*d6*/	{ inst_sui,	OP_ARG8 },
/*d7*/	{ inst_rst,	OP_0 },
/*d8*/	{ inst_retcc,	OP_0 },
/*d9*/	{ inst_exx,	OP_0 },
/*da*/	{ inst_jpcc,	OP_ARG16 },
/*db*/	{ inst_ina,	OP_ARG8 },
/*dc*/	{ inst_callcc,	OP_ARG16 },
/*dd*/	{ NULL,	OP_0 },
/*de*/	{ inst_sbi,	OP_ARG8 },
/*df*/	{ inst_rst,	OP_0 },
/*e0*/	{ inst_retcc,	OP_0 },
/*e1*/	{ inst_pop,	OP_0 },
/*e2*/	{ inst_jpcc,	OP_ARG16 },
/*e3*/	{ inst_xthl,	OP_0 },
/*e4*/	{ inst_callcc,	OP_ARG16 },
/*e5*/	{ inst_push,	OP_0 },
/*e6*/	{ inst_ani,	OP_ARG8 },
/*e7*/	{ inst_rst,	OP_0 },
/*e8*/	{ inst_retcc,	OP_0 },
/*e9*/	{ inst_pchl,	OP_0 },
/*ea*/	{ inst_jpcc,	OP_ARG16 },
/*eb*/	{ inst_xchg,	OP_0 },
/*ec*/	{ inst_callcc,	OP_ARG16 },
/*ed*/	{ NULL,	OP_0 },
/*ee*/	{ inst_xri,	OP_ARG8 },
/*ef*/	{ inst_rst,	OP_0 },
/*f0*/	{ inst_retcc,	OP_0 },
/*f1*/	{ inst_pop,	OP_0 },
/*f2*/	{ inst_jpcc,	OP_ARG16 },
/*f3*/	{ inst_di,	OP_0 },
/*f4*/	{ inst_callcc,	OP_ARG16 },
/*f5*/	{ inst_push,	OP_0 },
/*f6*/	{ inst_ori,	OP_ARG8 },
/*f7*/	{ inst_rst,	OP_0 },
/*f8*/	{ inst_retcc,	OP_0 },
/*f9*/	{ inst_sphl,	OP_0 },
/*fa*/	{ inst_jpcc,	OP_ARG16 },
/*fb*/	{ inst_ei,	OP_0 },
/*fc*/	{ inst_callcc,	OP_ARG16 },
/*fd*/	{ NULL,	OP_0 },
/*fe*/	{ inst_cmpi,	OP_ARG8 },
/*ff*/	{ inst_rst,	OP_0 }
};


/*
 * instruction counters
 */
static unsigned long counters[256];
static unsigned long ed_counters[256];
static unsigned long cb_counters[256];
static unsigned long dd_counters[256];
static unsigned long fd_counters[256];
static unsigned long dd_cb_counters[256];
static unsigned long fd_cb_counters[256];


/*
 * longjmp on reception of SIGINT, SIGTERM, or SIGQUIT
 */
static jmp_buf signal_jmp;


/*
 * signal handler just jumps to the top of the main loop
 */
static void handler(int s) {
	struct sigaction sa;
	switch (s) {
	case SIGTERM:
	case SIGQUIT:
	case SIGINT:
		/*
		 * these signals are handled only once; repeated
		 * occurrences are ignored
		 */
		sa.sa_handler = SIG_IGN;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGQUIT, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
		longjmp(signal_jmp, 1);
		break;
	case SIGUSR1:
		dump = 1;
		break;
	}
}


/*
 * after this number of instructions, the console is polled
 */
#define POLL_INTERVAL (128 * 1024)


/*
 * start emulation proper
 */
void
cpu_run(void) {
	int poll_counter = 0, delay_counter = 0;
	const struct instruction *inst_p;
	struct sigaction sa;
	struct timespec delay;
	/*
	 * initialize the nanosecond delay value
	 */
	if (delay_nanoseconds > 0) {
		memset(&delay, 0, sizeof delay);
		delay.tv_sec = delay_nanoseconds / 1000000000;
		delay.tv_nsec = delay_nanoseconds % 1000000000;
	}
	/*
	 * catch signals for termination of a runaway program
	 */
	if (setjmp(signal_jmp)) {
		if (! terminate) {
			terminate = 1;
			term_reason = ERR_SIGNAL;
		}
	} else {
		/*
		 * signals causing the emulation to terminate with
		 * status ERR_SIGNAL; the handlers block the occurrence
		 * of the other signals to avoid calling logjmp() twice.
		 */
		sa.sa_handler = handler;
		sigemptyset(&sa.sa_mask);
		sigaddset(&sa.sa_mask, SIGTERM);
		sigaddset(&sa.sa_mask, SIGQUIT);
		sigaddset(&sa.sa_mask, SIGINT);
		sa.sa_flags = 0;
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGQUIT, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
	}
	/*
	 * install signal handler if dump signals are requested
	 */
	if (conf_dump & DUMP_SIGNAL) {
		sa.sa_handler = handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGUSR1, &sa, NULL);
	}
	while (! terminate) {
		/*
		 * dump machine state
		 */
		if (dump) {
			dump = 0;
			dump_machine("signal");
		}
		/*
		 * mark start of new instruction
		 */
		current_instruction = reg_pc;
		/*
		 * fetch next opcode, handle instruction prefixes
		 */
		prefix = 0x00;
		for (;;) {
			opcode = fetch_m1();
			if (opcode != 0xdd && opcode != 0xfd) break;
			prefix = opcode;
		}
		inst_p = base_plane + opcode;
		/*
		 * get optional displacement
		 */
		if (prefix && (inst_p->flags & OP_INDEXED)) disp = fetch();
		/*
		 * instructions starting in 0xed are handled in
		 * a slightly different way (contrary to those starting in
		 * 0xcb they are not influenced by prefixes and have
		 * non-uniform arguments)
		 */
		if (opcode == 0xcb) {
			opcode2 = prefix ? fetch_m1() : fetch();
			if (log_level >= LL_COUNTERS) {
				switch (prefix) {
				case 0xdd: dd_cb_counters[opcode2]++;
				case 0xfd: fd_cb_counters[opcode2]++;
				default: cb_counters[opcode2]++;
				}
			}
		} else if (opcode == 0xed) {
			opcode2 = fetch_m1();
			inst_p = ed_plane + opcode2;
			if (log_level >= LL_COUNTERS) {
				ed_counters[opcode2]++;
			}
		} else {
			if (log_level >= LL_COUNTERS) {
				switch (prefix) {
				case 0xdd: dd_counters[opcode]++;
				case 0xfd: fd_counters[opcode]++;
				default: counters[opcode]++;
				}
			}
		}
		/*
		 * get optional 8-bit argument
		 */
		if (inst_p->flags & OP_ARG8) op_low = fetch();
		/*
		 * get optional 16-bit argument
		 */
		if (inst_p->flags & OP_ARG16) {
			op_low = fetch();
			op_high = fetch();
		}
		/*
		 * execute instruction
		 */
		(*inst_p->handler_p)();
		/*
		 * Poll the console in regular intervals; this is a rather
		 * clumsy solution to keep the VT52 emulation happy even
		 * if a program doesn't care about console input for a
		 * prolonged period.
		 */
		poll_counter++;
		if (poll_counter == POLL_INTERVAL) {
			poll_counter = 0;
			console_poll();
		}
		if (delay_count > 0) {
			/*
			 * add a delay of delay_nanoseconds every
			 * delay_count emulated instructions
			 */
			delay_counter++;
			if (delay_counter >= delay_count) {
				delay_counter = 0;
				nanosleep(&delay, NULL);
			}
		}
	}
}


/*
 * copy the instruction call counters of a instruction plane to the log
 */
static void
dump_plane(unsigned long counters[256], const char *name) {
	int low, high, n;
	char buffer[1024], *cp;
	plog("instruction counters for %s:", name);
	cp = buffer;
	cp += sprintf(cp, "  ");
	for (high = 0; high < 16; high++) {
		cp += sprintf(cp, "         %1xy", high);
	}
	plog("%s", buffer);
	for (low = 0; low < 16; low++) {
		cp = buffer;
		cp += sprintf(cp, "x%1x", low);
		for (high = 0; high < 16; high++) {
			n = high * 16 + low;
			if (counters[n]) {
				cp += sprintf(cp, " %10lu", counters[n]);
			} else {
				cp += sprintf(cp, "          -");
			}
		}
		plog("%s", buffer);
	}
}


/*
 * save (parts of) the Z80 memory as an Intel Hex file
 */
static int
save_memory_hex(void) {
	int rc = 0;
	int addr, bytes, checksum, i;
	FILE *fp = NULL;
	/*
	 * create text file
	 */
	fp = fopen(conf_save_file, "w");
	if (! fp) {
		perr("cannot create %s: %s", conf_save_file, strerror(errno));
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * save data in records of at most 32 bytes
	 */
	addr = conf_save_start;
	while (addr <= conf_save_end) {
		bytes = conf_save_end - addr + 1;
		if (bytes > 32) bytes = 32;
		checksum = bytes + ((addr >> 8) & 0xff) + (addr & 0xff);
		fprintf(fp, ":%02X%04X00", bytes, addr);
		for (i = 0; i < bytes; i++) {
			fprintf(fp, "%02X", memory[addr + i]);
			checksum += memory[addr + i];
		}
		checksum = ((0x100 - (checksum & 0xff)) & 0xff);
		fprintf(fp, "%02X\n", checksum);
		addr += bytes;
	}
	/*
	 * write EOF record
	 */
	checksum = 0 + ((conf_save_start >> 8) & 0xff) +
	    (conf_save_start & 0xff) + 1;
	checksum = ((0x100 - (checksum & 0xff)) & 0xff);
	fprintf(fp, ":00%04X01%02X\n", conf_save_start, checksum);
	/*
	 * summary error check
	 */
	if (ferror(fp)) {
		perr("write error in %s: %s", conf_save_file, strerror(errno));
		rc = (-1);
	}
premature_exit:
	if (fp) {
		/*
		 * close text file
		 */
		if (fclose(fp)) {
			perr("cannot close %s: %s", conf_save_file,
			    strerror(errno));
			rc = (-1);
		}
	}
	return rc;
}


/*
 * save (parts of) the Z80 memory as a binary file
 */
static int
save_memory_bin(void) {
	int rc = 0;
	size_t n;
	FILE *fp = NULL;
	/*
	 * create binary file
	 */
	fp = fopen(conf_save_file, "wb");
	if (! fp) {
		perr("cannot create %s: %s", conf_save_file, strerror(errno));
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * write memory contents
	 */
	n = conf_save_end - conf_save_start + 1;
	if (fwrite(memory + conf_save_start, sizeof memory[0], n, fp) != n) {
		perr("write error on %s: %s", conf_save_file, strerror(errno));
		rc = (-1);
	}
premature_exit:
	if (fp) {
		/*
		 * close binary file
		 */
		if (fclose(fp)) {
			perr("cannot close %s: %s", conf_save_file,
			    strerror(errno));
			rc = (-1);
		}
	}
	return rc;
}


/*
 * clean up after emulation run
 */
int
cpu_exit(void) {
	int rc = 0;
	/*
	 * finalize OS emulation
	 */
	rc = os_exit();
	/*
	 * perform exit or error dump
	 */
	if (conf_dump & DUMP_EXIT) {
		dump_machine("exit");
	} else if (conf_dump & DUMP_ERROR) {
		if (term_reason > OK_CTRLC) dump_machine("error");
	}
	/*
	 * display reason for program termination
	 */
	switch (term_reason) {
	case OK_NOTRUN:
		break;
	case OK_TERM:
		break;
	case OK_CTRLC:
		break;
	case ERR_BOOT:
		perr("BIOS cold boot entry called");
		break;
	case ERR_BDOSARG:
		perr("invalid argument in BDOS call");
		break;
	case ERR_SELECT:
		perr("access to invalid/unconfigured disk");
		break;
	case ERR_RODISK:
		perr("attempted write access to read-only disk");
		break;
	case ERR_ROFILE:
		perr("attempted write access to read-only file");
		break;
	case ERR_HOST:
		perr("host system call failed");
		break;
	case ERR_LOGIC:
		perr("guest program logic error");
		break;
	case ERR_SIGNAL:
		perr("program execution stopped by signal");
		break;
	}
	if (term_reason <= OK_CTRLC) {
		if (conf_save_file) {
			/*
			 * save (part of) the Z80 memory area
			 * on regular program termination
			 */
			if (conf_save_hex) {
				if (save_memory_hex()) rc = (-1);
			} else {
				if (save_memory_bin()) rc = (-1);
			}
		}
	} else {
		rc = (-1);
	}
	/*
	 * deallocate memory
	 */
	free(memory);
	/*
	 * dump instruction counters
	 */
	if (log_level >= LL_COUNTERS) {
		dump_plane(counters, "base plane ");
		dump_plane(cb_counters, "0xcb plane");
		dump_plane(dd_counters, "0xdd base plane");
		dump_plane(dd_cb_counters, "0xdd 0xcb plane");
		dump_plane(ed_counters, "0xed plane ");
		dump_plane(fd_counters, "0xfd base plane");
		dump_plane(fd_cb_counters, "0xfd 0xcb plane");
	}
	return rc;
}
