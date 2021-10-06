/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * Text properties implementation.  See ":help text-properties".
 *
 * TODO:
 * - Adjust text property column and length when text is inserted/deleted.
 *   -> a :substitute with a multi-line match
 *   -> search for changed_bytes() from misc1.c
 *   -> search for mark_col_adjust()
 * - Perhaps we only need TP_FLAG_CONT_NEXT and can drop TP_FLAG_CONT_PREV?
 * - Add an array for global_proptypes, to quickly lookup a prop type by ID
 * - Add an array for b_proptypes, to quickly lookup a prop type by ID
 * - Checking the text length to detect text properties is slow.  Use a flag in
 *   the index, like DB_MARKED?
 * - Also test line2byte() with many lines, so that ml_updatechunk() is taken
 *   into account.
 * - Perhaps have a window-local option to disable highlighting from text
 *   properties?
 */

#include "vim.h"

#if defined(FEAT_PROP_POPUP) || defined(PROTO)

/*
 * In a hashtable item "hi_key" points to "pt_name" in a proptype_T.
 * This avoids adding a pointer to the hashtable item.
 * PT2HIKEY() converts a proptype pointer to a hashitem key pointer.
 * HIKEY2PT() converts a hashitem key pointer to a proptype pointer.
 * HI2PT() converts a hashitem pointer to a proptype pointer.
 */
#define PT2HIKEY(p)  ((p)->pt_name)
#define HIKEY2PT(p)   ((proptype_T *)((p) - offsetof(proptype_T, pt_name)))
#define HI2PT(hi)      HIKEY2PT((hi)->hi_key)

// The global text property types.
static hashtab_T *global_proptypes = NULL;

// The last used text property type ID.
static int proptype_id = 0;

static char_u e_type_not_exist[] = N_("E971: Property type %s does not exist");
static char_u e_invalid_col[] = N_("E964: Invalid column number: %ld");
static char_u e_invalid_lnum[] = N_("E966: Invalid line number: %ld");

/*
 * Find a property type by name, return the hashitem.
 * Returns NULL if the item can't be found.
 */
    static hashitem_T *
find_prop_hi(char_u *name, buf_T *buf)
{
    hashtab_T	*ht;
    hashitem_T	*hi;

    if (*name == NUL)
	return NULL;
    if (buf == NULL)
	ht = global_proptypes;
    else
	ht = buf->b_proptypes;

    if (ht == NULL)
	return NULL;
    hi = hash_find(ht, name);
    if (HASHITEM_EMPTY(hi))
	return NULL;
    return hi;
}

/*
 * Like find_prop_hi() but return the property type.
 */
    static proptype_T *
find_prop(char_u *name, buf_T *buf)
{
    hashitem_T	*hi = find_prop_hi(name, buf);

    if (hi == NULL)
	return NULL;
    return HI2PT(hi);
}

/*
 * Get the prop type ID of "name".
 * When not found return zero.
 */
    int
find_prop_type_id(char_u *name, buf_T *buf)
{
    proptype_T *pt = find_prop(name, buf);

    if (pt == NULL)
	return 0;
    return pt->pt_id;
}

/*
 * Lookup a property type by name.  First in "buf" and when not found in the
 * global types.
 * When not found gives an error message and returns NULL.
 */
    static proptype_T *
lookup_prop_type(char_u *name, buf_T *buf)
{
    proptype_T *type = find_prop(name, buf);

    if (type == NULL)
	type = find_prop(name, NULL);
    if (type == NULL)
	semsg(_(e_type_not_exist), name);
    return type;
}

/*
 * Get an optional "bufnr" item from the dict in "arg".
 * When the argument is not used or "bufnr" is not present then "buf" is
 * unchanged.
 * If "bufnr" is valid or not present return OK.
 * When "arg" is not a dict or "bufnr" is invalid return FAIL.
 */
    static int
get_bufnr_from_arg(typval_T *arg, buf_T **buf)
{
    dictitem_T	*di;

    if (arg->v_type != VAR_DICT)
    {
	emsg(_(e_dictreq));
	return FAIL;
    }
    if (arg->vval.v_dict == NULL)
	return OK;  // NULL dict is like an empty dict
    di = dict_find(arg->vval.v_dict, (char_u *)"bufnr", -1);
    if (di != NULL)
    {
	*buf = get_buf_arg(&di->di_tv);
	if (*buf == NULL)
	    return FAIL;
    }
    return OK;
}

/*
 * prop_add({lnum}, {col}, {props})
 */
    void
f_prop_add(typval_T *argvars, typval_T *rettv UNUSED)
{
    linenr_T	start_lnum;
    colnr_T	start_col;

    start_lnum = tv_get_number(&argvars[0]);
    start_col = tv_get_number(&argvars[1]);
    if (start_col < 1)
    {
	semsg(_(e_invalid_col), (long)start_col);
	return;
    }
    if (argvars[2].v_type != VAR_DICT)
    {
	emsg(_(e_dictreq));
	return;
    }

    prop_add_common(start_lnum, start_col, argvars[2].vval.v_dict,
							  curbuf, &argvars[2]);
}

/*
 * Shared between prop_add() and popup_create().
 * "dict_arg" is the function argument of a dict containing "bufnr".
 * it is NULL for popup_create().
 */
    void
prop_add_common(
	linenr_T    start_lnum,
	colnr_T	    start_col,
	dict_T	    *dict,
	buf_T	    *default_buf,
	typval_T    *dict_arg)
{
    linenr_T	lnum;
    linenr_T	end_lnum;
    colnr_T	end_col;
    char_u	*type_name;
    proptype_T	*type;
    buf_T	*buf = default_buf;
    int		id = 0;
    char_u	*newtext;
    int		proplen;
    size_t	textlen;
    char_u	*props = NULL;
    char_u	*newprops;
    textprop_T	tmp_prop;
    int		i;

    if (dict == NULL || dict_find(dict, (char_u *)"type", -1) == NULL)
    {
	emsg(_("E965: missing property type name"));
	return;
    }
    type_name = dict_get_string(dict, (char_u *)"type", FALSE);

    if (dict_find(dict, (char_u *)"end_lnum", -1) != NULL)
    {
	end_lnum = dict_get_number(dict, (char_u *)"end_lnum");
	if (end_lnum < start_lnum)
	{
	    semsg(_(e_invargval), "end_lnum");
	    return;
	}
    }
    else
	end_lnum = start_lnum;

    if (dict_find(dict, (char_u *)"length", -1) != NULL)
    {
	long length = dict_get_number(dict, (char_u *)"length");

	if (length < 0 || end_lnum > start_lnum)
	{
	    semsg(_(e_invargval), "length");
	    return;
	}
	end_col = start_col + length;
    }
    else if (dict_find(dict, (char_u *)"end_col", -1) != NULL)
    {
	end_col = dict_get_number(dict, (char_u *)"end_col");
	if (end_col <= 0)
	{
	    semsg(_(e_invargval), "end_col");
	    return;
	}
    }
    else if (start_lnum == end_lnum)
	end_col = start_col;
    else
	end_col = 1;

    if (dict_find(dict, (char_u *)"id", -1) != NULL)
	id = dict_get_number(dict, (char_u *)"id");

    if (dict_arg != NULL && get_bufnr_from_arg(dict_arg, &buf) == FAIL)
	return;

    type = lookup_prop_type(type_name, buf);
    if (type == NULL)
	return;

    if (start_lnum < 1 || start_lnum > buf->b_ml.ml_line_count)
    {
	semsg(_(e_invalid_lnum), (long)start_lnum);
	return;
    }
    if (end_lnum < start_lnum || end_lnum > buf->b_ml.ml_line_count)
    {
	semsg(_(e_invalid_lnum), (long)end_lnum);
	return;
    }

    if (buf->b_ml.ml_mfp == NULL)
    {
	emsg(_("E275: Cannot add text property to unloaded buffer"));
	return;
    }

    for (lnum = start_lnum; lnum <= end_lnum; ++lnum)
    {
	colnr_T col;	// start column
	long	length;	// in bytes

	// Fetch the line to get the ml_line_len field updated.
	proplen = get_text_props(buf, lnum, &props, TRUE);
	textlen = buf->b_ml.ml_line_len - proplen * sizeof(textprop_T);

	if (lnum == start_lnum)
	    col = start_col;
	else
	    col = 1;
	if (col - 1 > (colnr_T)textlen)
	{
	    semsg(_(e_invalid_col), (long)start_col);
	    return;
	}

	if (lnum == end_lnum)
	    length = end_col - col;
	else
	    length = (int)textlen - col + 1;
	if (length > (long)textlen)
	    length = (int)textlen;	// can include the end-of-line
	if (length < 0)
	    length = 0;		// zero-width property

	// Allocate the new line with space for the new proprety.
	newtext = alloc(buf->b_ml.ml_line_len + sizeof(textprop_T));
	if (newtext == NULL)
	    return;
	// Copy the text, including terminating NUL.
	mch_memmove(newtext, buf->b_ml.ml_line_ptr, textlen);

	// Find the index where to insert the new property.
	// Since the text properties are not aligned properly when stored with
	// the text, we need to copy them as bytes before using it as a struct.
	for (i = 0; i < proplen; ++i)
	{
	    mch_memmove(&tmp_prop, props + i * sizeof(textprop_T),
							   sizeof(textprop_T));
	    if (tmp_prop.tp_col >= col)
		break;
	}
	newprops = newtext + textlen;
	if (i > 0)
	    mch_memmove(newprops, props, sizeof(textprop_T) * i);

	tmp_prop.tp_col = col;
	tmp_prop.tp_len = length;
	tmp_prop.tp_id = id;
	tmp_prop.tp_type = type->pt_id;
	tmp_prop.tp_flags = (lnum > start_lnum ? TP_FLAG_CONT_PREV : 0)
			  | (lnum < end_lnum ? TP_FLAG_CONT_NEXT : 0);
	mch_memmove(newprops + i * sizeof(textprop_T), &tmp_prop,
							   sizeof(textprop_T));

	if (i < proplen)
	    mch_memmove(newprops + (i + 1) * sizeof(textprop_T),
					    props + i * sizeof(textprop_T),
					    sizeof(textprop_T) * (proplen - i));

	if (buf->b_ml.ml_flags & ML_LINE_DIRTY)
	    vim_free(buf->b_ml.ml_line_ptr);
	buf->b_ml.ml_line_ptr = newtext;
	buf->b_ml.ml_line_len += sizeof(textprop_T);
	buf->b_ml.ml_flags |= ML_LINE_DIRTY;
    }

    buf->b_has_textprop = TRUE;  // this is never reset
    redraw_buf_later(buf, NOT_VALID);
}

/*
 * Fetch the text properties for line "lnum" in buffer "buf".
 * Returns the number of text properties and, when non-zero, a pointer to the
 * first one in "props" (note that it is not aligned, therefore the char_u
 * pointer).
 */
    int
get_text_props(buf_T *buf, linenr_T lnum, char_u **props, int will_change)
{
    char_u *text;
    size_t textlen;
    size_t proplen;

    // Be quick when no text property types have been defined or the buffer,
    // unless we are adding one.
    if ((!buf->b_has_textprop && !will_change) || buf->b_ml.ml_mfp == NULL)
	return 0;

    // Fetch the line to get the ml_line_len field updated.
    text = ml_get_buf(buf, lnum, will_change);
    textlen = STRLEN(text) + 1;
    proplen = buf->b_ml.ml_line_len - textlen;
    if (proplen % sizeof(textprop_T) != 0)
    {
	iemsg(_("E967: text property info corrupted"));
	return 0;
    }
    if (proplen > 0)
	*props = text + textlen;
    return (int)(proplen / sizeof(textprop_T));
}

/**
 * Return the number of text properties on line "lnum" in the current buffer.
 * When "only_starting" is true only text properties starting in this line will
 * be considered.
 */
    int
count_props(linenr_T lnum, int only_starting)
{
    char_u	*props;
    int		proplen = get_text_props(curbuf, lnum, &props, 0);
    int		result = proplen;
    int		i;
    textprop_T	prop;

    if (only_starting)
	for (i = 0; i < proplen; ++i)
	{
	    mch_memmove(&prop, props + i * sizeof(prop), sizeof(prop));
	    if (prop.tp_flags & TP_FLAG_CONT_PREV)
		--result;
	}
    return result;
}

/*
 * Find text property "type_id" in the visible lines of window "wp".
 * Match "id" when it is > 0.
 * Returns FAIL when not found.
 */
    int
find_visible_prop(win_T *wp, int type_id, int id, textprop_T *prop,
							  linenr_T *found_lnum)
{
    linenr_T		lnum;
    char_u		*props;
    int			count;
    int			i;

    // w_botline may not have been updated yet.
    validate_botline();
    for (lnum = wp->w_topline; lnum < wp->w_botline; ++lnum)
    {
	count = get_text_props(wp->w_buffer, lnum, &props, FALSE);
	for (i = 0; i < count; ++i)
	{
	    mch_memmove(prop, props + i * sizeof(textprop_T),
							   sizeof(textprop_T));
	    if (prop->tp_type == type_id && (id <= 0 || prop->tp_id == id))
	    {
		*found_lnum = lnum;
		return OK;
	    }
	}
    }
    return FAIL;
}

/*
 * Set the text properties for line "lnum" to "props" with length "len".
 * If "len" is zero text properties are removed, "props" is not used.
 * Any existing text properties are dropped.
 * Only works for the current buffer.
 */
    static void
set_text_props(linenr_T lnum, char_u *props, int len)
{
    char_u  *text;
    char_u  *newtext;
    int	    textlen;

    text = ml_get(lnum);
    textlen = (int)STRLEN(text) + 1;
    newtext = alloc(textlen + len);
    if (newtext == NULL)
	return;
    mch_memmove(newtext, text, textlen);
    if (len > 0)
	mch_memmove(newtext + textlen, props, len);
    if (curbuf->b_ml.ml_flags & ML_LINE_DIRTY)
	vim_free(curbuf->b_ml.ml_line_ptr);
    curbuf->b_ml.ml_line_ptr = newtext;
    curbuf->b_ml.ml_line_len = textlen + len;
    curbuf->b_ml.ml_flags |= ML_LINE_DIRTY;
}

    static proptype_T *
find_type_by_id(hashtab_T *ht, int id)
{
    long	todo;
    hashitem_T	*hi;

    if (ht == NULL)
	return NULL;

    // TODO: Make this faster by keeping a list of types sorted on ID and use
    // a binary search.

    todo = (long)ht->ht_used;
    for (hi = ht->ht_array; todo > 0; ++hi)
    {
	if (!HASHITEM_EMPTY(hi))
	{
	    proptype_T *prop = HI2PT(hi);

	    if (prop->pt_id == id)
		return prop;
	    --todo;
	}
    }
    return NULL;
}

/*
 * Fill 'dict' with text properties in 'prop'.
 */
    static void
prop_fill_dict(dict_T *dict, textprop_T *prop, buf_T *buf)
{
    proptype_T *pt;

    dict_add_number(dict, "col", prop->tp_col);
    dict_add_number(dict, "length", prop->tp_len);
    dict_add_number(dict, "id", prop->tp_id);
    dict_add_number(dict, "start", !(prop->tp_flags & TP_FLAG_CONT_PREV));
    dict_add_number(dict, "end", !(prop->tp_flags & TP_FLAG_CONT_NEXT));
    pt = text_prop_type_by_id(buf, prop->tp_type);
    if (pt != NULL)
	dict_add_string(dict, "type", pt->pt_name);
}

/*
 * Find a property type by ID in "buf" or globally.
 * Returns NULL if not found.
 */
    proptype_T *
text_prop_type_by_id(buf_T *buf, int id)
{
    proptype_T *type;

    type = find_type_by_id(buf->b_proptypes, id);
    if (type == NULL)
	type = find_type_by_id(global_proptypes, id);
    return type;
}

/*
 * prop_clear({lnum} [, {lnum_end} [, {bufnr}]])
 */
    void
f_prop_clear(typval_T *argvars, typval_T *rettv UNUSED)
{
    linenr_T start = tv_get_number(&argvars[0]);
    linenr_T end = start;
    linenr_T lnum;
    buf_T    *buf = curbuf;

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	end = tv_get_number(&argvars[1]);
	if (argvars[2].v_type != VAR_UNKNOWN)
	{
	    if (get_bufnr_from_arg(&argvars[2], &buf) == FAIL)
		return;
	}
    }
    if (start < 1 || end < 1)
    {
	emsg(_(e_invrange));
	return;
    }

    for (lnum = start; lnum <= end; ++lnum)
    {
	char_u *text;
	size_t len;

	if (lnum > buf->b_ml.ml_line_count)
	    break;
	text = ml_get_buf(buf, lnum, FALSE);
	len = STRLEN(text) + 1;
	if ((size_t)buf->b_ml.ml_line_len > len)
	{
	    if (!(buf->b_ml.ml_flags & ML_LINE_DIRTY))
	    {
		char_u *newtext = vim_strsave(text);

		// need to allocate the line now
		if (newtext == NULL)
		    return;
		buf->b_ml.ml_line_ptr = newtext;
		buf->b_ml.ml_flags |= ML_LINE_DIRTY;
	    }
	    buf->b_ml.ml_line_len = (int)len;
	}
    }
    redraw_buf_later(buf, NOT_VALID);
}

/*
 * prop_find({props} [, {direction}])
 */
    void
f_prop_find(typval_T *argvars, typval_T *rettv)
{
    pos_T       *cursor = &curwin->w_cursor;
    dict_T      *dict;
    buf_T       *buf = curbuf;
    dictitem_T  *di;
    int		lnum_start;
    int		start_pos_has_prop = 0;
    int		seen_end = 0;
    int		id = -1;
    int		type_id = -1;
    int		skipstart = 0;
    int		lnum = -1;
    int		col = -1;
    int		dir = 1;    // 1 = forward, -1 = backward

    if (argvars[0].v_type != VAR_DICT || argvars[0].vval.v_dict == NULL)
    {
	emsg(_(e_invarg));
	return;
    }
    dict = argvars[0].vval.v_dict;

    if (get_bufnr_from_arg(&argvars[0], &buf) == FAIL)
	return;
    if (buf->b_ml.ml_mfp == NULL)
	return;

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	char_u      *dir_s = tv_get_string(&argvars[1]);

	if (*dir_s == 'b')
	    dir = -1;
	else if (*dir_s != 'f')
	{
	    emsg(_(e_invarg));
	    return;
	}
    }

    di = dict_find(dict, (char_u *)"lnum", -1);
    if (di != NULL)
	lnum = tv_get_number(&di->di_tv);

    di = dict_find(dict, (char_u *)"col", -1);
    if (di != NULL)
	col = tv_get_number(&di->di_tv);

    if (lnum == -1)
    {
	lnum = cursor->lnum;
	col = cursor->col + 1;
    }
    else if (col == -1)
	col = 1;

    if (lnum < 1 || lnum > buf->b_ml.ml_line_count)
    {
	emsg(_(e_invrange));
	return;
    }

    skipstart = dict_get_bool(dict, (char_u *)"skipstart", 0);

    if (dict_find(dict, (char_u *)"id", -1) != NULL)
	id = dict_get_number(dict, (char_u *)"id");
    if (dict_find(dict, (char_u *)"type", -1))
    {
	char_u	    *name = dict_get_string(dict, (char_u *)"type", FALSE);
	proptype_T  *type = lookup_prop_type(name, buf);

	if (type == NULL)
	    return;
	type_id = type->pt_id;
    }
    if (id == -1 && type_id == -1)
    {
	emsg(_("E968: Need at least one of 'id' or 'type'"));
	return;
    }

    lnum_start = lnum;

    if (rettv_dict_alloc(rettv) == FAIL)
	return;

    while (1)
    {
	char_u	*text = ml_get_buf(buf, lnum, FALSE);
	size_t	textlen = STRLEN(text) + 1;
	int	count = (int)((buf->b_ml.ml_line_len - textlen)
							 / sizeof(textprop_T));
	int	    i;
	textprop_T  prop;
	int	    prop_start;
	int	    prop_end;

	for (i = 0; i < count; ++i)
	{
	    mch_memmove(&prop, text + textlen + i * sizeof(textprop_T),
			    sizeof(textprop_T));

	    if (lnum == lnum_start)
	    {
		if (dir < 0)
		{
		    if (col < prop.tp_col)
			break;
		}
		else if (prop.tp_col + prop.tp_len - (prop.tp_len != 0) < col)
		    continue;
	    }
	    if (prop.tp_id == id || prop.tp_type == type_id)
	    {
		// Check if the starting position has text props.
		if (lnum_start == lnum
			&& col >= prop.tp_col
			&& (col <= prop.tp_col + prop.tp_len
							 - (prop.tp_len != 0)))
		    start_pos_has_prop = 1;

		prop_start = !(prop.tp_flags & TP_FLAG_CONT_PREV);
		prop_end = !(prop.tp_flags & TP_FLAG_CONT_NEXT);
		if (!prop_start && prop_end && dir > 0)
		    seen_end = 1;

		// Skip lines without the start flag.
		if (!prop_start)
		{
		    // Always search backwards for start when search started
		    // on a prop and we're not skipping.
		    if (start_pos_has_prop && !skipstart)
			dir = -1;
		    continue;
		}

		// If skipstart is true, skip the prop at start pos (even if
		// continued from another line).
		if (start_pos_has_prop && skipstart && !seen_end)
		{
		    start_pos_has_prop = 0;
		    continue;
		}

		prop_fill_dict(rettv->vval.v_dict, &prop, buf);
		dict_add_number(rettv->vval.v_dict, "lnum", lnum);

		return;
	    }
	}

	if (dir > 0)
	{
	    if (lnum >= buf->b_ml.ml_line_count)
		break;
	    lnum++;
	}
	else
	{
	    if (lnum <= 1)
		break;
	    lnum--;
	}
	// Adjust col to indicate that we're continuing from prev/next line.
	col = dir < 0 ? buf->b_ml.ml_line_len : 1;
    }
}

/*
 * prop_list({lnum} [, {bufnr}])
 */
    void
f_prop_list(typval_T *argvars, typval_T *rettv)
{
    linenr_T lnum = tv_get_number(&argvars[0]);
    buf_T    *buf = curbuf;

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	if (get_bufnr_from_arg(&argvars[1], &buf) == FAIL)
	    return;
    }
    if (lnum < 1 || lnum > buf->b_ml.ml_line_count)
    {
	emsg(_(e_invrange));
	return;
    }

    if (rettv_list_alloc(rettv) == OK)
    {
	char_u	    *text = ml_get_buf(buf, lnum, FALSE);
	size_t	    textlen = STRLEN(text) + 1;
	int	    count = (int)((buf->b_ml.ml_line_len - textlen)
							 / sizeof(textprop_T));
	int	    i;
	textprop_T  prop;

	for (i = 0; i < count; ++i)
	{
	    dict_T *d = dict_alloc();

	    if (d == NULL)
		break;
	    mch_memmove(&prop, text + textlen + i * sizeof(textprop_T),
							   sizeof(textprop_T));
	    prop_fill_dict(d, &prop, buf);
	    list_append_dict(rettv->vval.v_list, d);
	}
    }
}

/*
 * prop_remove({props} [, {lnum} [, {lnum_end}]])
 */
    void
f_prop_remove(typval_T *argvars, typval_T *rettv)
{
    linenr_T	start = 1;
    linenr_T	end = 0;
    linenr_T	lnum;
    dict_T	*dict;
    buf_T	*buf = curbuf;
    int		do_all;
    int		id = -1;
    int		type_id = -1;
    int		both;

    rettv->vval.v_number = 0;
    if (argvars[0].v_type != VAR_DICT || argvars[0].vval.v_dict == NULL)
    {
	emsg(_(e_invarg));
	return;
    }

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	start = tv_get_number(&argvars[1]);
	end = start;
	if (argvars[2].v_type != VAR_UNKNOWN)
	    end = tv_get_number(&argvars[2]);
	if (start < 1 || end < 1)
	{
	    emsg(_(e_invrange));
	    return;
	}
    }

    dict = argvars[0].vval.v_dict;
    if (get_bufnr_from_arg(&argvars[0], &buf) == FAIL)
	return;
    if (buf->b_ml.ml_mfp == NULL)
	return;

    do_all = dict_get_bool(dict, (char_u *)"all", FALSE);

    if (dict_find(dict, (char_u *)"id", -1) != NULL)
	id = dict_get_number(dict, (char_u *)"id");
    if (dict_find(dict, (char_u *)"type", -1))
    {
	char_u	    *name = dict_get_string(dict, (char_u *)"type", FALSE);
	proptype_T  *type = lookup_prop_type(name, buf);

	if (type == NULL)
	    return;
	type_id = type->pt_id;
    }
    both = dict_get_bool(dict, (char_u *)"both", FALSE);

    if (id == -1 && type_id == -1)
    {
	emsg(_("E968: Need at least one of 'id' or 'type'"));
	return;
    }
    if (both && (id == -1 || type_id == -1))
    {
	emsg(_("E860: Need 'id' and 'type' with 'both'"));
	return;
    }

    if (end == 0)
	end = buf->b_ml.ml_line_count;
    for (lnum = start; lnum <= end; ++lnum)
    {
	char_u *text;
	size_t len;

	if (lnum > buf->b_ml.ml_line_count)
	    break;
	text = ml_get_buf(buf, lnum, FALSE);
	len = STRLEN(text) + 1;
	if ((size_t)buf->b_ml.ml_line_len > len)
	{
	    static textprop_T	textprop;  // static because of alignment
	    unsigned		idx;

	    for (idx = 0; idx < (buf->b_ml.ml_line_len - len)
						   / sizeof(textprop_T); ++idx)
	    {
		char_u *cur_prop = buf->b_ml.ml_line_ptr + len
						    + idx * sizeof(textprop_T);
		size_t	taillen;

		mch_memmove(&textprop, cur_prop, sizeof(textprop_T));
		if (both ? textprop.tp_id == id && textprop.tp_type == type_id
			 : textprop.tp_id == id || textprop.tp_type == type_id)
		{
		    if (!(buf->b_ml.ml_flags & ML_LINE_DIRTY))
		    {
			char_u *newptr = alloc(buf->b_ml.ml_line_len);

			// need to allocate the line to be able to change it
			if (newptr == NULL)
			    return;
			mch_memmove(newptr, buf->b_ml.ml_line_ptr,
							buf->b_ml.ml_line_len);
			buf->b_ml.ml_line_ptr = newptr;
			buf->b_ml.ml_flags |= ML_LINE_DIRTY;

			cur_prop = buf->b_ml.ml_line_ptr + len
						    + idx * sizeof(textprop_T);
		    }

		    taillen = buf->b_ml.ml_line_len - len
					      - (idx + 1) * sizeof(textprop_T);
		    if (taillen > 0)
			mch_memmove(cur_prop, cur_prop + sizeof(textprop_T),
								      taillen);
		    buf->b_ml.ml_line_len -= sizeof(textprop_T);
		    --idx;

		    ++rettv->vval.v_number;
		    if (!do_all)
			break;
		}
	    }
	}
    }
    redraw_buf_later(buf, NOT_VALID);
}

/*
 * Common for f_prop_type_add() and f_prop_type_change().
 */
    static void
prop_type_set(typval_T *argvars, int add)
{
    char_u	*name;
    buf_T	*buf = NULL;
    dict_T	*dict;
    dictitem_T  *di;
    proptype_T	*prop;

    name = tv_get_string(&argvars[0]);
    if (*name == NUL)
    {
	emsg(_(e_invarg));
	return;
    }

    if (get_bufnr_from_arg(&argvars[1], &buf) == FAIL)
	return;
    dict = argvars[1].vval.v_dict;

    prop = find_prop(name, buf);
    if (add)
    {
	hashtab_T **htp;

	if (prop != NULL)
	{
	    semsg(_("E969: Property type %s already defined"), name);
	    return;
	}
	prop = alloc_clear(offsetof(proptype_T, pt_name) + STRLEN(name) + 1);
	if (prop == NULL)
	    return;
	STRCPY(prop->pt_name, name);
	prop->pt_id = ++proptype_id;
	prop->pt_flags = PT_FLAG_COMBINE;
	htp = buf == NULL ? &global_proptypes : &buf->b_proptypes;
	if (*htp == NULL)
	{
	    *htp = ALLOC_ONE(hashtab_T);
	    if (*htp == NULL)
	    {
		vim_free(prop);
		return;
	    }
	    hash_init(*htp);
	}
	hash_add(*htp, PT2HIKEY(prop));
    }
    else
    {
	if (prop == NULL)
	{
	    semsg(_(e_type_not_exist), name);
	    return;
	}
    }

    if (dict != NULL)
    {
	di = dict_find(dict, (char_u *)"highlight", -1);
	if (di != NULL)
	{
	    char_u	*highlight;
	    int		hl_id = 0;

	    highlight = dict_get_string(dict, (char_u *)"highlight", FALSE);
	    if (highlight != NULL && *highlight != NUL)
		hl_id = syn_name2id(highlight);
	    if (hl_id <= 0)
	    {
		semsg(_("E970: Unknown highlight group name: '%s'"),
			highlight == NULL ? (char_u *)"" : highlight);
		return;
	    }
	    prop->pt_hl_id = hl_id;
	}

	di = dict_find(dict, (char_u *)"combine", -1);
	if (di != NULL)
	{
	    if (tv_get_bool(&di->di_tv))
		prop->pt_flags |= PT_FLAG_COMBINE;
	    else
		prop->pt_flags &= ~PT_FLAG_COMBINE;
	}

	di = dict_find(dict, (char_u *)"priority", -1);
	if (di != NULL)
	    prop->pt_priority = tv_get_number(&di->di_tv);

	di = dict_find(dict, (char_u *)"start_incl", -1);
	if (di != NULL)
	{
	    if (tv_get_bool(&di->di_tv))
		prop->pt_flags |= PT_FLAG_INS_START_INCL;
	    else
		prop->pt_flags &= ~PT_FLAG_INS_START_INCL;
	}

	di = dict_find(dict, (char_u *)"end_incl", -1);
	if (di != NULL)
	{
	    if (tv_get_bool(&di->di_tv))
		prop->pt_flags |= PT_FLAG_INS_END_INCL;
	    else
		prop->pt_flags &= ~PT_FLAG_INS_END_INCL;
	}
    }
}

/*
 * prop_type_add({name}, {props})
 */
    void
f_prop_type_add(typval_T *argvars, typval_T *rettv UNUSED)
{
    prop_type_set(argvars, TRUE);
}

/*
 * prop_type_change({name}, {props})
 */
    void
f_prop_type_change(typval_T *argvars, typval_T *rettv UNUSED)
{
    prop_type_set(argvars, FALSE);
}

/*
 * prop_type_delete({name} [, {bufnr}])
 */
    void
f_prop_type_delete(typval_T *argvars, typval_T *rettv UNUSED)
{
    char_u	*name;
    buf_T	*buf = NULL;
    hashitem_T	*hi;

    name = tv_get_string(&argvars[0]);
    if (*name == NUL)
    {
	emsg(_(e_invarg));
	return;
    }

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	if (get_bufnr_from_arg(&argvars[1], &buf) == FAIL)
	    return;
    }

    hi = find_prop_hi(name, buf);
    if (hi != NULL)
    {
	hashtab_T	*ht;
	proptype_T	*prop = HI2PT(hi);

	if (buf == NULL)
	    ht = global_proptypes;
	else
	    ht = buf->b_proptypes;
	hash_remove(ht, hi);
	vim_free(prop);
    }
}

/*
 * prop_type_get({name} [, {bufnr}])
 */
    void
f_prop_type_get(typval_T *argvars, typval_T *rettv)
{
    char_u *name = tv_get_string(&argvars[0]);

    if (*name == NUL)
    {
	emsg(_(e_invarg));
	return;
    }
    if (rettv_dict_alloc(rettv) == OK)
    {
	proptype_T  *prop = NULL;
	buf_T	    *buf = NULL;

	if (argvars[1].v_type != VAR_UNKNOWN)
	{
	    if (get_bufnr_from_arg(&argvars[1], &buf) == FAIL)
		return;
	}

	prop = find_prop(name, buf);
	if (prop != NULL)
	{
	    dict_T *d = rettv->vval.v_dict;

	    if (prop->pt_hl_id > 0)
		dict_add_string(d, "highlight", syn_id2name(prop->pt_hl_id));
	    dict_add_number(d, "priority", prop->pt_priority);
	    dict_add_number(d, "combine",
				   (prop->pt_flags & PT_FLAG_COMBINE) ? 1 : 0);
	    dict_add_number(d, "start_incl",
			    (prop->pt_flags & PT_FLAG_INS_START_INCL) ? 1 : 0);
	    dict_add_number(d, "end_incl",
			      (prop->pt_flags & PT_FLAG_INS_END_INCL) ? 1 : 0);
	    if (buf != NULL)
		dict_add_number(d, "bufnr", buf->b_fnum);
	}
    }
}

    static void
list_types(hashtab_T *ht, list_T *l)
{
    long	todo;
    hashitem_T	*hi;

    todo = (long)ht->ht_used;
    for (hi = ht->ht_array; todo > 0; ++hi)
    {
	if (!HASHITEM_EMPTY(hi))
	{
	    proptype_T *prop = HI2PT(hi);

	    list_append_string(l, prop->pt_name, -1);
	    --todo;
	}
    }
}

/*
 * prop_type_list([{bufnr}])
 */
    void
f_prop_type_list(typval_T *argvars, typval_T *rettv UNUSED)
{
    buf_T *buf = NULL;

    if (rettv_list_alloc(rettv) == OK)
    {
	if (argvars[0].v_type != VAR_UNKNOWN)
	{
	    if (get_bufnr_from_arg(&argvars[0], &buf) == FAIL)
		return;
	}
	if (buf == NULL)
	{
	    if (global_proptypes != NULL)
		list_types(global_proptypes, rettv->vval.v_list);
	}
	else if (buf->b_proptypes != NULL)
	    list_types(buf->b_proptypes, rettv->vval.v_list);
    }
}

/*
 * Free all property types in "ht".
 */
    static void
clear_ht_prop_types(hashtab_T *ht)
{
    long	todo;
    hashitem_T	*hi;

    if (ht == NULL)
	return;

    todo = (long)ht->ht_used;
    for (hi = ht->ht_array; todo > 0; ++hi)
    {
	if (!HASHITEM_EMPTY(hi))
	{
	    proptype_T *prop = HI2PT(hi);

	    vim_free(prop);
	    --todo;
	}
    }

    hash_clear(ht);
    vim_free(ht);
}

#if defined(EXITFREE) || defined(PROTO)
/*
 * Free all global property types.
 */
    void
clear_global_prop_types(void)
{
    clear_ht_prop_types(global_proptypes);
    global_proptypes = NULL;
}
#endif

/*
 * Free all property types for "buf".
 */
    void
clear_buf_prop_types(buf_T *buf)
{
    clear_ht_prop_types(buf->b_proptypes);
    buf->b_proptypes = NULL;
}

// Struct used to return two values from adjust_prop().
typedef struct
{
    int dirty;	    // if the property was changed
    int can_drop;   // whether after this change, the prop may be removed
} adjustres_T;

/*
 * Adjust the property for "added" bytes (can be negative) inserted at "col".
 *
 * Note that "col" is zero-based, while tp_col is one-based.
 * Only for the current buffer.
 * "flags" can have:
 * APC_SUBSTITUTE:	Text is replaced, not inserted.
 */
    static adjustres_T
adjust_prop(
	textprop_T *prop,
	colnr_T col,
	int added,
	int flags)
{
    proptype_T	*pt = text_prop_type_by_id(curbuf, prop->tp_type);
    int		start_incl = (pt != NULL
				    && (pt->pt_flags & PT_FLAG_INS_START_INCL))
						   || (flags & APC_SUBSTITUTE);
    int		end_incl = (pt != NULL
				     && (pt->pt_flags & PT_FLAG_INS_END_INCL));
		// Do not drop zero-width props if they later can increase in
		// size.
    int		droppable = !(start_incl || end_incl);
    adjustres_T res = {TRUE, FALSE};

    if (added > 0)
    {
	if (col + 1 <= prop->tp_col
		- (start_incl || (prop->tp_len == 0 && end_incl)))
	    // Change is entirely before the text property: Only shift
	    prop->tp_col += added;
	else if (col + 1 < prop->tp_col + prop->tp_len + end_incl)
	    // Insertion was inside text property
	    prop->tp_len += added;
    }
    else if (prop->tp_col > col + 1)
    {
	if (prop->tp_col + added < col + 1)
	{
	    prop->tp_len += (prop->tp_col - 1 - col) + added;
	    prop->tp_col = col + 1;
	    if (prop->tp_len <= 0)
	    {
		prop->tp_len = 0;
		res.can_drop = droppable;
	    }
	}
	else
	    prop->tp_col += added;
    }
    else if (prop->tp_len > 0 && prop->tp_col + prop->tp_len > col)
    {
	int after = col - added - (prop->tp_col - 1 + prop->tp_len);

	prop->tp_len += after > 0 ? added + after : added;
	res.can_drop = prop->tp_len <= 0 && droppable;
    }
    else
	res.dirty = FALSE;

    return res;
}

/*
 * Adjust the columns of text properties in line "lnum" after position "col" to
 * shift by "bytes_added" (can be negative).
 * Note that "col" is zero-based, while tp_col is one-based.
 * Only for the current buffer.
 * "flags" can have:
 * APC_SAVE_FOR_UNDO:	Call u_savesub() before making changes to the line.
 * APC_SUBSTITUTE:	Text is replaced, not inserted.
 * Caller is expected to check b_has_textprop and "bytes_added" being non-zero.
 * Returns TRUE when props were changed.
 */
    int
adjust_prop_columns(
	linenr_T    lnum,
	colnr_T	    col,
	int	    bytes_added,
	int	    flags)
{
    int		proplen;
    char_u	*props;
    int		dirty = FALSE;
    int		ri, wi;
    size_t	textlen;

    if (text_prop_frozen > 0)
	return FALSE;

    proplen = get_text_props(curbuf, lnum, &props, TRUE);
    if (proplen == 0)
	return FALSE;
    textlen = curbuf->b_ml.ml_line_len - proplen * sizeof(textprop_T);

    wi = 0; // write index
    for (ri = 0; ri < proplen; ++ri)
    {
	textprop_T	prop;
	adjustres_T	res;

	mch_memmove(&prop, props + ri * sizeof(prop), sizeof(prop));
	res = adjust_prop(&prop, col, bytes_added, flags);
	if (res.dirty)
	{
	    // Save for undo if requested and not done yet.
	    if ((flags & APC_SAVE_FOR_UNDO) && !dirty
						    && u_savesub(lnum) == FAIL)
		return FALSE;
	    dirty = TRUE;

	    // u_savesub() may have updated curbuf->b_ml, fetch it again
	    if (curbuf->b_ml.ml_line_lnum != lnum)
		proplen = get_text_props(curbuf, lnum, &props, TRUE);
	}
	if (res.can_drop)
	    continue; // Drop this text property
	mch_memmove(props + wi * sizeof(textprop_T), &prop, sizeof(textprop_T));
	++wi;
    }
    if (dirty)
    {
	colnr_T newlen = (int)textlen + wi * (colnr_T)sizeof(textprop_T);

	if ((curbuf->b_ml.ml_flags & ML_LINE_DIRTY) == 0)
	    curbuf->b_ml.ml_line_ptr =
				 vim_memsave(curbuf->b_ml.ml_line_ptr, newlen);
	curbuf->b_ml.ml_flags |= ML_LINE_DIRTY;
	curbuf->b_ml.ml_line_len = newlen;
    }
    return dirty;
}

/*
 * Adjust text properties for a line that was split in two.
 * "lnum_props" is the line that has the properties from before the split.
 * "lnum_top" is the top line.
 * "kept" is the number of bytes kept in the first line, while
 * "deleted" is the number of bytes deleted.
 */
    void
adjust_props_for_split(
	linenr_T lnum_props,
	linenr_T lnum_top,
	int kept,
	int deleted)
{
    char_u	*props;
    int		count;
    garray_T    prevprop;
    garray_T    nextprop;
    int		i;
    int		skipped = kept + deleted;

    if (!curbuf->b_has_textprop)
	return;

    // Get the text properties from "lnum_props".
    count = get_text_props(curbuf, lnum_props, &props, FALSE);
    ga_init2(&prevprop, sizeof(textprop_T), 10);
    ga_init2(&nextprop, sizeof(textprop_T), 10);

    // Keep the relevant ones in the first line, reducing the length if needed.
    // Copy the ones that include the split to the second line.
    // Move the ones after the split to the second line.
    for (i = 0; i < count; ++i)
    {
	textprop_T  prop;
	proptype_T *pt;
	int	    start_incl, end_incl;
	int	    cont_prev, cont_next;

	// copy the prop to an aligned structure
	mch_memmove(&prop, props + i * sizeof(textprop_T), sizeof(textprop_T));

	pt = text_prop_type_by_id(curbuf, prop.tp_type);
	start_incl = (pt != NULL && (pt->pt_flags & PT_FLAG_INS_START_INCL));
	end_incl = (pt != NULL && (pt->pt_flags & PT_FLAG_INS_END_INCL));
	cont_prev = prop.tp_col + !start_incl <= kept;
	cont_next = skipped <= prop.tp_col + prop.tp_len - !end_incl;

	if (cont_prev && ga_grow(&prevprop, 1) == OK)
	{
	    textprop_T *p = ((textprop_T *)prevprop.ga_data) + prevprop.ga_len;

	    *p = prop;
	    ++prevprop.ga_len;
	    if (p->tp_col + p->tp_len >= kept)
		p->tp_len = kept - p->tp_col;
	    if (cont_next)
		p->tp_flags |= TP_FLAG_CONT_NEXT;
	}

	// Only add the property to the next line if the length is bigger than
	// zero.
	if (cont_next && ga_grow(&nextprop, 1) == OK)
	{
	    textprop_T *p = ((textprop_T *)nextprop.ga_data) + nextprop.ga_len;
	    *p = prop;
	    ++nextprop.ga_len;
	    if (p->tp_col > skipped)
		p->tp_col -= skipped - 1;
	    else
	    {
		p->tp_len -= skipped - p->tp_col;
		p->tp_col = 1;
	    }
	    if (cont_prev)
		p->tp_flags |= TP_FLAG_CONT_PREV;
	}
    }

    set_text_props(lnum_top, prevprop.ga_data,
					 prevprop.ga_len * sizeof(textprop_T));
    ga_clear(&prevprop);
    set_text_props(lnum_top + 1, nextprop.ga_data,
					 nextprop.ga_len * sizeof(textprop_T));
    ga_clear(&nextprop);
}

/*
 * Prepend properties of joined line "lnum" to "new_props".
 */
    void
prepend_joined_props(
	char_u	    *new_props,
	int	    propcount,
	int	    *props_remaining,
	linenr_T    lnum,
	int	    add_all,
	long	    col,
	int	    removed)
{
    char_u *props;
    int	    proplen = get_text_props(curbuf, lnum, &props, FALSE);
    int	    i;

    for (i = proplen; i-- > 0; )
    {
	textprop_T  prop;
	int	    end;

	mch_memmove(&prop, props + i * sizeof(prop), sizeof(prop));
	end = !(prop.tp_flags & TP_FLAG_CONT_NEXT);

	adjust_prop(&prop, 0, -removed, 0); // Remove leading spaces
	adjust_prop(&prop, -1, col, 0); // Make line start at its final colum

	if (add_all || end)
	    mch_memmove(new_props + --(*props_remaining) * sizeof(prop),
							  &prop, sizeof(prop));
	else
	{
	    int j;
	    int	found = FALSE;

	    // Search for continuing prop.
	    for (j = *props_remaining; j < propcount; ++j)
	    {
		textprop_T op;

		mch_memmove(&op, new_props + j * sizeof(op), sizeof(op));
		if ((op.tp_flags & TP_FLAG_CONT_PREV)
			&& op.tp_id == prop.tp_id && op.tp_type == prop.tp_type)
		{
		    found = TRUE;
		    op.tp_len += op.tp_col - prop.tp_col;
		    op.tp_col = prop.tp_col;
		    // Start/end is taken care of when deleting joined lines
		    op.tp_flags = prop.tp_flags;
		    mch_memmove(new_props + j * sizeof(op), &op, sizeof(op));
		    break;
		}
	    }
	    if (!found)
		internal_error("text property above joined line not found");
	}
    }
}

#endif // FEAT_PROP_POPUP
