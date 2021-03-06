/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_text/text_draw.c
 *  \ingroup sptext
 */


#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "MEM_guardedalloc.h"

#include "BLF_api.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "DNA_text_types.h"
#include "DNA_space_types.h"
#include "DNA_screen_types.h"
#include "DNA_userdef_types.h"

#include "BKE_context.h"
#include "BKE_suggestions.h"
#include "BKE_text.h"

#include "GPU_colors.h"
#include "GPU_primitives.h"
#include "GPU_utility.h"

#include "BIF_glutil.h"

#include "ED_datafiles.h"
#include "UI_interface.h"
#include "UI_resources.h"

#include "text_intern.h"

/******************** text font drawing ******************/
// XXX, fixme
#define mono blf_mono_font

static void text_font_begin(SpaceText *st)
{
	BLF_size(mono, st->lheight, 72);
}

static void text_font_end(SpaceText *UNUSED(st))
{
}

static int text_font_draw(SpaceText *UNUSED(st), int x, int y, const char *str)
{
	BLF_position(mono, x, y, 0);
	BLF_draw(mono, str, BLF_DRAW_STR_DUMMY_MAX);

	return BLF_width(mono, str);
}

static int text_font_draw_character(SpaceText *st, int x, int y, char c)
{
	char str[2];
	str[0] = c;
	str[1] = '\0';

	BLF_position(mono, x, y, 0);
	BLF_draw(mono, str, 1);

	return st->cwidth;
}


/****************** flatten string **********************/

static void flatten_string_append(FlattenString *fs, const char *c, int accum, int len) 
{
	int i;
	
	if (fs->pos + len > fs->len) {
		char *nbuf; int *naccum;
		fs->len *= 2;

		nbuf = MEM_callocN(sizeof(*fs->buf) * fs->len, "fs->buf");
		naccum = MEM_callocN(sizeof(*fs->accum) * fs->len, "fs->accum");

		memcpy(nbuf, fs->buf, fs->pos * sizeof(*fs->buf));
		memcpy(naccum, fs->accum, fs->pos * sizeof(*fs->accum));
		
		if (fs->buf != fs->fixedbuf) {
			MEM_freeN(fs->buf);
			MEM_freeN(fs->accum);
		}
		
		fs->buf = nbuf;
		fs->accum = naccum;
	}
	
	for (i = 0; i < len; i++) {
		fs->buf[fs->pos + i] = c[i];
		fs->accum[fs->pos + i] = accum;
	}

	fs->pos += len;
}

int flatten_string(SpaceText *st, FlattenString *fs, const char *in)
{
	int r, i, total = 0;

	memset(fs, 0, sizeof(FlattenString));
	fs->buf = fs->fixedbuf;
	fs->accum = fs->fixedaccum;
	fs->len = sizeof(fs->fixedbuf);

	i = 0;
	for (r = 0; *in; r++) {
		if (*in == '\t') {
			i = st->tabnumber - (total % st->tabnumber);
			total += i;

			while (i--)
				flatten_string_append(fs, " ", r, 1);

			in++;
		}
		else {
			size_t len = BLI_str_utf8_size_safe(in);
			flatten_string_append(fs, in, r, len);
			in += len;
			total++;
		}
	}

	flatten_string_append(fs, "\0", r, 1);

	return total;
}

void flatten_string_free(FlattenString *fs)
{
	if (fs->buf != fs->fixedbuf)
		MEM_freeN(fs->buf);
	if (fs->accum != fs->fixedaccum)
		MEM_freeN(fs->accum);
}

/* Checks the specified source string for a Python built-in function name. This
 * name must start at the beginning of the source string and must be followed by
 * a non-identifier (see text_check_identifier(char)) or null character.
 * 
 * If a built-in function is found, the length of the matching name is returned.
 * Otherwise, -1 is returned.
 *
 * See:
 * http://docs.python.org/py3k/reference/lexical_analysis.html#keywords
 */

static int find_builtinfunc(char *string)
{
	int a, i;
	const char *builtinfuncs[] = {
		/* "False", "None", "True", */ /* see find_bool() */
		"and", "as", "assert", "break",
		"class", "continue", "def", "del", "elif", "else", "except",
		"finally", "for", "from", "global", "if", "import", "in",
		"is", "lambda", "nonlocal", "not", "or", "pass", "raise",
		"return", "try", "while", "with", "yield",
	};

	for (a = 0; a < sizeof(builtinfuncs) / sizeof(builtinfuncs[0]); a++) {
		i = 0;
		while (1) {
			/* If we hit the end of a keyword... (eg. "def") */
			if (builtinfuncs[a][i] == '\0') {
				/* If we still have identifier chars in the source (eg. "definate") */
				if (text_check_identifier(string[i]))
					i = -1;  /* No match */
				break; /* Next keyword if no match, otherwise we're done */
				
				/* If chars mismatch, move on to next keyword */
			}
			else if (string[i] != builtinfuncs[a][i]) {
				i = -1;
				break; /* Break inner loop, start next keyword */
			}
			i++;
		}
		if (i > 0) break;  /* If we have a match, we're done */
	}
	return i;
}

/* Checks the specified source string for a Python special name. This name must
 * start at the beginning of the source string and must be followed by a non-
 * identifier (see text_check_identifier(char)) or null character.
 * 
 * If a special name is found, the length of the matching name is returned.
 * Otherwise, -1 is returned. */

static int find_specialvar(char *string) 
{
	int i = 0;
	/* Check for "def" */
	if (string[0] == 'd' && string[1] == 'e' && string[2] == 'f')
		i = 3;
	/* Check for "class" */
	else if (string[0] == 'c' && string[1] == 'l' && string[2] == 'a' && string[3] == 's' && string[4] == 's')
		i = 5;
	/* If next source char is an identifier (eg. 'i' in "definate") no match */
	if (i == 0 || text_check_identifier(string[i]))
		return -1;
	return i;
}

static int find_decorator(char *string) 
{
	if (string[0] == '@') {
		int i = 1;
		while (text_check_identifier(string[i])) {
			i++;
		}
		return i;
	}
	return -1;
}

static int find_bool(char *string) 
{
	int i = 0;
	/* Check for "False" */
	if (string[0] == 'F' && string[1] == 'a' && string[2] == 'l' && string[3] == 's' && string[4] == 'e')
		i = 5;
	/* Check for "True" */
	else if (string[0] == 'T' && string[1] == 'r' && string[2] == 'u' && string[3] == 'e')
		i = 4;
	/* Check for "None" */
	else if (string[0] == 'N' && string[1] == 'o' && string[2] == 'n' && string[3] == 'e')
		i = 4;
	/* If next source char is an identifier (eg. 'i' in "definate") no match */
	if (i == 0 || text_check_identifier(string[i]))
		return -1;
	return i;
}

/* Ensures the format string for the given line is long enough, reallocating
 * as needed. Allocation is done here, alone, to ensure consistency. */
static int text_check_format_len(TextLine *line, unsigned int len)
{
	if (line->format) {
		if (strlen(line->format) < len) {
			MEM_freeN(line->format);
			line->format = MEM_mallocN(len + 2, "SyntaxFormat");
			if (!line->format) return 0;
		}
	}
	else {
		line->format = MEM_mallocN(len + 2, "SyntaxFormat");
		if (!line->format) return 0;
	}

	return 1;
}

/* Formats the specified line. If do_next is set, the process will move on to
 * the succeeding line if it is affected (eg. multiline strings). Format strings
 * may contain any of the following characters:
 *  '_'  Whitespace
 *  '#'  Comment text
 *  '!'  Punctuation and other symbols
 *  'n'  Numerals
 *  'l'  String letters
 *  'v'  Special variables (class, def)
 *  'b'  Built-in names (print, for, etc.)
 *  'q'  Other text (identifiers, etc.)
 * It is terminated with a null-terminator '\0' followed by a continuation
 * flag indicating whether the line is part of a multi-line string. */

static void txt_format_line(SpaceText *st, TextLine *line, int do_next)
{
	FlattenString fs;
	char *str, *fmt, orig, cont, find, prev = ' ';
	int len, i;

	/* Get continuation from previous line */
	if (line->prev && line->prev->format != NULL) {
		fmt = line->prev->format;
		cont = fmt[strlen(fmt) + 1]; /* Just after the null-terminator */
	}
	else cont = 0;

	/* Get original continuation from this line */
	if (line->format != NULL) {
		fmt = line->format;
		orig = fmt[strlen(fmt) + 1]; /* Just after the null-terminator */
	}
	else orig = 0xFF;

	len = flatten_string(st, &fs, line->line);
	str = fs.buf;
	if (!text_check_format_len(line, len)) {
		flatten_string_free(&fs);
		return;
	}
	fmt = line->format;

	while (*str) {
		/* Handle escape sequences by skipping both \ and next char */
		if (*str == '\\') {
			*fmt = prev; fmt++; str++;
			if (*str == '\0') break;
			*fmt = prev; fmt++; str += BLI_str_utf8_size_safe(str);
			continue;
		}
		/* Handle continuations */
		else if (cont) {
			/* Triple strings ("""...""" or '''...''') */
			if (cont & TXT_TRISTR) {
				find = (cont & TXT_DBLQUOTSTR) ? '"' : '\'';
				if (*str == find && *(str + 1) == find && *(str + 2) == find) {
					*fmt = 'l'; fmt++; str++;
					*fmt = 'l'; fmt++; str++;
					cont = 0;
				}
				/* Handle other strings */
			}
			else {
				find = (cont & TXT_DBLQUOTSTR) ? '"' : '\'';
				if (*str == find) cont = 0;
			}

			*fmt = 'l';
			str += BLI_str_utf8_size_safe(str) - 1;
		}
		/* Not in a string... */
		else {
			/* Deal with comments first */
			if (prev == '#' || *str == '#') {
				*fmt = '#';
				str += BLI_str_utf8_size_safe(str) - 1;
			}
			else if (*str == '"' || *str == '\'') {
				/* Strings */
				find = *str;
				cont = (*str == '"') ? TXT_DBLQUOTSTR : TXT_SNGQUOTSTR;
				if (*(str + 1) == find && *(str + 2) == find) {
					*fmt = 'l'; fmt++; str++;
					*fmt = 'l'; fmt++; str++;
					cont |= TXT_TRISTR;
				}
				*fmt = 'l';
			}
			/* Whitespace (all ws. has been converted to spaces) */
			else if (*str == ' ')
				*fmt = '_';
			/* Numbers (digits not part of an identifier and periods followed by digits) */
			else if ((prev != 'q' && text_check_digit(*str)) || (*str == '.' && text_check_digit(*(str + 1))))
				*fmt = 'n';
			/* Booleans */
			else if (prev != 'q' && (i = find_bool(str)) != -1)
				if (i > 0) {
					while (i > 1) {
						*fmt = 'n'; fmt++; str++;
						i--;
					}
					*fmt = 'n';
				}
				else {
					str += BLI_str_utf8_size_safe(str) - 1;
					*fmt = 'q';
				}
			/* Punctuation */
			else if (text_check_delim(*str))
				*fmt = '!';
			/* Identifiers and other text (no previous ws. or delims. so text continues) */
			else if (prev == 'q') {
				str += BLI_str_utf8_size_safe(str) - 1;
				*fmt = 'q';
			}
			/* Not ws, a digit, punct, or continuing text. Must be new, check for special words */
			else {
				/* Special vars(v) or built-in keywords(b) */
				if ((i = find_specialvar(str)) != -1)
					prev = 'v';
				else if ((i = find_builtinfunc(str)) != -1)
					prev = 'b';
				else if ((i = find_decorator(str)) != -1)
					prev = 'v';  /* could have a new color for this */
				if (i > 0) {
					while (i > 1) {
						*fmt = prev; fmt++; str++;
						i--;
					}
					*fmt = prev;
				}
				else {
					str += BLI_str_utf8_size_safe(str) - 1;
					*fmt = 'q';
				}
			}
		}
		prev = *fmt;
		fmt++;
		str++;
	}

	/* Terminate and add continuation char */
	*fmt = '\0'; fmt++;
	*fmt = cont;

	/* If continuation has changed and we're allowed, process the next line */
	if (cont != orig && do_next && line->next) {
		txt_format_line(st, line->next, do_next);
	}
	
	flatten_string_free(&fs);
}

#if 0
/* Formats every line of the current text */
static void txt_format_text(SpaceText *st) 
{
	TextLine *linep;

	if (!st->text) return;

	for (linep = st->text->lines.first; linep; linep = linep->next)
		txt_format_line(st, linep, 0);
}
#endif

/* Sets the current drawing color based on the format character specified */
static void format_draw_color(char formatchar)
{
	switch (formatchar) {
		case '_': /* Whitespace */
			break;
		case '!': /* Symbols */
			UI_ThemeColorBlend(TH_TEXT, TH_BACK, 0.5f);
			break;
		case '#': /* Comments */
			UI_ThemeColor(TH_SYNTAX_C);
			break;
		case 'n': /* Numerals */
			UI_ThemeColor(TH_SYNTAX_N);
			break;
		case 'l': /* Strings */
			UI_ThemeColor(TH_SYNTAX_L);
			break;
		case 'v': /* Specials: class, def */
			UI_ThemeColor(TH_SYNTAX_V);
			break;
		case 'b': /* Keywords: for, print, etc. */
			UI_ThemeColor(TH_SYNTAX_B);
			break;
		case 'q': /* Other text (identifiers) */
		default:
			UI_ThemeColor(TH_TEXT);
			break;
	}
}

/************************** draw text *****************************/

/* Notes on word-wrap
 * --
 * All word-wrap functions follow the algorithm below to maintain consistency.
 *     line        The line to wrap (tabs converted to spaces)
 *     view_width    The maximum number of characters displayable in the region
 *                 This equals region_width/font_width for the region
 *     wrap_chars    Characters that allow wrapping. This equals [' ', '\t', '-']
 * 
 * def wrap(line, view_width, wrap_chars):
 *     draw_start = 0
 *     draw_end = view_width
 *     pos = 0
 *     for c in line:
 *         if pos-draw_start >= view_width:
 *             print line[draw_start:draw_end]
 *             draw_start = draw_end
 *             draw_end += view_width
 *         elif c in wrap_chars:
 *             draw_end = pos+1
 *         pos += 1
 *     print line[draw_start:]
 * 
 */

int wrap_width(SpaceText *st, ARegion *ar)
{
	int winx = ar->winx - TXT_SCROLL_WIDTH;
	int x, max;
	
	x = st->showlinenrs ? TXT_OFFSET + TEXTXLOC : TXT_OFFSET;
	max = st->cwidth ? (winx - x) / st->cwidth : 0;
	return max > 8 ? max : 8;
}

/* Sets (offl, offc) for transforming (line, curs) to its wrapped position */
void wrap_offset(SpaceText *st, ARegion *ar, TextLine *linein, int cursin, int *offl, int *offc)
{
	Text *text;
	TextLine *linep;
	int i, j, start, end, max, chop;
	char ch;

	*offl = *offc = 0;

	if (!st->text) return;
	if (!st->wordwrap) return;

	text = st->text;

	/* Move pointer to first visible line (top) */
	linep = text->lines.first;
	i = st->top;
	while (i > 0 && linep) {
		int lines = text_get_visible_lines(st, ar, linep->line);

		/* Line before top */
		if (linep == linein) {
			if (lines <= i)
				/* no visible part of line */
				return;
		}

		if (i - lines < 0) {
			break;
		}
		else {
			linep = linep->next;
			(*offl) += lines - 1;
			i -= lines;
		}
	}

	max = wrap_width(st, ar);
	cursin = txt_utf8_offset_to_index(linein->line, cursin);

	while (linep) {
		start = 0;
		end = max;
		chop = 1;
		*offc = 0;
		for (i = 0, j = 0; linep->line[j]; j += BLI_str_utf8_size_safe(linep->line + j)) {
			int chars;

			/* Mimic replacement of tabs */
			ch = linep->line[j];
			if (ch == '\t') {
				chars = st->tabnumber - i % st->tabnumber;
				if (linep == linein && i < cursin) cursin += chars - 1;
				ch = ' ';
			}
			else {
				chars = 1;
			}

			while (chars--) {
				if (i - start >= max) {
					if (chop && linep == linein && i >= cursin) {
						if (i == cursin) {
							(*offl)++;
							*offc -= end - start;
						}

						return;
					}

					(*offl)++;
					*offc -= end - start;

					start = end;
					end += max;
					chop = 1;
				}
				else if (ch == ' ' || ch == '-') {
					end = i + 1;
					chop = 0;
					if (linep == linein && i >= cursin)
						return;
				}
				i++;
			}
		}
		if (linep == linein) break;
		linep = linep->next;
	}
}

/* cursin - mem, offc - view */
void wrap_offset_in_line(SpaceText *st, ARegion *ar, TextLine *linein, int cursin, int *offl, int *offc)
{
	int i, j, start, end, chars, max, chop;
	char ch;

	*offl = *offc = 0;

	if (!st->text) return;
	if (!st->wordwrap) return;

	max = wrap_width(st, ar);

	start = 0;
	end = max;
	chop = 1;
	*offc = 0;
	cursin = txt_utf8_offset_to_index(linein->line, cursin);

	for (i = 0, j = 0; linein->line[j]; j += BLI_str_utf8_size_safe(linein->line + j)) {

		/* Mimic replacement of tabs */
		ch = linein->line[j];
		if (ch == '\t') {
			chars = st->tabnumber - i % st->tabnumber;
			if (i < cursin) cursin += chars - 1;
			ch = ' ';
		}
		else
			chars = 1;

		while (chars--) {
			if (i - start >= max) {
				if (chop && i >= cursin) {
					if (i == cursin) {
						(*offl)++;
						*offc -= end - start;
					}

					return;
				}

				(*offl)++;
				*offc -= end - start;

				start = end;
				end += max;
				chop = 1;
			}
			else if (ch == ' ' || ch == '-') {
				end = i + 1;
				chop = 0;
				if (i >= cursin)
					return;
			}
			i++;
		}
	}
}

int text_get_char_pos(SpaceText *st, const char *line, int cur)
{
	int a = 0, i;
	
	for (i = 0; i < cur && line[i]; i += BLI_str_utf8_size_safe(line + i)) {
		if (line[i] == '\t')
			a += st->tabnumber - a % st->tabnumber;
		else
			a++;
	}
	return a;
}

static const char *txt_utf8_get_nth(const char *str, int n)
{
	int pos = 0;
	while (str[pos] && n--) {
		pos += BLI_str_utf8_size_safe(str + pos);
	}
	return str + pos;
}

static int text_draw_wrapped(SpaceText *st, const char *str, int x, int y, int w, const char *format, int skip)
{
	FlattenString fs;
	int basex, i, a, start, end, max, lines; /* view */
	int mi, ma, mstart, mend;                /* mem */
	
	flatten_string(st, &fs, str);
	str = fs.buf;
	max = w / st->cwidth;
	if (max < 8) max = 8;
	basex = x;
	lines = 1;
	
	start = 0; mstart = 0;
	end = max; mend = txt_utf8_get_nth(str, max) - str;
	
	for (i = 0, mi = 0; str[mi]; i++, mi += BLI_str_utf8_size_safe(str + mi)) {
		if (i - start >= max) {
			int ox;
			char last_format;
			char buffer[BLF_DRAW_STR_DUMMY_MAX];
			size_t len = 0;

			/* skip hidden part of line */
			if (skip) {
				skip--;
				start = end; mstart = mend;
				end += max; mend = txt_utf8_get_nth(str + mend, max) - str;
				continue;
			}

			if (st->showsyntax && format) {
				last_format = format[start];
				format_draw_color(format[start]);
			}

			ox = x;

			ma = mstart;

			/* Draw the visible portion of text on the overshot line */
			for (a = start; a < end; a++) {
				size_t char_len;

				if (st->showsyntax && format && last_format != format[a]) {
					memcpy(buffer, str + ma - len, len);
					buffer[len] = '\0';
					text_font_draw(st, ox, y, buffer);

					ox  = x;
					len = 0;

					format_draw_color(format[a]);
					last_format = format[a];
				}

				char_len = BLI_str_utf8_size(str + ma);
				len += char_len;
				ma  += char_len;

				x += st->cwidth;
			}

			/* draw last chunk */
			memcpy(buffer, str + ma - len, len);
			buffer[len] = '\0';
			text_font_draw(st, ox, y, buffer);

			y -= st->lheight + TXT_LINE_SPACING;
			x = basex;
			lines++;
			start = end; mstart = mend;
			end += max; mend = txt_utf8_get_nth(str + mend, max) - str;

			if (y <= 0) break;
		}
		else if (str[mi] == ' ' || str[mi] == '-') {
			end = i + 1; mend = mi + 1;
		}
	}

	/* Draw the remaining text */
	if (y > 0) {
		int ox;
		char last_format;
		char buffer[BLF_DRAW_STR_DUMMY_MAX];
		size_t len = 0;
		const int showsyntax = st->showsyntax && format;

		if (showsyntax) {
			last_format = format[start];
			format_draw_color(format[start]);
		}

		ox = x;

		ma = mstart;

		for (a = start; str[ma]; a++) {
			size_t char_len;

			if (showsyntax && last_format != format[a]) {
				memcpy(buffer, str + ma - len, len);
				buffer[len] = '\0';
				text_font_draw(st, ox, y, buffer);

				ox  = x;
				len = 0;

				format_draw_color(format[a]);
				last_format = format[a];
			}

			char_len = BLI_str_utf8_size(str + ma);
			len += char_len;
			ma  += char_len;

			x += st->cwidth;
		}

		/* draw last chunk */
		memcpy(buffer, str + ma - len, len);
		buffer[len] = '\0';
		text_font_draw(st, ox, y, buffer);
	}

	flatten_string_free(&fs);

	return lines;
}

static int text_draw(SpaceText *st, char *str, int cshift, int maxwidth, int draw, int x, int y, const char *format)
{
	FlattenString fs;
	int *acc, r = 0;
	const char *in;

	int w = flatten_string(st, &fs, str);
	if (w < cshift) {
		flatten_string_free(&fs);
		return 0; /* String is shorter than shift */
	}
	
	in = txt_utf8_get_nth(fs.buf, cshift);
	acc = fs.accum + cshift;
	w = w - cshift;

	if (draw) {
		int amount =
			maxwidth ?
				MIN3(w, maxwidth, BLF_DRAW_STR_DUMMY_MAX) :
				MIN2(w, BLF_DRAW_STR_DUMMY_MAX);

		if (st->showsyntax && format) {
			int a;
			int str_shift = 0;
			int ox;
			char last_format;
			char buffer[BLF_DRAW_STR_DUMMY_MAX];
			size_t len = 0;

			format = format + cshift;

			last_format = format[0];
			format_draw_color(format[0]);

			ox = x;

			for (a = 0; a < amount; a++) {
				size_t char_len;

				if (last_format != format[a]) {
					memcpy(buffer, in + str_shift - len, len);
					buffer[len] = '\0';
					text_font_draw(st, ox, y, buffer);

					ox  = x;
					len = 0;

					format_draw_color(format[a]);
					last_format = format[a];
				}

				char_len = BLI_str_utf8_size(in + str_shift);
				str_shift += char_len;
				len       += char_len;

				x += st->cwidth;
			}

			/* draw last chunk */
			memcpy(buffer, in + str_shift - len, len);
			buffer[len] = '\0';
			text_font_draw(st, ox, y, buffer);
		}
		else text_font_draw(st, x, y, in);
	}
	else {
		while (w-- && *acc++ < maxwidth)
			r += st->cwidth;
	}

	flatten_string_free(&fs);

	if (cshift && r == 0)
		return 0;
	else if (st->showlinenrs)
		return r + TXT_OFFSET + TEXTXLOC;
	else
		return r + TXT_OFFSET;
}

/************************ cache utilities *****************************/

typedef struct DrawCache {
	int *line_height;
	int total_lines, nlines;

	/* this is needed to check cache relevance */
	int winx, wordwrap, showlinenrs, tabnumber;
	short lheight;
	char cwidth;
	char text_id[MAX_ID_NAME];

	/* for partial lines recalculation */
	short update_flag;
	int valid_head, valid_tail; /* amount of unchanged lines */
} DrawCache;

static void text_drawcache_init(SpaceText *st)
{
	DrawCache *drawcache = MEM_callocN(sizeof(DrawCache), "text draw cache");

	drawcache->winx = -1;
	drawcache->nlines = BLI_countlist(&st->text->lines);
	drawcache->text_id[0] = '\0';

	st->drawcache = drawcache;
}

static void text_update_drawcache(SpaceText *st, ARegion *ar)
{
	DrawCache *drawcache;
	int full_update = 0, nlines = 0;
	Text *txt = st->text;

	if (!st->drawcache) text_drawcache_init(st);

	text_update_character_width(st);

	drawcache = (DrawCache *)st->drawcache;
	nlines = drawcache->nlines;

	/* check if full cache update is needed */
	full_update |= drawcache->winx != ar->winx;               /* area was resized */
	full_update |= drawcache->wordwrap != st->wordwrap;       /* word-wrapping option was toggled */
	full_update |= drawcache->showlinenrs != st->showlinenrs; /* word-wrapping option was toggled */
	full_update |= drawcache->tabnumber != st->tabnumber;     /* word-wrapping option was toggled */
	full_update |= drawcache->lheight != st->lheight;         /* word-wrapping option was toggled */
	full_update |= drawcache->cwidth != st->cwidth;           /* word-wrapping option was toggled */
	full_update |= strncmp(drawcache->text_id, txt->id.name, MAX_ID_NAME); /* text datablock was changed */

	if (st->wordwrap) {
		/* update line heights */
		if (full_update || !drawcache->line_height) {
			drawcache->valid_head  = 0;
			drawcache->valid_tail  = 0;
			drawcache->update_flag = 1;
		}

		if (drawcache->update_flag) {
			TextLine *line = st->text->lines.first;
			int lineno = 0, size, lines_count;
			int *fp = drawcache->line_height, *new_tail, *old_tail;

			nlines = BLI_countlist(&txt->lines);
			size = sizeof(int) * nlines;

			if (fp) fp = MEM_reallocN(fp, size);
			else fp = MEM_callocN(size, "text drawcache line_height");

			drawcache->valid_tail = drawcache->valid_head = 0;
			old_tail = fp + drawcache->nlines - drawcache->valid_tail;
			new_tail = fp + nlines - drawcache->valid_tail;
			memmove(new_tail, old_tail, drawcache->valid_tail);

			drawcache->total_lines = 0;

			if (st->showlinenrs)
				st->linenrs_tot = (int)floor(log10((float)nlines)) + 1;

			while (line) {
				if (drawcache->valid_head) { /* we're inside valid head lines */
					lines_count = fp[lineno];
					drawcache->valid_head--;
				}
				else if (lineno > new_tail - fp) {  /* we-re inside valid tail lines */
					lines_count = fp[lineno];
				}
				else {
					lines_count = text_get_visible_lines(st, ar, line->line);
				}

				fp[lineno] = lines_count;

				line = line->next;
				lineno++;
				drawcache->total_lines += lines_count;
			}

			drawcache->line_height = fp;
		}
	}
	else {
		if (drawcache->line_height) {
			MEM_freeN(drawcache->line_height);
			drawcache->line_height = NULL;
		}

		if (full_update || drawcache->update_flag) {
			nlines = BLI_countlist(&txt->lines);

			if (st->showlinenrs)
				st->linenrs_tot = (int)floor(log10((float)nlines)) + 1;
		}

		drawcache->total_lines = nlines;
	}

	drawcache->nlines = nlines;

	/* store settings */
	drawcache->winx        = ar->winx;
	drawcache->wordwrap    = st->wordwrap;
	drawcache->lheight     = st->lheight;
	drawcache->cwidth      = st->cwidth;
	drawcache->showlinenrs = st->showlinenrs;
	drawcache->tabnumber   = st->tabnumber;

	strncpy(drawcache->text_id, txt->id.name, MAX_ID_NAME);

	/* clear update flag */
	drawcache->update_flag = 0;
	drawcache->valid_head  = 0;
	drawcache->valid_tail  = 0;
}

void text_drawcache_tag_update(SpaceText *st, int full)
{
	/* this happens if text editor ops are caled from python */
	if (st == NULL)
		return;
		
	if (st->drawcache) {
		DrawCache *drawcache = (DrawCache *)st->drawcache;
		Text *txt = st->text;

		if (drawcache->update_flag) {
			/* happens when tagging update from space listener */
			/* should do nothing to prevent locally tagged cache be fully recalculated */
			return;
		}

		if (!full) {
			int sellno = BLI_findindex(&txt->lines, txt->sell);
			int curlno = BLI_findindex(&txt->lines, txt->curl);

			if (curlno < sellno) {
				drawcache->valid_head = curlno;
				drawcache->valid_tail = drawcache->nlines - sellno - 1;
			}
			else {
				drawcache->valid_head = sellno;
				drawcache->valid_tail = drawcache->nlines - curlno - 1;
			}

			/* quick cache recalculation is also used in delete operator,
			 * which could merge lines which are adjacent to current selection lines
			 * expand recalculate area to this lines */
			if (drawcache->valid_head > 0) drawcache->valid_head--;
			if (drawcache->valid_tail > 0) drawcache->valid_tail--;
		}
		else {
			drawcache->valid_head = 0;
			drawcache->valid_tail = 0;
		}

		drawcache->update_flag = 1;
	}
}

void text_free_caches(SpaceText *st)
{
	DrawCache *drawcache = (DrawCache *)st->drawcache;

	if (drawcache) {
		if (drawcache->line_height)
			MEM_freeN(drawcache->line_height);

		MEM_freeN(drawcache);
	}
}

/************************ word-wrap utilities *****************************/

/* cache should be updated in caller */
static int text_get_visible_lines_no(SpaceText *st, int lineno)
{
	DrawCache *drawcache = (DrawCache *)st->drawcache;

	return drawcache->line_height[lineno];
}

int text_get_visible_lines(SpaceText *st, ARegion *ar, const char *str)
{
	int i, j, start, end, max, lines, chars;
	char ch;

	max = wrap_width(st, ar);
	lines = 1;
	start = 0;
	end = max;
	for (i = 0, j = 0; str[j]; j += BLI_str_utf8_size_safe(str + j)) {
		/* Mimic replacement of tabs */
		ch = str[j];
		if (ch == '\t') {
			chars = st->tabnumber - i % st->tabnumber;
			ch = ' ';
		}
		else chars = 1;

		while (chars--) {
			if (i - start >= max) {
				lines++;
				start = end;
				end += max;
			}
			else if (ch == ' ' || ch == '-') {
				end = i + 1;
			}

			i++;
		}
	}

	return lines;
}

int text_get_span_wrap(SpaceText *st, ARegion *ar, TextLine *from, TextLine *to)
{
	if (st->wordwrap) {
		int ret = 0;
		TextLine *tmp = from;

		/* Look forwards */
		while (tmp) {
			if (tmp == to) return ret;
			ret += text_get_visible_lines(st, ar, tmp->line);
			tmp = tmp->next;
		}

		return ret;
	}
	else return txt_get_span(from, to);
}

int text_get_total_lines(SpaceText *st, ARegion *ar)
{
	DrawCache *drawcache;

	text_update_drawcache(st, ar);
	drawcache = (DrawCache *)st->drawcache;

	return drawcache->total_lines;
}

/************************ draw scrollbar *****************************/

static void calc_text_rcts(SpaceText *st, ARegion *ar, rcti *scroll, rcti *back)
{
	int lhlstart, lhlend, ltexth, sell_off, curl_off;
	short barheight, barstart, hlstart, hlend, blank_lines;
	short pix_available, pix_top_margin, pix_bottom_margin, pix_bardiff;

	pix_top_margin = 8;
	pix_bottom_margin = 4;
	pix_available = ar->winy - pix_top_margin - pix_bottom_margin;
	ltexth = text_get_total_lines(st, ar);
	blank_lines = st->viewlines / 2;
	
	/* nicer code: use scroll rect for entire bar */
	back->xmin = ar->winx - 18;
	back->xmax = ar->winx;
	back->ymin = 0;
	back->ymax = ar->winy;
	
	scroll->xmin = ar->winx - 17;
	scroll->xmax = ar->winx - 5;
	scroll->ymin = 4;
	scroll->ymax = 4 + pix_available;
	
	/* when re-sizing a view-port with the bar at the bottom to a greater height more blank lines will be added */
	if (ltexth + blank_lines < st->top + st->viewlines) {
		blank_lines = st->top + st->viewlines - ltexth;
	}
	
	ltexth += blank_lines;

	barheight = (ltexth > 0) ? (st->viewlines * pix_available) / ltexth : 0;
	pix_bardiff = 0;
	if (barheight < 20) {
		pix_bardiff = 20 - barheight; /* take into account the now non-linear sizing of the bar */
		barheight = 20;
	}
	barstart = (ltexth > 0) ? ((pix_available - pix_bardiff) * st->top) / ltexth : 0;

	st->txtbar = *scroll;
	st->txtbar.ymax -= barstart;
	st->txtbar.ymin = st->txtbar.ymax - barheight;

	CLAMP(st->txtbar.ymin, pix_bottom_margin, ar->winy - pix_top_margin);
	CLAMP(st->txtbar.ymax, pix_bottom_margin, ar->winy - pix_top_margin);

	st->pix_per_line = (pix_available > 0) ? (float) ltexth / pix_available : 0;
	if (st->pix_per_line < 0.1f) st->pix_per_line = 0.1f;

	curl_off = text_get_span_wrap(st, ar, st->text->lines.first, st->text->curl);
	sell_off = text_get_span_wrap(st, ar, st->text->lines.first, st->text->sell);
	lhlstart = MIN2(curl_off, sell_off);
	lhlend = MAX2(curl_off, sell_off);

	if (ltexth > 0) {
		hlstart = (lhlstart * pix_available) / ltexth;
		hlend = (lhlend * pix_available) / ltexth;

		/* the scrollbar is non-linear sized */
		if (pix_bardiff > 0) {
			/* the start of the highlight is in the current viewport */
			if (ltexth && st->viewlines && lhlstart >= st->top && lhlstart <= st->top + st->viewlines) {
				/* speed the progresion of the start of the highlight through the scrollbar */
				hlstart = ( ( (pix_available - pix_bardiff) * lhlstart) / ltexth) + (pix_bardiff * (lhlstart - st->top) / st->viewlines);
			}
			else if (lhlstart > st->top + st->viewlines && hlstart < barstart + barheight && hlstart > barstart) {
				/* push hl start down */
				hlstart = barstart + barheight;
			}
			else if (lhlend > st->top && lhlstart < st->top && hlstart > barstart) {
				/*fill out start */
				hlstart = barstart;
			}

			if (hlend <= hlstart) {
				hlend = hlstart + 2;
			}

			/* the end of the highlight is in the current viewport */
			if (ltexth && st->viewlines && lhlend >= st->top && lhlend <= st->top + st->viewlines) {
				/* speed the progresion of the end of the highlight through the scrollbar */
				hlend = (((pix_available - pix_bardiff) * lhlend) / ltexth) + (pix_bardiff * (lhlend - st->top) / st->viewlines);
			}
			else if (lhlend < st->top && hlend >= barstart - 2 && hlend < barstart + barheight) {
				/* push hl end up */
				hlend = barstart;
			}
			else if (lhlend > st->top + st->viewlines && lhlstart < st->top + st->viewlines && hlend < barstart + barheight) {
				/* fill out end */
				hlend = barstart + barheight;
			}

			if (hlend <= hlstart) {
				hlstart = hlend - 2;
			}
		}
	}
	else {
		hlstart = 0;
		hlend = 0;
	}

	if (hlend - hlstart < 2) {
		hlend = hlstart + 2;
	}
	
	st->txtscroll = *scroll;
	st->txtscroll.ymax = ar->winy - pix_top_margin - hlstart;
	st->txtscroll.ymin = ar->winy - pix_top_margin - hlend;

	CLAMP(st->txtscroll.ymin, pix_bottom_margin, ar->winy - pix_top_margin);
	CLAMP(st->txtscroll.ymax, pix_bottom_margin, ar->winy - pix_top_margin);
}

static void draw_textscroll(SpaceText *st, rcti *scroll, rcti *back)
{
	bTheme *btheme = UI_GetTheme();
	uiWidgetColors wcol = btheme->tui.wcol_scroll;
	unsigned char col[4];
	float rad;
	
	UI_ThemeColor(TH_BACK);
	gpuSingleFilledRecti(back->xmin, back->ymin, back->xmax, back->ymax);

	uiWidgetScrollDraw(&wcol, scroll, &st->txtbar, (st->flags & ST_SCROLL_SELECT) ? UI_SCROLL_PRESSED : 0);

	uiSetRoundBox(UI_CNR_ALL);
	rad = 0.4f * min_ii(BLI_rcti_size_x(&st->txtscroll), BLI_rcti_size_y(&st->txtscroll));
	UI_GetThemeColor3ubv(TH_HILITE, col);
	col[3] = 48;
	gpuCurrentColor4ubv(col);
	glEnable(GL_BLEND);
	uiRoundBox(st->txtscroll.xmin + 1, st->txtscroll.ymin, st->txtscroll.xmax - 1, st->txtscroll.ymax, rad);
	glDisable(GL_BLEND);
}

/*********************** draw documentation *******************************/

static void draw_documentation(SpaceText *st, ARegion *ar)
{
	TextLine *tmp;
	char *docs, buf[DOC_WIDTH + 1], *p;
	int i, br, lines;
	int boxw, boxh, l, x, y /* , top */ /* UNUSED */;
	
	if (!st || !st->text) return;
	if (!texttool_text_is_active(st->text)) return;
	
	docs = texttool_docs_get();

	if (!docs) return;

	/* Count the visible lines to the cursor */
	for (tmp = st->text->curl, l = -st->top; tmp; tmp = tmp->prev, l++) ;
	if (l < 0) return;
	
	if (st->showlinenrs) {
		x = st->cwidth * (st->text->curc - st->left) + TXT_OFFSET + TEXTXLOC - 4;
	}
	else {
		x = st->cwidth * (st->text->curc - st->left) + TXT_OFFSET - 4;
	}
	if (texttool_suggest_first()) {
		x += SUGG_LIST_WIDTH * st->cwidth + 50;
	}

	/* top = */ /* UNUSED */ y = ar->winy - st->lheight * l - 2;
	boxw = DOC_WIDTH * st->cwidth + 20;
	boxh = (DOC_HEIGHT + 1) * st->lheight;

	/* Draw panel */
	UI_ThemeColor(TH_BACK);
	gpuSingleFilledRecti(x, y, x + boxw, y - boxh);
	UI_ThemeColor(TH_SHADE1);
	gpuImmediateFormat_V2();
	gpuBegin(GL_LINE_LOOP);
	gpuVertex2i(x, y);
	gpuVertex2i(x + boxw, y);
	gpuVertex2i(x + boxw, y - boxh);
	gpuVertex2i(x, y - boxh);
	gpuEnd();
	gpuBegin(GL_LINE_LOOP);
	gpuVertex2i(x + boxw - 10, y - 7);
	gpuVertex2i(x + boxw - 4, y - 7);
	gpuVertex2i(x + boxw - 7, y - 2);
	gpuEnd();
	gpuBegin(GL_LINE_LOOP);
	gpuVertex2i(x + boxw - 10, y - boxh + 7);
	gpuVertex2i(x + boxw - 4, y - boxh + 7);
	gpuVertex2i(x + boxw - 7, y - boxh + 2);
	gpuEnd();
	gpuImmediateUnformat();
	UI_ThemeColor(TH_TEXT);

	BLF_draw_lock(mono);

	i = 0; br = DOC_WIDTH; lines = 0; // XXX -doc_scroll;
	for (p = docs; *p; p++) {
		if (*p == '\r' && *(++p) != '\n') *(--p) = '\n';  /* Fix line endings */
		if (*p == ' ' || *p == '\t')
			br = i;
		else if (*p == '\n') {
			buf[i] = '\0';
			if (lines >= 0) {
				y -= st->lheight;
				text_draw(st, buf, 0, 0, 1, x + 4, y - 3, NULL);
			}
			i = 0; br = DOC_WIDTH; lines++;
		}
		buf[i++] = *p;
		if (i == DOC_WIDTH) { /* Reached the width, go to last break and wrap there */
			buf[br] = '\0';
			if (lines >= 0) {
				y -= st->lheight;
				text_draw(st, buf, 0, 0, 1, x + 4, y - 3, NULL);
			}
			p -= i - br - 1; /* Rewind pointer to last break */
			i = 0; br = DOC_WIDTH; lines++;
		}
		if (lines >= DOC_HEIGHT) break;
	}

	BLF_draw_unlock(mono);

	if (0 /* XXX doc_scroll*/ > 0 && lines < DOC_HEIGHT) {
		// XXX doc_scroll--;
		draw_documentation(st, ar);
	}
}

/*********************** draw suggestion list *******************************/

static void draw_suggestion_list(SpaceText *st, ARegion *ar)
{
	SuggItem *item, *first, *last, *sel;
	TextLine *tmp;
	char str[SUGG_LIST_WIDTH + 1];
	int w, boxw = 0, boxh, i, l, x, y, b, *top;
	
	if (!st || !st->text) return;
	if (!texttool_text_is_active(st->text)) return;

	first = texttool_suggest_first();
	last = texttool_suggest_last();

	if (!first || !last) return;

	text_pop_suggest_list();
	sel = texttool_suggest_selected();
	top = texttool_suggest_top();

	/* Count the visible lines to the cursor */
	for (tmp = st->text->curl, l = -st->top; tmp; tmp = tmp->prev, l++) ;
	if (l < 0) return;
	
	if (st->showlinenrs) {
		x = st->cwidth * (st->text->curc - st->left) + TXT_OFFSET + TEXTXLOC - 4;
	}
	else {
		x = st->cwidth * (st->text->curc - st->left) + TXT_OFFSET - 4;
	}
	y = ar->winy - st->lheight * l - 2;

	boxw = SUGG_LIST_WIDTH * st->cwidth + 20;
	boxh = SUGG_LIST_SIZE * st->lheight + 8;
	
	UI_ThemeColor(TH_SHADE1);
	gpuSingleFilledRecti(x - 1, y + 1, x + boxw + 1, y - boxh - 1);
	UI_ThemeColor(TH_BACK);
	gpuSingleFilledRecti(x, y, x + boxw, y - boxh);

	BLF_draw_lock(mono);

	/* Set the top 'item' of the visible list */
	for (i = 0, item = first; i < *top && item->next; i++, item = item->next) ;

	for (i = 0; i < SUGG_LIST_SIZE && item; i++, item = item->next) {

		y -= st->lheight;

		BLI_strncpy(str, item->name, SUGG_LIST_WIDTH);

		w = BLF_width(mono, str);
		
		if (item == sel) {
			UI_ThemeColor(TH_SHADE2);
			gpuSingleFilledRecti(x + 16, y - 3, x + 16 + w, y + st->lheight - 3);
		}
		b = 1; /* b=1 color block, text is default. b=0 no block, color text */
		switch (item->type) {
			case 'k': UI_ThemeColor(TH_SYNTAX_B); b = 0; break;
			case 'm': UI_ThemeColor(TH_TEXT); break;
			case 'f': UI_ThemeColor(TH_SYNTAX_L); break;
			case 'v': UI_ThemeColor(TH_SYNTAX_N); break;
			case '?': UI_ThemeColor(TH_TEXT); b = 0; break;
		}
		if (b) {
			gpuSingleFilledRecti(x + 8, y + 2, x + 11, y + 5);
			UI_ThemeColor(TH_TEXT);
		}
		text_draw(st, str, 0, 0, 1, x + 16, y - 1, NULL);

		if (item == last) break;
	}

	BLF_draw_unlock(mono);
}

/*********************** draw cursor ************************/

static void draw_cursor(SpaceText *st, ARegion *ar)
{
	Text *text = st->text;
	int vcurl, vcurc, vsell, vselc, hidden = 0;
	int x, y, w, i;
	int lheight = st->lheight + TXT_LINE_SPACING;

	/* Draw the selection */
	if (text->curl != text->sell || text->curc != text->selc) {
		int offl, offc;
		/* Convert all to view space character coordinates */
		wrap_offset(st, ar, text->curl, text->curc, &offl, &offc);
		vcurl = txt_get_span(text->lines.first, text->curl) - st->top + offl;
		vcurc = text_get_char_pos(st, text->curl->line, text->curc) - st->left + offc;
		wrap_offset(st, ar, text->sell, text->selc, &offl, &offc);
		vsell = txt_get_span(text->lines.first, text->sell) - st->top + offl;
		vselc = text_get_char_pos(st, text->sell->line, text->selc) - st->left + offc;

		if (vcurc < 0) vcurc = 0;
		if (vselc < 0) vselc = 0, hidden = 1;
		
		UI_ThemeColor(TH_SHADE2);
		x = st->showlinenrs ? TXT_OFFSET + TEXTXLOC : TXT_OFFSET;
		y = ar->winy - 2;

		if (vcurl == vsell) {
			y -= vcurl * lheight;
			if (vcurc < vselc)
				gpuSingleFilledRecti(x + vcurc * st->cwidth - 1, y, x + vselc * st->cwidth, y - st->lheight + TXT_LINE_SPACING);
			else
				gpuSingleFilledRecti(x + vselc * st->cwidth - 1, y, x + vcurc * st->cwidth, y - st->lheight + TXT_LINE_SPACING);
		}
		else {
			int froml, fromc, tol, toc;

			if (vcurl < vsell) {
				froml = vcurl; tol = vsell;
				fromc = vcurc; toc = vselc;
			}
			else {
				froml = vsell; tol = vcurl;
				fromc = vselc; toc = vcurc;
			}

			y -= froml * lheight;
			gpuSingleFilledRecti(x + fromc * st->cwidth - 1, y, ar->winx, y - st->lheight);
			y -= st->lheight;
			for (i = froml + 1; i < tol; i++)

			for (i = froml + 1; i < tol; i++) {
				gpuSingleFilledRecti(x - 4, y, ar->winx, y - st->lheight + TXT_LINE_SPACING);
				y -= st->lheight;
			}

			gpuSingleFilledRecti(x - 4, y, x + toc * st->cwidth, y - st->lheight);
		}
	}
	else {
		int offl, offc;
		wrap_offset(st, ar, text->sell, text->selc, &offl, &offc);
		vsell = txt_get_span(text->lines.first, text->sell) - st->top + offl;
		vselc = text_get_char_pos(st, text->sell->line, text->selc) - st->left + offc;

		if (vselc < 0) {
			vselc = 0;
			hidden = 1;
		}
	}

	if (st->line_hlight) {
		int x1, x2, y1, y2;

		if (st->wordwrap) {
			int visible_lines = text_get_visible_lines(st, ar, text->sell->line);
			int offl, offc;

			wrap_offset_in_line(st, ar, text->sell, text->selc, &offl, &offc);

			y1 = ar->winy - 2 - (vsell - offl) * lheight;
			y2 = y1 - lheight * visible_lines + 1;
		}
		else {
			y1 = ar->winy - 2 - vsell * lheight;
			y2 = y1 - lheight + 1;
		}

		if (!(y1 < 0 || y2 > ar->winy)) { /* check we need to draw */
			x1 = st->showlinenrs ? TXT_OFFSET + TEXTXLOC : TXT_OFFSET;
			x2 = x1 + ar->winx;

			gpuCurrentColor4x(CPACK_WHITE, 0.125f);
			
			glEnable(GL_BLEND);
			gpuSingleFilledRecti(x1 - 4, y1, x2, y2 + TXT_LINE_SPACING);
			glDisable(GL_BLEND);
		}
	}

	if (!hidden) {
		/* Draw the cursor itself (we draw the sel. cursor as this is the leading edge) */
		x = st->showlinenrs ? TXT_OFFSET + TEXTXLOC : TXT_OFFSET;
		x += vselc * st->cwidth;
		y = ar->winy - 2 - vsell * lheight;
		
		if (st->overwrite) {
			char ch = text->sell->line[text->selc];
			
			y += TXT_LINE_SPACING;
			w = st->cwidth;
			if (ch == '\t') w *= st->tabnumber - (vselc + st->left) % st->tabnumber;
			
			UI_ThemeColor(TH_HILITE);
			gpuSingleFilledRecti(x, y - lheight - 1, x + w, y - lheight + 1);
		}
		else {
			UI_ThemeColor(TH_HILITE);
			gpuSingleFilledRecti(x - 1, y, x + 1, y - lheight + TXT_LINE_SPACING);
		}
	}
}

/******************* draw matching brackets *********************/

static void draw_brackets(SpaceText *st, ARegion *ar)
{
	TextLine *startl, *endl, *linep;
	Text *text = st->text;
	int b, fc, find, stack, viewc, viewl, offl, offc, x, y;
	int startc, endc, c;
	
	char ch;

	// showsyntax must be on or else the format string will be null
	if (!text->curl || !st->showsyntax) return;

	startl = text->curl;
	startc = text->curc;
	b = text_check_bracket(startl->line[startc]);
	if (b == 0 && startc > 0) b = text_check_bracket(startl->line[--startc]);
	if (b == 0) return;

	linep = startl;
	c = startc;
	fc = txt_utf8_offset_to_index(linep->line, startc);
	endl = NULL;
	endc = -1;
	find = -b;
	stack = 0;
	
	/* Don't highlight backets if syntax HL is off or bracket in string or comment. */
	if (!linep->format || linep->format[fc] == 'l' || linep->format[fc] == '#')
		return;

	if (b > 0) {
		/* opening bracket, search forward for close */
		fc++;
		c += BLI_str_utf8_size_safe(linep->line + c);
		while (linep) {
			while (c < linep->len) {
				if (linep->format && linep->format[fc] != 'l' && linep->format[fc] != '#') {
					b = text_check_bracket(linep->line[c]);
					if (b == find) {
						if (stack == 0) {
							endl = linep;
							endc = c;
							break;
						}
						stack--;
					}
					else if (b == -find) {
						stack++;
					}
				}
				fc++;
				c += BLI_str_utf8_size_safe(linep->line + c);
			}
			if (endl) break;
			linep = linep->next;
			c = 0;
			fc = 0;
		}
	}
	else {
		/* closing bracket, search backward for open */
		fc--;
		if (c > 0) c -= linep->line + c - BLI_str_prev_char_utf8(linep->line + c);
		while (linep) {
			while (fc >= 0) {
				if (linep->format && linep->format[fc] != 'l' && linep->format[fc] != '#') {
					b = text_check_bracket(linep->line[c]);
					if (b == find) {
						if (stack == 0) {
							endl = linep;
							endc = c;
							break;
						}
						stack--;
					}
					else if (b == -find) {
						stack++;
					}
				}
				fc--;
				if (c > 0) c -= linep->line + c - BLI_str_prev_char_utf8(linep->line + c);
			}
			if (endl) break;
			linep = linep->prev;
			if (linep) {
				if (linep->format) fc = strlen(linep->format) - 1;
				else fc = -1;
				if (linep->len) c = BLI_str_prev_char_utf8(linep->line + linep->len) - linep->line;
				else fc = -1;
			}
		}
	}

	if (!endl || endc == -1)
		return;

	UI_ThemeColor(TH_HILITE);
	x = st->showlinenrs ? TXT_OFFSET + TEXTXLOC : TXT_OFFSET;
	y = ar->winy - st->lheight;

	/* draw opening bracket */
	ch = startl->line[startc];
	wrap_offset(st, ar, startl, startc, &offl, &offc);
	viewc = text_get_char_pos(st, startl->line, startc) - st->left + offc;

	if (viewc >= 0) {
		viewl = txt_get_span(text->lines.first, startl) - st->top + offl;

		text_font_draw_character(st, x + viewc * st->cwidth, y - viewl * (st->lheight + TXT_LINE_SPACING), ch);
		text_font_draw_character(st, x + viewc * st->cwidth + 1, y - viewl * (st->lheight + TXT_LINE_SPACING), ch);
	}

	/* draw closing bracket */
	ch = endl->line[endc];
	wrap_offset(st, ar, endl, endc, &offl, &offc);
	viewc = text_get_char_pos(st, endl->line, endc) - st->left + offc;

	if (viewc >= 0) {
		viewl = txt_get_span(text->lines.first, endl) - st->top + offl;

		text_font_draw_character(st, x + viewc * st->cwidth, y - viewl * (st->lheight + TXT_LINE_SPACING), ch);
		text_font_draw_character(st, x + viewc * st->cwidth + 1, y - viewl * (st->lheight + TXT_LINE_SPACING), ch);
	}
}

/*********************** main area drawing *************************/

void draw_text_main(SpaceText *st, ARegion *ar)
{
	Text *text = st->text;
	TextLine *tmp;
	rcti scroll, back;
	char linenr[12];
	int i, x, y, winx, linecount = 0, lineno = 0;
	int wraplinecount = 0, wrap_skip = 0;
	int margin_column_x;

	if (st->lheight) st->viewlines = (int)ar->winy / (st->lheight + TXT_LINE_SPACING);
	else st->viewlines = 0;

	/* if no text, nothing to do */
	if (!text)
		return;
	
	text_update_drawcache(st, ar);

	/* make sure all the positional pointers exist */
	if (!text->curl || !text->sell || !text->lines.first || !text->lines.last)
		txt_clean_text(text);
	
	/* update rects for scroll */
	calc_text_rcts(st, ar, &scroll, &back); /* scroll will hold the entire bar size */

	/* update syntax formatting if needed */
	tmp = text->lines.first;
	lineno = 0;
	for (i = 0; i < st->top && tmp; i++) {
		if (st->showsyntax && !tmp->format)
			txt_format_line(st, tmp, 0);

		if (st->wordwrap) {
			int lines = text_get_visible_lines_no(st, lineno);

			if (wraplinecount + lines > st->top) {
				wrap_skip = st->top - wraplinecount;
				break;
			}
			else {
				wraplinecount += lines;
				tmp = tmp->next;
				linecount++;
			}
		}
		else {
			tmp = tmp->next;
			linecount++;
		}

		lineno++;
	}

	text_font_begin(st);
	st->cwidth = BLF_fixed_width(mono);
	st->cwidth = MAX2(st->cwidth, (char)1);

	/* draw line numbers background */
	if (st->showlinenrs) {
		x = TXT_OFFSET + TEXTXLOC;

		UI_ThemeColor(TH_GRID);
		gpuSingleFilledRecti((TXT_OFFSET - 12), 0, (TXT_OFFSET - 5) + TEXTXLOC, ar->winy - 2);
	}
	else {
		st->linenrs_tot = 0; /* not used */
		x = TXT_OFFSET;
	}
	y = ar->winy - st->lheight;
	winx = ar->winx - TXT_SCROLL_WIDTH;
	
	/* draw cursor */
	draw_cursor(st, ar);

	/* draw the text */
	UI_ThemeColor(TH_TEXT);

	GPU_STRING_MARKER("draw_text_main:begin");

	BLF_draw_lock(mono);

	for (i = 0; y > 0 && i < st->viewlines && tmp; i++, tmp = tmp->next) {
		if (st->showsyntax && !tmp->format)
			txt_format_line(st, tmp, 0);

		if (st->showlinenrs && !wrap_skip) {
			/* draw line number */
			if (tmp == text->curl)
				UI_ThemeColor(TH_HILITE);
			else
				UI_ThemeColor(TH_TEXT);

			BLI_snprintf(linenr, sizeof(linenr), "%*d", st->linenrs_tot, i + linecount + 1);
			/* itoa(i + linecount + 1, linenr, 10); */ /* not ansi-c :/ */
			text_font_draw(st, TXT_OFFSET - 7, y, linenr);

			UI_ThemeColor(TH_TEXT);
		}

		if (st->wordwrap) {
			/* draw word wrapped text */
			int lines = text_draw_wrapped(st, tmp->line, x, y, winx - x, tmp->format, wrap_skip);
			y -= lines * (st->lheight + TXT_LINE_SPACING);
		}
		else {
			/* draw unwrapped text */
			text_draw(st, tmp->line, st->left, ar->winx / st->cwidth, 1, x, y, tmp->format);
			y -= st->lheight + TXT_LINE_SPACING;
		}
		
		wrap_skip = 0;
	}

	BLF_draw_unlock(mono);

	GPU_STRING_MARKER("draw_text_main:end");

	if (st->flags & ST_SHOW_MARGIN) {
		UI_ThemeColor(TH_HILITE);

		margin_column_x = x + st->cwidth * (st->margin_column - st->left);
		
		if (margin_column_x >= x) {
			gpuImmediateFormat_V2();
			gpuBegin(GL_LINES);
			gpuVertex2i(x + st->cwidth * st->margin_column, 0);
			gpuVertex2i(x + st->cwidth * st->margin_column, ar->winy - 2);
			gpuEnd();
			gpuImmediateUnformat();
		}
	}

	/* draw other stuff */
	draw_brackets(st, ar);
	gpuTranslate(GLA_PIXEL_OFS, GLA_PIXEL_OFS, 0.0f); /* XXX scroll requires exact pixel space */
	draw_textscroll(st, &scroll, &back);
	draw_documentation(st, ar);
	draw_suggestion_list(st, ar);
	
	text_font_end(st);
}

/************************** update ***************************/

void text_update_character_width(SpaceText *st)
{
	text_font_begin(st);
	st->cwidth = BLF_fixed_width(mono);
	st->cwidth = MAX2(st->cwidth, (char)1);
	text_font_end(st);
}

/* Moves the view to the cursor location,
 * also used to make sure the view isn't outside the file */
void text_scroll_to_cursor(SpaceText *st, ScrArea *sa)
{
	Text *text;
	ARegion *ar = NULL;
	int i, x, winx = 0;

	if (ELEM3(NULL, st, st->text, st->text->curl)) return;

	text = st->text;

	for (ar = sa->regionbase.first; ar; ar = ar->next)
		if (ar->regiontype == RGN_TYPE_WINDOW) {
			winx = ar->winx;
			break;
		}
	
	winx -= TXT_SCROLL_WIDTH;

	text_update_character_width(st);

	i = txt_get_span(text->lines.first, text->sell);
	if (st->wordwrap) {
		int offl, offc;
		wrap_offset(st, ar, text->sell, text->selc, &offl, &offc);
		i += offl;
	}

	if (st->top + st->viewlines <= i || st->top > i)
		st->top = i - st->viewlines / 2;
	
	if (st->wordwrap) {
		st->left = 0;
	}
	else {
		BLF_draw_lock(mono);
		x = text_draw(st, text->sell->line, st->left, text->selc, 0, 0, 0, NULL);
		BLF_draw_unlock(mono);

		if (x == 0 || x > winx)
			st->left = text->curc - 0.5 * winx / st->cwidth;
	}

	if (st->top < 0) st->top = 0;
	if (st->left < 0) st->left = 0;
}

void text_update_cursor_moved(bContext *C)
{
	ScrArea *sa = CTX_wm_area(C);
	SpaceText *st = CTX_wm_space_text(C);

	text_scroll_to_cursor(st, sa);
}
