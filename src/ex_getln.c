/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * ex_getln.c: Functions for entering and editing an Ex command line.
 */

#include "vim.h"

#ifndef MAX
# define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

// The current cmdline_info.  It is initialized in getcmdline() and after that
// used by other functions.  When invoking getcmdline() recursively it needs
// to be saved with save_cmdline() and restored with restore_cmdline().
static cmdline_info_T ccline;

#ifdef FEAT_EVAL
static int	new_cmdpos;	/* position set by set_cmdline_pos() */
#endif

static int	extra_char = NUL;  /* extra character to display when redrawing
				    * the command line */
static int	extra_char_shift;

#ifdef FEAT_RIGHTLEFT
static int	cmd_hkmap = 0;	/* Hebrew mapping during command line */
#endif

static char_u	*getcmdline_int(int firstc, long count, int indent, int init_ccline);
static int	cmdline_charsize(int idx);
static void	set_cmdspos(void);
static void	set_cmdspos_cursor(void);
static void	correct_cmdspos(int idx, int cells);
static void	alloc_cmdbuff(int len);
static void	draw_cmdline(int start, int len);
static void	save_cmdline(cmdline_info_T *ccp);
static void	restore_cmdline(cmdline_info_T *ccp);
static int	cmdline_paste(int regname, int literally, int remcr);
#ifdef FEAT_WILDMENU
static void	cmdline_del(int from);
#endif
static void	redrawcmdprompt(void);
static int	ccheck_abbr(int);

#ifdef FEAT_CMDWIN
static int	open_cmdwin(void);
#endif


    static void
trigger_cmd_autocmd(int typechar, int evt)
{
    char_u	typestr[2];

    typestr[0] = typechar;
    typestr[1] = NUL;
    apply_autocmds(evt, typestr, typestr, FALSE, curbuf);
}

/*
 * Abandon the command line.
 */
    static void
abandon_cmdline(void)
{
    VIM_CLEAR(ccline.cmdbuff);
    if (msg_scrolled == 0)
	compute_cmdrow();
    msg("");
    redraw_cmdline = TRUE;
}

#ifdef FEAT_SEARCH_EXTRA
/*
 * Guess that the pattern matches everything.  Only finds specific cases, such
 * as a trailing \|, which can happen while typing a pattern.
 */
    static int
empty_pattern(char_u *p)
{
    size_t n = STRLEN(p);

    /* remove trailing \v and the like */
    while (n >= 2 && p[n - 2] == '\\'
			  && vim_strchr((char_u *)"mMvVcCZ", p[n - 1]) != NULL)
	n -= 2;
    return n == 0 || (n >= 2 && p[n - 2] == '\\' && p[n - 1] == '|');
}

// Struct to store the viewstate during 'incsearch' highlighting.
typedef struct {
    colnr_T	vs_curswant;
    colnr_T	vs_leftcol;
    linenr_T	vs_topline;
# ifdef FEAT_DIFF
    int		vs_topfill;
# endif
    linenr_T	vs_botline;
    linenr_T	vs_empty_rows;
} viewstate_T;

    static void
save_viewstate(viewstate_T *vs)
{
    vs->vs_curswant = curwin->w_curswant;
    vs->vs_leftcol = curwin->w_leftcol;
    vs->vs_topline = curwin->w_topline;
# ifdef FEAT_DIFF
    vs->vs_topfill = curwin->w_topfill;
# endif
    vs->vs_botline = curwin->w_botline;
    vs->vs_empty_rows = curwin->w_empty_rows;
}

    static void
restore_viewstate(viewstate_T *vs)
{
    curwin->w_curswant = vs->vs_curswant;
    curwin->w_leftcol = vs->vs_leftcol;
    curwin->w_topline = vs->vs_topline;
# ifdef FEAT_DIFF
    curwin->w_topfill = vs->vs_topfill;
# endif
    curwin->w_botline = vs->vs_botline;
    curwin->w_empty_rows = vs->vs_empty_rows;
}

// Struct to store the state of 'incsearch' highlighting.
typedef struct {
    pos_T	search_start;	// where 'incsearch' starts searching
    pos_T	save_cursor;
    viewstate_T	init_viewstate;
    viewstate_T	old_viewstate;
    pos_T	match_start;
    pos_T	match_end;
    int		did_incsearch;
    int		incsearch_postponed;
    int		magic_save;
} incsearch_state_T;

    static void
init_incsearch_state(incsearch_state_T *is_state)
{
    is_state->match_start = curwin->w_cursor;
    is_state->did_incsearch = FALSE;
    is_state->incsearch_postponed = FALSE;
    is_state->magic_save = p_magic;
    CLEAR_POS(&is_state->match_end);
    is_state->save_cursor = curwin->w_cursor;  // may be restored later
    is_state->search_start = curwin->w_cursor;
    save_viewstate(&is_state->init_viewstate);
    save_viewstate(&is_state->old_viewstate);
}

/*
 * First move cursor to end of match, then to the start.  This
 * moves the whole match onto the screen when 'nowrap' is set.
 */
    static void
set_search_match(pos_T *t)
{
    t->lnum += search_match_lines;
    t->col = search_match_endcol;
    if (t->lnum > curbuf->b_ml.ml_line_count)
    {
	t->lnum = curbuf->b_ml.ml_line_count;
	coladvance((colnr_T)MAXCOL);
    }
}

/*
 * Return TRUE when 'incsearch' highlighting is to be done.
 * Sets search_first_line and search_last_line to the address range.
 * May change the last search pattern.
 */
    static int
do_incsearch_highlighting(int firstc, incsearch_state_T *is_state,
						     int *skiplen, int *patlen)
{
    char_u	*cmd;
    cmdmod_T	save_cmdmod = cmdmod;
    char_u	*p;
    int		delim_optional = FALSE;
    int		delim;
    char_u	*end;
    char	*dummy;
    exarg_T	ea;
    pos_T	save_cursor;
    int		use_last_pat;

    *skiplen = 0;
    *patlen = ccline.cmdlen;

    if (!p_is || cmd_silent)
	return FALSE;

    // by default search all lines
    search_first_line = 0;
    search_last_line = MAXLNUM;

    if (firstc == '/' || firstc == '?')
	return TRUE;
    if (firstc != ':')
	return FALSE;

    vim_memset(&ea, 0, sizeof(ea));
    ea.line1 = 1;
    ea.line2 = 1;
    ea.cmd = ccline.cmdbuff;
    ea.addr_type = ADDR_LINES;

    parse_command_modifiers(&ea, &dummy, TRUE);
    cmdmod = save_cmdmod;

    cmd = skip_range(ea.cmd, NULL);
    if (vim_strchr((char_u *)"sgvl", *cmd) == NULL)
	return FALSE;

    // Skip over "substitute" to find the pattern separator.
    for (p = cmd; ASCII_ISALPHA(*p); ++p)
	;
    if (*skipwhite(p) == NUL)
	return FALSE;

    if (STRNCMP(cmd, "substitute", p - cmd) == 0
	    || STRNCMP(cmd, "smagic", p - cmd) == 0
	    || STRNCMP(cmd, "snomagic", MAX(p - cmd, 3)) == 0
	    || STRNCMP(cmd, "vglobal", p - cmd) == 0)
    {
	if (*cmd == 's' && cmd[1] == 'm')
	    p_magic = TRUE;
	else if (*cmd == 's' && cmd[1] == 'n')
	    p_magic = FALSE;
    }
    else if (STRNCMP(cmd, "sort", MAX(p - cmd, 3)) == 0)
    {
	// skip over flags
	while (ASCII_ISALPHA(*(p = skipwhite(p))))
	    ++p;
	if (*p == NUL)
	    return FALSE;
    }
    else if (STRNCMP(cmd, "vimgrep", MAX(p - cmd, 3)) == 0
	|| STRNCMP(cmd, "vimgrepadd", MAX(p - cmd, 8)) == 0
	|| STRNCMP(cmd, "lvimgrep", MAX(p - cmd, 2)) == 0
	|| STRNCMP(cmd, "lvimgrepadd", MAX(p - cmd, 9)) == 0
	|| STRNCMP(cmd, "global", p - cmd) == 0)
    {
	// skip over "!"
	if (*p == '!')
	{
	    p++;
	    if (*skipwhite(p) == NUL)
		return FALSE;
	}
	if (*cmd != 'g')
	    delim_optional = TRUE;
    }
    else
	return FALSE;

    p = skipwhite(p);
    delim = (delim_optional && vim_isIDc(*p)) ? ' ' : *p++;
    end = skip_regexp(p, delim, p_magic, NULL);

    use_last_pat = end == p && *end == delim;

    if (end == p && !use_last_pat)
	return FALSE;

    // Don't do 'hlsearch' highlighting if the pattern matches everything.
    if (!use_last_pat)
    {
	char c = *end;
	int  empty;

	*end = NUL;
	empty = empty_pattern(p);
	*end = c;
	if (empty)
	    return FALSE;
    }

    // found a non-empty pattern or //
    *skiplen = (int)(p - ccline.cmdbuff);
    *patlen = (int)(end - p);

    // parse the address range
    save_cursor = curwin->w_cursor;
    curwin->w_cursor = is_state->search_start;
    parse_cmd_address(&ea, &dummy, TRUE);
    if (ea.addr_count > 0)
    {
	// Allow for reverse match.
	if (ea.line2 < ea.line1)
	{
	    search_first_line = ea.line2;
	    search_last_line = ea.line1;
	}
	else
	{
	    search_first_line = ea.line1;
	    search_last_line = ea.line2;
	}
    }
    else if (cmd[0] == 's' && cmd[1] != 'o')
    {
	// :s defaults to the current line
	search_first_line = curwin->w_cursor.lnum;
	search_last_line = curwin->w_cursor.lnum;
    }

    curwin->w_cursor = save_cursor;
    return TRUE;
}

    static void
finish_incsearch_highlighting(
	int gotesc,
	incsearch_state_T *is_state,
	int call_update_screen)
{
    if (is_state->did_incsearch)
    {
	is_state->did_incsearch = FALSE;
	if (gotesc)
	    curwin->w_cursor = is_state->save_cursor;
	else
	{
	    if (!EQUAL_POS(is_state->save_cursor, is_state->search_start))
	    {
		// put the '" mark at the original position
		curwin->w_cursor = is_state->save_cursor;
		setpcmark();
	    }
	    curwin->w_cursor = is_state->search_start;
	}
	restore_viewstate(&is_state->old_viewstate);
	highlight_match = FALSE;

	// by default search all lines
	search_first_line = 0;
	search_last_line = MAXLNUM;

	p_magic = is_state->magic_save;

	validate_cursor();	/* needed for TAB */
	redraw_all_later(SOME_VALID);
	if (call_update_screen)
	    update_screen(SOME_VALID);
    }
}

/*
 * Do 'incsearch' highlighting if desired.
 */
    static void
may_do_incsearch_highlighting(
	int		    firstc,
	long		    count,
	incsearch_state_T   *is_state)
{
    int		skiplen, patlen;
    int		found;  // do_search() result
    pos_T	end_pos;
#ifdef FEAT_RELTIME
    proftime_T	tm;
    searchit_arg_T sia;
#endif
    int		next_char;
    int		use_last_pat;
    int		did_do_incsearch = is_state->did_incsearch;

    // Parsing range may already set the last search pattern.
    // NOTE: must call restore_last_search_pattern() before returning!
    save_last_search_pattern();

    if (!do_incsearch_highlighting(firstc, is_state, &skiplen, &patlen))
    {
	restore_last_search_pattern();
	finish_incsearch_highlighting(FALSE, is_state, TRUE);
	if (did_do_incsearch && vpeekc() == NUL)
	    // may have skipped a redraw, do it now
	    redrawcmd();
	return;
    }

    // If there is a character waiting, search and redraw later.
    if (char_avail())
    {
	restore_last_search_pattern();
	is_state->incsearch_postponed = TRUE;
	return;
    }
    is_state->incsearch_postponed = FALSE;

    if (search_first_line == 0)
	// start at the original cursor position
	curwin->w_cursor = is_state->search_start;
    else if (search_first_line > curbuf->b_ml.ml_line_count)
    {
	// start after the last line
	curwin->w_cursor.lnum = curbuf->b_ml.ml_line_count;
	curwin->w_cursor.col = MAXCOL;
    }
    else
    {
	// start at the first line in the range
	curwin->w_cursor.lnum = search_first_line;
	curwin->w_cursor.col = 0;
    }

    // Use the previous pattern for ":s//".
    next_char = ccline.cmdbuff[skiplen + patlen];
    use_last_pat = patlen == 0 && skiplen > 0
				   && ccline.cmdbuff[skiplen - 1] == next_char;

    // If there is no pattern, don't do anything.
    if (patlen == 0 && !use_last_pat)
    {
	found = 0;
	set_no_hlsearch(TRUE); // turn off previous highlight
	redraw_all_later(SOME_VALID);
    }
    else
    {
	int search_flags = SEARCH_OPT + SEARCH_NOOF + SEARCH_PEEK;

	cursor_off();	// so the user knows we're busy
	out_flush();
	++emsg_off;	// so it doesn't beep if bad expr
#ifdef FEAT_RELTIME
	// Set the time limit to half a second.
	profile_setlimit(500L, &tm);
#endif
	if (!p_hls)
	    search_flags += SEARCH_KEEP;
	if (search_first_line != 0)
	    search_flags += SEARCH_START;
	ccline.cmdbuff[skiplen + patlen] = NUL;
#ifdef FEAT_RELTIME
	vim_memset(&sia, 0, sizeof(sia));
	sia.sa_tm = &tm;
#endif
	found = do_search(NULL, firstc == ':' ? '/' : firstc,
				 ccline.cmdbuff + skiplen, count, search_flags,
#ifdef FEAT_RELTIME
		&sia
#else
		NULL
#endif
		);
	ccline.cmdbuff[skiplen + patlen] = next_char;
	--emsg_off;

	if (curwin->w_cursor.lnum < search_first_line
		|| curwin->w_cursor.lnum > search_last_line)
	{
	    // match outside of address range
	    found = 0;
	    curwin->w_cursor = is_state->search_start;
	}

	// if interrupted while searching, behave like it failed
	if (got_int)
	{
	    (void)vpeekc();	// remove <C-C> from input stream
	    got_int = FALSE;	// don't abandon the command line
	    found = 0;
	}
	else if (char_avail())
	    // cancelled searching because a char was typed
	    is_state->incsearch_postponed = TRUE;
    }
    if (found != 0)
	highlight_match = TRUE;		// highlight position
    else
	highlight_match = FALSE;	// remove highlight

    // First restore the old curwin values, so the screen is positioned in the
    // same way as the actual search command.
    restore_viewstate(&is_state->old_viewstate);
    changed_cline_bef_curs();
    update_topline();

    if (found != 0)
    {
	pos_T	    save_pos = curwin->w_cursor;

	is_state->match_start = curwin->w_cursor;
	set_search_match(&curwin->w_cursor);
	validate_cursor();
	end_pos = curwin->w_cursor;
	is_state->match_end = end_pos;
	curwin->w_cursor = save_pos;
    }
    else
	end_pos = curwin->w_cursor; // shutup gcc 4

    // Disable 'hlsearch' highlighting if the pattern matches everything.
    // Avoids a flash when typing "foo\|".
    if (!use_last_pat)
    {
	next_char = ccline.cmdbuff[skiplen + patlen];
	ccline.cmdbuff[skiplen + patlen] = NUL;
	if (empty_pattern(ccline.cmdbuff) && !no_hlsearch)
	{
	    redraw_all_later(SOME_VALID);
	    set_no_hlsearch(TRUE);
	}
	ccline.cmdbuff[skiplen + patlen] = next_char;
    }

    validate_cursor();
    // May redraw the status line to show the cursor position.
    if (p_ru && curwin->w_status_height > 0)
	curwin->w_redr_status = TRUE;

    update_screen(SOME_VALID);
    highlight_match = FALSE;
    restore_last_search_pattern();

    // Leave it at the end to make CTRL-R CTRL-W work.  But not when beyond the
    // end of the pattern, e.g. for ":s/pat/".
    if (ccline.cmdbuff[skiplen + patlen] != NUL)
	curwin->w_cursor = is_state->search_start;
    else if (found != 0)
	curwin->w_cursor = end_pos;

    msg_starthere();
    redrawcmdline();
    is_state->did_incsearch = TRUE;
}

/*
 * May adjust 'incsearch' highlighting for typing CTRL-G and CTRL-T, go to next
 * or previous match.
 * Returns FAIL when jumping to cmdline_not_changed;
 */
    static int
may_adjust_incsearch_highlighting(
	int			firstc,
	long			count,
	incsearch_state_T	*is_state,
	int			c)
{
    int	    skiplen, patlen;
    pos_T   t;
    char_u  *pat;
    int	    search_flags = SEARCH_NOOF;
    int	    i;
    int	    save;

    // Parsing range may already set the last search pattern.
    // NOTE: must call restore_last_search_pattern() before returning!
    save_last_search_pattern();

    if (!do_incsearch_highlighting(firstc, is_state, &skiplen, &patlen))
    {
	restore_last_search_pattern();
	return OK;
    }
    if (patlen == 0 && ccline.cmdbuff[skiplen] == NUL)
    {
	restore_last_search_pattern();
	return FAIL;
    }

    if (firstc == ccline.cmdbuff[skiplen])
    {
	pat = last_search_pattern();
	skiplen = 0;
	patlen = (int)STRLEN(pat);
    }
    else
	pat = ccline.cmdbuff + skiplen;

    cursor_off();
    out_flush();
    if (c == Ctrl_G)
    {
	t = is_state->match_end;
	if (LT_POS(is_state->match_start, is_state->match_end))
	    // Start searching at the end of the match not at the beginning of
	    // the next column.
	    (void)decl(&t);
	search_flags += SEARCH_COL;
    }
    else
	t = is_state->match_start;
    if (!p_hls)
	search_flags += SEARCH_KEEP;
    ++emsg_off;
    save = pat[patlen];
    pat[patlen] = NUL;
    i = searchit(curwin, curbuf, &t, NULL,
		 c == Ctrl_G ? FORWARD : BACKWARD,
		 pat, count, search_flags, RE_SEARCH, NULL);
    --emsg_off;
    pat[patlen] = save;
    if (i)
    {
	is_state->search_start = is_state->match_start;
	is_state->match_end = t;
	is_state->match_start = t;
	if (c == Ctrl_T && firstc != '?')
	{
	    // Move just before the current match, so that when nv_search
	    // finishes the cursor will be put back on the match.
	    is_state->search_start = t;
	    (void)decl(&is_state->search_start);
	}
	else if (c == Ctrl_G && firstc == '?')
	{
	    // Move just after the current match, so that when nv_search
	    // finishes the cursor will be put back on the match.
	    is_state->search_start = t;
	    (void)incl(&is_state->search_start);
	}
	if (LT_POS(t, is_state->search_start) && c == Ctrl_G)
	{
	    // wrap around
	    is_state->search_start = t;
	    if (firstc == '?')
		(void)incl(&is_state->search_start);
	    else
		(void)decl(&is_state->search_start);
	}

	set_search_match(&is_state->match_end);
	curwin->w_cursor = is_state->match_start;
	changed_cline_bef_curs();
	update_topline();
	validate_cursor();
	highlight_match = TRUE;
	save_viewstate(&is_state->old_viewstate);
	update_screen(NOT_VALID);
	highlight_match = FALSE;
	redrawcmdline();
	curwin->w_cursor = is_state->match_end;
    }
    else
	vim_beep(BO_ERROR);
    restore_last_search_pattern();
    return FAIL;
}

/*
 * When CTRL-L typed: add character from the match to the pattern.
 * May set "*c" to the added character.
 * Return OK when jumping to cmdline_not_changed.
 */
    static int
may_add_char_to_search(int firstc, int *c, incsearch_state_T *is_state)
{
    int		skiplen, patlen;

    // Parsing range may already set the last search pattern.
    // NOTE: must call restore_last_search_pattern() before returning!
    save_last_search_pattern();

    if (!do_incsearch_highlighting(firstc, is_state, &skiplen, &patlen))
    {
	restore_last_search_pattern();
	return FAIL;
    }
    restore_last_search_pattern();

    // Add a character from under the cursor for 'incsearch'.
    if (is_state->did_incsearch)
    {
	curwin->w_cursor = is_state->match_end;
	*c = gchar_cursor();
	if (*c != NUL)
	{
	    // If 'ignorecase' and 'smartcase' are set and the
	    // command line has no uppercase characters, convert
	    // the character to lowercase.
	    if (p_ic && p_scs && !pat_has_uppercase(ccline.cmdbuff + skiplen))
		*c = MB_TOLOWER(*c);
	    if (*c == firstc || vim_strchr((char_u *)(
			       p_magic ? "\\~^$.*[" : "\\^$"), *c) != NULL)
	    {
		// put a backslash before special characters
		stuffcharReadbuff(*c);
		*c = '\\';
	    }
	    // add any composing characters
	    if (mb_char2len(*c) != mb_ptr2len(ml_get_cursor()))
	    {
		int save_c = *c;

		while (mb_char2len(*c) != mb_ptr2len(ml_get_cursor()))
		{
		    curwin->w_cursor.col += mb_char2len(*c);
		    *c = gchar_cursor();
		    stuffcharReadbuff(*c);
		}
		*c = save_c;
	    }
	    return FAIL;
	}
    }
    return OK;
}
#endif

#ifdef FEAT_ARABIC
/*
 * Return TRUE if the command line has an Arabic character at or after "start"
 * for "len" bytes.
 */
    static int
cmdline_has_arabic(int start, int len)
{
    int	    j;
    int	    mb_l;
    int	    u8c;
    char_u  *p;
    int	    u8cc[MAX_MCO];

    if (!enc_utf8)
	return FALSE;

    for (j = start; j < start + len; j += mb_l)
    {
	p = ccline.cmdbuff + j;
	u8c = utfc_ptr2char_len(p, u8cc, start + len - j);
	mb_l = utfc_ptr2len_len(p, start + len - j);
	if (ARABIC_CHAR(u8c))
	    return TRUE;
    }
    return FALSE;
}
#endif

    void
cmdline_init(void)
{
    vim_memset(&ccline, 0, sizeof(cmdline_info_T));
}

/*
 * getcmdline() - accept a command line starting with firstc.
 *
 * firstc == ':'	    get ":" command line.
 * firstc == '/' or '?'	    get search pattern
 * firstc == '='	    get expression
 * firstc == '@'	    get text for input() function
 * firstc == '>'	    get text for debug mode
 * firstc == NUL	    get text for :insert command
 * firstc == -1		    like NUL, and break on CTRL-C
 *
 * The line is collected in ccline.cmdbuff, which is reallocated to fit the
 * command line.
 *
 * Careful: getcmdline() can be called recursively!
 *
 * Return pointer to allocated string if there is a commandline, NULL
 * otherwise.
 */
    char_u *
getcmdline(
    int		firstc,
    long	count,		// only used for incremental search
    int		indent,		// indent for inside conditionals
    int		do_concat UNUSED)
{
    return getcmdline_int(firstc, count, indent, TRUE);
}

    static char_u *
getcmdline_int(
    int		firstc,
    long	count UNUSED,	// only used for incremental search
    int		indent,		// indent for inside conditionals
    int		init_ccline)	// clear ccline first
{
    int		c;
    int		i;
    int		j;
    int		gotesc = FALSE;		/* TRUE when <ESC> just typed */
    int		do_abbr;		/* when TRUE check for abbr. */
    char_u	*lookfor = NULL;	/* string to match */
    int		hiscnt;			/* current history line in use */
    int		histype;		/* history type to be used */
#ifdef FEAT_SEARCH_EXTRA
    incsearch_state_T	is_state;
#endif
    int		did_wild_list = FALSE;	/* did wild_list() recently */
    int		wim_index = 0;		/* index in wim_flags[] */
    int		res;
    int		save_msg_scroll = msg_scroll;
    int		save_State = State;	/* remember State when called */
    int		some_key_typed = FALSE;	/* one of the keys was typed */
    /* mouse drag and release events are ignored, unless they are
     * preceded with a mouse down event */
    int		ignore_drag_release = TRUE;
#ifdef FEAT_EVAL
    int		break_ctrl_c = FALSE;
#endif
    expand_T	xpc;
    long	*b_im_ptr = NULL;
    cmdline_info_T save_ccline;
    int		did_save_ccline = FALSE;
    int		cmdline_type;

    if (ccline.cmdbuff != NULL)
    {
	// Being called recursively.  Since ccline is global, we need to save
	// the current buffer and restore it when returning.
	save_cmdline(&save_ccline);
	did_save_ccline = TRUE;
    }
    if (init_ccline)
	vim_memset(&ccline, 0, sizeof(cmdline_info_T));

#ifdef FEAT_EVAL
    if (firstc == -1)
    {
	firstc = NUL;
	break_ctrl_c = TRUE;
    }
#endif
#ifdef FEAT_RIGHTLEFT
    /* start without Hebrew mapping for a command line */
    if (firstc == ':' || firstc == '=' || firstc == '>')
	cmd_hkmap = 0;
#endif

    ccline.overstrike = FALSE;		    /* always start in insert mode */

#ifdef FEAT_SEARCH_EXTRA
    init_incsearch_state(&is_state);
#endif

    /*
     * set some variables for redrawcmd()
     */
    ccline.cmdfirstc = (firstc == '@' ? 0 : firstc);
    ccline.cmdindent = (firstc > 0 ? indent : 0);

    /* alloc initial ccline.cmdbuff */
    alloc_cmdbuff(exmode_active ? 250 : indent + 1);
    if (ccline.cmdbuff == NULL)
	goto theend;	// out of memory
    ccline.cmdlen = ccline.cmdpos = 0;
    ccline.cmdbuff[0] = NUL;
    sb_text_start_cmdline();

    /* autoindent for :insert and :append */
    if (firstc <= 0)
    {
	vim_memset(ccline.cmdbuff, ' ', indent);
	ccline.cmdbuff[indent] = NUL;
	ccline.cmdpos = indent;
	ccline.cmdspos = indent;
	ccline.cmdlen = indent;
    }

    ExpandInit(&xpc);
    ccline.xpc = &xpc;

#ifdef FEAT_RIGHTLEFT
    if (curwin->w_p_rl && *curwin->w_p_rlc == 's'
					  && (firstc == '/' || firstc == '?'))
	cmdmsg_rl = TRUE;
    else
	cmdmsg_rl = FALSE;
#endif

    redir_off = TRUE;		/* don't redirect the typed command */
    if (!cmd_silent)
    {
	i = msg_scrolled;
	msg_scrolled = 0;		/* avoid wait_return message */
	gotocmdline(TRUE);
	msg_scrolled += i;
	redrawcmdprompt();		/* draw prompt or indent */
	set_cmdspos();
    }
    xpc.xp_context = EXPAND_NOTHING;
    xpc.xp_backslash = XP_BS_NONE;
#ifndef BACKSLASH_IN_FILENAME
    xpc.xp_shell = FALSE;
#endif

#if defined(FEAT_EVAL)
    if (ccline.input_fn)
    {
	xpc.xp_context = ccline.xp_context;
	xpc.xp_pattern = ccline.cmdbuff;
	xpc.xp_arg = ccline.xp_arg;
    }
#endif

    /*
     * Avoid scrolling when called by a recursive do_cmdline(), e.g. when
     * doing ":@0" when register 0 doesn't contain a CR.
     */
    msg_scroll = FALSE;

    State = CMDLINE;

    if (firstc == '/' || firstc == '?' || firstc == '@')
    {
	/* Use ":lmap" mappings for search pattern and input(). */
	if (curbuf->b_p_imsearch == B_IMODE_USE_INSERT)
	    b_im_ptr = &curbuf->b_p_iminsert;
	else
	    b_im_ptr = &curbuf->b_p_imsearch;
	if (*b_im_ptr == B_IMODE_LMAP)
	    State |= LANGMAP;
#ifdef HAVE_INPUT_METHOD
	im_set_active(*b_im_ptr == B_IMODE_IM);
#endif
    }
#ifdef HAVE_INPUT_METHOD
    else if (p_imcmdline)
	im_set_active(TRUE);
#endif

    setmouse();
#ifdef CURSOR_SHAPE
    ui_cursor_shape();		/* may show different cursor shape */
#endif

    /* When inside an autocommand for writing "exiting" may be set and
     * terminal mode set to cooked.  Need to set raw mode here then. */
    settmode(TMODE_RAW);

    /* Trigger CmdlineEnter autocommands. */
    cmdline_type = firstc == NUL ? '-' : firstc;
    trigger_cmd_autocmd(cmdline_type, EVENT_CMDLINEENTER);

    init_history();
    hiscnt = get_hislen();	/* set hiscnt to impossible history value */
    histype = hist_char2type(firstc);

#ifdef FEAT_DIGRAPHS
    do_digraph(-1);		/* init digraph typeahead */
#endif

    /* If something above caused an error, reset the flags, we do want to type
     * and execute commands. Display may be messed up a bit. */
    if (did_emsg)
	redrawcmd();
    did_emsg = FALSE;
    got_int = FALSE;

    /*
     * Collect the command string, handling editing keys.
     */
    for (;;)
    {
	redir_off = TRUE;	/* Don't redirect the typed command.
				   Repeated, because a ":redir" inside
				   completion may switch it on. */
#ifdef USE_ON_FLY_SCROLL
	dont_scroll = FALSE;	/* allow scrolling here */
#endif
	quit_more = FALSE;	/* reset after CTRL-D which had a more-prompt */

	did_emsg = FALSE;	/* There can't really be a reason why an error
				   that occurs while typing a command should
				   cause the command not to be executed. */

	// Trigger SafeState if nothing is pending.
	may_trigger_safestate(xpc.xp_numfiles <= 0);

	cursorcmd();		/* set the cursor on the right spot */

	/* Get a character.  Ignore K_IGNORE and K_NOP, they should not do
	 * anything, such as stop completion. */
	do
	    c = safe_vgetc();
	while (c == K_IGNORE || c == K_NOP);

	if (KeyTyped)
	{
	    some_key_typed = TRUE;
#ifdef FEAT_RIGHTLEFT
	    if (cmd_hkmap)
		c = hkmap(c);
	    if (cmdmsg_rl && !KeyStuffed)
	    {
		/* Invert horizontal movements and operations.  Only when
		 * typed by the user directly, not when the result of a
		 * mapping. */
		switch (c)
		{
		    case K_RIGHT:   c = K_LEFT; break;
		    case K_S_RIGHT: c = K_S_LEFT; break;
		    case K_C_RIGHT: c = K_C_LEFT; break;
		    case K_LEFT:    c = K_RIGHT; break;
		    case K_S_LEFT:  c = K_S_RIGHT; break;
		    case K_C_LEFT:  c = K_C_RIGHT; break;
		}
	    }
#endif
	}

	/*
	 * Ignore got_int when CTRL-C was typed here.
	 * Don't ignore it in :global, we really need to break then, e.g., for
	 * ":g/pat/normal /pat" (without the <CR>).
	 * Don't ignore it for the input() function.
	 */
	if ((c == Ctrl_C
#ifdef UNIX
		|| c == intr_char
#endif
				)
#if defined(FEAT_EVAL) || defined(FEAT_CRYPT)
		&& firstc != '@'
#endif
#ifdef FEAT_EVAL
		&& !break_ctrl_c
#endif
		&& !global_busy)
	    got_int = FALSE;

	// free old command line when finished moving around in the history
	// list
	if (lookfor != NULL
		&& c != K_S_DOWN && c != K_S_UP
		&& c != K_DOWN && c != K_UP
		&& c != K_PAGEDOWN && c != K_PAGEUP
		&& c != K_KPAGEDOWN && c != K_KPAGEUP
		&& c != K_LEFT && c != K_RIGHT
		&& (xpc.xp_numfiles > 0 || (c != Ctrl_P && c != Ctrl_N)))
	    VIM_CLEAR(lookfor);

	/*
	 * When there are matching completions to select <S-Tab> works like
	 * CTRL-P (unless 'wc' is <S-Tab>).
	 */
	if (c != p_wc && c == K_S_TAB && xpc.xp_numfiles > 0)
	    c = Ctrl_P;

#ifdef FEAT_WILDMENU
	/* Special translations for 'wildmenu' */
	if (did_wild_list && p_wmnu)
	{
	    if (c == K_LEFT)
		c = Ctrl_P;
	    else if (c == K_RIGHT)
		c = Ctrl_N;
	}
	/* Hitting CR after "emenu Name.": complete submenu */
	if (xpc.xp_context == EXPAND_MENUNAMES && p_wmnu
		&& ccline.cmdpos > 1
		&& ccline.cmdbuff[ccline.cmdpos - 1] == '.'
		&& ccline.cmdbuff[ccline.cmdpos - 2] != '\\'
		&& (c == '\n' || c == '\r' || c == K_KENTER))
	    c = K_DOWN;
#endif

	/* free expanded names when finished walking through matches */
	if (xpc.xp_numfiles != -1
		&& !(c == p_wc && KeyTyped) && c != p_wcm
		&& c != Ctrl_N && c != Ctrl_P && c != Ctrl_A
		&& c != Ctrl_L)
	{
	    (void)ExpandOne(&xpc, NULL, NULL, 0, WILD_FREE);
	    did_wild_list = FALSE;
#ifdef FEAT_WILDMENU
	    if (!p_wmnu || (c != K_UP && c != K_DOWN))
#endif
		xpc.xp_context = EXPAND_NOTHING;
	    wim_index = 0;
#ifdef FEAT_WILDMENU
	    if (p_wmnu && wild_menu_showing != 0)
	    {
		int skt = KeyTyped;
		int old_RedrawingDisabled = RedrawingDisabled;

		if (ccline.input_fn)
		    RedrawingDisabled = 0;

		if (wild_menu_showing == WM_SCROLLED)
		{
		    /* Entered command line, move it up */
		    cmdline_row--;
		    redrawcmd();
		}
		else if (save_p_ls != -1)
		{
		    /* restore 'laststatus' and 'winminheight' */
		    p_ls = save_p_ls;
		    p_wmh = save_p_wmh;
		    last_status(FALSE);
		    update_screen(VALID);	/* redraw the screen NOW */
		    redrawcmd();
		    save_p_ls = -1;
		}
		else
		{
		    win_redraw_last_status(topframe);
		    redraw_statuslines();
		}
		KeyTyped = skt;
		wild_menu_showing = 0;
		if (ccline.input_fn)
		    RedrawingDisabled = old_RedrawingDisabled;
	    }
#endif
	}

#ifdef FEAT_WILDMENU
	/* Special translations for 'wildmenu' */
	if (xpc.xp_context == EXPAND_MENUNAMES && p_wmnu)
	{
	    /* Hitting <Down> after "emenu Name.": complete submenu */
	    if (c == K_DOWN && ccline.cmdpos > 0
				  && ccline.cmdbuff[ccline.cmdpos - 1] == '.')
		c = p_wc;
	    else if (c == K_UP)
	    {
		/* Hitting <Up>: Remove one submenu name in front of the
		 * cursor */
		int found = FALSE;

		j = (int)(xpc.xp_pattern - ccline.cmdbuff);
		i = 0;
		while (--j > 0)
		{
		    /* check for start of menu name */
		    if (ccline.cmdbuff[j] == ' '
			    && ccline.cmdbuff[j - 1] != '\\')
		    {
			i = j + 1;
			break;
		    }
		    /* check for start of submenu name */
		    if (ccline.cmdbuff[j] == '.'
			    && ccline.cmdbuff[j - 1] != '\\')
		    {
			if (found)
			{
			    i = j + 1;
			    break;
			}
			else
			    found = TRUE;
		    }
		}
		if (i > 0)
		    cmdline_del(i);
		c = p_wc;
		xpc.xp_context = EXPAND_NOTHING;
	    }
	}
	if ((xpc.xp_context == EXPAND_FILES
			      || xpc.xp_context == EXPAND_DIRECTORIES
			      || xpc.xp_context == EXPAND_SHELLCMD) && p_wmnu)
	{
	    char_u upseg[5];

	    upseg[0] = PATHSEP;
	    upseg[1] = '.';
	    upseg[2] = '.';
	    upseg[3] = PATHSEP;
	    upseg[4] = NUL;

	    if (c == K_DOWN
		    && ccline.cmdpos > 0
		    && ccline.cmdbuff[ccline.cmdpos - 1] == PATHSEP
		    && (ccline.cmdpos < 3
			|| ccline.cmdbuff[ccline.cmdpos - 2] != '.'
			|| ccline.cmdbuff[ccline.cmdpos - 3] != '.'))
	    {
		/* go down a directory */
		c = p_wc;
	    }
	    else if (STRNCMP(xpc.xp_pattern, upseg + 1, 3) == 0 && c == K_DOWN)
	    {
		/* If in a direct ancestor, strip off one ../ to go down */
		int found = FALSE;

		j = ccline.cmdpos;
		i = (int)(xpc.xp_pattern - ccline.cmdbuff);
		while (--j > i)
		{
		    if (has_mbyte)
			j -= (*mb_head_off)(ccline.cmdbuff, ccline.cmdbuff + j);
		    if (vim_ispathsep(ccline.cmdbuff[j]))
		    {
			found = TRUE;
			break;
		    }
		}
		if (found
			&& ccline.cmdbuff[j - 1] == '.'
			&& ccline.cmdbuff[j - 2] == '.'
			&& (vim_ispathsep(ccline.cmdbuff[j - 3]) || j == i + 2))
		{
		    cmdline_del(j - 2);
		    c = p_wc;
		}
	    }
	    else if (c == K_UP)
	    {
		/* go up a directory */
		int found = FALSE;

		j = ccline.cmdpos - 1;
		i = (int)(xpc.xp_pattern - ccline.cmdbuff);
		while (--j > i)
		{
		    if (has_mbyte)
			j -= (*mb_head_off)(ccline.cmdbuff, ccline.cmdbuff + j);
		    if (vim_ispathsep(ccline.cmdbuff[j])
#ifdef BACKSLASH_IN_FILENAME
			    && vim_strchr((char_u *)" *?[{`$%#",
				ccline.cmdbuff[j + 1]) == NULL
#endif
		       )
		    {
			if (found)
			{
			    i = j + 1;
			    break;
			}
			else
			    found = TRUE;
		    }
		}

		if (!found)
		    j = i;
		else if (STRNCMP(ccline.cmdbuff + j, upseg, 4) == 0)
		    j += 4;
		else if (STRNCMP(ccline.cmdbuff + j, upseg + 1, 3) == 0
			     && j == i)
		    j += 3;
		else
		    j = 0;
		if (j > 0)
		{
		    /* TODO this is only for DOS/UNIX systems - need to put in
		     * machine-specific stuff here and in upseg init */
		    cmdline_del(j);
		    put_on_cmdline(upseg + 1, 3, FALSE);
		}
		else if (ccline.cmdpos > i)
		    cmdline_del(i);

		/* Now complete in the new directory. Set KeyTyped in case the
		 * Up key came from a mapping. */
		c = p_wc;
		KeyTyped = TRUE;
	    }
	}

#endif	/* FEAT_WILDMENU */

	/* CTRL-\ CTRL-N goes to Normal mode, CTRL-\ CTRL-G goes to Insert
	 * mode when 'insertmode' is set, CTRL-\ e prompts for an expression. */
	if (c == Ctrl_BSL)
	{
	    ++no_mapping;
	    ++allow_keys;
	    c = plain_vgetc();
	    --no_mapping;
	    --allow_keys;
	    /* CTRL-\ e doesn't work when obtaining an expression, unless it
	     * is in a mapping. */
	    if (c != Ctrl_N && c != Ctrl_G && (c != 'e'
				    || (ccline.cmdfirstc == '=' && KeyTyped)
#ifdef FEAT_EVAL
				    || cmdline_star > 0
#endif
				    ))
	    {
		vungetc(c);
		c = Ctrl_BSL;
	    }
#ifdef FEAT_EVAL
	    else if (c == 'e')
	    {
		char_u	*p = NULL;
		int	len;

		/*
		 * Replace the command line with the result of an expression.
		 * Need to save and restore the current command line, to be
		 * able to enter a new one...
		 */
		if (ccline.cmdpos == ccline.cmdlen)
		    new_cmdpos = 99999;	/* keep it at the end */
		else
		    new_cmdpos = ccline.cmdpos;

		c = get_expr_register();
		if (c == '=')
		{
		    /* Need to save and restore ccline.  And set "textlock"
		     * to avoid nasty things like going to another buffer when
		     * evaluating an expression. */
		    ++textlock;
		    p = get_expr_line();
		    --textlock;

		    if (p != NULL)
		    {
			len = (int)STRLEN(p);
			if (realloc_cmdbuff(len + 1) == OK)
			{
			    ccline.cmdlen = len;
			    STRCPY(ccline.cmdbuff, p);
			    vim_free(p);

			    /* Restore the cursor or use the position set with
			     * set_cmdline_pos(). */
			    if (new_cmdpos > ccline.cmdlen)
				ccline.cmdpos = ccline.cmdlen;
			    else
				ccline.cmdpos = new_cmdpos;

			    KeyTyped = FALSE;	/* Don't do p_wc completion. */
			    redrawcmd();
			    goto cmdline_changed;
			}
			vim_free(p);
		    }
		}
		beep_flush();
		got_int = FALSE;	/* don't abandon the command line */
		did_emsg = FALSE;
		emsg_on_display = FALSE;
		redrawcmd();
		goto cmdline_not_changed;
	    }
#endif
	    else
	    {
		if (c == Ctrl_G && p_im && restart_edit == 0)
		    restart_edit = 'a';
		gotesc = TRUE;	/* will free ccline.cmdbuff after putting it
				   in history */
		goto returncmd;	/* back to Normal mode */
	    }
	}

#ifdef FEAT_CMDWIN
	if (c == cedit_key || c == K_CMDWIN)
	{
	    if (ex_normal_busy == 0 && got_int == FALSE)
	    {
		/*
		 * Open a window to edit the command line (and history).
		 */
		c = open_cmdwin();
		some_key_typed = TRUE;
	    }
	}
# ifdef FEAT_DIGRAPHS
	else
# endif
#endif
#ifdef FEAT_DIGRAPHS
	    c = do_digraph(c);
#endif

	if (c == '\n' || c == '\r' || c == K_KENTER || (c == ESC
			&& (!KeyTyped || vim_strchr(p_cpo, CPO_ESC) != NULL)))
	{
	    /* In Ex mode a backslash escapes a newline. */
	    if (exmode_active
		    && c != ESC
		    && ccline.cmdpos == ccline.cmdlen
		    && ccline.cmdpos > 0
		    && ccline.cmdbuff[ccline.cmdpos - 1] == '\\')
	    {
		if (c == K_KENTER)
		    c = '\n';
	    }
	    else
	    {
		gotesc = FALSE;	/* Might have typed ESC previously, don't
				       truncate the cmdline now. */
		if (ccheck_abbr(c + ABBR_OFF))
		    goto cmdline_changed;
		if (!cmd_silent)
		{
		    windgoto(msg_row, 0);
		    out_flush();
		}
		break;
	    }
	}

	/*
	 * Completion for 'wildchar' or 'wildcharm' key.
	 * - hitting <ESC> twice means: abandon command line.
	 * - wildcard expansion is only done when the 'wildchar' key is really
	 *   typed, not when it comes from a macro
	 */
	if ((c == p_wc && !gotesc && KeyTyped) || c == p_wcm)
	{
	    int options = WILD_NO_BEEP;
	    if (wim_flags[wim_index] & WIM_BUFLASTUSED)
		options |= WILD_BUFLASTUSED;
	    if (xpc.xp_numfiles > 0)   /* typed p_wc at least twice */
	    {
		/* if 'wildmode' contains "list" may still need to list */
		if (xpc.xp_numfiles > 1
			&& !did_wild_list
			&& (wim_flags[wim_index] & WIM_LIST))
		{
		    (void)showmatches(&xpc, FALSE);
		    redrawcmd();
		    did_wild_list = TRUE;
		}
		if (wim_flags[wim_index] & WIM_LONGEST)
		    res = nextwild(&xpc, WILD_LONGEST, options,
							       firstc != '@');
		else if (wim_flags[wim_index] & WIM_FULL)
		    res = nextwild(&xpc, WILD_NEXT, options,
							       firstc != '@');
		else
		    res = OK;	    /* don't insert 'wildchar' now */
	    }
	    else		    /* typed p_wc first time */
	    {
		wim_index = 0;
		j = ccline.cmdpos;
		/* if 'wildmode' first contains "longest", get longest
		 * common part */
		if (wim_flags[0] & WIM_LONGEST)
		    res = nextwild(&xpc, WILD_LONGEST, options,
							       firstc != '@');
		else
		    res = nextwild(&xpc, WILD_EXPAND_KEEP, options,
							       firstc != '@');

		/* if interrupted while completing, behave like it failed */
		if (got_int)
		{
		    (void)vpeekc();	/* remove <C-C> from input stream */
		    got_int = FALSE;	/* don't abandon the command line */
		    (void)ExpandOne(&xpc, NULL, NULL, 0, WILD_FREE);
#ifdef FEAT_WILDMENU
		    xpc.xp_context = EXPAND_NOTHING;
#endif
		    goto cmdline_changed;
		}

		/* when more than one match, and 'wildmode' first contains
		 * "list", or no change and 'wildmode' contains "longest,list",
		 * list all matches */
		if (res == OK && xpc.xp_numfiles > 1)
		{
		    /* a "longest" that didn't do anything is skipped (but not
		     * "list:longest") */
		    if (wim_flags[0] == WIM_LONGEST && ccline.cmdpos == j)
			wim_index = 1;
		    if ((wim_flags[wim_index] & WIM_LIST)
#ifdef FEAT_WILDMENU
			    || (p_wmnu && (wim_flags[wim_index] & WIM_FULL) != 0)
#endif
			    )
		    {
			if (!(wim_flags[0] & WIM_LONGEST))
			{
#ifdef FEAT_WILDMENU
			    int p_wmnu_save = p_wmnu;
			    p_wmnu = 0;
#endif
			    /* remove match */
			    nextwild(&xpc, WILD_PREV, 0, firstc != '@');
#ifdef FEAT_WILDMENU
			    p_wmnu = p_wmnu_save;
#endif
			}
#ifdef FEAT_WILDMENU
			(void)showmatches(&xpc, p_wmnu
				&& ((wim_flags[wim_index] & WIM_LIST) == 0));
#else
			(void)showmatches(&xpc, FALSE);
#endif
			redrawcmd();
			did_wild_list = TRUE;
			if (wim_flags[wim_index] & WIM_LONGEST)
			    nextwild(&xpc, WILD_LONGEST, options,
							       firstc != '@');
			else if (wim_flags[wim_index] & WIM_FULL)
			    nextwild(&xpc, WILD_NEXT, options,
							       firstc != '@');
		    }
		    else
			vim_beep(BO_WILD);
		}
#ifdef FEAT_WILDMENU
		else if (xpc.xp_numfiles == -1)
		    xpc.xp_context = EXPAND_NOTHING;
#endif
	    }
	    if (wim_index < 3)
		++wim_index;
	    if (c == ESC)
		gotesc = TRUE;
	    if (res == OK)
		goto cmdline_changed;
	}

	gotesc = FALSE;

	/* <S-Tab> goes to last match, in a clumsy way */
	if (c == K_S_TAB && KeyTyped)
	{
	    if (nextwild(&xpc, WILD_EXPAND_KEEP, 0, firstc != '@') == OK
		    && nextwild(&xpc, WILD_PREV, 0, firstc != '@') == OK
		    && nextwild(&xpc, WILD_PREV, 0, firstc != '@') == OK)
		goto cmdline_changed;
	}

	if (c == NUL || c == K_ZERO)	    /* NUL is stored as NL */
	    c = NL;

	do_abbr = TRUE;		/* default: check for abbreviation */

	/*
	 * Big switch for a typed command line character.
	 */
	switch (c)
	{
	case K_BS:
	case Ctrl_H:
	case K_DEL:
	case K_KDEL:
	case Ctrl_W:
		if (c == K_KDEL)
		    c = K_DEL;

		/*
		 * delete current character is the same as backspace on next
		 * character, except at end of line
		 */
		if (c == K_DEL && ccline.cmdpos != ccline.cmdlen)
		    ++ccline.cmdpos;
		if (has_mbyte && c == K_DEL)
		    ccline.cmdpos += mb_off_next(ccline.cmdbuff,
					      ccline.cmdbuff + ccline.cmdpos);
		if (ccline.cmdpos > 0)
		{
		    char_u *p;

		    j = ccline.cmdpos;
		    p = ccline.cmdbuff + j;
		    if (has_mbyte)
		    {
			p = mb_prevptr(ccline.cmdbuff, p);
			if (c == Ctrl_W)
			{
			    while (p > ccline.cmdbuff && vim_isspace(*p))
				p = mb_prevptr(ccline.cmdbuff, p);
			    i = mb_get_class(p);
			    while (p > ccline.cmdbuff && mb_get_class(p) == i)
				p = mb_prevptr(ccline.cmdbuff, p);
			    if (mb_get_class(p) != i)
				p += (*mb_ptr2len)(p);
			}
		    }
		    else if (c == Ctrl_W)
		    {
			while (p > ccline.cmdbuff && vim_isspace(p[-1]))
			    --p;
			i = vim_iswordc(p[-1]);
			while (p > ccline.cmdbuff && !vim_isspace(p[-1])
				&& vim_iswordc(p[-1]) == i)
			    --p;
		    }
		    else
			--p;
		    ccline.cmdpos = (int)(p - ccline.cmdbuff);
		    ccline.cmdlen -= j - ccline.cmdpos;
		    i = ccline.cmdpos;
		    while (i < ccline.cmdlen)
			ccline.cmdbuff[i++] = ccline.cmdbuff[j++];

		    /* Truncate at the end, required for multi-byte chars. */
		    ccline.cmdbuff[ccline.cmdlen] = NUL;
#ifdef FEAT_SEARCH_EXTRA
		    if (ccline.cmdlen == 0)
		    {
			is_state.search_start = is_state.save_cursor;
			/* save view settings, so that the screen
			 * won't be restored at the wrong position */
			is_state.old_viewstate = is_state.init_viewstate;
		    }
#endif
		    redrawcmd();
		}
		else if (ccline.cmdlen == 0 && c != Ctrl_W
				   && ccline.cmdprompt == NULL && indent == 0)
		{
		    /* In ex and debug mode it doesn't make sense to return. */
		    if (exmode_active
#ifdef FEAT_EVAL
			    || ccline.cmdfirstc == '>'
#endif
			    )
			goto cmdline_not_changed;

		    VIM_CLEAR(ccline.cmdbuff);	/* no commandline to return */
		    if (!cmd_silent)
		    {
#ifdef FEAT_RIGHTLEFT
			if (cmdmsg_rl)
			    msg_col = Columns;
			else
#endif
			    msg_col = 0;
			msg_putchar(' ');		/* delete ':' */
		    }
#ifdef FEAT_SEARCH_EXTRA
		    if (ccline.cmdlen == 0)
			is_state.search_start = is_state.save_cursor;
#endif
		    redraw_cmdline = TRUE;
		    goto returncmd;		/* back to cmd mode */
		}
		goto cmdline_changed;

	case K_INS:
	case K_KINS:
		ccline.overstrike = !ccline.overstrike;
#ifdef CURSOR_SHAPE
		ui_cursor_shape();	/* may show different cursor shape */
#endif
		goto cmdline_not_changed;

	case Ctrl_HAT:
		if (map_to_exists_mode((char_u *)"", LANGMAP, FALSE))
		{
		    /* ":lmap" mappings exists, toggle use of mappings. */
		    State ^= LANGMAP;
#ifdef HAVE_INPUT_METHOD
		    im_set_active(FALSE);	/* Disable input method */
#endif
		    if (b_im_ptr != NULL)
		    {
			if (State & LANGMAP)
			    *b_im_ptr = B_IMODE_LMAP;
			else
			    *b_im_ptr = B_IMODE_NONE;
		    }
		}
#ifdef HAVE_INPUT_METHOD
		else
		{
		    /* There are no ":lmap" mappings, toggle IM.  When
		     * 'imdisable' is set don't try getting the status, it's
		     * always off. */
		    if ((p_imdisable && b_im_ptr != NULL)
			    ? *b_im_ptr == B_IMODE_IM : im_get_status())
		    {
			im_set_active(FALSE);	/* Disable input method */
			if (b_im_ptr != NULL)
			    *b_im_ptr = B_IMODE_NONE;
		    }
		    else
		    {
			im_set_active(TRUE);	/* Enable input method */
			if (b_im_ptr != NULL)
			    *b_im_ptr = B_IMODE_IM;
		    }
		}
#endif
		if (b_im_ptr != NULL)
		{
		    if (b_im_ptr == &curbuf->b_p_iminsert)
			set_iminsert_global();
		    else
			set_imsearch_global();
		}
#ifdef CURSOR_SHAPE
		ui_cursor_shape();	/* may show different cursor shape */
#endif
#if defined(FEAT_KEYMAP)
		/* Show/unshow value of 'keymap' in status lines later. */
		status_redraw_curbuf();
#endif
		goto cmdline_not_changed;

/*	case '@':   only in very old vi */
	case Ctrl_U:
		/* delete all characters left of the cursor */
		j = ccline.cmdpos;
		ccline.cmdlen -= j;
		i = ccline.cmdpos = 0;
		while (i < ccline.cmdlen)
		    ccline.cmdbuff[i++] = ccline.cmdbuff[j++];
		/* Truncate at the end, required for multi-byte chars. */
		ccline.cmdbuff[ccline.cmdlen] = NUL;
#ifdef FEAT_SEARCH_EXTRA
		if (ccline.cmdlen == 0)
		    is_state.search_start = is_state.save_cursor;
#endif
		redrawcmd();
		goto cmdline_changed;

#ifdef FEAT_CLIPBOARD
	case Ctrl_Y:
		/* Copy the modeless selection, if there is one. */
		if (clip_star.state != SELECT_CLEARED)
		{
		    if (clip_star.state == SELECT_DONE)
			clip_copy_modeless_selection(TRUE);
		    goto cmdline_not_changed;
		}
		break;
#endif

	case ESC:	/* get here if p_wc != ESC or when ESC typed twice */
	case Ctrl_C:
		/* In exmode it doesn't make sense to return.  Except when
		 * ":normal" runs out of characters. */
		if (exmode_active
			       && (ex_normal_busy == 0 || typebuf.tb_len > 0))
		    goto cmdline_not_changed;

		gotesc = TRUE;		/* will free ccline.cmdbuff after
					   putting it in history */
		goto returncmd;		/* back to cmd mode */

	case Ctrl_R:			/* insert register */
#ifdef USE_ON_FLY_SCROLL
		dont_scroll = TRUE;	/* disallow scrolling here */
#endif
		putcmdline('"', TRUE);
		++no_mapping;
		i = c = plain_vgetc();	/* CTRL-R <char> */
		if (i == Ctrl_O)
		    i = Ctrl_R;		/* CTRL-R CTRL-O == CTRL-R CTRL-R */
		if (i == Ctrl_R)
		    c = plain_vgetc();	/* CTRL-R CTRL-R <char> */
		extra_char = NUL;
		--no_mapping;
#ifdef FEAT_EVAL
		/*
		 * Insert the result of an expression.
		 * Need to save the current command line, to be able to enter
		 * a new one...
		 */
		new_cmdpos = -1;
		if (c == '=')
		{
		    if (ccline.cmdfirstc == '='  // can't do this recursively
			    || cmdline_star > 0) // or when typing a password
		    {
			beep_flush();
			c = ESC;
		    }
		    else
			c = get_expr_register();
		}
#endif
		if (c != ESC)	    /* use ESC to cancel inserting register */
		{
		    cmdline_paste(c, i == Ctrl_R, FALSE);

#ifdef FEAT_EVAL
		    /* When there was a serious error abort getting the
		     * command line. */
		    if (aborting())
		    {
			gotesc = TRUE;  /* will free ccline.cmdbuff after
					   putting it in history */
			goto returncmd; /* back to cmd mode */
		    }
#endif
		    KeyTyped = FALSE;	/* Don't do p_wc completion. */
#ifdef FEAT_EVAL
		    if (new_cmdpos >= 0)
		    {
			/* set_cmdline_pos() was used */
			if (new_cmdpos > ccline.cmdlen)
			    ccline.cmdpos = ccline.cmdlen;
			else
			    ccline.cmdpos = new_cmdpos;
		    }
#endif
		}
		redrawcmd();
		goto cmdline_changed;

	case Ctrl_D:
		if (showmatches(&xpc, FALSE) == EXPAND_NOTHING)
		    break;	/* Use ^D as normal char instead */

		redrawcmd();
		continue;	/* don't do incremental search now */

	case K_RIGHT:
	case K_S_RIGHT:
	case K_C_RIGHT:
		do
		{
		    if (ccline.cmdpos >= ccline.cmdlen)
			break;
		    i = cmdline_charsize(ccline.cmdpos);
		    if (KeyTyped && ccline.cmdspos + i >= Columns * Rows)
			break;
		    ccline.cmdspos += i;
		    if (has_mbyte)
			ccline.cmdpos += (*mb_ptr2len)(ccline.cmdbuff
							     + ccline.cmdpos);
		    else
			++ccline.cmdpos;
		}
		while ((c == K_S_RIGHT || c == K_C_RIGHT
			       || (mod_mask & (MOD_MASK_SHIFT|MOD_MASK_CTRL)))
			&& ccline.cmdbuff[ccline.cmdpos] != ' ');
		if (has_mbyte)
		    set_cmdspos_cursor();
		goto cmdline_not_changed;

	case K_LEFT:
	case K_S_LEFT:
	case K_C_LEFT:
		if (ccline.cmdpos == 0)
		    goto cmdline_not_changed;
		do
		{
		    --ccline.cmdpos;
		    if (has_mbyte)	/* move to first byte of char */
			ccline.cmdpos -= (*mb_head_off)(ccline.cmdbuff,
					      ccline.cmdbuff + ccline.cmdpos);
		    ccline.cmdspos -= cmdline_charsize(ccline.cmdpos);
		}
		while (ccline.cmdpos > 0
			&& (c == K_S_LEFT || c == K_C_LEFT
			       || (mod_mask & (MOD_MASK_SHIFT|MOD_MASK_CTRL)))
			&& ccline.cmdbuff[ccline.cmdpos - 1] != ' ');
		if (has_mbyte)
		    set_cmdspos_cursor();
		goto cmdline_not_changed;

	case K_IGNORE:
		/* Ignore mouse event or open_cmdwin() result. */
		goto cmdline_not_changed;

#ifdef FEAT_GUI_MSWIN
	    /* On MS-Windows ignore <M-F4>, we get it when closing the window
	     * was cancelled. */
	case K_F4:
	    if (mod_mask == MOD_MASK_ALT)
	    {
		redrawcmd();	    /* somehow the cmdline is cleared */
		goto cmdline_not_changed;
	    }
	    break;
#endif

	case K_MIDDLEDRAG:
	case K_MIDDLERELEASE:
		goto cmdline_not_changed;	/* Ignore mouse */

	case K_MIDDLEMOUSE:
# ifdef FEAT_GUI
		/* When GUI is active, also paste when 'mouse' is empty */
		if (!gui.in_use)
# endif
		    if (!mouse_has(MOUSE_COMMAND))
			goto cmdline_not_changed;   /* Ignore mouse */
# ifdef FEAT_CLIPBOARD
		if (clip_star.available)
		    cmdline_paste('*', TRUE, TRUE);
		else
# endif
		    cmdline_paste(0, TRUE, TRUE);
		redrawcmd();
		goto cmdline_changed;

# ifdef FEAT_DND
	case K_DROP:
		cmdline_paste('~', TRUE, FALSE);
		redrawcmd();
		goto cmdline_changed;
# endif

	case K_LEFTDRAG:
	case K_LEFTRELEASE:
	case K_RIGHTDRAG:
	case K_RIGHTRELEASE:
		/* Ignore drag and release events when the button-down wasn't
		 * seen before. */
		if (ignore_drag_release)
		    goto cmdline_not_changed;
		/* FALLTHROUGH */
	case K_LEFTMOUSE:
	case K_RIGHTMOUSE:
		if (c == K_LEFTRELEASE || c == K_RIGHTRELEASE)
		    ignore_drag_release = TRUE;
		else
		    ignore_drag_release = FALSE;
# ifdef FEAT_GUI
		/* When GUI is active, also move when 'mouse' is empty */
		if (!gui.in_use)
# endif
		    if (!mouse_has(MOUSE_COMMAND))
			goto cmdline_not_changed;   /* Ignore mouse */
# ifdef FEAT_CLIPBOARD
		if (mouse_row < cmdline_row && clip_star.available)
		{
		    int	    button, is_click, is_drag;

		    /*
		     * Handle modeless selection.
		     */
		    button = get_mouse_button(KEY2TERMCAP1(c),
							 &is_click, &is_drag);
		    if (mouse_model_popup() && button == MOUSE_LEFT
					       && (mod_mask & MOD_MASK_SHIFT))
		    {
			/* Translate shift-left to right button. */
			button = MOUSE_RIGHT;
			mod_mask &= ~MOD_MASK_SHIFT;
		    }
		    clip_modeless(button, is_click, is_drag);
		    goto cmdline_not_changed;
		}
# endif

		set_cmdspos();
		for (ccline.cmdpos = 0; ccline.cmdpos < ccline.cmdlen;
							      ++ccline.cmdpos)
		{
		    i = cmdline_charsize(ccline.cmdpos);
		    if (mouse_row <= cmdline_row + ccline.cmdspos / Columns
				  && mouse_col < ccline.cmdspos % Columns + i)
			break;
		    if (has_mbyte)
		    {
			/* Count ">" for double-wide char that doesn't fit. */
			correct_cmdspos(ccline.cmdpos, i);
			ccline.cmdpos += (*mb_ptr2len)(ccline.cmdbuff
							 + ccline.cmdpos) - 1;
		    }
		    ccline.cmdspos += i;
		}
		goto cmdline_not_changed;

	/* Mouse scroll wheel: ignored here */
	case K_MOUSEDOWN:
	case K_MOUSEUP:
	case K_MOUSELEFT:
	case K_MOUSERIGHT:
	/* Alternate buttons ignored here */
	case K_X1MOUSE:
	case K_X1DRAG:
	case K_X1RELEASE:
	case K_X2MOUSE:
	case K_X2DRAG:
	case K_X2RELEASE:
	case K_MOUSEMOVE:
		goto cmdline_not_changed;

#ifdef FEAT_GUI
	case K_LEFTMOUSE_NM:	/* mousefocus click, ignored */
	case K_LEFTRELEASE_NM:
		goto cmdline_not_changed;

	case K_VER_SCROLLBAR:
		if (msg_scrolled == 0)
		{
		    gui_do_scroll();
		    redrawcmd();
		}
		goto cmdline_not_changed;

	case K_HOR_SCROLLBAR:
		if (msg_scrolled == 0)
		{
		    gui_do_horiz_scroll(scrollbar_value, FALSE);
		    redrawcmd();
		}
		goto cmdline_not_changed;
#endif
#ifdef FEAT_GUI_TABLINE
	case K_TABLINE:
	case K_TABMENU:
		/* Don't want to change any tabs here.  Make sure the same tab
		 * is still selected. */
		if (gui_use_tabline())
		    gui_mch_set_curtab(tabpage_index(curtab));
		goto cmdline_not_changed;
#endif

	case K_SELECT:	    /* end of Select mode mapping - ignore */
		goto cmdline_not_changed;

	case Ctrl_B:	    /* begin of command line */
	case K_HOME:
	case K_KHOME:
	case K_S_HOME:
	case K_C_HOME:
		ccline.cmdpos = 0;
		set_cmdspos();
		goto cmdline_not_changed;

	case Ctrl_E:	    /* end of command line */
	case K_END:
	case K_KEND:
	case K_S_END:
	case K_C_END:
		ccline.cmdpos = ccline.cmdlen;
		set_cmdspos_cursor();
		goto cmdline_not_changed;

	case Ctrl_A:	    /* all matches */
		if (nextwild(&xpc, WILD_ALL, 0, firstc != '@') == FAIL)
		    break;
		goto cmdline_changed;

	case Ctrl_L:
#ifdef FEAT_SEARCH_EXTRA
		if (may_add_char_to_search(firstc, &c, &is_state) == OK)
		    goto cmdline_not_changed;
#endif

		/* completion: longest common part */
		if (nextwild(&xpc, WILD_LONGEST, 0, firstc != '@') == FAIL)
		    break;
		goto cmdline_changed;

	case Ctrl_N:	    /* next match */
	case Ctrl_P:	    /* previous match */
		if (xpc.xp_numfiles > 0)
		{
		    if (nextwild(&xpc, (c == Ctrl_P) ? WILD_PREV : WILD_NEXT,
						    0, firstc != '@') == FAIL)
			break;
		    goto cmdline_not_changed;
		}
		/* FALLTHROUGH */
	case K_UP:
	case K_DOWN:
	case K_S_UP:
	case K_S_DOWN:
	case K_PAGEUP:
	case K_KPAGEUP:
	case K_PAGEDOWN:
	case K_KPAGEDOWN:
		if (get_hislen() == 0 || firstc == NUL)	/* no history */
		    goto cmdline_not_changed;

		i = hiscnt;

		/* save current command string so it can be restored later */
		if (lookfor == NULL)
		{
		    if ((lookfor = vim_strsave(ccline.cmdbuff)) == NULL)
			goto cmdline_not_changed;
		    lookfor[ccline.cmdpos] = NUL;
		}

		j = (int)STRLEN(lookfor);
		for (;;)
		{
		    /* one step backwards */
		    if (c == K_UP|| c == K_S_UP || c == Ctrl_P
			    || c == K_PAGEUP || c == K_KPAGEUP)
		    {
			if (hiscnt == get_hislen())	/* first time */
			    hiscnt = *get_hisidx(histype);
			else if (hiscnt == 0 && *get_hisidx(histype) != get_hislen() - 1)
			    hiscnt = get_hislen() - 1;
			else if (hiscnt != *get_hisidx(histype) + 1)
			    --hiscnt;
			else			/* at top of list */
			{
			    hiscnt = i;
			    break;
			}
		    }
		    else    /* one step forwards */
		    {
			/* on last entry, clear the line */
			if (hiscnt == *get_hisidx(histype))
			{
			    hiscnt = get_hislen();
			    break;
			}

			/* not on a history line, nothing to do */
			if (hiscnt == get_hislen())
			    break;
			if (hiscnt == get_hislen() - 1)   /* wrap around */
			    hiscnt = 0;
			else
			    ++hiscnt;
		    }
		    if (hiscnt < 0 || get_histentry(histype)[hiscnt].hisstr == NULL)
		    {
			hiscnt = i;
			break;
		    }
		    if ((c != K_UP && c != K_DOWN)
			    || hiscnt == i
			    || STRNCMP(get_histentry(histype)[hiscnt].hisstr,
						    lookfor, (size_t)j) == 0)
			break;
		}

		if (hiscnt != i)	/* jumped to other entry */
		{
		    char_u	*p;
		    int		len;
		    int		old_firstc;

		    VIM_CLEAR(ccline.cmdbuff);
		    xpc.xp_context = EXPAND_NOTHING;
		    if (hiscnt == get_hislen())
			p = lookfor;	/* back to the old one */
		    else
			p = get_histentry(histype)[hiscnt].hisstr;

		    if (histype == HIST_SEARCH
			    && p != lookfor
			    && (old_firstc = p[STRLEN(p) + 1]) != firstc)
		    {
			/* Correct for the separator character used when
			 * adding the history entry vs the one used now.
			 * First loop: count length.
			 * Second loop: copy the characters. */
			for (i = 0; i <= 1; ++i)
			{
			    len = 0;
			    for (j = 0; p[j] != NUL; ++j)
			    {
				/* Replace old sep with new sep, unless it is
				 * escaped. */
				if (p[j] == old_firstc
					      && (j == 0 || p[j - 1] != '\\'))
				{
				    if (i > 0)
					ccline.cmdbuff[len] = firstc;
				}
				else
				{
				    /* Escape new sep, unless it is already
				     * escaped. */
				    if (p[j] == firstc
					      && (j == 0 || p[j - 1] != '\\'))
				    {
					if (i > 0)
					    ccline.cmdbuff[len] = '\\';
					++len;
				    }
				    if (i > 0)
					ccline.cmdbuff[len] = p[j];
				}
				++len;
			    }
			    if (i == 0)
			    {
				alloc_cmdbuff(len);
				if (ccline.cmdbuff == NULL)
				    goto returncmd;
			    }
			}
			ccline.cmdbuff[len] = NUL;
		    }
		    else
		    {
			alloc_cmdbuff((int)STRLEN(p));
			if (ccline.cmdbuff == NULL)
			    goto returncmd;
			STRCPY(ccline.cmdbuff, p);
		    }

		    ccline.cmdpos = ccline.cmdlen = (int)STRLEN(ccline.cmdbuff);
		    redrawcmd();
		    goto cmdline_changed;
		}
		beep_flush();
		goto cmdline_not_changed;

#ifdef FEAT_SEARCH_EXTRA
	case Ctrl_G:	    /* next match */
	case Ctrl_T:	    /* previous match */
		if (may_adjust_incsearch_highlighting(
					  firstc, count, &is_state, c) == FAIL)
		    goto cmdline_not_changed;
		break;
#endif

	case Ctrl_V:
	case Ctrl_Q:
		ignore_drag_release = TRUE;
		putcmdline('^', TRUE);
		c = get_literal();	    /* get next (two) character(s) */
		do_abbr = FALSE;	    /* don't do abbreviation now */
		extra_char = NUL;
		/* may need to remove ^ when composing char was typed */
		if (enc_utf8 && utf_iscomposing(c) && !cmd_silent)
		{
		    draw_cmdline(ccline.cmdpos, ccline.cmdlen - ccline.cmdpos);
		    msg_putchar(' ');
		    cursorcmd();
		}
		break;

#ifdef FEAT_DIGRAPHS
	case Ctrl_K:
		ignore_drag_release = TRUE;
		putcmdline('?', TRUE);
# ifdef USE_ON_FLY_SCROLL
		dont_scroll = TRUE;	    /* disallow scrolling here */
# endif
		c = get_digraph(TRUE);
		extra_char = NUL;
		if (c != NUL)
		    break;

		redrawcmd();
		goto cmdline_not_changed;
#endif // FEAT_DIGRAPHS

#ifdef FEAT_RIGHTLEFT
	case Ctrl__:	    /* CTRL-_: switch language mode */
		if (!p_ari)
		    break;
		cmd_hkmap = !cmd_hkmap;
		goto cmdline_not_changed;
#endif

	case K_PS:
		bracketed_paste(PASTE_CMDLINE, FALSE, NULL);
		goto cmdline_changed;

	default:
#ifdef UNIX
		if (c == intr_char)
		{
		    gotesc = TRUE;	/* will free ccline.cmdbuff after
					   putting it in history */
		    goto returncmd;	/* back to Normal mode */
		}
#endif
		/*
		 * Normal character with no special meaning.  Just set mod_mask
		 * to 0x0 so that typing Shift-Space in the GUI doesn't enter
		 * the string <S-Space>.  This should only happen after ^V.
		 */
		if (!IS_SPECIAL(c))
		    mod_mask = 0x0;
		break;
	}
	/*
	 * End of switch on command line character.
	 * We come here if we have a normal character.
	 */

	if (do_abbr && (IS_SPECIAL(c) || !vim_iswordc(c))
		&& (ccheck_abbr(
			// Add ABBR_OFF for characters above 0x100, this is
			// what check_abbr() expects.
				(has_mbyte && c >= 0x100) ? (c + ABBR_OFF) : c)
		    || c == Ctrl_RSB))
	    goto cmdline_changed;

	/*
	 * put the character in the command line
	 */
	if (IS_SPECIAL(c) || mod_mask != 0)
	    put_on_cmdline(get_special_key_name(c, mod_mask), -1, TRUE);
	else
	{
	    if (has_mbyte)
	    {
		j = (*mb_char2bytes)(c, IObuff);
		IObuff[j] = NUL;	/* exclude composing chars */
		put_on_cmdline(IObuff, j, TRUE);
	    }
	    else
	    {
		IObuff[0] = c;
		put_on_cmdline(IObuff, 1, TRUE);
	    }
	}
	goto cmdline_changed;

/*
 * This part implements incremental searches for "/" and "?"
 * Jump to cmdline_not_changed when a character has been read but the command
 * line did not change. Then we only search and redraw if something changed in
 * the past.
 * Jump to cmdline_changed when the command line did change.
 * (Sorry for the goto's, I know it is ugly).
 */
cmdline_not_changed:
#ifdef FEAT_SEARCH_EXTRA
	if (!is_state.incsearch_postponed)
	    continue;
#endif

cmdline_changed:
	/* Trigger CmdlineChanged autocommands. */
	trigger_cmd_autocmd(cmdline_type, EVENT_CMDLINECHANGED);

#ifdef FEAT_SEARCH_EXTRA
	may_do_incsearch_highlighting(firstc, count, &is_state);
#endif

#ifdef FEAT_RIGHTLEFT
	if (cmdmsg_rl
# ifdef FEAT_ARABIC
		|| (p_arshape && !p_tbidi
				       && cmdline_has_arabic(0, ccline.cmdlen))
# endif
		)
	    /* Always redraw the whole command line to fix shaping and
	     * right-left typing.  Not efficient, but it works.
	     * Do it only when there are no characters left to read
	     * to avoid useless intermediate redraws. */
	    if (vpeekc() == NUL)
		redrawcmd();
#endif
    }

returncmd:

#ifdef FEAT_RIGHTLEFT
    cmdmsg_rl = FALSE;
#endif

    ExpandCleanup(&xpc);
    ccline.xpc = NULL;

#ifdef FEAT_SEARCH_EXTRA
    finish_incsearch_highlighting(gotesc, &is_state, FALSE);
#endif

    if (ccline.cmdbuff != NULL)
    {
	/*
	 * Put line in history buffer (":" and "=" only when it was typed).
	 */
	if (ccline.cmdlen && firstc != NUL
		&& (some_key_typed || histype == HIST_SEARCH))
	{
	    add_to_history(histype, ccline.cmdbuff, TRUE,
				       histype == HIST_SEARCH ? firstc : NUL);
	    if (firstc == ':')
	    {
		vim_free(new_last_cmdline);
		new_last_cmdline = vim_strsave(ccline.cmdbuff);
	    }
	}

	if (gotesc)
	    abandon_cmdline();
    }

    /*
     * If the screen was shifted up, redraw the whole screen (later).
     * If the line is too long, clear it, so ruler and shown command do
     * not get printed in the middle of it.
     */
    msg_check();
    msg_scroll = save_msg_scroll;
    redir_off = FALSE;

    /* When the command line was typed, no need for a wait-return prompt. */
    if (some_key_typed)
	need_wait_return = FALSE;

    /* Trigger CmdlineLeave autocommands. */
    trigger_cmd_autocmd(cmdline_type, EVENT_CMDLINELEAVE);

    State = save_State;
#ifdef HAVE_INPUT_METHOD
    if (b_im_ptr != NULL && *b_im_ptr != B_IMODE_LMAP)
	im_save_status(b_im_ptr);
    im_set_active(FALSE);
#endif
    setmouse();
#ifdef CURSOR_SHAPE
    ui_cursor_shape();		/* may show different cursor shape */
#endif
    sb_text_end_cmdline();

theend:
    {
	char_u *p = ccline.cmdbuff;

	if (did_save_ccline)
	    restore_cmdline(&save_ccline);
	else
	    ccline.cmdbuff = NULL;
	return p;
    }
}

#if (defined(FEAT_CRYPT) || defined(FEAT_EVAL)) || defined(PROTO)
/*
 * Get a command line with a prompt.
 * This is prepared to be called recursively from getcmdline() (e.g. by
 * f_input() when evaluating an expression from CTRL-R =).
 * Returns the command line in allocated memory, or NULL.
 */
    char_u *
getcmdline_prompt(
    int		firstc,
    char_u	*prompt,	/* command line prompt */
    int		attr,		/* attributes for prompt */
    int		xp_context,	/* type of expansion */
    char_u	*xp_arg)	/* user-defined expansion argument */
{
    char_u		*s;
    cmdline_info_T	save_ccline;
    int			did_save_ccline = FALSE;
    int			msg_col_save = msg_col;
    int			msg_silent_save = msg_silent;

    if (ccline.cmdbuff != NULL)
    {
	// Save the values of the current cmdline and restore them below.
	save_cmdline(&save_ccline);
	did_save_ccline = TRUE;
    }

    vim_memset(&ccline, 0, sizeof(cmdline_info_T));
    ccline.cmdprompt = prompt;
    ccline.cmdattr = attr;
# ifdef FEAT_EVAL
    ccline.xp_context = xp_context;
    ccline.xp_arg = xp_arg;
    ccline.input_fn = (firstc == '@');
# endif
    msg_silent = 0;
    s = getcmdline_int(firstc, 1L, 0, FALSE);

    if (did_save_ccline)
	restore_cmdline(&save_ccline);

    msg_silent = msg_silent_save;
    /* Restore msg_col, the prompt from input() may have changed it.
     * But only if called recursively and the commandline is therefore being
     * restored to an old one; if not, the input() prompt stays on the screen,
     * so we need its modified msg_col left intact. */
    if (ccline.cmdbuff != NULL)
	msg_col = msg_col_save;

    return s;
}
#endif

/*
 * Return TRUE when the text must not be changed and we can't switch to
 * another window or buffer.  Used when editing the command line, evaluating
 * 'balloonexpr', etc.
 */
    int
text_locked(void)
{
#ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
	return TRUE;
#endif
    return textlock != 0;
}

/*
 * Give an error message for a command that isn't allowed while the cmdline
 * window is open or editing the cmdline in another way.
 */
    void
text_locked_msg(void)
{
    emsg(_(get_text_locked_msg()));
}

    char *
get_text_locked_msg(void)
{
#ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
	return e_cmdwin;
#endif
    return e_secure;
}

/*
 * Check if "curbuf_lock" or "allbuf_lock" is set and return TRUE when it is
 * and give an error message.
 */
    int
curbuf_locked(void)
{
    if (curbuf_lock > 0)
    {
	emsg(_("E788: Not allowed to edit another buffer now"));
	return TRUE;
    }
    return allbuf_locked();
}

/*
 * Check if "allbuf_lock" is set and return TRUE when it is and give an error
 * message.
 */
    int
allbuf_locked(void)
{
    if (allbuf_lock > 0)
    {
	emsg(_("E811: Not allowed to change buffer information now"));
	return TRUE;
    }
    return FALSE;
}

    static int
cmdline_charsize(int idx)
{
#if defined(FEAT_CRYPT) || defined(FEAT_EVAL)
    if (cmdline_star > 0)	    /* showing '*', always 1 position */
	return 1;
#endif
    return ptr2cells(ccline.cmdbuff + idx);
}

/*
 * Compute the offset of the cursor on the command line for the prompt and
 * indent.
 */
    static void
set_cmdspos(void)
{
    if (ccline.cmdfirstc != NUL)
	ccline.cmdspos = 1 + ccline.cmdindent;
    else
	ccline.cmdspos = 0 + ccline.cmdindent;
}

/*
 * Compute the screen position for the cursor on the command line.
 */
    static void
set_cmdspos_cursor(void)
{
    int		i, m, c;

    set_cmdspos();
    if (KeyTyped)
    {
	m = Columns * Rows;
	if (m < 0)	/* overflow, Columns or Rows at weird value */
	    m = MAXCOL;
    }
    else
	m = MAXCOL;
    for (i = 0; i < ccline.cmdlen && i < ccline.cmdpos; ++i)
    {
	c = cmdline_charsize(i);
	/* Count ">" for double-wide multi-byte char that doesn't fit. */
	if (has_mbyte)
	    correct_cmdspos(i, c);
	/* If the cmdline doesn't fit, show cursor on last visible char.
	 * Don't move the cursor itself, so we can still append. */
	if ((ccline.cmdspos += c) >= m)
	{
	    ccline.cmdspos -= c;
	    break;
	}
	if (has_mbyte)
	    i += (*mb_ptr2len)(ccline.cmdbuff + i) - 1;
    }
}

/*
 * Check if the character at "idx", which is "cells" wide, is a multi-byte
 * character that doesn't fit, so that a ">" must be displayed.
 */
    static void
correct_cmdspos(int idx, int cells)
{
    if ((*mb_ptr2len)(ccline.cmdbuff + idx) > 1
		&& (*mb_ptr2cells)(ccline.cmdbuff + idx) > 1
		&& ccline.cmdspos % Columns + cells > Columns)
	ccline.cmdspos++;
}

/*
 * Get an Ex command line for the ":" command.
 */
    char_u *
getexline(
    int		c,		/* normally ':', NUL for ":append" */
    void	*cookie UNUSED,
    int		indent,		/* indent for inside conditionals */
    int		do_concat)
{
    /* When executing a register, remove ':' that's in front of each line. */
    if (exec_from_reg && vpeekc() == ':')
	(void)vgetc();
    return getcmdline(c, 1L, indent, do_concat);
}

/*
 * Get an Ex command line for Ex mode.
 * In Ex mode we only use the OS supplied line editing features and no
 * mappings or abbreviations.
 * Returns a string in allocated memory or NULL.
 */
    char_u *
getexmodeline(
    int		promptc,	/* normally ':', NUL for ":append" and '?' for
				   :s prompt */
    void	*cookie UNUSED,
    int		indent,		/* indent for inside conditionals */
    int		do_concat UNUSED)
{
    garray_T	line_ga;
    char_u	*pend;
    int		startcol = 0;
    int		c1 = 0;
    int		escaped = FALSE;	/* CTRL-V typed */
    int		vcol = 0;
    char_u	*p;
    int		prev_char;
    int		len;

    /* Switch cursor on now.  This avoids that it happens after the "\n", which
     * confuses the system function that computes tabstops. */
    cursor_on();

    /* always start in column 0; write a newline if necessary */
    compute_cmdrow();
    if ((msg_col || msg_didout) && promptc != '?')
	msg_putchar('\n');
    if (promptc == ':')
    {
	/* indent that is only displayed, not in the line itself */
	if (p_prompt)
	    msg_putchar(':');
	while (indent-- > 0)
	    msg_putchar(' ');
	startcol = msg_col;
    }

    ga_init2(&line_ga, 1, 30);

    /* autoindent for :insert and :append is in the line itself */
    if (promptc <= 0)
    {
	vcol = indent;
	while (indent >= 8)
	{
	    ga_append(&line_ga, TAB);
	    msg_puts("        ");
	    indent -= 8;
	}
	while (indent-- > 0)
	{
	    ga_append(&line_ga, ' ');
	    msg_putchar(' ');
	}
    }
    ++no_mapping;
    ++allow_keys;

    /*
     * Get the line, one character at a time.
     */
    got_int = FALSE;
    while (!got_int)
    {
	long    sw;
	char_u *s;

	if (ga_grow(&line_ga, 40) == FAIL)
	    break;

	/*
	 * Get one character at a time.
	 */
	prev_char = c1;

	/* Check for a ":normal" command and no more characters left. */
	if (ex_normal_busy > 0 && typebuf.tb_len == 0)
	    c1 = '\n';
	else
	    c1 = vgetc();

	/*
	 * Handle line editing.
	 * Previously this was left to the system, putting the terminal in
	 * cooked mode, but then CTRL-D and CTRL-T can't be used properly.
	 */
	if (got_int)
	{
	    msg_putchar('\n');
	    break;
	}

	if (c1 == K_PS)
	{
	    bracketed_paste(PASTE_EX, FALSE, &line_ga);
	    goto redraw;
	}

	if (!escaped)
	{
	    /* CR typed means "enter", which is NL */
	    if (c1 == '\r')
		c1 = '\n';

	    if (c1 == BS || c1 == K_BS
			  || c1 == DEL || c1 == K_DEL || c1 == K_KDEL)
	    {
		if (line_ga.ga_len > 0)
		{
		    if (has_mbyte)
		    {
			p = (char_u *)line_ga.ga_data;
			p[line_ga.ga_len] = NUL;
			len = (*mb_head_off)(p, p + line_ga.ga_len - 1) + 1;
			line_ga.ga_len -= len;
		    }
		    else
			--line_ga.ga_len;
		    goto redraw;
		}
		continue;
	    }

	    if (c1 == Ctrl_U)
	    {
		msg_col = startcol;
		msg_clr_eos();
		line_ga.ga_len = 0;
		goto redraw;
	    }

	    if (c1 == Ctrl_T)
	    {
		sw = get_sw_value(curbuf);
		p = (char_u *)line_ga.ga_data;
		p[line_ga.ga_len] = NUL;
		indent = get_indent_str(p, 8, FALSE);
		indent += sw - indent % sw;
add_indent:
		while (get_indent_str(p, 8, FALSE) < indent)
		{
		    (void)ga_grow(&line_ga, 2);  /* one more for the NUL */
		    p = (char_u *)line_ga.ga_data;
		    s = skipwhite(p);
		    mch_memmove(s + 1, s, line_ga.ga_len - (s - p) + 1);
		    *s = ' ';
		    ++line_ga.ga_len;
		}
redraw:
		/* redraw the line */
		msg_col = startcol;
		vcol = 0;
		p = (char_u *)line_ga.ga_data;
		p[line_ga.ga_len] = NUL;
		while (p < (char_u *)line_ga.ga_data + line_ga.ga_len)
		{
		    if (*p == TAB)
		    {
			do
			    msg_putchar(' ');
			while (++vcol % 8);
			++p;
		    }
		    else
		    {
			len = mb_ptr2len(p);
			msg_outtrans_len(p, len);
			vcol += ptr2cells(p);
			p += len;
		    }
		}
		msg_clr_eos();
		windgoto(msg_row, msg_col);
		continue;
	    }

	    if (c1 == Ctrl_D)
	    {
		/* Delete one shiftwidth. */
		p = (char_u *)line_ga.ga_data;
		if (prev_char == '0' || prev_char == '^')
		{
		    if (prev_char == '^')
			ex_keep_indent = TRUE;
		    indent = 0;
		    p[--line_ga.ga_len] = NUL;
		}
		else
		{
		    p[line_ga.ga_len] = NUL;
		    indent = get_indent_str(p, 8, FALSE);
		    if (indent > 0)
		    {
			--indent;
			indent -= indent % get_sw_value(curbuf);
		    }
		}
		while (get_indent_str(p, 8, FALSE) > indent)
		{
		    s = skipwhite(p);
		    mch_memmove(s - 1, s, line_ga.ga_len - (s - p) + 1);
		    --line_ga.ga_len;
		}
		goto add_indent;
	    }

	    if (c1 == Ctrl_V || c1 == Ctrl_Q)
	    {
		escaped = TRUE;
		continue;
	    }

	    /* Ignore special key codes: mouse movement, K_IGNORE, etc. */
	    if (IS_SPECIAL(c1))
		continue;
	}

	if (IS_SPECIAL(c1))
	    c1 = '?';
	if (has_mbyte)
	    len = (*mb_char2bytes)(c1,
				  (char_u *)line_ga.ga_data + line_ga.ga_len);
	else
	{
	    len = 1;
	    ((char_u *)line_ga.ga_data)[line_ga.ga_len] = c1;
	}
	if (c1 == '\n')
	    msg_putchar('\n');
	else if (c1 == TAB)
	{
	    /* Don't use chartabsize(), 'ts' can be different */
	    do
		msg_putchar(' ');
	    while (++vcol % 8);
	}
	else
	{
	    msg_outtrans_len(
		     ((char_u *)line_ga.ga_data) + line_ga.ga_len, len);
	    vcol += char2cells(c1);
	}
	line_ga.ga_len += len;
	escaped = FALSE;

	windgoto(msg_row, msg_col);
	pend = (char_u *)(line_ga.ga_data) + line_ga.ga_len;

	/* We are done when a NL is entered, but not when it comes after an
	 * odd number of backslashes, that results in a NUL. */
	if (line_ga.ga_len > 0 && pend[-1] == '\n')
	{
	    int bcount = 0;

	    while (line_ga.ga_len - 2 >= bcount && pend[-2 - bcount] == '\\')
		++bcount;

	    if (bcount > 0)
	    {
		/* Halve the number of backslashes: "\NL" -> "NUL", "\\NL" ->
		 * "\NL", etc. */
		line_ga.ga_len -= (bcount + 1) / 2;
		pend -= (bcount + 1) / 2;
		pend[-1] = '\n';
	    }

	    if ((bcount & 1) == 0)
	    {
		--line_ga.ga_len;
		--pend;
		*pend = NUL;
		break;
	    }
	}
    }

    --no_mapping;
    --allow_keys;

    /* make following messages go to the next line */
    msg_didout = FALSE;
    msg_col = 0;
    if (msg_row < Rows - 1)
	++msg_row;
    emsg_on_display = FALSE;		/* don't want ui_delay() */

    if (got_int)
	ga_clear(&line_ga);

    return (char_u *)line_ga.ga_data;
}

# if defined(MCH_CURSOR_SHAPE) || defined(FEAT_GUI) \
	|| defined(FEAT_MOUSESHAPE) || defined(PROTO)
/*
 * Return TRUE if ccline.overstrike is on.
 */
    int
cmdline_overstrike(void)
{
    return ccline.overstrike;
}

/*
 * Return TRUE if the cursor is at the end of the cmdline.
 */
    int
cmdline_at_end(void)
{
    return (ccline.cmdpos >= ccline.cmdlen);
}
#endif

#if (defined(FEAT_XIM) && (defined(FEAT_GUI_GTK))) || defined(PROTO)
/*
 * Return the virtual column number at the current cursor position.
 * This is used by the IM code to obtain the start of the preedit string.
 */
    colnr_T
cmdline_getvcol_cursor(void)
{
    if (ccline.cmdbuff == NULL || ccline.cmdpos > ccline.cmdlen)
	return MAXCOL;

    if (has_mbyte)
    {
	colnr_T	col;
	int	i = 0;

	for (col = 0; i < ccline.cmdpos; ++col)
	    i += (*mb_ptr2len)(ccline.cmdbuff + i);

	return col;
    }
    else
	return ccline.cmdpos;
}
#endif

#if defined(FEAT_XIM) && defined(FEAT_GUI_GTK)
/*
 * If part of the command line is an IM preedit string, redraw it with
 * IM feedback attributes.  The cursor position is restored after drawing.
 */
    static void
redrawcmd_preedit(void)
{
    if ((State & CMDLINE)
	    && xic != NULL
	    /* && im_get_status()  doesn't work when using SCIM */
	    && !p_imdisable
	    && im_is_preediting())
    {
	int	cmdpos = 0;
	int	cmdspos;
	int	old_row;
	int	old_col;
	colnr_T	col;

	old_row = msg_row;
	old_col = msg_col;
	cmdspos = ((ccline.cmdfirstc != NUL) ? 1 : 0) + ccline.cmdindent;

	if (has_mbyte)
	{
	    for (col = 0; col < preedit_start_col
			  && cmdpos < ccline.cmdlen; ++col)
	    {
		cmdspos += (*mb_ptr2cells)(ccline.cmdbuff + cmdpos);
		cmdpos  += (*mb_ptr2len)(ccline.cmdbuff + cmdpos);
	    }
	}
	else
	{
	    cmdspos += preedit_start_col;
	    cmdpos  += preedit_start_col;
	}

	msg_row = cmdline_row + (cmdspos / (int)Columns);
	msg_col = cmdspos % (int)Columns;
	if (msg_row >= Rows)
	    msg_row = Rows - 1;

	for (col = 0; cmdpos < ccline.cmdlen; ++col)
	{
	    int char_len;
	    int char_attr;

	    char_attr = im_get_feedback_attr(col);
	    if (char_attr < 0)
		break; /* end of preedit string */

	    if (has_mbyte)
		char_len = (*mb_ptr2len)(ccline.cmdbuff + cmdpos);
	    else
		char_len = 1;

	    msg_outtrans_len_attr(ccline.cmdbuff + cmdpos, char_len, char_attr);
	    cmdpos += char_len;
	}

	msg_row = old_row;
	msg_col = old_col;
    }
}
#endif /* FEAT_XIM && FEAT_GUI_GTK */

/*
 * Allocate a new command line buffer.
 * Assigns the new buffer to ccline.cmdbuff and ccline.cmdbufflen.
 */
    static void
alloc_cmdbuff(int len)
{
    /*
     * give some extra space to avoid having to allocate all the time
     */
    if (len < 80)
	len = 100;
    else
	len += 20;

    ccline.cmdbuff = alloc(len);    /* caller should check for out-of-memory */
    ccline.cmdbufflen = len;
}

/*
 * Re-allocate the command line to length len + something extra.
 * return FAIL for failure, OK otherwise
 */
    int
realloc_cmdbuff(int len)
{
    char_u	*p;

    if (len < ccline.cmdbufflen)
	return OK;			/* no need to resize */

    p = ccline.cmdbuff;
    alloc_cmdbuff(len);			/* will get some more */
    if (ccline.cmdbuff == NULL)		/* out of memory */
    {
	ccline.cmdbuff = p;		/* keep the old one */
	return FAIL;
    }
    /* There isn't always a NUL after the command, but it may need to be
     * there, thus copy up to the NUL and add a NUL. */
    mch_memmove(ccline.cmdbuff, p, (size_t)ccline.cmdlen);
    ccline.cmdbuff[ccline.cmdlen] = NUL;
    vim_free(p);

    if (ccline.xpc != NULL
	    && ccline.xpc->xp_pattern != NULL
	    && ccline.xpc->xp_context != EXPAND_NOTHING
	    && ccline.xpc->xp_context != EXPAND_UNSUCCESSFUL)
    {
	int i = (int)(ccline.xpc->xp_pattern - p);

	/* If xp_pattern points inside the old cmdbuff it needs to be adjusted
	 * to point into the newly allocated memory. */
	if (i >= 0 && i <= ccline.cmdlen)
	    ccline.xpc->xp_pattern = ccline.cmdbuff + i;
    }

    return OK;
}

#if defined(FEAT_ARABIC) || defined(PROTO)
static char_u	*arshape_buf = NULL;

# if defined(EXITFREE) || defined(PROTO)
    void
free_arshape_buf(void)
{
    vim_free(arshape_buf);
}
# endif
#endif

/*
 * Draw part of the cmdline at the current cursor position.  But draw stars
 * when cmdline_star is TRUE.
 */
    static void
draw_cmdline(int start, int len)
{
#if defined(FEAT_CRYPT) || defined(FEAT_EVAL)
    int		i;

    if (cmdline_star > 0)
	for (i = 0; i < len; ++i)
	{
	    msg_putchar('*');
	    if (has_mbyte)
		i += (*mb_ptr2len)(ccline.cmdbuff + start + i) - 1;
	}
    else
#endif
#ifdef FEAT_ARABIC
	if (p_arshape && !p_tbidi && cmdline_has_arabic(start, len))
    {
	static int	buflen = 0;
	char_u		*p;
	int		j;
	int		newlen = 0;
	int		mb_l;
	int		pc, pc1 = 0;
	int		prev_c = 0;
	int		prev_c1 = 0;
	int		u8c;
	int		u8cc[MAX_MCO];
	int		nc = 0;

	/*
	 * Do arabic shaping into a temporary buffer.  This is very
	 * inefficient!
	 */
	if (len * 2 + 2 > buflen)
	{
	    /* Re-allocate the buffer.  We keep it around to avoid a lot of
	     * alloc()/free() calls. */
	    vim_free(arshape_buf);
	    buflen = len * 2 + 2;
	    arshape_buf = alloc(buflen);
	    if (arshape_buf == NULL)
		return;	/* out of memory */
	}

	if (utf_iscomposing(utf_ptr2char(ccline.cmdbuff + start)))
	{
	    /* Prepend a space to draw the leading composing char on. */
	    arshape_buf[0] = ' ';
	    newlen = 1;
	}

	for (j = start; j < start + len; j += mb_l)
	{
	    p = ccline.cmdbuff + j;
	    u8c = utfc_ptr2char_len(p, u8cc, start + len - j);
	    mb_l = utfc_ptr2len_len(p, start + len - j);
	    if (ARABIC_CHAR(u8c))
	    {
		/* Do Arabic shaping. */
		if (cmdmsg_rl)
		{
		    /* displaying from right to left */
		    pc = prev_c;
		    pc1 = prev_c1;
		    prev_c1 = u8cc[0];
		    if (j + mb_l >= start + len)
			nc = NUL;
		    else
			nc = utf_ptr2char(p + mb_l);
		}
		else
		{
		    /* displaying from left to right */
		    if (j + mb_l >= start + len)
			pc = NUL;
		    else
		    {
			int	pcc[MAX_MCO];

			pc = utfc_ptr2char_len(p + mb_l, pcc,
						      start + len - j - mb_l);
			pc1 = pcc[0];
		    }
		    nc = prev_c;
		}
		prev_c = u8c;

		u8c = arabic_shape(u8c, NULL, &u8cc[0], pc, pc1, nc);

		newlen += (*mb_char2bytes)(u8c, arshape_buf + newlen);
		if (u8cc[0] != 0)
		{
		    newlen += (*mb_char2bytes)(u8cc[0], arshape_buf + newlen);
		    if (u8cc[1] != 0)
			newlen += (*mb_char2bytes)(u8cc[1],
							arshape_buf + newlen);
		}
	    }
	    else
	    {
		prev_c = u8c;
		mch_memmove(arshape_buf + newlen, p, mb_l);
		newlen += mb_l;
	    }
	}

	msg_outtrans_len(arshape_buf, newlen);
    }
    else
#endif
	msg_outtrans_len(ccline.cmdbuff + start, len);
}

/*
 * Put a character on the command line.  Shifts the following text to the
 * right when "shift" is TRUE.  Used for CTRL-V, CTRL-K, etc.
 * "c" must be printable (fit in one display cell)!
 */
    void
putcmdline(int c, int shift)
{
    if (cmd_silent)
	return;
    msg_no_more = TRUE;
    msg_putchar(c);
    if (shift)
	draw_cmdline(ccline.cmdpos, ccline.cmdlen - ccline.cmdpos);
    msg_no_more = FALSE;
    cursorcmd();
    extra_char = c;
    extra_char_shift = shift;
}

/*
 * Undo a putcmdline(c, FALSE).
 */
    void
unputcmdline(void)
{
    if (cmd_silent)
	return;
    msg_no_more = TRUE;
    if (ccline.cmdlen == ccline.cmdpos)
	msg_putchar(' ');
    else if (has_mbyte)
	draw_cmdline(ccline.cmdpos,
			       (*mb_ptr2len)(ccline.cmdbuff + ccline.cmdpos));
    else
	draw_cmdline(ccline.cmdpos, 1);
    msg_no_more = FALSE;
    cursorcmd();
    extra_char = NUL;
}

/*
 * Put the given string, of the given length, onto the command line.
 * If len is -1, then STRLEN() is used to calculate the length.
 * If 'redraw' is TRUE then the new part of the command line, and the remaining
 * part will be redrawn, otherwise it will not.  If this function is called
 * twice in a row, then 'redraw' should be FALSE and redrawcmd() should be
 * called afterwards.
 */
    int
put_on_cmdline(char_u *str, int len, int redraw)
{
    int		retval;
    int		i;
    int		m;
    int		c;

    if (len < 0)
	len = (int)STRLEN(str);

    /* Check if ccline.cmdbuff needs to be longer */
    if (ccline.cmdlen + len + 1 >= ccline.cmdbufflen)
	retval = realloc_cmdbuff(ccline.cmdlen + len + 1);
    else
	retval = OK;
    if (retval == OK)
    {
	if (!ccline.overstrike)
	{
	    mch_memmove(ccline.cmdbuff + ccline.cmdpos + len,
					       ccline.cmdbuff + ccline.cmdpos,
				     (size_t)(ccline.cmdlen - ccline.cmdpos));
	    ccline.cmdlen += len;
	}
	else
	{
	    if (has_mbyte)
	    {
		/* Count nr of characters in the new string. */
		m = 0;
		for (i = 0; i < len; i += (*mb_ptr2len)(str + i))
		    ++m;
		/* Count nr of bytes in cmdline that are overwritten by these
		 * characters. */
		for (i = ccline.cmdpos; i < ccline.cmdlen && m > 0;
				 i += (*mb_ptr2len)(ccline.cmdbuff + i))
		    --m;
		if (i < ccline.cmdlen)
		{
		    mch_memmove(ccline.cmdbuff + ccline.cmdpos + len,
			    ccline.cmdbuff + i, (size_t)(ccline.cmdlen - i));
		    ccline.cmdlen += ccline.cmdpos + len - i;
		}
		else
		    ccline.cmdlen = ccline.cmdpos + len;
	    }
	    else if (ccline.cmdpos + len > ccline.cmdlen)
		ccline.cmdlen = ccline.cmdpos + len;
	}
	mch_memmove(ccline.cmdbuff + ccline.cmdpos, str, (size_t)len);
	ccline.cmdbuff[ccline.cmdlen] = NUL;

	if (enc_utf8)
	{
	    /* When the inserted text starts with a composing character,
	     * backup to the character before it.  There could be two of them.
	     */
	    i = 0;
	    c = utf_ptr2char(ccline.cmdbuff + ccline.cmdpos);
	    while (ccline.cmdpos > 0 && utf_iscomposing(c))
	    {
		i = (*mb_head_off)(ccline.cmdbuff,
				      ccline.cmdbuff + ccline.cmdpos - 1) + 1;
		ccline.cmdpos -= i;
		len += i;
		c = utf_ptr2char(ccline.cmdbuff + ccline.cmdpos);
	    }
#ifdef FEAT_ARABIC
	    if (i == 0 && ccline.cmdpos > 0 && arabic_maycombine(c))
	    {
		/* Check the previous character for Arabic combining pair. */
		i = (*mb_head_off)(ccline.cmdbuff,
				      ccline.cmdbuff + ccline.cmdpos - 1) + 1;
		if (arabic_combine(utf_ptr2char(ccline.cmdbuff
						     + ccline.cmdpos - i), c))
		{
		    ccline.cmdpos -= i;
		    len += i;
		}
		else
		    i = 0;
	    }
#endif
	    if (i != 0)
	    {
		/* Also backup the cursor position. */
		i = ptr2cells(ccline.cmdbuff + ccline.cmdpos);
		ccline.cmdspos -= i;
		msg_col -= i;
		if (msg_col < 0)
		{
		    msg_col += Columns;
		    --msg_row;
		}
	    }
	}

	if (redraw && !cmd_silent)
	{
	    msg_no_more = TRUE;
	    i = cmdline_row;
	    cursorcmd();
	    draw_cmdline(ccline.cmdpos, ccline.cmdlen - ccline.cmdpos);
	    /* Avoid clearing the rest of the line too often. */
	    if (cmdline_row != i || ccline.overstrike)
		msg_clr_eos();
	    msg_no_more = FALSE;
	}
	if (KeyTyped)
	{
	    m = Columns * Rows;
	    if (m < 0)	/* overflow, Columns or Rows at weird value */
		m = MAXCOL;
	}
	else
	    m = MAXCOL;
	for (i = 0; i < len; ++i)
	{
	    c = cmdline_charsize(ccline.cmdpos);
	    /* count ">" for a double-wide char that doesn't fit. */
	    if (has_mbyte)
		correct_cmdspos(ccline.cmdpos, c);
	    /* Stop cursor at the end of the screen, but do increment the
	     * insert position, so that entering a very long command
	     * works, even though you can't see it. */
	    if (ccline.cmdspos + c < m)
		ccline.cmdspos += c;

	    if (has_mbyte)
	    {
		c = (*mb_ptr2len)(ccline.cmdbuff + ccline.cmdpos) - 1;
		if (c > len - i - 1)
		    c = len - i - 1;
		ccline.cmdpos += c;
		i += c;
	    }
	    ++ccline.cmdpos;
	}
    }
    if (redraw)
	msg_check();
    return retval;
}

static cmdline_info_T	prev_ccline;
static int		prev_ccline_used = FALSE;

/*
 * Save ccline, because obtaining the "=" register may execute "normal :cmd"
 * and overwrite it.  But get_cmdline_str() may need it, thus make it
 * available globally in prev_ccline.
 */
    static void
save_cmdline(cmdline_info_T *ccp)
{
    if (!prev_ccline_used)
    {
	vim_memset(&prev_ccline, 0, sizeof(cmdline_info_T));
	prev_ccline_used = TRUE;
    }
    *ccp = prev_ccline;
    prev_ccline = ccline;
    ccline.cmdbuff = NULL;  // signal that ccline is not in use
}

/*
 * Restore ccline after it has been saved with save_cmdline().
 */
    static void
restore_cmdline(cmdline_info_T *ccp)
{
    ccline = prev_ccline;
    prev_ccline = *ccp;
}

/*
 * Paste a yank register into the command line.
 * Used by CTRL-R command in command-line mode.
 * insert_reg() can't be used here, because special characters from the
 * register contents will be interpreted as commands.
 *
 * Return FAIL for failure, OK otherwise.
 */
    static int
cmdline_paste(
    int regname,
    int literally,	/* Insert text literally instead of "as typed" */
    int remcr)		/* remove trailing CR */
{
    long		i;
    char_u		*arg;
    char_u		*p;
    int			allocated;

    /* check for valid regname; also accept special characters for CTRL-R in
     * the command line */
    if (regname != Ctrl_F && regname != Ctrl_P && regname != Ctrl_W
	    && regname != Ctrl_A && regname != Ctrl_L
	    && !valid_yank_reg(regname, FALSE))
	return FAIL;

    /* A register containing CTRL-R can cause an endless loop.  Allow using
     * CTRL-C to break the loop. */
    line_breakcheck();
    if (got_int)
	return FAIL;

#ifdef FEAT_CLIPBOARD
    regname = may_get_selection(regname);
#endif

    // Need to  set "textlock" to avoid nasty things like going to another
    // buffer when evaluating an expression.
    ++textlock;
    i = get_spec_reg(regname, &arg, &allocated, TRUE);
    --textlock;

    if (i)
    {
	/* Got the value of a special register in "arg". */
	if (arg == NULL)
	    return FAIL;

	/* When 'incsearch' is set and CTRL-R CTRL-W used: skip the duplicate
	 * part of the word. */
	p = arg;
	if (p_is && regname == Ctrl_W)
	{
	    char_u  *w;
	    int	    len;

	    /* Locate start of last word in the cmd buffer. */
	    for (w = ccline.cmdbuff + ccline.cmdpos; w > ccline.cmdbuff; )
	    {
		if (has_mbyte)
		{
		    len = (*mb_head_off)(ccline.cmdbuff, w - 1) + 1;
		    if (!vim_iswordc(mb_ptr2char(w - len)))
			break;
		    w -= len;
		}
		else
		{
		    if (!vim_iswordc(w[-1]))
			break;
		    --w;
		}
	    }
	    len = (int)((ccline.cmdbuff + ccline.cmdpos) - w);
	    if (p_ic ? STRNICMP(w, arg, len) == 0 : STRNCMP(w, arg, len) == 0)
		p += len;
	}

	cmdline_paste_str(p, literally);
	if (allocated)
	    vim_free(arg);
	return OK;
    }

    return cmdline_paste_reg(regname, literally, remcr);
}

/*
 * Put a string on the command line.
 * When "literally" is TRUE, insert literally.
 * When "literally" is FALSE, insert as typed, but don't leave the command
 * line.
 */
    void
cmdline_paste_str(char_u *s, int literally)
{
    int		c, cv;

    if (literally)
	put_on_cmdline(s, -1, TRUE);
    else
	while (*s != NUL)
	{
	    cv = *s;
	    if (cv == Ctrl_V && s[1])
		++s;
	    if (has_mbyte)
		c = mb_cptr2char_adv(&s);
	    else
		c = *s++;
	    if (cv == Ctrl_V || c == ESC || c == Ctrl_C
		    || c == CAR || c == NL || c == Ctrl_L
#ifdef UNIX
		    || c == intr_char
#endif
		    || (c == Ctrl_BSL && *s == Ctrl_N))
		stuffcharReadbuff(Ctrl_V);
	    stuffcharReadbuff(c);
	}
}

#ifdef FEAT_WILDMENU
/*
 * Delete characters on the command line, from "from" to the current
 * position.
 */
    static void
cmdline_del(int from)
{
    mch_memmove(ccline.cmdbuff + from, ccline.cmdbuff + ccline.cmdpos,
	    (size_t)(ccline.cmdlen - ccline.cmdpos + 1));
    ccline.cmdlen -= ccline.cmdpos - from;
    ccline.cmdpos = from;
}
#endif

/*
 * This function is called when the screen size changes and with incremental
 * search and in other situations where the command line may have been
 * overwritten.
 */
    void
redrawcmdline(void)
{
    redrawcmdline_ex(TRUE);
}

    void
redrawcmdline_ex(int do_compute_cmdrow)
{
    if (cmd_silent)
	return;
    need_wait_return = FALSE;
    if (do_compute_cmdrow)
	compute_cmdrow();
    redrawcmd();
    cursorcmd();
}

    static void
redrawcmdprompt(void)
{
    int		i;

    if (cmd_silent)
	return;
    if (ccline.cmdfirstc != NUL)
	msg_putchar(ccline.cmdfirstc);
    if (ccline.cmdprompt != NULL)
    {
	msg_puts_attr((char *)ccline.cmdprompt, ccline.cmdattr);
	ccline.cmdindent = msg_col + (msg_row - cmdline_row) * Columns;
	/* do the reverse of set_cmdspos() */
	if (ccline.cmdfirstc != NUL)
	    --ccline.cmdindent;
    }
    else
	for (i = ccline.cmdindent; i > 0; --i)
	    msg_putchar(' ');
}

/*
 * Redraw what is currently on the command line.
 */
    void
redrawcmd(void)
{
    if (cmd_silent)
	return;

    /* when 'incsearch' is set there may be no command line while redrawing */
    if (ccline.cmdbuff == NULL)
    {
	windgoto(cmdline_row, 0);
	msg_clr_eos();
	return;
    }

    msg_start();
    redrawcmdprompt();

    /* Don't use more prompt, truncate the cmdline if it doesn't fit. */
    msg_no_more = TRUE;
    draw_cmdline(0, ccline.cmdlen);
    msg_clr_eos();
    msg_no_more = FALSE;

    set_cmdspos_cursor();
    if (extra_char != NUL)
	putcmdline(extra_char, extra_char_shift);

    /*
     * An emsg() before may have set msg_scroll. This is used in normal mode,
     * in cmdline mode we can reset them now.
     */
    msg_scroll = FALSE;		/* next message overwrites cmdline */

    /* Typing ':' at the more prompt may set skip_redraw.  We don't want this
     * in cmdline mode */
    skip_redraw = FALSE;
}

    void
compute_cmdrow(void)
{
    if (exmode_active || msg_scrolled != 0)
	cmdline_row = Rows - 1;
    else
	cmdline_row = W_WINROW(lastwin) + lastwin->w_height
						    + lastwin->w_status_height;
}

    void
cursorcmd(void)
{
    if (cmd_silent)
	return;

#ifdef FEAT_RIGHTLEFT
    if (cmdmsg_rl)
    {
	msg_row = cmdline_row  + (ccline.cmdspos / (int)(Columns - 1));
	msg_col = (int)Columns - (ccline.cmdspos % (int)(Columns - 1)) - 1;
	if (msg_row <= 0)
	    msg_row = Rows - 1;
    }
    else
#endif
    {
	msg_row = cmdline_row + (ccline.cmdspos / (int)Columns);
	msg_col = ccline.cmdspos % (int)Columns;
	if (msg_row >= Rows)
	    msg_row = Rows - 1;
    }

    windgoto(msg_row, msg_col);
#if defined(FEAT_XIM) && defined(FEAT_GUI_GTK)
    if (p_imst == IM_ON_THE_SPOT)
	redrawcmd_preedit();
#endif
#ifdef MCH_CURSOR_SHAPE
    mch_update_cursor();
#endif
}

    void
gotocmdline(int clr)
{
    msg_start();
#ifdef FEAT_RIGHTLEFT
    if (cmdmsg_rl)
	msg_col = Columns - 1;
    else
#endif
	msg_col = 0;	    /* always start in column 0 */
    if (clr)		    /* clear the bottom line(s) */
	msg_clr_eos();	    /* will reset clear_cmdline */
    windgoto(cmdline_row, 0);
}

/*
 * Check the word in front of the cursor for an abbreviation.
 * Called when the non-id character "c" has been entered.
 * When an abbreviation is recognized it is removed from the text with
 * backspaces and the replacement string is inserted, followed by "c".
 */
    static int
ccheck_abbr(int c)
{
    int spos = 0;

    if (p_paste || no_abbr)	    /* no abbreviations or in paste mode */
	return FALSE;

    /* Do not consider '<,'> be part of the mapping, skip leading whitespace.
     * Actually accepts any mark. */
    while (VIM_ISWHITE(ccline.cmdbuff[spos]) && spos < ccline.cmdlen)
	spos++;
    if (ccline.cmdlen - spos > 5
	    && ccline.cmdbuff[spos] == '\''
	    && ccline.cmdbuff[spos + 2] == ','
	    && ccline.cmdbuff[spos + 3] == '\'')
	spos += 5;
    else
	/* check abbreviation from the beginning of the commandline */
	spos = 0;

    return check_abbr(c, ccline.cmdbuff, ccline.cmdpos, spos);
}

/*
 * Escape special characters in "fname" for when used as a file name argument
 * after a Vim command, or, when "shell" is non-zero, a shell command.
 * Returns the result in allocated memory.
 */
    char_u *
vim_strsave_fnameescape(char_u *fname, int shell UNUSED)
{
    char_u	*p;
#ifdef BACKSLASH_IN_FILENAME
    char_u	buf[20];
    int		j = 0;

    /* Don't escape '[', '{' and '!' if they are in 'isfname'. */
    for (p = PATH_ESC_CHARS; *p != NUL; ++p)
	if ((*p != '[' && *p != '{' && *p != '!') || !vim_isfilec(*p))
	    buf[j++] = *p;
    buf[j] = NUL;
    p = vim_strsave_escaped(fname, buf);
#else
    p = vim_strsave_escaped(fname, shell ? SHELL_ESC_CHARS : PATH_ESC_CHARS);
    if (shell && csh_like_shell() && p != NULL)
    {
	char_u	    *s;

	/* For csh and similar shells need to put two backslashes before '!'.
	 * One is taken by Vim, one by the shell. */
	s = vim_strsave_escaped(p, (char_u *)"!");
	vim_free(p);
	p = s;
    }
#endif

    /* '>' and '+' are special at the start of some commands, e.g. ":edit" and
     * ":write".  "cd -" has a special meaning. */
    if (p != NULL && (*p == '>' || *p == '+' || (*p == '-' && p[1] == NUL)))
	escape_fname(&p);

    return p;
}

/*
 * Put a backslash before the file name in "pp", which is in allocated memory.
 */
    void
escape_fname(char_u **pp)
{
    char_u	*p;

    p = alloc(STRLEN(*pp) + 2);
    if (p != NULL)
    {
	p[0] = '\\';
	STRCPY(p + 1, *pp);
	vim_free(*pp);
	*pp = p;
    }
}

/*
 * For each file name in files[num_files]:
 * If 'orig_pat' starts with "~/", replace the home directory with "~".
 */
    void
tilde_replace(
    char_u  *orig_pat,
    int	    num_files,
    char_u  **files)
{
    int	    i;
    char_u  *p;

    if (orig_pat[0] == '~' && vim_ispathsep(orig_pat[1]))
    {
	for (i = 0; i < num_files; ++i)
	{
	    p = home_replace_save(NULL, files[i]);
	    if (p != NULL)
	    {
		vim_free(files[i]);
		files[i] = p;
	    }
	}
    }
}

/*
 * Get a pointer to the current command line info.
 */
    cmdline_info_T *
get_cmdline_info(void)
{
    return &ccline;
}

#if defined(FEAT_EVAL) || defined(FEAT_CMDWIN) || defined(PROTO)
/*
 * Get pointer to the command line info to use. save_ccline() may clear
 * ccline and put the previous value in prev_ccline.
 */
    static cmdline_info_T *
get_ccline_ptr(void)
{
    if ((State & CMDLINE) == 0)
	return NULL;
    if (ccline.cmdbuff != NULL)
	return &ccline;
    if (prev_ccline_used && prev_ccline.cmdbuff != NULL)
	return &prev_ccline;
    return NULL;
}
#endif

#if defined(FEAT_EVAL) || defined(PROTO)
/*
 * Get the current command line in allocated memory.
 * Only works when the command line is being edited.
 * Returns NULL when something is wrong.
 */
    static char_u *
get_cmdline_str(void)
{
    cmdline_info_T *p;

    if (cmdline_star > 0)
	return NULL;
    p = get_ccline_ptr();
    if (p == NULL)
	return NULL;
    return vim_strnsave(p->cmdbuff, p->cmdlen);
}

/*
 * "getcmdline()" function
 */
    void
f_getcmdline(typval_T *argvars UNUSED, typval_T *rettv)
{
    rettv->v_type = VAR_STRING;
    rettv->vval.v_string = get_cmdline_str();
}

/*
 * "getcmdpos()" function
 */
    void
f_getcmdpos(typval_T *argvars UNUSED, typval_T *rettv)
{
    cmdline_info_T *p = get_ccline_ptr();

    rettv->vval.v_number = 0;
    if (p != NULL)
    rettv->vval.v_number = p->cmdpos + 1;
}

/*
 * Set the command line byte position to "pos".  Zero is the first position.
 * Only works when the command line is being edited.
 * Returns 1 when failed, 0 when OK.
 */
    static int
set_cmdline_pos(
    int		pos)
{
    cmdline_info_T *p = get_ccline_ptr();

    if (p == NULL)
	return 1;

    /* The position is not set directly but after CTRL-\ e or CTRL-R = has
     * changed the command line. */
    if (pos < 0)
	new_cmdpos = 0;
    else
	new_cmdpos = pos;
    return 0;
}

/*
 * "setcmdpos()" function
 */
    void
f_setcmdpos(typval_T *argvars, typval_T *rettv)
{
    int		pos = (int)tv_get_number(&argvars[0]) - 1;

    if (pos >= 0)
	rettv->vval.v_number = set_cmdline_pos(pos);
}

/*
 * "getcmdtype()" function
 */
    void
f_getcmdtype(typval_T *argvars UNUSED, typval_T *rettv)
{
    rettv->v_type = VAR_STRING;
    rettv->vval.v_string = alloc(2);
    if (rettv->vval.v_string != NULL)
    {
	rettv->vval.v_string[0] = get_cmdline_type();
	rettv->vval.v_string[1] = NUL;
    }
}

#endif

#if defined(FEAT_EVAL) || defined(FEAT_CMDWIN) || defined(PROTO)
/*
 * Get the current command-line type.
 * Returns ':' or '/' or '?' or '@' or '>' or '-'
 * Only works when the command line is being edited.
 * Returns NUL when something is wrong.
 */
    int
get_cmdline_type(void)
{
    cmdline_info_T *p = get_ccline_ptr();

    if (p == NULL)
	return NUL;
    if (p->cmdfirstc == NUL)
	return
# ifdef FEAT_EVAL
	    (p->input_fn) ? '@' :
# endif
	    '-';
    return p->cmdfirstc;
}
#endif

/*
 * Return the first character of the current command line.
 */
    int
get_cmdline_firstc(void)
{
    return ccline.cmdfirstc;
}

/*
 * Get indices "num1,num2" that specify a range within a list (not a range of
 * text lines in a buffer!) from a string.  Used for ":history" and ":clist".
 * Returns OK if parsed successfully, otherwise FAIL.
 */
    int
get_list_range(char_u **str, int *num1, int *num2)
{
    int		len;
    int		first = FALSE;
    varnumber_T	num;

    *str = skipwhite(*str);
    if (**str == '-' || vim_isdigit(**str))  /* parse "from" part of range */
    {
	vim_str2nr(*str, NULL, &len, 0, &num, NULL, 0, FALSE);
	*str += len;
	*num1 = (int)num;
	first = TRUE;
    }
    *str = skipwhite(*str);
    if (**str == ',')			/* parse "to" part of range */
    {
	*str = skipwhite(*str + 1);
	vim_str2nr(*str, NULL, &len, 0, &num, NULL, 0, FALSE);
	if (len > 0)
	{
	    *num2 = (int)num;
	    *str = skipwhite(*str + len);
	}
	else if (!first)		/* no number given at all */
	    return FAIL;
    }
    else if (first)			/* only one number given */
	*num2 = *num1;
    return OK;
}

#if defined(FEAT_CMDWIN) || defined(PROTO)
/*
 * Open a window on the current command line and history.  Allow editing in
 * the window.  Returns when the window is closed.
 * Returns:
 *	CR	 if the command is to be executed
 *	Ctrl_C	 if it is to be abandoned
 *	K_IGNORE if editing continues
 */
    static int
open_cmdwin(void)
{
    bufref_T		old_curbuf;
    win_T		*old_curwin = curwin;
    bufref_T		bufref;
    win_T		*wp;
    int			i;
    linenr_T		lnum;
    int			histtype;
    garray_T		winsizes;
    int			save_restart_edit = restart_edit;
    int			save_State = State;
    int			save_exmode = exmode_active;
#ifdef FEAT_RIGHTLEFT
    int			save_cmdmsg_rl = cmdmsg_rl;
#endif
#ifdef FEAT_FOLDING
    int			save_KeyTyped;
#endif

    /* Can't do this recursively.  Can't do it when typing a password. */
    if (cmdwin_type != 0
# if defined(FEAT_CRYPT) || defined(FEAT_EVAL)
	    || cmdline_star > 0
# endif
	    )
    {
	beep_flush();
	return K_IGNORE;
    }
    set_bufref(&old_curbuf, curbuf);

    // Save current window sizes.
    win_size_save(&winsizes);

    // When using completion in Insert mode with <C-R>=<C-F> one can open the
    // command line window, but we don't want the popup menu then.
    pum_undisplay();

    // don't use a new tab page
    cmdmod.tab = 0;
    cmdmod.noswapfile = 1;

    // Create a window for the command-line buffer.
    if (win_split((int)p_cwh, WSP_BOT) == FAIL)
    {
	beep_flush();
	return K_IGNORE;
    }
    cmdwin_type = get_cmdline_type();

    // Create the command-line buffer empty.
    (void)do_ecmd(0, NULL, NULL, NULL, ECMD_ONE, ECMD_HIDE, NULL);
    (void)setfname(curbuf, (char_u *)"[Command Line]", NULL, TRUE);
    set_option_value((char_u *)"bt", 0L, (char_u *)"nofile", OPT_LOCAL);
    curbuf->b_p_ma = TRUE;
#ifdef FEAT_FOLDING
    curwin->w_p_fen = FALSE;
#endif
# ifdef FEAT_RIGHTLEFT
    curwin->w_p_rl = cmdmsg_rl;
    cmdmsg_rl = FALSE;
# endif
    RESET_BINDING(curwin);

    // Don't allow switching to another buffer.
    ++curbuf_lock;

    // Showing the prompt may have set need_wait_return, reset it.
    need_wait_return = FALSE;

    histtype = hist_char2type(cmdwin_type);
    if (histtype == HIST_CMD || histtype == HIST_DEBUG)
    {
	if (p_wc == TAB)
	{
	    add_map((char_u *)"<buffer> <Tab> <C-X><C-V>", INSERT);
	    add_map((char_u *)"<buffer> <Tab> a<C-X><C-V>", NORMAL);
	}
	set_option_value((char_u *)"ft", 0L, (char_u *)"vim", OPT_LOCAL);
    }
    --curbuf_lock;

    // Reset 'textwidth' after setting 'filetype' (the Vim filetype plugin
    // sets 'textwidth' to 78).
    curbuf->b_p_tw = 0;

    // Fill the buffer with the history.
    init_history();
    if (get_hislen() > 0)
    {
	i = *get_hisidx(histtype);
	if (i >= 0)
	{
	    lnum = 0;
	    do
	    {
		if (++i == get_hislen())
		    i = 0;
		if (get_histentry(histtype)[i].hisstr != NULL)
		    ml_append(lnum++, get_histentry(histtype)[i].hisstr,
							   (colnr_T)0, FALSE);
	    }
	    while (i != *get_hisidx(histtype));
	}
    }

    /* Replace the empty last line with the current command-line and put the
     * cursor there. */
    ml_replace(curbuf->b_ml.ml_line_count, ccline.cmdbuff, TRUE);
    curwin->w_cursor.lnum = curbuf->b_ml.ml_line_count;
    curwin->w_cursor.col = ccline.cmdpos;
    changed_line_abv_curs();
    invalidate_botline();
    redraw_later(SOME_VALID);

    // No Ex mode here!
    exmode_active = 0;

    State = NORMAL;
    setmouse();

    // Reset here so it can be set by a CmdWinEnter autocommand.
    cmdwin_result = 0;

    // Trigger CmdwinEnter autocommands.
    trigger_cmd_autocmd(cmdwin_type, EVENT_CMDWINENTER);
    if (restart_edit != 0)	// autocmd with ":startinsert"
	stuffcharReadbuff(K_NOP);

    i = RedrawingDisabled;
    RedrawingDisabled = 0;

    /*
     * Call the main loop until <CR> or CTRL-C is typed.
     */
    main_loop(TRUE, FALSE);

    RedrawingDisabled = i;

# ifdef FEAT_FOLDING
    save_KeyTyped = KeyTyped;
# endif

    // Trigger CmdwinLeave autocommands.
    trigger_cmd_autocmd(cmdwin_type, EVENT_CMDWINLEAVE);

# ifdef FEAT_FOLDING
    // Restore KeyTyped in case it is modified by autocommands
    KeyTyped = save_KeyTyped;
# endif

    cmdwin_type = 0;
    exmode_active = save_exmode;

    /* Safety check: The old window or buffer was deleted: It's a bug when
     * this happens! */
    if (!win_valid(old_curwin) || !bufref_valid(&old_curbuf))
    {
	cmdwin_result = Ctrl_C;
	emsg(_("E199: Active window or buffer deleted"));
    }
    else
    {
# if defined(FEAT_EVAL)
	/* autocmds may abort script processing */
	if (aborting() && cmdwin_result != K_IGNORE)
	    cmdwin_result = Ctrl_C;
# endif
	/* Set the new command line from the cmdline buffer. */
	vim_free(ccline.cmdbuff);
	if (cmdwin_result == K_XF1 || cmdwin_result == K_XF2) /* :qa[!] typed */
	{
	    char *p = (cmdwin_result == K_XF2) ? "qa" : "qa!";

	    if (histtype == HIST_CMD)
	    {
		/* Execute the command directly. */
		ccline.cmdbuff = vim_strsave((char_u *)p);
		cmdwin_result = CAR;
	    }
	    else
	    {
		/* First need to cancel what we were doing. */
		ccline.cmdbuff = NULL;
		stuffcharReadbuff(':');
		stuffReadbuff((char_u *)p);
		stuffcharReadbuff(CAR);
	    }
	}
	else if (cmdwin_result == K_XF2)	/* :qa typed */
	{
	    ccline.cmdbuff = vim_strsave((char_u *)"qa");
	    cmdwin_result = CAR;
	}
	else if (cmdwin_result == Ctrl_C)
	{
	    /* :q or :close, don't execute any command
	     * and don't modify the cmd window. */
	    ccline.cmdbuff = NULL;
	}
	else
	    ccline.cmdbuff = vim_strsave(ml_get_curline());
	if (ccline.cmdbuff == NULL)
	{
	    ccline.cmdbuff = vim_strsave((char_u *)"");
	    ccline.cmdlen = 0;
	    ccline.cmdbufflen = 1;
	    ccline.cmdpos = 0;
	    cmdwin_result = Ctrl_C;
	}
	else
	{
	    ccline.cmdlen = (int)STRLEN(ccline.cmdbuff);
	    ccline.cmdbufflen = ccline.cmdlen + 1;
	    ccline.cmdpos = curwin->w_cursor.col;
	    if (ccline.cmdpos > ccline.cmdlen)
		ccline.cmdpos = ccline.cmdlen;
	    if (cmdwin_result == K_IGNORE)
	    {
		set_cmdspos_cursor();
		redrawcmd();
	    }
	}

# ifdef FEAT_CONCEAL
	// Avoid command-line window first character being concealed.
	curwin->w_p_cole = 0;
# endif
	wp = curwin;
	set_bufref(&bufref, curbuf);
	win_goto(old_curwin);
	win_close(wp, TRUE);

	// win_close() may have already wiped the buffer when 'bh' is
	// set to 'wipe'
	if (bufref_valid(&bufref))
	    close_buffer(NULL, bufref.br_buf, DOBUF_WIPE, FALSE);

	// Restore window sizes.
	win_size_restore(&winsizes);
    }

    ga_clear(&winsizes);
    restart_edit = save_restart_edit;
# ifdef FEAT_RIGHTLEFT
    cmdmsg_rl = save_cmdmsg_rl;
# endif

    State = save_State;
    setmouse();

    return cmdwin_result;
}
#endif // FEAT_CMDWIN

/*
 * Used for commands that either take a simple command string argument, or:
 *	cmd << endmarker
 *	  {script}
 *	endmarker
 * Returns a pointer to allocated memory with {script} or NULL.
 */
    char_u *
script_get(exarg_T *eap, char_u *cmd)
{
    char_u	*theline;
    char	*end_pattern = NULL;
    char	dot[] = ".";
    garray_T	ga;

    if (cmd[0] != '<' || cmd[1] != '<' || eap->getline == NULL)
	return NULL;

    ga_init2(&ga, 1, 0x400);

    if (cmd[2] != NUL)
	end_pattern = (char *)skipwhite(cmd + 2);
    else
	end_pattern = dot;

    for (;;)
    {
	theline = eap->getline(
#ifdef FEAT_EVAL
	    eap->cstack->cs_looplevel > 0 ? -1 :
#endif
	    NUL, eap->cookie, 0, TRUE);

	if (theline == NULL || STRCMP(end_pattern, theline) == 0)
	{
	    vim_free(theline);
	    break;
	}

	ga_concat(&ga, theline);
	ga_append(&ga, '\n');
	vim_free(theline);
    }
    ga_append(&ga, NUL);

    return (char_u *)ga.ga_data;
}

#if defined(FEAT_EVAL) || defined(PROTO)
/*
 * This function is used by f_input() and f_inputdialog() functions. The third
 * argument to f_input() specifies the type of completion to use at the
 * prompt. The third argument to f_inputdialog() specifies the value to return
 * when the user cancels the prompt.
 */
    void
get_user_input(
    typval_T	*argvars,
    typval_T	*rettv,
    int		inputdialog,
    int		secret)
{
    char_u	*prompt = tv_get_string_chk(&argvars[0]);
    char_u	*p = NULL;
    int		c;
    char_u	buf[NUMBUFLEN];
    int		cmd_silent_save = cmd_silent;
    char_u	*defstr = (char_u *)"";
    int		xp_type = EXPAND_NOTHING;
    char_u	*xp_arg = NULL;

    rettv->v_type = VAR_STRING;
    rettv->vval.v_string = NULL;

#ifdef NO_CONSOLE_INPUT
    // While starting up, there is no place to enter text. When running tests
    // with --not-a-term we assume feedkeys() will be used.
    if (no_console_input() && !is_not_a_term())
	return;
#endif

    cmd_silent = FALSE;		// Want to see the prompt.
    if (prompt != NULL)
    {
	// Only the part of the message after the last NL is considered as
	// prompt for the command line
	p = vim_strrchr(prompt, '\n');
	if (p == NULL)
	    p = prompt;
	else
	{
	    ++p;
	    c = *p;
	    *p = NUL;
	    msg_start();
	    msg_clr_eos();
	    msg_puts_attr((char *)prompt, get_echo_attr());
	    msg_didout = FALSE;
	    msg_starthere();
	    *p = c;
	}
	cmdline_row = msg_row;

	if (argvars[1].v_type != VAR_UNKNOWN)
	{
	    defstr = tv_get_string_buf_chk(&argvars[1], buf);
	    if (defstr != NULL)
		stuffReadbuffSpec(defstr);

	    if (!inputdialog && argvars[2].v_type != VAR_UNKNOWN)
	    {
		char_u	*xp_name;
		int	xp_namelen;
		long	argt;

		// input() with a third argument: completion
		rettv->vval.v_string = NULL;

		xp_name = tv_get_string_buf_chk(&argvars[2], buf);
		if (xp_name == NULL)
		    return;

		xp_namelen = (int)STRLEN(xp_name);

		if (parse_compl_arg(xp_name, xp_namelen, &xp_type, &argt,
							     &xp_arg) == FAIL)
		    return;
	    }
	}

	if (defstr != NULL)
	{
	    int save_ex_normal_busy = ex_normal_busy;

	    ex_normal_busy = 0;
	    rettv->vval.v_string =
		getcmdline_prompt(secret ? NUL : '@', p, get_echo_attr(),
							      xp_type, xp_arg);
	    ex_normal_busy = save_ex_normal_busy;
	}
	if (inputdialog && rettv->vval.v_string == NULL
		&& argvars[1].v_type != VAR_UNKNOWN
		&& argvars[2].v_type != VAR_UNKNOWN)
	    rettv->vval.v_string = vim_strsave(tv_get_string_buf(
							   &argvars[2], buf));

	vim_free(xp_arg);

	// since the user typed this, no need to wait for return
	need_wait_return = FALSE;
	msg_didout = FALSE;
    }
    cmd_silent = cmd_silent_save;
}
#endif
