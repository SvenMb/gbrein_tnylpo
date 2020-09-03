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


#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <limits.h>
#include <errno.h>

#include <unistd.h>

#include "tnylpo.h"


/*
 * global variables containing the configuration information
 * from the command line resp. from the configuration file
 */
/*
 * size of the emulated terminal (doesn't change)
 */
int lines = 0;
int cols = 0;
/*
 * use terminal emulation (1) or line orientated/batch console (0)
 */
int conf_interactive = (-1);
/*
 * use WordStar (1) or VT52 (0) cursor keys (terminal emulation only)
 */
int altkeys = (-1);
/*
 * reverse the backspace and the delete key (terminal emulation only)
 */
int reverse_bs_del = (-1);
/*
 * seconds to wait before exiting full screen mode (terminal emulation only)
 */
int screen_delay = (-1);
/*
 * CP/M charset in use (0 ... primary, 1 ... secondary);
 */
int charset = 0;
/*
 * primary and secondary character set
 */
wchar_t *conf_charset[256];
wchar_t *conf_alt_charset[256];
/*
 * use this character to represent unprintable characters
 */
wchar_t *conf_unprintable = NULL;
/*
 * paths corresponding to the CP/M drives A...P and their read-only flags
 */
char *conf_drives[16];
int conf_readonly[16];
/*
 * name of the command file to execute
 */
char *conf_command = NULL;
/*
 * additional command line parameters
 */
int conf_argc = 0;
char **conf_argv = NULL;
/*
 * files corresponding to the CP/M character devices LST, PUN, and RDR
 * and the flags controlling translation to Unix format
 */
char *conf_printer = NULL;
int conf_printer_raw = (-1);
char *conf_punch = NULL;
int conf_punch_raw = (-1);
char *conf_reader = NULL;
int conf_reader_raw = (-1);
/*
 * path of the log file
 */
char *conf_log = NULL;
/*
 * log level
 */
enum log_level log_level = LL_UNSET;
/*
 * CP/M default drive (0...15 corresponding to A...P)
 */
int default_drive = (-1);
/*
 * flag controlling wether calling BDOS function 19 closes the
 * corresponding Unix file or not (some programs, e.g. dBase II continue
 * to use FCBs for file I/O after closing them)
 */
int dont_close = (-1);
/*
 * dump configuration: default is no dump
 */
enum dump conf_dump = 0;
/*
 * emulation delay: insert a pause of delay_nanoseconds every
 * delay_count instructions (default: no delay)
 */
int delay_count = (-1);
int delay_nanoseconds = (-1);
/*
 * save configuration: default is no saving done
 */
const char *conf_save_file = NULL;
int conf_save_hex = 0;
int conf_save_start = 0;
int conf_save_end = 0;


/*
 * maximal length of a line in the configuration file
 */
#define L_LINE 1024


/*
 * variables for communication between the procedures of the
 * configuration file parser
 */
/*
 * file pointer, file name, and current line number of the configuration file
 */
static FILE *cf = NULL;
static char *cfn = NULL;
static int ln = 0;
/*
 * pointer to the current character in current line of the configuration file
 */
static wchar_t *curr_p = NULL;
/*
 * current token and its attributes
 */
static int token = (-1);
static unsigned long token_ul = 0;
static wchar_t *token_ident = NULL;
static wchar_t *token_string = NULL;
/*
 * character class for blank; Solaris < 10 doesn't support
 * iswblank(), so we use iswctype() instead; for this, we need the
 * value of wctype("blank"), which we get once and store in this variable
 */
static wctype_t wctype_blank;


/*
 * built-in character sets: VT52 (ASCII + VT52 graphical character set),
 * pure ASCII, and ISO-8859-1/Latin 1
 */
enum charset {
	CS_NONE = (-1),
	CS_VT52 = 0,
	CS_ASCII = 1,
	CS_LATIN1 = 2,
	CS_TNYLPO = 3
};


/*
 * Unfortunately, some platforms (though supporting UTF-8) have
 * problems with some characters, e. g. Solaris < 10, which displays
 * some symbols and the box-drawing characters in double width,
 * which is not supported by tnylpo.
 *
 * The following #ifs are a hack to keep tnylpo useable on these platforms.
 */


/*
 * built-in character sets in wchar_t representation (unfortunately,
 * not all of the VT52 graphical characters are available)
 */
static const wchar_t *default_charset[256][4] = {
#if (defined OLD_SOLARIS)
/*00*/	{ NULL, NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ L"°", NULL, NULL, L"·" },
	{ L"±", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"÷", NULL, NULL, L"+" },
	{ L"·", NULL, NULL, L"+" },
	{ NULL, NULL, NULL, L"+" },
	{ NULL, NULL, NULL, L"+" },
	{ NULL, NULL, NULL, L"+" },
	{ NULL, NULL, NULL, L"·" },
/*10*/	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"-" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"+" },
	{ L"·", NULL, NULL, L"+" },
	{ L"·", NULL, NULL, L"+" },
	{ L"·", NULL, NULL, L"+" },
	{ L"·", NULL, NULL, L"|" },
	{ L"·", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"·", NULL, NULL, L"·" },
	{ L"¶", NULL, NULL, L"·" },
	{ L" ", NULL, NULL, L"·" },
/*20*/	{ L" ", L" ", L" ", L" " },
	{ L"!", L"!", L"!", L"!" },
	{ L"\"", L"\"", L"\"", L"\"" },
	{ L"#", L"#", L"#", L"#" },
	{ L"$", L"$", L"$", L"$" },
	{ L"%", L"%", L"%", L"%" },
	{ L"&", L"&", L"&", L"&" },
	{ L"\'", L"\'", L"\'", L"\'" },
	{ L"(", L"(", L"(", L"(" },
	{ L")", L")", L")", L")" },
	{ L"*", L"*", L"*", L"*" },
	{ L"+", L"+", L"+", L"+" },
	{ L",", L",", L",", L"," },
	{ L"-", L"-", L"-", L"-" },
	{ L".", L".", L".", L"." },
	{ L"/", L"/", L"/", L"/" },
/*30*/	{ L"0", L"0", L"0", L"0" },
	{ L"1", L"1", L"1", L"1" },
	{ L"2", L"2", L"2", L"2" },
	{ L"3", L"3", L"3", L"3" },
	{ L"4", L"4", L"4", L"4" },
	{ L"5", L"5", L"5", L"5" },
	{ L"6", L"6", L"6", L"6" },
	{ L"7", L"7", L"7", L"7" },
	{ L"8", L"8", L"8", L"8" },
	{ L"9", L"9", L"9", L"9" },
	{ L":", L":", L":", L":" },
	{ L";", L";", L";", L";" },
	{ L"<", L"<", L"<", L"<" },
	{ L"=", L"=", L"=", L"=" },
	{ L">", L">", L">", L">" },
	{ L"?", L"?", L"?", L"?" },
/*40*/	{ L"@", L"@", L"@", L"@" },
	{ L"A", L"A", L"A", L"A" },
	{ L"B", L"B", L"B", L"B" },
	{ L"C", L"C", L"C", L"C" },
	{ L"D", L"D", L"D", L"D" },
	{ L"E", L"E", L"E", L"E" },
	{ L"F", L"F", L"F", L"F" },
	{ L"G", L"G", L"G", L"G" },
	{ L"H", L"H", L"H", L"H" },
	{ L"I", L"I", L"I", L"I" },
	{ L"J", L"J", L"J", L"J" },
	{ L"K", L"K", L"K", L"K" },
	{ L"L", L"L", L"L", L"L" },
	{ L"M", L"M", L"M", L"M" },
	{ L"N", L"N", L"N", L"N" },
	{ L"O", L"O", L"O", L"O" },
/*50*/	{ L"P", L"P", L"P", L"P" },
	{ L"Q", L"Q", L"Q", L"Q" },
	{ L"R", L"R", L"R", L"R" },
	{ L"S", L"S", L"S", L"S" },
	{ L"T", L"T", L"T", L"T" },
	{ L"U", L"U", L"U", L"U" },
	{ L"V", L"V", L"V", L"V" },
	{ L"W", L"W", L"W", L"W" },
	{ L"X", L"X", L"X", L"X" },
	{ L"Y", L"Y", L"Y", L"Y" },
	{ L"Z", L"Z", L"Z", L"Z" },
	{ L"[", L"[", L"[", L"[" },
	{ L"\\", L"\\", L"\\", L"\\" },
	{ L"]", L"]", L"]", L"]" },
	{ L"^", L"^", L"^", L"^" },
	{ L"_", L"_", L"_", L"_" },
/*60*/	{ L"`", L"`", L"`", L"`" },
	{ L"a", L"a", L"a", L"a" },
	{ L"b", L"b", L"b", L"b" },
	{ L"c", L"c", L"c", L"c" },
	{ L"d", L"d", L"d", L"d" },
	{ L"e", L"e", L"e", L"e" },
	{ L"f", L"f", L"f", L"f" },
	{ L"g", L"g", L"g", L"g" },
	{ L"h", L"h", L"h", L"h" },
	{ L"i", L"i", L"i", L"i" },
	{ L"j", L"j", L"j", L"j" },
	{ L"k", L"k", L"k", L"k" },
	{ L"l", L"l", L"l", L"l" },
	{ L"m", L"m", L"m", L"m" },
	{ L"n", L"n", L"n", L"n" },
	{ L"o", L"o", L"o", L"o" },
/*70*/	{ L"p", L"p", L"p", L"p" },
	{ L"q", L"q", L"q", L"q" },
	{ L"r", L"r", L"r", L"r" },
	{ L"s", L"s", L"s", L"s" },
	{ L"t", L"t", L"t", L"t" },
	{ L"u", L"u", L"u", L"u" },
	{ L"v", L"v", L"v", L"v" },
	{ L"w", L"w", L"w", L"w" },
	{ L"x", L"x", L"x", L"x" },
	{ L"y", L"y", L"y", L"y" },
	{ L"z", L"z", L"z", L"z" },
	{ L"{", L"{", L"{", L"{" },
	{ L"|", L"|", L"|", L"|" },
	{ L"}", L"}", L"}", L"}" },
	{ L"~", L"~", L"~", L"~" },
	{ NULL, NULL, NULL, L"·" },
/*80*/	{ NULL, NULL, NULL, L"€" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"Š" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"Œ" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"Ž" },
	{ NULL, NULL, NULL, L"·" },
/*90*/	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"š" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"œ" },
	{ NULL, NULL, NULL, L"·" },
	{ NULL, NULL, NULL, L"ž" },
	{ NULL, NULL, NULL, L"Ÿ" },
/*a0*/	{ NULL, NULL, L" ", L" " },
	{ NULL, NULL, L"¡", L"¡" },
	{ NULL, NULL, L"¢", L"¢" },
	{ NULL, NULL, L"£", L"£" },
	{ NULL, NULL, L"¤", L"¤" },
	{ NULL, NULL, L"¥", L"¥" },
	{ NULL, NULL, L"¦", L"¦" },
	{ NULL, NULL, L"§", L"§" },
	{ NULL, NULL, L"¨", L"¨" },
	{ NULL, NULL, L"©", L"©" },
	{ NULL, NULL, L"ª", L"ª" },
	{ NULL, NULL, L"«", L"«" },
	{ NULL, NULL, L"¬", L"¬" },
	{ NULL, NULL, L"-", L"-" },
	{ NULL, NULL, L"®", L"®" },
	{ NULL, NULL, L"¯", L"¯" },
/*b0*/	{ NULL, NULL, L"°", L"°" },
	{ NULL, NULL, L"±", L"±" },
	{ NULL, NULL, L"²", L"²" },
	{ NULL, NULL, L"³", L"³" },
	{ NULL, NULL, L"´", L"´" },
	{ NULL, NULL, L"µ", L"µ" },
	{ NULL, NULL, L"¶", L"¶" },
	{ NULL, NULL, L"·", L"·" },
	{ NULL, NULL, L"¸", L"¸" },
	{ NULL, NULL, L"¹", L"¹" },
	{ NULL, NULL, L"º", L"º" },
	{ NULL, NULL, L"»", L"»" },
	{ NULL, NULL, L"¼", L"¼" },
	{ NULL, NULL, L"½", L"½" },
	{ NULL, NULL, L"¾", L"¾" },
	{ NULL, NULL, L"¿", L"¿" },
/*c0*/	{ NULL, NULL, L"À", L"À" },
	{ NULL, NULL, L"Á", L"Á" },
	{ NULL, NULL, L"Â", L"Â" },
	{ NULL, NULL, L"Ã", L"Ã" },
	{ NULL, NULL, L"Ä", L"Ä" },
	{ NULL, NULL, L"Å", L"Å" },
	{ NULL, NULL, L"Æ", L"Æ" },
	{ NULL, NULL, L"Ç", L"Ç" },
	{ NULL, NULL, L"È", L"È" },
	{ NULL, NULL, L"É", L"É" },
	{ NULL, NULL, L"Ê", L"Ê" },
	{ NULL, NULL, L"Ë", L"Ë" },
	{ NULL, NULL, L"Ì", L"Ì" },
	{ NULL, NULL, L"Í", L"Í" },
	{ NULL, NULL, L"Î", L"Î" },
	{ NULL, NULL, L"Ï", L"Ï" },
/*d0*/	{ NULL, NULL, L"Ð", L"Ð" },
	{ NULL, NULL, L"Ñ", L"Ñ" },
	{ NULL, NULL, L"Ò", L"Ò" },
	{ NULL, NULL, L"Ó", L"Ó" },
	{ NULL, NULL, L"Ô", L"Ô" },
	{ NULL, NULL, L"Õ", L"Õ" },
	{ NULL, NULL, L"Ö", L"Ö" },
	{ NULL, NULL, L"×", L"×" },
	{ NULL, NULL, L"Ø", L"Ø" },
	{ NULL, NULL, L"Ù", L"Ù" },
	{ NULL, NULL, L"Ú", L"Ú" },
	{ NULL, NULL, L"Û", L"Û" },
	{ NULL, NULL, L"Ü", L"Ü" },
	{ NULL, NULL, L"Ý", L"Ý" },
	{ NULL, NULL, L"Þ", L"Þ" },
	{ NULL, NULL, L"ß", L"ß" },
/*e0*/	{ NULL, NULL, L"à", L"à" },
	{ NULL, NULL, L"á", L"á" },
	{ NULL, NULL, L"â", L"â" },
	{ NULL, NULL, L"ã", L"ã" },
	{ NULL, NULL, L"ä", L"ä" },
	{ NULL, NULL, L"å", L"å" },
	{ NULL, NULL, L"æ", L"æ" },
	{ NULL, NULL, L"ç", L"ç" },
	{ NULL, NULL, L"è", L"è" },
	{ NULL, NULL, L"é", L"é" },
	{ NULL, NULL, L"ê", L"ê" },
	{ NULL, NULL, L"ë", L"ë" },
	{ NULL, NULL, L"ì", L"ì" },
	{ NULL, NULL, L"í", L"í" },
	{ NULL, NULL, L"î", L"î" },
	{ NULL, NULL, L"ï", L"ï" },
/*f0*/	{ NULL, NULL, L"ð", L"ð" },
	{ NULL, NULL, L"ñ", L"ñ" },
	{ NULL, NULL, L"ò", L"ò" },
	{ NULL, NULL, L"ó", L"ó" },
	{ NULL, NULL, L"ô", L"ô" },
	{ NULL, NULL, L"õ", L"õ" },
	{ NULL, NULL, L"ö", L"ö" },
	{ NULL, NULL, L"÷", L"÷" },
	{ NULL, NULL, L"ø", L"ø" },
	{ NULL, NULL, L"ù", L"ù" },
	{ NULL, NULL, L"ú", L"ú" },
	{ NULL, NULL, L"û", L"û" },
	{ NULL, NULL, L"ü", L"ü" },
	{ NULL, NULL, L"ý", L"ý" },
	{ NULL, NULL, L"þ", L"þ" },
	{ NULL, NULL, L"ÿ", L"ÿ" }
#else
/*00*/	{ NULL, NULL, NULL, L"▄" },
	{ L"█", NULL, NULL, L"█" },
	{ L"⅟", NULL, NULL, L"▐" },
	{ NULL, NULL, NULL, L"▖" },
	{ NULL, NULL, NULL, L"▗" },
	{ NULL, NULL, NULL, L"▘" },
	{ L"°", NULL, NULL, L"▝" },
	{ L"±", NULL, NULL, L"▌" },
	{ L"→", NULL, NULL, L"▀" },
	{ L"…", NULL, NULL, L"▞" },
	{ L"÷", NULL, NULL, L"┘" },
	{ L"↓", NULL, NULL, L"┐" },
	{ NULL, NULL, NULL, L"┌" },
	{ NULL, NULL, NULL, L"└" },
	{ NULL, NULL, NULL, L"┼" },
	{ NULL, NULL, NULL, L"▙" },
/*10*/	{ NULL, NULL, NULL, L"▛" },
	{ NULL, NULL, NULL, L"─" },
	{ NULL, NULL, NULL, L"▜" },
	{ NULL, NULL, NULL, L"▟" },
	{ L"₀", NULL, NULL, L"├" },
	{ L"₁", NULL, NULL, L"┤" },
	{ L"₂", NULL, NULL, L"┴" },
	{ L"₃", NULL, NULL, L"┬" },
	{ L"₄", NULL, NULL, L"│" },
	{ L"₅", NULL, NULL, L"←" },
	{ L"₆", NULL, NULL, L"↑" },
	{ L"₇", NULL, NULL, L"→" },
	{ L"₈", NULL, NULL, L"↓" },
	{ L"₉", NULL, NULL, L"▚" },
	{ L"¶", NULL, NULL, L"░" },
	{ L" ", NULL, NULL, L"▒" },
/*20*/	{ L" ", L" ", L" ", L" " },
	{ L"!", L"!", L"!", L"!" },
	{ L"\"", L"\"", L"\"", L"\"" },
	{ L"#", L"#", L"#", L"#" },
	{ L"$", L"$", L"$", L"$" },
	{ L"%", L"%", L"%", L"%" },
	{ L"&", L"&", L"&", L"&" },
	{ L"\'", L"\'", L"\'", L"\'" },
	{ L"(", L"(", L"(", L"(" },
	{ L")", L")", L")", L")" },
	{ L"*", L"*", L"*", L"*" },
	{ L"+", L"+", L"+", L"+" },
	{ L",", L",", L",", L"," },
	{ L"-", L"-", L"-", L"-" },
	{ L".", L".", L".", L"." },
	{ L"/", L"/", L"/", L"/" },
/*30*/	{ L"0", L"0", L"0", L"0" },
	{ L"1", L"1", L"1", L"1" },
	{ L"2", L"2", L"2", L"2" },
	{ L"3", L"3", L"3", L"3" },
	{ L"4", L"4", L"4", L"4" },
	{ L"5", L"5", L"5", L"5" },
	{ L"6", L"6", L"6", L"6" },
	{ L"7", L"7", L"7", L"7" },
	{ L"8", L"8", L"8", L"8" },
	{ L"9", L"9", L"9", L"9" },
	{ L":", L":", L":", L":" },
	{ L";", L";", L";", L";" },
	{ L"<", L"<", L"<", L"<" },
	{ L"=", L"=", L"=", L"=" },
	{ L">", L">", L">", L">" },
	{ L"?", L"?", L"?", L"?" },
/*40*/	{ L"@", L"@", L"@", L"@" },
	{ L"A", L"A", L"A", L"A" },
	{ L"B", L"B", L"B", L"B" },
	{ L"C", L"C", L"C", L"C" },
	{ L"D", L"D", L"D", L"D" },
	{ L"E", L"E", L"E", L"E" },
	{ L"F", L"F", L"F", L"F" },
	{ L"G", L"G", L"G", L"G" },
	{ L"H", L"H", L"H", L"H" },
	{ L"I", L"I", L"I", L"I" },
	{ L"J", L"J", L"J", L"J" },
	{ L"K", L"K", L"K", L"K" },
	{ L"L", L"L", L"L", L"L" },
	{ L"M", L"M", L"M", L"M" },
	{ L"N", L"N", L"N", L"N" },
	{ L"O", L"O", L"O", L"O" },
/*50*/	{ L"P", L"P", L"P", L"P" },
	{ L"Q", L"Q", L"Q", L"Q" },
	{ L"R", L"R", L"R", L"R" },
	{ L"S", L"S", L"S", L"S" },
	{ L"T", L"T", L"T", L"T" },
	{ L"U", L"U", L"U", L"U" },
	{ L"V", L"V", L"V", L"V" },
	{ L"W", L"W", L"W", L"W" },
	{ L"X", L"X", L"X", L"X" },
	{ L"Y", L"Y", L"Y", L"Y" },
	{ L"Z", L"Z", L"Z", L"Z" },
	{ L"[", L"[", L"[", L"[" },
	{ L"\\", L"\\", L"\\", L"\\" },
	{ L"]", L"]", L"]", L"]" },
	{ L"^", L"^", L"^", L"^" },
	{ L"_", L"_", L"_", L"_" },
/*60*/	{ L"`", L"`", L"`", L"`" },
	{ L"a", L"a", L"a", L"a" },
	{ L"b", L"b", L"b", L"b" },
	{ L"c", L"c", L"c", L"c" },
	{ L"d", L"d", L"d", L"d" },
	{ L"e", L"e", L"e", L"e" },
	{ L"f", L"f", L"f", L"f" },
	{ L"g", L"g", L"g", L"g" },
	{ L"h", L"h", L"h", L"h" },
	{ L"i", L"i", L"i", L"i" },
	{ L"j", L"j", L"j", L"j" },
	{ L"k", L"k", L"k", L"k" },
	{ L"l", L"l", L"l", L"l" },
	{ L"m", L"m", L"m", L"m" },
	{ L"n", L"n", L"n", L"n" },
	{ L"o", L"o", L"o", L"o" },
/*70*/	{ L"p", L"p", L"p", L"p" },
	{ L"q", L"q", L"q", L"q" },
	{ L"r", L"r", L"r", L"r" },
	{ L"s", L"s", L"s", L"s" },
	{ L"t", L"t", L"t", L"t" },
	{ L"u", L"u", L"u", L"u" },
	{ L"v", L"v", L"v", L"v" },
	{ L"w", L"w", L"w", L"w" },
	{ L"x", L"x", L"x", L"x" },
	{ L"y", L"y", L"y", L"y" },
	{ L"z", L"z", L"z", L"z" },
	{ L"{", L"{", L"{", L"{" },
	{ L"|", L"|", L"|", L"|" },
	{ L"}", L"}", L"}", L"}" },
	{ L"~", L"~", L"~", L"~" },
	{ NULL, NULL, NULL, L"▓" },
/*80*/	{ NULL, NULL, NULL, L"€" },
	{ NULL, NULL, NULL, L"≠" },
	{ NULL, NULL, NULL, L"‚" },
	{ NULL, NULL, NULL, L"ƒ" },
	{ NULL, NULL, NULL, L"„" },
	{ NULL, NULL, NULL, L"…" },
	{ NULL, NULL, NULL, L"†" },
	{ NULL, NULL, NULL, L"‡" },
	{ NULL, NULL, NULL, L"ˆ" },
	{ NULL, NULL, NULL, L"‰" },
	{ NULL, NULL, NULL, L"Š" },
	{ NULL, NULL, NULL, L"‹" },
	{ NULL, NULL, NULL, L"Œ" },
	{ NULL, NULL, NULL, L"Ĳ" },
	{ NULL, NULL, NULL, L"Ž" },
	{ NULL, NULL, NULL, L"≤" },
/*90*/	{ NULL, NULL, NULL, L"≥" },
	{ NULL, NULL, NULL, L"‘" },
	{ NULL, NULL, NULL, L"’" },
	{ NULL, NULL, NULL, L"“" },
	{ NULL, NULL, NULL, L"”" },
	{ NULL, NULL, NULL, L"•" },
	{ NULL, NULL, NULL, L"–" },
	{ NULL, NULL, NULL, L"—" },
	{ NULL, NULL, NULL, L"˜" },
	{ NULL, NULL, NULL, L"™" },
	{ NULL, NULL, NULL, L"š" },
	{ NULL, NULL, NULL, L"›" },
	{ NULL, NULL, NULL, L"œ" },
	{ NULL, NULL, NULL, L"ĳ" },
	{ NULL, NULL, NULL, L"ž" },
	{ NULL, NULL, NULL, L"Ÿ" },
/*a0*/	{ NULL, NULL, L" ", L" " },
	{ NULL, NULL, L"¡", L"¡" },
	{ NULL, NULL, L"¢", L"¢" },
	{ NULL, NULL, L"£", L"£" },
	{ NULL, NULL, L"¤", L"¤" },
	{ NULL, NULL, L"¥", L"¥" },
	{ NULL, NULL, L"¦", L"¦" },
	{ NULL, NULL, L"§", L"§" },
	{ NULL, NULL, L"¨", L"¨" },
	{ NULL, NULL, L"©", L"©" },
	{ NULL, NULL, L"ª", L"ª" },
	{ NULL, NULL, L"«", L"«" },
	{ NULL, NULL, L"¬", L"¬" },
	{ NULL, NULL, L"–", L"–" },
	{ NULL, NULL, L"®", L"®" },
	{ NULL, NULL, L"¯", L"¯" },
/*b0*/	{ NULL, NULL, L"°", L"°" },
	{ NULL, NULL, L"±", L"±" },
	{ NULL, NULL, L"²", L"²" },
	{ NULL, NULL, L"³", L"³" },
	{ NULL, NULL, L"´", L"´" },
	{ NULL, NULL, L"µ", L"µ" },
	{ NULL, NULL, L"¶", L"¶" },
	{ NULL, NULL, L"·", L"·" },
	{ NULL, NULL, L"¸", L"¸" },
	{ NULL, NULL, L"¹", L"¹" },
	{ NULL, NULL, L"º", L"º" },
	{ NULL, NULL, L"»", L"»" },
	{ NULL, NULL, L"¼", L"¼" },
	{ NULL, NULL, L"½", L"½" },
	{ NULL, NULL, L"¾", L"¾" },
	{ NULL, NULL, L"¿", L"¿" },
/*c0*/	{ NULL, NULL, L"À", L"À" },
	{ NULL, NULL, L"Á", L"Á" },
	{ NULL, NULL, L"Â", L"Â" },
	{ NULL, NULL, L"Ã", L"Ã" },
	{ NULL, NULL, L"Ä", L"Ä" },
	{ NULL, NULL, L"Å", L"Å" },
	{ NULL, NULL, L"Æ", L"Æ" },
	{ NULL, NULL, L"Ç", L"Ç" },
	{ NULL, NULL, L"È", L"È" },
	{ NULL, NULL, L"É", L"É" },
	{ NULL, NULL, L"Ê", L"Ê" },
	{ NULL, NULL, L"Ë", L"Ë" },
	{ NULL, NULL, L"Ì", L"Ì" },
	{ NULL, NULL, L"Í", L"Í" },
	{ NULL, NULL, L"Î", L"Î" },
	{ NULL, NULL, L"Ï", L"Ï" },
/*d0*/	{ NULL, NULL, L"Ð", L"Ð" },
	{ NULL, NULL, L"Ñ", L"Ñ" },
	{ NULL, NULL, L"Ò", L"Ò" },
	{ NULL, NULL, L"Ó", L"Ó" },
	{ NULL, NULL, L"Ô", L"Ô" },
	{ NULL, NULL, L"Õ", L"Õ" },
	{ NULL, NULL, L"Ö", L"Ö" },
	{ NULL, NULL, L"×", L"×" },
	{ NULL, NULL, L"Ø", L"Ø" },
	{ NULL, NULL, L"Ù", L"Ù" },
	{ NULL, NULL, L"Ú", L"Ú" },
	{ NULL, NULL, L"Û", L"Û" },
	{ NULL, NULL, L"Ü", L"Ü" },
	{ NULL, NULL, L"Ý", L"Ý" },
	{ NULL, NULL, L"Þ", L"Þ" },
	{ NULL, NULL, L"ß", L"ß" },
/*e0*/	{ NULL, NULL, L"à", L"à" },
	{ NULL, NULL, L"á", L"á" },
	{ NULL, NULL, L"â", L"â" },
	{ NULL, NULL, L"ã", L"ã" },
	{ NULL, NULL, L"ä", L"ä" },
	{ NULL, NULL, L"å", L"å" },
	{ NULL, NULL, L"æ", L"æ" },
	{ NULL, NULL, L"ç", L"ç" },
	{ NULL, NULL, L"è", L"è" },
	{ NULL, NULL, L"é", L"é" },
	{ NULL, NULL, L"ê", L"ê" },
	{ NULL, NULL, L"ë", L"ë" },
	{ NULL, NULL, L"ì", L"ì" },
	{ NULL, NULL, L"í", L"í" },
	{ NULL, NULL, L"î", L"î" },
	{ NULL, NULL, L"ï", L"ï" },
/*f0*/	{ NULL, NULL, L"ð", L"ð" },
	{ NULL, NULL, L"ñ", L"ñ" },
	{ NULL, NULL, L"ò", L"ò" },
	{ NULL, NULL, L"ó", L"ó" },
	{ NULL, NULL, L"ô", L"ô" },
	{ NULL, NULL, L"õ", L"õ" },
	{ NULL, NULL, L"ö", L"ö" },
	{ NULL, NULL, L"÷", L"÷" },
	{ NULL, NULL, L"ø", L"ø" },
	{ NULL, NULL, L"ù", L"ù" },
	{ NULL, NULL, L"ú", L"ú" },
	{ NULL, NULL, L"û", L"û" },
	{ NULL, NULL, L"ü", L"ü" },
	{ NULL, NULL, L"ý", L"ý" },
	{ NULL, NULL, L"þ", L"þ" },
	{ NULL, NULL, L"ÿ", L"ÿ" }
#endif
};


/*
 * three functions for common syntax errors in the configuration file
 */
static void
pexpected(const char *s) {
	perr("%s(%d): %s expected", cfn, ln, s);
}


static void
pinvalid(const char *s) {
	perr("%s(%d): invalid %s", cfn, ln, s);
}


static void
predefined(const char *s) {
	perr("%s(%d): %s redefined", cfn, ln, s);
}


/*
 * free the current token and its attributes
 */
static void
free_token(void) {
	token = (-1);
	free(token_string);
	token_string = NULL;
	free(token_ident);
	token_ident = NULL;
}


/*
 * read the next token from the configuration file and return it
 * and ts attributes via static variables
 */
static void
get_token(void) {
	const wchar_t *start_p;
	wchar_t *tp;
	size_t lb, ls;
	/*
	 * free the last token resp. its remains, set token to invalid (-1)
	 */
	free_token();
	/*
	 * skip blanks
	 */
	while (iswctype(*curr_p, wctype_blank)) curr_p++;
	/*
	 * test for EOL resp. start of a comment
	 */
	if (*curr_p == L'\0' || *curr_p == L'#' || *curr_p == L';') {
		/*
		 * 0 is the token for EOL
		 */
		token = 0;
	} else if (iswdigit(*curr_p)) {
		/*
		 * tokens starting in a decimal digit are numbers;
		 * hexadecimal (0x), octal (0), and decimal (1..9)
		 * numbers are accepted
		 */
		errno = 0;
		if (*curr_p == L'0') {
			if (*(curr_p + 1) == L'x') {
				token_ul = wcstoul(curr_p + 2, &tp, 16);
			} else {
				token_ul = wcstoul(curr_p, &tp, 8);
			}
		} else {
			token_ul = wcstoul(curr_p, &tp, 10);
		}
		/*
		 * check for numeric overflow
		 */
		if (token_ul == ULONG_MAX && errno == ERANGE) {
			perr("%s(%d): integer out of range", cfn, ln);
		} else {
			/*
			 * token is a number
			 */
			token = '0';
			curr_p = tp;
		}
	} else if (iswalpha(*curr_p)) {
		/*
		 * identifiers start with an alphabetic character...
		 */
		start_p = curr_p;
		/*
		 * and may contain alphabetic and numeric characters
		 * resp. the underscore character
		 */
		while (iswalnum(*curr_p) || *curr_p == L'_') curr_p++;
		ls = curr_p - start_p;
		token_ident = alloc(sizeof (wchar_t) * (ls + 1));
		memcpy(token_ident, start_p, sizeof (wchar_t) * ls);
		token_ident[ls] = L'\0';
		/*
		 * token is an identifier
		 */
		token = 'i';
	} else if (*curr_p == L'=') {
		/*
		 * token is a equal sign
		 */
		token = '=';
		curr_p++;
	} else if (*curr_p == L',') {
		/*
		 * token is a comma
		 */
		token = ',';
		curr_p++;
	} else if (*curr_p == L'\"') {
		/*
		 * strings are enclosed in double quotes and may contain
		 * graphical characters and escapes \", \', and \\
		 */
		lb = 0;
		ls = 0;
		curr_p++;
		for (;;) {
			if (*curr_p == L'\0') {
				perr("%s(%d): unterminated string", cfn, ln);
				free_token();
				break;
			}
			if (*curr_p == L'"') {
				curr_p++;
				token_string = resize(token_string,
				    (ls + 1) * sizeof (wchar_t));
				token_string[ls] = L'\0';
				token = 's';
				break;
			}
			if (*curr_p == L'\\') {
				curr_p++;
				if (*curr_p != L'\"' && *curr_p != L'\\'
				    && *curr_p != L'\'') {
				    	pinvalid("escape sequence");
					free_token();
					break;
				}
			}
			if (ls == lb) {
				token_string = resize(token_string,
				    (lb + 16) * sizeof (wchar_t));
				lb += 16;
			}
			token_string[ls++] = *curr_p++;
		}
	} else {
		pinvalid("token");
	}
}


/*
 * translate drive letters 'a' ... 'p' to drive numbers 0 ... 15
 */
static int
cpm_drive(wchar_t c) {
	int rc = (-1);
	wchar_t *cp;
	static const wchar_t drives[16] = L"abcdefghijklmnop";
	cp = wcschr(drives, towlower(c));
	if (cp) rc = cp - drives;
	return rc;
}


/*
 * common syntax checks: is the next token a equal sign, a string,
 * a keyword, or a number?
 */

#define CHECK(NAME, TOKEN, MESSAGE) \
static int \
check_##NAME(int *rc_p) { \
	if (token != TOKEN) { \
		pexpected(MESSAGE); \
		*rc_p = (-1); \
		return 0; \
	} \
	return 1; \
}

CHECK(equal, '=', "=")
CHECK(string, 's', "string")
CHECK(keyword, 'i', "keyword")
CHECK(number, '0', "number")


/*
 * string in character definition may contain only a single character
 */
static int
check_char(int *rc_p) {
	if (! check_string(rc_p)) return 0;
	if (wcslen(token_string) > 1) {
		perr("%s(%d): string may contain only one character", cfn, ln);
		*rc_p = (-1);
		return 0;
	}
	return 1;
}


/*
 * set all undefined character positions of the primary or
 * secondary character set from the given built-in character set
 * (the built-in character sets have gaps as well)
 */
static void
set_charset(enum charset cs, wchar_t *charset[256]) {
	int i;
	size_t l;
	for (i = 0; i < 256; i++) {
		if (charset[i]) continue;
		if (! default_charset[i][cs]) continue;
		l = (wcslen(default_charset[i][cs]) + 1) * sizeof (wchar_t);
		charset[i] = alloc(l);
		memcpy(charset[i], default_charset[i][cs], l);
	}
}


/*
 * parse a screen dimension definition; the dimension may be either "current"
 * (resulting in (-1) being returned as value) or in the range defined
 * by the parameters min and max
 */
static int
parse_dim(const char *s, int min, int max, int *lines_p) {
	int rc = 0;
	get_token();
	if (! check_equal(&rc)) goto premature_exit;
	get_token();
	if (token == 'i' && ! wcscmp(token_ident, L"current")) {
		*lines_p = (-1);
	} else {
		if (! check_number(&rc)) goto premature_exit;
		if (token_ul < min || token_ul > max) {
			perr("%s(%d): %s number out of range (%d..%d)",
			    cfn, ln, s, min, max);
			rc = (-1);
			goto premature_exit;
		}
		*lines_p = (int) token_ul;
	}
	get_token();
premature_exit:
	return rc;
}


/*
 * parse a boolean definition: valid values are the
 * identifiers true or false
 */
static int
parse_boolean(int *value_p) {
	int rc = 0;
	get_token();
	if (! check_equal(&rc)) goto premature_exit;
	get_token();
	if (token == 'i' && ! wcscmp(token_ident, L"true")) {
		*value_p = 1;
	} else if (token == 'i' && ! wcscmp(token_ident, L"false")) {
		*value_p = 0;
	} else {
		perr("%s(%d): boolean value expected", cfn, ln);
		rc = (-1);
		goto premature_exit;
	}
	get_token();
premature_exit:
	return rc;
}


/*
 * convert a wide character string from the configuration file to
 * a multi-byte string
 */
static int
unix_path(const wchar_t *wcs, char **pp) {
	int rc = 0;
	size_t l;
	char *cp;
	l = wcslen(wcs) * MB_LEN_MAX + 1;
	cp = alloc(l);
	l = wcstombs(cp, wcs, l);
	if (l == (size_t) (-1)) {
		free(cp);
		cp = NULL;
		rc = (-1);
	} else {
		cp = resize(cp, l + 1);
	}
	*pp = cp;
	return rc;
}


/*
 * parse the configuration for one of the three CP/M character
 * devices LST, PUN, or RDR; all three can have both a path
 * and a mode (raw or translated)
 */
static int
parse_aux(const char *s, char **name_p, int *raw_p) {
	int rc = 0;
	get_token();
	if (! check_keyword(&rc)) goto premature_exit;
	if (! wcscmp(token_ident, L"file")) {
		/*
		 * path definition of the corresponding Unix file
		 */
		if (*name_p) {
			perr("%s(%d): %s file redefined", cfn, ln, s);
			rc = (-1);
			goto premature_exit;
		}
		get_token();
		if (! check_equal(&rc)) goto premature_exit;
		get_token();
		if (! check_string(&rc)) goto premature_exit;
		rc = unix_path(token_string, name_p);
		if (rc) {
			pinvalid("file name");
			goto premature_exit;
		}
		get_token();
	} else if (! wcscmp(token_ident, L"mode")) {
		/*
		 * the mode definition is one of the identifiers raw or text
		 */
		if (*raw_p != (-1)) {
			perr("%s(%d): %s mode redefined", cfn, ln, s);
			rc = (-1);
			goto premature_exit;
		}
		get_token();
		if (! check_equal(&rc)) goto premature_exit;
		get_token();
		if (! check_keyword(&rc)) goto premature_exit;
		if (! wcscmp(token_ident, L"text")) {
			*raw_p = 0;
		} else if (! wcscmp(token_ident, L"raw")) {
			*raw_p = 1;
		} else {
			pexpected("text or raw");
			rc = (-1);
			goto premature_exit;
		}
		get_token();
	} else {
		pexpected("file or mode");
		rc = (-1);
		goto premature_exit;
	}
premature_exit:
	return rc;
}


/*
 * parse dump options form configuration file
 */
static int
parse_dump(enum dump *dp) {
	int rc = 0;
	get_token();
	if (! check_equal(&rc)) goto premature_exit;
	if (*dp) {
	    	perr("%s(%d): dump options redefined", cfn, ln);
		rc = (-1);
		goto premature_exit;
	}
	do {
		get_token();
		if (token != 'i') {
			pexpected("dump option");
			rc = (-1);
			goto premature_exit;
		}
		if (! wcscmp(token_ident, L"all")) {
			*dp |= DUMP_ALL;
		} else if (! wcscmp(token_ident, L"none")) {
			*dp |= DUMP_NONE;
		} else if (! wcscmp(token_ident, L"startup")) {
			*dp |= DUMP_STARTUP;
		} else if (! wcscmp(token_ident, L"signal")) {
			*dp |= DUMP_SIGNAL;
		} else if (! wcscmp(token_ident, L"exit")) {
			*dp |= DUMP_EXIT;
		} else if (! wcscmp(token_ident, L"error")) {
			*dp |= DUMP_ERROR;
		} else {
			pexpected("dump option");
			rc = (-1);
			goto premature_exit;
		}
		get_token();
	} while (token == ',');
	/*
	 * check for illegal combinations
	 */
	if (((*dp & DUMP_ALL) && (*dp & ~DUMP_ALL)) ||
	    ((*dp & DUMP_NONE) && (*dp & ~DUMP_NONE)) ||
	    ((*dp & DUMP_ERROR) && (*dp & DUMP_EXIT))) {
	    	perr("%s(%d): illegal dump option combination", cfn, ln);
		rc = (-1);
		goto premature_exit;
	}
	/*
	 * "all" is a macro
	 */
	if (*dp & DUMP_ALL) *dp |= DUMP_STARTUP | DUMP_EXIT | DUMP_SIGNAL;
premature_exit:
	return rc;
}


/*
 * read parameters from the configuration file; parameters already
 * defined on the command line take precedence
 */
int
parse_config(void) {
	int rc = 0, alt, n, drive_no, temp_altkeys = (-1),
	    temp_dont_close = (-1), temp_interactive = (-1),
	    temp_screen_delay = (-1), temp_default_drive = (-1),
	    temp_reverse_bs_del = (-1), temp_delay_count = (-1),
	    temp_delay_nanoseconds = (-1);
	enum dump temp_dump = 0;
	wchar_t line[L_LINE];
	size_t l;
	enum charset default_cs[2] = { CS_NONE, CS_NONE };
	wchar_t **cs;
	enum log_level temp_log_level = LL_UNSET;
	/*
	 * get wctype("blank") once
	 */
	wctype_blank = wctype("blank");
	/*
	 * parse the file
	 */
	for (;;) {
		/*
		 * get next line from configuration file and initialize
		 * the token scanner
		 */
		ln++;
		if (! fgetws(line, L_LINE, cf)) {
			if (ferror(cf)) {
				perr("error reading %s: %s", cfn,
				    strerror(errno));
				rc = (-1);
				goto premature_exit;
			}
			break;
		}
		l = wcslen(line) - 1;
		if (line[l] != L'\n') {
			perr("%s(%d): line too long", cfn, ln);
			rc = (-1);
			goto premature_exit;
		}
		line[l] = L'\0';
		curr_p = line;
		/*
		 * get first token; skip empty lines and lines containing
		 * only comments
		 */
		get_token();
		if (! token) continue;
		/*
		 * if the first token is the identifier alt, it must be
		 * followed by another keyword
		 */
		alt = (token == 'i') && (! wcscmp(token_ident, L"alt"));
		if (alt) get_token();
		if (! check_keyword(&rc)) continue;
		if (! wcscmp(token_ident, L"charset")) {
			/*
			 * charset resp. alt charset: define a default for
			 * characters not explicitly defined in the
			 * configuration file
			 */
			if (default_cs[alt] != CS_NONE) {
				predefined(alt ? "alt charset" : "charset");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (token != 'i') {
				pexpected("charset name");
				rc = (-1);
				continue;
			}
			/*
			 * valid options are vt52, ascii, latin1, and tnylpo
			 */
			if (! wcscmp(token_ident, L"vt52")) {
				default_cs[alt] = CS_VT52;
			} else if (! wcscmp(token_ident, L"ascii")) {
				default_cs[alt] = CS_ASCII;
			} else if (! wcscmp(token_ident, L"latin1")) {
				default_cs[alt] = CS_LATIN1;
			} else if (! wcscmp(token_ident, L"tnylpo")) {
				default_cs[alt] = CS_TNYLPO;
			} else {
				pinvalid("charset name");
				rc = (-1);
				continue;
			}
			get_token();
		} else if (! wcscmp(token_ident, L"char")) {
			/*
			 * char resp. alt char: explicitly define
			 * a CP/M character
			 */
			get_token();
			if (token != '0' || token_ul > 256) {
				pexpected("number (0..255)");
				rc = (-1);
				continue;
			}
			n = (int) token_ul;
			cs = (alt ? conf_alt_charset : conf_charset) + n;
			if (*cs) {
				predefined(alt ? "alt char" : "char");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			/*
			 * parameter must be a string of length 1
			 */
			get_token();
			if (! check_char(&rc)) continue;
			*cs = token_string;
			token_string = NULL;
			get_token();
		} else if (alt) {
			/*
			 * all other keywords may not be prefixed by alt
			 */
			perr("%s(%d): keyword alt unexpected", cfn, ln);
			rc = (-1);
			continue;
		} else if (! wcscmp(token_ident, L"cpu")) {
			/*
			 * specify CPU delay: an instruction count
			 * and a number of nanoseconds, separated by a
			 * comma
			 */
			get_token();
			if (token != 'i' || wcscmp(token_ident, L"delay")) {
				pexpected("delay");
				rc = (-1);
				continue;
			}
			if (temp_delay_count != (-1)) {
				predefined("cpu delay");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (! check_number(&rc)) continue;
			if (token_ul < 1 || token_ul > INT_MAX) {
				perr("%s(%d): cpu delay count out of range",
				    cfn, ln);
				rc = (-1);
				continue;
			}
			temp_delay_count = (int) token_ul;
			get_token();
			if (token != ',') {
				pexpected(",");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_number(&rc)) continue;
			if (token_ul < 1 || token_ul > INT_MAX) {
				perr("%s(%d): cpu delay nanoseconds out "
				    "of range", cfn, ln);
				rc = (-1);
				continue;
			}
			temp_delay_nanoseconds = (int) token_ul;
			get_token();
		} else if (! wcscmp(token_ident, L"console")) {
			/*
			 * use emulated terminal (full) or line
			 * orientated/batch console (line)?
			 */
			if (temp_interactive != (-1)) {
				predefined("console");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (! check_keyword(&rc)) continue;
			if (! wcscmp(token_ident, L"full")) {
				temp_interactive = 1;
			} else if (! wcscmp(token_ident, L"line")) {
				temp_interactive = 0;
			} else {
				pexpected("full or line");
				rc = (-1);
				continue;
			}
			get_token();
		} else if (! wcscmp(token_ident, L"unprintable")) {
			/*
			 * character to be used to represent an
			 * undefined character written to the console
			 */
			if (conf_unprintable) {
				predefined("unprintable char");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (! check_char(&rc)) continue;
			conf_unprintable = token_string;
			token_string = NULL;
			get_token();
		} else if (! wcscmp(token_ident, L"close")) {
			/*
			 * close files determines whether BDOS function 19
			 * actually closes the corresponding Unix file
			 * or not
			 */
			get_token();
			if (token != 'i' || wcscmp(token_ident, L"files")) {
				pexpected("files");
				rc = (-1);
				continue;
			}
			if (temp_dont_close != (-1)) {
				predefined("close files");
				rc = (-1);
				continue;
			}
			if (parse_boolean(&temp_dont_close) == (-1)) {
				rc = (-1);
				continue;
			}
			temp_dont_close = ! temp_dont_close;

		} else if (! wcscmp(token_ident, L"screen")) {
			/*
			 * define a delay in seconds between program
			 * termination and resetting the terminal emulation
			 * to allow reading of the final screen contents
			 * (ncurses restores the previous screen contents
			 * on exit if possible); a value of key  waits for
			 * a keypress before resetting the terminal.
			 */
			get_token();
			if (token != 'i' || wcscmp(token_ident, L"delay")) {
				pexpected("delay");
				rc = (-1);
				continue;
			}
			if (temp_screen_delay != (-1)) {
				predefined("screen delay");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (token == 'i' && ! wcscmp(token_ident, L"key")) {
				temp_screen_delay = (-2);
			} else {
				if (! check_number(&rc)) continue;
				if (token_ul > INT_MAX) {
					perr("%s(%d): screen delay out of "
					    "range", cfn, ln);
					rc = (-1);
					continue;
				}
				temp_screen_delay = (int) token_ul;
			}
			get_token();
		} else if (! wcscmp(token_ident, L"application")) {
			/*
			 * application cursor determines whether the
			 * terminal emulation returns WordStar cursor
			 * movement codes instead of the VT52 cursor
			 * key sequences
			 */
			get_token();
			if (token != 'i' || wcscmp(token_ident, L"cursor")) {
				pexpected("cursor");
				rc = (-1);
				continue;
			}
			if (temp_dont_close != (-1)) {
				predefined("application cursor");
				rc = (-1);
				continue;
			}
			if (parse_boolean(&temp_altkeys) == (-1)) {
				rc = (-1);
				continue;
			}
		} else if (! wcscmp(token_ident, L"exchange")) {
			/*
			 * exchange delete exchanges delete and backspace
			 * keys in full screen mode
			 */
			get_token();
			if (token != 'i' || wcscmp(token_ident, L"delete")) {
				pexpected("delete");
				rc = (-1);
				continue;
			}
			if (temp_reverse_bs_del != (-1)) {
				predefined("exchange delete");
				rc = (-1);
				continue;
			}
			if (parse_boolean(&reverse_bs_del) == (-1)) {
				rc = (-1);
				continue;
			}
		} else if (! wcscmp(token_ident, L"default")) {
			/*
			 * defines the default drive
			 */
			get_token();
			if (token != 'i' || wcscmp(token_ident, L"drive")) {
				pexpected("drive");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (temp_default_drive != (-1)) {
				predefined("default drive");
				rc = (-1);
				goto premature_exit;
			}
			if (token != 'i' || wcslen(token_ident) != 1) {
				temp_default_drive = (-1);
			} else {
				temp_default_drive = cpm_drive(token_ident[0]);
			}
			if (temp_default_drive == (-1)) {
				pinvalid("drive name");
				rc = (-1);
				continue;
			}
			get_token();
		} else if (! wcscmp(token_ident, L"drive")) {
			/*
			 * defines one of the 16 CP/M disk drives A...P;
			 * the drive name is a identifier a, b, c, ..., p;
			 * the parameter value is a string containing a
			 * Unix path, optionally preceded by the
			 * identifier readonly and a comma
			 */
			get_token();
			if (token != 'i' || wcslen(token_ident) != 1) {
				drive_no = (-1);
			} else {
				drive_no = cpm_drive(token_ident[0]);
			}
			if (drive_no == (-1)) {
				pinvalid("drive name");
				rc = (-1);
				continue;
			}
			if (conf_drives[drive_no]) {
				predefined("drive");
				rc = (-1);
				continue;
			}
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (token == 'i') {
				/*
				 * read only?
				 */
				if (wcscmp(token_ident, L"readonly")) {
					pexpected("string");
					rc = (-1);
					continue;
				}
				conf_readonly[drive_no] = 1;
				get_token();
				if (token != ',') {
					pexpected(",");
					rc = (-1);
					continue;
				}
				get_token();
			}
			if (! check_string(&rc)) continue;
			if (unix_path(token_string, conf_drives + drive_no)) {
				pinvalid("file name");
				rc = (-1);
				continue;
			}
			get_token();
		} else if (! wcscmp(token_ident, L"logfile")) {
			/*
			 * the parameter of logfile is a string
			 * containing a Unix path
			 */
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (! check_string(&rc)) continue;
			if (unix_path(token_string, &conf_log)) {
				pinvalid("file name");
				rc = (-1);
				continue;
			}
			get_token();
		} else if (! wcscmp(token_ident, L"loglevel")) {
			/*
			 * the parameter of loglevel is a number
			 */
			get_token();
			if (! check_equal(&rc)) continue;
			get_token();
			if (! check_number(&rc)) continue;
			if (token_ul >= LL_INVALID) {
				perr("%s(%d): log level out of range",
				    cfn, ln);
				rc = (-1);
				continue;
			}
			if (temp_log_level != LL_UNSET) {
				predefined("log level");
				rc = (-1);
				continue;
			}
			temp_log_level = (enum log_level) token_ul;
			get_token();
		} else if (! wcscmp(token_ident, L"lines")) {
			/*
			 * number of lines used by the VT52 emulation
			 */
			if (parse_dim("line", MIN_LINES, MAX_LINES, &n)) {
				rc = (-1);
				continue;
			}
			if (! lines) lines = n;
		} else if (! wcscmp(token_ident, L"columns")) {
			/*
			 * number of columns used by the VT52 emulation
			 */
			if (parse_dim("column", MIN_COLS, MAX_COLS, &n)) {
				rc = (-1);
				continue;
			}
			if (! cols) cols = n;
		} else if (! wcscmp(token_ident, L"printer")) {
			/*
			 * printer file definition
			 */
			if (parse_aux("printer", &conf_printer,
			    &conf_printer_raw)) {
			    	rc = (-1);
				continue;
			}
		} else if (! wcscmp(token_ident, L"punch")) {
			/*
			 * punch file definition
			 */
			if (parse_aux("punch", &conf_punch,
			    &conf_punch_raw)) {
			    	rc = (-1);
				continue;
			}
		} else if (! wcscmp(token_ident, L"reader")) {
			/*
			 * reader file definition
			 */
			if (parse_aux("reader", &conf_reader,
			    &conf_reader_raw)) {
			    	rc = (-1);
				continue;
			}
		} else if (! wcscmp(token_ident, L"dump")) {
			if (parse_dump(&temp_dump)) {
				rc = (-1);
				continue;
			}
		}
		if (token) {
			perr("%s(%d): syntax error", cfn, ln);
			rc = (-1);
		}
	}
	/*
	 * In case of a syntax or semantical error the parser skips
	 * to the next line in order to check as much as possible
	 * of the configuration file; if any errors occurred up to
	 * this point, further processing of the configuration
	 * is skipped.
	 */
	if (rc) goto premature_exit;
	/*
	 * take values from the configuration file only if
	 * they have not already been defined on the command line
	 */
	if (log_level == LL_UNSET) log_level = temp_log_level;
	if (dont_close == (-1)) dont_close = temp_dont_close;
	if (altkeys == (-1)) altkeys = temp_altkeys;
	if (reverse_bs_del == (-1)) reverse_bs_del = temp_reverse_bs_del;
	if (screen_delay == (-1)) screen_delay = temp_screen_delay;
	if (conf_interactive == (-1)) conf_interactive = temp_interactive;
	if (default_drive == (-1)) default_drive = temp_default_drive;
	if (conf_dump == 0) conf_dump = temp_dump;
	if (delay_count == (-1)) {
		delay_count = temp_delay_count;
		delay_nanoseconds = temp_delay_nanoseconds;
	}
	/*
	 * characters and character sets cannot be specified on
	 * the command line
	 */
	if (default_cs[0] != CS_NONE) {
		set_charset(default_cs[0], conf_charset);
	} else {
		set_charset(CS_VT52, conf_charset);
	}
	if (default_cs[1] != CS_NONE) {
		set_charset(default_cs[1], conf_alt_charset);
	} else {
		set_charset(CS_VT52, conf_alt_charset);
	}
premature_exit:
	free_token();
	return rc;
}


/*
 * read the optional configuration file
 */
int read_config(char *fn) {
	int rc = 0;
	const char *home;
	/*
	 * handle configuration file
	 */
	if (fn) {
		/*
		 * configuration file given on the command line
		 */
		/*
		 * make configuration file name available to subprograms
		 */
		cfn = fn;
		cf = fopen(cfn, "r");
		if (! cf) {
			perr("cannot open %s: %s", cfn, strerror(errno));
			rc = (-1);
			goto premature_exit;
		}
	} else {
		/*
		 * take the configuraton file in the current working directory,
		 * if one exists
		 */
		cfn = "./.tnylpo.conf";
		cf = fopen(cfn, "r");
		if (! cf) {
			/*
			 * otherwise, use the configuration file from the
			 * user's home directory, if present
			 */
			home = getenv("HOME");
			if (home) {
				cfn = alloc(strlen(home) + 14);
				sprintf(cfn, "%s/.tnylpo.conf",home);
				cf = fopen(cfn, "r");
			}
		}
	}
	/*
	 * Even if there is no configuration file, this program
	 * assumes a minimalistic working configuration: one CP/M
	 * drive (A; the current Unix working directory); VT52
	 * character set; actually close files; screen size 80x24; use
	 * VT52 cursor keys (the last two assumptions only have
	 * an effect if the user passes the -s option on the command
	 * line, since the console is line orientated by default).
	 */
	if (cf) {
		rc = parse_config();
		if (rc) goto premature_exit;
	} else {
		/*
		 * without a config file, assume the VT52 character set
		 * both as primary and as secondary character set
		 */
		set_charset(CS_VT52, conf_charset);
		set_charset(CS_VT52, conf_alt_charset);
	}
premature_exit:
	if (cf) {
		if (fclose(cf)) {
			perr("cannot close %s: %s", cfn, strerror(errno));
			rc = (-1);
		}
	}
	return rc;
}
