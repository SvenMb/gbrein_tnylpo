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
#include <wchar.h>
#include <wctype.h>

#include "tnylpo.h"


/*
 * return base name of Unix path
 */
const char *
base_name(const char *path) {
	const char *cp = path + strlen(path);
	while (cp != path && *(cp - 1) != '/') cp--;
	return cp;
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
 * convert a Unix character to the CP/M character set
 * returns (-1) if the character cannot converted, and the 8-bit
 * character value otherwise
 */
int
to_cpm(wchar_t c) {
	int i;
	wchar_t **cs;
	/*
	 * control characters are passed through unaltered
	 */
	if ((c >= 0x00 && c <= 0x1f) || c == 0x7f) return (int) c;
	/*
	 * primary or alternate character set?
	 */
	cs = charset ? conf_alt_charset : conf_charset;
	/*
	 * check the potentially printable character range (skipping DEL)
	 */
	for (i = 0x20; i < 0x100; i++) {
		if (i == 0x7f || ! cs[i]) continue;
		if (cs[i][0] == c) return i;
	}
	return (-1);
}


/*
 * convert a CP/M character to a Unix character
 * returns (wint_t) (-1) if the character cannot be translated and there
 * is no representation for unprintable characters defined, and the
 * Unix wchar otherwise
 */
wint_t
from_cpm(unsigned char c) {
	wchar_t **cs;
	/*
	 * control characters are passed through unaltered
	 */
	if (c <= 0x1f /* US */ || c == 0x7f /* DEL */) return c;
	/*
	 * primary or alternate character set?
	 */
	cs = charset ? conf_alt_charset : conf_charset;
	if (! cs[c]) {
		/*
		 * replace untranslateable characters by the
		 * "unprintable" character, if one is defined
		 */
		if (conf_unprintable) return conf_unprintable[0];
		return (-1);
	}
	return cs[c][0];
}


/*
 * same as from_cpm(), but characters in the range 0x5e ... 0x7e are
 * mapped to 0x7f, 0x1f, 0x00 ... 0x1e to implement the "graphic
 * character set" feature of the VT52
 */
wint_t
from_graph(unsigned char c) {
	wchar_t **cs;
	/*
	 * control characters are passed through unaltered
	 */
	if (c <= 0x1f /* US */ || c == 0x7f /* DEL */) return c;
	/*
	 * map 0x5e ... 0x7e to the "graphic" positions
	 */
	if (c >= 0x60 /* @ */ && c <= 0x7e /* ~ */) {
		c -= 0x60; /* @ --> NUL; ~ --> RS */
	} else if (c == 0x5f /* _ */) {
		c = 0x1f /* US */;
	} else if (c == 0x5e /* ^ */) {
		c = 0x7f /* DEL */;
	}
	/*
	 * primary or alternate character set?
	 */
	cs = charset ? conf_alt_charset : conf_charset;
	if (! cs[c]) {
		if (conf_unprintable) return conf_unprintable[0];
		return (-1);
	}
	return cs[c][0];
}
