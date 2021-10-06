/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * register.c: functions for managing registers
 */

#include "vim.h"

/*
 * Registers:
 *	0 = unnamed register, for normal yanks and puts
 *   1..9 = registers '1' to '9', for deletes
 * 10..35 = registers 'a' to 'z' ('A' to 'Z' for appending)
 *     36 = delete register '-'
 *     37 = Selection register '*'. Only if FEAT_CLIPBOARD defined
 *     38 = Clipboard register '+'. Only if FEAT_CLIPBOARD and FEAT_X11 defined
 */
static yankreg_T	y_regs[NUM_REGISTERS];

static yankreg_T	*y_current;	    // ptr to current yankreg
static int		y_append;	    // TRUE when appending
static yankreg_T	*y_previous = NULL; // ptr to last written yankreg

static int	stuff_yank(int, char_u *);
static void	put_reedit_in_typebuf(int silent);
static int	put_in_typebuf(char_u *s, int esc, int colon,
								 int silent);
static void	free_yank_all(void);
static int	yank_copy_line(struct block_def *bd, long y_idx);
#ifdef FEAT_CLIPBOARD
static void	copy_yank_reg(yankreg_T *reg);
static void	may_set_selection(void);
#endif
static void	dis_msg(char_u *p, int skip_esc);
#if defined(FEAT_CLIPBOARD) || defined(FEAT_EVAL)
static void	str_to_reg(yankreg_T *y_ptr, int yank_type, char_u *str, long len, long blocklen, int str_list);
#endif

    yankreg_T *
get_y_regs(void)
{
    return y_regs;
}

    yankreg_T *
get_y_current(void)
{
    return y_current;
}

    yankreg_T *
get_y_previous(void)
{
    return y_previous;
}

    void
set_y_previous(yankreg_T *yreg)
{
    y_previous = yreg;
}

#if defined(FEAT_EVAL) || defined(PROTO)
/*
 * Keep the last expression line here, for repeating.
 */
static char_u	*expr_line = NULL;

/*
 * Get an expression for the "\"=expr1" or "CTRL-R =expr1"
 * Returns '=' when OK, NUL otherwise.
 */
    int
get_expr_register(void)
{
    char_u	*new_line;

    new_line = getcmdline('=', 0L, 0, TRUE);
    if (new_line == NULL)
	return NUL;
    if (*new_line == NUL)	// use previous line
	vim_free(new_line);
    else
	set_expr_line(new_line);
    return '=';
}

/*
 * Set the expression for the '=' register.
 * Argument must be an allocated string.
 */
    void
set_expr_line(char_u *new_line)
{
    vim_free(expr_line);
    expr_line = new_line;
}

/*
 * Get the result of the '=' register expression.
 * Returns a pointer to allocated memory, or NULL for failure.
 */
    char_u *
get_expr_line(void)
{
    char_u	*expr_copy;
    char_u	*rv;
    static int	nested = 0;

    if (expr_line == NULL)
	return NULL;

    // Make a copy of the expression, because evaluating it may cause it to be
    // changed.
    expr_copy = vim_strsave(expr_line);
    if (expr_copy == NULL)
	return NULL;

    // When we are invoked recursively limit the evaluation to 10 levels.
    // Then return the string as-is.
    if (nested >= 10)
	return expr_copy;

    ++nested;
    rv = eval_to_string(expr_copy, NULL, TRUE);
    --nested;
    vim_free(expr_copy);
    return rv;
}

/*
 * Get the '=' register expression itself, without evaluating it.
 */
    static char_u *
get_expr_line_src(void)
{
    if (expr_line == NULL)
	return NULL;
    return vim_strsave(expr_line);
}
#endif // FEAT_EVAL

/*
 * Check if 'regname' is a valid name of a yank register.
 * Note: There is no check for 0 (default register), caller should do this
 */
    int
valid_yank_reg(
    int	    regname,
    int	    writing)	    // if TRUE check for writable registers
{
    if (       (regname > 0 && ASCII_ISALNUM(regname))
	    || (!writing && vim_strchr((char_u *)
#ifdef FEAT_EVAL
				    "/.%:="
#else
				    "/.%:"
#endif
					, regname) != NULL)
	    || regname == '#'
	    || regname == '"'
	    || regname == '-'
	    || regname == '_'
#ifdef FEAT_CLIPBOARD
	    || regname == '*'
	    || regname == '+'
#endif
#ifdef FEAT_DND
	    || (!writing && regname == '~')
#endif
							)
	return TRUE;
    return FALSE;
}

/*
 * Set y_current and y_append, according to the value of "regname".
 * Cannot handle the '_' register.
 * Must only be called with a valid register name!
 *
 * If regname is 0 and writing, use register 0
 * If regname is 0 and reading, use previous register
 *
 * Return TRUE when the register should be inserted literally (selection or
 * clipboard).
 */
    int
get_yank_register(int regname, int writing)
{
    int	    i;
    int	    ret = FALSE;

    y_append = FALSE;
    if ((regname == 0 || regname == '"') && !writing && y_previous != NULL)
    {
	y_current = y_previous;
	return ret;
    }
    i = regname;
    if (VIM_ISDIGIT(i))
	i -= '0';
    else if (ASCII_ISLOWER(i))
	i = CharOrdLow(i) + 10;
    else if (ASCII_ISUPPER(i))
    {
	i = CharOrdUp(i) + 10;
	y_append = TRUE;
    }
    else if (regname == '-')
	i = DELETION_REGISTER;
#ifdef FEAT_CLIPBOARD
    // When selection is not available, use register 0 instead of '*'
    else if (clip_star.available && regname == '*')
    {
	i = STAR_REGISTER;
	ret = TRUE;
    }
    // When clipboard is not available, use register 0 instead of '+'
    else if (clip_plus.available && regname == '+')
    {
	i = PLUS_REGISTER;
	ret = TRUE;
    }
#endif
#ifdef FEAT_DND
    else if (!writing && regname == '~')
	i = TILDE_REGISTER;
#endif
    else		// not 0-9, a-z, A-Z or '-': use register 0
	i = 0;
    y_current = &(y_regs[i]);
    if (writing)	// remember the register we write into for do_put()
	y_previous = y_current;
    return ret;
}

#if defined(FEAT_CLIPBOARD) || defined(PROTO)
/*
 * When "regname" is a clipboard register, obtain the selection.  If it's not
 * available return zero, otherwise return "regname".
 */
    int
may_get_selection(int regname)
{
    if (regname == '*')
    {
	if (!clip_star.available)
	    regname = 0;
	else
	    clip_get_selection(&clip_star);
    }
    else if (regname == '+')
    {
	if (!clip_plus.available)
	    regname = 0;
	else
	    clip_get_selection(&clip_plus);
    }
    return regname;
}
#endif

/*
 * Obtain the contents of a "normal" register. The register is made empty.
 * The returned pointer has allocated memory, use put_register() later.
 */
    void *
get_register(
    int		name,
    int		copy)	// make a copy, if FALSE make register empty.
{
    yankreg_T	*reg;
    int		i;

#ifdef FEAT_CLIPBOARD
    // When Visual area changed, may have to update selection.  Obtain the
    // selection too.
    if (name == '*' && clip_star.available)
    {
	if (clip_isautosel_star())
	    clip_update_selection(&clip_star);
	may_get_selection(name);
    }
    if (name == '+' && clip_plus.available)
    {
	if (clip_isautosel_plus())
	    clip_update_selection(&clip_plus);
	may_get_selection(name);
    }
#endif

    get_yank_register(name, 0);
    reg = ALLOC_ONE(yankreg_T);
    if (reg != NULL)
    {
	*reg = *y_current;
	if (copy)
	{
	    // If we run out of memory some or all of the lines are empty.
	    if (reg->y_size == 0)
		reg->y_array = NULL;
	    else
		reg->y_array = ALLOC_MULT(char_u *, reg->y_size);
	    if (reg->y_array != NULL)
	    {
		for (i = 0; i < reg->y_size; ++i)
		    reg->y_array[i] = vim_strsave(y_current->y_array[i]);
	    }
	}
	else
	    y_current->y_array = NULL;
    }
    return (void *)reg;
}

/*
 * Put "reg" into register "name".  Free any previous contents and "reg".
 */
    void
put_register(int name, void *reg)
{
    get_yank_register(name, 0);
    free_yank_all();
    *y_current = *(yankreg_T *)reg;
    vim_free(reg);

#ifdef FEAT_CLIPBOARD
    // Send text written to clipboard register to the clipboard.
    may_set_selection();
#endif
}

#if (defined(FEAT_CLIPBOARD) && defined(FEAT_X11) && defined(USE_SYSTEM)) \
	|| defined(PROTO)
    void
free_register(void *reg)
{
    yankreg_T tmp;

    tmp = *y_current;
    *y_current = *(yankreg_T *)reg;
    free_yank_all();
    vim_free(reg);
    *y_current = tmp;
}
#endif

/*
 * return TRUE if the current yank register has type MLINE
 */
    int
yank_register_mline(int regname)
{
    if (regname != 0 && !valid_yank_reg(regname, FALSE))
	return FALSE;
    if (regname == '_')		// black hole is always empty
	return FALSE;
    get_yank_register(regname, FALSE);
    return (y_current->y_type == MLINE);
}

/*
 * Start or stop recording into a yank register.
 *
 * Return FAIL for failure, OK otherwise.
 */
    int
do_record(int c)
{
    char_u	    *p;
    static int	    regname;
    yankreg_T	    *old_y_previous, *old_y_current;
    int		    retval;

    if (reg_recording == 0)	    // start recording
    {
	// registers 0-9, a-z and " are allowed
	if (c < 0 || (!ASCII_ISALNUM(c) && c != '"'))
	    retval = FAIL;
	else
	{
	    reg_recording = c;
	    showmode();
	    regname = c;
	    retval = OK;
	}
    }
    else			    // stop recording
    {
	// Get the recorded key hits.  K_SPECIAL and CSI will be escaped, this
	// needs to be removed again to put it in a register.  exec_reg then
	// adds the escaping back later.
	reg_recording = 0;
	msg("");
	p = get_recorded();
	if (p == NULL)
	    retval = FAIL;
	else
	{
	    // Remove escaping for CSI and K_SPECIAL in multi-byte chars.
	    vim_unescape_csi(p);

	    // We don't want to change the default register here, so save and
	    // restore the current register name.
	    old_y_previous = y_previous;
	    old_y_current = y_current;

	    retval = stuff_yank(regname, p);

	    y_previous = old_y_previous;
	    y_current = old_y_current;
	}
    }
    return retval;
}

/*
 * Stuff string "p" into yank register "regname" as a single line (append if
 * uppercase).	"p" must have been alloced.
 *
 * return FAIL for failure, OK otherwise
 */
    static int
stuff_yank(int regname, char_u *p)
{
    char_u	*lp;
    char_u	**pp;

    // check for read-only register
    if (regname != 0 && !valid_yank_reg(regname, TRUE))
    {
	vim_free(p);
	return FAIL;
    }
    if (regname == '_')		    // black hole: don't do anything
    {
	vim_free(p);
	return OK;
    }
    get_yank_register(regname, TRUE);
    if (y_append && y_current->y_array != NULL)
    {
	pp = &(y_current->y_array[y_current->y_size - 1]);
	lp = alloc(STRLEN(*pp) + STRLEN(p) + 1);
	if (lp == NULL)
	{
	    vim_free(p);
	    return FAIL;
	}
	STRCPY(lp, *pp);
	STRCAT(lp, p);
	vim_free(p);
	vim_free(*pp);
	*pp = lp;
    }
    else
    {
	free_yank_all();
	if ((y_current->y_array = ALLOC_ONE(char_u *)) == NULL)
	{
	    vim_free(p);
	    return FAIL;
	}
	y_current->y_array[0] = p;
	y_current->y_size = 1;
	y_current->y_type = MCHAR;  // used to be MLINE, why?
	y_current->y_op_change = 0;
#ifdef FEAT_VIMINFO
	y_current->y_time_set = vim_time();
#endif
    }
    return OK;
}

static int execreg_lastc = NUL;

    int
get_execreg_lastc(void)
{
    return execreg_lastc;
}

    void
set_execreg_lastc(int lastc)
{
    execreg_lastc = lastc;
}

/*
 * Execute a yank register: copy it into the stuff buffer.
 *
 * Return FAIL for failure, OK otherwise.
 */
    int
do_execreg(
    int	    regname,
    int	    colon,		// insert ':' before each line
    int	    addcr,		// always add '\n' to end of line
    int	    silent)		// set "silent" flag in typeahead buffer
{
    long	i;
    char_u	*p;
    int		retval = OK;
    int		remap;

    // repeat previous one
    if (regname == '@')
    {
	if (execreg_lastc == NUL)
	{
	    emsg(_("E748: No previously used register"));
	    return FAIL;
	}
	regname = execreg_lastc;
    }
    // check for valid regname
    if (regname == '%' || regname == '#' || !valid_yank_reg(regname, FALSE))
    {
	emsg_invreg(regname);
	return FAIL;
    }
    execreg_lastc = regname;

#ifdef FEAT_CLIPBOARD
    regname = may_get_selection(regname);
#endif

    // black hole: don't stuff anything
    if (regname == '_')
	return OK;

    // use last command line
    if (regname == ':')
    {
	if (last_cmdline == NULL)
	{
	    emsg(_(e_nolastcmd));
	    return FAIL;
	}
	// don't keep the cmdline containing @:
	VIM_CLEAR(new_last_cmdline);
	// Escape all control characters with a CTRL-V
	p = vim_strsave_escaped_ext(last_cmdline,
		    (char_u *)"\001\002\003\004\005\006\007"
			  "\010\011\012\013\014\015\016\017"
			  "\020\021\022\023\024\025\026\027"
			  "\030\031\032\033\034\035\036\037",
		    Ctrl_V, FALSE);
	if (p != NULL)
	{
	    // When in Visual mode "'<,'>" will be prepended to the command.
	    // Remove it when it's already there.
	    if (VIsual_active && STRNCMP(p, "'<,'>", 5) == 0)
		retval = put_in_typebuf(p + 5, TRUE, TRUE, silent);
	    else
		retval = put_in_typebuf(p, TRUE, TRUE, silent);
	}
	vim_free(p);
    }
#ifdef FEAT_EVAL
    else if (regname == '=')
    {
	p = get_expr_line();
	if (p == NULL)
	    return FAIL;
	retval = put_in_typebuf(p, TRUE, colon, silent);
	vim_free(p);
    }
#endif
    else if (regname == '.')		// use last inserted text
    {
	p = get_last_insert_save();
	if (p == NULL)
	{
	    emsg(_(e_noinstext));
	    return FAIL;
	}
	retval = put_in_typebuf(p, FALSE, colon, silent);
	vim_free(p);
    }
    else
    {
	get_yank_register(regname, FALSE);
	if (y_current->y_array == NULL)
	    return FAIL;

	// Disallow remaping for ":@r".
	remap = colon ? REMAP_NONE : REMAP_YES;

	// Insert lines into typeahead buffer, from last one to first one.
	put_reedit_in_typebuf(silent);
	for (i = y_current->y_size; --i >= 0; )
	{
	    char_u *escaped;

	    // insert NL between lines and after last line if type is MLINE
	    if (y_current->y_type == MLINE || i < y_current->y_size - 1
								     || addcr)
	    {
		if (ins_typebuf((char_u *)"\n", remap, 0, TRUE, silent) == FAIL)
		    return FAIL;
	    }
	    escaped = vim_strsave_escape_csi(y_current->y_array[i]);
	    if (escaped == NULL)
		return FAIL;
	    retval = ins_typebuf(escaped, remap, 0, TRUE, silent);
	    vim_free(escaped);
	    if (retval == FAIL)
		return FAIL;
	    if (colon && ins_typebuf((char_u *)":", remap, 0, TRUE, silent)
								      == FAIL)
		return FAIL;
	}
	reg_executing = regname == 0 ? '"' : regname; // disable "q" command
    }
    return retval;
}

/*
 * If "restart_edit" is not zero, put it in the typeahead buffer, so that it's
 * used only after other typeahead has been processed.
 */
    static void
put_reedit_in_typebuf(int silent)
{
    char_u	buf[3];

    if (restart_edit != NUL)
    {
	if (restart_edit == 'V')
	{
	    buf[0] = 'g';
	    buf[1] = 'R';
	    buf[2] = NUL;
	}
	else
	{
	    buf[0] = restart_edit == 'I' ? 'i' : restart_edit;
	    buf[1] = NUL;
	}
	if (ins_typebuf(buf, REMAP_NONE, 0, TRUE, silent) == OK)
	    restart_edit = NUL;
    }
}

/*
 * Insert register contents "s" into the typeahead buffer, so that it will be
 * executed again.
 * When "esc" is TRUE it is to be taken literally: Escape CSI characters and
 * no remapping.
 */
    static int
put_in_typebuf(
    char_u	*s,
    int		esc,
    int		colon,	    // add ':' before the line
    int		silent)
{
    int		retval = OK;

    put_reedit_in_typebuf(silent);
    if (colon)
	retval = ins_typebuf((char_u *)"\n", REMAP_NONE, 0, TRUE, silent);
    if (retval == OK)
    {
	char_u	*p;

	if (esc)
	    p = vim_strsave_escape_csi(s);
	else
	    p = s;
	if (p == NULL)
	    retval = FAIL;
	else
	    retval = ins_typebuf(p, esc ? REMAP_NONE : REMAP_YES,
							     0, TRUE, silent);
	if (esc)
	    vim_free(p);
    }
    if (colon && retval == OK)
	retval = ins_typebuf((char_u *)":", REMAP_NONE, 0, TRUE, silent);
    return retval;
}

/*
 * Insert a yank register: copy it into the Read buffer.
 * Used by CTRL-R command and middle mouse button in insert mode.
 *
 * return FAIL for failure, OK otherwise
 */
    int
insert_reg(
    int		regname,
    int		literally_arg)	// insert literally, not as if typed
{
    long	i;
    int		retval = OK;
    char_u	*arg;
    int		allocated;
    int		literally = literally_arg;

    // It is possible to get into an endless loop by having CTRL-R a in
    // register a and then, in insert mode, doing CTRL-R a.
    // If you hit CTRL-C, the loop will be broken here.
    ui_breakcheck();
    if (got_int)
	return FAIL;

    // check for valid regname
    if (regname != NUL && !valid_yank_reg(regname, FALSE))
	return FAIL;

#ifdef FEAT_CLIPBOARD
    regname = may_get_selection(regname);
#endif

    if (regname == '.')			// insert last inserted text
	retval = stuff_inserted(NUL, 1L, TRUE);
    else if (get_spec_reg(regname, &arg, &allocated, TRUE))
    {
	if (arg == NULL)
	    return FAIL;
	stuffescaped(arg, literally);
	if (allocated)
	    vim_free(arg);
    }
    else				// name or number register
    {
	if (get_yank_register(regname, FALSE))
	    literally = TRUE;
	if (y_current->y_array == NULL)
	    retval = FAIL;
	else
	{
	    for (i = 0; i < y_current->y_size; ++i)
	    {
		stuffescaped(y_current->y_array[i], literally);
		// Insert a newline between lines and after last line if
		// y_type is MLINE.
		if (y_current->y_type == MLINE || i < y_current->y_size - 1)
		    stuffcharReadbuff('\n');
	    }
	}
    }

    return retval;
}

/*
 * If "regname" is a special register, return TRUE and store a pointer to its
 * value in "argp".
 */
    int
get_spec_reg(
    int		regname,
    char_u	**argp,
    int		*allocated,	// return: TRUE when value was allocated
    int		errmsg)		// give error message when failing
{
    int		cnt;

    *argp = NULL;
    *allocated = FALSE;
    switch (regname)
    {
	case '%':		// file name
	    if (errmsg)
		check_fname();	// will give emsg if not set
	    *argp = curbuf->b_fname;
	    return TRUE;

	case '#':		// alternate file name
	    *argp = getaltfname(errmsg);	// may give emsg if not set
	    return TRUE;

#ifdef FEAT_EVAL
	case '=':		// result of expression
	    *argp = get_expr_line();
	    *allocated = TRUE;
	    return TRUE;
#endif

	case ':':		// last command line
	    if (last_cmdline == NULL && errmsg)
		emsg(_(e_nolastcmd));
	    *argp = last_cmdline;
	    return TRUE;

	case '/':		// last search-pattern
	    if (last_search_pat() == NULL && errmsg)
		emsg(_(e_noprevre));
	    *argp = last_search_pat();
	    return TRUE;

	case '.':		// last inserted text
	    *argp = get_last_insert_save();
	    *allocated = TRUE;
	    if (*argp == NULL && errmsg)
		emsg(_(e_noinstext));
	    return TRUE;

#ifdef FEAT_SEARCHPATH
	case Ctrl_F:		// Filename under cursor
	case Ctrl_P:		// Path under cursor, expand via "path"
	    if (!errmsg)
		return FALSE;
	    *argp = file_name_at_cursor(FNAME_MESS | FNAME_HYP
			    | (regname == Ctrl_P ? FNAME_EXP : 0), 1L, NULL);
	    *allocated = TRUE;
	    return TRUE;
#endif

	case Ctrl_W:		// word under cursor
	case Ctrl_A:		// WORD (mnemonic All) under cursor
	    if (!errmsg)
		return FALSE;
	    cnt = find_ident_under_cursor(argp, regname == Ctrl_W
				   ?  (FIND_IDENT|FIND_STRING) : FIND_STRING);
	    *argp = cnt ? vim_strnsave(*argp, cnt) : NULL;
	    *allocated = TRUE;
	    return TRUE;

	case Ctrl_L:		// Line under cursor
	    if (!errmsg)
		return FALSE;

	    *argp = ml_get_buf(curwin->w_buffer,
			curwin->w_cursor.lnum, FALSE);
	    return TRUE;

	case '_':		// black hole: always empty
	    *argp = (char_u *)"";
	    return TRUE;
    }

    return FALSE;
}

/*
 * Paste a yank register into the command line.
 * Only for non-special registers.
 * Used by CTRL-R command in command-line mode
 * insert_reg() can't be used here, because special characters from the
 * register contents will be interpreted as commands.
 *
 * return FAIL for failure, OK otherwise
 */
    int
cmdline_paste_reg(
    int regname,
    int literally_arg,	// Insert text literally instead of "as typed"
    int remcr)		// don't add CR characters
{
    long	i;
    int		literally = literally_arg;

    if (get_yank_register(regname, FALSE))
	literally = TRUE;
    if (y_current->y_array == NULL)
	return FAIL;

    for (i = 0; i < y_current->y_size; ++i)
    {
	cmdline_paste_str(y_current->y_array[i], literally);

	// Insert ^M between lines and after last line if type is MLINE.
	// Don't do this when "remcr" is TRUE.
	if ((y_current->y_type == MLINE || i < y_current->y_size - 1) && !remcr)
	    cmdline_paste_str((char_u *)"\r", literally);

	// Check for CTRL-C, in case someone tries to paste a few thousand
	// lines and gets bored.
	ui_breakcheck();
	if (got_int)
	    return FAIL;
    }
    return OK;
}

#if defined(FEAT_CLIPBOARD) || defined(PROTO)
/*
 * Adjust the register name pointed to with "rp" for the clipboard being
 * used always and the clipboard being available.
 */
    void
adjust_clip_reg(int *rp)
{
    // If no reg. specified, and "unnamed" or "unnamedplus" is in 'clipboard',
    // use '*' or '+' reg, respectively. "unnamedplus" prevails.
    if (*rp == 0 && (clip_unnamed != 0 || clip_unnamed_saved != 0))
    {
	if (clip_unnamed != 0)
	    *rp = ((clip_unnamed & CLIP_UNNAMED_PLUS) && clip_plus.available)
								  ? '+' : '*';
	else
	    *rp = ((clip_unnamed_saved & CLIP_UNNAMED_PLUS)
					   && clip_plus.available) ? '+' : '*';
    }
    if (!clip_star.available && *rp == '*')
	*rp = 0;
    if (!clip_plus.available && *rp == '+')
	*rp = 0;
}
#endif

/*
 * Shift the delete registers: "9 is cleared, "8 becomes "9, etc.
 */
    void
shift_delete_registers()
{
    int		n;

    y_current = &y_regs[9];
    free_yank_all();			// free register nine
    for (n = 9; n > 1; --n)
	y_regs[n] = y_regs[n - 1];
    y_previous = y_current = &y_regs[1];
    y_regs[1].y_array = NULL;		// set register one to empty
}

#if defined(FEAT_EVAL)
    void
yank_do_autocmd(oparg_T *oap, yankreg_T *reg)
{
    static int	recursive = FALSE;
    dict_T	*v_event;
    list_T	*list;
    int		n;
    char_u	buf[NUMBUFLEN + 2];
    long	reglen = 0;

    if (recursive)
	return;

    v_event = get_vim_var_dict(VV_EVENT);

    list = list_alloc();
    if (list == NULL)
	return;
    for (n = 0; n < reg->y_size; n++)
	list_append_string(list, reg->y_array[n], -1);
    list->lv_lock = VAR_FIXED;
    dict_add_list(v_event, "regcontents", list);

    buf[0] = (char_u)oap->regname;
    buf[1] = NUL;
    dict_add_string(v_event, "regname", buf);

    buf[0] = get_op_char(oap->op_type);
    buf[1] = get_extra_op_char(oap->op_type);
    buf[2] = NUL;
    dict_add_string(v_event, "operator", buf);

    buf[0] = NUL;
    buf[1] = NUL;
    switch (get_reg_type(oap->regname, &reglen))
    {
	case MLINE: buf[0] = 'V'; break;
	case MCHAR: buf[0] = 'v'; break;
	case MBLOCK:
		vim_snprintf((char *)buf, sizeof(buf), "%c%ld", Ctrl_V,
			     reglen + 1);
		break;
    }
    dict_add_string(v_event, "regtype", buf);

    // Lock the dictionary and its keys
    dict_set_items_ro(v_event);

    recursive = TRUE;
    textlock++;
    apply_autocmds(EVENT_TEXTYANKPOST, NULL, NULL, FALSE, curbuf);
    textlock--;
    recursive = FALSE;

    // Empty the dictionary, v:event is still valid
    dict_free_contents(v_event);
    hash_init(&v_event->dv_hashtab);
}
#endif

/*
 * set all the yank registers to empty (called from main())
 */
    void
init_yank(void)
{
    int		i;

    for (i = 0; i < NUM_REGISTERS; ++i)
	y_regs[i].y_array = NULL;
}

#if defined(EXITFREE) || defined(PROTO)
    void
clear_registers(void)
{
    int		i;

    for (i = 0; i < NUM_REGISTERS; ++i)
    {
	y_current = &y_regs[i];
	if (y_current->y_array != NULL)
	    free_yank_all();
    }
}
#endif

/*
 * Free "n" lines from the current yank register.
 * Called for normal freeing and in case of error.
 */
    static void
free_yank(long n)
{
    if (y_current->y_array != NULL)
    {
	long	    i;

	for (i = n; --i >= 0; )
	{
#ifdef AMIGA	    // only for very slow machines
	    if ((i & 1023) == 1023)  // this may take a while
	    {
		// This message should never cause a hit-return message.
		// Overwrite this message with any next message.
		++no_wait_return;
		smsg(_("freeing %ld lines"), i + 1);
		--no_wait_return;
		msg_didout = FALSE;
		msg_col = 0;
	    }
#endif
	    vim_free(y_current->y_array[i]);
	}
	VIM_CLEAR(y_current->y_array);
#ifdef AMIGA
	if (n >= 1000)
	    msg("");
#endif
    }
}

    static void
free_yank_all(void)
{
    free_yank(y_current->y_size);
}

/*
 * Yank the text between "oap->start" and "oap->end" into a yank register.
 * If we are to append (uppercase register), we first yank into a new yank
 * register and then concatenate the old and the new one (so we keep the old
 * one in case of out-of-memory).
 *
 * Return FAIL for failure, OK otherwise.
 */
    int
op_yank(oparg_T *oap, int deleting, int mess)
{
    long		y_idx;		// index in y_array[]
    yankreg_T		*curr;		// copy of y_current
    yankreg_T		newreg;		// new yank register when appending
    char_u		**new_ptr;
    linenr_T		lnum;		// current line number
    long		j;
    int			yanktype = oap->motion_type;
    long		yanklines = oap->line_count;
    linenr_T		yankendlnum = oap->end.lnum;
    char_u		*p;
    char_u		*pnew;
    struct block_def	bd;
#if defined(FEAT_CLIPBOARD) && defined(FEAT_X11)
    int			did_star = FALSE;
#endif

				    // check for read-only register
    if (oap->regname != 0 && !valid_yank_reg(oap->regname, TRUE))
    {
	beep_flush();
	return FAIL;
    }
    if (oap->regname == '_')	    // black hole: nothing to do
	return OK;

#ifdef FEAT_CLIPBOARD
    if (!clip_star.available && oap->regname == '*')
	oap->regname = 0;
    else if (!clip_plus.available && oap->regname == '+')
	oap->regname = 0;
#endif

    if (!deleting)		    // op_delete() already set y_current
	get_yank_register(oap->regname, TRUE);

    curr = y_current;
				    // append to existing contents
    if (y_append && y_current->y_array != NULL)
	y_current = &newreg;
    else
	free_yank_all();	    // free previously yanked lines

    // If the cursor was in column 1 before and after the movement, and the
    // operator is not inclusive, the yank is always linewise.
    if (       oap->motion_type == MCHAR
	    && oap->start.col == 0
	    && !oap->inclusive
	    && (!oap->is_VIsual || *p_sel == 'o')
	    && !oap->block_mode
	    && oap->end.col == 0
	    && yanklines > 1)
    {
	yanktype = MLINE;
	--yankendlnum;
	--yanklines;
    }

    y_current->y_size = yanklines;
    y_current->y_type = yanktype;   // set the yank register type
    y_current->y_op_change = (oap->op_type == OP_CHANGE) && inindent(0);
    y_current->y_width = 0;
    y_current->y_array = lalloc_clear(sizeof(char_u *) * yanklines, TRUE);
    if (y_current->y_array == NULL)
    {
	y_current = curr;
	return FAIL;
    }
#ifdef FEAT_VIMINFO
    y_current->y_time_set = vim_time();
#endif

    y_idx = 0;
    lnum = oap->start.lnum;

    if (oap->block_mode)
    {
	// Visual block mode
	y_current->y_type = MBLOCK;	    // set the yank register type
	y_current->y_width = oap->end_vcol - oap->start_vcol;

	if (curwin->w_curswant == MAXCOL && y_current->y_width > 0)
	    y_current->y_width--;
    }

    for ( ; lnum <= yankendlnum; lnum++, y_idx++)
    {
	switch (y_current->y_type)
	{
	    case MBLOCK:
		block_prep(oap, &bd, lnum, FALSE);
		if (yank_copy_line(&bd, y_idx) == FAIL)
		    goto fail;
		break;

	    case MLINE:
		if ((y_current->y_array[y_idx] =
			    vim_strsave(ml_get(lnum))) == NULL)
		    goto fail;
		break;

	    case MCHAR:
		{
		    colnr_T startcol = 0, endcol = MAXCOL;
		    int is_oneChar = FALSE;
		    colnr_T cs, ce;

		    p = ml_get(lnum);
		    bd.startspaces = 0;
		    bd.endspaces = 0;

		    if (lnum == oap->start.lnum)
		    {
			startcol = oap->start.col;
			if (virtual_op)
			{
			    getvcol(curwin, &oap->start, &cs, NULL, &ce);
			    if (ce != cs && oap->start.coladd > 0)
			    {
				// Part of a tab selected -- but don't
				// double-count it.
				bd.startspaces = (ce - cs + 1)
							  - oap->start.coladd;
				startcol++;
			    }
			}
		    }

		    if (lnum == oap->end.lnum)
		    {
			endcol = oap->end.col;
			if (virtual_op)
			{
			    getvcol(curwin, &oap->end, &cs, NULL, &ce);
			    if (p[endcol] == NUL || (cs + oap->end.coladd < ce
					// Don't add space for double-wide
					// char; endcol will be on last byte
					// of multi-byte char.
					&& (*mb_head_off)(p, p + endcol) == 0))
			    {
				if (oap->start.lnum == oap->end.lnum
					    && oap->start.col == oap->end.col)
				{
				    // Special case: inside a single char
				    is_oneChar = TRUE;
				    bd.startspaces = oap->end.coladd
					 - oap->start.coladd + oap->inclusive;
				    endcol = startcol;
				}
				else
				{
				    bd.endspaces = oap->end.coladd
							     + oap->inclusive;
				    endcol -= oap->inclusive;
				}
			    }
			}
		    }
		    if (endcol == MAXCOL)
			endcol = (colnr_T)STRLEN(p);
		    if (startcol > endcol || is_oneChar)
			bd.textlen = 0;
		    else
			bd.textlen = endcol - startcol + oap->inclusive;
		    bd.textstart = p + startcol;
		    if (yank_copy_line(&bd, y_idx) == FAIL)
			goto fail;
		    break;
		}
		// NOTREACHED
	}
    }

    if (curr != y_current)	// append the new block to the old block
    {
	new_ptr = ALLOC_MULT(char_u *, curr->y_size + y_current->y_size);
	if (new_ptr == NULL)
	    goto fail;
	for (j = 0; j < curr->y_size; ++j)
	    new_ptr[j] = curr->y_array[j];
	vim_free(curr->y_array);
	curr->y_array = new_ptr;
#ifdef FEAT_VIMINFO
	curr->y_time_set = vim_time();
#endif

	if (yanktype == MLINE)	// MLINE overrides MCHAR and MBLOCK
	    curr->y_type = MLINE;

	// Concatenate the last line of the old block with the first line of
	// the new block, unless being Vi compatible.
	if (curr->y_type == MCHAR && vim_strchr(p_cpo, CPO_REGAPPEND) == NULL)
	{
	    pnew = alloc(STRLEN(curr->y_array[curr->y_size - 1])
					  + STRLEN(y_current->y_array[0]) + 1);
	    if (pnew == NULL)
	    {
		y_idx = y_current->y_size - 1;
		goto fail;
	    }
	    STRCPY(pnew, curr->y_array[--j]);
	    STRCAT(pnew, y_current->y_array[0]);
	    vim_free(curr->y_array[j]);
	    vim_free(y_current->y_array[0]);
	    curr->y_array[j++] = pnew;
	    y_idx = 1;
	}
	else
	    y_idx = 0;
	while (y_idx < y_current->y_size)
	    curr->y_array[j++] = y_current->y_array[y_idx++];
	curr->y_size = j;
	vim_free(y_current->y_array);
	y_current = curr;
    }
    if (curwin->w_p_rnu)
	redraw_later(SOME_VALID);	// cursor moved to start
    if (mess)			// Display message about yank?
    {
	if (yanktype == MCHAR
		&& !oap->block_mode
		&& yanklines == 1)
	    yanklines = 0;
	// Some versions of Vi use ">=" here, some don't...
	if (yanklines > p_report)
	{
	    char namebuf[100];

	    if (oap->regname == NUL)
		*namebuf = NUL;
	    else
		vim_snprintf(namebuf, sizeof(namebuf),
						_(" into \"%c"), oap->regname);

	    // redisplay now, so message is not deleted
	    update_topline_redraw();
	    if (oap->block_mode)
	    {
		smsg(NGETTEXT("block of %ld line yanked%s",
				     "block of %ld lines yanked%s", yanklines),
			yanklines, namebuf);
	    }
	    else
	    {
		smsg(NGETTEXT("%ld line yanked%s",
					      "%ld lines yanked%s", yanklines),
			yanklines, namebuf);
	    }
	}
    }

    // Set "'[" and "']" marks.
    curbuf->b_op_start = oap->start;
    curbuf->b_op_end = oap->end;
    if (yanktype == MLINE && !oap->block_mode)
    {
	curbuf->b_op_start.col = 0;
	curbuf->b_op_end.col = MAXCOL;
    }

#ifdef FEAT_CLIPBOARD
    // If we were yanking to the '*' register, send result to clipboard.
    // If no register was specified, and "unnamed" in 'clipboard', make a copy
    // to the '*' register.
    if (clip_star.available
	    && (curr == &(y_regs[STAR_REGISTER])
		|| (!deleting && oap->regname == 0
		   && ((clip_unnamed | clip_unnamed_saved) & CLIP_UNNAMED))))
    {
	if (curr != &(y_regs[STAR_REGISTER]))
	    // Copy the text from register 0 to the clipboard register.
	    copy_yank_reg(&(y_regs[STAR_REGISTER]));

	clip_own_selection(&clip_star);
	clip_gen_set_selection(&clip_star);
# ifdef FEAT_X11
	did_star = TRUE;
# endif
    }

# ifdef FEAT_X11
    // If we were yanking to the '+' register, send result to selection.
    // Also copy to the '*' register, in case auto-select is off.
    if (clip_plus.available
	    && (curr == &(y_regs[PLUS_REGISTER])
		|| (!deleting && oap->regname == 0
		  && ((clip_unnamed | clip_unnamed_saved) &
		      CLIP_UNNAMED_PLUS))))
    {
	if (curr != &(y_regs[PLUS_REGISTER]))
	    // Copy the text from register 0 to the clipboard register.
	    copy_yank_reg(&(y_regs[PLUS_REGISTER]));

	clip_own_selection(&clip_plus);
	clip_gen_set_selection(&clip_plus);
	if (!clip_isautosel_star() && !clip_isautosel_plus()
		&& !did_star && curr == &(y_regs[PLUS_REGISTER]))
	{
	    copy_yank_reg(&(y_regs[STAR_REGISTER]));
	    clip_own_selection(&clip_star);
	    clip_gen_set_selection(&clip_star);
	}
    }
# endif
#endif

#if defined(FEAT_EVAL)
    if (!deleting && has_textyankpost())
	yank_do_autocmd(oap, y_current);
#endif

    return OK;

fail:		// free the allocated lines
    free_yank(y_idx + 1);
    y_current = curr;
    return FAIL;
}

    static int
yank_copy_line(struct block_def *bd, long y_idx)
{
    char_u	*pnew;

    if ((pnew = alloc(bd->startspaces + bd->endspaces + bd->textlen + 1))
								      == NULL)
	return FAIL;
    y_current->y_array[y_idx] = pnew;
    vim_memset(pnew, ' ', (size_t)bd->startspaces);
    pnew += bd->startspaces;
    mch_memmove(pnew, bd->textstart, (size_t)bd->textlen);
    pnew += bd->textlen;
    vim_memset(pnew, ' ', (size_t)bd->endspaces);
    pnew += bd->endspaces;
    *pnew = NUL;
    return OK;
}

#ifdef FEAT_CLIPBOARD
/*
 * Make a copy of the y_current register to register "reg".
 */
    static void
copy_yank_reg(yankreg_T *reg)
{
    yankreg_T	*curr = y_current;
    long	j;

    y_current = reg;
    free_yank_all();
    *y_current = *curr;
    y_current->y_array = lalloc_clear(
				    sizeof(char_u *) * y_current->y_size, TRUE);
    if (y_current->y_array == NULL)
	y_current->y_size = 0;
    else
	for (j = 0; j < y_current->y_size; ++j)
	    if ((y_current->y_array[j] = vim_strsave(curr->y_array[j])) == NULL)
	    {
		free_yank(j);
		y_current->y_size = 0;
		break;
	    }
    y_current = curr;
}
#endif

/*
 * Put contents of register "regname" into the text.
 * Caller must check "regname" to be valid!
 * "flags": PUT_FIXINDENT	make indent look nice
 *	    PUT_CURSEND		leave cursor after end of new text
 *	    PUT_LINE		force linewise put (":put")
 */
    void
do_put(
    int		regname,
    int		dir,		// BACKWARD for 'P', FORWARD for 'p'
    long	count,
    int		flags)
{
    char_u	*ptr;
    char_u	*newp, *oldp;
    int		yanklen;
    int		totlen = 0;		// init for gcc
    linenr_T	lnum;
    colnr_T	col;
    long	i;			// index in y_array[]
    int		y_type;
    long	y_size;
    int		oldlen;
    long	y_width = 0;
    colnr_T	vcol;
    int		delcount;
    int		incr = 0;
    long	j;
    struct block_def bd;
    char_u	**y_array = NULL;
    long	nr_lines = 0;
    pos_T	new_cursor;
    int		indent;
    int		orig_indent = 0;	// init for gcc
    int		indent_diff = 0;	// init for gcc
    int		first_indent = TRUE;
    int		lendiff = 0;
    pos_T	old_pos;
    char_u	*insert_string = NULL;
    int		allocated = FALSE;
    long	cnt;

#ifdef FEAT_CLIPBOARD
    // Adjust register name for "unnamed" in 'clipboard'.
    adjust_clip_reg(&regname);
    (void)may_get_selection(regname);
#endif

    if (flags & PUT_FIXINDENT)
	orig_indent = get_indent();

    curbuf->b_op_start = curwin->w_cursor;	// default for '[ mark
    curbuf->b_op_end = curwin->w_cursor;	// default for '] mark

    // Using inserted text works differently, because the register includes
    // special characters (newlines, etc.).
    if (regname == '.')
    {
	if (VIsual_active)
	    stuffcharReadbuff(VIsual_mode);
	(void)stuff_inserted((dir == FORWARD ? (count == -1 ? 'o' : 'a') :
				    (count == -1 ? 'O' : 'i')), count, FALSE);
	// Putting the text is done later, so can't really move the cursor to
	// the next character.  Use "l" to simulate it.
	if ((flags & PUT_CURSEND) && gchar_cursor() != NUL)
	    stuffcharReadbuff('l');
	return;
    }

    // For special registers '%' (file name), '#' (alternate file name) and
    // ':' (last command line), etc. we have to create a fake yank register.
    if (get_spec_reg(regname, &insert_string, &allocated, TRUE))
    {
	if (insert_string == NULL)
	    return;
    }

    // Autocommands may be executed when saving lines for undo.  This might
    // make "y_array" invalid, so we start undo now to avoid that.
    if (u_save(curwin->w_cursor.lnum, curwin->w_cursor.lnum + 1) == FAIL)
	goto end;

    if (insert_string != NULL)
    {
	y_type = MCHAR;
#ifdef FEAT_EVAL
	if (regname == '=')
	{
	    // For the = register we need to split the string at NL
	    // characters.
	    // Loop twice: count the number of lines and save them.
	    for (;;)
	    {
		y_size = 0;
		ptr = insert_string;
		while (ptr != NULL)
		{
		    if (y_array != NULL)
			y_array[y_size] = ptr;
		    ++y_size;
		    ptr = vim_strchr(ptr, '\n');
		    if (ptr != NULL)
		    {
			if (y_array != NULL)
			    *ptr = NUL;
			++ptr;
			// A trailing '\n' makes the register linewise.
			if (*ptr == NUL)
			{
			    y_type = MLINE;
			    break;
			}
		    }
		}
		if (y_array != NULL)
		    break;
		y_array = ALLOC_MULT(char_u *, y_size);
		if (y_array == NULL)
		    goto end;
	    }
	}
	else
#endif
	{
	    y_size = 1;		// use fake one-line yank register
	    y_array = &insert_string;
	}
    }
    else
    {
	get_yank_register(regname, FALSE);

	y_type = y_current->y_type;
	y_width = y_current->y_width;
	y_size = y_current->y_size;
	if (Unix2003_compat && y_size > 1 && y_type == MCHAR && curwin->w_cursor.col == 0 && y_current->y_op_change) {
		y_type = MLINE;
	}
	y_array = y_current->y_array;
    }

    if (y_type == MLINE)
    {
	if (flags & PUT_LINE_SPLIT)
	{
	    char_u *p;

	    // "p" or "P" in Visual mode: split the lines to put the text in
	    // between.
	    if (u_save_cursor() == FAIL)
		goto end;
	    p = ml_get_cursor();
	    if (dir == FORWARD && *p != NUL)
		MB_PTR_ADV(p);
	    ptr = vim_strsave(p);
	    if (ptr == NULL)
		goto end;
	    ml_append(curwin->w_cursor.lnum, ptr, (colnr_T)0, FALSE);
	    vim_free(ptr);

	    oldp = ml_get_curline();
	    p = oldp + curwin->w_cursor.col;
	    if (dir == FORWARD && *p != NUL)
		MB_PTR_ADV(p);
	    ptr = vim_strnsave(oldp, p - oldp);
	    if (ptr == NULL)
		goto end;
	    ml_replace(curwin->w_cursor.lnum, ptr, FALSE);
	    ++nr_lines;
	    dir = FORWARD;
	}
	if (flags & PUT_LINE_FORWARD)
	{
	    // Must be "p" for a Visual block, put lines below the block.
	    curwin->w_cursor = curbuf->b_visual.vi_end;
	    dir = FORWARD;
	}
	curbuf->b_op_start = curwin->w_cursor;	// default for '[ mark
	curbuf->b_op_end = curwin->w_cursor;	// default for '] mark
    }

    if (flags & PUT_LINE)	// :put command or "p" in Visual line mode.
	y_type = MLINE;

    if (y_size == 0 || y_array == NULL)
    {
	semsg(_("E353: Nothing in register %s"),
		  regname == 0 ? (char_u *)"\"" : transchar(regname));
	goto end;
    }

    if (y_type == MBLOCK)
    {
	lnum = curwin->w_cursor.lnum + y_size + 1;
	if (lnum > curbuf->b_ml.ml_line_count)
	    lnum = curbuf->b_ml.ml_line_count + 1;
	if (u_save(curwin->w_cursor.lnum - 1, lnum) == FAIL)
	    goto end;
    }
    else if (y_type == MLINE)
    {
	lnum = curwin->w_cursor.lnum;
#ifdef FEAT_FOLDING
	// Correct line number for closed fold.  Don't move the cursor yet,
	// u_save() uses it.
	if (dir == BACKWARD)
	    (void)hasFolding(lnum, &lnum, NULL);
	else
	    (void)hasFolding(lnum, NULL, &lnum);
#endif
	if (dir == FORWARD)
	    ++lnum;
	// In an empty buffer the empty line is going to be replaced, include
	// it in the saved lines.
	if ((BUFEMPTY() ? u_save(0, 2) : u_save(lnum - 1, lnum)) == FAIL)
	    goto end;
#ifdef FEAT_FOLDING
	if (dir == FORWARD)
	    curwin->w_cursor.lnum = lnum - 1;
	else
	    curwin->w_cursor.lnum = lnum;
	curbuf->b_op_start = curwin->w_cursor;	// for mark_adjust()
#endif
    }
    else if (u_save_cursor() == FAIL)
	goto end;

    yanklen = (int)STRLEN(y_array[0]);

    if (ve_flags == VE_ALL && y_type == MCHAR)
    {
	if (gchar_cursor() == TAB)
	{
	    // Don't need to insert spaces when "p" on the last position of a
	    // tab or "P" on the first position.
#ifdef FEAT_VARTABS
	    int viscol = getviscol();
	    if (dir == FORWARD
		    ? tabstop_padding(viscol, curbuf->b_p_ts,
						    curbuf->b_p_vts_array) != 1
		    : curwin->w_cursor.coladd > 0)
		coladvance_force(viscol);
#else
	    if (dir == FORWARD
		    ? (int)curwin->w_cursor.coladd < curbuf->b_p_ts - 1
						: curwin->w_cursor.coladd > 0)
		coladvance_force(getviscol());
#endif
	    else
		curwin->w_cursor.coladd = 0;
	}
	else if (curwin->w_cursor.coladd > 0 || gchar_cursor() == NUL)
	    coladvance_force(getviscol() + (dir == FORWARD));
    }

    lnum = curwin->w_cursor.lnum;
    col = curwin->w_cursor.col;

    // Block mode
    if (y_type == MBLOCK)
    {
	int	c = gchar_cursor();
	colnr_T	endcol2 = 0;

	if (dir == FORWARD && c != NUL)
	{
	    if (ve_flags == VE_ALL)
		getvcol(curwin, &curwin->w_cursor, &col, NULL, &endcol2);
	    else
		getvcol(curwin, &curwin->w_cursor, NULL, NULL, &col);

	    if (has_mbyte)
		// move to start of next multi-byte character
		curwin->w_cursor.col += (*mb_ptr2len)(ml_get_cursor());
	    else
	    if (c != TAB || ve_flags != VE_ALL)
		++curwin->w_cursor.col;
	    ++col;
	}
	else
	    getvcol(curwin, &curwin->w_cursor, &col, NULL, &endcol2);

	col += curwin->w_cursor.coladd;
	if (ve_flags == VE_ALL
		&& (curwin->w_cursor.coladd > 0
		    || endcol2 == curwin->w_cursor.col))
	{
	    if (dir == FORWARD && c == NUL)
		++col;
	    if (dir != FORWARD && c != NUL)
		++curwin->w_cursor.col;
	    if (c == TAB)
	    {
		if (dir == BACKWARD && curwin->w_cursor.col)
		    curwin->w_cursor.col--;
		if (dir == FORWARD && col - 1 == endcol2)
		    curwin->w_cursor.col++;
	    }
	}
	curwin->w_cursor.coladd = 0;
	bd.textcol = 0;
	for (i = 0; i < y_size; ++i)
	{
	    int spaces;
	    char shortline;

	    bd.startspaces = 0;
	    bd.endspaces = 0;
	    vcol = 0;
	    delcount = 0;

	    // add a new line
	    if (curwin->w_cursor.lnum > curbuf->b_ml.ml_line_count)
	    {
		if (ml_append(curbuf->b_ml.ml_line_count, (char_u *)"",
						   (colnr_T)1, FALSE) == FAIL)
		    break;
		++nr_lines;
	    }
	    // get the old line and advance to the position to insert at
	    oldp = ml_get_curline();
	    oldlen = (int)STRLEN(oldp);
	    for (ptr = oldp; vcol < col && *ptr; )
	    {
		// Count a tab for what it's worth (if list mode not on)
		incr = lbr_chartabsize_adv(oldp, &ptr, (colnr_T)vcol);
		vcol += incr;
	    }
	    bd.textcol = (colnr_T)(ptr - oldp);

	    shortline = (vcol < col) || (vcol == col && !*ptr) ;

	    if (vcol < col) // line too short, padd with spaces
		bd.startspaces = col - vcol;
	    else if (vcol > col)
	    {
		bd.endspaces = vcol - col;
		bd.startspaces = incr - bd.endspaces;
		--bd.textcol;
		delcount = 1;
		if (has_mbyte)
		    bd.textcol -= (*mb_head_off)(oldp, oldp + bd.textcol);
		if (oldp[bd.textcol] != TAB)
		{
		    // Only a Tab can be split into spaces.  Other
		    // characters will have to be moved to after the
		    // block, causing misalignment.
		    delcount = 0;
		    bd.endspaces = 0;
		}
	    }

	    yanklen = (int)STRLEN(y_array[i]);

	    // calculate number of spaces required to fill right side of block
	    spaces = y_width + 1;
	    for (j = 0; j < yanklen; j++)
		spaces -= lbr_chartabsize(NULL, &y_array[i][j], 0);
	    if (spaces < 0)
		spaces = 0;

	    // insert the new text
	    totlen = count * (yanklen + spaces) + bd.startspaces + bd.endspaces;
	    newp = alloc(totlen + oldlen + 1);
	    if (newp == NULL)
		break;
	    // copy part up to cursor to new line
	    ptr = newp;
	    mch_memmove(ptr, oldp, (size_t)bd.textcol);
	    ptr += bd.textcol;
	    // may insert some spaces before the new text
	    vim_memset(ptr, ' ', (size_t)bd.startspaces);
	    ptr += bd.startspaces;
	    // insert the new text
	    for (j = 0; j < count; ++j)
	    {
		mch_memmove(ptr, y_array[i], (size_t)yanklen);
		ptr += yanklen;

		// insert block's trailing spaces only if there's text behind
		if ((j < count - 1 || !shortline) && spaces)
		{
		    vim_memset(ptr, ' ', (size_t)spaces);
		    ptr += spaces;
		}
	    }
	    // may insert some spaces after the new text
	    vim_memset(ptr, ' ', (size_t)bd.endspaces);
	    ptr += bd.endspaces;
	    // move the text after the cursor to the end of the line.
	    mch_memmove(ptr, oldp + bd.textcol + delcount,
				(size_t)(oldlen - bd.textcol - delcount + 1));
	    ml_replace(curwin->w_cursor.lnum, newp, FALSE);

	    ++curwin->w_cursor.lnum;
	    if (i == 0)
		curwin->w_cursor.col += bd.startspaces;
	}

	changed_lines(lnum, 0, curwin->w_cursor.lnum, nr_lines);

	// Set '[ mark.
	curbuf->b_op_start = curwin->w_cursor;
	curbuf->b_op_start.lnum = lnum;

	// adjust '] mark
	curbuf->b_op_end.lnum = curwin->w_cursor.lnum - 1;
	curbuf->b_op_end.col = bd.textcol + totlen - 1;
	curbuf->b_op_end.coladd = 0;
	if (flags & PUT_CURSEND)
	{
	    colnr_T len;

	    curwin->w_cursor = curbuf->b_op_end;
	    curwin->w_cursor.col++;

	    // in Insert mode we might be after the NUL, correct for that
	    len = (colnr_T)STRLEN(ml_get_curline());
	    if (curwin->w_cursor.col > len)
		curwin->w_cursor.col = len;
	}
	else
	    curwin->w_cursor.lnum = lnum;
    }
    else
    {
	// Character or Line mode
	if (y_type == MCHAR)
	{
	    // if type is MCHAR, FORWARD is the same as BACKWARD on the next
	    // char
	    if (dir == FORWARD && gchar_cursor() != NUL)
	    {
		if (has_mbyte)
		{
		    int bytelen = (*mb_ptr2len)(ml_get_cursor());

		    // put it on the next of the multi-byte character.
		    col += bytelen;
		    if (yanklen)
		    {
			curwin->w_cursor.col += bytelen;
			curbuf->b_op_end.col += bytelen;
		    }
		}
		else
		{
		    ++col;
		    if (yanklen)
		    {
			++curwin->w_cursor.col;
			++curbuf->b_op_end.col;
		    }
		}
	    }
	    curbuf->b_op_start = curwin->w_cursor;
	}
	// Line mode: BACKWARD is the same as FORWARD on the previous line
	else if (dir == BACKWARD)
	    --lnum;
	new_cursor = curwin->w_cursor;

	// simple case: insert into current line
	if (y_type == MCHAR && y_size == 1)
	{
	    linenr_T end_lnum = 0; // init for gcc

	    if (VIsual_active)
	    {
		end_lnum = curbuf->b_visual.vi_end.lnum;
		if (end_lnum < curbuf->b_visual.vi_start.lnum)
		    end_lnum = curbuf->b_visual.vi_start.lnum;
	    }

	    do {
		totlen = count * yanklen;
		if (totlen > 0)
		{
		    oldp = ml_get(lnum);
		    if (VIsual_active && col > (int)STRLEN(oldp))
		    {
			lnum++;
			continue;
		    }
		    newp = alloc(STRLEN(oldp) + totlen + 1);
		    if (newp == NULL)
			goto end;	// alloc() gave an error message
		    mch_memmove(newp, oldp, (size_t)col);
		    ptr = newp + col;
		    for (i = 0; i < count; ++i)
		    {
			mch_memmove(ptr, y_array[0], (size_t)yanklen);
			ptr += yanklen;
		    }
		    STRMOVE(ptr, oldp + col);
		    ml_replace(lnum, newp, FALSE);
		    // Place cursor on last putted char.
		    if (lnum == curwin->w_cursor.lnum)
		    {
			// make sure curwin->w_virtcol is updated
			changed_cline_bef_curs();
			curwin->w_cursor.col += (colnr_T)(totlen - 1);
		    }
		}
		if (VIsual_active)
		    lnum++;
	    } while (VIsual_active && lnum <= end_lnum);

	    if (VIsual_active) // reset lnum to the last visual line
		lnum--;

	    curbuf->b_op_end = curwin->w_cursor;
	    // For "CTRL-O p" in Insert mode, put cursor after last char
	    if (totlen && (restart_edit != 0 || (flags & PUT_CURSEND)))
		++curwin->w_cursor.col;
	    changed_bytes(lnum, col);
	}
	else
	{
	    // Insert at least one line.  When y_type is MCHAR, break the first
	    // line in two.
	    for (cnt = 1; cnt <= count; ++cnt)
	    {
		i = 0;
		if (y_type == MCHAR)
		{
		    // Split the current line in two at the insert position.
		    // First insert y_array[size - 1] in front of second line.
		    // Then append y_array[0] to first line.
		    lnum = new_cursor.lnum;
		    ptr = ml_get(lnum) + col;
		    totlen = (int)STRLEN(y_array[y_size - 1]);
		    newp = alloc(STRLEN(ptr) + totlen + 1);
		    if (newp == NULL)
			goto error;
		    STRCPY(newp, y_array[y_size - 1]);
		    STRCAT(newp, ptr);
		    // insert second line
		    ml_append(lnum, newp, (colnr_T)0, FALSE);
		    vim_free(newp);

		    oldp = ml_get(lnum);
		    newp = alloc(col + yanklen + 1);
		    if (newp == NULL)
			goto error;
					    // copy first part of line
		    mch_memmove(newp, oldp, (size_t)col);
					    // append to first line
		    mch_memmove(newp + col, y_array[0], (size_t)(yanklen + 1));
		    ml_replace(lnum, newp, FALSE);

		    curwin->w_cursor.lnum = lnum;
		    i = 1;
		}

		for (; i < y_size; ++i)
		{
		    if ((y_type != MCHAR || i < y_size - 1)
			    && ml_append(lnum, y_array[i], (colnr_T)0, FALSE)
								      == FAIL)
			    goto error;
		    lnum++;
		    ++nr_lines;
		    if (flags & PUT_FIXINDENT)
		    {
			old_pos = curwin->w_cursor;
			curwin->w_cursor.lnum = lnum;
			ptr = ml_get(lnum);
			if (cnt == count && i == y_size - 1)
			    lendiff = (int)STRLEN(ptr);
#if defined(FEAT_SMARTINDENT) || defined(FEAT_CINDENT)
			if (*ptr == '#' && preprocs_left())
			    indent = 0;     // Leave # lines at start
			else
#endif
			     if (*ptr == NUL)
			    indent = 0;     // Ignore empty lines
			else if (first_indent)
			{
			    indent_diff = orig_indent - get_indent();
			    indent = orig_indent;
			    first_indent = FALSE;
			}
			else if ((indent = get_indent() + indent_diff) < 0)
			    indent = 0;
			(void)set_indent(indent, 0);
			curwin->w_cursor = old_pos;
			// remember how many chars were removed
			if (cnt == count && i == y_size - 1)
			    lendiff -= (int)STRLEN(ml_get(lnum));
		    }
		}
	    }

error:
	    // Adjust marks.
	    if (y_type == MLINE)
	    {
		curbuf->b_op_start.col = 0;
		if (dir == FORWARD)
		    curbuf->b_op_start.lnum++;
	    }
	    // Skip mark_adjust when adding lines after the last one, there
	    // can't be marks there. But still needed in diff mode.
	    if (curbuf->b_op_start.lnum + (y_type == MCHAR) - 1 + nr_lines
						 < curbuf->b_ml.ml_line_count
#ifdef FEAT_DIFF
						 || curwin->w_p_diff
#endif
						 )
		mark_adjust(curbuf->b_op_start.lnum + (y_type == MCHAR),
					     (linenr_T)MAXLNUM, nr_lines, 0L);

	    // note changed text for displaying and folding
	    if (y_type == MCHAR)
		changed_lines(curwin->w_cursor.lnum, col,
					 curwin->w_cursor.lnum + 1, nr_lines);
	    else
		changed_lines(curbuf->b_op_start.lnum, 0,
					   curbuf->b_op_start.lnum, nr_lines);

	    // put '] mark at last inserted character
	    curbuf->b_op_end.lnum = lnum;
	    // correct length for change in indent
	    col = (colnr_T)STRLEN(y_array[y_size - 1]) - lendiff;
	    if (col > 1)
		curbuf->b_op_end.col = col - 1;
	    else
		curbuf->b_op_end.col = 0;

	    if (flags & PUT_CURSLINE)
	    {
		// ":put": put cursor on last inserted line
		curwin->w_cursor.lnum = lnum;
		beginline(BL_WHITE | BL_FIX);
	    }
	    else if (flags & PUT_CURSEND)
	    {
		// put cursor after inserted text
		if (y_type == MLINE)
		{
		    if (lnum >= curbuf->b_ml.ml_line_count)
			curwin->w_cursor.lnum = curbuf->b_ml.ml_line_count;
		    else
			curwin->w_cursor.lnum = lnum + 1;
		    curwin->w_cursor.col = 0;
		}
		else
		{
		    curwin->w_cursor.lnum = lnum;
		    curwin->w_cursor.col = col;
		}
	    }
	    else if (y_type == MLINE)
	    {
		// put cursor on first non-blank in first inserted line
		curwin->w_cursor.col = 0;
		if (dir == FORWARD)
		    ++curwin->w_cursor.lnum;
		beginline(BL_WHITE | BL_FIX);
	    }
	    else if (dir == BACKWARD && Unix2003_compat)
	    {
		    /* put cursor on last inserted character */
		    curwin->w_cursor.lnum = lnum;
		    curwin->w_cursor.col = col > 0 ? (col - 1) : 0;
	    }
	    else	// put cursor on first inserted character
		curwin->w_cursor = new_cursor;
	}
    }

    msgmore(nr_lines);
    curwin->w_set_curswant = TRUE;

end:
    if (allocated)
	vim_free(insert_string);
    if (regname == '=')
	vim_free(y_array);

    VIsual_active = FALSE;

    // If the cursor is past the end of the line put it at the end.
    adjust_cursor_eol();
}

/*
 * Return the character name of the register with the given number.
 */
    int
get_register_name(int num)
{
    if (num == -1)
	return '"';
    else if (num < 10)
	return num + '0';
    else if (num == DELETION_REGISTER)
	return '-';
#ifdef FEAT_CLIPBOARD
    else if (num == STAR_REGISTER)
	return '*';
    else if (num == PLUS_REGISTER)
	return '+';
#endif
    else
    {
#ifdef EBCDIC
	int i;

	// EBCDIC is really braindead ...
	i = 'a' + (num - 10);
	if (i > 'i')
	    i += 7;
	if (i > 'r')
	    i += 8;
	return i;
#else
	return num + 'a' - 10;
#endif
    }
}

/*
 * ":dis" and ":registers": Display the contents of the yank registers.
 */
    void
ex_display(exarg_T *eap)
{
    int		i, n;
    long	j;
    char_u	*p;
    yankreg_T	*yb;
    int		name;
    int		attr;
    char_u	*arg = eap->arg;
    int		clen;
    int		type;

    if (arg != NULL && *arg == NUL)
	arg = NULL;
    attr = HL_ATTR(HLF_8);

    // Highlight title
    msg_puts_title(_("\nType Name Content"));
    for (i = -1; i < NUM_REGISTERS && !got_int; ++i)
    {
	name = get_register_name(i);
	switch (get_reg_type(name, NULL))
	{
	    case MLINE: type = 'l'; break;
	    case MCHAR: type = 'c'; break;
	    default:	type = 'b'; break;
	}
	if (arg != NULL && vim_strchr(arg, name) == NULL
#ifdef ONE_CLIPBOARD
	    // Star register and plus register contain the same thing.
		&& (name != '*' || vim_strchr(arg, '+') == NULL)
#endif
		)
	    continue;	    // did not ask for this register

#ifdef FEAT_CLIPBOARD
	// Adjust register name for "unnamed" in 'clipboard'.
	// When it's a clipboard register, fill it with the current contents
	// of the clipboard.
	adjust_clip_reg(&name);
	(void)may_get_selection(name);
#endif

	if (i == -1)
	{
	    if (y_previous != NULL)
		yb = y_previous;
	    else
		yb = &(y_regs[0]);
	}
	else
	    yb = &(y_regs[i]);

#ifdef FEAT_EVAL
	if (name == MB_TOLOWER(redir_reg)
		|| (redir_reg == '"' && yb == y_previous))
	    continue;	    // do not list register being written to, the
			    // pointer can be freed
#endif

	if (yb->y_array != NULL)
	{
	    int do_show = FALSE;

	    for (j = 0; !do_show && j < yb->y_size; ++j)
		do_show = !message_filtered(yb->y_array[j]);

	    if (do_show || yb->y_size == 0)
	    {
		msg_putchar('\n');
		msg_puts("  ");
		msg_putchar(type);
		msg_puts("  ");
		msg_putchar('"');
		msg_putchar(name);
		msg_puts("   ");

		n = (int)Columns - 11;
		for (j = 0; j < yb->y_size && n > 1; ++j)
		{
		    if (j)
		    {
			msg_puts_attr("^J", attr);
			n -= 2;
		    }
		    for (p = yb->y_array[j]; *p && (n -= ptr2cells(p)) >= 0;
									   ++p)
		    {
			clen = (*mb_ptr2len)(p);
			msg_outtrans_len(p, clen);
			p += clen - 1;
		    }
		}
		if (n > 1 && yb->y_type == MLINE)
		    msg_puts_attr("^J", attr);
		out_flush();		    // show one line at a time
	    }
	    ui_breakcheck();
	}
    }

    // display last inserted text
    if ((p = get_last_insert()) != NULL
		  && (arg == NULL || vim_strchr(arg, '.') != NULL) && !got_int
						      && !message_filtered(p))
    {
	msg_puts("\n  c  \".   ");
	dis_msg(p, TRUE);
    }

    // display last command line
    if (last_cmdline != NULL && (arg == NULL || vim_strchr(arg, ':') != NULL)
			       && !got_int && !message_filtered(last_cmdline))
    {
	msg_puts("\n  c  \":   ");
	dis_msg(last_cmdline, FALSE);
    }

    // display current file name
    if (curbuf->b_fname != NULL
	    && (arg == NULL || vim_strchr(arg, '%') != NULL) && !got_int
					&& !message_filtered(curbuf->b_fname))
    {
	msg_puts("\n  c  \"%   ");
	dis_msg(curbuf->b_fname, FALSE);
    }

    // display alternate file name
    if ((arg == NULL || vim_strchr(arg, '%') != NULL) && !got_int)
    {
	char_u	    *fname;
	linenr_T    dummy;

	if (buflist_name_nr(0, &fname, &dummy) != FAIL
						  && !message_filtered(fname))
	{
	    msg_puts("\n  c  \"#   ");
	    dis_msg(fname, FALSE);
	}
    }

    // display last search pattern
    if (last_search_pat() != NULL
		 && (arg == NULL || vim_strchr(arg, '/') != NULL) && !got_int
				      && !message_filtered(last_search_pat()))
    {
	msg_puts("\n  c  \"/   ");
	dis_msg(last_search_pat(), FALSE);
    }

#ifdef FEAT_EVAL
    // display last used expression
    if (expr_line != NULL && (arg == NULL || vim_strchr(arg, '=') != NULL)
				  && !got_int && !message_filtered(expr_line))
    {
	msg_puts("\n  c  \"=   ");
	dis_msg(expr_line, FALSE);
    }
#endif
}

/*
 * display a string for do_dis()
 * truncate at end of screen line
 */
    static void
dis_msg(
    char_u	*p,
    int		skip_esc)	    // if TRUE, ignore trailing ESC
{
    int		n;
    int		l;

    n = (int)Columns - 6;
    while (*p != NUL
	    && !(*p == ESC && skip_esc && *(p + 1) == NUL)
	    && (n -= ptr2cells(p)) >= 0)
    {
	if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1)
	{
	    msg_outtrans_len(p, l);
	    p += l;
	}
	else
	    msg_outtrans_len(p++, 1);
    }
    ui_breakcheck();
}

#if defined(FEAT_CLIPBOARD) || defined(PROTO)
    void
clip_free_selection(Clipboard_T *cbd)
{
    yankreg_T *y_ptr = y_current;

    if (cbd == &clip_plus)
	y_current = &y_regs[PLUS_REGISTER];
    else
	y_current = &y_regs[STAR_REGISTER];
    free_yank_all();
    y_current->y_size = 0;
    y_current = y_ptr;
}

/*
 * Get the selected text and put it in register '*' or '+'.
 */
    void
clip_get_selection(Clipboard_T *cbd)
{
    yankreg_T	*old_y_previous, *old_y_current;
    pos_T	old_cursor;
    pos_T	old_visual;
    int		old_visual_mode;
    colnr_T	old_curswant;
    int		old_set_curswant;
    pos_T	old_op_start, old_op_end;
    oparg_T	oa;
    cmdarg_T	ca;

    if (cbd->owned)
    {
	if ((cbd == &clip_plus && y_regs[PLUS_REGISTER].y_array != NULL)
		|| (cbd == &clip_star && y_regs[STAR_REGISTER].y_array != NULL))
	    return;

	// Get the text between clip_star.start & clip_star.end
	old_y_previous = y_previous;
	old_y_current = y_current;
	old_cursor = curwin->w_cursor;
	old_curswant = curwin->w_curswant;
	old_set_curswant = curwin->w_set_curswant;
	old_op_start = curbuf->b_op_start;
	old_op_end = curbuf->b_op_end;
	old_visual = VIsual;
	old_visual_mode = VIsual_mode;
	clear_oparg(&oa);
	oa.regname = (cbd == &clip_plus ? '+' : '*');
	oa.op_type = OP_YANK;
	vim_memset(&ca, 0, sizeof(ca));
	ca.oap = &oa;
	ca.cmdchar = 'y';
	ca.count1 = 1;
	ca.retval = CA_NO_ADJ_OP_END;
	do_pending_operator(&ca, 0, TRUE);
	y_previous = old_y_previous;
	y_current = old_y_current;
	curwin->w_cursor = old_cursor;
	changed_cline_bef_curs();   // need to update w_virtcol et al
	curwin->w_curswant = old_curswant;
	curwin->w_set_curswant = old_set_curswant;
	curbuf->b_op_start = old_op_start;
	curbuf->b_op_end = old_op_end;
	VIsual = old_visual;
	VIsual_mode = old_visual_mode;
    }
    else if (!is_clipboard_needs_update())
    {
	clip_free_selection(cbd);

	// Try to get selected text from another window
	clip_gen_request_selection(cbd);
    }
}

/*
 * Convert from the GUI selection string into the '*'/'+' register.
 */
    void
clip_yank_selection(
    int		type,
    char_u	*str,
    long	len,
    Clipboard_T *cbd)
{
    yankreg_T *y_ptr;

    if (cbd == &clip_plus)
	y_ptr = &y_regs[PLUS_REGISTER];
    else
	y_ptr = &y_regs[STAR_REGISTER];

    clip_free_selection(cbd);

    str_to_reg(y_ptr, type, str, len, 0L, FALSE);
}

/*
 * Convert the '*'/'+' register into a GUI selection string returned in *str
 * with length *len.
 * Returns the motion type, or -1 for failure.
 */
    int
clip_convert_selection(char_u **str, long_u *len, Clipboard_T *cbd)
{
    char_u	*p;
    int		lnum;
    int		i, j;
    int_u	eolsize;
    yankreg_T	*y_ptr;

    if (cbd == &clip_plus)
	y_ptr = &y_regs[PLUS_REGISTER];
    else
	y_ptr = &y_regs[STAR_REGISTER];

# ifdef USE_CRNL
    eolsize = 2;
# else
    eolsize = 1;
# endif

    *str = NULL;
    *len = 0;
    if (y_ptr->y_array == NULL)
	return -1;

    for (i = 0; i < y_ptr->y_size; i++)
	*len += (long_u)STRLEN(y_ptr->y_array[i]) + eolsize;

    // Don't want newline character at end of last line if we're in MCHAR mode.
    if (y_ptr->y_type == MCHAR && *len >= eolsize)
	*len -= eolsize;

    p = *str = alloc(*len + 1);	// add one to avoid zero
    if (p == NULL)
	return -1;
    lnum = 0;
    for (i = 0, j = 0; i < (int)*len; i++, j++)
    {
	if (y_ptr->y_array[lnum][j] == '\n')
	    p[i] = NUL;
	else if (y_ptr->y_array[lnum][j] == NUL)
	{
# ifdef USE_CRNL
	    p[i++] = '\r';
# endif
	    p[i] = '\n';
	    lnum++;
	    j = -1;
	}
	else
	    p[i] = y_ptr->y_array[lnum][j];
    }
    return y_ptr->y_type;
}


/*
 * If we have written to a clipboard register, send the text to the clipboard.
 */
    static void
may_set_selection(void)
{
    if (y_current == &(y_regs[STAR_REGISTER]) && clip_star.available)
    {
	clip_own_selection(&clip_star);
	clip_gen_set_selection(&clip_star);
    }
    else if (y_current == &(y_regs[PLUS_REGISTER]) && clip_plus.available)
    {
	clip_own_selection(&clip_plus);
	clip_gen_set_selection(&clip_plus);
    }
}

#endif // FEAT_CLIPBOARD || PROTO


#if defined(FEAT_DND) || defined(PROTO)
/*
 * Replace the contents of the '~' register with str.
 */
    void
dnd_yank_drag_data(char_u *str, long len)
{
    yankreg_T *curr;

    curr = y_current;
    y_current = &y_regs[TILDE_REGISTER];
    free_yank_all();
    str_to_reg(y_current, MCHAR, str, len, 0L, FALSE);
    y_current = curr;
}
#endif


/*
 * Return the type of a register.
 * Used for getregtype()
 * Returns MAUTO for error.
 */
    char_u
get_reg_type(int regname, long *reglen)
{
    switch (regname)
    {
	case '%':		// file name
	case '#':		// alternate file name
	case '=':		// expression
	case ':':		// last command line
	case '/':		// last search-pattern
	case '.':		// last inserted text
# ifdef FEAT_SEARCHPATH
	case Ctrl_F:		// Filename under cursor
	case Ctrl_P:		// Path under cursor, expand via "path"
# endif
	case Ctrl_W:		// word under cursor
	case Ctrl_A:		// WORD (mnemonic All) under cursor
	case '_':		// black hole: always empty
	    return MCHAR;
    }

# ifdef FEAT_CLIPBOARD
    regname = may_get_selection(regname);
# endif

    if (regname != NUL && !valid_yank_reg(regname, FALSE))
	return MAUTO;

    get_yank_register(regname, FALSE);

    if (y_current->y_array != NULL)
    {
	if (reglen != NULL && y_current->y_type == MBLOCK)
	    *reglen = y_current->y_width;
	return y_current->y_type;
    }
    return MAUTO;
}

#if defined(FEAT_EVAL) || defined(PROTO)
/*
 * When "flags" has GREG_LIST return a list with text "s".
 * Otherwise just return "s".
 */
    static char_u *
getreg_wrap_one_line(char_u *s, int flags)
{
    if (flags & GREG_LIST)
    {
	list_T *list = list_alloc();

	if (list != NULL)
	{
	    if (list_append_string(list, NULL, -1) == FAIL)
	    {
		list_free(list);
		return NULL;
	    }
	    list->lv_first->li_tv.vval.v_string = s;
	}
	return (char_u *)list;
    }
    return s;
}

/*
 * Return the contents of a register as a single allocated string.
 * Used for "@r" in expressions and for getreg().
 * Returns NULL for error.
 * Flags:
 *	GREG_NO_EXPR	Do not allow expression register
 *	GREG_EXPR_SRC	For the expression register: return expression itself,
 *			not the result of its evaluation.
 *	GREG_LIST	Return a list of lines in place of a single string.
 */
    char_u *
get_reg_contents(int regname, int flags)
{
    long	i;
    char_u	*retval;
    int		allocated;
    long	len;

    // Don't allow using an expression register inside an expression
    if (regname == '=')
    {
	if (flags & GREG_NO_EXPR)
	    return NULL;
	if (flags & GREG_EXPR_SRC)
	    return getreg_wrap_one_line(get_expr_line_src(), flags);
	return getreg_wrap_one_line(get_expr_line(), flags);
    }

    if (regname == '@')	    // "@@" is used for unnamed register
	regname = '"';

    // check for valid regname
    if (regname != NUL && !valid_yank_reg(regname, FALSE))
	return NULL;

# ifdef FEAT_CLIPBOARD
    regname = may_get_selection(regname);
# endif

    if (get_spec_reg(regname, &retval, &allocated, FALSE))
    {
	if (retval == NULL)
	    return NULL;
	if (allocated)
	    return getreg_wrap_one_line(retval, flags);
	return getreg_wrap_one_line(vim_strsave(retval), flags);
    }

    get_yank_register(regname, FALSE);
    if (y_current->y_array == NULL)
	return NULL;

    if (flags & GREG_LIST)
    {
	list_T	*list = list_alloc();
	int	error = FALSE;

	if (list == NULL)
	    return NULL;
	for (i = 0; i < y_current->y_size; ++i)
	    if (list_append_string(list, y_current->y_array[i], -1) == FAIL)
		error = TRUE;
	if (error)
	{
	    list_free(list);
	    return NULL;
	}
	return (char_u *)list;
    }

    // Compute length of resulting string.
    len = 0;
    for (i = 0; i < y_current->y_size; ++i)
    {
	len += (long)STRLEN(y_current->y_array[i]);
	// Insert a newline between lines and after last line if
	// y_type is MLINE.
	if (y_current->y_type == MLINE || i < y_current->y_size - 1)
	    ++len;
    }

    retval = alloc(len + 1);

    // Copy the lines of the yank register into the string.
    if (retval != NULL)
    {
	len = 0;
	for (i = 0; i < y_current->y_size; ++i)
	{
	    STRCPY(retval + len, y_current->y_array[i]);
	    len += (long)STRLEN(retval + len);

	    // Insert a NL between lines and after the last line if y_type is
	    // MLINE.
	    if (y_current->y_type == MLINE || i < y_current->y_size - 1)
		retval[len++] = '\n';
	}
	retval[len] = NUL;
    }

    return retval;
}

    static int
init_write_reg(
    int		name,
    yankreg_T	**old_y_previous,
    yankreg_T	**old_y_current,
    int		must_append,
    int		*yank_type UNUSED)
{
    if (!valid_yank_reg(name, TRUE))	    // check for valid reg name
    {
	emsg_invreg(name);
	return FAIL;
    }

    // Don't want to change the current (unnamed) register
    *old_y_previous = y_previous;
    *old_y_current = y_current;

    get_yank_register(name, TRUE);
    if (!y_append && !must_append)
	free_yank_all();
    return OK;
}

    static void
finish_write_reg(
    int		name,
    yankreg_T	*old_y_previous,
    yankreg_T	*old_y_current)
{
# ifdef FEAT_CLIPBOARD
    // Send text of clipboard register to the clipboard.
    may_set_selection();
# endif

    // ':let @" = "val"' should change the meaning of the "" register
    if (name != '"')
	y_previous = old_y_previous;
    y_current = old_y_current;
}

/*
 * Store string "str" in register "name".
 * "maxlen" is the maximum number of bytes to use, -1 for all bytes.
 * If "must_append" is TRUE, always append to the register.  Otherwise append
 * if "name" is an uppercase letter.
 * Note: "maxlen" and "must_append" don't work for the "/" register.
 * Careful: 'str' is modified, you may have to use a copy!
 * If "str" ends in '\n' or '\r', use linewise, otherwise use characterwise.
 */
    void
write_reg_contents(
    int		name,
    char_u	*str,
    int		maxlen,
    int		must_append)
{
    write_reg_contents_ex(name, str, maxlen, must_append, MAUTO, 0L);
}

    void
write_reg_contents_lst(
    int		name,
    char_u	**strings,
    int		maxlen UNUSED,
    int		must_append,
    int		yank_type,
    long	block_len)
{
    yankreg_T  *old_y_previous, *old_y_current;

    if (name == '/' || name == '=')
    {
	char_u	*s;

	if (strings[0] == NULL)
	    s = (char_u *)"";
	else if (strings[1] != NULL)
	{
	    emsg(_("E883: search pattern and expression register may not "
			"contain two or more lines"));
	    return;
	}
	else
	    s = strings[0];
	write_reg_contents_ex(name, s, -1, must_append, yank_type, block_len);
	return;
    }

    if (name == '_')	    // black hole: nothing to do
	return;

    if (init_write_reg(name, &old_y_previous, &old_y_current, must_append,
		&yank_type) == FAIL)
	return;

    str_to_reg(y_current, yank_type, (char_u *) strings, -1, block_len, TRUE);

    finish_write_reg(name, old_y_previous, old_y_current);
}

    void
write_reg_contents_ex(
    int		name,
    char_u	*str,
    int		maxlen,
    int		must_append,
    int		yank_type,
    long	block_len)
{
    yankreg_T	*old_y_previous, *old_y_current;
    long	len;

    if (maxlen >= 0)
	len = maxlen;
    else
	len = (long)STRLEN(str);

    // Special case: '/' search pattern
    if (name == '/')
    {
	set_last_search_pat(str, RE_SEARCH, TRUE, TRUE);
	return;
    }

    if (name == '#')
    {
	buf_T	*buf;

	if (VIM_ISDIGIT(*str))
	{
	    int	num = atoi((char *)str);

	    buf = buflist_findnr(num);
	    if (buf == NULL)
		semsg(_(e_nobufnr), (long)num);
	}
	else
	    buf = buflist_findnr(buflist_findpat(str, str + STRLEN(str),
							 TRUE, FALSE, FALSE));
	if (buf == NULL)
	    return;
	curwin->w_alt_fnum = buf->b_fnum;
	return;
    }

    if (name == '=')
    {
	char_u	    *p, *s;

	p = vim_strnsave(str, (int)len);
	if (p == NULL)
	    return;
	if (must_append)
	{
	    s = concat_str(get_expr_line_src(), p);
	    vim_free(p);
	    p = s;
	}
	set_expr_line(p);
	return;
    }

    if (name == '_')	    // black hole: nothing to do
	return;

    if (init_write_reg(name, &old_y_previous, &old_y_current, must_append,
		&yank_type) == FAIL)
	return;

    str_to_reg(y_current, yank_type, str, len, block_len, FALSE);

    finish_write_reg(name, old_y_previous, old_y_current);
}
#endif	// FEAT_EVAL

#if defined(FEAT_CLIPBOARD) || defined(FEAT_EVAL)
/*
 * Put a string into a register.  When the register is not empty, the string
 * is appended.
 */
    static void
str_to_reg(
    yankreg_T	*y_ptr,		// pointer to yank register
    int		yank_type,	// MCHAR, MLINE, MBLOCK, MAUTO
    char_u	*str,		// string to put in register
    long	len,		// length of string
    long	blocklen,	// width of Visual block
    int		str_list)	// TRUE if str is char_u **
{
    int		type;			// MCHAR, MLINE or MBLOCK
    int		lnum;
    long	start;
    long	i;
    int		extra;
    int		newlines;		// number of lines added
    int		extraline = 0;		// extra line at the end
    int		append = FALSE;		// append to last line in register
    char_u	*s;
    char_u	**ss;
    char_u	**pp;
    long	maxlen;

    if (y_ptr->y_array == NULL)		// NULL means empty register
	y_ptr->y_size = 0;

    if (yank_type == MAUTO)
	type = ((str_list || (len > 0 && (str[len - 1] == NL
					    || str[len - 1] == CAR)))
							     ? MLINE : MCHAR);
    else
	type = yank_type;

    // Count the number of lines within the string
    newlines = 0;
    if (str_list)
    {
	for (ss = (char_u **) str; *ss != NULL; ++ss)
	    ++newlines;
    }
    else
    {
	for (i = 0; i < len; i++)
	    if (str[i] == '\n')
		++newlines;
	if (type == MCHAR || len == 0 || str[len - 1] != '\n')
	{
	    extraline = 1;
	    ++newlines;	// count extra newline at the end
	}
	if (y_ptr->y_size > 0 && y_ptr->y_type == MCHAR)
	{
	    append = TRUE;
	    --newlines;	// uncount newline when appending first line
	}
    }

    // Without any lines make the register empty.
    if (y_ptr->y_size + newlines == 0)
    {
	VIM_CLEAR(y_ptr->y_array);
	return;
    }

    // Allocate an array to hold the pointers to the new register lines.
    // If the register was not empty, move the existing lines to the new array.
    pp = lalloc_clear((y_ptr->y_size + newlines) * sizeof(char_u *), TRUE);
    if (pp == NULL)	// out of memory
	return;
    for (lnum = 0; lnum < y_ptr->y_size; ++lnum)
	pp[lnum] = y_ptr->y_array[lnum];
    vim_free(y_ptr->y_array);
    y_ptr->y_array = pp;
    maxlen = 0;

    // Find the end of each line and save it into the array.
    if (str_list)
    {
	for (ss = (char_u **) str; *ss != NULL; ++ss, ++lnum)
	{
	    i = (long)STRLEN(*ss);
	    pp[lnum] = vim_strnsave(*ss, i);
	    if (i > maxlen)
		maxlen = i;
	}
    }
    else
    {
	for (start = 0; start < len + extraline; start += i + 1)
	{
	    for (i = start; i < len; ++i)	// find the end of the line
		if (str[i] == '\n')
		    break;
	    i -= start;			// i is now length of line
	    if (i > maxlen)
		maxlen = i;
	    if (append)
	    {
		--lnum;
		extra = (int)STRLEN(y_ptr->y_array[lnum]);
	    }
	    else
		extra = 0;
	    s = alloc(i + extra + 1);
	    if (s == NULL)
		break;
	    if (extra)
		mch_memmove(s, y_ptr->y_array[lnum], (size_t)extra);
	    if (append)
		vim_free(y_ptr->y_array[lnum]);
	    if (i)
		mch_memmove(s + extra, str + start, (size_t)i);
	    extra += i;
	    s[extra] = NUL;
	    y_ptr->y_array[lnum++] = s;
	    while (--extra >= 0)
	    {
		if (*s == NUL)
		    *s = '\n';	    // replace NUL with newline
		++s;
	    }
	    append = FALSE;		    // only first line is appended
	}
    }
    y_ptr->y_type = type;
    y_ptr->y_size = lnum;
    if (type == MBLOCK)
	y_ptr->y_width = (blocklen < 0 ? maxlen - 1 : blocklen);
    else
	y_ptr->y_width = 0;
# ifdef FEAT_VIMINFO
    y_ptr->y_time_set = vim_time();
# endif
}
#endif // FEAT_CLIPBOARD || FEAT_EVAL || PROTO
