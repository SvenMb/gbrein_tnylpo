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


/*
 * This is a simple text-based implementation of a Minesweeper-like
 * game as a demonstration of tnylpo; it expects tnylpo's VT52
 * emulation and the tnylpo character set.
 *
 * This program was developed using the Hitech C Compiler for CP/M;
 * Compile it with the command "c -o mine.c", which should produce
 " the executable file "mine.com".
 *
 * Execute it with the command "tnylpo -f mine.conf mine".
 */

/*
 * BIOS functions
 */
extern int bios(int, ...);

#define WBOOT 1
#define CONST 2
#define CONIN 3
#define CONOUT 4


/*
 * display size
 */
#define X_SIZE 80
#define Y_SIZE 24


/*
 * key codes
 */
#define KEY_CTRLD 0x04
#define KEY_CTRLE 0x05
#define KEY_TAB 0x09
#define KEY_CR 0x0d
#define KEY_CTRLS 0x13
#define KEY_CTRLX 0x18
#define KEY_ESC 0x1b


/*
 * representation of a mine, a flag, and a falsely flagged square in
 * the screen
 */
#define MINE '\xa4'
#define FLAG 'F'
#define WRONG '*'


/*
 * levels of the game; the total number of squares (x * y) must be
 * relative prime to 17, y may not be larger than Y_SIZE - 4, x may not
 * be larger then XSIZE - 2
 */
static struct {
	char x, y;
	unsigned char mines;
} levels[5] = {
	{ 10, 10, 20 },
	{ 20, 10, 40 },
	{ 30, 15, 90 },
	{ 40, 15, 120 },
	{ 60, 20, 240 }
};


/*
 * global variables
 */
/*
 * seed of the pseudo random number generator
 */
static unsigned long random_seed = 0;
/*
 * current level of the game: board dimensions, number of mines
 */
static char x_size, y_size;
static unsigned char nr_mines;
/*
 * offsets of the board on screen
 */
static char x_offset = 0, y_offset = 0;
/*
 * termination flag: mine hit
 */
static char mine_hit;
/*
 * number of remaining unchecked fields
 */
static int unchecked;
/*
 * maximal legal index of board
 */
static int l_field;
/*
 * board flags
 */
#define M_MINE 0x80
#define M_FLAG 0x40
#define M_OPEN 0x20
#define M_NUMBER 0x0f
static unsigned char field[(Y_SIZE - 4) * (X_SIZE - 2)];


/*
 * macros describing the lines above and below the framed board
 */
#define ABOVE (y_offset - 2)
#define BELOW (y_offset + y_size + 1)


/*
 * return a pseudo random number in the range [0..32768]
 */
static int
rand(void) {
	random_seed  = random_seed * 1103515245 + 12345;
	return (int) ((random_seed >> 16) & 0x7fff);
}


/*
 * print a single character on the console
 */
static void
pchar(char c) { bios(CONOUT, c); }


/*
 * print a null terminated string on the console
 */
static void
pstr(char *cp) { while (*cp) bios(CONOUT, *cp++); }


/*
 * display a unsigned byte as decimal number on the console
 */
static void
pubyte(unsigned char uc) {
	pchar(uc / 100 + '0');
	pchar(uc % 100 / 10 + '0');
	pchar(uc % 10 + '0');
}


/*
 * turn on bold reverse video on and off
 */

static void
br_on(void) { pstr("\33e\33i"); }

static void
br_off(void) { pstr("\33j\33f"); }


/*
 * turn bold video on and off
 */

static void
b_on(void) { pstr("\33e"); }

static void
b_off(void) { pstr("\33f"); }


/*
 * turn graphic characters on and off
 */

static void
graph_on(void) { pstr("\33F"); }

static
void graph_off(void) { pstr("\33G"); }


/*
 * clear screen
 */
static void
clear(void) { pstr("\33E"); }


/*
 * clear to end of line
 */
static void
clear_eoln(void) { pstr("\33K"); }


/*
 * read a character from the console
 */
static char
gchar(void) { return bios(CONIN); }


/*
 * position cursor to (x, y)
 */
static void
goto_xy(char x, char y) {
	pstr("\33Y");
	pchar(y + ' ');
	pchar(x + ' ');
}


/*
 * display x_size characters c on the console
 */
static void
hline(char c) {
	char i;
	for (i = 0; i < x_size; i++) pchar(c);
}


/*
 * check if the square (x, y) contains a mine
 */
static unsigned char
is_mine(char x, char y) {
	if (x < 0 || x > x_size - 1 || y < 0 || y > y_size - 1) return 0;
	return((field[x_size * y + x] & M_MINE) == M_MINE);
}


/*
 * show the square (x, y); square must not contain a mine; if square has
 * no neighbouring mines, recursively open all adjacent unflagged squares
 */
static void
open_field(char x, char y) {
	int i;
	char n;
	/*
	 * skip square out of range
	 */
	if (x < 0 || x > x_size - 1 || y < 0 || y > y_size - 1) return;
	/*
	 * skip flagged or already opened square
	 */
	i = x_size * y + x;
	if (field[i] & (M_OPEN | M_FLAG)) return;
	/*
	 * mark square as opened, decrease number of unopened squares
	 */
	field[i] |= M_OPEN;
	unchecked--;
	/*
	 * get and display number of neighbouring mines
	 */
	n = field[i] & M_NUMBER;
	goto_xy(x + x_offset, y + y_offset);
	if (n) {
		pchar(n + '0');
		return;
	}
	/*
	 * if the square has no neighbouring mines, try to open all neighbours
	 */
	pchar(' ');
	open_field(x - 1, y - 1);
	open_field(x, y - 1);
	open_field(x + 1, y - 1);
	open_field(x - 1, y);
	open_field(x + 1, y);
	open_field(x - 1, y + 1);
	open_field(x, y + 1);
	open_field(x + 1, y + 1);
}


/*
 * test square (x, y); if there is a mine, the game is lost, if not,
 * the square is opened
 */
static void
touch_field(char x, char y) {
	int i;
	/*
	 * skip square out of range
	 */
	if (x < 0 || x > x_size - 1 || y < 0 || y > y_size - 1) return;
	i = x_size * y + x;
	/*
	 * skip opened or flagges square
	 */
	if (field[i] & (M_OPEN | M_FLAG)) return;
	/*
	 * if there is a mine, it explodes and the game is lost
	 */
	if (field[i] & M_MINE) {
		mine_hit = 1;
		return;
	}
	/*
	 * otherwise, open the square (and potentially, its neighbours
	 */
	open_field(x, y);
}


/*
 * play a game of Mine Disposal
 */
static void
play(void) {
	/*
	 * message strings
	 */
	static char you_win[] = "You win!";
	static char you_lose[] = "You lose!";
	static char mines[] = "Mines:";
	char x, y, key, mines_x;
	unsigned char n, mines_left, old_left;
	int i;
	/*
	 * number of mines not flagged
	 */
	mines_left = old_left = nr_mines;
	/*
	 * clear the screen and paint the board
	 */
	clear();
	goto_xy(x_offset - 1, y_offset - 1);
	graph_on();
	pchar('l');
	hline('q');
	pchar('k');
	for (y = 0; y < y_size; y++) {
		goto_xy(x_offset - 1, y_offset + y);
		pchar('x');
		br_on();
		hline(' ');
		br_off();
		pchar('x');
	}
	goto_xy(x_offset - 1, y_offset + y_size);
	pchar('m');
	hline('q');
	pchar('j');
	graph_off();
	/*
	 * display number of unflagged mines
	 */
	mines_x = (X_SIZE - sizeof mines - 2) / 2;
	goto_xy(mines_x, ABOVE);
	pstr(mines);
	mines_x += sizeof mines;
	goto_xy(mines_x, ABOVE);
	pubyte(mines_left);
	/*
	 * initialize the board
	 */
	for (i = 0; i < l_field; i++) field[i] = 0;
	/*
	 * randomly set the mines
	 */
	for (n = 0; n < nr_mines; n++) {
		i = rand() % l_field;
		while (field[i] & M_MINE) {
			i += 17;
			if (i >= l_field) i -= l_field;
		}
		field[i] |= M_MINE;
	}
	/*
	 * calculate the number of neighbouring mines for all other squares
	 */
	for (y = 0; y < y_size; y++) {
		for (x = 0; x < x_size; x++) {
			i = x_size * y + x;
			if (field[i] & M_MINE) continue;
			n = is_mine(x - 1, y - 1);
			n += is_mine(x, y - 1);
			n += is_mine(x + 1, y - 1);
			n += is_mine(x - 1, y);
			n += is_mine(x + 1, y);
			n += is_mine(x - 1, y + 1);
			n += is_mine(x, y + 1);
			n += is_mine(x + 1, y + 1);
			field[i] = n;
		}
	}
	/*
	 * main loop is performed until all unmined squares have been opened
	 * or a mine has been hit
	 */
	x = y = 0;
	unchecked = l_field - nr_mines;
	mine_hit = 0;
	while (unchecked && ! mine_hit) {
		/*
		 * display number of unflagged mines, if it has changed
		 */
		if (mines_left != old_left) {
			goto_xy(mines_x, ABOVE);
			clear_eoln();
			pubyte(mines_left);
			old_left = mines_left;
		}
		/*
		 * get a character from the current position;
		 * if it is escape, it is a cursor/function key
		 */
		goto_xy(x + x_offset, y + y_offset);
		key = gchar();
		if (key == KEY_ESC) {
			key = gchar();
			switch (key) {
			case 'A': key = KEY_CTRLE; break;
			case 'B': key = KEY_CTRLX; break;
			case 'C': key = KEY_CTRLD; break;
			case 'D': key = KEY_CTRLS; break;
			case 'P': key = ' '; break;
			case 'Q': key = KEY_TAB; break;
			case 'R': key = KEY_CR; break;
			default: continue;
			}
		}
		switch (key) {
		case KEY_CTRLE:
			/*
			 * cursor up
			 */
			if (y > 0) y--;
			continue;
		case KEY_CTRLX:
			/*
			 * cursor down
			 */
			if (y < y_size - 1) y++;
			continue;
		case KEY_CTRLS:
			/*
			 * cursor left
			 */
			if (x > 0) x--;
			continue;
		case KEY_CTRLD:
			/*
			 * cursor right
			 */
			if (x < x_size - 1) x++;
			continue;
		case KEY_CR:
			/*
			 * set/reset flag
			 */
			i = x_size * y + x;
			if (field[i] & M_OPEN) continue;
			br_on();
			if (field[i] & M_FLAG) {
				field[i] &= ~M_FLAG;
				pchar(' ');
				mines_left++;
			} else {
				field[i] |= M_FLAG;
				pchar(FLAG);
				mines_left--;
			}
			br_off();
			continue;
		case KEY_TAB:
			/*
			 * hit all adjacent squares, unless flagged
			 */
			i = x_size * y + x;
			if (! (field[i] & M_OPEN)) continue;
			if (! field[i]) continue;
			touch_field(x - 1, y - 1);
			touch_field(x, y - 1);
			touch_field(x + 1, y - 1);
			touch_field(x - 1, y);
			touch_field(x + 1, y);
			touch_field(x - 1, y + 1);
			touch_field(x, y + 1);
			touch_field(x + 1, y + 1);
			continue;
		case ' ':
			/*
			 * hit field
			 */
			touch_field(x, y);
			continue;
		}
	}
	/*
	 * display win or lose message
	 */
	goto_xy(0, ABOVE);
	clear_eoln();
	goto_xy((X_SIZE - sizeof you_win + 1) / 2, ABOVE);
	b_on();
	pstr(mine_hit ? you_lose : you_win);
	b_off();
	/*
	 * show all falsely flagged squares or unflagged mines
	 */
	i = 0;
	for (y = 0; y < y_size; y++) {
		for (x = 0; x < x_size; x++) {
			if (field[i] & M_FLAG) {
				if (! (field[i] & M_MINE)) {
					goto_xy(x + x_offset, y + y_offset);
					br_on();
					pchar(WRONG);
					br_off();
				}
			} else if (field[i] & M_MINE) {
				/*
				 * in case of loss, show all undiscovered
				 * mines; in case of a win (i. e., if all
				 * remaining unopened fields contain mines,
				 " show them as flagged
				 */
				goto_xy(x + x_offset, y + y_offset);
				br_on();
				pchar(mine_hit ? MINE : FLAG);
				br_off();
			}
			i++;
		}
	}
}


void
main(void) {
	/*
	 * message string
	 */
	static char again[] = "Play again (y/n): ";
	char key, level;
	/*
	 * clear screen and show title
	 */
	clear();
	b_on();
	pstr("Mine Disposal");
	b_off();
	/*
	 * query level; the time to the first keypress of the user's answer
	 * is used to initialize the pseudo random number generator
	 */
	pstr("\r\n\nEnter level (1-5): ");
	while (! bios(CONST)) random_seed++;
	do {
		key = gchar();
	} while (key < '1' || key > '5');
	pchar(key);
	level = key - '1';
	/*
	 * get parameters of selected level
	 */
	x_size = levels[level].x;
	y_size = levels[level].y;
	nr_mines = levels[level].mines;
	/*
	 * calculate board limit
	 */
	l_field = (int) x_size * (int) y_size;
	/*
	 * calculate board offset on screen
	 */
	x_offset = (X_SIZE - x_size) / 2;
	y_offset = (Y_SIZE - y_size) / 2;
	/*
	 * repeat game until user doesn't want to play again
	 */
	do {
		play();
		goto_xy((X_SIZE - sizeof again) / 2, BELOW);
		pstr(again);
		do {
			key = gchar();
		} while (key != 'N' && key != 'n' && key != 'y' && key != 'Y');
		pchar(key);
	} while (key == 'Y' || key == 'y');
	/*
	 * exit game
	 */
	goto_xy(0, Y_SIZE - 1);
	bios(WBOOT);
}
