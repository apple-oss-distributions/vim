/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * ex_docmd.c: functions for executing an Ex command line.
 */

#include "vim.h"

static int	quitmore = 0;
static int	ex_pressedreturn = FALSE;
#ifndef FEAT_PRINTER
# define ex_hardcopy	ex_ni
#endif

#ifdef FEAT_EVAL
static char_u	*do_one_cmd(char_u **, int, struct condstack *, char_u *(*fgetline)(int, void *, int, int), void *cookie);
#else
static char_u	*do_one_cmd(char_u **, int, char_u *(*fgetline)(int, void *, int, int), void *cookie);
static int	if_level = 0;		/* depth in :if */
#endif
static void	free_cmdmod(void);
static void	append_command(char_u *cmd);
static char_u	*find_command(exarg_T *eap, int *full);

#ifndef FEAT_MENU
# define ex_emenu		ex_ni
# define ex_menu		ex_ni
# define ex_menutranslate	ex_ni
#endif
static void	ex_autocmd(exarg_T *eap);
static void	ex_doautocmd(exarg_T *eap);
static void	ex_bunload(exarg_T *eap);
static void	ex_buffer(exarg_T *eap);
static void	ex_bmodified(exarg_T *eap);
static void	ex_bnext(exarg_T *eap);
static void	ex_bprevious(exarg_T *eap);
static void	ex_brewind(exarg_T *eap);
static void	ex_blast(exarg_T *eap);
static char_u	*getargcmd(char_u **);
static int	getargopt(exarg_T *eap);
#ifndef FEAT_QUICKFIX
# define ex_make		ex_ni
# define ex_cbuffer		ex_ni
# define ex_cc			ex_ni
# define ex_cnext		ex_ni
# define ex_cbelow		ex_ni
# define ex_cfile		ex_ni
# define qf_list		ex_ni
# define qf_age			ex_ni
# define qf_history		ex_ni
# define ex_helpgrep		ex_ni
# define ex_vimgrep		ex_ni
#endif
#if !defined(FEAT_QUICKFIX)
# define ex_cclose		ex_ni
# define ex_copen		ex_ni
# define ex_cwindow		ex_ni
# define ex_cbottom		ex_ni
#endif
#if !defined(FEAT_QUICKFIX) || !defined(FEAT_EVAL)
# define ex_cexpr		ex_ni
#endif

static linenr_T get_address(exarg_T *, char_u **, cmd_addr_T addr_type, int skip, int silent, int to_other_file, int address_count);
static void	get_flags(exarg_T *eap);
#if !defined(FEAT_PERL) \
	|| !defined(FEAT_PYTHON) || !defined(FEAT_PYTHON3) \
	|| !defined(FEAT_TCL) \
	|| !defined(FEAT_RUBY) \
	|| !defined(FEAT_LUA) \
	|| !defined(FEAT_MZSCHEME)
# define HAVE_EX_SCRIPT_NI
static void	ex_script_ni(exarg_T *eap);
#endif
static char	*invalid_range(exarg_T *eap);
static void	correct_range(exarg_T *eap);
#ifdef FEAT_QUICKFIX
static char_u	*replace_makeprg(exarg_T *eap, char_u *p, char_u **cmdlinep);
#endif
static char_u	*repl_cmdline(exarg_T *eap, char_u *src, int srclen, char_u *repl, char_u **cmdlinep);
static void	ex_highlight(exarg_T *eap);
static void	ex_colorscheme(exarg_T *eap);
static void	ex_cquit(exarg_T *eap);
static void	ex_quit_all(exarg_T *eap);
static void	ex_close(exarg_T *eap);
static void	ex_win_close(int forceit, win_T *win, tabpage_T *tp);
static void	ex_only(exarg_T *eap);
static void	ex_resize(exarg_T *eap);
static void	ex_stag(exarg_T *eap);
static void	ex_tabclose(exarg_T *eap);
static void	ex_tabonly(exarg_T *eap);
static void	ex_tabnext(exarg_T *eap);
static void	ex_tabmove(exarg_T *eap);
static void	ex_tabs(exarg_T *eap);
#if defined(FEAT_QUICKFIX)
static void	ex_pclose(exarg_T *eap);
static void	ex_ptag(exarg_T *eap);
static void	ex_pedit(exarg_T *eap);
#else
# define ex_pclose		ex_ni
# define ex_ptag		ex_ni
# define ex_pedit		ex_ni
#endif
static void	ex_hide(exarg_T *eap);
static void	ex_stop(exarg_T *eap);
static void	ex_exit(exarg_T *eap);
static void	ex_print(exarg_T *eap);
#ifdef FEAT_BYTEOFF
static void	ex_goto(exarg_T *eap);
#else
# define ex_goto		ex_ni
#endif
static void	ex_shell(exarg_T *eap);
static void	ex_preserve(exarg_T *eap);
static void	ex_recover(exarg_T *eap);
static void	ex_mode(exarg_T *eap);
static void	ex_wrongmodifier(exarg_T *eap);
static void	ex_find(exarg_T *eap);
static void	ex_open(exarg_T *eap);
static void	ex_edit(exarg_T *eap);
#ifndef FEAT_GUI
# define ex_gui			ex_nogui
static void	ex_nogui(exarg_T *eap);
#endif
#if defined(FEAT_GUI_MSWIN) && defined(FEAT_MENU) && defined(FEAT_TEAROFF)
static void	ex_tearoff(exarg_T *eap);
#else
# define ex_tearoff		ex_ni
#endif
#if (defined(FEAT_GUI_MSWIN) || defined(FEAT_GUI_GTK) \
	|| defined(FEAT_TERM_POPUP_MENU)) && defined(FEAT_MENU)
static void	ex_popup(exarg_T *eap);
#else
# define ex_popup		ex_ni
#endif
#ifndef FEAT_GUI_MSWIN
# define ex_simalt		ex_ni
#endif
#if !defined(FEAT_GUI_MSWIN) && !defined(FEAT_GUI_GTK) && !defined(FEAT_GUI_MOTIF)
# define gui_mch_find_dialog	ex_ni
# define gui_mch_replace_dialog ex_ni
#endif
#if !defined(FEAT_GUI_GTK)
# define ex_helpfind		ex_ni
#endif
#ifndef FEAT_CSCOPE
# define ex_cscope		ex_ni
# define ex_scscope		ex_ni
# define ex_cstag		ex_ni
#endif
#ifndef FEAT_SYN_HL
# define ex_syntax		ex_ni
# define ex_ownsyntax		ex_ni
#endif
#if !defined(FEAT_SYN_HL) || !defined(FEAT_PROFILE)
# define ex_syntime		ex_ni
#endif
#ifndef FEAT_SPELL
# define ex_spell		ex_ni
# define ex_mkspell		ex_ni
# define ex_spelldump		ex_ni
# define ex_spellinfo		ex_ni
# define ex_spellrepall		ex_ni
#endif
#ifndef FEAT_PERSISTENT_UNDO
# define ex_rundo		ex_ni
# define ex_wundo		ex_ni
#endif
#ifndef FEAT_LUA
# define ex_lua			ex_script_ni
# define ex_luado		ex_ni
# define ex_luafile		ex_ni
#endif
#ifndef FEAT_MZSCHEME
# define ex_mzscheme		ex_script_ni
# define ex_mzfile		ex_ni
#endif
#ifndef FEAT_PERL
# define ex_perl		ex_script_ni
# define ex_perldo		ex_ni
#endif
#ifndef FEAT_PYTHON
# define ex_python		ex_script_ni
# define ex_pydo		ex_ni
# define ex_pyfile		ex_ni
#endif
#ifndef FEAT_PYTHON3
# define ex_py3			ex_script_ni
# define ex_py3do		ex_ni
# define ex_py3file		ex_ni
#endif
#if !defined(FEAT_PYTHON) && !defined(FEAT_PYTHON3)
# define ex_pyx			ex_script_ni
# define ex_pyxdo		ex_ni
# define ex_pyxfile		ex_ni
#endif
#ifndef FEAT_TCL
# define ex_tcl			ex_script_ni
# define ex_tcldo		ex_ni
# define ex_tclfile		ex_ni
#endif
#ifndef FEAT_RUBY
# define ex_ruby		ex_script_ni
# define ex_rubydo		ex_ni
# define ex_rubyfile		ex_ni
#endif
#ifndef FEAT_KEYMAP
# define ex_loadkeymap		ex_ni
#endif
static void	ex_swapname(exarg_T *eap);
static void	ex_syncbind(exarg_T *eap);
static void	ex_read(exarg_T *eap);
static void	ex_pwd(exarg_T *eap);
static void	ex_equal(exarg_T *eap);
static void	ex_sleep(exarg_T *eap);
static void	ex_winsize(exarg_T *eap);
static void	ex_wincmd(exarg_T *eap);
#if defined(FEAT_GUI) || defined(UNIX) || defined(VMS) || defined(MSWIN)
static void	ex_winpos(exarg_T *eap);
#else
# define ex_winpos	    ex_ni
#endif
static void	ex_operators(exarg_T *eap);
static void	ex_put(exarg_T *eap);
static void	ex_copymove(exarg_T *eap);
static void	ex_submagic(exarg_T *eap);
static void	ex_join(exarg_T *eap);
static void	ex_at(exarg_T *eap);
static void	ex_bang(exarg_T *eap);
static void	ex_undo(exarg_T *eap);
#ifdef FEAT_PERSISTENT_UNDO
static void	ex_wundo(exarg_T *eap);
static void	ex_rundo(exarg_T *eap);
#endif
static void	ex_redo(exarg_T *eap);
static void	ex_later(exarg_T *eap);
static void	ex_redir(exarg_T *eap);
static void	ex_redrawstatus(exarg_T *eap);
static void	ex_redrawtabline(exarg_T *eap);
static void	close_redir(void);
static void	ex_mark(exarg_T *eap);
static void	ex_startinsert(exarg_T *eap);
static void	ex_stopinsert(exarg_T *eap);
#ifdef FEAT_FIND_ID
static void	ex_checkpath(exarg_T *eap);
static void	ex_findpat(exarg_T *eap);
#else
# define ex_findpat		ex_ni
# define ex_checkpath		ex_ni
#endif
#if defined(FEAT_FIND_ID) && defined(FEAT_QUICKFIX)
static void	ex_psearch(exarg_T *eap);
#else
# define ex_psearch		ex_ni
#endif
static void	ex_tag(exarg_T *eap);
static void	ex_tag_cmd(exarg_T *eap, char_u *name);
#ifndef FEAT_EVAL
# define ex_break		ex_ni
# define ex_breakadd		ex_ni
# define ex_breakdel		ex_ni
# define ex_breaklist		ex_ni
# define ex_call		ex_ni
# define ex_catch		ex_ni
# define ex_compiler		ex_ni
# define ex_const		ex_ni
# define ex_continue		ex_ni
# define ex_debug		ex_ni
# define ex_debuggreedy		ex_ni
# define ex_delfunction		ex_ni
# define ex_echo		ex_ni
# define ex_echohl		ex_ni
# define ex_else		ex_ni
# define ex_endfunction		ex_ni
# define ex_endif		ex_ni
# define ex_endtry		ex_ni
# define ex_endwhile		ex_ni
# define ex_eval		ex_ni
# define ex_execute		ex_ni
# define ex_finally		ex_ni
# define ex_finish		ex_ni
# define ex_function		ex_ni
# define ex_if			ex_ni
# define ex_let			ex_ni
# define ex_lockvar		ex_ni
# define ex_oldfiles		ex_ni
# define ex_options		ex_ni
# define ex_packadd		ex_ni
# define ex_packloadall		ex_ni
# define ex_return		ex_ni
# define ex_scriptnames		ex_ni
# define ex_throw		ex_ni
# define ex_try			ex_ni
# define ex_unlet		ex_ni
# define ex_unlockvar		ex_ni
# define ex_while		ex_ni
#endif
#ifndef FEAT_SESSION
# define ex_loadview		ex_ni
#endif
#ifndef FEAT_VIMINFO
# define ex_viminfo		ex_ni
#endif
static void	ex_behave(exarg_T *eap);
static void	ex_filetype(exarg_T *eap);
static void	ex_setfiletype(exarg_T *eap);
#ifndef FEAT_DIFF
# define ex_diffoff		ex_ni
# define ex_diffpatch		ex_ni
# define ex_diffgetput		ex_ni
# define ex_diffsplit		ex_ni
# define ex_diffthis		ex_ni
# define ex_diffupdate		ex_ni
#endif
static void	ex_digraphs(exarg_T *eap);
static void	ex_set(exarg_T *eap);
#ifdef FEAT_SEARCH_EXTRA
static void	ex_nohlsearch(exarg_T *eap);
#else
# define ex_nohlsearch		ex_ni
# define ex_match		ex_ni
#endif
#ifdef FEAT_CRYPT
static void	ex_X(exarg_T *eap);
#else
# define ex_X			ex_ni
#endif
#ifdef FEAT_FOLDING
static void	ex_fold(exarg_T *eap);
static void	ex_foldopen(exarg_T *eap);
static void	ex_folddo(exarg_T *eap);
#else
# define ex_fold		ex_ni
# define ex_foldopen		ex_ni
# define ex_folddo		ex_ni
#endif
#if !(defined(HAVE_LOCALE_H) || defined(X_LOCALE))
# define ex_language		ex_ni
#endif
#ifndef FEAT_SIGNS
# define ex_sign		ex_ni
#endif
#ifndef FEAT_NETBEANS_INTG
# define ex_nbclose		ex_ni
# define ex_nbkey		ex_ni
# define ex_nbstart		ex_ni
#endif

#ifndef FEAT_JUMPLIST
# define ex_jumps		ex_ni
# define ex_clearjumps		ex_ni
# define ex_changes		ex_ni
#endif

#ifndef FEAT_PROFILE
# define ex_profile		ex_ni
#endif
#ifndef FEAT_TERMINAL
# define ex_terminal		ex_ni
#endif
#if !defined(FEAT_X11) || !defined(FEAT_XCLIPBOARD)
# define ex_xrestore		ex_ni
#endif
#if !defined(FEAT_TEXT_PROP)
# define ex_popupclear		ex_ni
#endif

/*
 * Declare cmdnames[].
 */
#define DO_DECLARE_EXCMD
#include "ex_cmds.h"

/*
 * Table used to quickly search for a command, based on its first character.
 */
static cmdidx_T cmdidxs[27] =
{
      CMD_append,
      CMD_buffer,
      CMD_change,
      CMD_delete,
      CMD_edit,
      CMD_file,
      CMD_global,
      CMD_help,
      CMD_insert,
      CMD_join,
      CMD_k,
      CMD_list,
      CMD_move,
      CMD_next,
      CMD_open,
      CMD_print,
      CMD_quit,
      CMD_read,
      CMD_substitute,
      CMD_t,
      CMD_undo,
      CMD_vglobal,
      CMD_write,
      CMD_xit,
      CMD_yank,
      CMD_z,
      CMD_bang
};

static char_u dollar_command[2] = {'$', 0};


#ifdef FEAT_EVAL
/* Struct for storing a line inside a while/for loop */
typedef struct
{
    char_u	*line;		/* command line */
    linenr_T	lnum;		/* sourcing_lnum of the line */
} wcmd_T;

/*
 * Structure used to store info for line position in a while or for loop.
 * This is required, because do_one_cmd() may invoke ex_function(), which
 * reads more lines that may come from the while/for loop.
 */
struct loop_cookie
{
    garray_T	*lines_gap;		/* growarray with line info */
    int		current_line;		/* last read line from growarray */
    int		repeating;		/* TRUE when looping a second time */
    /* When "repeating" is FALSE use "getline" and "cookie" to get lines */
    char_u	*(*getline)(int, void *, int, int);
    void	*cookie;
};

static char_u	*get_loop_line(int c, void *cookie, int indent, int do_concat);
static int	store_loop_line(garray_T *gap, char_u *line);
static void	free_cmdlines(garray_T *gap);

/* Struct to save a few things while debugging.  Used in do_cmdline() only. */
struct dbg_stuff
{
    int		trylevel;
    int		force_abort;
    except_T	*caught_stack;
    char_u	*vv_exception;
    char_u	*vv_throwpoint;
    int		did_emsg;
    int		got_int;
    int		did_throw;
    int		need_rethrow;
    int		check_cstack;
    except_T	*current_exception;
};

    static void
save_dbg_stuff(struct dbg_stuff *dsp)
{
    dsp->trylevel	= trylevel;		trylevel = 0;
    dsp->force_abort	= force_abort;		force_abort = FALSE;
    dsp->caught_stack	= caught_stack;		caught_stack = NULL;
    dsp->vv_exception	= v_exception(NULL);
    dsp->vv_throwpoint	= v_throwpoint(NULL);

    /* Necessary for debugging an inactive ":catch", ":finally", ":endtry" */
    dsp->did_emsg	= did_emsg;		did_emsg     = FALSE;
    dsp->got_int	= got_int;		got_int	     = FALSE;
    dsp->did_throw	= did_throw;		did_throw    = FALSE;
    dsp->need_rethrow	= need_rethrow;		need_rethrow = FALSE;
    dsp->check_cstack	= check_cstack;		check_cstack = FALSE;
    dsp->current_exception = current_exception;	current_exception = NULL;
}

    static void
restore_dbg_stuff(struct dbg_stuff *dsp)
{
    suppress_errthrow = FALSE;
    trylevel = dsp->trylevel;
    force_abort = dsp->force_abort;
    caught_stack = dsp->caught_stack;
    (void)v_exception(dsp->vv_exception);
    (void)v_throwpoint(dsp->vv_throwpoint);
    did_emsg = dsp->did_emsg;
    got_int = dsp->got_int;
    did_throw = dsp->did_throw;
    need_rethrow = dsp->need_rethrow;
    check_cstack = dsp->check_cstack;
    current_exception = dsp->current_exception;
}
#endif


/*
 * do_exmode(): Repeatedly get commands for the "Ex" mode, until the ":vi"
 * command is given.
 */
    void
do_exmode(
    int		improved)	    /* TRUE for "improved Ex" mode */
{
    int		save_msg_scroll;
    int		prev_msg_row;
    linenr_T	prev_line;
    varnumber_T	changedtick;

    if (improved)
	exmode_active = EXMODE_VIM;
    else
	exmode_active = EXMODE_NORMAL;
    State = NORMAL;

    /* When using ":global /pat/ visual" and then "Q" we return to continue
     * the :global command. */
    if (global_busy)
	return;

    save_msg_scroll = msg_scroll;
    ++RedrawingDisabled;	    /* don't redisplay the window */
    ++no_wait_return;		    /* don't wait for return */
#ifdef FEAT_GUI
    /* Ignore scrollbar and mouse events in Ex mode */
    ++hold_gui_events;
#endif

    msg(_("Entering Ex mode.  Type \"visual\" to go to Normal mode."));
    while (exmode_active)
    {
	global_last_cmd = 0;
	/* Check for a ":normal" command and no more characters left. */
	if (ex_normal_busy > 0 && typebuf.tb_len == 0)
	{
	    exmode_active = FALSE;
	    break;
	}
	msg_scroll = TRUE;
	need_wait_return = FALSE;
	ex_pressedreturn = FALSE;
	ex_no_reprint = FALSE;
	changedtick = CHANGEDTICK(curbuf);
	prev_msg_row = msg_row;
	prev_line = curwin->w_cursor.lnum;
	if (improved)
	{
	    cmdline_row = msg_row;
	    do_cmdline(NULL, getexline, NULL, 0);
	}
	else
	    do_cmdline(NULL, getexmodeline, NULL, DOCMD_NOWAIT);
	lines_left = Rows - 1;

	if ((prev_line != curwin->w_cursor.lnum
		   || changedtick != CHANGEDTICK(curbuf)) && !ex_no_reprint)
	{
	    if ((curbuf->b_ml.ml_flags & ML_EMPTY) && !Unix2003_compat)
		emsg(_(e_emptybuf));
	    else
	    {
		if (ex_pressedreturn)
		{
		    /* go up one line, to overwrite the ":<CR>" line, so the
		     * output doesn't contain empty lines. */
		    msg_row = prev_msg_row;
		    if (prev_msg_row == Rows - 1)
			msg_row--;
		}
		msg_col = 0;
		if (p_ap) {	/* only if  autoprint is on */
		    if (!(Unix2003_compat && global_last_cmd)) {
			 /* not if :global or :v ex_08 test 478 */
			print_line_no_prefix(curwin->w_cursor.lnum, FALSE, FALSE);
			msg_clr_eos();
		    }
		}
	    }
	}
	else if (ex_pressedreturn && !ex_no_reprint)	/* must be at EOF */
	{
	    if (curbuf->b_ml.ml_flags & ML_EMPTY)
		emsg(_(e_emptybuf));
	    else
		emsg(_("E501: At end-of-file"));
	}
    }

#ifdef FEAT_GUI
    --hold_gui_events;
#endif
    --RedrawingDisabled;
    --no_wait_return;
    update_screen(CLEAR);
    need_wait_return = FALSE;
    msg_scroll = save_msg_scroll;
}

/*
 * Print the executed command for when 'verbose' is set.
 * When "lnum" is 0 only print the command.
 */
    static void
msg_verbose_cmd(linenr_T lnum, char_u *cmd)
{
    ++no_wait_return;
    verbose_enter_scroll();

    if (lnum == 0)
	smsg(_("Executing: %s"), cmd);
    else
	smsg(_("line %ld: %s"), (long)lnum, cmd);
    if (msg_silent == 0)
	msg_puts("\n");   // don't overwrite this

    verbose_leave_scroll();
    --no_wait_return;
}

/*
 * Execute a simple command line.  Used for translated commands like "*".
 */
    int
do_cmdline_cmd(char_u *cmd)
{
    return do_cmdline(cmd, NULL, NULL,
				   DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_KEYTYPED);
}

/*
 * do_cmdline(): execute one Ex command line
 *
 * 1. Execute "cmdline" when it is not NULL.
 *    If "cmdline" is NULL, or more lines are needed, fgetline() is used.
 * 2. Split up in parts separated with '|'.
 *
 * This function can be called recursively!
 *
 * flags:
 * DOCMD_VERBOSE  - The command will be included in the error message.
 * DOCMD_NOWAIT   - Don't call wait_return() and friends.
 * DOCMD_REPEAT   - Repeat execution until fgetline() returns NULL.
 * DOCMD_KEYTYPED - Don't reset KeyTyped.
 * DOCMD_EXCRESET - Reset the exception environment (used for debugging).
 * DOCMD_KEEPLINE - Store first typed line (for repeating with ".").
 *
 * return FAIL if cmdline could not be executed, OK otherwise
 */
    int
do_cmdline(
    char_u	*cmdline,
    char_u	*(*fgetline)(int, void *, int, int),
    void	*cookie,		/* argument for fgetline() */
    int		flags)
{
    char_u	*next_cmdline;		/* next cmd to execute */
    char_u	*cmdline_copy = NULL;	/* copy of cmd line */
    int		used_getline = FALSE;	/* used "fgetline" to obtain command */
    static int	recursive = 0;		/* recursive depth */
    int		msg_didout_before_start = 0;
    int		count = 0;		/* line number count */
    int		did_inc = FALSE;	/* incremented RedrawingDisabled */
    int		retval = OK;
#ifdef FEAT_EVAL
    struct condstack cstack;		/* conditional stack */
    garray_T	lines_ga;		/* keep lines for ":while"/":for" */
    int		current_line = 0;	/* active line in lines_ga */
    char_u	*fname = NULL;		/* function or script name */
    linenr_T	*breakpoint = NULL;	/* ptr to breakpoint field in cookie */
    int		*dbg_tick = NULL;	/* ptr to dbg_tick field in cookie */
    struct dbg_stuff debug_saved;	/* saved things for debug mode */
    int		initial_trylevel;
    struct msglist	**saved_msg_list = NULL;
    struct msglist	*private_msg_list;

    /* "fgetline" and "cookie" passed to do_one_cmd() */
    char_u	*(*cmd_getline)(int, void *, int, int);
    void	*cmd_cookie;
    struct loop_cookie cmd_loop_cookie;
    void	*real_cookie;
    int		getline_is_func;
#else
# define cmd_getline fgetline
# define cmd_cookie cookie
#endif
    static int	call_depth = 0;		/* recursiveness */

#ifdef FEAT_EVAL
    /* For every pair of do_cmdline()/do_one_cmd() calls, use an extra memory
     * location for storing error messages to be converted to an exception.
     * This ensures that the do_errthrow() call in do_one_cmd() does not
     * combine the messages stored by an earlier invocation of do_one_cmd()
     * with the command name of the later one.  This would happen when
     * BufWritePost autocommands are executed after a write error. */
    saved_msg_list = msg_list;
    msg_list = &private_msg_list;
    private_msg_list = NULL;
#endif

    /* It's possible to create an endless loop with ":execute", catch that
     * here.  The value of 200 allows nested function calls, ":source", etc.
     * Allow 200 or 'maxfuncdepth', whatever is larger. */
    if (call_depth >= 200
#ifdef FEAT_EVAL
	    && call_depth >= p_mfd
#endif
	    )
    {
	emsg(_("E169: Command too recursive"));
#ifdef FEAT_EVAL
	/* When converting to an exception, we do not include the command name
	 * since this is not an error of the specific command. */
	do_errthrow((struct condstack *)NULL, (char_u *)NULL);
	msg_list = saved_msg_list;
#endif
	return FAIL;
    }
    ++call_depth;

#ifdef FEAT_EVAL
    cstack.cs_idx = -1;
    cstack.cs_looplevel = 0;
    cstack.cs_trylevel = 0;
    cstack.cs_emsg_silent_list = NULL;
    cstack.cs_lflags = 0;
    ga_init2(&lines_ga, (int)sizeof(wcmd_T), 10);

    real_cookie = getline_cookie(fgetline, cookie);

    /* Inside a function use a higher nesting level. */
    getline_is_func = getline_equal(fgetline, cookie, get_func_line);
    if (getline_is_func && ex_nesting_level == func_level(real_cookie))
	++ex_nesting_level;

    /* Get the function or script name and the address where the next breakpoint
     * line and the debug tick for a function or script are stored. */
    if (getline_is_func)
    {
	fname = func_name(real_cookie);
	breakpoint = func_breakpoint(real_cookie);
	dbg_tick = func_dbg_tick(real_cookie);
    }
    else if (getline_equal(fgetline, cookie, getsourceline))
    {
	fname = sourcing_name;
	breakpoint = source_breakpoint(real_cookie);
	dbg_tick = source_dbg_tick(real_cookie);
    }

    /*
     * Initialize "force_abort"  and "suppress_errthrow" at the top level.
     */
    if (!recursive)
    {
	force_abort = FALSE;
	suppress_errthrow = FALSE;
    }

    /*
     * If requested, store and reset the global values controlling the
     * exception handling (used when debugging).  Otherwise clear it to avoid
     * a bogus compiler warning when the optimizer uses inline functions...
     */
    if (flags & DOCMD_EXCRESET)
	save_dbg_stuff(&debug_saved);
    else
	vim_memset(&debug_saved, 0, sizeof(debug_saved));

    initial_trylevel = trylevel;

    /*
     * "did_throw" will be set to TRUE when an exception is being thrown.
     */
    did_throw = FALSE;
#endif
    /*
     * "did_emsg" will be set to TRUE when emsg() is used, in which case we
     * cancel the whole command line, and any if/endif or loop.
     * If force_abort is set, we cancel everything.
     */
    did_emsg = FALSE;

    /*
     * KeyTyped is only set when calling vgetc().  Reset it here when not
     * calling vgetc() (sourced command lines).
     */
    if (!(flags & DOCMD_KEYTYPED)
			       && !getline_equal(fgetline, cookie, getexline))
	KeyTyped = FALSE;

    /*
     * Continue executing command lines:
     * - when inside an ":if", ":while" or ":for"
     * - for multiple commands on one line, separated with '|'
     * - when repeating until there are no more lines (for ":source")
     */
    next_cmdline = cmdline;
    do
    {
#ifdef FEAT_EVAL
	getline_is_func = getline_equal(fgetline, cookie, get_func_line);
#endif

	/* stop skipping cmds for an error msg after all endif/while/for */
	if (next_cmdline == NULL
#ifdef FEAT_EVAL
		&& !force_abort
		&& cstack.cs_idx < 0
		&& !(getline_is_func && func_has_abort(real_cookie))
#endif
							)
	    did_emsg = FALSE;

	/*
	 * 1. If repeating a line in a loop, get a line from lines_ga.
	 * 2. If no line given: Get an allocated line with fgetline().
	 * 3. If a line is given: Make a copy, so we can mess with it.
	 */

#ifdef FEAT_EVAL
	/* 1. If repeating, get a previous line from lines_ga. */
	if (cstack.cs_looplevel > 0 && current_line < lines_ga.ga_len)
	{
	    /* Each '|' separated command is stored separately in lines_ga, to
	     * be able to jump to it.  Don't use next_cmdline now. */
	    VIM_CLEAR(cmdline_copy);

	    /* Check if a function has returned or, unless it has an unclosed
	     * try conditional, aborted. */
	    if (getline_is_func)
	    {
# ifdef FEAT_PROFILE
		if (do_profiling == PROF_YES)
		    func_line_end(real_cookie);
# endif
		if (func_has_ended(real_cookie))
		{
		    retval = FAIL;
		    break;
		}
	    }
#ifdef FEAT_PROFILE
	    else if (do_profiling == PROF_YES
			    && getline_equal(fgetline, cookie, getsourceline))
		script_line_end();
#endif

	    /* Check if a sourced file hit a ":finish" command. */
	    if (source_finished(fgetline, cookie))
	    {
		retval = FAIL;
		break;
	    }

	    /* If breakpoints have been added/deleted need to check for it. */
	    if (breakpoint != NULL && dbg_tick != NULL
						   && *dbg_tick != debug_tick)
	    {
		*breakpoint = dbg_find_breakpoint(
				getline_equal(fgetline, cookie, getsourceline),
							fname, sourcing_lnum);
		*dbg_tick = debug_tick;
	    }

	    next_cmdline = ((wcmd_T *)(lines_ga.ga_data))[current_line].line;
	    sourcing_lnum = ((wcmd_T *)(lines_ga.ga_data))[current_line].lnum;

	    /* Did we encounter a breakpoint? */
	    if (breakpoint != NULL && *breakpoint != 0
					      && *breakpoint <= sourcing_lnum)
	    {
		dbg_breakpoint(fname, sourcing_lnum);
		/* Find next breakpoint. */
		*breakpoint = dbg_find_breakpoint(
			       getline_equal(fgetline, cookie, getsourceline),
							fname, sourcing_lnum);
		*dbg_tick = debug_tick;
	    }
# ifdef FEAT_PROFILE
	    if (do_profiling == PROF_YES)
	    {
		if (getline_is_func)
		    func_line_start(real_cookie);
		else if (getline_equal(fgetline, cookie, getsourceline))
		    script_line_start();
	    }
# endif
	}

	if (cstack.cs_looplevel > 0)
	{
	    /* Inside a while/for loop we need to store the lines and use them
	     * again.  Pass a different "fgetline" function to do_one_cmd()
	     * below, so that it stores lines in or reads them from
	     * "lines_ga".  Makes it possible to define a function inside a
	     * while/for loop. */
	    cmd_getline = get_loop_line;
	    cmd_cookie = (void *)&cmd_loop_cookie;
	    cmd_loop_cookie.lines_gap = &lines_ga;
	    cmd_loop_cookie.current_line = current_line;
	    cmd_loop_cookie.getline = fgetline;
	    cmd_loop_cookie.cookie = cookie;
	    cmd_loop_cookie.repeating = (current_line < lines_ga.ga_len);
	}
	else
	{
	    cmd_getline = fgetline;
	    cmd_cookie = cookie;
	}
#endif

	/* 2. If no line given, get an allocated line with fgetline(). */
	if (next_cmdline == NULL)
	{
	    /*
	     * Need to set msg_didout for the first line after an ":if",
	     * otherwise the ":if" will be overwritten.
	     */
	    if (count == 1 && getline_equal(fgetline, cookie, getexline))
		msg_didout = TRUE;
	    if (fgetline == NULL || (next_cmdline = fgetline(':', cookie,
#ifdef FEAT_EVAL
		    cstack.cs_idx < 0 ? 0 : (cstack.cs_idx + 1) * 2
#else
		    0
#endif
		    , TRUE)) == NULL)
	    {
		/* Don't call wait_return for aborted command line.  The NULL
		 * returned for the end of a sourced file or executed function
		 * doesn't do this. */
		if (KeyTyped && !(flags & DOCMD_REPEAT))
		    need_wait_return = FALSE;
		retval = FAIL;
		break;
	    }
	    used_getline = TRUE;

	    /*
	     * Keep the first typed line.  Clear it when more lines are typed.
	     */
	    if (flags & DOCMD_KEEPLINE)
	    {
		vim_free(repeat_cmdline);
		if (count == 0)
		    repeat_cmdline = vim_strsave(next_cmdline);
		else
		    repeat_cmdline = NULL;
	    }
	}

	/* 3. Make a copy of the command so we can mess with it. */
	else if (cmdline_copy == NULL)
	{
	    next_cmdline = vim_strsave(next_cmdline);
	    if (next_cmdline == NULL)
	    {
		emsg(_(e_outofmem));
		retval = FAIL;
		break;
	    }
	}
	cmdline_copy = next_cmdline;

#ifdef FEAT_EVAL
	/*
	 * Save the current line when inside a ":while" or ":for", and when
	 * the command looks like a ":while" or ":for", because we may need it
	 * later.  When there is a '|' and another command, it is stored
	 * separately, because we need to be able to jump back to it from an
	 * :endwhile/:endfor.
	 */
	if (current_line == lines_ga.ga_len
		&& (cstack.cs_looplevel || has_loop_cmd(next_cmdline)))
	{
	    if (store_loop_line(&lines_ga, next_cmdline) == FAIL)
	    {
		retval = FAIL;
		break;
	    }
	}
	did_endif = FALSE;
#endif

	if (count++ == 0)
	{
	    /*
	     * All output from the commands is put below each other, without
	     * waiting for a return. Don't do this when executing commands
	     * from a script or when being called recursive (e.g. for ":e
	     * +command file").
	     */
	    if (!(flags & DOCMD_NOWAIT) && !recursive)
	    {
		msg_didout_before_start = msg_didout;
		msg_didany = FALSE; /* no output yet */
		msg_start();
		msg_scroll = TRUE;  /* put messages below each other */
		++no_wait_return;   /* don't wait for return until finished */
		++RedrawingDisabled;
		did_inc = TRUE;
	    }
	}

	if (p_verbose >= 15 && sourcing_name != NULL)
	    msg_verbose_cmd(sourcing_lnum, cmdline_copy);

	/*
	 * 2. Execute one '|' separated command.
	 *    do_one_cmd() will return NULL if there is no trailing '|'.
	 *    "cmdline_copy" can change, e.g. for '%' and '#' expansion.
	 */
	++recursive;
	next_cmdline = do_one_cmd(&cmdline_copy, flags & DOCMD_VERBOSE,
#ifdef FEAT_EVAL
				&cstack,
#endif
				cmd_getline, cmd_cookie);
	--recursive;

#ifdef FEAT_EVAL
	if (cmd_cookie == (void *)&cmd_loop_cookie)
	    /* Use "current_line" from "cmd_loop_cookie", it may have been
	     * incremented when defining a function. */
	    current_line = cmd_loop_cookie.current_line;
#endif

	if (next_cmdline == NULL)
	{
	    VIM_CLEAR(cmdline_copy);

	    /*
	     * If the command was typed, remember it for the ':' register.
	     * Do this AFTER executing the command to make :@: work.
	     */
	    if (getline_equal(fgetline, cookie, getexline)
						  && new_last_cmdline != NULL)
	    {
		vim_free(last_cmdline);
		last_cmdline = new_last_cmdline;
		new_last_cmdline = NULL;
	    }
	}
	else
	{
	    /* need to copy the command after the '|' to cmdline_copy, for the
	     * next do_one_cmd() */
	    STRMOVE(cmdline_copy, next_cmdline);
	    next_cmdline = cmdline_copy;
	}


#ifdef FEAT_EVAL
	/* reset did_emsg for a function that is not aborted by an error */
	if (did_emsg && !force_abort
		&& getline_equal(fgetline, cookie, get_func_line)
					      && !func_has_abort(real_cookie))
	    did_emsg = FALSE;

	if (cstack.cs_looplevel > 0)
	{
	    ++current_line;

	    /*
	     * An ":endwhile", ":endfor" and ":continue" is handled here.
	     * If we were executing commands, jump back to the ":while" or
	     * ":for".
	     * If we were not executing commands, decrement cs_looplevel.
	     */
	    if (cstack.cs_lflags & (CSL_HAD_CONT | CSL_HAD_ENDLOOP))
	    {
		cstack.cs_lflags &= ~(CSL_HAD_CONT | CSL_HAD_ENDLOOP);

		/* Jump back to the matching ":while" or ":for".  Be careful
		 * not to use a cs_line[] from an entry that isn't a ":while"
		 * or ":for": It would make "current_line" invalid and can
		 * cause a crash. */
		if (!did_emsg && !got_int && !did_throw
			&& cstack.cs_idx >= 0
			&& (cstack.cs_flags[cstack.cs_idx]
						      & (CSF_WHILE | CSF_FOR))
			&& cstack.cs_line[cstack.cs_idx] >= 0
			&& (cstack.cs_flags[cstack.cs_idx] & CSF_ACTIVE))
		{
		    current_line = cstack.cs_line[cstack.cs_idx];
						/* remember we jumped there */
		    cstack.cs_lflags |= CSL_HAD_LOOP;
		    line_breakcheck();		/* check if CTRL-C typed */

		    /* Check for the next breakpoint at or after the ":while"
		     * or ":for". */
		    if (breakpoint != NULL)
		    {
			*breakpoint = dbg_find_breakpoint(
			       getline_equal(fgetline, cookie, getsourceline),
									fname,
			   ((wcmd_T *)lines_ga.ga_data)[current_line].lnum-1);
			*dbg_tick = debug_tick;
		    }
		}
		else
		{
		    /* can only get here with ":endwhile" or ":endfor" */
		    if (cstack.cs_idx >= 0)
			rewind_conditionals(&cstack, cstack.cs_idx - 1,
				   CSF_WHILE | CSF_FOR, &cstack.cs_looplevel);
		}
	    }

	    /*
	     * For a ":while" or ":for" we need to remember the line number.
	     */
	    else if (cstack.cs_lflags & CSL_HAD_LOOP)
	    {
		cstack.cs_lflags &= ~CSL_HAD_LOOP;
		cstack.cs_line[cstack.cs_idx] = current_line - 1;
	    }
	}

	/* Check for the next breakpoint after a watchexpression */
	if (breakpoint != NULL && has_watchexpr())
	{
	    *breakpoint = dbg_find_breakpoint(FALSE, fname, sourcing_lnum);
	    *dbg_tick = debug_tick;
	}

	/*
	 * When not inside any ":while" loop, clear remembered lines.
	 */
	if (cstack.cs_looplevel == 0)
	{
	    if (lines_ga.ga_len > 0)
	    {
		sourcing_lnum =
		       ((wcmd_T *)lines_ga.ga_data)[lines_ga.ga_len - 1].lnum;
		free_cmdlines(&lines_ga);
	    }
	    current_line = 0;
	}

	/*
	 * A ":finally" makes did_emsg, got_int, and did_throw pending for
	 * being restored at the ":endtry".  Reset them here and set the
	 * ACTIVE and FINALLY flags, so that the finally clause gets executed.
	 * This includes the case where a missing ":endif", ":endwhile" or
	 * ":endfor" was detected by the ":finally" itself.
	 */
	if (cstack.cs_lflags & CSL_HAD_FINA)
	{
	    cstack.cs_lflags &= ~CSL_HAD_FINA;
	    report_make_pending(cstack.cs_pending[cstack.cs_idx]
		    & (CSTP_ERROR | CSTP_INTERRUPT | CSTP_THROW),
		    did_throw ? (void *)current_exception : NULL);
	    did_emsg = got_int = did_throw = FALSE;
	    cstack.cs_flags[cstack.cs_idx] |= CSF_ACTIVE | CSF_FINALLY;
	}

	/* Update global "trylevel" for recursive calls to do_cmdline() from
	 * within this loop. */
	trylevel = initial_trylevel + cstack.cs_trylevel;

	/*
	 * If the outermost try conditional (across function calls and sourced
	 * files) is aborted because of an error, an interrupt, or an uncaught
	 * exception, cancel everything.  If it is left normally, reset
	 * force_abort to get the non-EH compatible abortion behavior for
	 * the rest of the script.
	 */
	if (trylevel == 0 && !did_emsg && !got_int && !did_throw)
	    force_abort = FALSE;

	/* Convert an interrupt to an exception if appropriate. */
	(void)do_intthrow(&cstack);
#endif /* FEAT_EVAL */

    }
    /*
     * Continue executing command lines when:
     * - no CTRL-C typed, no aborting error, no exception thrown or try
     *   conditionals need to be checked for executing finally clauses or
     *   catching an interrupt exception
     * - didn't get an error message or lines are not typed
     * - there is a command after '|', inside a :if, :while, :for or :try, or
     *   looping for ":source" command or function call.
     */
    while (!((got_int
#ifdef FEAT_EVAL
		    || (did_emsg && force_abort) || did_throw
#endif
	     )
#ifdef FEAT_EVAL
		&& cstack.cs_trylevel == 0
#endif
	    )
	    && !(did_emsg
#ifdef FEAT_EVAL
		/* Keep going when inside try/catch, so that the error can be
		 * deal with, except when it is a syntax error, it may cause
		 * the :endtry to be missed. */
		&& (cstack.cs_trylevel == 0 || did_emsg_syntax)
#endif
		&& used_getline
			    && (getline_equal(fgetline, cookie, getexmodeline)
			       || getline_equal(fgetline, cookie, getexline)))
	    && (next_cmdline != NULL
#ifdef FEAT_EVAL
			|| cstack.cs_idx >= 0
#endif
			|| (flags & DOCMD_REPEAT)));

    vim_free(cmdline_copy);
    did_emsg_syntax = FALSE;
#ifdef FEAT_EVAL
    free_cmdlines(&lines_ga);
    ga_clear(&lines_ga);

    if (cstack.cs_idx >= 0)
    {
	/*
	 * If a sourced file or executed function ran to its end, report the
	 * unclosed conditional.
	 */
	if (!got_int && !did_throw
		&& ((getline_equal(fgetline, cookie, getsourceline)
			&& !source_finished(fgetline, cookie))
		    || (getline_equal(fgetline, cookie, get_func_line)
					    && !func_has_ended(real_cookie))))
	{
	    if (cstack.cs_flags[cstack.cs_idx] & CSF_TRY)
		emsg(_(e_endtry));
	    else if (cstack.cs_flags[cstack.cs_idx] & CSF_WHILE)
		emsg(_(e_endwhile));
	    else if (cstack.cs_flags[cstack.cs_idx] & CSF_FOR)
		emsg(_(e_endfor));
	    else
		emsg(_(e_endif));
	}

	/*
	 * Reset "trylevel" in case of a ":finish" or ":return" or a missing
	 * ":endtry" in a sourced file or executed function.  If the try
	 * conditional is in its finally clause, ignore anything pending.
	 * If it is in a catch clause, finish the caught exception.
	 * Also cleanup any "cs_forinfo" structures.
	 */
	do
	{
	    int idx = cleanup_conditionals(&cstack, 0, TRUE);

	    if (idx >= 0)
		--idx;	    /* remove try block not in its finally clause */
	    rewind_conditionals(&cstack, idx, CSF_WHILE | CSF_FOR,
							&cstack.cs_looplevel);
	}
	while (cstack.cs_idx >= 0);
	trylevel = initial_trylevel;
    }

    /* If a missing ":endtry", ":endwhile", ":endfor", or ":endif" or a memory
     * lack was reported above and the error message is to be converted to an
     * exception, do this now after rewinding the cstack. */
    do_errthrow(&cstack, getline_equal(fgetline, cookie, get_func_line)
				  ? (char_u *)"endfunction" : (char_u *)NULL);

    if (trylevel == 0)
    {
	/*
	 * When an exception is being thrown out of the outermost try
	 * conditional, discard the uncaught exception, disable the conversion
	 * of interrupts or errors to exceptions, and ensure that no more
	 * commands are executed.
	 */
	if (did_throw)
	{
	    void	*p = NULL;
	    char_u	*saved_sourcing_name;
	    int		saved_sourcing_lnum;
	    struct msglist	*messages = NULL, *next;

	    /*
	     * If the uncaught exception is a user exception, report it as an
	     * error.  If it is an error exception, display the saved error
	     * message now.  For an interrupt exception, do nothing; the
	     * interrupt message is given elsewhere.
	     */
	    switch (current_exception->type)
	    {
		case ET_USER:
		    vim_snprintf((char *)IObuff, IOSIZE,
			    _("E605: Exception not caught: %s"),
			    current_exception->value);
		    p = vim_strsave(IObuff);
		    break;
		case ET_ERROR:
		    messages = current_exception->messages;
		    current_exception->messages = NULL;
		    break;
		case ET_INTERRUPT:
		    break;
	    }

	    saved_sourcing_name = sourcing_name;
	    saved_sourcing_lnum = sourcing_lnum;
	    sourcing_name = current_exception->throw_name;
	    sourcing_lnum = current_exception->throw_lnum;
	    current_exception->throw_name = NULL;

	    discard_current_exception();	/* uses IObuff if 'verbose' */
	    suppress_errthrow = TRUE;
	    force_abort = TRUE;

	    if (messages != NULL)
	    {
		do
		{
		    next = messages->next;
		    emsg(messages->msg);
		    vim_free(messages->msg);
		    vim_free(messages);
		    messages = next;
		}
		while (messages != NULL);
	    }
	    else if (p != NULL)
	    {
		emsg(p);
		vim_free(p);
	    }
	    vim_free(sourcing_name);
	    sourcing_name = saved_sourcing_name;
	    sourcing_lnum = saved_sourcing_lnum;
	}

	/*
	 * On an interrupt or an aborting error not converted to an exception,
	 * disable the conversion of errors to exceptions.  (Interrupts are not
	 * converted any more, here.) This enables also the interrupt message
	 * when force_abort is set and did_emsg unset in case of an interrupt
	 * from a finally clause after an error.
	 */
	else if (got_int || (did_emsg && force_abort))
	    suppress_errthrow = TRUE;
    }

    /*
     * The current cstack will be freed when do_cmdline() returns.  An uncaught
     * exception will have to be rethrown in the previous cstack.  If a function
     * has just returned or a script file was just finished and the previous
     * cstack belongs to the same function or, respectively, script file, it
     * will have to be checked for finally clauses to be executed due to the
     * ":return" or ":finish".  This is done in do_one_cmd().
     */
    if (did_throw)
	need_rethrow = TRUE;
    if ((getline_equal(fgetline, cookie, getsourceline)
		&& ex_nesting_level > source_level(real_cookie))
	    || (getline_equal(fgetline, cookie, get_func_line)
		&& ex_nesting_level > func_level(real_cookie) + 1))
    {
	if (!did_throw)
	    check_cstack = TRUE;
    }
    else
    {
	/* When leaving a function, reduce nesting level. */
	if (getline_equal(fgetline, cookie, get_func_line))
	    --ex_nesting_level;
	/*
	 * Go to debug mode when returning from a function in which we are
	 * single-stepping.
	 */
	if ((getline_equal(fgetline, cookie, getsourceline)
		    || getline_equal(fgetline, cookie, get_func_line))
		&& ex_nesting_level + 1 <= debug_break_level)
	    do_debug(getline_equal(fgetline, cookie, getsourceline)
		    ? (char_u *)_("End of sourced file")
		    : (char_u *)_("End of function"));
    }

    /*
     * Restore the exception environment (done after returning from the
     * debugger).
     */
    if (flags & DOCMD_EXCRESET)
	restore_dbg_stuff(&debug_saved);

    msg_list = saved_msg_list;
#endif /* FEAT_EVAL */

    /*
     * If there was too much output to fit on the command line, ask the user to
     * hit return before redrawing the screen. With the ":global" command we do
     * this only once after the command is finished.
     */
    if (did_inc)
    {
	--RedrawingDisabled;
	--no_wait_return;
	msg_scroll = FALSE;

	/*
	 * When just finished an ":if"-":else" which was typed, no need to
	 * wait for hit-return.  Also for an error situation.
	 */
	if (retval == FAIL
#ifdef FEAT_EVAL
		|| (did_endif && KeyTyped && !did_emsg)
#endif
					    )
	{
	    need_wait_return = FALSE;
	    msg_didany = FALSE;		/* don't wait when restarting edit */
	}
	else if (need_wait_return)
	{
	    /*
	     * The msg_start() above clears msg_didout. The wait_return we do
	     * here should not overwrite the command that may be shown before
	     * doing that.
	     */
	    msg_didout |= msg_didout_before_start;
	    wait_return(FALSE);
	}
    }

#ifdef FEAT_EVAL
    did_endif = FALSE;  /* in case do_cmdline used recursively */
#else
    /*
     * Reset if_level, in case a sourced script file contains more ":if" than
     * ":endif" (could be ":if x | foo | endif").
     */
    if_level = 0;
#endif

    --call_depth;
    return retval;
}

#ifdef FEAT_EVAL
/*
 * Obtain a line when inside a ":while" or ":for" loop.
 */
    static char_u *
get_loop_line(int c, void *cookie, int indent, int do_concat)
{
    struct loop_cookie	*cp = (struct loop_cookie *)cookie;
    wcmd_T		*wp;
    char_u		*line;

    if (cp->current_line + 1 >= cp->lines_gap->ga_len)
    {
	if (cp->repeating)
	    return NULL;	/* trying to read past ":endwhile"/":endfor" */

	/* First time inside the ":while"/":for": get line normally. */
	if (cp->getline == NULL)
	    line = getcmdline(c, 0L, indent, do_concat);
	else
	    line = cp->getline(c, cp->cookie, indent, do_concat);
	if (line != NULL && store_loop_line(cp->lines_gap, line) == OK)
	    ++cp->current_line;

	return line;
    }

    KeyTyped = FALSE;
    ++cp->current_line;
    wp = (wcmd_T *)(cp->lines_gap->ga_data) + cp->current_line;
    sourcing_lnum = wp->lnum;
    return vim_strsave(wp->line);
}

/*
 * Store a line in "gap" so that a ":while" loop can execute it again.
 */
    static int
store_loop_line(garray_T *gap, char_u *line)
{
    if (ga_grow(gap, 1) == FAIL)
	return FAIL;
    ((wcmd_T *)(gap->ga_data))[gap->ga_len].line = vim_strsave(line);
    ((wcmd_T *)(gap->ga_data))[gap->ga_len].lnum = sourcing_lnum;
    ++gap->ga_len;
    return OK;
}

/*
 * Free the lines stored for a ":while" or ":for" loop.
 */
    static void
free_cmdlines(garray_T *gap)
{
    while (gap->ga_len > 0)
    {
	vim_free(((wcmd_T *)(gap->ga_data))[gap->ga_len - 1].line);
	--gap->ga_len;
    }
}
#endif

/*
 * If "fgetline" is get_loop_line(), return TRUE if the getline it uses equals
 * "func".  * Otherwise return TRUE when "fgetline" equals "func".
 */
    int
getline_equal(
    char_u	*(*fgetline)(int, void *, int, int),
    void	*cookie UNUSED,		/* argument for fgetline() */
    char_u	*(*func)(int, void *, int, int))
{
#ifdef FEAT_EVAL
    char_u		*(*gp)(int, void *, int, int);
    struct loop_cookie *cp;

    /* When "fgetline" is "get_loop_line()" use the "cookie" to find the
     * function that's originally used to obtain the lines.  This may be
     * nested several levels. */
    gp = fgetline;
    cp = (struct loop_cookie *)cookie;
    while (gp == get_loop_line)
    {
	gp = cp->getline;
	cp = cp->cookie;
    }
    return gp == func;
#else
    return fgetline == func;
#endif
}

/*
 * If "fgetline" is get_loop_line(), return the cookie used by the original
 * getline function.  Otherwise return "cookie".
 */
    void *
getline_cookie(
    char_u	*(*fgetline)(int, void *, int, int) UNUSED,
    void	*cookie)		/* argument for fgetline() */
{
#ifdef FEAT_EVAL
    char_u		*(*gp)(int, void *, int, int);
    struct loop_cookie *cp;

    /* When "fgetline" is "get_loop_line()" use the "cookie" to find the
     * cookie that's originally used to obtain the lines.  This may be nested
     * several levels. */
    gp = fgetline;
    cp = (struct loop_cookie *)cookie;
    while (gp == get_loop_line)
    {
	gp = cp->getline;
	cp = cp->cookie;
    }
    return cp;
#else
    return cookie;
#endif
}


/*
 * Helper function to apply an offset for buffer commands, i.e. ":bdelete",
 * ":bwipeout", etc.
 * Returns the buffer number.
 */
    static int
compute_buffer_local_count(int addr_type, int lnum, int offset)
{
    buf_T   *buf;
    buf_T   *nextbuf;
    int     count = offset;

    buf = firstbuf;
    while (buf->b_next != NULL && buf->b_fnum < lnum)
	buf = buf->b_next;
    while (count != 0)
    {
	count += (offset < 0) ? 1 : -1;
	nextbuf = (offset < 0) ? buf->b_prev : buf->b_next;
	if (nextbuf == NULL)
	    break;
	buf = nextbuf;
	if (addr_type == ADDR_LOADED_BUFFERS)
	    /* skip over unloaded buffers */
	    while (buf->b_ml.ml_mfp == NULL)
	    {
		nextbuf = (offset < 0) ? buf->b_prev : buf->b_next;
		if (nextbuf == NULL)
		    break;
		buf = nextbuf;
	    }
    }
    /* we might have gone too far, last buffer is not loadedd */
    if (addr_type == ADDR_LOADED_BUFFERS)
	while (buf->b_ml.ml_mfp == NULL)
	{
	    nextbuf = (offset >= 0) ? buf->b_prev : buf->b_next;
	    if (nextbuf == NULL)
		break;
	    buf = nextbuf;
	}
    return buf->b_fnum;
}

/*
 * Return the window number of "win".
 * When "win" is NULL return the number of windows.
 */
    static int
current_win_nr(win_T *win)
{
    win_T	*wp;
    int		nr = 0;

    FOR_ALL_WINDOWS(wp)
    {
	++nr;
	if (wp == win)
	    break;
    }
    return nr;
}

    static int
current_tab_nr(tabpage_T *tab)
{
    tabpage_T	*tp;
    int		nr = 0;

    FOR_ALL_TABPAGES(tp)
    {
	++nr;
	if (tp == tab)
	    break;
    }
    return nr;
}

# define CURRENT_WIN_NR current_win_nr(curwin)
# define LAST_WIN_NR current_win_nr(NULL)
# define CURRENT_TAB_NR current_tab_nr(curtab)
# define LAST_TAB_NR current_tab_nr(NULL)

/*
 * Execute one Ex command.
 *
 * If 'sourcing' is TRUE, the command will be included in the error message.
 *
 * 1. skip comment lines and leading space
 * 2. handle command modifiers
 * 3. find the command
 * 4. parse range
 * 5. Parse the command.
 * 6. parse arguments
 * 7. switch on command name
 *
 * Note: "fgetline" can be NULL.
 *
 * This function may be called recursively!
 */
#if (_MSC_VER == 1200)
/*
 * Avoid optimisation bug in VC++ version 6.0
 */
 #pragma optimize( "g", off )
#endif
    static char_u *
do_one_cmd(
    char_u		**cmdlinep,
    int			sourcing,
#ifdef FEAT_EVAL
    struct condstack	*cstack,
#endif
    char_u		*(*fgetline)(int, void *, int, int),
    void		*cookie)		/* argument for fgetline() */
{
    char_u		*p;
    linenr_T		lnum;
    long		n;
    char		*errormsg = NULL;	/* error message */
    char_u		*after_modifier = NULL;
    exarg_T		ea;			/* Ex command arguments */
    int			save_msg_scroll = msg_scroll;
    cmdmod_T		save_cmdmod;
    int			save_reg_executing = reg_executing;
    int			ni;			/* set when Not Implemented */
    char_u		*cmd;

    vim_memset(&ea, 0, sizeof(ea));
    ea.line1 = 1;
    ea.line2 = 1;
#ifdef FEAT_EVAL
    ++ex_nesting_level;
#endif

    /* When the last file has not been edited :q has to be typed twice. */
    if (quitmore
#ifdef FEAT_EVAL
	    /* avoid that a function call in 'statusline' does this */
	    && !getline_equal(fgetline, cookie, get_func_line)
#endif
	    /* avoid that an autocommand, e.g. QuitPre, does this */
	    && !getline_equal(fgetline, cookie, getnextac))
	--quitmore;

    /*
     * Reset browse, confirm, etc..  They are restored when returning, for
     * recursive calls.
     */
    save_cmdmod = cmdmod;

    /* "#!anything" is handled like a comment. */
    if ((*cmdlinep)[0] == '#' && (*cmdlinep)[1] == '!')
	goto doend;

    if (p_verbose >= 16)
	msg_verbose_cmd(0, *cmdlinep);

/*
 * 1. Skip comment lines and leading white space and colons.
 * 2. Handle command modifiers.
 */
    // The "ea" structure holds the arguments that can be used.
    ea.cmd = *cmdlinep;
    ea.cmdlinep = cmdlinep;
    ea.getline = fgetline;
    ea.cookie = cookie;
#ifdef FEAT_EVAL
    ea.cstack = cstack;
#endif
    if (parse_command_modifiers(&ea, &errormsg, FALSE) == FAIL)
	goto doend;

    after_modifier = ea.cmd;

#ifdef FEAT_EVAL
    ea.skip = did_emsg || got_int || did_throw || (cstack->cs_idx >= 0
			 && !(cstack->cs_flags[cstack->cs_idx] & CSF_ACTIVE));
#else
    ea.skip = (if_level > 0);
#endif

/*
 * 3. Skip over the range to find the command.  Let "p" point to after it.
 *
 * We need the command to know what kind of range it uses.
 */
    cmd = ea.cmd;
    ea.cmd = skip_range(ea.cmd, NULL);
    if (*ea.cmd == '*' && vim_strchr(p_cpo, CPO_STAR) == NULL)
	ea.cmd = skipwhite(ea.cmd + 1);
    p = find_command(&ea, NULL);

#ifdef FEAT_EVAL
# ifdef FEAT_PROFILE
    // Count this line for profiling if skip is TRUE.
    if (do_profiling == PROF_YES
	    && (!ea.skip || cstack->cs_idx == 0 || (cstack->cs_idx > 0
		     && (cstack->cs_flags[cstack->cs_idx - 1] & CSF_ACTIVE))))
    {
	int skip = did_emsg || got_int || did_throw;

	if (ea.cmdidx == CMD_catch)
	    skip = !skip && !(cstack->cs_idx >= 0
			  && (cstack->cs_flags[cstack->cs_idx] & CSF_THROWN)
			  && !(cstack->cs_flags[cstack->cs_idx] & CSF_CAUGHT));
	else if (ea.cmdidx == CMD_else || ea.cmdidx == CMD_elseif)
	    skip = skip || !(cstack->cs_idx >= 0
			  && !(cstack->cs_flags[cstack->cs_idx]
						  & (CSF_ACTIVE | CSF_TRUE)));
	else if (ea.cmdidx == CMD_finally)
	    skip = FALSE;
	else if (ea.cmdidx != CMD_endif
		&& ea.cmdidx != CMD_endfor
		&& ea.cmdidx != CMD_endtry
		&& ea.cmdidx != CMD_endwhile)
	    skip = ea.skip;

	if (!skip)
	{
	    if (getline_equal(fgetline, cookie, get_func_line))
		func_line_exec(getline_cookie(fgetline, cookie));
	    else if (getline_equal(fgetline, cookie, getsourceline))
		script_line_exec();
	}
    }
# endif

    /* May go to debug mode.  If this happens and the ">quit" debug command is
     * used, throw an interrupt exception and skip the next command. */
    dbg_check_breakpoint(&ea);
    if (!ea.skip && got_int)
    {
	ea.skip = TRUE;
	(void)do_intthrow(cstack);
    }
#endif

/*
 * 4. parse a range specifier of the form: addr [,addr] [;addr] ..
 *
 * where 'addr' is:
 *
 * %	      (entire file)
 * $  [+-NUM]
 * 'x [+-NUM] (where x denotes a currently defined mark)
 * .  [+-NUM]
 * [+-NUM]..
 * NUM
 *
 * The ea.cmd pointer is updated to point to the first character following the
 * range spec. If an initial address is found, but no second, the upper bound
 * is equal to the lower.
 */

    // ea.addr_type for user commands is set by find_ucmd
    if (!IS_USER_CMDIDX(ea.cmdidx))
    {
	if (ea.cmdidx != CMD_SIZE)
	    ea.addr_type = cmdnames[(int)ea.cmdidx].cmd_addr_type;
	else
	    ea.addr_type = ADDR_LINES;

	// :wincmd range depends on the argument.
	if (ea.cmdidx == CMD_wincmd && p != NULL)
	    get_wincmd_addr_type(skipwhite(p), &ea);
#ifdef FEAT_QUICKFIX
	// :.cc in quickfix window uses line number
	if ((ea.cmdidx == CMD_cc || ea.cmdidx == CMD_ll) && bt_quickfix(curbuf))
	    ea.addr_type = ADDR_OTHER;
#endif
    }

    ea.cmd = cmd;
    if (parse_cmd_address(&ea, &errormsg, FALSE) == FAIL)
	goto doend;

/*
 * 5. Parse the command.
 */

    /*
     * Skip ':' and any white space
     */
    ea.cmd = skipwhite(ea.cmd);
    while (*ea.cmd == ':')
	ea.cmd = skipwhite(ea.cmd + 1);

    /*
     * If we got a line, but no command, then go to the line.
     * If we find a '|' or '\n' we set ea.nextcmd.
     */
    if (*ea.cmd == NUL || *ea.cmd == '"'
			      || (ea.nextcmd = check_nextcmd(ea.cmd)) != NULL)
    {
	/*
	 * strange vi behaviour:
	 * ":3"		jumps to line 3
	 * ":3|..."	prints line 3
	 * ":|"		prints current line
	 */
	if (ea.skip)	    /* skip this if inside :if */
	    goto doend;
	if (*ea.cmd == '|' || (exmode_active && ea.line1 != ea.line2))
	{
	    ea.cmdidx = CMD_print;
	    ea.argt = EX_RANGE+EX_COUNT+EX_TRLBAR;
	    if ((errormsg = invalid_range(&ea)) == NULL)
	    {
		correct_range(&ea);
		ex_print(&ea);
	    }
	}
	else if (ea.addr_count != 0)
	{
	    if (ea.line2 > curbuf->b_ml.ml_line_count)
	    {
		/* With '-' in 'cpoptions' a line number past the file is an
		 * error, otherwise put it at the end of the file. */
		if (vim_strchr(p_cpo, CPO_MINUS) != NULL)
		    ea.line2 = -1;
		else
		    ea.line2 = curbuf->b_ml.ml_line_count;
	    }

	    if (ea.line2 < 0)
		errormsg = _(e_invrange);
	    else
	    {
		if (ea.line2 == 0)
		    curwin->w_cursor.lnum = 1;
		else
		    curwin->w_cursor.lnum = ea.line2;
		beginline(BL_SOL | BL_FIX);
	    }
	}
	goto doend;
    }

    /* If this looks like an undefined user command and there are CmdUndefined
     * autocommands defined, trigger the matching autocommands. */
    if (p != NULL && ea.cmdidx == CMD_SIZE && !ea.skip
	    && ASCII_ISUPPER(*ea.cmd)
	    && has_cmdundefined())
    {
	int ret;

	p = ea.cmd;
	while (ASCII_ISALNUM(*p))
	    ++p;
	p = vim_strnsave(ea.cmd, (int)(p - ea.cmd));
	ret = apply_autocmds(EVENT_CMDUNDEFINED, p, p, TRUE, NULL);
	vim_free(p);
	/* If the autocommands did something and didn't cause an error, try
	 * finding the command again. */
	p = (ret
#ifdef FEAT_EVAL
		&& !aborting()
#endif
		) ? find_command(&ea, NULL) : ea.cmd;
    }

    if (p == NULL)
    {
	if (!ea.skip)
	    errormsg = _("E464: Ambiguous use of user-defined command");
	goto doend;
    }
    // Check for wrong commands.
    if (*p == '!' && ea.cmd[1] == 0151 && ea.cmd[0] == 78
	    && !IS_USER_CMDIDX(ea.cmdidx))
    {
	errormsg = uc_fun_cmd();
	goto doend;
    }

    if (ea.cmdidx == CMD_SIZE)
    {
	if (!ea.skip)
	{
	    STRCPY(IObuff, _("E492: Not an editor command"));
	    if (!sourcing)
	    {
		/* If the modifier was parsed OK the error must be in the
		 * following command */
		if (after_modifier != NULL)
		    append_command(after_modifier);
		else
		    append_command(*cmdlinep);
	    }
	    errormsg = (char *)IObuff;
	    did_emsg_syntax = TRUE;
	}
	goto doend;
    }

    ni = (!IS_USER_CMDIDX(ea.cmdidx)
	    && (cmdnames[ea.cmdidx].cmd_func == ex_ni
#ifdef HAVE_EX_SCRIPT_NI
	     || cmdnames[ea.cmdidx].cmd_func == ex_script_ni
#endif
	     ));

#ifndef FEAT_EVAL
    /*
     * When the expression evaluation is disabled, recognize the ":if" and
     * ":endif" commands and ignore everything in between it.
     */
    if (ea.cmdidx == CMD_if)
	++if_level;
    if (if_level)
    {
	if (ea.cmdidx == CMD_endif)
	    --if_level;
	goto doend;
    }

#endif

    /* forced commands */
    if (*p == '!' && ea.cmdidx != CMD_substitute
	    && ea.cmdidx != CMD_smagic && ea.cmdidx != CMD_snomagic)
    {
	++p;
	ea.forceit = TRUE;
    }
    else
	ea.forceit = FALSE;

/*
 * 6. Parse arguments.  Then check for errors.
 */
    if (!IS_USER_CMDIDX(ea.cmdidx))
	ea.argt = (long)cmdnames[(int)ea.cmdidx].cmd_argt;

    if (!ea.skip)
    {
#ifdef HAVE_SANDBOX
	if (sandbox != 0 && !(ea.argt & EX_SBOXOK))
	{
	    // Command not allowed in sandbox.
	    errormsg = _(e_sandbox);
	    goto doend;
	}
#endif
	if (restricted != 0 && (ea.argt & EX_RESTRICT))
	{
	    errormsg = _("E981: Command not allowed in rvim");
	    goto doend;
	}
	if (!curbuf->b_p_ma && (ea.argt & EX_MODIFY))
	{
	    /* Command not allowed in non-'modifiable' buffer */
	    errormsg = _(e_modifiable);
	    goto doend;
	}

	if (text_locked() && !(ea.argt & EX_CMDWIN)
		&& !IS_USER_CMDIDX(ea.cmdidx))
	{
	    /* Command not allowed when editing the command line. */
	    errormsg = _(get_text_locked_msg());
	    goto doend;
	}

	/* Disallow editing another buffer when "curbuf_lock" is set.
	 * Do allow ":checktime" (it is postponed).
	 * Do allow ":edit" (check for an argument later).
	 * Do allow ":file" with no arguments (check for an argument later). */
	if (!(ea.argt & EX_CMDWIN)
		&& ea.cmdidx != CMD_checktime
		&& ea.cmdidx != CMD_edit
		&& ea.cmdidx != CMD_file
		&& !IS_USER_CMDIDX(ea.cmdidx)
		&& curbuf_locked())
	    goto doend;

	if (!ni && !(ea.argt & EX_RANGE) && ea.addr_count > 0)
	{
	    /* no range allowed */
	    errormsg = _(e_norange);
	    goto doend;
	}
    }

    if (!ni && !(ea.argt & EX_BANG) && ea.forceit)	// no <!> allowed
    {
	errormsg = _(e_nobang);
	goto doend;
    }

    /*
     * Don't complain about the range if it is not used
     * (could happen if line_count is accidentally set to 0).
     */
    if (!ea.skip && !ni && (ea.argt & EX_RANGE))
    {
	/*
	 * If the range is backwards, ask for confirmation and, if given, swap
	 * ea.line1 & ea.line2 so it's forwards again.
	 * When global command is busy, don't ask, will fail below.
	 */
	if (!global_busy && ea.line1 > ea.line2)
	{
	    if (msg_silent == 0)
	    {
		if (sourcing || exmode_active)
		{
		    errormsg = _("E493: Backwards range given");
		    goto doend;
		}
		if (ask_yesno((char_u *)
			_("Backwards range given, OK to swap"), FALSE) != 'y')
		    goto doend;
	    }
	    lnum = ea.line1;
	    ea.line1 = ea.line2;
	    ea.line2 = lnum;
	}
	if ((errormsg = invalid_range(&ea)) != NULL)
	    goto doend;
    }

    if ((ea.addr_type == ADDR_OTHER) && ea.addr_count == 0)
	// default is 1, not cursor
	ea.line2 = 1;

    correct_range(&ea);

#ifdef FEAT_FOLDING
    if (((ea.argt & EX_WHOLEFOLD) || ea.addr_count >= 2) && !global_busy
	    && ea.addr_type == ADDR_LINES)
    {
	/* Put the first line at the start of a closed fold, put the last line
	 * at the end of a closed fold. */
	(void)hasFolding(ea.line1, &ea.line1, NULL);
	(void)hasFolding(ea.line2, NULL, &ea.line2);
    }
#endif

#ifdef FEAT_QUICKFIX
    /*
     * For the ":make" and ":grep" commands we insert the 'makeprg'/'grepprg'
     * option here, so things like % get expanded.
     */
    p = replace_makeprg(&ea, p, cmdlinep);
    if (p == NULL)
	goto doend;
#endif

    /*
     * Skip to start of argument.
     * Don't do this for the ":!" command, because ":!! -l" needs the space.
     */
    if (ea.cmdidx == CMD_bang)
	ea.arg = p;
    else
	ea.arg = skipwhite(p);

    // ":file" cannot be run with an argument when "curbuf_lock" is set
    if (ea.cmdidx == CMD_file && *ea.arg != NUL && curbuf_locked())
	goto doend;

    /*
     * Check for "++opt=val" argument.
     * Must be first, allow ":w ++enc=utf8 !cmd"
     */
    if (ea.argt & EX_ARGOPT)
	while (ea.arg[0] == '+' && ea.arg[1] == '+')
	    if (getargopt(&ea) == FAIL && !ni)
	    {
		errormsg = _(e_invarg);
		goto doend;
	    }

    if (ea.cmdidx == CMD_write || ea.cmdidx == CMD_update)
    {
	if (*ea.arg == '>')			/* append */
	{
	    if (*++ea.arg != '>')		/* typed wrong */
	    {
		errormsg = _("E494: Use w or w>>");
		goto doend;
	    }
	    ea.arg = skipwhite(ea.arg + 1);
	    ea.append = TRUE;
	}
	else if (*ea.arg == '!' && ea.cmdidx == CMD_write)  /* :w !filter */
	{
	    ++ea.arg;
	    ea.usefilter = TRUE;
	}
    }

    if (ea.cmdidx == CMD_read)
    {
	if (ea.forceit)
	{
	    ea.usefilter = TRUE;		/* :r! filter if ea.forceit */
	    ea.forceit = FALSE;
	}
	else if (*ea.arg == '!')		/* :r !filter */
	{
	    ++ea.arg;
	    ea.usefilter = TRUE;
	}
    }

    if (ea.cmdidx == CMD_lshift || ea.cmdidx == CMD_rshift)
    {
	ea.amount = 1;
	while (*ea.arg == *ea.cmd)		/* count number of '>' or '<' */
	{
	    ++ea.arg;
	    ++ea.amount;
	}
	ea.arg = skipwhite(ea.arg);
    }

    /*
     * Check for "+command" argument, before checking for next command.
     * Don't do this for ":read !cmd" and ":write !cmd".
     */
    if ((ea.argt & EX_CMDARG) && !ea.usefilter)
	ea.do_ecmd_cmd = getargcmd(&ea.arg);

    /*
     * Check for '|' to separate commands and '"' to start comments.
     * Don't do this for ":read !cmd" and ":write !cmd".
     */
    if ((ea.argt & EX_TRLBAR) && !ea.usefilter)
	separate_nextcmd(&ea);

    /*
     * Check for <newline> to end a shell command.
     * Also do this for ":read !cmd", ":write !cmd" and ":global".
     * Any others?
     */
    else if (ea.cmdidx == CMD_bang
	    || ea.cmdidx == CMD_terminal
	    || ea.cmdidx == CMD_global
	    || ea.cmdidx == CMD_vglobal
	    || ea.usefilter)
    {
	for (p = ea.arg; *p; ++p)
	{
	    /* Remove one backslash before a newline, so that it's possible to
	     * pass a newline to the shell and also a newline that is preceded
	     * with a backslash.  This makes it impossible to end a shell
	     * command in a backslash, but that doesn't appear useful.
	     * Halving the number of backslashes is incompatible with previous
	     * versions. */
	    if (*p == '\\' && p[1] == '\n')
		STRMOVE(p, p + 1);
	}
    }

    if ((ea.argt & EX_DFLALL) && ea.addr_count == 0)
    {
	buf_T	    *buf;

	ea.line1 = 1;
	switch (ea.addr_type)
	{
	    case ADDR_LINES:
	    case ADDR_OTHER:
		ea.line2 = curbuf->b_ml.ml_line_count;
		break;
	    case ADDR_LOADED_BUFFERS:
		buf = firstbuf;
		while (buf->b_next != NULL && buf->b_ml.ml_mfp == NULL)
		    buf = buf->b_next;
		ea.line1 = buf->b_fnum;
		buf = lastbuf;
		while (buf->b_prev != NULL && buf->b_ml.ml_mfp == NULL)
		    buf = buf->b_prev;
		ea.line2 = buf->b_fnum;
		break;
	    case ADDR_BUFFERS:
		ea.line1 = firstbuf->b_fnum;
		ea.line2 = lastbuf->b_fnum;
		break;
	    case ADDR_WINDOWS:
		ea.line2 = LAST_WIN_NR;
		break;
	    case ADDR_TABS:
		ea.line2 = LAST_TAB_NR;
		break;
	    case ADDR_TABS_RELATIVE:
		ea.line2 = 1;
		break;
	    case ADDR_ARGUMENTS:
		if (ARGCOUNT == 0)
		    ea.line1 = ea.line2 = 0;
		else
		    ea.line2 = ARGCOUNT;
		break;
	    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
		ea.line2 = qf_get_valid_size(&ea);
		if (ea.line2 == 0)
		    ea.line2 = 1;
#endif
		break;
	    case ADDR_NONE:
	    case ADDR_UNSIGNED:
	    case ADDR_QUICKFIX:
		iemsg(_("INTERNAL: Cannot use EX_DFLALL with ADDR_NONE, ADDR_UNSIGNED or ADDR_QUICKFIX"));
		break;
	}
    }

    /* accept numbered register only when no count allowed (:put) */
    if (       (ea.argt & EX_REGSTR)
	    && *ea.arg != NUL
	       /* Do not allow register = for user commands */
	    && (!IS_USER_CMDIDX(ea.cmdidx) || *ea.arg != '=')
	    && !((ea.argt & EX_COUNT) && VIM_ISDIGIT(*ea.arg)))
    {
#ifndef FEAT_CLIPBOARD
	/* check these explicitly for a more specific error message */
	if (*ea.arg == '*' || *ea.arg == '+')
	{
	    errormsg = _(e_invalidreg);
	    goto doend;
	}
#endif
	if (valid_yank_reg(*ea.arg, (ea.cmdidx != CMD_put
					      && !IS_USER_CMDIDX(ea.cmdidx))))
	{
	    ea.regname = *ea.arg++;
#ifdef FEAT_EVAL
	    /* for '=' register: accept the rest of the line as an expression */
	    if (ea.arg[-1] == '=' && ea.arg[0] != NUL)
	    {
		set_expr_line(vim_strsave(ea.arg));
		ea.arg += STRLEN(ea.arg);
	    }
#endif
	    ea.arg = skipwhite(ea.arg);
	}
    }

    /*
     * Check for a count.  When accepting a EX_BUFNAME, don't use "123foo" as a
     * count, it's a buffer name.
     */
    if ((ea.argt & EX_COUNT) && VIM_ISDIGIT(*ea.arg)
	    && (!(ea.argt & EX_BUFNAME) || *(p = skipdigits(ea.arg)) == NUL
							  || VIM_ISWHITE(*p)))
    {
	n = getdigits(&ea.arg);
	ea.arg = skipwhite(ea.arg);
	if (n <= 0 && !ni && (ea.argt & EX_ZEROR) == 0)
	{
	    errormsg = _(e_zerocount);
	    goto doend;
	}
	if (ea.addr_type != ADDR_LINES)	// e.g. :buffer 2, :sleep 3
	{
	    ea.line2 = n;
	    if (ea.addr_count == 0)
		ea.addr_count = 1;
	}
	else
	{
	    ea.line1 = ea.line2;
	    ea.line2 += n - 1;
	    ++ea.addr_count;
	    /*
	     * Be vi compatible: no error message for out of range.
	     */
	    if (ea.line2 > curbuf->b_ml.ml_line_count)
		ea.line2 = curbuf->b_ml.ml_line_count;
	}
    }

    /*
     * Check for flags: 'l', 'p' and '#'.
     */
    if (ea.argt & EX_FLAGS)
	get_flags(&ea);
    if (!ni && !(ea.argt & EX_EXTRA) && *ea.arg != NUL
	    && *ea.arg != '"' && (*ea.arg != '|' || (ea.argt & EX_TRLBAR) == 0))
    {
	// no arguments allowed but there is something
	errormsg = _(e_trailing);
	goto doend;
    }

    if (!ni && (ea.argt & EX_NEEDARG) && *ea.arg == NUL)
    {
	errormsg = _(e_argreq);
	goto doend;
    }

#ifdef FEAT_EVAL
    /*
     * Skip the command when it's not going to be executed.
     * The commands like :if, :endif, etc. always need to be executed.
     * Also make an exception for commands that handle a trailing command
     * themselves.
     */
    if (ea.skip)
    {
	switch (ea.cmdidx)
	{
	    /* commands that need evaluation */
	    case CMD_while:
	    case CMD_endwhile:
	    case CMD_for:
	    case CMD_endfor:
	    case CMD_if:
	    case CMD_elseif:
	    case CMD_else:
	    case CMD_endif:
	    case CMD_try:
	    case CMD_catch:
	    case CMD_finally:
	    case CMD_endtry:
	    case CMD_function:
				break;

	    /* Commands that handle '|' themselves.  Check: A command should
	     * either have the EX_TRLBAR flag, appear in this list or appear in
	     * the list at ":help :bar". */
	    case CMD_aboveleft:
	    case CMD_and:
	    case CMD_belowright:
	    case CMD_botright:
	    case CMD_browse:
	    case CMD_call:
	    case CMD_confirm:
	    case CMD_delfunction:
	    case CMD_djump:
	    case CMD_dlist:
	    case CMD_dsearch:
	    case CMD_dsplit:
	    case CMD_echo:
	    case CMD_echoerr:
	    case CMD_echomsg:
	    case CMD_echon:
	    case CMD_execute:
	    case CMD_filter:
	    case CMD_help:
	    case CMD_hide:
	    case CMD_ijump:
	    case CMD_ilist:
	    case CMD_isearch:
	    case CMD_isplit:
	    case CMD_keepalt:
	    case CMD_keepjumps:
	    case CMD_keepmarks:
	    case CMD_keeppatterns:
	    case CMD_leftabove:
	    case CMD_let:
	    case CMD_lockmarks:
	    case CMD_lua:
	    case CMD_match:
	    case CMD_mzscheme:
	    case CMD_noautocmd:
	    case CMD_noswapfile:
	    case CMD_perl:
	    case CMD_psearch:
	    case CMD_python:
	    case CMD_py3:
	    case CMD_python3:
	    case CMD_return:
	    case CMD_rightbelow:
	    case CMD_ruby:
	    case CMD_silent:
	    case CMD_smagic:
	    case CMD_snomagic:
	    case CMD_substitute:
	    case CMD_syntax:
	    case CMD_tab:
	    case CMD_tcl:
	    case CMD_throw:
	    case CMD_tilde:
	    case CMD_topleft:
	    case CMD_unlet:
	    case CMD_verbose:
	    case CMD_vertical:
	    case CMD_wincmd:
				break;

	    default:		goto doend;
	}
    }
#endif

    if (ea.argt & EX_XFILE)
    {
	if (expand_filename(&ea, cmdlinep, &errormsg) == FAIL)
	    goto doend;
    }

    /*
     * Accept buffer name.  Cannot be used at the same time with a buffer
     * number.  Don't do this for a user command.
     */
    if ((ea.argt & EX_BUFNAME) && *ea.arg != NUL && ea.addr_count == 0
	    && !IS_USER_CMDIDX(ea.cmdidx))
    {
	/*
	 * :bdelete, :bwipeout and :bunload take several arguments, separated
	 * by spaces: find next space (skipping over escaped characters).
	 * The others take one argument: ignore trailing spaces.
	 */
	if (ea.cmdidx == CMD_bdelete || ea.cmdidx == CMD_bwipeout
						  || ea.cmdidx == CMD_bunload)
	    p = skiptowhite_esc(ea.arg);
	else
	{
	    p = ea.arg + STRLEN(ea.arg);
	    while (p > ea.arg && VIM_ISWHITE(p[-1]))
		--p;
	}
	ea.line2 = buflist_findpat(ea.arg, p, (ea.argt & EX_BUFUNL) != 0,
								FALSE, FALSE);
	if (ea.line2 < 0)	    /* failed */
	    goto doend;
	ea.addr_count = 1;
	ea.arg = skipwhite(p);
    }

    /* The :try command saves the emsg_silent flag, reset it here when
     * ":silent! try" was used, it should only apply to :try itself. */
    if (ea.cmdidx == CMD_try && ea.did_esilent > 0)
    {
	emsg_silent -= ea.did_esilent;
	if (emsg_silent < 0)
	    emsg_silent = 0;
	ea.did_esilent = 0;
    }

/*
 * 7. Execute the command.
 */

    if (IS_USER_CMDIDX(ea.cmdidx))
    {
	/*
	 * Execute a user-defined command.
	 */
	do_ucmd(&ea);
    }
    else
    {
	/*
	 * Call the function to execute the builtin command.
	 */
	ea.errmsg = NULL;
	(cmdnames[ea.cmdidx].cmd_func)(&ea);
	if (ea.errmsg != NULL)
	    errormsg = _(ea.errmsg);
    }

#ifdef FEAT_EVAL
    /*
     * If the command just executed called do_cmdline(), any throw or ":return"
     * or ":finish" encountered there must also check the cstack of the still
     * active do_cmdline() that called this do_one_cmd().  Rethrow an uncaught
     * exception, or reanimate a returned function or finished script file and
     * return or finish it again.
     */
    if (need_rethrow)
	do_throw(cstack);
    else if (check_cstack)
    {
	if (source_finished(fgetline, cookie))
	    do_finish(&ea, TRUE);
	else if (getline_equal(fgetline, cookie, get_func_line)
						   && current_func_returned())
	    do_return(&ea, TRUE, FALSE, NULL);
    }
    need_rethrow = check_cstack = FALSE;
#endif

doend:
    if (curwin->w_cursor.lnum == 0)	/* can happen with zero line number */
    {
	curwin->w_cursor.lnum = 1;
	curwin->w_cursor.col = 0;
    }

    if (errormsg != NULL && *errormsg != NUL && !did_emsg)
    {
	if (sourcing)
	{
	    if (errormsg != (char *)IObuff)
	    {
		STRCPY(IObuff, errormsg);
		errormsg = (char *)IObuff;
	    }
	    append_command(*cmdlinep);
	}
	emsg(errormsg);
    }
#ifdef FEAT_EVAL
    do_errthrow(cstack,
	    (ea.cmdidx != CMD_SIZE && !IS_USER_CMDIDX(ea.cmdidx))
			? cmdnames[(int)ea.cmdidx].cmd_name : (char_u *)NULL);
#endif

    if (ea.verbose_save >= 0)
	p_verbose = ea.verbose_save;

    free_cmdmod();
    cmdmod = save_cmdmod;
    reg_executing = save_reg_executing;

    if (ea.save_msg_silent != -1)
    {
	/* messages could be enabled for a serious error, need to check if the
	 * counters don't become negative */
	if (!did_emsg || msg_silent > ea.save_msg_silent)
	    msg_silent = ea.save_msg_silent;
	emsg_silent -= ea.did_esilent;
	if (emsg_silent < 0)
	    emsg_silent = 0;
	/* Restore msg_scroll, it's set by file I/O commands, even when no
	 * message is actually displayed. */
	msg_scroll = save_msg_scroll;

	/* "silent reg" or "silent echo x" inside "redir" leaves msg_col
	 * somewhere in the line.  Put it back in the first column. */
	if (redirecting())
	    msg_col = 0;
    }

#ifdef HAVE_SANDBOX
    if (ea.did_sandbox)
	--sandbox;
#endif

    if (ea.nextcmd && *ea.nextcmd == NUL)	/* not really a next command */
	ea.nextcmd = NULL;

#ifdef FEAT_EVAL
    --ex_nesting_level;
#endif

    return ea.nextcmd;
}
#if (_MSC_VER == 1200)
 #pragma optimize( "", on )
#endif

/*
 * Parse and skip over command modifiers:
 * - update eap->cmd
 * - store flags in "cmdmod".
 * - Set ex_pressedreturn for an empty command line.
 * - set msg_silent for ":silent"
 * - set 'eventignore' to "all" for ":noautocmd"
 * - set p_verbose for ":verbose"
 * - Increment "sandbox" for ":sandbox"
 * When "skip_only" is TRUE the global variables are not changed, except for
 * "cmdmod".
 * Return FAIL when the command is not to be executed.
 * May set "errormsg" to an error message.
 */
    int
parse_command_modifiers(exarg_T *eap, char **errormsg, int skip_only)
{
    char_u *p;

    vim_memset(&cmdmod, 0, sizeof(cmdmod));
    eap->verbose_save = -1;
    eap->save_msg_silent = -1;

    // Repeat until no more command modifiers are found.
    for (;;)
    {
	while (*eap->cmd == ' ' || *eap->cmd == '\t' || *eap->cmd == ':')
	    ++eap->cmd;

	/* in ex mode, an empty line works like :+ */
	if (*eap->cmd == NUL && exmode_active
		   && (getline_equal(eap->getline, eap->cookie, getexmodeline)
		       || getline_equal(eap->getline, eap->cookie, getexline))
			&& curwin->w_cursor.lnum < curbuf->b_ml.ml_line_count)
	{
	    eap->cmd = (char_u *)"+";
	    if (!skip_only)
		ex_pressedreturn = TRUE;
	}

	/* ignore comment and empty lines */
	if (*eap->cmd == '"')
	    return FAIL;
	if (*eap->cmd == NUL)
	{
	    if (!skip_only)
		ex_pressedreturn = TRUE;
	    return FAIL;
	}

	p = skip_range(eap->cmd, NULL);
	switch (*p)
	{
	    /* When adding an entry, also modify cmd_exists(). */
	    case 'a':	if (!checkforcmd(&eap->cmd, "aboveleft", 3))
			    break;
			cmdmod.split |= WSP_ABOVE;
			continue;

	    case 'b':	if (checkforcmd(&eap->cmd, "belowright", 3))
			{
			    cmdmod.split |= WSP_BELOW;
			    continue;
			}
			if (checkforcmd(&eap->cmd, "browse", 3))
			{
#ifdef FEAT_BROWSE_CMD
			    cmdmod.browse = TRUE;
#endif
			    continue;
			}
			if (!checkforcmd(&eap->cmd, "botright", 2))
			    break;
			cmdmod.split |= WSP_BOT;
			continue;

	    case 'c':	if (!checkforcmd(&eap->cmd, "confirm", 4))
			    break;
#if defined(FEAT_GUI_DIALOG) || defined(FEAT_CON_DIALOG)
			cmdmod.confirm = TRUE;
#endif
			continue;

	    case 'k':	if (checkforcmd(&eap->cmd, "keepmarks", 3))
			{
			    cmdmod.keepmarks = TRUE;
			    continue;
			}
			if (checkforcmd(&eap->cmd, "keepalt", 5))
			{
			    cmdmod.keepalt = TRUE;
			    continue;
			}
			if (checkforcmd(&eap->cmd, "keeppatterns", 5))
			{
			    cmdmod.keeppatterns = TRUE;
			    continue;
			}
			if (!checkforcmd(&eap->cmd, "keepjumps", 5))
			    break;
			cmdmod.keepjumps = TRUE;
			continue;

	    case 'f':	/* only accept ":filter {pat} cmd" */
			{
			    char_u *reg_pat;

			    if (!checkforcmd(&p, "filter", 4)
						|| *p == NUL || ends_excmd(*p))
				break;
			    if (*p == '!')
			    {
				cmdmod.filter_force = TRUE;
				p = skipwhite(p + 1);
				if (*p == NUL || ends_excmd(*p))
				    break;
			    }
			    if (skip_only)
				p = skip_vimgrep_pat(p, NULL, NULL);
			    else
				// NOTE: This puts a NUL after the pattern.
				p = skip_vimgrep_pat(p, &reg_pat, NULL);
			    if (p == NULL || *p == NUL)
				break;
			    if (!skip_only)
			    {
				cmdmod.filter_regmatch.regprog =
						vim_regcomp(reg_pat, RE_MAGIC);
				if (cmdmod.filter_regmatch.regprog == NULL)
				    break;
			    }
			    eap->cmd = p;
			    continue;
			}

			/* ":hide" and ":hide | cmd" are not modifiers */
	    case 'h':	if (p != eap->cmd || !checkforcmd(&p, "hide", 3)
					       || *p == NUL || ends_excmd(*p))
			    break;
			eap->cmd = p;
			cmdmod.hide = TRUE;
			continue;

	    case 'l':	if (checkforcmd(&eap->cmd, "lockmarks", 3))
			{
			    cmdmod.lockmarks = TRUE;
			    continue;
			}

			if (!checkforcmd(&eap->cmd, "leftabove", 5))
			    break;
			cmdmod.split |= WSP_ABOVE;
			continue;

	    case 'n':	if (checkforcmd(&eap->cmd, "noautocmd", 3))
			{
			    if (cmdmod.save_ei == NULL && !skip_only)
			    {
				/* Set 'eventignore' to "all". Restore the
				 * existing option value later. */
				cmdmod.save_ei = vim_strsave(p_ei);
				set_string_option_direct((char_u *)"ei", -1,
					 (char_u *)"all", OPT_FREE, SID_NONE);
			    }
			    continue;
			}
			if (!checkforcmd(&eap->cmd, "noswapfile", 3))
			    break;
			cmdmod.noswapfile = TRUE;
			continue;

	    case 'r':	if (!checkforcmd(&eap->cmd, "rightbelow", 6))
			    break;
			cmdmod.split |= WSP_BELOW;
			continue;

	    case 's':	if (checkforcmd(&eap->cmd, "sandbox", 3))
			{
#ifdef HAVE_SANDBOX
			    if (!skip_only)
			    {
				if (!eap->did_sandbox)
				    ++sandbox;
				eap->did_sandbox = TRUE;
			    }
#endif
			    continue;
			}
			if (!checkforcmd(&eap->cmd, "silent", 3))
			    break;
			if (!skip_only)
			{
			    if (eap->save_msg_silent == -1)
				eap->save_msg_silent = msg_silent;
			    ++msg_silent;
			}
			if (*eap->cmd == '!' && !VIM_ISWHITE(eap->cmd[-1]))
			{
			    /* ":silent!", but not "silent !cmd" */
			    eap->cmd = skipwhite(eap->cmd + 1);
			    if (!skip_only)
			    {
				++emsg_silent;
				++eap->did_esilent;
			    }
			}
			continue;

	    case 't':	if (checkforcmd(&p, "tab", 3))
			{
			    if (!skip_only)
			    {
				long tabnr = get_address(eap, &eap->cmd,
						    ADDR_TABS, eap->skip,
						    skip_only, FALSE, 1);
				if (tabnr == MAXLNUM)
				    cmdmod.tab = tabpage_index(curtab) + 1;
				else
				{
				    if (tabnr < 0 || tabnr > LAST_TAB_NR)
				    {
					*errormsg = _(e_invrange);
					return FAIL;
				    }
				    cmdmod.tab = tabnr + 1;
				}
			    }
			    eap->cmd = p;
			    continue;
			}
			if (!checkforcmd(&eap->cmd, "topleft", 2))
			    break;
			cmdmod.split |= WSP_TOP;
			continue;

	    case 'u':	if (!checkforcmd(&eap->cmd, "unsilent", 3))
			    break;
			if (!skip_only)
			{
			    if (eap->save_msg_silent == -1)
				eap->save_msg_silent = msg_silent;
			    msg_silent = 0;
			}
			continue;

	    case 'v':	if (checkforcmd(&eap->cmd, "vertical", 4))
			{
			    cmdmod.split |= WSP_VERT;
			    continue;
			}
			if (!checkforcmd(&p, "verbose", 4))
			    break;
			if (!skip_only)
			{
			    if (eap->verbose_save < 0)
				eap->verbose_save = p_verbose;
			    if (vim_isdigit(*eap->cmd))
				p_verbose = atoi((char *)eap->cmd);
			    else
				p_verbose = 1;
			}
			eap->cmd = p;
			continue;
	}
	break;
    }

    return OK;
}

/*
 * Free contents of "cmdmod".
 */
    static void
free_cmdmod(void)
{
    if (cmdmod.save_ei != NULL)
    {
	/* Restore 'eventignore' to the value before ":noautocmd". */
	set_string_option_direct((char_u *)"ei", -1, cmdmod.save_ei,
							  OPT_FREE, SID_NONE);
	free_string_option(cmdmod.save_ei);
    }

    if (cmdmod.filter_regmatch.regprog != NULL)
	vim_regfree(cmdmod.filter_regmatch.regprog);
}

/*
 * Parse the address range, if any, in "eap".
 * May set the last search pattern, unless "silent" is TRUE.
 * Return FAIL and set "errormsg" or return OK.
 */
    int
parse_cmd_address(exarg_T *eap, char **errormsg, int silent)
{
    int		address_count = 1;
    linenr_T	lnum;

    // Repeat for all ',' or ';' separated addresses.
    for (;;)
    {
	eap->line1 = eap->line2;
	switch (eap->addr_type)
	{
	    case ADDR_LINES:
	    case ADDR_OTHER:
		// default is current line number
		eap->line2 = curwin->w_cursor.lnum;
		break;
	    case ADDR_WINDOWS:
		eap->line2 = CURRENT_WIN_NR;
		break;
	    case ADDR_ARGUMENTS:
		eap->line2 = curwin->w_arg_idx + 1;
		if (eap->line2 > ARGCOUNT)
		    eap->line2 = ARGCOUNT;
		break;
	    case ADDR_LOADED_BUFFERS:
	    case ADDR_BUFFERS:
		eap->line2 = curbuf->b_fnum;
		break;
	    case ADDR_TABS:
		eap->line2 = CURRENT_TAB_NR;
		break;
	    case ADDR_TABS_RELATIVE:
	    case ADDR_UNSIGNED:
		eap->line2 = 1;
		break;
	    case ADDR_QUICKFIX:
#ifdef FEAT_QUICKFIX
		eap->line2 = qf_get_cur_idx(eap);
#endif
		break;
	    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
		eap->line2 = qf_get_cur_valid_idx(eap);
#endif
		break;
	    case ADDR_NONE:
		// Will give an error later if a range is found.
		break;
	}
	eap->cmd = skipwhite(eap->cmd);
	lnum = get_address(eap, &eap->cmd, eap->addr_type, eap->skip, silent,
					eap->addr_count == 0, address_count++);
	if (eap->cmd == NULL)	// error detected
	    return FAIL;
	if (lnum == MAXLNUM)
	{
	    if (*eap->cmd == '%')   // '%' - all lines
	    {
		++eap->cmd;
		switch (eap->addr_type)
		{
		    case ADDR_LINES:
		    case ADDR_OTHER:
			eap->line1 = 1;
			eap->line2 = curbuf->b_ml.ml_line_count;
			break;
		    case ADDR_LOADED_BUFFERS:
			{
			    buf_T	*buf = firstbuf;

			    while (buf->b_next != NULL
						  && buf->b_ml.ml_mfp == NULL)
				buf = buf->b_next;
			    eap->line1 = buf->b_fnum;
			    buf = lastbuf;
			    while (buf->b_prev != NULL
						  && buf->b_ml.ml_mfp == NULL)
				buf = buf->b_prev;
			    eap->line2 = buf->b_fnum;
			    break;
			}
		    case ADDR_BUFFERS:
			eap->line1 = firstbuf->b_fnum;
			eap->line2 = lastbuf->b_fnum;
			break;
		    case ADDR_WINDOWS:
		    case ADDR_TABS:
			if (IS_USER_CMDIDX(eap->cmdidx))
			{
			    eap->line1 = 1;
			    eap->line2 = eap->addr_type == ADDR_WINDOWS
						  ? LAST_WIN_NR : LAST_TAB_NR;
			}
			else
			{
			    // there is no Vim command which uses '%' and
			    // ADDR_WINDOWS or ADDR_TABS
			    *errormsg = _(e_invrange);
			    return FAIL;
			}
			break;
		    case ADDR_TABS_RELATIVE:
		    case ADDR_UNSIGNED:
		    case ADDR_QUICKFIX:
			*errormsg = _(e_invrange);
			return FAIL;
		    case ADDR_ARGUMENTS:
			if (ARGCOUNT == 0)
			    eap->line1 = eap->line2 = 0;
			else
			{
			    eap->line1 = 1;
			    eap->line2 = ARGCOUNT;
			}
			break;
		    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
			eap->line1 = 1;
			eap->line2 = qf_get_valid_size(eap);
			if (eap->line2 == 0)
			    eap->line2 = 1;
#endif
			break;
		    case ADDR_NONE:
			// Will give an error later if a range is found.
			break;
		}
		++eap->addr_count;
	    }
	    else if (*eap->cmd == '*' && vim_strchr(p_cpo, CPO_STAR) == NULL)
	    {
		pos_T	    *fp;

		// '*' - visual area
		if (eap->addr_type != ADDR_LINES)
		{
		    *errormsg = _(e_invrange);
		    return FAIL;
		}

		++eap->cmd;
		if (!eap->skip)
		{
		    fp = getmark('<', FALSE);
		    if (check_mark(fp) == FAIL)
			return FAIL;
		    eap->line1 = fp->lnum;
		    fp = getmark('>', FALSE);
		    if (check_mark(fp) == FAIL)
			return FAIL;
		    eap->line2 = fp->lnum;
		    ++eap->addr_count;
		}
	    }
	}
	else
	    eap->line2 = lnum;
	eap->addr_count++;

	if (*eap->cmd == ';')
	{
	    if (!eap->skip)
	    {
		curwin->w_cursor.lnum = eap->line2;
		// don't leave the cursor on an illegal line or column
		check_cursor();
	    }
	}
	else if (*eap->cmd != ',')
	    break;
	++eap->cmd;
    }

    // One address given: set start and end lines.
    if (eap->addr_count == 1)
    {
	eap->line1 = eap->line2;
	// ... but only implicit: really no address given
	if (lnum == MAXLNUM)
	    eap->addr_count = 0;
    }
    return OK;
}

/*
 * Check for an Ex command with optional tail.
 * If there is a match advance "pp" to the argument and return TRUE.
 */
    int
checkforcmd(
    char_u	**pp,		/* start of command */
    char	*cmd,		/* name of command */
    int		len)		/* required length */
{
    int		i;

    for (i = 0; cmd[i] != NUL; ++i)
	if (((char_u *)cmd)[i] != (*pp)[i])
	    break;
    if (i >= len && !isalpha((*pp)[i]))
    {
	*pp = skipwhite(*pp + i);
	return TRUE;
    }
    return FALSE;
}

/*
 * Append "cmd" to the error message in IObuff.
 * Takes care of limiting the length and handling 0xa0, which would be
 * invisible otherwise.
 */
    static void
append_command(char_u *cmd)
{
    char_u *s = cmd;
    char_u *d;

    STRCAT(IObuff, ": ");
    d = IObuff + STRLEN(IObuff);
    while (*s != NUL && d - IObuff < IOSIZE - 7)
    {
	if (enc_utf8 ? (s[0] == 0xc2 && s[1] == 0xa0) : *s == 0xa0)
	{
	    s += enc_utf8 ? 2 : 1;
	    STRCPY(d, "<a0>");
	    d += 4;
	}
	else
	    MB_COPY_CHAR(s, d);
    }
    *d = NUL;
}

/*
 * Find an Ex command by its name, either built-in or user.
 * Start of the name can be found at eap->cmd.
 * Sets eap->cmdidx and returns a pointer to char after the command name.
 * "full" is set to TRUE if the whole command name matched.
 * Returns NULL for an ambiguous user command.
 */
    static char_u *
find_command(exarg_T *eap, int *full UNUSED)
{
    int		len;
    char_u	*p;
    int		i;

    /*
     * Isolate the command and search for it in the command table.
     * Exceptions:
     * - the 'k' command can directly be followed by any character.
     * - the 's' command can be followed directly by 'c', 'g', 'i', 'I' or 'r'
     *	    but :sre[wind] is another command, as are :scr[iptnames],
     *	    :scs[cope], :sim[alt], :sig[ns] and :sil[ent].
     * - the "d" command can directly be followed by 'l' or 'p' flag.
     */
    p = eap->cmd;
    if (*p == 'k')
    {
	eap->cmdidx = CMD_k;
	++p;
    }
    else if (p[0] == 's'
	    && ((p[1] == 'c' && (p[2] == NUL || (p[2] != 's' && p[2] != 'r'
			&& (p[3] == NUL || (p[3] != 'i' && p[4] != 'p')))))
		|| p[1] == 'g'
		|| (p[1] == 'i' && p[2] != 'm' && p[2] != 'l' && p[2] != 'g')
		|| p[1] == 'I'
		|| (p[1] == 'r' && p[2] != 'e')))
    {
	eap->cmdidx = CMD_substitute;
	++p;
    }
    else
    {
	while (ASCII_ISALPHA(*p))
	    ++p;
	/* for python 3.x support ":py3", ":python3", ":py3file", etc. */
	if (eap->cmd[0] == 'p' && eap->cmd[1] == 'y')
	    while (ASCII_ISALNUM(*p))
		++p;

	/* check for non-alpha command */
	if (p == eap->cmd && vim_strchr((char_u *)"@*!=><&~#", *p) != NULL)
	    ++p;
	len = (int)(p - eap->cmd);
	if (*eap->cmd == 'd' && (p[-1] == 'l' || p[-1] == 'p'))
	{
	    /* Check for ":dl", ":dell", etc. to ":deletel": that's
	     * :delete with the 'l' flag.  Same for 'p'. */
	    for (i = 0; i < len; ++i)
		if (eap->cmd[i] != ((char_u *)"delete")[i])
		    break;
	    if (i == len - 1)
	    {
		--len;
		if (p[-1] == 'l')
		    eap->flags |= EXFLAG_LIST;
		else
		    eap->flags |= EXFLAG_PRINT;
	    }
	}

	if (ASCII_ISLOWER(*eap->cmd))
	    eap->cmdidx = cmdidxs[CharOrdLow(*eap->cmd)];

	for ( ; (int)eap->cmdidx < (int)CMD_SIZE;
			       eap->cmdidx = (cmdidx_T)((int)eap->cmdidx + 1))
	    if (STRNCMP(cmdnames[(int)eap->cmdidx].cmd_name, (char *)eap->cmd,
							    (size_t)len) == 0)
	    {
#ifdef FEAT_EVAL
		if (full != NULL
			   && cmdnames[(int)eap->cmdidx].cmd_name[len] == NUL)
		    *full = TRUE;
#endif
		break;
	    }

	// Look for a user defined command as a last resort.  Let ":Print" be
	// overruled by a user defined command.
	if ((eap->cmdidx == CMD_SIZE || eap->cmdidx == CMD_Print)
		&& *eap->cmd >= 'A' && *eap->cmd <= 'Z')
	{
	    // User defined commands may contain digits.
	    while (ASCII_ISALNUM(*p))
		++p;
	    p = find_ucmd(eap, p, full, NULL, NULL);
	}
	if (p == eap->cmd)
	    eap->cmdidx = CMD_SIZE;
    }

    return p;
}

#if defined(FEAT_EVAL) || defined(PROTO)
static struct cmdmod
{
    char	*name;
    int		minlen;
    int		has_count;  /* :123verbose  :3tab */
} cmdmods[] = {
    {"aboveleft", 3, FALSE},
    {"belowright", 3, FALSE},
    {"botright", 2, FALSE},
    {"browse", 3, FALSE},
    {"confirm", 4, FALSE},
    {"filter", 4, FALSE},
    {"hide", 3, FALSE},
    {"keepalt", 5, FALSE},
    {"keepjumps", 5, FALSE},
    {"keepmarks", 3, FALSE},
    {"keeppatterns", 5, FALSE},
    {"leftabove", 5, FALSE},
    {"lockmarks", 3, FALSE},
    {"noautocmd", 3, FALSE},
    {"noswapfile", 3, FALSE},
    {"rightbelow", 6, FALSE},
    {"sandbox", 3, FALSE},
    {"silent", 3, FALSE},
    {"tab", 3, TRUE},
    {"topleft", 2, FALSE},
    {"unsilent", 3, FALSE},
    {"verbose", 4, TRUE},
    {"vertical", 4, FALSE},
};

/*
 * Return length of a command modifier (including optional count).
 * Return zero when it's not a modifier.
 */
    int
modifier_len(char_u *cmd)
{
    int		i, j;
    char_u	*p = cmd;

    if (VIM_ISDIGIT(*cmd))
	p = skipwhite(skipdigits(cmd));
    for (i = 0; i < (int)(sizeof(cmdmods) / sizeof(struct cmdmod)); ++i)
    {
	for (j = 0; p[j] != NUL; ++j)
	    if (p[j] != cmdmods[i].name[j])
		break;
	if (!ASCII_ISALPHA(p[j]) && j >= cmdmods[i].minlen
					&& (p == cmd || cmdmods[i].has_count))
	    return j + (int)(p - cmd);
    }
    return 0;
}

/*
 * Return > 0 if an Ex command "name" exists.
 * Return 2 if there is an exact match.
 * Return 3 if there is an ambiguous match.
 */
    int
cmd_exists(char_u *name)
{
    exarg_T	ea;
    int		full = FALSE;
    int		i;
    int		j;
    char_u	*p;

    /* Check command modifiers. */
    for (i = 0; i < (int)(sizeof(cmdmods) / sizeof(struct cmdmod)); ++i)
    {
	for (j = 0; name[j] != NUL; ++j)
	    if (name[j] != cmdmods[i].name[j])
		break;
	if (name[j] == NUL && j >= cmdmods[i].minlen)
	    return (cmdmods[i].name[j] == NUL ? 2 : 1);
    }

    /* Check built-in commands and user defined commands.
     * For ":2match" and ":3match" we need to skip the number. */
    ea.cmd = (*name == '2' || *name == '3') ? name + 1 : name;
    ea.cmdidx = (cmdidx_T)0;
    p = find_command(&ea, &full);
    if (p == NULL)
	return 3;
    if (vim_isdigit(*name) && ea.cmdidx != CMD_match)
	return 0;
    if (*skipwhite(p) != NUL)
	return 0;	/* trailing garbage */
    return (ea.cmdidx == CMD_SIZE ? 0 : (full ? 2 : 1));
}
#endif

    cmdidx_T
excmd_get_cmdidx(char_u *cmd, int len)
{
    cmdidx_T idx;

    for (idx = (cmdidx_T)0; (int)idx < (int)CMD_SIZE;
	    idx = (cmdidx_T)((int)idx + 1))
	if (STRNCMP(cmdnames[(int)idx].cmd_name, cmd, (size_t)len) == 0)
	    break;

    return idx;
}

    long
excmd_get_argt(cmdidx_T idx)
{
    return (long)cmdnames[(int)idx].cmd_argt;
}

/*
 * Skip a range specifier of the form: addr [,addr] [;addr] ..
 *
 * Backslashed delimiters after / or ? will be skipped, and commands will
 * not be expanded between /'s and ?'s or after "'".
 *
 * Also skip white space and ":" characters.
 * Returns the "cmd" pointer advanced to beyond the range.
 */
    char_u *
skip_range(
    char_u	*cmd,
    int		*ctx)	/* pointer to xp_context or NULL */
{
    unsigned	delim;

    while (vim_strchr((char_u *)" \t0123456789.$%'/?-+,;\\", *cmd) != NULL)
    {
	if (*cmd == '\\')
	{
	    if (cmd[1] == '?' || cmd[1] == '/' || cmd[1] == '&')
		++cmd;
	    else
		break;
	}
	else if (*cmd == '\'')
	{
	    if (*++cmd == NUL && ctx != NULL)
		*ctx = EXPAND_NOTHING;
	}
	else if (*cmd == '/' || *cmd == '?')
	{
	    delim = *cmd++;
	    while (*cmd != NUL && *cmd != delim)
		if (*cmd++ == '\\' && *cmd != NUL)
		    ++cmd;
	    if (*cmd == NUL && ctx != NULL)
		*ctx = EXPAND_NOTHING;
	}
	if (*cmd != NUL)
	    ++cmd;
    }

    /* Skip ":" and white space. */
    while (*cmd == ':')
	cmd = skipwhite(cmd + 1);

    return cmd;
}

    static void
addr_error(cmd_addr_T addr_type)
{
    if (addr_type == ADDR_NONE)
	emsg(_(e_norange));
    else
	emsg(_(e_invrange));
}

/*
 * Get a single EX address.
 *
 * Set ptr to the next character after the part that was interpreted.
 * Set ptr to NULL when an error is encountered.
 * This may set the last used search pattern.
 *
 * Return MAXLNUM when no Ex address was found.
 */
    static linenr_T
get_address(
    exarg_T	*eap UNUSED,
    char_u	**ptr,
    cmd_addr_T	addr_type_arg,
    int		skip,		// only skip the address, don't use it
    int		silent,		// no errors or side effects
    int		to_other_file,  // flag: may jump to other file
    int		address_count UNUSED) // 1 for first address, >1 after comma
{
    cmd_addr_T	addr_type = addr_type_arg;
    int		c;
    int		i;
    long	n;
    char_u	*cmd;
    pos_T	pos;
    pos_T	*fp;
    linenr_T	lnum;
    buf_T	*buf;

    cmd = skipwhite(*ptr);
    lnum = MAXLNUM;
    do
    {
	switch (*cmd)
	{
	    case '.':			    /* '.' - Cursor position */
		++cmd;
		switch (addr_type)
		{
		    case ADDR_LINES:
		    case ADDR_OTHER:
			lnum = curwin->w_cursor.lnum;
			break;
		    case ADDR_WINDOWS:
			lnum = CURRENT_WIN_NR;
			break;
		    case ADDR_ARGUMENTS:
			lnum = curwin->w_arg_idx + 1;
			break;
		    case ADDR_LOADED_BUFFERS:
		    case ADDR_BUFFERS:
			lnum = curbuf->b_fnum;
			break;
		    case ADDR_TABS:
			lnum = CURRENT_TAB_NR;
			break;
		    case ADDR_NONE:
		    case ADDR_TABS_RELATIVE:
		    case ADDR_UNSIGNED:
			addr_error(addr_type);
			cmd = NULL;
			goto error;
			break;
		    case ADDR_QUICKFIX:
#ifdef FEAT_QUICKFIX
			lnum = qf_get_cur_idx(eap);
#endif
			break;
		    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
			lnum = qf_get_cur_valid_idx(eap);
#endif
			break;
		}
		break;

	    case '$':			    /* '$' - last line */
		++cmd;
		switch (addr_type)
		{
		    case ADDR_LINES:
		    case ADDR_OTHER:
			lnum = curbuf->b_ml.ml_line_count;
			break;
		    case ADDR_WINDOWS:
			lnum = LAST_WIN_NR;
			break;
		    case ADDR_ARGUMENTS:
			lnum = ARGCOUNT;
			break;
		    case ADDR_LOADED_BUFFERS:
			buf = lastbuf;
			while (buf->b_ml.ml_mfp == NULL)
			{
			    if (buf->b_prev == NULL)
				break;
			    buf = buf->b_prev;
			}
			lnum = buf->b_fnum;
			break;
		    case ADDR_BUFFERS:
			lnum = lastbuf->b_fnum;
			break;
		    case ADDR_TABS:
			lnum = LAST_TAB_NR;
			break;
		    case ADDR_NONE:
		    case ADDR_TABS_RELATIVE:
		    case ADDR_UNSIGNED:
			addr_error(addr_type);
			cmd = NULL;
			goto error;
			break;
		    case ADDR_QUICKFIX:
#ifdef FEAT_QUICKFIX
			lnum = qf_get_size(eap);
			if (lnum == 0)
			    lnum = 1;
#endif
			break;
		    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
			lnum = qf_get_valid_size(eap);
			if (lnum == 0)
			    lnum = 1;
#endif
			break;
		}
		break;

	    case '\'':			    /* ''' - mark */
		if (*++cmd == NUL)
		{
		    cmd = NULL;
		    goto error;
		}
		if (addr_type != ADDR_LINES)
		{
		    addr_error(addr_type);
		    cmd = NULL;
		    goto error;
		}
		if (skip)
		    ++cmd;
		else
		{
		    /* Only accept a mark in another file when it is
		     * used by itself: ":'M". */
		    fp = getmark(*cmd, to_other_file && cmd[1] == NUL);
		    ++cmd;
		    if (fp == (pos_T *)-1)
			/* Jumped to another file. */
			lnum = curwin->w_cursor.lnum;
		    else
		    {
			if (check_mark(fp) == FAIL)
			{
			    cmd = NULL;
			    goto error;
			}
			lnum = fp->lnum;
		    }
		}
		break;

	    case '/':
	    case '?':			/* '/' or '?' - search */
		c = *cmd++;
		if (addr_type != ADDR_LINES)
		{
		    addr_error(addr_type);
		    cmd = NULL;
		    goto error;
		}
		if (skip)	/* skip "/pat/" */
		{
		    cmd = skip_regexp(cmd, c, (int)p_magic, NULL);
		    if (*cmd == c)
			++cmd;
		}
		else
		{
		    int flags;

		    pos = curwin->w_cursor; // save curwin->w_cursor

		    // When '/' or '?' follows another address, start from
		    // there.
		    if (lnum != MAXLNUM)
			curwin->w_cursor.lnum = lnum;

		    // Start a forward search at the end of the line (unless
		    // before the first line).
		    // Start a backward search at the start of the line.
		    // This makes sure we never match in the current
		    // line, and can match anywhere in the
		    // next/previous line.
		    if (c == '/' && curwin->w_cursor.lnum > 0)
			curwin->w_cursor.col = MAXCOL;
		    else
			curwin->w_cursor.col = 0;
		    searchcmdlen = 0;
		    flags = silent ? 0 : SEARCH_HIS | SEARCH_MSG;
		    if (!do_search(NULL, c, cmd, 1L, flags, NULL))
		    {
			curwin->w_cursor = pos;
			cmd = NULL;
			goto error;
		    }
		    lnum = curwin->w_cursor.lnum;
		    curwin->w_cursor = pos;
		    /* adjust command string pointer */
		    cmd += searchcmdlen;
		}
		break;

	    case '\\':		    /* "\?", "\/" or "\&", repeat search */
		++cmd;
		if (addr_type != ADDR_LINES)
		{
		    addr_error(addr_type);
		    cmd = NULL;
		    goto error;
		}
		if (*cmd == '&')
		    i = RE_SUBST;
		else if (*cmd == '?' || *cmd == '/')
		    i = RE_SEARCH;
		else
		{
		    emsg(_(e_backslash));
		    cmd = NULL;
		    goto error;
		}

		if (!skip)
		{
		    /*
		     * When search follows another address, start from
		     * there.
		     */
		    if (lnum != MAXLNUM)
			pos.lnum = lnum;
		    else
			pos.lnum = curwin->w_cursor.lnum;

		    /*
		     * Start the search just like for the above
		     * do_search().
		     */
		    if (*cmd != '?')
			pos.col = MAXCOL;
		    else
			pos.col = 0;
		    pos.coladd = 0;
		    if (searchit(curwin, curbuf, &pos, NULL,
				*cmd == '?' ? BACKWARD : FORWARD,
				(char_u *)"", 1L, SEARCH_MSG, i, NULL) != FAIL)
			lnum = pos.lnum;
		    else
		    {
			cmd = NULL;
			goto error;
		    }
		}
		++cmd;
		break;

	    default:
		if (VIM_ISDIGIT(*cmd))	/* absolute line number */
		    lnum = getdigits(&cmd);
	}

	for (;;)
	{
	    cmd = skipwhite(cmd);
	    if (*cmd != '-' && *cmd != '+' && !VIM_ISDIGIT(*cmd))
		break;

	    if (lnum == MAXLNUM)
	    {
		switch (addr_type)
		{
		    case ADDR_LINES:
		    case ADDR_OTHER:
			// "+1" is same as ".+1"
			lnum = curwin->w_cursor.lnum;
			break;
		    case ADDR_WINDOWS:
			lnum = CURRENT_WIN_NR;
			break;
		    case ADDR_ARGUMENTS:
			lnum = curwin->w_arg_idx + 1;
			break;
		    case ADDR_LOADED_BUFFERS:
		    case ADDR_BUFFERS:
			lnum = curbuf->b_fnum;
			break;
		    case ADDR_TABS:
			lnum = CURRENT_TAB_NR;
			break;
		    case ADDR_TABS_RELATIVE:
			lnum = 1;
			break;
		    case ADDR_QUICKFIX:
#ifdef FEAT_QUICKFIX
			lnum = qf_get_cur_idx(eap);
#endif
			break;
		    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
			lnum = qf_get_cur_valid_idx(eap);
#endif
			break;
		    case ADDR_NONE:
		    case ADDR_UNSIGNED:
			lnum = 0;
			break;
		}
	    }

	    if (VIM_ISDIGIT(*cmd))
		i = '+';		/* "number" is same as "+number" */
	    else
		i = *cmd++;
	    if (!VIM_ISDIGIT(*cmd))	/* '+' is '+1', but '+0' is not '+1' */
		n = 1;
	    else
		n = getdigits(&cmd);

	    if (addr_type == ADDR_TABS_RELATIVE)
	    {
		emsg(_(e_invrange));
		cmd = NULL;
		goto error;
	    }
	    else if (addr_type == ADDR_LOADED_BUFFERS
		    || addr_type == ADDR_BUFFERS)
		lnum = compute_buffer_local_count(
				    addr_type, lnum, (i == '-') ? -1 * n : n);
	    else
	    {
#ifdef FEAT_FOLDING
		/* Relative line addressing, need to adjust for folded lines
		 * now, but only do it after the first address. */
		if (addr_type == ADDR_LINES && (i == '-' || i == '+')
			&& address_count >= 2)
		    (void)hasFolding(lnum, NULL, &lnum);
#endif
		if (i == '-')
		    lnum -= n;
		else
		    lnum += n;
	    }
	}
    } while (*cmd == '/' || *cmd == '?');

error:
    *ptr = cmd;
    return lnum;
}

/*
 * Get flags from an Ex command argument.
 */
    static void
get_flags(exarg_T *eap)
{
    while (vim_strchr((char_u *)"lp#", *eap->arg) != NULL)
    {
	if (*eap->arg == 'l')
	    eap->flags |= EXFLAG_LIST;
	else if (*eap->arg == 'p')
	    eap->flags |= EXFLAG_PRINT;
	else
	    eap->flags |= EXFLAG_NR;
	eap->arg = skipwhite(eap->arg + 1);
    }
}

/*
 * Function called for command which is Not Implemented.  NI!
 */
    void
ex_ni(exarg_T *eap)
{
    if (!eap->skip)
	eap->errmsg = N_("E319: Sorry, the command is not available in this version");
}

#ifdef HAVE_EX_SCRIPT_NI
/*
 * Function called for script command which is Not Implemented.  NI!
 * Skips over ":perl <<EOF" constructs.
 */
    static void
ex_script_ni(exarg_T *eap)
{
    if (!eap->skip)
	ex_ni(eap);
    else
	vim_free(script_get(eap, eap->arg));
}
#endif

/*
 * Check range in Ex command for validity.
 * Return NULL when valid, error message when invalid.
 */
    static char *
invalid_range(exarg_T *eap)
{
    buf_T	*buf;

    if (       eap->line1 < 0
	    || eap->line2 < 0
	    || eap->line1 > eap->line2)
	return _(e_invrange);

    if (eap->argt & EX_RANGE)
    {
	switch (eap->addr_type)
	{
	    case ADDR_LINES:
		if (eap->line2 > curbuf->b_ml.ml_line_count
#ifdef FEAT_DIFF
			    + (eap->cmdidx == CMD_diffget)
#endif
		   )
		    return _(e_invrange);
		break;
	    case ADDR_ARGUMENTS:
		/* add 1 if ARGCOUNT is 0 */
		if (eap->line2 > ARGCOUNT + (!ARGCOUNT))
		    return _(e_invrange);
		break;
	    case ADDR_BUFFERS:
		// Only a boundary check, not whether the buffers actually
		// exist.
		if (eap->line1 < 1 || eap->line2 > get_highest_fnum())
		    return _(e_invrange);
		break;
	    case ADDR_LOADED_BUFFERS:
		buf = firstbuf;
		while (buf->b_ml.ml_mfp == NULL)
		{
		    if (buf->b_next == NULL)
			return _(e_invrange);
		    buf = buf->b_next;
		}
		if (eap->line1 < buf->b_fnum)
		    return _(e_invrange);
		buf = lastbuf;
		while (buf->b_ml.ml_mfp == NULL)
		{
		    if (buf->b_prev == NULL)
			return _(e_invrange);
		    buf = buf->b_prev;
		}
		if (eap->line2 > buf->b_fnum)
		    return _(e_invrange);
		break;
	    case ADDR_WINDOWS:
		if (eap->line2 > LAST_WIN_NR)
		    return _(e_invrange);
		break;
	    case ADDR_TABS:
		if (eap->line2 > LAST_TAB_NR)
		    return _(e_invrange);
		break;
	    case ADDR_TABS_RELATIVE:
	    case ADDR_OTHER:
		// Any range is OK.
		break;
	    case ADDR_QUICKFIX:
#ifdef FEAT_QUICKFIX
		// No error for value that is too big, will use the last entry.
		if (eap->line2 <= 0)
		    return _(e_invrange);
#endif
		break;
	    case ADDR_QUICKFIX_VALID:
#ifdef FEAT_QUICKFIX
		if ((eap->line2 != 1 && eap->line2 > qf_get_valid_size(eap))
			|| eap->line2 < 0)
		    return _(e_invrange);
#endif
		break;
	    case ADDR_UNSIGNED:
		if (eap->line2 < 0)
		    return _(e_invrange);
		break;
	    case ADDR_NONE:
		// Will give an error elsewhere.
		break;
	}
    }
    return NULL;
}

/*
 * Correct the range for zero line number, if required.
 */
    static void
correct_range(exarg_T *eap)
{
    if (!(eap->argt & EX_ZEROR))	    // zero in range not allowed
    {
	if (eap->line1 == 0)
	    eap->line1 = 1;
	if (eap->line2 == 0)
	    eap->line2 = 1;
    }
}

#ifdef FEAT_QUICKFIX
/*
 * For a ":vimgrep" or ":vimgrepadd" command return a pointer past the
 * pattern.  Otherwise return eap->arg.
 */
    static char_u *
skip_grep_pat(exarg_T *eap)
{
    char_u	*p = eap->arg;

    if (*p != NUL && (eap->cmdidx == CMD_vimgrep || eap->cmdidx == CMD_lvimgrep
		|| eap->cmdidx == CMD_vimgrepadd
		|| eap->cmdidx == CMD_lvimgrepadd
		|| grep_internal(eap->cmdidx)))
    {
	p = skip_vimgrep_pat(p, NULL, NULL);
	if (p == NULL)
	    p = eap->arg;
    }
    return p;
}

/*
 * For the ":make" and ":grep" commands insert the 'makeprg'/'grepprg' option
 * in the command line, so that things like % get expanded.
 */
    static char_u *
replace_makeprg(exarg_T *eap, char_u *p, char_u **cmdlinep)
{
    char_u	*new_cmdline;
    char_u	*program;
    char_u	*pos;
    char_u	*ptr;
    int		len;
    int		i;

    /*
     * Don't do it when ":vimgrep" is used for ":grep".
     */
    if ((eap->cmdidx == CMD_make || eap->cmdidx == CMD_lmake
		     || eap->cmdidx == CMD_grep || eap->cmdidx == CMD_lgrep
		     || eap->cmdidx == CMD_grepadd
		     || eap->cmdidx == CMD_lgrepadd)
	    && !grep_internal(eap->cmdidx))
    {
	if (eap->cmdidx == CMD_grep || eap->cmdidx == CMD_lgrep
	    || eap->cmdidx == CMD_grepadd || eap->cmdidx == CMD_lgrepadd)
	{
	    if (*curbuf->b_p_gp == NUL)
		program = p_gp;
	    else
		program = curbuf->b_p_gp;
	}
	else
	{
	    if (*curbuf->b_p_mp == NUL)
		program = p_mp;
	    else
		program = curbuf->b_p_mp;
	}

	p = skipwhite(p);

	if ((pos = (char_u *)strstr((char *)program, "$*")) != NULL)
	{
	    /* replace $* by given arguments */
	    i = 1;
	    while ((pos = (char_u *)strstr((char *)pos + 2, "$*")) != NULL)
		++i;
	    len = (int)STRLEN(p);
	    new_cmdline = alloc(STRLEN(program) + i * (len - 2) + 1);
	    if (new_cmdline == NULL)
		return NULL;			/* out of memory */
	    ptr = new_cmdline;
	    while ((pos = (char_u *)strstr((char *)program, "$*")) != NULL)
	    {
		i = (int)(pos - program);
		STRNCPY(ptr, program, i);
		STRCPY(ptr += i, p);
		ptr += len;
		program = pos + 2;
	    }
	    STRCPY(ptr, program);
	}
	else
	{
	    new_cmdline = alloc(STRLEN(program) + STRLEN(p) + 2);
	    if (new_cmdline == NULL)
		return NULL;			/* out of memory */
	    STRCPY(new_cmdline, program);
	    STRCAT(new_cmdline, " ");
	    STRCAT(new_cmdline, p);
	}
	msg_make(p);

	/* 'eap->cmd' is not set here, because it is not used at CMD_make */
	vim_free(*cmdlinep);
	*cmdlinep = new_cmdline;
	p = new_cmdline;
    }
    return p;
}
#endif

/*
 * Expand file name in Ex command argument.
 * When an error is detected, "errormsgp" is set to a non-NULL pointer.
 * Return FAIL for failure, OK otherwise.
 */
    int
expand_filename(
    exarg_T	*eap,
    char_u	**cmdlinep,
    char	**errormsgp)
{
    int		has_wildcards;	/* need to expand wildcards */
    char_u	*repl;
    int		srclen;
    char_u	*p;
    int		n;
    int		escaped;

#ifdef FEAT_QUICKFIX
    /* Skip a regexp pattern for ":vimgrep[add] pat file..." */
    p = skip_grep_pat(eap);
#else
    p = eap->arg;
#endif

    /*
     * Decide to expand wildcards *before* replacing '%', '#', etc.  If
     * the file name contains a wildcard it should not cause expanding.
     * (it will be expanded anyway if there is a wildcard before replacing).
     */
    has_wildcards = mch_has_wildcard(p);
    while (*p != NUL)
    {
#ifdef FEAT_EVAL
	/* Skip over `=expr`, wildcards in it are not expanded. */
	if (p[0] == '`' && p[1] == '=')
	{
	    p += 2;
	    (void)skip_expr(&p);
	    if (*p == '`')
		++p;
	    continue;
	}
#endif
	/*
	 * Quick check if this cannot be the start of a special string.
	 * Also removes backslash before '%', '#' and '<'.
	 */
	if (vim_strchr((char_u *)"%#<", *p) == NULL)
	{
	    ++p;
	    continue;
	}

	/*
	 * Try to find a match at this position.
	 */
	repl = eval_vars(p, eap->arg, &srclen, &(eap->do_ecmd_lnum),
							 errormsgp, &escaped);
	if (*errormsgp != NULL)		/* error detected */
	    return FAIL;
	if (repl == NULL)		/* no match found */
	{
	    p += srclen;
	    continue;
	}

	/* Wildcards won't be expanded below, the replacement is taken
	 * literally.  But do expand "~/file", "~user/file" and "$HOME/file". */
	if (vim_strchr(repl, '$') != NULL || vim_strchr(repl, '~') != NULL)
	{
	    char_u *l = repl;

	    repl = expand_env_save(repl);
	    vim_free(l);
	}

	/* Need to escape white space et al. with a backslash.
	 * Don't do this for:
	 * - replacement that already has been escaped: "##"
	 * - shell commands (may have to use quotes instead).
	 * - non-unix systems when there is a single argument (spaces don't
	 *   separate arguments then).
	 */
	if (!eap->usefilter
		&& !escaped
		&& eap->cmdidx != CMD_bang
		&& eap->cmdidx != CMD_grep
		&& eap->cmdidx != CMD_grepadd
		&& eap->cmdidx != CMD_hardcopy
		&& eap->cmdidx != CMD_lgrep
		&& eap->cmdidx != CMD_lgrepadd
		&& eap->cmdidx != CMD_lmake
		&& eap->cmdidx != CMD_make
		&& eap->cmdidx != CMD_terminal
#ifndef UNIX
		&& !(eap->argt & EX_NOSPC)
#endif
		)
	{
	    char_u	*l;
#ifdef BACKSLASH_IN_FILENAME
	    /* Don't escape a backslash here, because rem_backslash() doesn't
	     * remove it later. */
	    static char_u *nobslash = (char_u *)" \t\"|";
# define ESCAPE_CHARS nobslash
#else
# define ESCAPE_CHARS escape_chars
#endif

	    for (l = repl; *l; ++l)
		if (vim_strchr(ESCAPE_CHARS, *l) != NULL)
		{
		    l = vim_strsave_escaped(repl, ESCAPE_CHARS);
		    if (l != NULL)
		    {
			vim_free(repl);
			repl = l;
		    }
		    break;
		}
	}

	/* For a shell command a '!' must be escaped. */
	if ((eap->usefilter || eap->cmdidx == CMD_bang
						|| eap->cmdidx == CMD_terminal)
			    && vim_strpbrk(repl, (char_u *)"!") != NULL)
	{
	    char_u	*l;

	    l = vim_strsave_escaped(repl, (char_u *)"!");
	    if (l != NULL)
	    {
		vim_free(repl);
		repl = l;
	    }
	}

	p = repl_cmdline(eap, p, srclen, repl, cmdlinep);
	vim_free(repl);
	if (p == NULL)
	    return FAIL;
    }

    /*
     * One file argument: Expand wildcards.
     * Don't do this with ":r !command" or ":w !command".
     */
    if ((eap->argt & EX_NOSPC) && !eap->usefilter)
    {
	/*
	 * May do this twice:
	 * 1. Replace environment variables.
	 * 2. Replace any other wildcards, remove backslashes.
	 */
	for (n = 1; n <= 2; ++n)
	{
	    if (n == 2)
	    {
		/*
		 * Halve the number of backslashes (this is Vi compatible).
		 * For Unix and OS/2, when wildcards are expanded, this is
		 * done by ExpandOne() below.
		 */
#if defined(UNIX)
		if (!has_wildcards)
#endif
		    backslash_halve(eap->arg);
	    }

	    if (has_wildcards)
	    {
		if (n == 1)
		{
		    /*
		     * First loop: May expand environment variables.  This
		     * can be done much faster with expand_env() than with
		     * something else (e.g., calling a shell).
		     * After expanding environment variables, check again
		     * if there are still wildcards present.
		     */
		    if (vim_strchr(eap->arg, '$') != NULL
			    || vim_strchr(eap->arg, '~') != NULL)
		    {
			expand_env_esc(eap->arg, NameBuff, MAXPATHL,
							    TRUE, TRUE, NULL);
			has_wildcards = mch_has_wildcard(NameBuff);
			p = NameBuff;
		    }
		    else
			p = NULL;
		}
		else /* n == 2 */
		{
		    expand_T	xpc;
		    int		options = WILD_LIST_NOTFOUND
					       | WILD_NOERROR | WILD_ADD_SLASH;

		    ExpandInit(&xpc);
		    xpc.xp_context = EXPAND_FILES;
		    if (p_wic)
			options += WILD_ICASE;
		    p = ExpandOne(&xpc, eap->arg, NULL,
						   options, WILD_EXPAND_FREE);
		    if (p == NULL)
			return FAIL;
		}
		if (p != NULL)
		{
		    (void)repl_cmdline(eap, eap->arg, (int)STRLEN(eap->arg),
								 p, cmdlinep);
		    if (n == 2)	/* p came from ExpandOne() */
			vim_free(p);
		}
	    }
	}
    }
    return OK;
}

/*
 * Replace part of the command line, keeping eap->cmd, eap->arg and
 * eap->nextcmd correct.
 * "src" points to the part that is to be replaced, of length "srclen".
 * "repl" is the replacement string.
 * Returns a pointer to the character after the replaced string.
 * Returns NULL for failure.
 */
    static char_u *
repl_cmdline(
    exarg_T	*eap,
    char_u	*src,
    int		srclen,
    char_u	*repl,
    char_u	**cmdlinep)
{
    int		len;
    int		i;
    char_u	*new_cmdline;

    /*
     * The new command line is build in new_cmdline[].
     * First allocate it.
     * Careful: a "+cmd" argument may have been NUL terminated.
     */
    len = (int)STRLEN(repl);
    i = (int)(src - *cmdlinep) + (int)STRLEN(src + srclen) + len + 3;
    if (eap->nextcmd != NULL)
	i += (int)STRLEN(eap->nextcmd);/* add space for next command */
    if ((new_cmdline = alloc(i)) == NULL)
	return NULL;			/* out of memory! */

    /*
     * Copy the stuff before the expanded part.
     * Copy the expanded stuff.
     * Copy what came after the expanded part.
     * Copy the next commands, if there are any.
     */
    i = (int)(src - *cmdlinep);	/* length of part before match */
    mch_memmove(new_cmdline, *cmdlinep, (size_t)i);

    mch_memmove(new_cmdline + i, repl, (size_t)len);
    i += len;				/* remember the end of the string */
    STRCPY(new_cmdline + i, src + srclen);
    src = new_cmdline + i;		/* remember where to continue */

    if (eap->nextcmd != NULL)		/* append next command */
    {
	i = (int)STRLEN(new_cmdline) + 1;
	STRCPY(new_cmdline + i, eap->nextcmd);
	eap->nextcmd = new_cmdline + i;
    }
    eap->cmd = new_cmdline + (eap->cmd - *cmdlinep);
    eap->arg = new_cmdline + (eap->arg - *cmdlinep);
    if (eap->do_ecmd_cmd != NULL && eap->do_ecmd_cmd != dollar_command)
	eap->do_ecmd_cmd = new_cmdline + (eap->do_ecmd_cmd - *cmdlinep);
    vim_free(*cmdlinep);
    *cmdlinep = new_cmdline;

    return src;
}

/*
 * Check for '|' to separate commands and '"' to start comments.
 */
    void
separate_nextcmd(exarg_T *eap)
{
    char_u	*p;

#ifdef FEAT_QUICKFIX
    p = skip_grep_pat(eap);
#else
    p = eap->arg;
#endif

    for ( ; *p; MB_PTR_ADV(p))
    {
	if (*p == Ctrl_V)
	{
	    if (eap->argt & (EX_CTRLV | EX_XFILE))
		++p;		/* skip CTRL-V and next char */
	    else
				/* remove CTRL-V and skip next char */
		STRMOVE(p, p + 1);
	    if (*p == NUL)		/* stop at NUL after CTRL-V */
		break;
	}

#ifdef FEAT_EVAL
	/* Skip over `=expr` when wildcards are expanded. */
	else if (p[0] == '`' && p[1] == '=' && (eap->argt & EX_XFILE))
	{
	    p += 2;
	    (void)skip_expr(&p);
	}
#endif

	/* Check for '"': start of comment or '|': next command */
	/* :@" and :*" do not start a comment!
	 * :redir @" doesn't either. */
	else if ((*p == '"' && !(eap->argt & EX_NOTRLCOM)
		    && ((eap->cmdidx != CMD_at && eap->cmdidx != CMD_star)
			|| p != eap->arg)
		    && (eap->cmdidx != CMD_redir
			|| p != eap->arg + 1 || p[-1] != '@'))
		|| *p == '|' || *p == '\n')
	{
	    /*
	     * We remove the '\' before the '|', unless EX_CTRLV is used
	     * AND 'b' is present in 'cpoptions'.
	     */
	    if ((vim_strchr(p_cpo, CPO_BAR) == NULL
			      || !(eap->argt & EX_CTRLV)) && *(p - 1) == '\\')
	    {
		STRMOVE(p - 1, p);	/* remove the '\' */
		--p;
	    }
	    else
	    {
		eap->nextcmd = check_nextcmd(p);
		*p = NUL;
		break;
	    }
	}
    }

    if (!(eap->argt & EX_NOTRLCOM))	/* remove trailing spaces */
	del_trailing_spaces(eap->arg);
}

/*
 * get + command from ex argument
 */
    static char_u *
getargcmd(char_u **argp)
{
    char_u *arg = *argp;
    char_u *command = NULL;

    if (*arg == '+')	    /* +[command] */
    {
	++arg;
	if (vim_isspace(*arg) || *arg == NUL)
	    command = dollar_command;
	else
	{
	    command = arg;
	    arg = skip_cmd_arg(command, TRUE);
	    if (*arg != NUL)
		*arg++ = NUL;		/* terminate command with NUL */
	}

	arg = skipwhite(arg);	/* skip over spaces */
	*argp = arg;
    }
    return command;
}

/*
 * Find end of "+command" argument.  Skip over "\ " and "\\".
 */
    char_u *
skip_cmd_arg(
    char_u *p,
    int	   rembs)	/* TRUE to halve the number of backslashes */
{
    while (*p && !vim_isspace(*p))
    {
	if (*p == '\\' && p[1] != NUL)
	{
	    if (rembs)
		STRMOVE(p, p + 1);
	    else
		++p;
	}
	MB_PTR_ADV(p);
    }
    return p;
}

    int
get_bad_opt(char_u *p, exarg_T *eap)
{
    if (STRICMP(p, "keep") == 0)
	eap->bad_char = BAD_KEEP;
    else if (STRICMP(p, "drop") == 0)
	eap->bad_char = BAD_DROP;
    else if (MB_BYTE2LEN(*p) == 1 && p[1] == NUL)
	eap->bad_char = *p;
    else
	return FAIL;
    return OK;
}

/*
 * Get "++opt=arg" argument.
 * Return FAIL or OK.
 */
    static int
getargopt(exarg_T *eap)
{
    char_u	*arg = eap->arg + 2;
    int		*pp = NULL;
    int		bad_char_idx;
    char_u	*p;

    /* ":edit ++[no]bin[ary] file" */
    if (STRNCMP(arg, "bin", 3) == 0 || STRNCMP(arg, "nobin", 5) == 0)
    {
	if (*arg == 'n')
	{
	    arg += 2;
	    eap->force_bin = FORCE_NOBIN;
	}
	else
	    eap->force_bin = FORCE_BIN;
	if (!checkforcmd(&arg, "binary", 3))
	    return FAIL;
	eap->arg = skipwhite(arg);
	return OK;
    }

    /* ":read ++edit file" */
    if (STRNCMP(arg, "edit", 4) == 0)
    {
	eap->read_edit = TRUE;
	eap->arg = skipwhite(arg + 4);
	return OK;
    }

    if (STRNCMP(arg, "ff", 2) == 0)
    {
	arg += 2;
	pp = &eap->force_ff;
    }
    else if (STRNCMP(arg, "fileformat", 10) == 0)
    {
	arg += 10;
	pp = &eap->force_ff;
    }
    else if (STRNCMP(arg, "enc", 3) == 0)
    {
	if (STRNCMP(arg, "encoding", 8) == 0)
	    arg += 8;
	else
	    arg += 3;
	pp = &eap->force_enc;
    }
    else if (STRNCMP(arg, "bad", 3) == 0)
    {
	arg += 3;
	pp = &bad_char_idx;
    }

    if (pp == NULL || *arg != '=')
	return FAIL;

    ++arg;
    *pp = (int)(arg - eap->cmd);
    arg = skip_cmd_arg(arg, FALSE);
    eap->arg = skipwhite(arg);
    *arg = NUL;

    if (pp == &eap->force_ff)
    {
	if (check_ff_value(eap->cmd + eap->force_ff) == FAIL)
	    return FAIL;
	eap->force_ff = eap->cmd[eap->force_ff];
    }
    else if (pp == &eap->force_enc)
    {
	/* Make 'fileencoding' lower case. */
	for (p = eap->cmd + eap->force_enc; *p != NUL; ++p)
	    *p = TOLOWER_ASC(*p);
    }
    else
    {
	/* Check ++bad= argument.  Must be a single-byte character, "keep" or
	 * "drop". */
	if (get_bad_opt(eap->cmd + bad_char_idx, eap) == FAIL)
	    return FAIL;
    }

    return OK;
}

    static void
ex_autocmd(exarg_T *eap)
{
    /*
     * Disallow autocommands from .exrc and .vimrc in current
     * directory for security reasons.
     */
    if (secure)
    {
	secure = 2;
	eap->errmsg = e_curdir;
    }
    else if (eap->cmdidx == CMD_autocmd)
	do_autocmd(eap->arg, eap->forceit);
    else
	do_augroup(eap->arg, eap->forceit);
}

/*
 * ":doautocmd": Apply the automatic commands to the current buffer.
 */
    static void
ex_doautocmd(exarg_T *eap)
{
    char_u	*arg = eap->arg;
    int		call_do_modelines = check_nomodeline(&arg);
    int		did_aucmd;

    (void)do_doautocmd(arg, TRUE, &did_aucmd);
    /* Only when there is no <nomodeline>. */
    if (call_do_modelines && did_aucmd)
	do_modelines(0);
}

/*
 * :[N]bunload[!] [N] [bufname] unload buffer
 * :[N]bdelete[!] [N] [bufname] delete buffer from buffer list
 * :[N]bwipeout[!] [N] [bufname] delete buffer really
 */
    static void
ex_bunload(exarg_T *eap)
{
    if (ERROR_IF_POPUP_WINDOW)
	return;
    eap->errmsg = do_bufdel(
	    eap->cmdidx == CMD_bdelete ? DOBUF_DEL
		: eap->cmdidx == CMD_bwipeout ? DOBUF_WIPE
		: DOBUF_UNLOAD, eap->arg,
	    eap->addr_count, (int)eap->line1, (int)eap->line2, eap->forceit);
}

/*
 * :[N]buffer [N]	to buffer N
 * :[N]sbuffer [N]	to buffer N
 */
    static void
ex_buffer(exarg_T *eap)
{
    if (ERROR_IF_POPUP_WINDOW)
	return;
    if (*eap->arg)
	eap->errmsg = e_trailing;
    else
    {
	if (eap->addr_count == 0)	/* default is current buffer */
	    goto_buffer(eap, DOBUF_CURRENT, FORWARD, 0);
	else
	    goto_buffer(eap, DOBUF_FIRST, FORWARD, (int)eap->line2);
	if (eap->do_ecmd_cmd != NULL)
	    do_cmdline_cmd(eap->do_ecmd_cmd);
    }
}

/*
 * :[N]bmodified [N]	to next mod. buffer
 * :[N]sbmodified [N]	to next mod. buffer
 */
    static void
ex_bmodified(exarg_T *eap)
{
    goto_buffer(eap, DOBUF_MOD, FORWARD, (int)eap->line2);
    if (eap->do_ecmd_cmd != NULL)
	do_cmdline_cmd(eap->do_ecmd_cmd);
}

/*
 * :[N]bnext [N]	to next buffer
 * :[N]sbnext [N]	split and to next buffer
 */
    static void
ex_bnext(exarg_T *eap)
{
    goto_buffer(eap, DOBUF_CURRENT, FORWARD, (int)eap->line2);
    if (eap->do_ecmd_cmd != NULL)
	do_cmdline_cmd(eap->do_ecmd_cmd);
}

/*
 * :[N]bNext [N]	to previous buffer
 * :[N]bprevious [N]	to previous buffer
 * :[N]sbNext [N]	split and to previous buffer
 * :[N]sbprevious [N]	split and to previous buffer
 */
    static void
ex_bprevious(exarg_T *eap)
{
    goto_buffer(eap, DOBUF_CURRENT, BACKWARD, (int)eap->line2);
    if (eap->do_ecmd_cmd != NULL)
	do_cmdline_cmd(eap->do_ecmd_cmd);
}

/*
 * :brewind		to first buffer
 * :bfirst		to first buffer
 * :sbrewind		split and to first buffer
 * :sbfirst		split and to first buffer
 */
    static void
ex_brewind(exarg_T *eap)
{
    goto_buffer(eap, DOBUF_FIRST, FORWARD, 0);
    if (eap->do_ecmd_cmd != NULL)
	do_cmdline_cmd(eap->do_ecmd_cmd);
}

/*
 * :blast		to last buffer
 * :sblast		split and to last buffer
 */
    static void
ex_blast(exarg_T *eap)
{
    goto_buffer(eap, DOBUF_LAST, BACKWARD, 0);
    if (eap->do_ecmd_cmd != NULL)
	do_cmdline_cmd(eap->do_ecmd_cmd);
}

    int
ends_excmd(int c)
{
    return (c == NUL || c == '|' || c == '"' || c == '\n');
}

#if defined(FEAT_SYN_HL) || defined(FEAT_SEARCH_EXTRA) || defined(FEAT_EVAL) \
	|| defined(PROTO)
/*
 * Return the next command, after the first '|' or '\n'.
 * Return NULL if not found.
 */
    char_u *
find_nextcmd(char_u *p)
{
    while (*p != '|' && *p != '\n')
    {
	if (*p == NUL)
	    return NULL;
	++p;
    }
    return (p + 1);
}
#endif

/*
 * Check if *p is a separator between Ex commands, skipping over white space.
 * Return NULL if it isn't, the following character if it is.
 */
    char_u *
check_nextcmd(char_u *p)
{
    char_u *s = skipwhite(p);

    if (*s == '|' || *s == '\n')
	return (s + 1);
    else
	return NULL;
}

/*
 * - if there are more files to edit
 * - and this is the last window
 * - and forceit not used
 * - and not repeated twice on a row
 *    return FAIL and give error message if 'message' TRUE
 * return OK otherwise
 */
    static int
check_more(
    int message,	    /* when FALSE check only, no messages */
    int forceit)
{
    int	    n = ARGCOUNT - curwin->w_arg_idx - 1;

    if (!forceit && only_one_window()
	    && ARGCOUNT > 1 && !arg_had_last && n >= 0 && quitmore == 0)
    {
	if (message)
	{
#if defined(FEAT_GUI_DIALOG) || defined(FEAT_CON_DIALOG)
	    if ((p_confirm || cmdmod.confirm) && curbuf->b_fname != NULL)
	    {
		char_u	buff[DIALOG_MSG_SIZE];

		vim_snprintf((char *)buff, DIALOG_MSG_SIZE,
			NGETTEXT("%d more file to edit.  Quit anyway?",
			    "%d more files to edit.  Quit anyway?", n), n);
		if (vim_dialog_yesno(VIM_QUESTION, NULL, buff, 1) == VIM_YES)
		    return OK;
		return FAIL;
	    }
#endif
	    semsg(NGETTEXT("E173: %d more file to edit",
			"E173: %d more files to edit", n), n);
	    quitmore = 2;	    /* next try to quit is allowed */
	}
	return FAIL;
    }
    return OK;
}

/*
 * Function given to ExpandGeneric() to obtain the list of command names.
 */
    char_u *
get_command_name(expand_T *xp UNUSED, int idx)
{
    if (idx >= (int)CMD_SIZE)
	return get_user_command_name(idx);
    return cmdnames[idx].cmd_name;
}

    static void
ex_colorscheme(exarg_T *eap)
{
    if (*eap->arg == NUL)
    {
#ifdef FEAT_EVAL
	char_u *expr = vim_strsave((char_u *)"g:colors_name");
	char_u *p = NULL;

	if (expr != NULL)
	{
	    ++emsg_off;
	    p = eval_to_string(expr, NULL, FALSE);
	    --emsg_off;
	    vim_free(expr);
	}
	if (p != NULL)
	{
	    msg((char *)p);
	    vim_free(p);
	}
	else
	    msg("default");
#else
	msg(_("unknown"));
#endif
    }
    else if (load_colors(eap->arg) == FAIL)
	semsg(_("E185: Cannot find color scheme '%s'"), eap->arg);

#ifdef FEAT_VTP
    else if (has_vtp_working())
    {
	// background color change requires clear + redraw
	update_screen(CLEAR);
	redrawcmd();
    }
#endif
}

    static void
ex_highlight(exarg_T *eap)
{
    if (*eap->arg == NUL && eap->cmd[2] == '!')
	msg(_("Greetings, Vim user!"));
    do_highlight(eap->arg, eap->forceit, FALSE);
}


/*
 * Call this function if we thought we were going to exit, but we won't
 * (because of an error).  May need to restore the terminal mode.
 */
    void
not_exiting(void)
{
    exiting = FALSE;
    settmode(TMODE_RAW);
}

    static int
before_quit_autocmds(win_T *wp, int quit_all, int forceit)
{
    apply_autocmds(EVENT_QUITPRE, NULL, NULL, FALSE, wp->w_buffer);

    // Bail out when autocommands closed the window.
    // Refuse to quit when the buffer in the last window is being closed (can
    // only happen in autocommands).
    if (!win_valid(wp)
	    || curbuf_locked()
	    || (wp->w_buffer->b_nwindows == 1 && wp->w_buffer->b_locked > 0))
	return TRUE;

    if (quit_all || (check_more(FALSE, forceit) == OK && only_one_window()))
    {
	apply_autocmds(EVENT_EXITPRE, NULL, NULL, FALSE, curbuf);
	// Refuse to quit when locked or when the window was closed or the
	// buffer in the last window is being closed (can only happen in
	// autocommands).
	if (!win_valid(wp) || curbuf_locked()
			  || (curbuf->b_nwindows == 1 && curbuf->b_locked > 0))
	    return TRUE;
    }

    return FALSE;
}

/*
 * ":quit": quit current window, quit Vim if the last window is closed.
 * ":{nr}quit": quit window {nr}
 * Also used when closing a terminal window that's the last one.
 */
    void
ex_quit(exarg_T *eap)
{
    win_T	*wp;

#ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
    {
	cmdwin_result = Ctrl_C;
	return;
    }
#endif
    /* Don't quit while editing the command line. */
    if (text_locked())
    {
	text_locked_msg();
	return;
    }
    if (eap->addr_count > 0)
    {
	int	wnr = eap->line2;

	for (wp = firstwin; wp->w_next != NULL; wp = wp->w_next)
	    if (--wnr <= 0)
		break;
    }
    else
	wp = curwin;

    /* Refuse to quit when locked. */
    if (curbuf_locked())
	return;

    /* Trigger QuitPre and maybe ExitPre */
    if (before_quit_autocmds(wp, FALSE, eap->forceit))
	return;

#ifdef FEAT_NETBEANS_INTG
    netbeansForcedQuit = eap->forceit;
#endif

    /*
     * If there are more files or windows we won't exit.
     */
    if (check_more(FALSE, eap->forceit) == OK && only_one_window())
	exiting = TRUE;
    if ((!buf_hide(wp->w_buffer)
		&& check_changed(wp->w_buffer, (p_awa ? CCGD_AW : 0)
				       | (eap->forceit ? CCGD_FORCEIT : 0)
				       | CCGD_EXCMD))
	    || check_more(TRUE, eap->forceit) == FAIL
	    || (only_one_window() && check_changed_any(eap->forceit, TRUE)))
    {
	not_exiting();
    }
    else
    {
	/* quit last window
	 * Note: only_one_window() returns true, even so a help window is
	 * still open. In that case only quit, if no address has been
	 * specified. Example:
	 * :h|wincmd w|1q     - don't quit
	 * :h|wincmd w|q      - quit
	 */
	if (only_one_window() && (ONE_WINDOW || eap->addr_count == 0))
	    getout(0);
	not_exiting();
#ifdef FEAT_GUI
	need_mouse_correct = TRUE;
#endif
	/* close window; may free buffer */
	win_close(wp, !buf_hide(wp->w_buffer) || eap->forceit);
    }
}

/*
 * ":cquit".
 */
    static void
ex_cquit(exarg_T *eap UNUSED)
{
    getout(1);	/* this does not always pass on the exit code to the Manx
		   compiler. why? */
}

/*
 * ":qall": try to quit all windows
 */
    static void
ex_quit_all(exarg_T *eap)
{
# ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
    {
	if (eap->forceit)
	    cmdwin_result = K_XF1;	/* ex_window() takes care of this */
	else
	    cmdwin_result = K_XF2;
	return;
    }
# endif

    /* Don't quit while editing the command line. */
    if (text_locked())
    {
	text_locked_msg();
	return;
    }

    if (before_quit_autocmds(curwin, TRUE, eap->forceit))
	return;

    exiting = TRUE;
    if (eap->forceit || !check_changed_any(FALSE, FALSE))
	getout(0);
    not_exiting();
}

/*
 * ":close": close current window, unless it is the last one
 */
    static void
ex_close(exarg_T *eap)
{
    win_T	*win;
    int		winnr = 0;
#ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
	cmdwin_result = Ctrl_C;
    else
#endif
	if (!text_locked() && !curbuf_locked())
	{
	    if (eap->addr_count == 0)
		ex_win_close(eap->forceit, curwin, NULL);
	    else
	    {
		FOR_ALL_WINDOWS(win)
		{
		    winnr++;
		    if (winnr == eap->line2)
			break;
		}
		if (win == NULL)
		    win = lastwin;
		ex_win_close(eap->forceit, win, NULL);
	    }
	}
}

#ifdef FEAT_QUICKFIX
/*
 * ":pclose": Close any preview window.
 */
    static void
ex_pclose(exarg_T *eap)
{
    win_T	*win;

    // First close any normal window.
    FOR_ALL_WINDOWS(win)
	if (win->w_p_pvw)
	{
	    ex_win_close(eap->forceit, win, NULL);
	    return;
	}
# ifdef FEAT_TEXT_PROP
    // Also when 'previewpopup' is empty, it might have been cleared.
    popup_close_preview();
# endif
}
#endif

/*
 * Close window "win" and take care of handling closing the last window for a
 * modified buffer.
 */
    static void
ex_win_close(
    int		forceit,
    win_T	*win,
    tabpage_T	*tp)		/* NULL or the tab page "win" is in */
{
    int		need_hide;
    buf_T	*buf = win->w_buffer;

    need_hide = (bufIsChanged(buf) && buf->b_nwindows <= 1);
    if (need_hide && !buf_hide(buf) && !forceit)
    {
#if defined(FEAT_GUI_DIALOG) || defined(FEAT_CON_DIALOG)
	if ((p_confirm || cmdmod.confirm) && p_write)
	{
	    bufref_T bufref;

	    set_bufref(&bufref, buf);
	    dialog_changed(buf, FALSE);
	    if (bufref_valid(&bufref) && bufIsChanged(buf))
		return;
	    need_hide = FALSE;
	}
	else
#endif
	{
	    no_write_message();
	    return;
	}
    }

#ifdef FEAT_GUI
    need_mouse_correct = TRUE;
#endif

    /* free buffer when not hiding it or when it's a scratch buffer */
    if (tp == NULL)
	win_close(win, !need_hide && !buf_hide(buf));
    else
	win_close_othertab(win, !need_hide && !buf_hide(buf), tp);
}

/*
 * Handle the argument for a tabpage related ex command.
 * Returns a tabpage number.
 * When an error is encountered then eap->errmsg is set.
 */
    static int
get_tabpage_arg(exarg_T *eap)
{
    int tab_number;
    int unaccept_arg0 = (eap->cmdidx == CMD_tabmove) ? 0 : 1;

    if (eap->arg && *eap->arg != NUL)
    {
	char_u *p = eap->arg;
	char_u *p_save;
	int    relative = 0; /* argument +N/-N means: go to N places to the
			      * right/left relative to the current position. */

	if (*p == '-')
	{
	    relative = -1;
	    p++;
	}
	else if (*p == '+')
	{
	    relative = 1;
	    p++;
	}

	p_save = p;
	tab_number = getdigits(&p);

	if (relative == 0)
	{
	    if (STRCMP(p, "$") == 0)
		tab_number = LAST_TAB_NR;
	    else if (p == p_save || *p_save == '-' || *p != NUL
		    || tab_number > LAST_TAB_NR)
	    {
		/* No numbers as argument. */
		eap->errmsg = e_invarg;
		goto theend;
	    }
	}
	else
	{
	    if (*p_save == NUL)
		tab_number = 1;
	    else if (p == p_save || *p_save == '-' || *p != NUL
		    || tab_number == 0)
	    {
		/* No numbers as argument. */
		eap->errmsg = e_invarg;
		goto theend;
	    }
	    tab_number = tab_number * relative + tabpage_index(curtab);
	    if (!unaccept_arg0 && relative == -1)
		--tab_number;
	}
	if (tab_number < unaccept_arg0 || tab_number > LAST_TAB_NR)
	    eap->errmsg = e_invarg;
    }
    else if (eap->addr_count > 0)
    {
	if (unaccept_arg0 && eap->line2 == 0)
	{
	    eap->errmsg = e_invrange;
	    tab_number = 0;
	}
	else
	{
	    tab_number = eap->line2;
	    if (!unaccept_arg0 && *skipwhite(*eap->cmdlinep) == '-')
	    {
		--tab_number;
		if (tab_number < unaccept_arg0)
		    eap->errmsg = e_invarg;
	    }
	}
    }
    else
    {
	switch (eap->cmdidx)
	{
	case CMD_tabnext:
	    tab_number = tabpage_index(curtab) + 1;
	    if (tab_number > LAST_TAB_NR)
		tab_number = 1;
	    break;
	case CMD_tabmove:
	    tab_number = LAST_TAB_NR;
	    break;
	default:
	    tab_number = tabpage_index(curtab);
	}
    }

theend:
    return tab_number;
}

/*
 * ":tabclose": close current tab page, unless it is the last one.
 * ":tabclose N": close tab page N.
 */
    static void
ex_tabclose(exarg_T *eap)
{
    tabpage_T	*tp;
    int		tab_number;

# ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
	cmdwin_result = K_IGNORE;
    else
# endif
	if (first_tabpage->tp_next == NULL)
	    emsg(_("E784: Cannot close last tab page"));
	else
	{
	    tab_number = get_tabpage_arg(eap);
	    if (eap->errmsg == NULL)
	    {
		tp = find_tabpage(tab_number);
		if (tp == NULL)
		{
		    beep_flush();
		    return;
		}
		if (tp != curtab)
		{
		    tabpage_close_other(tp, eap->forceit);
		    return;
		}
		else if (!text_locked() && !curbuf_locked())
		    tabpage_close(eap->forceit);
	    }
	}
}

/*
 * ":tabonly": close all tab pages except the current one
 */
    static void
ex_tabonly(exarg_T *eap)
{
    tabpage_T	*tp;
    int		done;
    int		tab_number;

# ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
	cmdwin_result = K_IGNORE;
    else
# endif
	if (first_tabpage->tp_next == NULL)
	    msg(_("Already only one tab page"));
	else
	{
	    tab_number = get_tabpage_arg(eap);
	    if (eap->errmsg == NULL)
	    {
		goto_tabpage(tab_number);
		/* Repeat this up to a 1000 times, because autocommands may
		 * mess up the lists. */
		for (done = 0; done < 1000; ++done)
		{
		    FOR_ALL_TABPAGES(tp)
			if (tp->tp_topframe != topframe)
			{
			    tabpage_close_other(tp, eap->forceit);
			    /* if we failed to close it quit */
			    if (valid_tabpage(tp))
				done = 1000;
			    /* start over, "tp" is now invalid */
			    break;
			}
		    if (first_tabpage->tp_next == NULL)
			break;
		}
	    }
	}
}

/*
 * Close the current tab page.
 */
    void
tabpage_close(int forceit)
{
    /* First close all the windows but the current one.  If that worked then
     * close the last window in this tab, that will close it. */
    if (!ONE_WINDOW)
	close_others(TRUE, forceit);
    if (ONE_WINDOW)
	ex_win_close(forceit, curwin, NULL);
# ifdef FEAT_GUI
    need_mouse_correct = TRUE;
# endif
}

/*
 * Close tab page "tp", which is not the current tab page.
 * Note that autocommands may make "tp" invalid.
 * Also takes care of the tab pages line disappearing when closing the
 * last-but-one tab page.
 */
    void
tabpage_close_other(tabpage_T *tp, int forceit)
{
    int		done = 0;
    win_T	*wp;
    int		h = tabline_height();

    /* Limit to 1000 windows, autocommands may add a window while we close
     * one.  OK, so I'm paranoid... */
    while (++done < 1000)
    {
	wp = tp->tp_firstwin;
	ex_win_close(forceit, wp, tp);

	/* Autocommands may delete the tab page under our fingers and we may
	 * fail to close a window with a modified buffer. */
	if (!valid_tabpage(tp) || tp->tp_firstwin == wp)
	    break;
    }

    apply_autocmds(EVENT_TABCLOSED, NULL, NULL, FALSE, curbuf);

    redraw_tabline = TRUE;
    if (h != tabline_height())
	shell_new_rows();
}

/*
 * ":only".
 */
    static void
ex_only(exarg_T *eap)
{
    win_T   *wp;
    int	    wnr;
# ifdef FEAT_GUI
    need_mouse_correct = TRUE;
# endif
    if (eap->addr_count > 0)
    {
	wnr = eap->line2;
	for (wp = firstwin; --wnr > 0; )
	{
	    if (wp->w_next == NULL)
		break;
	    else
		wp = wp->w_next;
	}
	win_goto(wp);
    }
    close_others(TRUE, eap->forceit);
}

    static void
ex_hide(exarg_T *eap UNUSED)
{
    /* ":hide" or ":hide | cmd": hide current window */
    if (!eap->skip)
    {
#ifdef FEAT_GUI
	need_mouse_correct = TRUE;
#endif
	if (eap->addr_count == 0)
	    win_close(curwin, FALSE);	/* don't free buffer */
	else
	{
	    int	winnr = 0;
	    win_T	*win;

	    FOR_ALL_WINDOWS(win)
	    {
		winnr++;
		if (winnr == eap->line2)
		    break;
	    }
	    if (win == NULL)
		win = lastwin;
	    win_close(win, FALSE);
	}
    }
}

/*
 * ":stop" and ":suspend": Suspend Vim.
 */
    static void
ex_stop(exarg_T *eap)
{
    /*
     * Disallow suspending for "rvim".
     */
    if (!check_restricted())
    {
	if (!eap->forceit)
	    autowrite_all();
	windgoto((int)Rows - 1, 0);
	out_char('\n');
	out_flush();
	stoptermcap();
	out_flush();		/* needed for SUN to restore xterm buffer */
#ifdef FEAT_TITLE
	mch_restore_title(SAVE_RESTORE_BOTH);	/* restore window titles */
#endif
	ui_suspend();		/* call machine specific function */
#ifdef FEAT_TITLE
	maketitle();
	resettitle();		/* force updating the title */
#endif
	starttermcap();
	scroll_start();		/* scroll screen before redrawing */
	redraw_later_clear();
	shell_resized();	/* may have resized window */
    }
}

/*
 * ":exit", ":xit" and ":wq": Write file and quite the current window.
 */
    static void
ex_exit(exarg_T *eap)
{
#ifdef FEAT_CMDWIN
    if (cmdwin_type != 0)
    {
	cmdwin_result = Ctrl_C;
	return;
    }
#endif
    /* Don't quit while editing the command line. */
    if (text_locked())
    {
	text_locked_msg();
	return;
    }

    if (before_quit_autocmds(curwin, FALSE, eap->forceit))
	return;

    /*
     * if more files or windows we won't exit
     */
    if (check_more(FALSE, eap->forceit) == OK && only_one_window())
	exiting = TRUE;
    if (       ((eap->cmdidx == CMD_wq
		    || curbufIsChanged())
		&& do_write(eap) == FAIL)
	    || check_more(TRUE, eap->forceit) == FAIL
	    || (only_one_window() && check_changed_any(eap->forceit, FALSE)))
    {
	not_exiting();
    }
    else
    {
	if (only_one_window())	    /* quit last window, exit Vim */
	    getout(0);
	not_exiting();
# ifdef FEAT_GUI
	need_mouse_correct = TRUE;
# endif
	/* Quit current window, may free the buffer. */
	win_close(curwin, !buf_hide(curwin->w_buffer));
    }
}

/*
 * ":print", ":list", ":number".
 */
    static void
ex_print(exarg_T *eap)
{
    if (curbuf->b_ml.ml_flags & ML_EMPTY)
	emsg(_(e_emptybuf));
    else
    {
	for ( ;!got_int; ui_breakcheck())
	{
	    print_line(eap->line1,
		    (eap->cmdidx == CMD_number || eap->cmdidx == CMD_pound
						 || (eap->flags & EXFLAG_NR)),
		    eap->cmdidx == CMD_list || (eap->flags & EXFLAG_LIST));
	    if (++eap->line1 > eap->line2)
		break;
	    out_flush();	    /* show one line at a time */
	}
	setpcmark();
	/* put cursor at last line */
	curwin->w_cursor.lnum = eap->line2;
	beginline(BL_SOL | BL_FIX);
    }

    ex_no_reprint = TRUE;
}

#ifdef FEAT_BYTEOFF
    static void
ex_goto(exarg_T *eap)
{
    goto_byte(eap->line2);
}
#endif

/*
 * ":shell".
 */
    static void
ex_shell(exarg_T *eap UNUSED)
{
    do_shell(NULL, 0);
}

#if defined(HAVE_DROP_FILE) || defined(PROTO)

static int drop_busy = FALSE;
static int drop_filec;
static char_u **drop_filev = NULL;
static int drop_split;
static void (*drop_callback)(void *);
static void *drop_cookie;

    static void
handle_drop_internal(void)
{
    exarg_T	ea;
    int		save_msg_scroll = msg_scroll;

    // Setting the argument list may cause screen updates and being called
    // recursively.  Avoid that by setting drop_busy.
    drop_busy = TRUE;

    /* Check whether the current buffer is changed. If so, we will need
     * to split the current window or data could be lost.
     * We don't need to check if the 'hidden' option is set, as in this
     * case the buffer won't be lost.
     */
    if (!buf_hide(curbuf) && !drop_split)
    {
	++emsg_off;
	drop_split = check_changed(curbuf, CCGD_AW);
	--emsg_off;
    }
    if (drop_split)
    {
	if (win_split(0, 0) == FAIL)
	    return;
	RESET_BINDING(curwin);

	/* When splitting the window, create a new alist.  Otherwise the
	 * existing one is overwritten. */
	alist_unlink(curwin->w_alist);
	alist_new();
    }

    /*
     * Set up the new argument list.
     */
    alist_set(ALIST(curwin), drop_filec, drop_filev, FALSE, NULL, 0);

    /*
     * Move to the first file.
     */
    /* Fake up a minimal "next" command for do_argfile() */
    vim_memset(&ea, 0, sizeof(ea));
    ea.cmd = (char_u *)"next";
    do_argfile(&ea, 0);

    /* do_ecmd() may set need_start_insertmode, but since we never left Insert
     * mode that is not needed here. */
    need_start_insertmode = FALSE;

    /* Restore msg_scroll, otherwise a following command may cause scrolling
     * unexpectedly.  The screen will be redrawn by the caller, thus
     * msg_scroll being set by displaying a message is irrelevant. */
    msg_scroll = save_msg_scroll;

    if (drop_callback != NULL)
	drop_callback(drop_cookie);

    drop_filev = NULL;
    drop_busy = FALSE;
}

/*
 * Handle a file drop. The code is here because a drop is *nearly* like an
 * :args command, but not quite (we have a list of exact filenames, so we
 * don't want to (a) parse a command line, or (b) expand wildcards). So the
 * code is very similar to :args and hence needs access to a lot of the static
 * functions in this file.
 *
 * The "filev" list must have been allocated using alloc(), as should each item
 * in the list. This function takes over responsibility for freeing the "filev"
 * list.
 */
    void
handle_drop(
    int		filec,		// the number of files dropped
    char_u	**filev,	// the list of files dropped
    int		split,		// force splitting the window
    void	(*callback)(void *), // to be called after setting the argument
				     // list
    void	*cookie)	// argument for "callback" (allocated)
{
    // Cannot handle recursive drops, finish the pending one.
    if (drop_busy)
    {
	FreeWild(filec, filev);
	vim_free(cookie);
	return;
    }

    // When calling handle_drop() more than once in a row we only use the last
    // one.
    if (drop_filev != NULL)
    {
	FreeWild(drop_filec, drop_filev);
	vim_free(drop_cookie);
    }

    drop_filec = filec;
    drop_filev = filev;
    drop_split = split;
    drop_callback = callback;
    drop_cookie = cookie;

    // Postpone this when:
    // - editing the command line
    // - not possible to change the current buffer
    // - updating the screen
    // As it may change buffers and window structures that are in use and cause
    // freed memory to be used.
    if (text_locked() || curbuf_locked() || updating_screen)
	return;

    handle_drop_internal();
}

/*
 * To be called when text is unlocked, curbuf is unlocked or updating_screen is
 * reset: Handle a postponed drop.
 */
    void
handle_any_postponed_drop(void)
{
    if (!drop_busy && drop_filev != NULL
		     && !text_locked() && !curbuf_locked() && !updating_screen)
	handle_drop_internal();
}
#endif

/*
 * ":preserve".
 */
    static void
ex_preserve(exarg_T *eap UNUSED)
{
    curbuf->b_flags |= BF_PRESERVED;
    ml_preserve(curbuf, TRUE);
}

/*
 * ":recover".
 */
    static void
ex_recover(exarg_T *eap)
{
    /* Set recoverymode right away to avoid the ATTENTION prompt. */
    recoverymode = TRUE;
    if (!check_changed(curbuf, (p_awa ? CCGD_AW : 0)
			     | CCGD_MULTWIN
			     | (eap->forceit ? CCGD_FORCEIT : 0)
			     | CCGD_EXCMD)

	    && (*eap->arg == NUL
			     || setfname(curbuf, eap->arg, NULL, TRUE) == OK))
	ml_recover(TRUE);
    recoverymode = FALSE;
}

/*
 * Command modifier used in a wrong way.
 */
    static void
ex_wrongmodifier(exarg_T *eap)
{
    eap->errmsg = e_invcmd;
}

/*
 * :sview [+command] file	split window with new file, read-only
 * :split [[+command] file]	split window with current or new file
 * :vsplit [[+command] file]	split window vertically with current or new file
 * :new [[+command] file]	split window with no or new file
 * :vnew [[+command] file]	split vertically window with no or new file
 * :sfind [+command] file	split window with file in 'path'
 *
 * :tabedit			open new Tab page with empty window
 * :tabedit [+command] file	open new Tab page and edit "file"
 * :tabnew [[+command] file]	just like :tabedit
 * :tabfind [+command] file	open new Tab page and find "file"
 */
    void
ex_splitview(exarg_T *eap)
{
    win_T	*old_curwin = curwin;
#if defined(FEAT_SEARCHPATH) || defined(FEAT_BROWSE)
    char_u	*fname = NULL;
#endif
#ifdef FEAT_BROWSE
    int		browse_flag = cmdmod.browse;
#endif
    int		use_tab = eap->cmdidx == CMD_tabedit
		       || eap->cmdidx == CMD_tabfind
		       || eap->cmdidx == CMD_tabnew;

    if (ERROR_IF_POPUP_WINDOW)
	return;

#ifdef FEAT_GUI
    need_mouse_correct = TRUE;
#endif

#ifdef FEAT_QUICKFIX
    /* A ":split" in the quickfix window works like ":new".  Don't want two
     * quickfix windows.  But it's OK when doing ":tab split". */
    if (bt_quickfix(curbuf) && cmdmod.tab == 0)
    {
	if (eap->cmdidx == CMD_split)
	    eap->cmdidx = CMD_new;
	if (eap->cmdidx == CMD_vsplit)
	    eap->cmdidx = CMD_vnew;
    }
#endif

#ifdef FEAT_SEARCHPATH
    if (eap->cmdidx == CMD_sfind || eap->cmdidx == CMD_tabfind)
    {
	fname = find_file_in_path(eap->arg, (int)STRLEN(eap->arg),
					  FNAME_MESS, TRUE, curbuf->b_ffname);
	if (fname == NULL)
	    goto theend;
	eap->arg = fname;
    }
# ifdef FEAT_BROWSE
    else
# endif
#endif
#ifdef FEAT_BROWSE
    if (cmdmod.browse
	    && eap->cmdidx != CMD_vnew
	    && eap->cmdidx != CMD_new)
    {
	if (
# ifdef FEAT_GUI
	    !gui.in_use &&
# endif
		au_has_group((char_u *)"FileExplorer"))
	{
	    /* No browsing supported but we do have the file explorer:
	     * Edit the directory. */
	    if (*eap->arg == NUL || !mch_isdir(eap->arg))
		eap->arg = (char_u *)".";
	}
	else
	{
	    fname = do_browse(0, (char_u *)(use_tab
			? _("Edit File in new tab page")
			: _("Edit File in new window")),
					  eap->arg, NULL, NULL, NULL, curbuf);
	    if (fname == NULL)
		goto theend;
	    eap->arg = fname;
	}
    }
    cmdmod.browse = FALSE;	/* Don't browse again in do_ecmd(). */
#endif

    /*
     * Either open new tab page or split the window.
     */
    if (use_tab)
    {
	if (win_new_tabpage(cmdmod.tab != 0 ? cmdmod.tab
			 : eap->addr_count == 0 ? 0
					       : (int)eap->line2 + 1) != FAIL)
	{
	    do_exedit(eap, old_curwin);

	    /* set the alternate buffer for the window we came from */
	    if (curwin != old_curwin
		    && win_valid(old_curwin)
		    && old_curwin->w_buffer != curbuf
		    && !cmdmod.keepalt)
		old_curwin->w_alt_fnum = curbuf->b_fnum;
	}
    }
    else if (win_split(eap->addr_count > 0 ? (int)eap->line2 : 0,
				     *eap->cmd == 'v' ? WSP_VERT : 0) != FAIL)
    {
	/* Reset 'scrollbind' when editing another file, but keep it when
	 * doing ":split" without arguments. */
	if (*eap->arg != NUL
# ifdef FEAT_BROWSE
		|| cmdmod.browse
# endif
	   )
	    RESET_BINDING(curwin);
	else
	    do_check_scrollbind(FALSE);
	do_exedit(eap, old_curwin);
    }

# ifdef FEAT_BROWSE
    cmdmod.browse = browse_flag;
# endif

# if defined(FEAT_SEARCHPATH) || defined(FEAT_BROWSE)
theend:
    vim_free(fname);
# endif
}

/*
 * Open a new tab page.
 */
    void
tabpage_new(void)
{
    exarg_T	ea;

    vim_memset(&ea, 0, sizeof(ea));
    ea.cmdidx = CMD_tabnew;
    ea.cmd = (char_u *)"tabn";
    ea.arg = (char_u *)"";
    ex_splitview(&ea);
}

/*
 * :tabnext command
 */
    static void
ex_tabnext(exarg_T *eap)
{
    int tab_number;

    if (ERROR_IF_POPUP_WINDOW)
	return;
    switch (eap->cmdidx)
    {
	case CMD_tabfirst:
	case CMD_tabrewind:
	    goto_tabpage(1);
	    break;
	case CMD_tablast:
	    goto_tabpage(9999);
	    break;
	case CMD_tabprevious:
	case CMD_tabNext:
	    if (eap->arg && *eap->arg != NUL)
	    {
		char_u *p = eap->arg;
		char_u *p_save = p;

		tab_number = getdigits(&p);
		if (p == p_save || *p_save == '-' || *p != NUL
			    || tab_number == 0)
		{
		    /* No numbers as argument. */
		    eap->errmsg = e_invarg;
		    return;
		}
	    }
	    else
	    {
		if (eap->addr_count == 0)
		    tab_number = 1;
		else
		{
		    tab_number = eap->line2;
		    if (tab_number < 1)
		    {
			eap->errmsg = e_invrange;
			return;
		    }
		}
	    }
	    goto_tabpage(-tab_number);
	    break;
	default: /* CMD_tabnext */
	    tab_number = get_tabpage_arg(eap);
	    if (eap->errmsg == NULL)
		goto_tabpage(tab_number);
	    break;
    }
}

/*
 * :tabmove command
 */
    static void
ex_tabmove(exarg_T *eap)
{
    int tab_number;

    tab_number = get_tabpage_arg(eap);
    if (eap->errmsg == NULL)
	tabpage_move(tab_number);
}

/*
 * :tabs command: List tabs and their contents.
 */
    static void
ex_tabs(exarg_T *eap UNUSED)
{
    tabpage_T	*tp;
    win_T	*wp;
    int		tabcount = 1;

    msg_start();
    msg_scroll = TRUE;
    for (tp = first_tabpage; tp != NULL && !got_int; tp = tp->tp_next)
    {
	msg_putchar('\n');
	vim_snprintf((char *)IObuff, IOSIZE, _("Tab page %d"), tabcount++);
	msg_outtrans_attr(IObuff, HL_ATTR(HLF_T));
	out_flush();	    /* output one line at a time */
	ui_breakcheck();

	if (tp  == curtab)
	    wp = firstwin;
	else
	    wp = tp->tp_firstwin;
	for ( ; wp != NULL && !got_int; wp = wp->w_next)
	{
	    msg_putchar('\n');
	    msg_putchar(wp == curwin ? '>' : ' ');
	    msg_putchar(' ');
	    msg_putchar(bufIsChanged(wp->w_buffer) ? '+' : ' ');
	    msg_putchar(' ');
	    if (buf_spname(wp->w_buffer) != NULL)
		vim_strncpy(IObuff, buf_spname(wp->w_buffer), IOSIZE - 1);
	    else
		home_replace(wp->w_buffer, wp->w_buffer->b_fname,
							IObuff, IOSIZE, TRUE);
	    msg_outtrans(IObuff);
	    out_flush();	    /* output one line at a time */
	    ui_breakcheck();
	}
    }
}

/*
 * ":mode": Set screen mode.
 * If no argument given, just get the screen size and redraw.
 */
    static void
ex_mode(exarg_T *eap)
{
    if (*eap->arg == NUL)
	shell_resized();
    else
	mch_screenmode(eap->arg);
}

/*
 * ":resize".
 * set, increment or decrement current window height
 */
    static void
ex_resize(exarg_T *eap)
{
    int		n;
    win_T	*wp = curwin;

    if (eap->addr_count > 0)
    {
	n = eap->line2;
	for (wp = firstwin; wp->w_next != NULL && --n > 0; wp = wp->w_next)
	    ;
    }

# ifdef FEAT_GUI
    need_mouse_correct = TRUE;
# endif
    n = atol((char *)eap->arg);
    if (cmdmod.split & WSP_VERT)
    {
	if (*eap->arg == '-' || *eap->arg == '+')
	    n += curwin->w_width;
	else if (n == 0 && eap->arg[0] == NUL)	/* default is very wide */
	    n = 9999;
	win_setwidth_win((int)n, wp);
    }
    else
    {
	if (*eap->arg == '-' || *eap->arg == '+')
	    n += curwin->w_height;
	else if (n == 0 && eap->arg[0] == NUL)	/* default is very high */
	    n = 9999;
	win_setheight_win((int)n, wp);
    }
}

/*
 * ":find [+command] <file>" command.
 */
    static void
ex_find(exarg_T *eap)
{
#ifdef FEAT_SEARCHPATH
    char_u	*fname;
    int		count;

    fname = find_file_in_path(eap->arg, (int)STRLEN(eap->arg), FNAME_MESS,
						      TRUE, curbuf->b_ffname);
    if (eap->addr_count > 0)
    {
	/* Repeat finding the file "count" times.  This matters when it
	 * appears several times in the path. */
	count = eap->line2;
	while (fname != NULL && --count > 0)
	{
	    vim_free(fname);
	    fname = find_file_in_path(NULL, 0, FNAME_MESS,
						     FALSE, curbuf->b_ffname);
	}
    }

    if (fname != NULL)
    {
	eap->arg = fname;
#endif
	do_exedit(eap, NULL);
#ifdef FEAT_SEARCHPATH
	vim_free(fname);
    }
#endif
}

/*
 * ":open" simulation: for now just work like ":visual".
 */
    static void
ex_open(exarg_T *eap)
{
    regmatch_T	regmatch;
    char_u	*p;

    curwin->w_cursor.lnum = eap->line2;
    beginline(BL_SOL | BL_FIX);
    if (*eap->arg == '/')
    {
	/* ":open /pattern/": put cursor in column found with pattern */
	++eap->arg;
	p = skip_regexp(eap->arg, '/', p_magic, NULL);
	*p = NUL;
	regmatch.regprog = vim_regcomp(eap->arg, p_magic ? RE_MAGIC : 0);
	if (regmatch.regprog != NULL)
	{
	    regmatch.rm_ic = p_ic;
	    p = ml_get_curline();
	    if (vim_regexec(&regmatch, p, (colnr_T)0))
		curwin->w_cursor.col = (colnr_T)(regmatch.startp[0] - p);
	    else
		emsg(_(e_nomatch));
	    vim_regfree(regmatch.regprog);
	}
	/* Move to the NUL, ignore any other arguments. */
	eap->arg += STRLEN(eap->arg);
    }
    check_cursor();

    eap->cmdidx = CMD_visual;
    do_exedit(eap, NULL);
}

/*
 * ":edit", ":badd", ":visual".
 */
    static void
ex_edit(exarg_T *eap)
{
    do_exedit(eap, NULL);
}

/*
 * ":edit <file>" command and alikes.
 */
    void
do_exedit(
    exarg_T	*eap,
    win_T	*old_curwin)	    /* curwin before doing a split or NULL */
{
    int		n;
    int		need_hide;
    int		exmode_was = exmode_active;

    if (eap->cmdidx != CMD_pedit && ERROR_IF_POPUP_WINDOW)
	return;
    /*
     * ":vi" command ends Ex mode.
     */
    if (exmode_active && (eap->cmdidx == CMD_visual
						|| eap->cmdidx == CMD_view))
    {
	exmode_active = FALSE;
	if (*eap->arg == NUL)
	{
	    /* Special case:  ":global/pat/visual\NLvi-commands" */
	    if (global_busy)
	    {
		int	rd = RedrawingDisabled;
		int	nwr = no_wait_return;
		int	ms = msg_scroll;
#ifdef FEAT_GUI
		int	he = hold_gui_events;
#endif

		if (eap->nextcmd != NULL)
		{
		    stuffReadbuff(eap->nextcmd);
		    eap->nextcmd = NULL;
		}

		if (exmode_was != EXMODE_VIM)
		    settmode(TMODE_RAW);
		RedrawingDisabled = 0;
		no_wait_return = 0;
		need_wait_return = FALSE;
		msg_scroll = 0;
#ifdef FEAT_GUI
		hold_gui_events = 0;
#endif
		must_redraw = CLEAR;

		main_loop(FALSE, TRUE);

		RedrawingDisabled = rd;
		no_wait_return = nwr;
		msg_scroll = ms;
#ifdef FEAT_GUI
		hold_gui_events = he;
#endif
	    }
	    return;
	}
    }

    if ((eap->cmdidx == CMD_new
		|| eap->cmdidx == CMD_tabnew
		|| eap->cmdidx == CMD_tabedit
		|| eap->cmdidx == CMD_vnew) && *eap->arg == NUL)
    {
	/* ":new" or ":tabnew" without argument: edit an new empty buffer */
	setpcmark();
	(void)do_ecmd(0, NULL, NULL, eap, ECMD_ONE,
		      ECMD_HIDE + (eap->forceit ? ECMD_FORCEIT : 0),
		      old_curwin == NULL ? curwin : NULL);
    }
    else if ((eap->cmdidx != CMD_split && eap->cmdidx != CMD_vsplit)
	    || *eap->arg != NUL
#ifdef FEAT_BROWSE
	    || cmdmod.browse
#endif
	    )
    {
	/* Can't edit another file when "curbuf_lock" is set.  Only ":edit"
	 * can bring us here, others are stopped earlier. */
	if (*eap->arg != NUL && curbuf_locked())
	    return;

	n = readonlymode;
	if (eap->cmdidx == CMD_view || eap->cmdidx == CMD_sview)
	    readonlymode = TRUE;
	else if (eap->cmdidx == CMD_enew)
	    readonlymode = FALSE;   /* 'readonly' doesn't make sense in an
				       empty buffer */
	setpcmark();
	if (do_ecmd(0, (eap->cmdidx == CMD_enew ? NULL : eap->arg),
		    NULL, eap,
		    /* ":edit" goes to first line if Vi compatible */
		    (*eap->arg == NUL && eap->do_ecmd_lnum == 0
				      && vim_strchr(p_cpo, CPO_GOTO1) != NULL)
					       ? ECMD_ONE : eap->do_ecmd_lnum,
		    (buf_hide(curbuf) ? ECMD_HIDE : 0)
		    + (eap->forceit ? ECMD_FORCEIT : 0)
		      /* after a split we can use an existing buffer */
		    + (old_curwin != NULL ? ECMD_OLDBUF : 0)
		    + (eap->cmdidx == CMD_badd ? ECMD_ADDBUF : 0 )
		    , old_curwin == NULL ? curwin : NULL) == FAIL)
	{
	    /* Editing the file failed.  If the window was split, close it. */
	    if (old_curwin != NULL)
	    {
		need_hide = (curbufIsChanged() && curbuf->b_nwindows <= 1);
		if (!need_hide || buf_hide(curbuf))
		{
#if defined(FEAT_EVAL)
		    cleanup_T   cs;

		    /* Reset the error/interrupt/exception state here so that
		     * aborting() returns FALSE when closing a window. */
		    enter_cleanup(&cs);
#endif
#ifdef FEAT_GUI
		    need_mouse_correct = TRUE;
#endif
		    win_close(curwin, !need_hide && !buf_hide(curbuf));

#if defined(FEAT_EVAL)
		    /* Restore the error/interrupt/exception state if not
		     * discarded by a new aborting error, interrupt, or
		     * uncaught exception. */
		    leave_cleanup(&cs);
#endif
		}
	    }
	}
	else if (readonlymode && curbuf->b_nwindows == 1)
	{
	    /* When editing an already visited buffer, 'readonly' won't be set
	     * but the previous value is kept.  With ":view" and ":sview" we
	     * want the  file to be readonly, except when another window is
	     * editing the same buffer. */
	    curbuf->b_p_ro = TRUE;
	}
	readonlymode = n;
    }
    else
    {
	if (eap->do_ecmd_cmd != NULL)
	    do_cmdline_cmd(eap->do_ecmd_cmd);
#ifdef FEAT_TITLE
	n = curwin->w_arg_idx_invalid;
#endif
	check_arg_idx(curwin);
#ifdef FEAT_TITLE
	if (n != curwin->w_arg_idx_invalid)
	    maketitle();
#endif
    }

    /*
     * if ":split file" worked, set alternate file name in old window to new
     * file
     */
    if (old_curwin != NULL
	    && *eap->arg != NUL
	    && curwin != old_curwin
	    && win_valid(old_curwin)
	    && old_curwin->w_buffer != curbuf
	    && !cmdmod.keepalt)
	old_curwin->w_alt_fnum = curbuf->b_fnum;

    ex_no_reprint = TRUE;
}

#ifndef FEAT_GUI
/*
 * ":gui" and ":gvim" when there is no GUI.
 */
    static void
ex_nogui(exarg_T *eap)
{
    eap->errmsg = e_nogvim;
}
#endif

#if defined(FEAT_GUI_MSWIN) && defined(FEAT_MENU) && defined(FEAT_TEAROFF)
    static void
ex_tearoff(exarg_T *eap)
{
    gui_make_tearoff(eap->arg);
}
#endif

#if (defined(FEAT_GUI_MSWIN) || defined(FEAT_GUI_GTK) \
	|| defined(FEAT_TERM_POPUP_MENU)) && defined(FEAT_MENU)
    static void
ex_popup(exarg_T *eap)
{
# if defined(FEAT_GUI_MSWIN) || defined(FEAT_GUI_GTK)
    if (gui.in_use)
	gui_make_popup(eap->arg, eap->forceit);
#  ifdef FEAT_TERM_POPUP_MENU
    else
#  endif
# endif
# ifdef FEAT_TERM_POPUP_MENU
	pum_make_popup(eap->arg, eap->forceit);
# endif
}
#endif

    static void
ex_swapname(exarg_T *eap UNUSED)
{
    if (curbuf->b_ml.ml_mfp == NULL || curbuf->b_ml.ml_mfp->mf_fname == NULL)
	msg(_("No swap file"));
    else
	msg((char *)curbuf->b_ml.ml_mfp->mf_fname);
}

/*
 * ":syncbind" forces all 'scrollbind' windows to have the same relative
 * offset.
 * (1998-11-02 16:21:01  R. Edward Ralston <eralston@computer.org>)
 */
    static void
ex_syncbind(exarg_T *eap UNUSED)
{
    win_T	*wp;
    win_T	*save_curwin = curwin;
    buf_T	*save_curbuf = curbuf;
    long	topline;
    long	y;
    linenr_T	old_linenr = curwin->w_cursor.lnum;

    setpcmark();

    /*
     * determine max topline
     */
    if (curwin->w_p_scb)
    {
	topline = curwin->w_topline;
	FOR_ALL_WINDOWS(wp)
	{
	    if (wp->w_p_scb && wp->w_buffer)
	    {
		y = wp->w_buffer->b_ml.ml_line_count - get_scrolloff_value();
		if (topline > y)
		    topline = y;
	    }
	}
	if (topline < 1)
	    topline = 1;
    }
    else
    {
	topline = 1;
    }


    /*
     * Set all scrollbind windows to the same topline.
     */
    FOR_ALL_WINDOWS(curwin)
    {
	if (curwin->w_p_scb)
	{
	    curbuf = curwin->w_buffer;
	    y = topline - curwin->w_topline;
	    if (y > 0)
		scrollup(y, TRUE);
	    else
		scrolldown(-y, TRUE);
	    curwin->w_scbind_pos = topline;
	    redraw_later(VALID);
	    cursor_correct();
	    curwin->w_redr_status = TRUE;
	}
    }
    curwin = save_curwin;
    curbuf = save_curbuf;
    if (curwin->w_p_scb)
    {
	did_syncbind = TRUE;
	checkpcmark();
	if (old_linenr != curwin->w_cursor.lnum)
	{
	    char_u ctrl_o[2];

	    ctrl_o[0] = Ctrl_O;
	    ctrl_o[1] = 0;
	    ins_typebuf(ctrl_o, REMAP_NONE, 0, TRUE, FALSE);
	}
    }
}


    static void
ex_read(exarg_T *eap)
{
    int		i;
    int		empty = (curbuf->b_ml.ml_flags & ML_EMPTY);
    linenr_T	lnum;

    if (eap->usefilter)			/* :r!cmd */
	do_bang(1, eap, FALSE, FALSE, TRUE);
    else
    {
	if (u_save(eap->line2, (linenr_T)(eap->line2 + 1)) == FAIL)
	    return;

#ifdef FEAT_BROWSE
	if (cmdmod.browse)
	{
	    char_u *browseFile;

	    browseFile = do_browse(0, (char_u *)_("Append File"), eap->arg,
						    NULL, NULL, NULL, curbuf);
	    if (browseFile != NULL)
	    {
		i = readfile(browseFile, NULL,
			  eap->line2, (linenr_T)0, (linenr_T)MAXLNUM, eap, 0);
		vim_free(browseFile);
	    }
	    else
		i = OK;
	}
	else
#endif
	     if (*eap->arg == NUL)
	{
	    if (check_fname() == FAIL)	/* check for no file name */
		return;
	    i = readfile(curbuf->b_ffname, curbuf->b_fname,
			  eap->line2, (linenr_T)0, (linenr_T)MAXLNUM, eap, 0);
	}
	else
	{
	    if (vim_strchr(p_cpo, CPO_ALTREAD) != NULL)
		(void)setaltfname(eap->arg, eap->arg, (linenr_T)1);
	    i = readfile(eap->arg, NULL,
			  eap->line2, (linenr_T)0, (linenr_T)MAXLNUM, eap, 0);

	}
	if (i != OK)
	{
#if defined(FEAT_EVAL)
	    if (!aborting())
#endif
		semsg(_(e_notopen), eap->arg);
	}
	else
	{
	    if (empty && exmode_active)
	    {
		/* Delete the empty line that remains.  Historically ex does
		 * this but vi doesn't. */
		if (eap->line2 == 0)
		    lnum = curbuf->b_ml.ml_line_count;
		else
		    lnum = 1;
		if (*ml_get(lnum) == NUL && u_savedel(lnum, 1L) == OK)
		{
		    ml_delete(lnum, FALSE);
		    if (curwin->w_cursor.lnum > 1
					     && curwin->w_cursor.lnum >= lnum)
			--curwin->w_cursor.lnum;
		    deleted_lines_mark(lnum, 1L);
		}
	    }
	    redraw_curbuf_later(VALID);
	}
    }
}

static char_u	*prev_dir = NULL;

#if defined(EXITFREE) || defined(PROTO)
    void
free_cd_dir(void)
{
    VIM_CLEAR(prev_dir);
    VIM_CLEAR(globaldir);
}
#endif

/*
 * Deal with the side effects of changing the current directory.
 * When 'scope' is CDSCOPE_TABPAGE then this was after an ":tcd" command.
 * When 'scope' is CDSCOPE_WINDOW then this was after an ":lcd" command.
 */
    void
post_chdir(cdscope_T scope)
{
    if (scope != CDSCOPE_WINDOW)
	// Clear tab local directory for both :cd and :tcd
	VIM_CLEAR(curtab->tp_localdir);
    VIM_CLEAR(curwin->w_localdir);
    if (scope != CDSCOPE_GLOBAL)
    {
	/* If still in global directory, need to remember current
	 * directory as global directory. */
	if (globaldir == NULL && prev_dir != NULL)
	    globaldir = vim_strsave(prev_dir);
	/* Remember this local directory for the window. */
	if (mch_dirname(NameBuff, MAXPATHL) == OK)
	{
	    if (scope == CDSCOPE_TABPAGE)
		curtab->tp_localdir = vim_strsave(NameBuff);
	    else
		curwin->w_localdir = vim_strsave(NameBuff);
	}
    }
    else
    {
	/* We are now in the global directory, no need to remember its
	 * name. */
	VIM_CLEAR(globaldir);
    }

    shorten_fnames(TRUE);
}

/*
 * Change directory function used by :cd/:tcd/:lcd Ex commands and the
 * chdir() function. If 'winlocaldir' is TRUE, then changes the window-local
 * directory. If 'tablocaldir' is TRUE, then changes the tab-local directory.
 * Otherwise changes the global directory.
 * Returns TRUE if the directory is successfully changed.
 */
    int
changedir_func(
	char_u		*new_dir,
	int		forceit,
	cdscope_T	scope)
{
    char_u	*tofree;
    int		dir_differs;
    int		retval = FALSE;

    if (allbuf_locked())
	return FALSE;

    if (vim_strchr(p_cpo, CPO_CHDIR) != NULL && curbufIsChanged() && !forceit)
    {
	emsg(_("E747: Cannot change directory, buffer is modified (add ! to override)"));
	return FALSE;
    }

    // ":cd -": Change to previous directory
    if (STRCMP(new_dir, "-") == 0)
    {
	if (prev_dir == NULL)
	{
	    emsg(_("E186: No previous directory"));
	    return FALSE;
	}
	new_dir = prev_dir;
    }

    // Save current directory for next ":cd -"
    tofree = prev_dir;
    if (mch_dirname(NameBuff, MAXPATHL) == OK)
	prev_dir = vim_strsave(NameBuff);
    else
	prev_dir = NULL;

#if defined(UNIX) || defined(VMS)
    // for UNIX ":cd" means: go to home directory
    if (*new_dir == NUL)
    {
	// use NameBuff for home directory name
# ifdef VMS
	char_u	*p;

	p = mch_getenv((char_u *)"SYS$LOGIN");
	if (p == NULL || *p == NUL)	// empty is the same as not set
	    NameBuff[0] = NUL;
	else
	    vim_strncpy(NameBuff, p, MAXPATHL - 1);
# else
	expand_env((char_u *)"$HOME", NameBuff, MAXPATHL);
# endif
	new_dir = NameBuff;
    }
#endif
    dir_differs = new_dir == NULL || prev_dir == NULL
	|| pathcmp((char *)prev_dir, (char *)new_dir, -1) != 0;
    if (new_dir == NULL || (dir_differs && vim_chdir(new_dir)))
	emsg(_(e_failed));
    else
    {
	char_u  *acmd_fname;

	post_chdir(scope);

	if (dir_differs)
	{
	    if (scope == CDSCOPE_WINDOW)
		acmd_fname = (char_u *)"window";
	    else if (scope == CDSCOPE_TABPAGE)
		acmd_fname = (char_u *)"tabpage";
	    else
		acmd_fname = (char_u *)"global";
	    apply_autocmds(EVENT_DIRCHANGED, acmd_fname, new_dir, FALSE,
								curbuf);
	}
	retval = TRUE;
    }
    vim_free(tofree);

    return retval;
}

/*
 * ":cd", ":tcd", ":lcd", ":chdir" ":tchdir" and ":lchdir".
 */
    void
ex_cd(exarg_T *eap)
{
    char_u	*new_dir;

    new_dir = eap->arg;
#if !defined(UNIX) && !defined(VMS)
    // for non-UNIX ":cd" means: print current directory
    if (*new_dir == NUL)
	ex_pwd(NULL);
    else
#endif
    {
	cdscope_T	scope = CDSCOPE_GLOBAL;

	if (eap->cmdidx == CMD_lcd || eap->cmdidx == CMD_lchdir)
	    scope = CDSCOPE_WINDOW;
	else if (eap->cmdidx == CMD_tcd || eap->cmdidx == CMD_tchdir)
	    scope = CDSCOPE_TABPAGE;

	if (changedir_func(new_dir, eap->forceit, scope))
	{
	    // Echo the new current directory if the command was typed.
	    if (KeyTyped || p_verbose >= 5)
		ex_pwd(eap);
	}
    }
}

/*
 * ":pwd".
 */
    static void
ex_pwd(exarg_T *eap UNUSED)
{
    if (mch_dirname(NameBuff, MAXPATHL) == OK)
    {
#ifdef BACKSLASH_IN_FILENAME
	slash_adjust(NameBuff);
#endif
	msg((char *)NameBuff);
    }
    else
	emsg(_("E187: Unknown"));
}

/*
 * ":=".
 */
    static void
ex_equal(exarg_T *eap)
{
    int save_silent=silent_mode;
    int save_info_message=info_message;
    if (Unix2003_compat) {
	silent_mode = FALSE;	/* = command output is not silenced */
	info_message = TRUE;
    }
    smsg("%ld", (long)eap->line2);
    silent_mode = save_silent;
    info_message = save_info_message;
    ex_may_print(eap);
}

    static void
ex_sleep(exarg_T *eap)
{
    int		n;
    long	len;

    if (cursor_valid())
    {
	n = W_WINROW(curwin) + curwin->w_wrow - msg_scrolled;
	if (n >= 0)
	    windgoto((int)n, curwin->w_wincol + curwin->w_wcol);
    }

    len = eap->line2;
    switch (*eap->arg)
    {
	case 'm': break;
	case NUL: len *= 1000L; break;
	default: semsg(_(e_invarg2), eap->arg); return;
    }
    do_sleep(len);
}

/*
 * Sleep for "msec" milliseconds, but keep checking for a CTRL-C every second.
 */
    void
do_sleep(long msec)
{
    long	done = 0;
    long	wait_now;
# ifdef ELAPSED_FUNC
    elapsed_T	start_tv;

    // Remember at what time we started, so that we know how much longer we
    // should wait after waiting for a bit.
    ELAPSED_INIT(start_tv);
# endif

    cursor_on();
    out_flush_cursor(FALSE, FALSE);
    while (!got_int && done < msec)
    {
	wait_now = msec - done > 1000L ? 1000L : msec - done;
#ifdef FEAT_TIMERS
	{
	    long    due_time = check_due_timer();

	    if (due_time > 0 && due_time < wait_now)
		wait_now = due_time;
	}
#endif
#ifdef FEAT_JOB_CHANNEL
	if (has_any_channel() && wait_now > 20L)
	    wait_now = 20L;
#endif
#ifdef FEAT_SOUND
	if (has_any_sound_callback() && wait_now > 20L)
	    wait_now = 20L;
#endif
	ui_delay(wait_now, TRUE);

#ifdef FEAT_JOB_CHANNEL
	if (has_any_channel())
	    ui_breakcheck_force(TRUE);
	else
#endif
	    ui_breakcheck();
#ifdef MESSAGE_QUEUE
	// Process the netbeans and clientserver messages that may have been
	// received in the call to ui_breakcheck() when the GUI is in use. This
	// may occur when running a test case.
	parse_queued_messages();
#endif

# ifdef ELAPSED_FUNC
	// actual time passed
	done = ELAPSED_FUNC(start_tv);
# else
	// guestimate time passed (will actually be more)
	done += wait_now;
# endif
    }

    // If CTRL-C was typed to interrupt the sleep, drop the CTRL-C from the
    // input buffer, otherwise a following call to input() fails.
    if (got_int)
	(void)vpeekc();
}

/*
 * ":winsize" command (obsolete).
 */
    static void
ex_winsize(exarg_T *eap)
{
    int		w, h;
    char_u	*arg = eap->arg;
    char_u	*p;

    w = getdigits(&arg);
    arg = skipwhite(arg);
    p = arg;
    h = getdigits(&arg);
    if (*p != NUL && *arg == NUL)
	set_shellsize(w, h, TRUE);
    else
	emsg(_("E465: :winsize requires two number arguments"));
}

    static void
ex_wincmd(exarg_T *eap)
{
    int		xchar = NUL;
    char_u	*p;

    if (*eap->arg == 'g' || *eap->arg == Ctrl_G)
    {
	/* CTRL-W g and CTRL-W CTRL-G  have an extra command character */
	if (eap->arg[1] == NUL)
	{
	    emsg(_(e_invarg));
	    return;
	}
	xchar = eap->arg[1];
	p = eap->arg + 2;
    }
    else
	p = eap->arg + 1;

    eap->nextcmd = check_nextcmd(p);
    p = skipwhite(p);
    if (*p != NUL && *p != '"' && eap->nextcmd == NULL)
	emsg(_(e_invarg));
    else if (!eap->skip)
    {
	/* Pass flags on for ":vertical wincmd ]". */
	postponed_split_flags = cmdmod.split;
	postponed_split_tab = cmdmod.tab;
	do_window(*eap->arg, eap->addr_count > 0 ? eap->line2 : 0L, xchar);
	postponed_split_flags = 0;
	postponed_split_tab = 0;
    }
}

#if defined(FEAT_GUI) || defined(UNIX) || defined(VMS) || defined(MSWIN)
/*
 * ":winpos".
 */
    static void
ex_winpos(exarg_T *eap)
{
    int		x, y;
    char_u	*arg = eap->arg;
    char_u	*p;

    if (*arg == NUL)
    {
# if defined(FEAT_GUI) || defined(MSWIN)
#  ifdef VIMDLL
	if (gui.in_use ? gui_mch_get_winpos(&x, &y) != FAIL :
		mch_get_winpos(&x, &y) != FAIL)
#  elif defined(FEAT_GUI)
	if (gui.in_use && gui_mch_get_winpos(&x, &y) != FAIL)
#  else
	if (mch_get_winpos(&x, &y) != FAIL)
#  endif
	{
	    sprintf((char *)IObuff, _("Window position: X %d, Y %d"), x, y);
	    msg((char *)IObuff);
	}
	else
# endif
	    emsg(_("E188: Obtaining window position not implemented for this platform"));
    }
    else
    {
	x = getdigits(&arg);
	arg = skipwhite(arg);
	p = arg;
	y = getdigits(&arg);
	if (*p == NUL || *arg != NUL)
	{
	    emsg(_("E466: :winpos requires two number arguments"));
	    return;
	}
# ifdef FEAT_GUI
	if (gui.in_use)
	    gui_mch_set_winpos(x, y);
	else if (gui.starting)
	{
	    /* Remember the coordinates for when the window is opened. */
	    gui_win_x = x;
	    gui_win_y = y;
	}
#  if defined(HAVE_TGETENT) || defined(VIMDLL)
	else
#  endif
# endif
# if defined(MSWIN) && (!defined(FEAT_GUI) || defined(VIMDLL))
	    mch_set_winpos(x, y);
# endif
# ifdef HAVE_TGETENT
	if (*T_CWP)
	    term_set_winpos(x, y);
# endif
    }
}
#endif

/*
 * Handle command that work like operators: ":delete", ":yank", ":>" and ":<".
 */
    static void
ex_operators(exarg_T *eap)
{
    oparg_T	oa;

    clear_oparg(&oa);
    oa.regname = eap->regname;
    oa.start.lnum = eap->line1;
    oa.end.lnum = eap->line2;
    oa.line_count = eap->line2 - eap->line1 + 1;
    oa.motion_type = MLINE;
    virtual_op = FALSE;
    if (eap->cmdidx != CMD_yank)	/* position cursor for undo */
    {
	setpcmark();
	curwin->w_cursor.lnum = eap->line1;
	beginline(BL_SOL | BL_FIX);
    }

    if (VIsual_active)
	end_visual_mode();

    switch (eap->cmdidx)
    {
	case CMD_delete:
	    oa.op_type = OP_DELETE;
	    op_delete(&oa);
	    break;

	case CMD_yank:
	    oa.op_type = OP_YANK;
	    (void)op_yank(&oa, FALSE, TRUE);
	    break;

	default:    /* CMD_rshift or CMD_lshift */
	    if (
#ifdef FEAT_RIGHTLEFT
		(eap->cmdidx == CMD_rshift) ^ curwin->w_p_rl
#else
		eap->cmdidx == CMD_rshift
#endif
						)
		oa.op_type = OP_RSHIFT;
	    else
		oa.op_type = OP_LSHIFT;
	    op_shift(&oa, FALSE, eap->amount);
	    break;
    }
    virtual_op = MAYBE;
    ex_may_print(eap);
}

/*
 * ":put".
 */
    static void
ex_put(exarg_T *eap)
{
    /* ":0put" works like ":1put!". */
    if (eap->line2 == 0)
    {
	eap->line2 = 1;
	eap->forceit = TRUE;
    }
    curwin->w_cursor.lnum = eap->line2;
    do_put(eap->regname, eap->forceit ? BACKWARD : FORWARD, 1L,
						       PUT_LINE|PUT_CURSLINE);
}

/*
 * Handle ":copy" and ":move".
 */
    static void
ex_copymove(exarg_T *eap)
{
    long	n;

    n = get_address(eap, &eap->arg, eap->addr_type, FALSE, FALSE, FALSE, 1);
    if (eap->arg == NULL)	    /* error detected */
    {
	eap->nextcmd = NULL;
	return;
    }
    get_flags(eap);

    /*
     * move or copy lines from 'eap->line1'-'eap->line2' to below line 'n'
     */
    if (n == MAXLNUM || n < 0 || n > curbuf->b_ml.ml_line_count)
    {
	emsg(_(e_invrange));
	return;
    }

    if (eap->cmdidx == CMD_move)
    {
	if (do_move(eap->line1, eap->line2, n) == FAIL)
	    return;
    }
    else
	ex_copy(eap->line1, eap->line2, n);
    u_clearline();
    beginline(BL_SOL | BL_FIX);
    ex_may_print(eap);
}

/*
 * Print the current line if flags were given to the Ex command.
 */
    void
ex_may_print(exarg_T *eap)
{
    if (eap->flags != 0)
    {
	print_line(curwin->w_cursor.lnum, (eap->flags & EXFLAG_NR),
						  (eap->flags & EXFLAG_LIST));
	ex_no_reprint = TRUE;
    }
}

/*
 * ":smagic" and ":snomagic".
 */
    static void
ex_submagic(exarg_T *eap)
{
    int		magic_save = p_magic;

    p_magic = (eap->cmdidx == CMD_smagic);
    do_sub(eap);
    p_magic = magic_save;
}

/*
 * ":join".
 */
    static void
ex_join(exarg_T *eap)
{
    curwin->w_cursor.lnum = eap->line1;
    if (eap->line1 == eap->line2)
    {
	if (eap->addr_count >= 2)   /* :2,2join does nothing */
	    return;
	if (eap->line2 == curbuf->b_ml.ml_line_count)
	{
	    beep_flush();
	    return;
	}
	++eap->line2;
    }
    (void)do_join(eap->line2 - eap->line1 + 1, !eap->forceit, TRUE, TRUE, TRUE);
    beginline(BL_WHITE | BL_FIX);
    ex_may_print(eap);
}

/*
 * ":[addr]@r" or ":[addr]*r": execute register
 */
    static void
ex_at(exarg_T *eap)
{
    int		c;
    int		prev_len = typebuf.tb_len;

    curwin->w_cursor.lnum = eap->line2;
    check_cursor_col();

#ifdef USE_ON_FLY_SCROLL
    dont_scroll = TRUE;		/* disallow scrolling here */
#endif

    /* get the register name.  No name means to use the previous one */
    c = *eap->arg;
    if (c == NUL || (c == '*' && *eap->cmd == '*'))
	c = '@';
    /* Put the register in the typeahead buffer with the "silent" flag. */
    if (do_execreg(c, TRUE, vim_strchr(p_cpo, CPO_EXECBUF) != NULL, TRUE)
								      == FAIL)
    {
	beep_flush();
    }
    else
    {
	int	save_efr = exec_from_reg;

	exec_from_reg = TRUE;

	/*
	 * Execute from the typeahead buffer.
	 * Continue until the stuff buffer is empty and all added characters
	 * have been consumed.
	 */
	while (!stuff_empty() || typebuf.tb_len > prev_len)
	    (void)do_cmdline(NULL, getexline, NULL, DOCMD_NOWAIT|DOCMD_VERBOSE);

	exec_from_reg = save_efr;
    }
}

/*
 * ":!".
 */
    static void
ex_bang(exarg_T *eap)
{
    do_bang(eap->addr_count, eap, eap->forceit, TRUE, TRUE);
}

/*
 * ":undo".
 */
    static void
ex_undo(exarg_T *eap)
{
    if (eap->addr_count == 1)	    /* :undo 123 */
	undo_time(eap->line2, FALSE, FALSE, TRUE);
    else
	u_undo(1);
}

#ifdef FEAT_PERSISTENT_UNDO
    static void
ex_wundo(exarg_T *eap)
{
    char_u hash[UNDO_HASH_SIZE];

    u_compute_hash(hash);
    u_write_undo(eap->arg, eap->forceit, curbuf, hash);
}

    static void
ex_rundo(exarg_T *eap)
{
    char_u hash[UNDO_HASH_SIZE];

    u_compute_hash(hash);
    u_read_undo(eap->arg, hash, NULL);
}
#endif

/*
 * ":redo".
 */
    static void
ex_redo(exarg_T *eap UNUSED)
{
    u_redo(1);
}

/*
 * ":earlier" and ":later".
 */
    static void
ex_later(exarg_T *eap)
{
    long	count = 0;
    int		sec = FALSE;
    int		file = FALSE;
    char_u	*p = eap->arg;

    if (*p == NUL)
	count = 1;
    else if (isdigit(*p))
    {
	count = getdigits(&p);
	switch (*p)
	{
	    case 's': ++p; sec = TRUE; break;
	    case 'm': ++p; sec = TRUE; count *= 60; break;
	    case 'h': ++p; sec = TRUE; count *= 60 * 60; break;
	    case 'd': ++p; sec = TRUE; count *= 24 * 60 * 60; break;
	    case 'f': ++p; file = TRUE; break;
	}
    }

    if (*p != NUL)
	semsg(_(e_invarg2), eap->arg);
    else
	undo_time(eap->cmdidx == CMD_earlier ? -count : count,
							    sec, file, FALSE);
}

/*
 * ":redir": start/stop redirection.
 */
    static void
ex_redir(exarg_T *eap)
{
    char	*mode;
    char_u	*fname;
    char_u	*arg = eap->arg;

#ifdef FEAT_EVAL
    if (redir_execute)
    {
	emsg(_("E930: Cannot use :redir inside execute()"));
	return;
    }
#endif

    if (STRICMP(eap->arg, "END") == 0)
	close_redir();
    else
    {
	if (*arg == '>')
	{
	    ++arg;
	    if (*arg == '>')
	    {
		++arg;
		mode = "a";
	    }
	    else
		mode = "w";
	    arg = skipwhite(arg);

	    close_redir();

	    /* Expand environment variables and "~/". */
	    fname = expand_env_save(arg);
	    if (fname == NULL)
		return;
#ifdef FEAT_BROWSE
	    if (cmdmod.browse)
	    {
		char_u	*browseFile;

		browseFile = do_browse(BROWSE_SAVE,
			(char_u *)_("Save Redirection"),
			fname, NULL, NULL,
			(char_u *)_(BROWSE_FILTER_ALL_FILES), curbuf);
		if (browseFile == NULL)
		    return;		/* operation cancelled */
		vim_free(fname);
		fname = browseFile;
		eap->forceit = TRUE;	/* since dialog already asked */
	    }
#endif

	    redir_fd = open_exfile(fname, eap->forceit, mode);
	    vim_free(fname);
	}
#ifdef FEAT_EVAL
	else if (*arg == '@')
	{
	    /* redirect to a register a-z (resp. A-Z for appending) */
	    close_redir();
	    ++arg;
	    if (ASCII_ISALPHA(*arg)
# ifdef FEAT_CLIPBOARD
		    || *arg == '*'
		    || *arg == '+'
# endif
		    || *arg == '"')
	    {
		redir_reg = *arg++;
		if (*arg == '>' && arg[1] == '>')  /* append */
		    arg += 2;
		else
		{
		    /* Can use both "@a" and "@a>". */
		    if (*arg == '>')
			arg++;
		    /* Make register empty when not using @A-@Z and the
		     * command is valid. */
		    if (*arg == NUL && !isupper(redir_reg))
			write_reg_contents(redir_reg, (char_u *)"", -1, FALSE);
		}
	    }
	    if (*arg != NUL)
	    {
		redir_reg = 0;
		semsg(_(e_invarg2), eap->arg);
	    }
	}
	else if (*arg == '=' && arg[1] == '>')
	{
	    int append;

	    /* redirect to a variable */
	    close_redir();
	    arg += 2;

	    if (*arg == '>')
	    {
		++arg;
		append = TRUE;
	    }
	    else
		append = FALSE;

	    if (var_redir_start(skipwhite(arg), append) == OK)
		redir_vname = 1;
	}
#endif

	/* TODO: redirect to a buffer */

	else
	    semsg(_(e_invarg2), eap->arg);
    }

    /* Make sure redirection is not off.  Can happen for cmdline completion
     * that indirectly invokes a command to catch its output. */
    if (redir_fd != NULL
#ifdef FEAT_EVAL
			  || redir_reg || redir_vname
#endif
							)
	redir_off = FALSE;
}

/*
 * ":redraw": force redraw
 */
    void
ex_redraw(exarg_T *eap)
{
    int		r = RedrawingDisabled;
    int		p = p_lz;

    RedrawingDisabled = 0;
    p_lz = FALSE;
    validate_cursor();
    update_topline();
    update_screen(eap->forceit ? CLEAR : VIsual_active ? INVERTED : 0);
#ifdef FEAT_TITLE
    if (need_maketitle)
	maketitle();
#endif
#if defined(MSWIN) && (!defined(FEAT_GUI_MSWIN) || defined(VIMDLL))
# ifdef VIMDLL
    if (!gui.in_use)
# endif
	resize_console_buf();
#endif
    RedrawingDisabled = r;
    p_lz = p;

    /* Reset msg_didout, so that a message that's there is overwritten. */
    msg_didout = FALSE;
    msg_col = 0;

    /* No need to wait after an intentional redraw. */
    need_wait_return = FALSE;

    out_flush();
}

/*
 * ":redrawstatus": force redraw of status line(s)
 */
    static void
ex_redrawstatus(exarg_T *eap UNUSED)
{
    int		r = RedrawingDisabled;
    int		p = p_lz;

    RedrawingDisabled = 0;
    p_lz = FALSE;
    if (eap->forceit)
	status_redraw_all();
    else
	status_redraw_curbuf();
    update_screen(VIsual_active ? INVERTED : 0);
    RedrawingDisabled = r;
    p_lz = p;
    out_flush();
}

/*
 * ":redrawtabline": force redraw of the tabline
 */
    static void
ex_redrawtabline(exarg_T *eap UNUSED)
{
    int		r = RedrawingDisabled;
    int		p = p_lz;

    RedrawingDisabled = 0;
    p_lz = FALSE;

    draw_tabline();

    RedrawingDisabled = r;
    p_lz = p;
    out_flush();
}

    static void
close_redir(void)
{
    if (redir_fd != NULL)
    {
	fclose(redir_fd);
	redir_fd = NULL;
    }
#ifdef FEAT_EVAL
    redir_reg = 0;
    if (redir_vname)
    {
	var_redir_stop();
	redir_vname = 0;
    }
#endif
}

#if (defined(FEAT_SESSION) || defined(FEAT_EVAL)) || defined(PROTO)
    int
vim_mkdir_emsg(char_u *name, int prot UNUSED)
{
    if (vim_mkdir(name, prot) != 0)
    {
	semsg(_("E739: Cannot create directory: %s"), name);
	return FAIL;
    }
    return OK;
}
#endif

/*
 * Open a file for writing for an Ex command, with some checks.
 * Return file descriptor, or NULL on failure.
 */
    FILE *
open_exfile(
    char_u	*fname,
    int		forceit,
    char	*mode)	    /* "w" for create new file or "a" for append */
{
    FILE	*fd;

#ifdef UNIX
    /* with Unix it is possible to open a directory */
    if (mch_isdir(fname))
    {
	semsg(_(e_isadir2), fname);
	return NULL;
    }
#endif
    if (!forceit && *mode != 'a' && vim_fexists(fname))
    {
	semsg(_("E189: \"%s\" exists (add ! to override)"), fname);
	return NULL;
    }

    if ((fd = mch_fopen((char *)fname, mode)) == NULL)
	semsg(_("E190: Cannot open \"%s\" for writing"), fname);

    return fd;
}

/*
 * ":mark" and ":k".
 */
    static void
ex_mark(exarg_T *eap)
{
    pos_T	pos;

    if (*eap->arg == NUL)		/* No argument? */
	emsg(_(e_argreq));
    else if (eap->arg[1] != NUL)	/* more than one character? */
	emsg(_(e_trailing));
    else
    {
	pos = curwin->w_cursor;		/* save curwin->w_cursor */
	curwin->w_cursor.lnum = eap->line2;
	beginline(BL_WHITE | BL_FIX);
	if (setmark(*eap->arg) == FAIL)	/* set mark */
	    emsg(_("E191: Argument must be a letter or forward/backward quote"));
	curwin->w_cursor = pos;		/* restore curwin->w_cursor */
    }
}

/*
 * Update w_topline, w_leftcol and the cursor position.
 */
    void
update_topline_cursor(void)
{
    check_cursor();		/* put cursor on valid line */
    update_topline();
    if (!curwin->w_p_wrap)
	validate_cursor();
    update_curswant();
}

/*
 * Save the current State and go to Normal mode.
 * Return TRUE if the typeahead could be saved.
 */
    int
save_current_state(save_state_T *sst)
{
    sst->save_msg_scroll = msg_scroll;
    sst->save_restart_edit = restart_edit;
    sst->save_msg_didout = msg_didout;
    sst->save_State = State;
    sst->save_insertmode = p_im;
    sst->save_finish_op = finish_op;
    sst->save_opcount = opcount;
    sst->save_reg_executing = reg_executing;

    msg_scroll = FALSE;	    /* no msg scrolling in Normal mode */
    restart_edit = 0;	    /* don't go to Insert mode */
    p_im = FALSE;	    /* don't use 'insertmode' */

    /*
     * Save the current typeahead.  This is required to allow using ":normal"
     * from an event handler and makes sure we don't hang when the argument
     * ends with half a command.
     */
    save_typeahead(&sst->tabuf);
    return sst->tabuf.typebuf_valid;
}

    void
restore_current_state(save_state_T *sst)
{
    /* Restore the previous typeahead. */
    restore_typeahead(&sst->tabuf);

    msg_scroll = sst->save_msg_scroll;
    restart_edit = sst->save_restart_edit;
    p_im = sst->save_insertmode;
    finish_op = sst->save_finish_op;
    opcount = sst->save_opcount;
    reg_executing = sst->save_reg_executing;
    msg_didout |= sst->save_msg_didout;	/* don't reset msg_didout now */

    /* Restore the state (needed when called from a function executed for
     * 'indentexpr'). Update the mouse and cursor, they may have changed. */
    State = sst->save_State;
#ifdef CURSOR_SHAPE
    ui_cursor_shape();		/* may show different cursor shape */
#endif
}

/*
 * ":normal[!] {commands}": Execute normal mode commands.
 */
    void
ex_normal(exarg_T *eap)
{
    save_state_T save_state;
    char_u	*arg = NULL;
    int		l;
    char_u	*p;

    if (ex_normal_lock > 0)
    {
	emsg(_(e_secure));
	return;
    }
    if (ex_normal_busy >= p_mmd)
    {
	emsg(_("E192: Recursive use of :normal too deep"));
	return;
    }

    /*
     * vgetc() expects a CSI and K_SPECIAL to have been escaped.  Don't do
     * this for the K_SPECIAL leading byte, otherwise special keys will not
     * work.
     */
    if (has_mbyte)
    {
	int	len = 0;

	/* Count the number of characters to be escaped. */
	for (p = eap->arg; *p != NUL; ++p)
	{
#ifdef FEAT_GUI
	    if (*p == CSI)  /* leadbyte CSI */
		len += 2;
#endif
	    for (l = (*mb_ptr2len)(p) - 1; l > 0; --l)
		if (*++p == K_SPECIAL	  /* trailbyte K_SPECIAL or CSI */
#ifdef FEAT_GUI
			|| *p == CSI
#endif
			)
		    len += 2;
	}
	if (len > 0)
	{
	    arg = alloc(STRLEN(eap->arg) + len + 1);
	    if (arg != NULL)
	    {
		len = 0;
		for (p = eap->arg; *p != NUL; ++p)
		{
		    arg[len++] = *p;
#ifdef FEAT_GUI
		    if (*p == CSI)
		    {
			arg[len++] = KS_EXTRA;
			arg[len++] = (int)KE_CSI;
		    }
#endif
		    for (l = (*mb_ptr2len)(p) - 1; l > 0; --l)
		    {
			arg[len++] = *++p;
			if (*p == K_SPECIAL)
			{
			    arg[len++] = KS_SPECIAL;
			    arg[len++] = KE_FILLER;
			}
#ifdef FEAT_GUI
			else if (*p == CSI)
			{
			    arg[len++] = KS_EXTRA;
			    arg[len++] = (int)KE_CSI;
			}
#endif
		    }
		    arg[len] = NUL;
		}
	    }
	}
    }

    ++ex_normal_busy;
    if (save_current_state(&save_state))
    {
	/*
	 * Repeat the :normal command for each line in the range.  When no
	 * range given, execute it just once, without positioning the cursor
	 * first.
	 */
	do
	{
	    if (eap->addr_count != 0)
	    {
		curwin->w_cursor.lnum = eap->line1++;
		curwin->w_cursor.col = 0;
		check_cursor_moved(curwin);
	    }

	    exec_normal_cmd(arg != NULL
		     ? arg
		     : eap->arg, eap->forceit ? REMAP_NONE : REMAP_YES, FALSE);
	}
	while (eap->addr_count > 0 && eap->line1 <= eap->line2 && !got_int);
    }

    /* Might not return to the main loop when in an event handler. */
    update_topline_cursor();

    restore_current_state(&save_state);
    --ex_normal_busy;
    setmouse();
#ifdef CURSOR_SHAPE
    ui_cursor_shape();		/* may show different cursor shape */
#endif

    vim_free(arg);
}

/*
 * ":startinsert", ":startreplace" and ":startgreplace"
 */
    static void
ex_startinsert(exarg_T *eap)
{
    if (eap->forceit)
    {
	// cursor line can be zero on startup
	if (!curwin->w_cursor.lnum)
	    curwin->w_cursor.lnum = 1;
	set_cursor_for_append_to_line();
    }

    // Ignore the command when already in Insert mode.  Inserting an
    // expression register that invokes a function can do this.
    if (State & INSERT)
	return;

    if (eap->cmdidx == CMD_startinsert)
	restart_edit = 'a';
    else if (eap->cmdidx == CMD_startreplace)
	restart_edit = 'R';
    else
	restart_edit = 'V';

    if (!eap->forceit)
    {
	if (eap->cmdidx == CMD_startinsert)
	    restart_edit = 'i';
	curwin->w_curswant = 0;	    // avoid MAXCOL
    }
}

/*
 * ":stopinsert"
 */
    static void
ex_stopinsert(exarg_T *eap UNUSED)
{
    restart_edit = 0;
    stop_insert_mode = TRUE;
    clearmode();
}

/*
 * Execute normal mode command "cmd".
 * "remap" can be REMAP_NONE or REMAP_YES.
 */
    void
exec_normal_cmd(char_u *cmd, int remap, int silent)
{
    /* Stuff the argument into the typeahead buffer. */
    ins_typebuf(cmd, remap, 0, TRUE, silent);
    exec_normal(FALSE, FALSE, FALSE);
}

/*
 * Execute normal_cmd() until there is no typeahead left.
 * When "use_vpeekc" is TRUE use vpeekc() to check for available chars.
 */
    void
exec_normal(int was_typed, int use_vpeekc, int may_use_terminal_loop UNUSED)
{
    oparg_T	oa;
    int		c;

    // When calling vpeekc() from feedkeys() it will return Ctrl_C when there
    // is nothing to get, so also check for Ctrl_C.
    clear_oparg(&oa);
    finish_op = FALSE;
    while ((!stuff_empty()
		|| ((was_typed || !typebuf_typed()) && typebuf.tb_len > 0)
		|| (use_vpeekc && (c = vpeekc()) != NUL && c != Ctrl_C))
	    && !got_int)
    {
	update_topline_cursor();
#ifdef FEAT_TERMINAL
	if (may_use_terminal_loop && term_use_loop()
		&& oa.op_type == OP_NOP && oa.regname == NUL
		&& !VIsual_active)
	{
	    /* If terminal_loop() returns OK we got a key that is handled
	     * in Normal model.  With FAIL we first need to position the
	     * cursor and the screen needs to be redrawn. */
	    if (terminal_loop(TRUE) == OK)
		normal_cmd(&oa, TRUE);
	}
	else
#endif
	    /* execute a Normal mode cmd */
	    normal_cmd(&oa, TRUE);
    }
}

#ifdef FEAT_FIND_ID
    static void
ex_checkpath(exarg_T *eap)
{
    find_pattern_in_path(NULL, 0, 0, FALSE, FALSE, CHECK_PATH, 1L,
				   eap->forceit ? ACTION_SHOW_ALL : ACTION_SHOW,
					      (linenr_T)1, (linenr_T)MAXLNUM);
}

#if defined(FEAT_QUICKFIX)
/*
 * ":psearch"
 */
    static void
ex_psearch(exarg_T *eap)
{
    g_do_tagpreview = p_pvh;
    ex_findpat(eap);
    g_do_tagpreview = 0;
}
#endif

    static void
ex_findpat(exarg_T *eap)
{
    int		whole = TRUE;
    long	n;
    char_u	*p;
    int		action;

    switch (cmdnames[eap->cmdidx].cmd_name[2])
    {
	case 'e':	/* ":psearch", ":isearch" and ":dsearch" */
		if (cmdnames[eap->cmdidx].cmd_name[0] == 'p')
		    action = ACTION_GOTO;
		else
		    action = ACTION_SHOW;
		break;
	case 'i':	/* ":ilist" and ":dlist" */
		action = ACTION_SHOW_ALL;
		break;
	case 'u':	/* ":ijump" and ":djump" */
		action = ACTION_GOTO;
		break;
	default:	/* ":isplit" and ":dsplit" */
		action = ACTION_SPLIT;
		break;
    }

    n = 1;
    if (vim_isdigit(*eap->arg))	/* get count */
    {
	n = getdigits(&eap->arg);
	eap->arg = skipwhite(eap->arg);
    }
    if (*eap->arg == '/')   /* Match regexp, not just whole words */
    {
	whole = FALSE;
	++eap->arg;
	p = skip_regexp(eap->arg, '/', p_magic, NULL);
	if (*p)
	{
	    *p++ = NUL;
	    p = skipwhite(p);

	    /* Check for trailing illegal characters */
	    if (!ends_excmd(*p))
		eap->errmsg = e_trailing;
	    else
		eap->nextcmd = check_nextcmd(p);
	}
    }
    if (!eap->skip)
	find_pattern_in_path(eap->arg, 0, (int)STRLEN(eap->arg),
			    whole, !eap->forceit,
			    *eap->cmd == 'd' ?	FIND_DEFINE : FIND_ANY,
			    n, action, eap->line1, eap->line2);
}
#endif


#ifdef FEAT_QUICKFIX
/*
 * ":ptag", ":ptselect", ":ptjump", ":ptnext", etc.
 */
    static void
ex_ptag(exarg_T *eap)
{
    g_do_tagpreview = p_pvh;  /* will be reset to 0 in ex_tag_cmd() */
    ex_tag_cmd(eap, cmdnames[eap->cmdidx].cmd_name + 1);
}

/*
 * ":pedit"
 */
    static void
ex_pedit(exarg_T *eap)
{
    win_T	*curwin_save = curwin;

    // Open the preview window or popup and make it the current window.
    g_do_tagpreview = p_pvh;
    prepare_tagpreview(TRUE, TRUE, FALSE);

    // Edit the file.
    do_exedit(eap, NULL);

    if (curwin != curwin_save && win_valid(curwin_save))
    {
	// Return cursor to where we were
	validate_cursor();
	redraw_later(VALID);
	win_enter(curwin_save, TRUE);
    }
# ifdef FEAT_TEXT_PROP
    else if (WIN_IS_POPUP(curwin))
    {
	// can't keep focus in popup window
	win_enter(firstwin, TRUE);
    }
# endif
    g_do_tagpreview = 0;
}
#endif

/*
 * ":stag", ":stselect" and ":stjump".
 */
    static void
ex_stag(exarg_T *eap)
{
    postponed_split = -1;
    postponed_split_flags = cmdmod.split;
    postponed_split_tab = cmdmod.tab;
    ex_tag_cmd(eap, cmdnames[eap->cmdidx].cmd_name + 1);
    postponed_split_flags = 0;
    postponed_split_tab = 0;
}

/*
 * ":tag", ":tselect", ":tjump", ":tnext", etc.
 */
    static void
ex_tag(exarg_T *eap)
{
    ex_tag_cmd(eap, cmdnames[eap->cmdidx].cmd_name);
}

    static void
ex_tag_cmd(exarg_T *eap, char_u *name)
{
    int		cmd;

    switch (name[1])
    {
	case 'j': cmd = DT_JUMP;	/* ":tjump" */
		  break;
	case 's': cmd = DT_SELECT;	/* ":tselect" */
		  break;
	case 'p': cmd = DT_PREV;	/* ":tprevious" */
		  break;
	case 'N': cmd = DT_PREV;	/* ":tNext" */
		  break;
	case 'n': cmd = DT_NEXT;	/* ":tnext" */
		  break;
	case 'o': cmd = DT_POP;		/* ":pop" */
		  break;
	case 'f':			/* ":tfirst" */
	case 'r': cmd = DT_FIRST;	/* ":trewind" */
		  break;
	case 'l': cmd = DT_LAST;	/* ":tlast" */
		  break;
	default:			/* ":tag" */
#ifdef FEAT_CSCOPE
		  if (p_cst && *eap->arg != NUL)
		  {
		      ex_cstag(eap);
		      return;
		  }
#endif
		  cmd = DT_TAG;
		  break;
    }

    if (name[0] == 'l')
    {
#ifndef FEAT_QUICKFIX
	ex_ni(eap);
	return;
#else
	cmd = DT_LTAG;
#endif
    }

    do_tag(eap->arg, cmd, eap->addr_count > 0 ? (int)eap->line2 : 1,
							  eap->forceit, TRUE);
}

/*
 * Check "str" for starting with a special cmdline variable.
 * If found return one of the SPEC_ values and set "*usedlen" to the length of
 * the variable.  Otherwise return -1 and "*usedlen" is unchanged.
 */
    int
find_cmdline_var(char_u *src, int *usedlen)
{
    int		len;
    int		i;
    static char *(spec_str[]) = {
		    "%",
#define SPEC_PERC   0
		    "#",
#define SPEC_HASH   (SPEC_PERC + 1)
		    "<cword>",		/* cursor word */
#define SPEC_CWORD  (SPEC_HASH + 1)
		    "<cWORD>",		/* cursor WORD */
#define SPEC_CCWORD (SPEC_CWORD + 1)
		    "<cexpr>",		/* expr under cursor */
#define SPEC_CEXPR  (SPEC_CCWORD + 1)
		    "<cfile>",		/* cursor path name */
#define SPEC_CFILE  (SPEC_CEXPR + 1)
		    "<sfile>",		/* ":so" file name */
#define SPEC_SFILE  (SPEC_CFILE + 1)
		    "<slnum>",		/* ":so" file line number */
#define SPEC_SLNUM  (SPEC_SFILE + 1)
		    "<afile>",		/* autocommand file name */
#define SPEC_AFILE  (SPEC_SLNUM + 1)
		    "<abuf>",		/* autocommand buffer number */
#define SPEC_ABUF   (SPEC_AFILE + 1)
		    "<amatch>",		/* autocommand match name */
#define SPEC_AMATCH (SPEC_ABUF + 1)
		    "<sflnum>",		/* script file line number */
#define SPEC_SFLNUM  (SPEC_AMATCH + 1)
#ifdef FEAT_CLIENTSERVER
		    "<client>"
# define SPEC_CLIENT (SPEC_SFLNUM + 1)
#endif
    };

    for (i = 0; i < (int)(sizeof(spec_str) / sizeof(char *)); ++i)
    {
	len = (int)STRLEN(spec_str[i]);
	if (STRNCMP(src, spec_str[i], len) == 0)
	{
	    *usedlen = len;
	    return i;
	}
    }
    return -1;
}

/*
 * Evaluate cmdline variables.
 *
 * change '%'	    to curbuf->b_ffname
 *	  '#'	    to curwin->w_altfile
 *	  '<cword>' to word under the cursor
 *	  '<cWORD>' to WORD under the cursor
 *	  '<cexpr>' to C-expression under the cursor
 *	  '<cfile>' to path name under the cursor
 *	  '<sfile>' to sourced file name
 *	  '<slnum>' to sourced file line number
 *	  '<afile>' to file name for autocommand
 *	  '<abuf>'  to buffer number for autocommand
 *	  '<amatch>' to matching name for autocommand
 *
 * When an error is detected, "errormsg" is set to a non-NULL pointer (may be
 * "" for error without a message) and NULL is returned.
 * Returns an allocated string if a valid match was found.
 * Returns NULL if no match was found.	"usedlen" then still contains the
 * number of characters to skip.
 */
    char_u *
eval_vars(
    char_u	*src,		/* pointer into commandline */
    char_u	*srcstart,	/* beginning of valid memory for src */
    int		*usedlen,	/* characters after src that are used */
    linenr_T	*lnump,		/* line number for :e command, or NULL */
    char	**errormsg,	/* pointer to error message */
    int		*escaped)	/* return value has escaped white space (can
				 * be NULL) */
{
    int		i;
    char_u	*s;
    char_u	*result;
    char_u	*resultbuf = NULL;
    int		resultlen;
    buf_T	*buf;
    int		valid = VALID_HEAD + VALID_PATH;    /* assume valid result */
    int		spec_idx;
    int		tilde_file = FALSE;
    int		skip_mod = FALSE;
    char_u	strbuf[30];

    *errormsg = NULL;
    if (escaped != NULL)
	*escaped = FALSE;

    /*
     * Check if there is something to do.
     */
    spec_idx = find_cmdline_var(src, usedlen);
    if (spec_idx < 0)	/* no match */
    {
	*usedlen = 1;
	return NULL;
    }

    /*
     * Skip when preceded with a backslash "\%" and "\#".
     * Note: In "\\%" the % is also not recognized!
     */
    if (src > srcstart && src[-1] == '\\')
    {
	*usedlen = 0;
	STRMOVE(src - 1, src);	/* remove backslash */
	return NULL;
    }

    /*
     * word or WORD under cursor
     */
    if (spec_idx == SPEC_CWORD || spec_idx == SPEC_CCWORD
						     || spec_idx == SPEC_CEXPR)
    {
	resultlen = find_ident_under_cursor(&result,
		spec_idx == SPEC_CWORD ? (FIND_IDENT | FIND_STRING)
	      : spec_idx == SPEC_CEXPR ? (FIND_IDENT | FIND_STRING | FIND_EVAL)
	      : FIND_STRING);
	if (resultlen == 0)
	{
	    *errormsg = "";
	    return NULL;
	}
    }

    /*
     * '#': Alternate file name
     * '%': Current file name
     *	    File name under the cursor
     *	    File name for autocommand
     *	and following modifiers
     */
    else
    {
	switch (spec_idx)
	{
	case SPEC_PERC:		/* '%': current file */
		if (curbuf->b_fname == NULL)
		{
		    result = (char_u *)"";
		    valid = 0;	    /* Must have ":p:h" to be valid */
		}
		else
		{
		    result = curbuf->b_fname;
		    tilde_file = STRCMP(result, "~") == 0;
		}
		break;

	case SPEC_HASH:		/* '#' or "#99": alternate file */
		if (src[1] == '#')  /* "##": the argument list */
		{
		    result = arg_all();
		    resultbuf = result;
		    *usedlen = 2;
		    if (escaped != NULL)
			*escaped = TRUE;
		    skip_mod = TRUE;
		    break;
		}
		s = src + 1;
		if (*s == '<')		/* "#<99" uses v:oldfiles */
		    ++s;
		i = (int)getdigits(&s);
		if (s == src + 2 && src[1] == '-')
		    /* just a minus sign, don't skip over it */
		    s--;
		*usedlen = (int)(s - src); /* length of what we expand */

		if (src[1] == '<' && i != 0)
		{
		    if (*usedlen < 2)
		    {
			/* Should we give an error message for #<text? */
			*usedlen = 1;
			return NULL;
		    }
#ifdef FEAT_EVAL
		    result = list_find_str(get_vim_var_list(VV_OLDFILES),
								     (long)i);
		    if (result == NULL)
		    {
			*errormsg = "";
			return NULL;
		    }
#else
		    *errormsg = _("E809: #< is not available without the +eval feature");
		    return NULL;
#endif
		}
		else
		{
		    if (i == 0 && src[1] == '<' && *usedlen > 1)
			*usedlen = 1;
		    buf = buflist_findnr(i);
		    if (buf == NULL)
		    {
			*errormsg = _("E194: No alternate file name to substitute for '#'");
			return NULL;
		    }
		    if (lnump != NULL)
			*lnump = ECMD_LAST;
		    if (buf->b_fname == NULL)
		    {
			result = (char_u *)"";
			valid = 0;	    /* Must have ":p:h" to be valid */
		    }
		    else
		    {
			result = buf->b_fname;
			tilde_file = STRCMP(result, "~") == 0;
		    }
		}
		break;

#ifdef FEAT_SEARCHPATH
	case SPEC_CFILE:	/* file name under cursor */
		result = file_name_at_cursor(FNAME_MESS|FNAME_HYP, 1L, NULL);
		if (result == NULL)
		{
		    *errormsg = "";
		    return NULL;
		}
		resultbuf = result;	    /* remember allocated string */
		break;
#endif

	case SPEC_AFILE:	/* file name for autocommand */
		result = autocmd_fname;
		if (result != NULL && !autocmd_fname_full)
		{
		    /* Still need to turn the fname into a full path.  It is
		     * postponed to avoid a delay when <afile> is not used. */
		    autocmd_fname_full = TRUE;
		    result = FullName_save(autocmd_fname, FALSE);
		    vim_free(autocmd_fname);
		    autocmd_fname = result;
		}
		if (result == NULL)
		{
		    *errormsg = _("E495: no autocommand file name to substitute for \"<afile>\"");
		    return NULL;
		}
		result = shorten_fname1(result);
		break;

	case SPEC_ABUF:		/* buffer number for autocommand */
		if (autocmd_bufnr <= 0)
		{
		    *errormsg = _("E496: no autocommand buffer number to substitute for \"<abuf>\"");
		    return NULL;
		}
		sprintf((char *)strbuf, "%d", autocmd_bufnr);
		result = strbuf;
		break;

	case SPEC_AMATCH:	/* match name for autocommand */
		result = autocmd_match;
		if (result == NULL)
		{
		    *errormsg = _("E497: no autocommand match name to substitute for \"<amatch>\"");
		    return NULL;
		}
		break;

	case SPEC_SFILE:	/* file name for ":so" command */
		result = sourcing_name;
		if (result == NULL)
		{
		    *errormsg = _("E498: no :source file name to substitute for \"<sfile>\"");
		    return NULL;
		}
		break;

	case SPEC_SLNUM:	/* line in file for ":so" command */
		if (sourcing_name == NULL || sourcing_lnum == 0)
		{
		    *errormsg = _("E842: no line number to use for \"<slnum>\"");
		    return NULL;
		}
		sprintf((char *)strbuf, "%ld", (long)sourcing_lnum);
		result = strbuf;
		break;

#ifdef FEAT_EVAL
	case SPEC_SFLNUM:	/* line in script file */
		if (current_sctx.sc_lnum + sourcing_lnum == 0)
		{
		    *errormsg = _("E961: no line number to use for \"<sflnum>\"");
		    return NULL;
		}
		sprintf((char *)strbuf, "%ld",
				 (long)(current_sctx.sc_lnum + sourcing_lnum));
		result = strbuf;
		break;
#endif

#ifdef FEAT_CLIENTSERVER
	case SPEC_CLIENT:	/* Source of last submitted input */
		sprintf((char *)strbuf, PRINTF_HEX_LONG_U,
							(long_u)clientWindow);
		result = strbuf;
		break;
#endif

	default:
		result = (char_u *)""; /* avoid gcc warning */
		break;
	}

	resultlen = (int)STRLEN(result);	/* length of new string */
	if (src[*usedlen] == '<')	/* remove the file name extension */
	{
	    ++*usedlen;
	    if ((s = vim_strrchr(result, '.')) != NULL && s >= gettail(result))
		resultlen = (int)(s - result);
	}
	else if (!skip_mod)
	{
	    valid |= modify_fname(src, tilde_file, usedlen, &result, &resultbuf,
								  &resultlen);
	    if (result == NULL)
	    {
		*errormsg = "";
		return NULL;
	    }
	}
    }

    if (resultlen == 0 || valid != VALID_HEAD + VALID_PATH)
    {
	if (valid != VALID_HEAD + VALID_PATH)
	    /* xgettext:no-c-format */
	    *errormsg = _("E499: Empty file name for '%' or '#', only works with \":p:h\"");
	else
	    *errormsg = _("E500: Evaluates to an empty string");
	result = NULL;
    }
    else
	result = vim_strnsave(result, resultlen);
    vim_free(resultbuf);
    return result;
}

/*
 * Expand the <sfile> string in "arg".
 *
 * Returns an allocated string, or NULL for any error.
 */
    char_u *
expand_sfile(char_u *arg)
{
    char	*errormsg;
    int		len;
    char_u	*result;
    char_u	*newres;
    char_u	*repl;
    int		srclen;
    char_u	*p;

    result = vim_strsave(arg);
    if (result == NULL)
	return NULL;

    for (p = result; *p; )
    {
	if (STRNCMP(p, "<sfile>", 7) != 0)
	    ++p;
	else
	{
	    /* replace "<sfile>" with the sourced file name, and do ":" stuff */
	    repl = eval_vars(p, result, &srclen, NULL, &errormsg, NULL);
	    if (errormsg != NULL)
	    {
		if (*errormsg)
		    emsg(errormsg);
		vim_free(result);
		return NULL;
	    }
	    if (repl == NULL)		/* no match (cannot happen) */
	    {
		p += srclen;
		continue;
	    }
	    len = (int)STRLEN(result) - srclen + (int)STRLEN(repl) + 1;
	    newres = alloc(len);
	    if (newres == NULL)
	    {
		vim_free(repl);
		vim_free(result);
		return NULL;
	    }
	    mch_memmove(newres, result, (size_t)(p - result));
	    STRCPY(newres + (p - result), repl);
	    len = (int)STRLEN(newres);
	    STRCAT(newres, p + srclen);
	    vim_free(repl);
	    vim_free(result);
	    result = newres;
	    p = newres + len;		/* continue after the match */
	}
    }

    return result;
}

#if defined(FEAT_GUI_DIALOG) || defined(FEAT_CON_DIALOG) || defined(PROTO)
/*
 * Make a dialog message in "buff[DIALOG_MSG_SIZE]".
 * "format" must contain "%s".
 */
    void
dialog_msg(char_u *buff, char *format, char_u *fname)
{
    if (fname == NULL)
	fname = (char_u *)_("Untitled");
    vim_snprintf((char *)buff, DIALOG_MSG_SIZE, format, fname);
}
#endif

/*
 * ":behave {mswin,xterm}"
 */
    static void
ex_behave(exarg_T *eap)
{
    if (STRCMP(eap->arg, "mswin") == 0)
    {
	set_option_value((char_u *)"selection", 0L, (char_u *)"exclusive", 0);
	set_option_value((char_u *)"selectmode", 0L, (char_u *)"mouse,key", 0);
	set_option_value((char_u *)"mousemodel", 0L, (char_u *)"popup", 0);
	set_option_value((char_u *)"keymodel", 0L,
					     (char_u *)"startsel,stopsel", 0);
    }
    else if (STRCMP(eap->arg, "xterm") == 0)
    {
	set_option_value((char_u *)"selection", 0L, (char_u *)"inclusive", 0);
	set_option_value((char_u *)"selectmode", 0L, (char_u *)"", 0);
	set_option_value((char_u *)"mousemodel", 0L, (char_u *)"extend", 0);
	set_option_value((char_u *)"keymodel", 0L, (char_u *)"", 0);
    }
    else
	semsg(_(e_invarg2), eap->arg);
}

static int filetype_detect = FALSE;
static int filetype_plugin = FALSE;
static int filetype_indent = FALSE;

/*
 * ":filetype [plugin] [indent] {on,off,detect}"
 * on: Load the filetype.vim file to install autocommands for file types.
 * off: Load the ftoff.vim file to remove all autocommands for file types.
 * plugin on: load filetype.vim and ftplugin.vim
 * plugin off: load ftplugof.vim
 * indent on: load filetype.vim and indent.vim
 * indent off: load indoff.vim
 */
    static void
ex_filetype(exarg_T *eap)
{
    char_u	*arg = eap->arg;
    int		plugin = FALSE;
    int		indent = FALSE;

    if (*eap->arg == NUL)
    {
	/* Print current status. */
	smsg("filetype detection:%s  plugin:%s  indent:%s",
		filetype_detect ? "ON" : "OFF",
		filetype_plugin ? (filetype_detect ? "ON" : "(on)") : "OFF",
		filetype_indent ? (filetype_detect ? "ON" : "(on)") : "OFF");
	return;
    }

    /* Accept "plugin" and "indent" in any order. */
    for (;;)
    {
	if (STRNCMP(arg, "plugin", 6) == 0)
	{
	    plugin = TRUE;
	    arg = skipwhite(arg + 6);
	    continue;
	}
	if (STRNCMP(arg, "indent", 6) == 0)
	{
	    indent = TRUE;
	    arg = skipwhite(arg + 6);
	    continue;
	}
	break;
    }
    if (STRCMP(arg, "on") == 0 || STRCMP(arg, "detect") == 0)
    {
	if (*arg == 'o' || !filetype_detect)
	{
	    source_runtime((char_u *)FILETYPE_FILE, DIP_ALL);
	    filetype_detect = TRUE;
	    if (plugin)
	    {
		source_runtime((char_u *)FTPLUGIN_FILE, DIP_ALL);
		filetype_plugin = TRUE;
	    }
	    if (indent)
	    {
		source_runtime((char_u *)INDENT_FILE, DIP_ALL);
		filetype_indent = TRUE;
	    }
	}
	if (*arg == 'd')
	{
	    (void)do_doautocmd((char_u *)"filetypedetect BufRead", TRUE, NULL);
	    do_modelines(0);
	}
    }
    else if (STRCMP(arg, "off") == 0)
    {
	if (plugin || indent)
	{
	    if (plugin)
	    {
		source_runtime((char_u *)FTPLUGOF_FILE, DIP_ALL);
		filetype_plugin = FALSE;
	    }
	    if (indent)
	    {
		source_runtime((char_u *)INDOFF_FILE, DIP_ALL);
		filetype_indent = FALSE;
	    }
	}
	else
	{
	    source_runtime((char_u *)FTOFF_FILE, DIP_ALL);
	    filetype_detect = FALSE;
	}
    }
    else
	semsg(_(e_invarg2), arg);
}

/*
 * ":setfiletype [FALLBACK] {name}"
 */
    static void
ex_setfiletype(exarg_T *eap)
{
    if (!did_filetype)
    {
	char_u *arg = eap->arg;

	if (STRNCMP(arg, "FALLBACK ", 9) == 0)
	    arg += 9;

	set_option_value((char_u *)"filetype", 0L, arg, OPT_LOCAL);
	if (arg != eap->arg)
	    did_filetype = FALSE;
    }
}

    static void
ex_digraphs(exarg_T *eap UNUSED)
{
#ifdef FEAT_DIGRAPHS
    if (*eap->arg != NUL)
	putdigraph(eap->arg);
    else
	listdigraphs(eap->forceit);
#else
    emsg(_("E196: No digraphs in this version"));
#endif
}

    static void
ex_set(exarg_T *eap)
{
    int		flags = 0;

    if (eap->cmdidx == CMD_setlocal)
	flags = OPT_LOCAL;
    else if (eap->cmdidx == CMD_setglobal)
	flags = OPT_GLOBAL;
#if defined(FEAT_EVAL) && defined(FEAT_BROWSE)
    if (cmdmod.browse && flags == 0)
	ex_options(eap);
    else
#endif
	(void)do_set(eap->arg, flags);
}

#if defined(FEAT_SEARCH_EXTRA) || defined(PROTO)
    void
set_no_hlsearch(int flag)
{
    no_hlsearch = flag;
# ifdef FEAT_EVAL
    set_vim_var_nr(VV_HLSEARCH, !no_hlsearch && p_hls);
# endif
}

/*
 * ":nohlsearch"
 */
    static void
ex_nohlsearch(exarg_T *eap UNUSED)
{
    set_no_hlsearch(TRUE);
    redraw_all_later(SOME_VALID);
}
#endif

#ifdef FEAT_CRYPT
/*
 * ":X": Get crypt key
 */
    static void
ex_X(exarg_T *eap UNUSED)
{
    crypt_check_current_method();
    (void)crypt_get_key(TRUE, TRUE);
}
#endif

#ifdef FEAT_FOLDING
    static void
ex_fold(exarg_T *eap)
{
    if (foldManualAllowed(TRUE))
	foldCreate(eap->line1, eap->line2);
}

    static void
ex_foldopen(exarg_T *eap)
{
    opFoldRange(eap->line1, eap->line2, eap->cmdidx == CMD_foldopen,
							 eap->forceit, FALSE);
}

    static void
ex_folddo(exarg_T *eap)
{
    linenr_T	lnum;

# ifdef FEAT_CLIPBOARD
    start_global_changes();
# endif

    /* First set the marks for all lines closed/open. */
    for (lnum = eap->line1; lnum <= eap->line2; ++lnum)
	if (hasFolding(lnum, NULL, NULL) == (eap->cmdidx == CMD_folddoclosed))
	    ml_setmarked(lnum);

    /* Execute the command on the marked lines. */
    global_exe(eap->arg);
    ml_clearmarked();	   /* clear rest of the marks */
# ifdef FEAT_CLIPBOARD
    end_global_changes();
# endif
}
#endif

#ifdef FEAT_QUICKFIX
/*
 * Returns TRUE if the supplied Ex cmdidx is for a location list command
 * instead of a quickfix command.
 */
    int
is_loclist_cmd(int cmdidx)
{
    if (cmdidx < 0 || cmdidx >= CMD_SIZE)
	return FALSE;
    return cmdnames[cmdidx].cmd_name[0] == 'l';
}
#endif

#if defined(FEAT_TIMERS) || defined(PROTO)
    int
get_pressedreturn(void)
{
    return ex_pressedreturn;
}

    void
set_pressedreturn(int val)
{
     ex_pressedreturn = val;
}
#endif
