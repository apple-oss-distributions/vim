/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * profiler.c: vim script profiler
 */

#include "vim.h"

#if defined(FEAT_EVAL) || defined(PROTO)
# if defined(FEAT_PROFILE) || defined(FEAT_RELTIME) || defined(PROTO)
/*
 * Store the current time in "tm".
 */
    void
profile_start(proftime_T *tm)
{
# ifdef MSWIN
    QueryPerformanceCounter(tm);
# else
    gettimeofday(tm, NULL);
# endif
}

/*
 * Compute the elapsed time from "tm" till now and store in "tm".
 */
    void
profile_end(proftime_T *tm)
{
    proftime_T now;

# ifdef MSWIN
    QueryPerformanceCounter(&now);
    tm->QuadPart = now.QuadPart - tm->QuadPart;
# else
    gettimeofday(&now, NULL);
    tm->tv_usec = now.tv_usec - tm->tv_usec;
    tm->tv_sec = now.tv_sec - tm->tv_sec;
    if (tm->tv_usec < 0)
    {
	tm->tv_usec += 1000000;
	--tm->tv_sec;
    }
# endif
}

/*
 * Subtract the time "tm2" from "tm".
 */
    void
profile_sub(proftime_T *tm, proftime_T *tm2)
{
# ifdef MSWIN
    tm->QuadPart -= tm2->QuadPart;
# else
    tm->tv_usec -= tm2->tv_usec;
    tm->tv_sec -= tm2->tv_sec;
    if (tm->tv_usec < 0)
    {
	tm->tv_usec += 1000000;
	--tm->tv_sec;
    }
# endif
}

/*
 * Return a string that represents the time in "tm".
 * Uses a static buffer!
 */
    char *
profile_msg(proftime_T *tm)
{
    static char buf[50];

# ifdef MSWIN
    LARGE_INTEGER   fr;

    QueryPerformanceFrequency(&fr);
    sprintf(buf, "%10.6lf", (double)tm->QuadPart / (double)fr.QuadPart);
# else
    sprintf(buf, "%3ld.%06ld", (long)tm->tv_sec, (long)tm->tv_usec);
# endif
    return buf;
}

# if defined(FEAT_FLOAT) || defined(PROTO)
/*
 * Return a float that represents the time in "tm".
 */
    float_T
profile_float(proftime_T *tm)
{
#  ifdef MSWIN
    LARGE_INTEGER   fr;

    QueryPerformanceFrequency(&fr);
    return (float_T)tm->QuadPart / (float_T)fr.QuadPart;
#  else
    return (float_T)tm->tv_sec + (float_T)tm->tv_usec / 1000000.0;
#  endif
}
# endif

/*
 * Put the time "msec" past now in "tm".
 */
    void
profile_setlimit(long msec, proftime_T *tm)
{
    if (msec <= 0)   // no limit
	profile_zero(tm);
    else
    {
# ifdef MSWIN
	LARGE_INTEGER   fr;

	QueryPerformanceCounter(tm);
	QueryPerformanceFrequency(&fr);
	tm->QuadPart += (LONGLONG)((double)msec / 1000.0 * (double)fr.QuadPart);
# else
	long	    usec;

	gettimeofday(tm, NULL);
	usec = (long)tm->tv_usec + (long)msec * 1000;
	tm->tv_usec = usec % 1000000L;
	tm->tv_sec += usec / 1000000L;
# endif
    }
}

/*
 * Return TRUE if the current time is past "tm".
 */
    int
profile_passed_limit(proftime_T *tm)
{
    proftime_T	now;

# ifdef MSWIN
    if (tm->QuadPart == 0)  // timer was not set
	return FALSE;
    QueryPerformanceCounter(&now);
    return (now.QuadPart > tm->QuadPart);
# else
    if (tm->tv_sec == 0)    // timer was not set
	return FALSE;
    gettimeofday(&now, NULL);
    return (now.tv_sec > tm->tv_sec
	    || (now.tv_sec == tm->tv_sec && now.tv_usec > tm->tv_usec));
# endif
}

/*
 * Set the time in "tm" to zero.
 */
    void
profile_zero(proftime_T *tm)
{
# ifdef MSWIN
    tm->QuadPart = 0;
# else
    tm->tv_usec = 0;
    tm->tv_sec = 0;
# endif
}

# endif  // FEAT_PROFILE || FEAT_RELTIME

#if defined(FEAT_SYN_HL) && defined(FEAT_RELTIME) && defined(FEAT_FLOAT) && defined(FEAT_PROFILE)
# if defined(HAVE_MATH_H)
#  include <math.h>
# endif

/*
 * Divide the time "tm" by "count" and store in "tm2".
 */
    void
profile_divide(proftime_T *tm, int count, proftime_T *tm2)
{
    if (count == 0)
	profile_zero(tm2);
    else
    {
# ifdef MSWIN
	tm2->QuadPart = tm->QuadPart / count;
# else
	double usec = (tm->tv_sec * 1000000.0 + tm->tv_usec) / count;

	tm2->tv_sec = floor(usec / 1000000.0);
	tm2->tv_usec = vim_round(usec - (tm2->tv_sec * 1000000.0));
# endif
    }
}
#endif

# if defined(FEAT_PROFILE) || defined(PROTO)
/*
 * Functions for profiling.
 */
static proftime_T prof_wait_time;

/*
 * Add the time "tm2" to "tm".
 */
    void
profile_add(proftime_T *tm, proftime_T *tm2)
{
# ifdef MSWIN
    tm->QuadPart += tm2->QuadPart;
# else
    tm->tv_usec += tm2->tv_usec;
    tm->tv_sec += tm2->tv_sec;
    if (tm->tv_usec >= 1000000)
    {
	tm->tv_usec -= 1000000;
	++tm->tv_sec;
    }
# endif
}

/*
 * Add the "self" time from the total time and the children's time.
 */
    void
profile_self(proftime_T *self, proftime_T *total, proftime_T *children)
{
    // Check that the result won't be negative.  Can happen with recursive
    // calls.
#ifdef MSWIN
    if (total->QuadPart <= children->QuadPart)
	return;
#else
    if (total->tv_sec < children->tv_sec
	    || (total->tv_sec == children->tv_sec
		&& total->tv_usec <= children->tv_usec))
	return;
#endif
    profile_add(self, total);
    profile_sub(self, children);
}

/*
 * Get the current waittime.
 */
    static void
profile_get_wait(proftime_T *tm)
{
    *tm = prof_wait_time;
}

/*
 * Subtract the passed waittime since "tm" from "tma".
 */
    void
profile_sub_wait(proftime_T *tm, proftime_T *tma)
{
    proftime_T tm3 = prof_wait_time;

    profile_sub(&tm3, tm);
    profile_sub(tma, &tm3);
}

/*
 * Return TRUE if "tm1" and "tm2" are equal.
 */
    static int
profile_equal(proftime_T *tm1, proftime_T *tm2)
{
# ifdef MSWIN
    return (tm1->QuadPart == tm2->QuadPart);
# else
    return (tm1->tv_usec == tm2->tv_usec && tm1->tv_sec == tm2->tv_sec);
# endif
}

/*
 * Return <0, 0 or >0 if "tm1" < "tm2", "tm1" == "tm2" or "tm1" > "tm2"
 */
    int
profile_cmp(const proftime_T *tm1, const proftime_T *tm2)
{
# ifdef MSWIN
    return (int)(tm2->QuadPart - tm1->QuadPart);
# else
    if (tm1->tv_sec == tm2->tv_sec)
	return tm2->tv_usec - tm1->tv_usec;
    return tm2->tv_sec - tm1->tv_sec;
# endif
}

static char_u	*profile_fname = NULL;
static proftime_T pause_time;

/*
 * ":profile cmd args"
 */
    void
ex_profile(exarg_T *eap)
{
    char_u	*e;
    int		len;

    e = skiptowhite(eap->arg);
    len = (int)(e - eap->arg);
    e = skipwhite(e);

    if (len == 5 && STRNCMP(eap->arg, "start", 5) == 0 && *e != NUL)
    {
	vim_free(profile_fname);
	profile_fname = expand_env_save_opt(e, TRUE);
	do_profiling = PROF_YES;
	profile_zero(&prof_wait_time);
	set_vim_var_nr(VV_PROFILING, 1L);
    }
    else if (do_profiling == PROF_NONE)
	emsg(_("E750: First use \":profile start {fname}\""));
    else if (STRCMP(eap->arg, "pause") == 0)
    {
	if (do_profiling == PROF_YES)
	    profile_start(&pause_time);
	do_profiling = PROF_PAUSED;
    }
    else if (STRCMP(eap->arg, "continue") == 0)
    {
	if (do_profiling == PROF_PAUSED)
	{
	    profile_end(&pause_time);
	    profile_add(&prof_wait_time, &pause_time);
	}
	do_profiling = PROF_YES;
    }
    else
    {
	// The rest is similar to ":breakadd".
	ex_breakadd(eap);
    }
}

// Command line expansion for :profile.
static enum
{
    PEXP_SUBCMD,	// expand :profile sub-commands
    PEXP_FUNC		// expand :profile func {funcname}
} pexpand_what;

static char *pexpand_cmds[] = {
			"start",
#define PROFCMD_START	0
			"pause",
#define PROFCMD_PAUSE	1
			"continue",
#define PROFCMD_CONTINUE 2
			"func",
#define PROFCMD_FUNC	3
			"file",
#define PROFCMD_FILE	4
			NULL
#define PROFCMD_LAST	5
};

/*
 * Function given to ExpandGeneric() to obtain the profile command
 * specific expansion.
 */
    char_u *
get_profile_name(expand_T *xp UNUSED, int idx)
{
    switch (pexpand_what)
    {
    case PEXP_SUBCMD:
	return (char_u *)pexpand_cmds[idx];
    // case PEXP_FUNC: TODO
    default:
	return NULL;
    }
}

/*
 * Handle command line completion for :profile command.
 */
    void
set_context_in_profile_cmd(expand_T *xp, char_u *arg)
{
    char_u	*end_subcmd;

    // Default: expand subcommands.
    xp->xp_context = EXPAND_PROFILE;
    pexpand_what = PEXP_SUBCMD;
    xp->xp_pattern = arg;

    end_subcmd = skiptowhite(arg);
    if (*end_subcmd == NUL)
	return;

    if (end_subcmd - arg == 5 && STRNCMP(arg, "start", 5) == 0)
    {
	xp->xp_context = EXPAND_FILES;
	xp->xp_pattern = skipwhite(end_subcmd);
	return;
    }

    // TODO: expand function names after "func"
    xp->xp_context = EXPAND_NOTHING;
}

static proftime_T inchar_time;

/*
 * Called when starting to wait for the user to type a character.
 */
    void
prof_inchar_enter(void)
{
    profile_start(&inchar_time);
}

/*
 * Called when finished waiting for the user to type a character.
 */
    void
prof_inchar_exit(void)
{
    profile_end(&inchar_time);
    profile_add(&prof_wait_time, &inchar_time);
}


/*
 * Return TRUE when a function defined in the current script should be
 * profiled.
 */
    int
prof_def_func(void)
{
    if (current_sctx.sc_sid > 0)
	return SCRIPT_ITEM(current_sctx.sc_sid)->sn_pr_force;
    return FALSE;
}

/*
 * Print the count and times for one function or function line.
 */
    static void
prof_func_line(
    FILE	*fd,
    int		count,
    proftime_T	*total,
    proftime_T	*self,
    int		prefer_self)	// when equal print only self time
{
    if (count > 0)
    {
	fprintf(fd, "%5d ", count);
	if (prefer_self && profile_equal(total, self))
	    fprintf(fd, "           ");
	else
	    fprintf(fd, "%s ", profile_msg(total));
	if (!prefer_self && profile_equal(total, self))
	    fprintf(fd, "           ");
	else
	    fprintf(fd, "%s ", profile_msg(self));
    }
    else
	fprintf(fd, "                            ");
}

    static void
prof_sort_list(
    FILE	*fd,
    ufunc_T	**sorttab,
    int		st_len,
    char	*title,
    int		prefer_self)	// when equal print only self time
{
    int		i;
    ufunc_T	*fp;

    fprintf(fd, "FUNCTIONS SORTED ON %s TIME\n", title);
    fprintf(fd, "count  total (s)   self (s)  function\n");
    for (i = 0; i < 20 && i < st_len; ++i)
    {
	fp = sorttab[i];
	prof_func_line(fd, fp->uf_tm_count, &fp->uf_tm_total, &fp->uf_tm_self,
								 prefer_self);
	if (fp->uf_name[0] == K_SPECIAL)
	    fprintf(fd, " <SNR>%s()\n", fp->uf_name + 3);
	else
	    fprintf(fd, " %s()\n", fp->uf_name);
    }
    fprintf(fd, "\n");
}

/*
 * Compare function for total time sorting.
 */
    static int
prof_total_cmp(const void *s1, const void *s2)
{
    ufunc_T	*p1, *p2;

    p1 = *(ufunc_T **)s1;
    p2 = *(ufunc_T **)s2;
    return profile_cmp(&p1->uf_tm_total, &p2->uf_tm_total);
}

/*
 * Compare function for self time sorting.
 */
    static int
prof_self_cmp(const void *s1, const void *s2)
{
    ufunc_T	*p1, *p2;

    p1 = *(ufunc_T **)s1;
    p2 = *(ufunc_T **)s2;
    return profile_cmp(&p1->uf_tm_self, &p2->uf_tm_self);
}

/*
 * Start profiling function "fp".
 */
    void
func_do_profile(ufunc_T *fp)
{
    int		len = fp->uf_lines.ga_len;

    if (!fp->uf_prof_initialized)
    {
	if (len == 0)
	    len = 1;  // avoid getting error for allocating zero bytes
	fp->uf_tm_count = 0;
	profile_zero(&fp->uf_tm_self);
	profile_zero(&fp->uf_tm_total);
	if (fp->uf_tml_count == NULL)
	    fp->uf_tml_count = ALLOC_CLEAR_MULT(int, len);
	if (fp->uf_tml_total == NULL)
	    fp->uf_tml_total = ALLOC_CLEAR_MULT(proftime_T, len);
	if (fp->uf_tml_self == NULL)
	    fp->uf_tml_self = ALLOC_CLEAR_MULT(proftime_T, len);
	fp->uf_tml_idx = -1;
	if (fp->uf_tml_count == NULL || fp->uf_tml_total == NULL
						    || fp->uf_tml_self == NULL)
	    return;	    // out of memory
	fp->uf_prof_initialized = TRUE;
    }

    fp->uf_profiling = TRUE;
}

/*
 * Prepare profiling for entering a child or something else that is not
 * counted for the script/function itself.
 * Should always be called in pair with prof_child_exit().
 */
    void
prof_child_enter(
    proftime_T *tm)	// place to store waittime
{
    funccall_T *fc = get_current_funccal();

    if (fc != NULL && fc->func->uf_profiling)
	profile_start(&fc->prof_child);
    script_prof_save(tm);
}

/*
 * Take care of time spent in a child.
 * Should always be called after prof_child_enter().
 */
    void
prof_child_exit(
    proftime_T *tm)	// where waittime was stored
{
    funccall_T *fc = get_current_funccal();

    if (fc != NULL && fc->func->uf_profiling)
    {
	profile_end(&fc->prof_child);
	profile_sub_wait(tm, &fc->prof_child); // don't count waiting time
	profile_add(&fc->func->uf_tm_children, &fc->prof_child);
	profile_add(&fc->func->uf_tml_children, &fc->prof_child);
    }
    script_prof_restore(tm);
}

/*
 * Called when starting to read a function line.
 * "sourcing_lnum" must be correct!
 * When skipping lines it may not actually be executed, but we won't find out
 * until later and we need to store the time now.
 */
    void
func_line_start(void *cookie)
{
    funccall_T	*fcp = (funccall_T *)cookie;
    ufunc_T	*fp = fcp->func;

    if (fp->uf_profiling && SOURCING_LNUM >= 1
				      && SOURCING_LNUM <= fp->uf_lines.ga_len)
    {
	fp->uf_tml_idx = SOURCING_LNUM - 1;
	// Skip continuation lines.
	while (fp->uf_tml_idx > 0 && FUNCLINE(fp, fp->uf_tml_idx) == NULL)
	    --fp->uf_tml_idx;
	fp->uf_tml_execed = FALSE;
	profile_start(&fp->uf_tml_start);
	profile_zero(&fp->uf_tml_children);
	profile_get_wait(&fp->uf_tml_wait);
    }
}

/*
 * Called when actually executing a function line.
 */
    void
func_line_exec(void *cookie)
{
    funccall_T	*fcp = (funccall_T *)cookie;
    ufunc_T	*fp = fcp->func;

    if (fp->uf_profiling && fp->uf_tml_idx >= 0)
	fp->uf_tml_execed = TRUE;
}

/*
 * Called when done with a function line.
 */
    void
func_line_end(void *cookie)
{
    funccall_T	*fcp = (funccall_T *)cookie;
    ufunc_T	*fp = fcp->func;

    if (fp->uf_profiling && fp->uf_tml_idx >= 0)
    {
	if (fp->uf_tml_execed)
	{
	    ++fp->uf_tml_count[fp->uf_tml_idx];
	    profile_end(&fp->uf_tml_start);
	    profile_sub_wait(&fp->uf_tml_wait, &fp->uf_tml_start);
	    profile_add(&fp->uf_tml_total[fp->uf_tml_idx], &fp->uf_tml_start);
	    profile_self(&fp->uf_tml_self[fp->uf_tml_idx], &fp->uf_tml_start,
							&fp->uf_tml_children);
	}
	fp->uf_tml_idx = -1;
    }
}

/*
 * Dump the profiling results for all functions in file "fd".
 */
    static void
func_dump_profile(FILE *fd)
{
    hashtab_T	*functbl;
    hashitem_T	*hi;
    int		todo;
    ufunc_T	*fp;
    int		i;
    ufunc_T	**sorttab;
    int		st_len = 0;
    char_u	*p;

    functbl = func_tbl_get();
    todo = (int)functbl->ht_used;
    if (todo == 0)
	return;     // nothing to dump

    sorttab = ALLOC_MULT(ufunc_T *, todo);

    for (hi = functbl->ht_array; todo > 0; ++hi)
    {
	if (!HASHITEM_EMPTY(hi))
	{
	    --todo;
	    fp = HI2UF(hi);
	    if (fp->uf_prof_initialized)
	    {
		if (sorttab != NULL)
		    sorttab[st_len++] = fp;

		if (fp->uf_name[0] == K_SPECIAL)
		    fprintf(fd, "FUNCTION  <SNR>%s()\n", fp->uf_name + 3);
		else
		    fprintf(fd, "FUNCTION  %s()\n", fp->uf_name);
		if (fp->uf_script_ctx.sc_sid > 0)
		{
		    p = home_replace_save(NULL,
				     get_scriptname(fp->uf_script_ctx.sc_sid));
		    if (p != NULL)
		    {
			fprintf(fd, "    Defined: %s:%ld\n",
					   p, (long)fp->uf_script_ctx.sc_lnum);
			vim_free(p);
		    }
		}
		if (fp->uf_tm_count == 1)
		    fprintf(fd, "Called 1 time\n");
		else
		    fprintf(fd, "Called %d times\n", fp->uf_tm_count);
		fprintf(fd, "Total time: %s\n", profile_msg(&fp->uf_tm_total));
		fprintf(fd, " Self time: %s\n", profile_msg(&fp->uf_tm_self));
		fprintf(fd, "\n");
		fprintf(fd, "count  total (s)   self (s)\n");

		for (i = 0; i < fp->uf_lines.ga_len; ++i)
		{
		    if (FUNCLINE(fp, i) == NULL)
			continue;
		    prof_func_line(fd, fp->uf_tml_count[i],
			     &fp->uf_tml_total[i], &fp->uf_tml_self[i], TRUE);
		    fprintf(fd, "%s\n", FUNCLINE(fp, i));
		}
		fprintf(fd, "\n");
	    }
	}
    }

    if (sorttab != NULL && st_len > 0)
    {
	qsort((void *)sorttab, (size_t)st_len, sizeof(ufunc_T *),
							      prof_total_cmp);
	prof_sort_list(fd, sorttab, st_len, "TOTAL", FALSE);
	qsort((void *)sorttab, (size_t)st_len, sizeof(ufunc_T *),
							      prof_self_cmp);
	prof_sort_list(fd, sorttab, st_len, "SELF", TRUE);
    }

    vim_free(sorttab);
}

/*
 * Start profiling script "fp".
 */
    void
script_do_profile(scriptitem_T *si)
{
    si->sn_pr_count = 0;
    profile_zero(&si->sn_pr_total);
    profile_zero(&si->sn_pr_self);

    ga_init2(&si->sn_prl_ga, sizeof(sn_prl_T), 100);
    si->sn_prl_idx = -1;
    si->sn_prof_on = TRUE;
    si->sn_pr_nest = 0;
}

/*
 * Save time when starting to invoke another script or function.
 */
    void
script_prof_save(
    proftime_T	*tm)	    // place to store wait time
{
    scriptitem_T    *si;

    if (SCRIPT_ID_VALID(current_sctx.sc_sid))
    {
	si = SCRIPT_ITEM(current_sctx.sc_sid);
	if (si->sn_prof_on && si->sn_pr_nest++ == 0)
	    profile_start(&si->sn_pr_child);
    }
    profile_get_wait(tm);
}

/*
 * Count time spent in children after invoking another script or function.
 */
    void
script_prof_restore(proftime_T *tm)
{
    scriptitem_T    *si;

    if (SCRIPT_ID_VALID(current_sctx.sc_sid))
    {
	si = SCRIPT_ITEM(current_sctx.sc_sid);
	if (si->sn_prof_on && --si->sn_pr_nest == 0)
	{
	    profile_end(&si->sn_pr_child);
	    profile_sub_wait(tm, &si->sn_pr_child); // don't count wait time
	    profile_add(&si->sn_pr_children, &si->sn_pr_child);
	    profile_add(&si->sn_prl_children, &si->sn_pr_child);
	}
    }
}

/*
 * Dump the profiling results for all scripts in file "fd".
 */
    static void
script_dump_profile(FILE *fd)
{
    int		    id;
    scriptitem_T    *si;
    int		    i;
    FILE	    *sfd;
    sn_prl_T	    *pp;

    for (id = 1; id <= script_items.ga_len; ++id)
    {
	si = SCRIPT_ITEM(id);
	if (si->sn_prof_on)
	{
	    fprintf(fd, "SCRIPT  %s\n", si->sn_name);
	    if (si->sn_pr_count == 1)
		fprintf(fd, "Sourced 1 time\n");
	    else
		fprintf(fd, "Sourced %d times\n", si->sn_pr_count);
	    fprintf(fd, "Total time: %s\n", profile_msg(&si->sn_pr_total));
	    fprintf(fd, " Self time: %s\n", profile_msg(&si->sn_pr_self));
	    fprintf(fd, "\n");
	    fprintf(fd, "count  total (s)   self (s)\n");

	    sfd = mch_fopen((char *)si->sn_name, "r");
	    if (sfd == NULL)
		fprintf(fd, "Cannot open file!\n");
	    else
	    {
		// Keep going till the end of file, so that trailing
		// continuation lines are listed.
		for (i = 0; ; ++i)
		{
		    if (vim_fgets(IObuff, IOSIZE, sfd))
			break;
		    // When a line has been truncated, append NL, taking care
		    // of multi-byte characters .
		    if (IObuff[IOSIZE - 2] != NUL && IObuff[IOSIZE - 2] != NL)
		    {
			int n = IOSIZE - 2;

			if (enc_utf8)
			{
			    // Move to the first byte of this char.
			    // utf_head_off() doesn't work, because it checks
			    // for a truncated character.
			    while (n > 0 && (IObuff[n] & 0xc0) == 0x80)
				--n;
			}
			else if (has_mbyte)
			    n -= mb_head_off(IObuff, IObuff + n);
			IObuff[n] = NL;
			IObuff[n + 1] = NUL;
		    }
		    if (i < si->sn_prl_ga.ga_len
				     && (pp = &PRL_ITEM(si, i))->snp_count > 0)
		    {
			fprintf(fd, "%5d ", pp->snp_count);
			if (profile_equal(&pp->sn_prl_total, &pp->sn_prl_self))
			    fprintf(fd, "           ");
			else
			    fprintf(fd, "%s ", profile_msg(&pp->sn_prl_total));
			fprintf(fd, "%s ", profile_msg(&pp->sn_prl_self));
		    }
		    else
			fprintf(fd, "                            ");
		    fprintf(fd, "%s", IObuff);
		}
		fclose(sfd);
	    }
	    fprintf(fd, "\n");
	}
    }
}

/*
 * Dump the profiling info.
 */
    void
profile_dump(void)
{
    FILE	*fd;

    if (profile_fname != NULL)
    {
	fd = mch_fopen((char *)profile_fname, "w");
	if (fd == NULL)
	    semsg(_(e_notopen), profile_fname);
	else
	{
	    script_dump_profile(fd);
	    func_dump_profile(fd);
	    fclose(fd);
	}
    }
}

/*
 * Called when starting to read a script line.
 * "sourcing_lnum" must be correct!
 * When skipping lines it may not actually be executed, but we won't find out
 * until later and we need to store the time now.
 */
    void
script_line_start(void)
{
    scriptitem_T    *si;
    sn_prl_T	    *pp;

    if (!SCRIPT_ID_VALID(current_sctx.sc_sid))
	return;
    si = SCRIPT_ITEM(current_sctx.sc_sid);
    if (si->sn_prof_on && SOURCING_LNUM >= 1)
    {
	// Grow the array before starting the timer, so that the time spent
	// here isn't counted.
	(void)ga_grow(&si->sn_prl_ga,
				  (int)(SOURCING_LNUM - si->sn_prl_ga.ga_len));
	si->sn_prl_idx = SOURCING_LNUM - 1;
	while (si->sn_prl_ga.ga_len <= si->sn_prl_idx
		&& si->sn_prl_ga.ga_len < si->sn_prl_ga.ga_maxlen)
	{
	    // Zero counters for a line that was not used before.
	    pp = &PRL_ITEM(si, si->sn_prl_ga.ga_len);
	    pp->snp_count = 0;
	    profile_zero(&pp->sn_prl_total);
	    profile_zero(&pp->sn_prl_self);
	    ++si->sn_prl_ga.ga_len;
	}
	si->sn_prl_execed = FALSE;
	profile_start(&si->sn_prl_start);
	profile_zero(&si->sn_prl_children);
	profile_get_wait(&si->sn_prl_wait);
    }
}

/*
 * Called when actually executing a function line.
 */
    void
script_line_exec(void)
{
    scriptitem_T    *si;

    if (!SCRIPT_ID_VALID(current_sctx.sc_sid))
	return;
    si = SCRIPT_ITEM(current_sctx.sc_sid);
    if (si->sn_prof_on && si->sn_prl_idx >= 0)
	si->sn_prl_execed = TRUE;
}

/*
 * Called when done with a script line.
 */
    void
script_line_end(void)
{
    scriptitem_T    *si;
    sn_prl_T	    *pp;

    if (!SCRIPT_ID_VALID(current_sctx.sc_sid))
	return;
    si = SCRIPT_ITEM(current_sctx.sc_sid);
    if (si->sn_prof_on && si->sn_prl_idx >= 0
				     && si->sn_prl_idx < si->sn_prl_ga.ga_len)
    {
	if (si->sn_prl_execed)
	{
	    pp = &PRL_ITEM(si, si->sn_prl_idx);
	    ++pp->snp_count;
	    profile_end(&si->sn_prl_start);
	    profile_sub_wait(&si->sn_prl_wait, &si->sn_prl_start);
	    profile_add(&pp->sn_prl_total, &si->sn_prl_start);
	    profile_self(&pp->sn_prl_self, &si->sn_prl_start,
							&si->sn_prl_children);
	}
	si->sn_prl_idx = -1;
    }
}
# endif // FEAT_PROFILE

#endif
