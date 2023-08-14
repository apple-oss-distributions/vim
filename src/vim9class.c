/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * vim9class.c: Vim9 script class support
 */

#define USING_FLOAT_STUFF
#include "vim.h"

#if defined(FEAT_EVAL) || defined(PROTO)

// When not generating protos this is included in proto.h
#ifdef PROTO
# include "vim9.h"
#endif

/*
 * Parse a member declaration, both object and class member.
 * Returns OK or FAIL.  When OK then "varname_end" is set to just after the
 * variable name and "type_ret" is set to the decleared or detected type.
 * "init_expr" is set to the initialisation expression (allocated), if there is
 * one.  For an interface "init_expr" is NULL.
 */
    static int
parse_member(
	exarg_T	*eap,
	char_u	*line,
	char_u	*varname,
	int	has_public,	    // TRUE if "public" seen before "varname"
	char_u	**varname_end,
	garray_T *type_list,
	type_T	**type_ret,
	char_u	**init_expr)
{
    *varname_end = to_name_end(varname, FALSE);
    if (*varname == '_' && has_public)
    {
	semsg(_(e_public_member_name_cannot_start_with_underscore_str), line);
	return FAIL;
    }

    char_u *colon = skipwhite(*varname_end);
    char_u *type_arg = colon;
    type_T *type = NULL;
    if (*colon == ':')
    {
	if (VIM_ISWHITE(**varname_end))
	{
	    semsg(_(e_no_white_space_allowed_before_colon_str), varname);
	    return FAIL;
	}
	if (!VIM_ISWHITE(colon[1]))
	{
	    semsg(_(e_white_space_required_after_str_str), ":", varname);
	    return FAIL;
	}
	type_arg = skipwhite(colon + 1);
	type = parse_type(&type_arg, type_list, TRUE);
	if (type == NULL)
	    return FAIL;
    }

    char_u *expr_start = skipwhite(type_arg);
    char_u *expr_end = expr_start;
    if (type == NULL && *expr_start != '=')
    {
	emsg(_(e_type_or_initialization_required));
	return FAIL;
    }

    if (*expr_start == '=')
    {
	if (!VIM_ISWHITE(expr_start[-1]) || !VIM_ISWHITE(expr_start[1]))
	{
	    semsg(_(e_white_space_required_before_and_after_str_at_str),
							"=", type_arg);
	    return FAIL;
	}
	expr_start = skipwhite(expr_start + 1);

	expr_end = expr_start;
	evalarg_T evalarg;
	fill_evalarg_from_eap(&evalarg, eap, FALSE);
	skip_expr(&expr_end, NULL);

	if (type == NULL)
	{
	    // No type specified, use the type of the initializer.
	    typval_T tv;
	    tv.v_type = VAR_UNKNOWN;
	    char_u *expr = expr_start;
	    int res = eval0(expr, &tv, eap, &evalarg);

	    if (res == OK)
	    {
		type = typval2type(&tv, get_copyID(), type_list,
						       TVTT_DO_MEMBER);
		clear_tv(&tv);
	    }
	    if (type == NULL)
	    {
		semsg(_(e_cannot_get_object_member_type_from_initializer_str),
			expr_start);
		clear_evalarg(&evalarg, NULL);
		return FAIL;
	    }
	}
	clear_evalarg(&evalarg, NULL);
    }
    if (!valid_declaration_type(type))
	return FAIL;

    *type_ret = type;
    if (expr_end > expr_start)
    {
	if (init_expr == NULL)
	{
	    emsg(_(e_cannot_initialize_member_in_interface));
	    return FAIL;
	}
	*init_expr = vim_strnsave(expr_start, expr_end - expr_start);
    }
    return OK;
}

/*
 * Add a member to an object or a class.
 * Returns OK when successful, "init_expr" will be consumed then.
 * Returns FAIL otherwise, caller might need to free "init_expr".
 */
    static int
add_member(
	garray_T    *gap,
	char_u	    *varname,
	char_u	    *varname_end,
	int	    has_public,
	type_T	    *type,
	char_u	    *init_expr)
{
    if (ga_grow(gap, 1) == FAIL)
	return FAIL;
    ocmember_T *m = ((ocmember_T *)gap->ga_data) + gap->ga_len;
    m->ocm_name = vim_strnsave(varname, varname_end - varname);
    m->ocm_access = has_public ? VIM_ACCESS_ALL
		      : *varname == '_' ? VIM_ACCESS_PRIVATE : VIM_ACCESS_READ;
    m->ocm_type = type;
    if (init_expr != NULL)
	m->ocm_init = init_expr;
    ++gap->ga_len;
    return OK;
}

/*
 * Move the class or object members found while parsing a class into the class.
 * "gap" contains the found members.
 * "parent_members" points to the members in the parent class (if any)
 * "parent_count" is the number of members in the parent class
 * "members" will be set to the newly allocated array of members and
 * "member_count" set to the number of members.
 * Returns OK or FAIL.
 */
    static int
add_members_to_class(
    garray_T	*gap,
    ocmember_T	*parent_members,
    int		parent_count,
    ocmember_T	**members,
    int		*member_count)
{
    *member_count = parent_count + gap->ga_len;
    *members = *member_count == 0 ? NULL
				       : ALLOC_MULT(ocmember_T, *member_count);
    if (*member_count > 0 && *members == NULL)
	return FAIL;
    for (int i = 0; i < parent_count; ++i)
    {
	// parent members need to be copied
	ocmember_T	*m = *members + i;
	*m = parent_members[i];
	m->ocm_name = vim_strsave(m->ocm_name);
	if (m->ocm_init != NULL)
	    m->ocm_init = vim_strsave(m->ocm_init);
    }
    if (gap->ga_len > 0)
	// new members are moved
	mch_memmove(*members + parent_count,
			       gap->ga_data, sizeof(ocmember_T) * gap->ga_len);
    VIM_CLEAR(gap->ga_data);
    return OK;
}

/*
 * Convert a member index "idx" of interface "itf" to the member index of class
 * "cl" implementing that interface.
 */
    int
object_index_from_itf_index(class_T *itf, int is_method, int idx, class_T *cl)
{
    if (idx > (is_method ? itf->class_obj_method_count
						: itf->class_obj_member_count))
    {
	siemsg("index %d out of range for interface %s", idx, itf->class_name);
	return 0;
    }
    itf2class_T *i2c;
    for (i2c = itf->class_itf2class; i2c != NULL; i2c = i2c->i2c_next)
	if (i2c->i2c_class == cl && i2c->i2c_is_method == is_method)
	    break;
    if (i2c == NULL)
    {
	siemsg("class %s not found on interface %s",
					      cl->class_name, itf->class_name);
	return 0;
    }
    int *table = (int *)(i2c + 1);
    return table[idx];
}

/*
 * Handle ":class" and ":abstract class" up to ":endclass".
 * Handle ":interface" up to ":endinterface".
 */
    void
ex_class(exarg_T *eap)
{
    int	    is_class = eap->cmdidx == CMD_class;  // FALSE for :interface
    long    start_lnum = SOURCING_LNUM;

    char_u *arg = eap->arg;
    int is_abstract = eap->cmdidx == CMD_abstract;
    if (is_abstract)
    {
	if (STRNCMP(arg, "class", 5) != 0 || !VIM_ISWHITE(arg[5]))
	{
	    semsg(_(e_invalid_argument_str), arg);
	    return;
	}
	arg = skipwhite(arg + 5);
	is_class = TRUE;
    }

    if (!current_script_is_vim9()
		|| (cmdmod.cmod_flags & CMOD_LEGACY)
		|| !getline_equal(eap->getline, eap->cookie, getsourceline))
    {
	if (is_class)
	    emsg(_(e_class_can_only_be_defined_in_vim9_script));
	else
	    emsg(_(e_interface_can_only_be_defined_in_vim9_script));
	return;
    }

    if (!ASCII_ISUPPER(*arg))
    {
	if (is_class)
	    semsg(_(e_class_name_must_start_with_uppercase_letter_str), arg);
	else
	    semsg(_(e_interface_name_must_start_with_uppercase_letter_str),
									  arg);
	return;
    }
    char_u *name_end = find_name_end(arg, NULL, NULL, FNE_CHECK_START);
    if (!IS_WHITE_OR_NUL(*name_end))
    {
	semsg(_(e_white_space_required_after_name_str), arg);
	return;
    }
    char_u *name_start = arg;

    // "export class" gets used when creating the class, don't use "is_export"
    // for the items inside the class.
    int class_export = is_export;
    is_export = FALSE;

    // TODO:
    //    generics: <Tkey, Tentry>

    // Name for "extends BaseClass"
    char_u *extends = NULL;

    // Names for "implements SomeInterface"
    garray_T	ga_impl;
    ga_init2(&ga_impl, sizeof(char_u *), 5);

    arg = skipwhite(name_end);
    while (*arg != NUL && *arg != '#' && *arg != '\n')
    {
	// TODO:
	//    specifies SomeInterface
	if (STRNCMP(arg, "extends", 7) == 0 && IS_WHITE_OR_NUL(arg[7]))
	{
	    if (extends != NULL)
	    {
		emsg(_(e_duplicate_extends));
		goto early_ret;
	    }
	    arg = skipwhite(arg + 7);
	    char_u *end = find_name_end(arg, NULL, NULL, FNE_CHECK_START);
	    if (!IS_WHITE_OR_NUL(*end))
	    {
		semsg(_(e_white_space_required_after_name_str), arg);
		goto early_ret;
	    }
	    extends = vim_strnsave(arg, end - arg);
	    if (extends == NULL)
		goto early_ret;

	    arg = skipwhite(end + 1);
	}
	else if (STRNCMP(arg, "implements", 10) == 0
						   && IS_WHITE_OR_NUL(arg[10]))
	{
	    if (ga_impl.ga_len > 0)
	    {
		emsg(_(e_duplicate_implements));
		goto early_ret;
	    }
	    arg = skipwhite(arg + 10);

	    for (;;)
	    {
		char_u *impl_end = find_name_end(arg, NULL, NULL,
							      FNE_CHECK_START);
		if (!IS_WHITE_OR_NUL(*impl_end) && *impl_end != ',')
		{
		    semsg(_(e_white_space_required_after_name_str), arg);
		    goto early_ret;
		}
		char_u *iname = vim_strnsave(arg, impl_end - arg);
		if (iname == NULL)
		    goto early_ret;
		for (int i = 0; i < ga_impl.ga_len; ++i)
		    if (STRCMP(((char_u **)ga_impl.ga_data)[i], iname) == 0)
		    {
			semsg(_(e_duplicate_interface_after_implements_str),
									iname);
			vim_free(iname);
			goto early_ret;
		    }
		if (ga_add_string(&ga_impl, iname) == FAIL)
		{
		    vim_free(iname);
		    goto early_ret;
		}
		if (*impl_end != ',')
		{
		    arg = skipwhite(impl_end);
		    break;
		}
		arg = skipwhite(impl_end + 1);
	    }
	}
	else
	{
	    semsg(_(e_trailing_characters_str), arg);
early_ret:
	    vim_free(extends);
	    ga_clear_strings(&ga_impl);
	    return;
	}
    }

    garray_T	type_list;	    // list of pointers to allocated types
    ga_init2(&type_list, sizeof(type_T *), 10);

    // Growarray with class members declared in the class.
    garray_T classmembers;
    ga_init2(&classmembers, sizeof(ocmember_T), 10);

    // Growarray with functions declared in the class.
    garray_T classfunctions;
    ga_init2(&classfunctions, sizeof(ufunc_T *), 10);

    // Growarray with object members declared in the class.
    garray_T objmembers;
    ga_init2(&objmembers, sizeof(ocmember_T), 10);

    // Growarray with object methods declared in the class.
    garray_T objmethods;
    ga_init2(&objmethods, sizeof(ufunc_T *), 10);

    /*
     * Go over the body of the class/interface until "endclass" or
     * "endinterface" is found.
     */
    char_u *theline = NULL;
    int success = FALSE;
    for (;;)
    {
	vim_free(theline);
	theline = eap->getline(':', eap->cookie, 0, GETLINE_CONCAT_ALL);
	if (theline == NULL)
	    break;
	char_u *line = skipwhite(theline);

	// Skip empty and comment lines.
	if (*line == NUL)
	    continue;
	if (*line == '#')
	{
	    if (vim9_bad_comment(line))
		break;
	    continue;
	}

	char_u *p = line;
	char *end_name = is_class ? "endclass" : "endinterface";
	if (checkforcmd(&p, end_name, is_class ? 4 : 5))
	{
	    if (STRNCMP(line, end_name, is_class ? 8 : 12) != 0)
		semsg(_(e_command_cannot_be_shortened_str), line);
	    else if (*p == '|' || !ends_excmd2(line, p))
		semsg(_(e_trailing_characters_str), p);
	    else
		success = TRUE;
	    break;
	}
	char *wrong_name = is_class ? "endinterface" : "endclass";
	if (checkforcmd(&p, wrong_name, is_class ? 5 : 4))
	{
	    semsg(_(e_invalid_command_str_expected_str), line, end_name);
	    break;
	}

	int has_public = FALSE;
	if (checkforcmd(&p, "public", 3))
	{
	    if (STRNCMP(line, "public", 6) != 0)
	    {
		semsg(_(e_command_cannot_be_shortened_str), line);
		break;
	    }
	    has_public = TRUE;
	    p = skipwhite(line + 6);

	    if (STRNCMP(p, "this", 4) != 0 && STRNCMP(p, "static", 6) != 0)
	    {
		emsg(_(e_public_must_be_followed_by_this_or_static));
		break;
	    }
	}

	int has_static = FALSE;
	char_u *ps = p;
	if (checkforcmd(&p, "static", 4))
	{
	    if (STRNCMP(ps, "static", 6) != 0)
	    {
		semsg(_(e_command_cannot_be_shortened_str), ps);
		break;
	    }
	    has_static = TRUE;
	    p = skipwhite(ps + 6);
	}

	// object members (public, read access, private):
	//	"this._varname"
	//	"this.varname"
	//	"public this.varname"
	if (STRNCMP(p, "this", 4) == 0)
	{
	    if (p[4] != '.' || !eval_isnamec1(p[5]))
	    {
		semsg(_(e_invalid_object_member_declaration_str), p);
		break;
	    }
	    char_u *varname = p + 5;
	    char_u *varname_end = NULL;
	    type_T *type = NULL;
	    char_u *init_expr = NULL;
	    if (parse_member(eap, line, varname, has_public,
			  &varname_end, &type_list, &type,
			  is_class ? &init_expr: NULL) == FAIL)
		break;
	    if (add_member(&objmembers, varname, varname_end,
					  has_public, type, init_expr) == FAIL)
	    {
		vim_free(init_expr);
		break;
	    }
	}

	// constructors:
	//	  def new()
	//	  enddef
	//	  def newOther()
	//	  enddef
	// object methods and class functions:
	//	  def SomeMethod()
	//	  enddef
	//	  static def ClassFunction()
	//	  enddef
	// TODO:
	//	  def <Tval> someMethod()
	//	  enddef
	else if (checkforcmd(&p, "def", 3))
	{
	    exarg_T	ea;
	    garray_T	lines_to_free;

	    // TODO: error for "public static def Func()"?

	    CLEAR_FIELD(ea);
	    ea.cmd = line;
	    ea.arg = p;
	    ea.cmdidx = CMD_def;
	    ea.getline = eap->getline;
	    ea.cookie = eap->cookie;

	    ga_init2(&lines_to_free, sizeof(char_u *), 50);
	    ufunc_T *uf = define_function(&ea, NULL, &lines_to_free,
					   is_class ? CF_CLASS : CF_INTERFACE);
	    ga_clear_strings(&lines_to_free);

	    if (uf != NULL)
	    {
		char_u *name = uf->uf_name;
		int is_new = STRNCMP(name, "new", 3) == 0;
		if (is_new && is_abstract)
		{
		    emsg(_(e_cannot_define_new_function_in_abstract_class));
		    success = FALSE;
		    break;
		}
		garray_T *fgap = has_static || is_new
					       ? &classfunctions : &objmethods;
		// Check the name isn't used already.
		for (int i = 0; i < fgap->ga_len; ++i)
		{
		    char_u *n = ((ufunc_T **)fgap->ga_data)[i]->uf_name;
		    if (STRCMP(name, n) == 0)
		    {
			semsg(_(e_duplicate_function_str), name);
			break;
		    }
		}

		if (ga_grow(fgap, 1) == OK)
		{
		    if (is_new)
			uf->uf_flags |= FC_NEW;

		    ((ufunc_T **)fgap->ga_data)[fgap->ga_len] = uf;
		    ++fgap->ga_len;
		}
	    }
	}

	// class members
	else if (has_static)
	{
	    // class members (public, read access, private):
	    //	"static _varname"
	    //	"static varname"
	    //	"public static varname"
	    char_u *varname = p;
	    char_u *varname_end = NULL;
	    type_T *type = NULL;
	    char_u *init_expr = NULL;
	    if (parse_member(eap, line, varname, has_public,
		      &varname_end, &type_list, &type,
		      is_class ? &init_expr : NULL) == FAIL)
		break;
	    if (add_member(&classmembers, varname, varname_end,
				      has_public, type, init_expr) == FAIL)
	    {
		vim_free(init_expr);
		break;
	    }
	}

	else
	{
	    if (is_class)
		semsg(_(e_not_valid_command_in_class_str), line);
	    else
		semsg(_(e_not_valid_command_in_interface_str), line);
	    break;
	}
    }
    vim_free(theline);

    class_T *extends_cl = NULL;  // class from "extends" argument

    /*
     * Check a few things before defining the class.
     */

    // Check the "extends" class is valid.
    if (success && extends != NULL)
    {
	typval_T tv;
	tv.v_type = VAR_UNKNOWN;
	if (eval_variable_import(extends, &tv) == FAIL)
	{
	    semsg(_(e_class_name_not_found_str), extends);
	    success = FALSE;
	}
	else
	{
	    if (tv.v_type != VAR_CLASS
		    || tv.vval.v_class == NULL
		    || (tv.vval.v_class->class_flags & CLASS_INTERFACE) != 0)
	    {
		semsg(_(e_cannot_extend_str), extends);
		success = FALSE;
	    }
	    else
	    {
		extends_cl = tv.vval.v_class;
		++extends_cl->class_refcount;
	    }
	    clear_tv(&tv);
	}
    }
    VIM_CLEAR(extends);

    class_T **intf_classes = NULL;

    // Check all "implements" entries are valid.
    if (success && ga_impl.ga_len > 0)
    {
	intf_classes = ALLOC_CLEAR_MULT(class_T *, ga_impl.ga_len);

	for (int i = 0; i < ga_impl.ga_len && success; ++i)
	{
	    char_u *impl = ((char_u **)ga_impl.ga_data)[i];
	    typval_T tv;
	    tv.v_type = VAR_UNKNOWN;
	    if (eval_variable_import(impl, &tv) == FAIL)
	    {
		semsg(_(e_interface_name_not_found_str), impl);
		success = FALSE;
		break;
	    }

	    if (tv.v_type != VAR_CLASS
		    || tv.vval.v_class == NULL
		    || (tv.vval.v_class->class_flags & CLASS_INTERFACE) == 0)
	    {
		semsg(_(e_not_valid_interface_str), impl);
		success = FALSE;
	    }

	    class_T *ifcl = tv.vval.v_class;
	    intf_classes[i] = ifcl;
	    ++ifcl->class_refcount;

	    // check the members of the interface match the members of the class
	    for (int loop = 1; loop <= 2 && success; ++loop)
	    {
		// loop == 1: check class members
		// loop == 2: check object members
		int if_count = loop == 1 ? ifcl->class_class_member_count
					 : ifcl->class_obj_member_count;
		if (if_count == 0)
		    continue;
		ocmember_T *if_ms = loop == 1 ? ifcl->class_class_members
					       : ifcl->class_obj_members;
		ocmember_T *cl_ms = (ocmember_T *)(loop == 1
						    ? classmembers.ga_data
						    : objmembers.ga_data);
		int cl_count = loop == 1 ? classmembers.ga_len
							   : objmembers.ga_len;
		for (int if_i = 0; if_i < if_count; ++if_i)
		{
		    int cl_i;
		    for (cl_i = 0; cl_i < cl_count; ++cl_i)
		    {
			ocmember_T *m = &cl_ms[cl_i];
			if (STRCMP(if_ms[if_i].ocm_name, m->ocm_name) == 0)
			{
			    // TODO: check type
			    break;
			}
		    }
		    if (cl_i == cl_count)
		    {
			semsg(_(e_member_str_of_interface_str_not_implemented),
						   if_ms[if_i].ocm_name, impl);
			success = FALSE;
			break;
		    }
		}
	    }

	    // check the functions/methods of the interface match the
	    // functions/methods of the class
	    for (int loop = 1; loop <= 2 && success; ++loop)
	    {
		// loop == 1: check class functions
		// loop == 2: check object methods
		int if_count = loop == 1 ? ifcl->class_class_function_count
					 : ifcl->class_obj_method_count;
		if (if_count == 0)
		    continue;
		ufunc_T **if_fp = loop == 1 ? ifcl->class_class_functions
					    : ifcl->class_obj_methods;
		ufunc_T **cl_fp = (ufunc_T **)(loop == 1
						? classfunctions.ga_data
						: objmethods.ga_data);
		int cl_count = loop == 1 ? classfunctions.ga_len
							   : objmethods.ga_len;
		for (int if_i = 0; if_i < if_count; ++if_i)
		{
		    char_u *if_name = if_fp[if_i]->uf_name;
		    int cl_i;
		    for (cl_i = 0; cl_i < cl_count; ++cl_i)
		    {
			char_u *cl_name = cl_fp[cl_i]->uf_name;
			if (STRCMP(if_name, cl_name) == 0)
			{
			    // TODO: check return and argument types
			    break;
			}
		    }
		    if (cl_i == cl_count)
		    {
			semsg(_(e_function_str_of_interface_str_not_implemented),
								if_name, impl);
			success = FALSE;
			break;
		    }
		}
	    }

	    clear_tv(&tv);
	}
    }

    if (success)
    {
	// Check no function argument name is used as an object/class member.
	for (int loop = 1; loop <= 2 && success; ++loop)
	{
	    garray_T *gap = loop == 1 ? &classfunctions : &objmethods;

	    for (int fi = 0; fi < gap->ga_len && success; ++fi)
	    {
		ufunc_T *uf = ((ufunc_T **)gap->ga_data)[fi];

		for (int i = 0; i < uf->uf_args.ga_len && success; ++i)
		{
		    char_u *aname = ((char_u **)uf->uf_args.ga_data)[i];
		    for (int il = 1; il <= 2 && success; ++il)
		    {
			// For a "new()" function "this.member" arguments are
			// OK.  TODO: check for the "this." prefix.
			if (STRNCMP(uf->uf_name, "new", 3) == 0 && il == 2)
			    continue;
			garray_T *mgap = il == 1 ? &classmembers : &objmembers;
			for (int mi = 0; mi < mgap->ga_len; ++mi)
			{
			    char_u *mname = ((ocmember_T *)mgap->ga_data
							       + mi)->ocm_name;
			    if (STRCMP(aname, mname) == 0)
			    {
				success = FALSE;
				semsg(_(e_argument_already_declared_in_class_str),
									aname);
				break;
			    }
			}
		    }
		}
	    }
	}
    }


    class_T *cl = NULL;
    if (success)
    {
	// "endclass" encountered without failures: Create the class.

	cl = ALLOC_CLEAR_ONE(class_T);
	if (cl == NULL)
	    goto cleanup;
	if (!is_class)
	    cl->class_flags = CLASS_INTERFACE;

	cl->class_refcount = 1;
	cl->class_name = vim_strnsave(name_start, name_end - name_start);
	if (cl->class_name == NULL)
	    goto cleanup;

	if (extends_cl != NULL)
	{
	    cl->class_extends = extends_cl;
	    extends_cl->class_flags |= CLASS_EXTENDED;
	}

	// Add class and object members to "cl".
	if (add_members_to_class(&classmembers,
				 extends_cl == NULL ? NULL
					     : extends_cl->class_class_members,
				 extends_cl == NULL ? 0
					: extends_cl->class_class_member_count,
				 &cl->class_class_members,
				 &cl->class_class_member_count) == FAIL
		|| add_members_to_class(&objmembers,
				 extends_cl == NULL ? NULL
					       : extends_cl->class_obj_members,
				 extends_cl == NULL ? 0
					  : extends_cl->class_obj_member_count,
				 &cl->class_obj_members,
				 &cl->class_obj_member_count) == FAIL)
	    goto cleanup;

	if (ga_impl.ga_len > 0)
	{
	    // Move the "implements" names into the class.
	    cl->class_interface_count = ga_impl.ga_len;
	    cl->class_interfaces = ALLOC_MULT(char_u *, ga_impl.ga_len);
	    if (cl->class_interfaces == NULL)
		goto cleanup;
	    for (int i = 0; i < ga_impl.ga_len; ++i)
		cl->class_interfaces[i] = ((char_u **)ga_impl.ga_data)[i];
	    VIM_CLEAR(ga_impl.ga_data);
	    ga_impl.ga_len = 0;

	    cl->class_interfaces_cl = intf_classes;
	    intf_classes = NULL;
	}

	if (cl->class_interface_count > 0 || extends_cl != NULL)
	{
	    // For each interface add a lookuptable for the member index on the
	    // interface to the member index in this class.
	    // And a lookuptable for the object method index on the interface
	    // to the object method index in this class.
	    // Also do this for the extended class, if any.
	    for (int i = 0; i <= cl->class_interface_count; ++i)
	    {
		class_T *ifcl = i < cl->class_interface_count
					    ? cl->class_interfaces_cl[i]
					    : extends_cl;
		if (ifcl == NULL)
		    continue;

		// Table for members.
		itf2class_T *if2cl = alloc_clear(sizeof(itf2class_T)
				 + ifcl->class_obj_member_count * sizeof(int));
		if (if2cl == NULL)
		    goto cleanup;
		if2cl->i2c_next = ifcl->class_itf2class;
		ifcl->class_itf2class = if2cl;
		if2cl->i2c_class = cl;
		if2cl->i2c_is_method = FALSE;

		for (int if_i = 0; if_i < ifcl->class_obj_member_count; ++if_i)
		    for (int cl_i = 0; cl_i < cl->class_obj_member_count;
									++cl_i)
		    {
			if (STRCMP(ifcl->class_obj_members[if_i].ocm_name,
				    cl->class_obj_members[cl_i].ocm_name) == 0)
			{
			    int *table = (int *)(if2cl + 1);
			    table[if_i] = cl_i;
			    break;
			}
		    }

		// Table for methods.
		if2cl = alloc_clear(sizeof(itf2class_T)
				 + ifcl->class_obj_method_count * sizeof(int));
		if (if2cl == NULL)
		    goto cleanup;
		if2cl->i2c_next = ifcl->class_itf2class;
		ifcl->class_itf2class = if2cl;
		if2cl->i2c_class = cl;
		if2cl->i2c_is_method = TRUE;

		for (int if_i = 0; if_i < ifcl->class_obj_method_count; ++if_i)
		{
		    int done = FALSE;
		    for (int cl_i = 0; cl_i < objmethods.ga_len; ++cl_i)
		    {
			if (STRCMP(ifcl->class_obj_methods[if_i]->uf_name,
			       ((ufunc_T **)objmethods.ga_data)[cl_i]->uf_name)
									  == 0)
			{
			    int *table = (int *)(if2cl + 1);
			    table[if_i] = cl_i;
			    done = TRUE;
			    break;
			}
		    }

		    if (!done && extends_cl != NULL)
		    {
			for (int cl_i = 0;
			     cl_i < extends_cl->class_obj_member_count; ++cl_i)
			{
			    if (STRCMP(ifcl->class_obj_methods[if_i]->uf_name,
				   extends_cl->class_obj_methods[cl_i]->uf_name)
									  == 0)
			    {
				int *table = (int *)(if2cl + 1);
				table[if_i] = cl_i;
				break;
			    }
			}
		    }
		}
	    }
	}

	if (is_class && cl->class_class_member_count > 0)
	{
	    // Allocate a typval for each class member and initialize it.
	    cl->class_members_tv = ALLOC_CLEAR_MULT(typval_T,
						 cl->class_class_member_count);
	    if (cl->class_members_tv != NULL)
		for (int i = 0; i < cl->class_class_member_count; ++i)
		{
		    ocmember_T *m = &cl->class_class_members[i];
		    typval_T *tv = &cl->class_members_tv[i];
		    if (m->ocm_init != NULL)
		    {
			typval_T *etv = eval_expr(m->ocm_init, eap);
			if (etv != NULL)
			{
			    *tv = *etv;
			    vim_free(etv);
			}
		    }
		    else
		    {
			// TODO: proper default value
			tv->v_type = m->ocm_type->tt_type;
			tv->vval.v_string = NULL;
		    }
		}
	}

	int have_new = FALSE;
	for (int i = 0; i < classfunctions.ga_len; ++i)
	    if (STRCMP(((ufunc_T **)classfunctions.ga_data)[i]->uf_name,
								   "new") == 0)
	    {
		have_new = TRUE;
		break;
	    }
	if (is_class && !is_abstract && !have_new)
	{
	    // No new() method was defined, add the default constructor.
	    garray_T fga;
	    ga_init2(&fga, 1, 1000);
	    ga_concat(&fga, (char_u *)"new(");
	    for (int i = 0; i < cl->class_obj_member_count; ++i)
	    {
		if (i > 0)
		    ga_concat(&fga, (char_u *)", ");
		ga_concat(&fga, (char_u *)"this.");
		ocmember_T *m = cl->class_obj_members + i;
		ga_concat(&fga, (char_u *)m->ocm_name);
		ga_concat(&fga, (char_u *)" = v:none");
	    }
	    ga_concat(&fga, (char_u *)")\nenddef\n");
	    ga_append(&fga, NUL);

	    exarg_T fea;
	    CLEAR_FIELD(fea);
	    fea.cmdidx = CMD_def;
	    fea.cmd = fea.arg = fga.ga_data;

	    garray_T lines_to_free;
	    ga_init2(&lines_to_free, sizeof(char_u *), 50);

	    ufunc_T *nf = define_function(&fea, NULL, &lines_to_free, CF_CLASS);

	    ga_clear_strings(&lines_to_free);
	    vim_free(fga.ga_data);

	    if (nf != NULL && ga_grow(&classfunctions, 1) == OK)
	    {
		((ufunc_T **)classfunctions.ga_data)[classfunctions.ga_len]
									  = nf;
		++classfunctions.ga_len;

		nf->uf_flags |= FC_NEW;
		nf->uf_ret_type = get_type_ptr(&type_list);
		if (nf->uf_ret_type != NULL)
		{
		    nf->uf_ret_type->tt_type = VAR_OBJECT;
		    nf->uf_ret_type->tt_class = cl;
		    nf->uf_ret_type->tt_argcount = 0;
		    nf->uf_ret_type->tt_args = NULL;
		}
	    }
	}

	// Move all the functions into the created class.
	// loop 1: class functions, loop 2: object methods
	for (int loop = 1; loop <= 2; ++loop)
	{
	    garray_T *gap = loop == 1 ? &classfunctions : &objmethods;
	    int	     *fcount = loop == 1 ? &cl->class_class_function_count
					 : &cl->class_obj_method_count;
	    ufunc_T ***fup = loop == 1 ? &cl->class_class_functions
				       : &cl->class_obj_methods;

	    int parent_count = 0;
	    if (extends_cl != NULL)
		// Include functions from the parent.
		parent_count = loop == 1
				    ? extends_cl->class_class_function_count
				    : extends_cl->class_obj_method_count;

	    *fcount = parent_count + gap->ga_len;
	    if (*fcount == 0)
	    {
		*fup = NULL;
		continue;
	    }
	    *fup = ALLOC_MULT(ufunc_T *, *fcount);
	    if (*fup == NULL)
		goto cleanup;

	    mch_memmove(*fup, gap->ga_data, sizeof(ufunc_T *) * gap->ga_len);
	    vim_free(gap->ga_data);
	    if (loop == 1)
		cl->class_class_function_count_child = gap->ga_len;
	    else
		cl->class_obj_method_count_child = gap->ga_len;

	    int skipped = 0;
	    for (int i = 0; i < parent_count; ++i)
	    {
		// Copy functions from the parent.  Can't use the same
		// function, because "uf_class" is different and compilation
		// will have a different result.
		// Put them after the functions in the current class, object
		// methods may be overruled, then "super.Method()" is used to
		// find a method from the parent.
		// Skip "new" functions. TODO: not all of them.
		if (loop == 1 && STRNCMP(
			    extends_cl->class_class_functions[i]->uf_name,
								"new", 3) == 0)
		    ++skipped;
		else
		{
		    ufunc_T *pf = (loop == 1
					? extends_cl->class_class_functions
					: extends_cl->class_obj_methods)[i];
		    (*fup)[gap->ga_len + i - skipped] = copy_function(pf);

		    // If the child class overrides a function from the parent
		    // the signature must be equal.
		    char_u *pname = pf->uf_name;
		    for (int ci = 0; ci < gap->ga_len; ++ci)
		    {
			ufunc_T *cf = (*fup)[ci];
			char_u *cname = cf->uf_name;
			if (STRCMP(pname, cname) == 0)
			{
			    where_T where = WHERE_INIT;
			    where.wt_func_name = (char *)pname;
			    (void)check_type(pf->uf_func_type, cf->uf_func_type,
								  TRUE, where);
			}
		    }
		}
	    }

	    *fcount -= skipped;

	    // Set the class pointer on all the functions and object methods.
	    for (int i = 0; i < *fcount; ++i)
	    {
		ufunc_T *fp = (*fup)[i];
		fp->uf_class = cl;
		if (loop == 2)
		    fp->uf_flags |= FC_OBJECT;
	    }
	}

	cl->class_type.tt_type = VAR_CLASS;
	cl->class_type.tt_class = cl;
	cl->class_object_type.tt_type = VAR_OBJECT;
	cl->class_object_type.tt_class = cl;
	cl->class_type_list = type_list;

	// TODO:
	// - Fill hashtab with object members and methods ?

	// Add the class to the script-local variables.
	// TODO: handle other context, e.g. in a function
	typval_T tv;
	tv.v_type = VAR_CLASS;
	tv.vval.v_class = cl;
	is_export = class_export;
	SOURCING_LNUM = start_lnum;
	set_var_const(cl->class_name, current_sctx.sc_sid,
						       NULL, &tv, FALSE, 0, 0);
	return;
    }

cleanup:
    if (cl != NULL)
    {
	vim_free(cl->class_name);
	vim_free(cl->class_class_functions);
	if (cl->class_interfaces != NULL)
	{
	    for (int i = 0; i < cl->class_interface_count; ++i)
		vim_free(cl->class_interfaces[i]);
	    vim_free(cl->class_interfaces);
	}
	if (cl->class_interfaces_cl != NULL)
	{
	    for (int i = 0; i < cl->class_interface_count; ++i)
		class_unref(cl->class_interfaces_cl[i]);
	    vim_free(cl->class_interfaces_cl);
	}
	vim_free(cl->class_obj_members);
	vim_free(cl->class_obj_methods);
	vim_free(cl);
    }

    vim_free(extends);
    class_unref(extends_cl);

    if (intf_classes != NULL)
    {
	for (int i = 0; i < ga_impl.ga_len; ++i)
	    class_unref(intf_classes[i]);
	vim_free(intf_classes);
    }
    ga_clear_strings(&ga_impl);

    for (int round = 1; round <= 2; ++round)
    {
	garray_T *gap = round == 1 ? &classmembers : &objmembers;
	if (gap->ga_len == 0 || gap->ga_data == NULL)
	    continue;

	for (int i = 0; i < gap->ga_len; ++i)
	{
	    ocmember_T *m = ((ocmember_T *)gap->ga_data) + i;
	    vim_free(m->ocm_name);
	    vim_free(m->ocm_init);
	}
	ga_clear(gap);
    }

    for (int i = 0; i < objmethods.ga_len; ++i)
    {
	ufunc_T *uf = ((ufunc_T **)objmethods.ga_data)[i];
	func_clear_free(uf, FALSE);
    }
    ga_clear(&objmethods);

    for (int i = 0; i < classfunctions.ga_len; ++i)
    {
	ufunc_T *uf = ((ufunc_T **)classfunctions.ga_data)[i];
	func_clear_free(uf, FALSE);
    }
    ga_clear(&classfunctions);

    clear_type_list(&type_list);
}

/*
 * Find member "name" in class "cl", set "member_idx" to the member index and
 * return its type.
 * When not found "member_idx" is set to -1 and t_any is returned.
 */
    type_T *
class_member_type(
	class_T *cl,
	char_u	*name,
	char_u	*name_end,
	int	*member_idx)
{
    *member_idx = -1;  // not found (yet)
    size_t len = name_end - name;

    for (int i = 0; i < cl->class_obj_member_count; ++i)
    {
	ocmember_T *m = cl->class_obj_members + i;
	if (STRNCMP(m->ocm_name, name, len) == 0 && m->ocm_name[len] == NUL)
	{
	    *member_idx = i;
	    return m->ocm_type;
	}
    }

    semsg(_(e_unknown_variable_str), name);
    return &t_any;
}

/*
 * Handle ":enum" up to ":endenum".
 */
    void
ex_enum(exarg_T *eap UNUSED)
{
    // TODO
}

/*
 * Handle ":type".
 */
    void
ex_type(exarg_T *eap UNUSED)
{
    // TODO
}

/*
 * Evaluate what comes after a class:
 * - class member: SomeClass.varname
 * - class function: SomeClass.SomeMethod()
 * - class constructor: SomeClass.new()
 * - object member: someObject.varname
 * - object method: someObject.SomeMethod()
 *
 * "*arg" points to the '.'.
 * "*arg" is advanced to after the member name or method call.
 *
 * Returns FAIL or OK.
 */
    int
class_object_index(
    char_u	**arg,
    typval_T	*rettv,
    evalarg_T	*evalarg,
    int		verbose UNUSED)	// give error messages
{
    if (VIM_ISWHITE((*arg)[1]))
    {
	semsg(_(e_no_white_space_allowed_after_str_str), ".", *arg);
	return FAIL;
    }

    ++*arg;
    char_u *name = *arg;
    char_u *name_end = find_name_end(name, NULL, NULL, FNE_CHECK_START);
    if (name_end == name)
	return FAIL;
    size_t len = name_end - name;

    class_T *cl;
    if (rettv->v_type == VAR_CLASS)
	cl = rettv->vval.v_class;
    else // VAR_OBJECT
    {
	if (rettv->vval.v_object == NULL)
	{
	    emsg(_(e_using_null_object));
	    return FAIL;
	}
	cl = rettv->vval.v_object->obj_class;
    }

    if (cl == NULL)
    {
	emsg(_(e_incomplete_type));
	return FAIL;
    }

    if (*name_end == '(')
    {
	int on_class = rettv->v_type == VAR_CLASS;
	int count = on_class ? cl->class_class_function_count
			     : cl->class_obj_method_count;
	for (int i = 0; i < count; ++i)
	{
	    ufunc_T *fp = on_class ? cl->class_class_functions[i]
				   : cl->class_obj_methods[i];
	    // Use a separate pointer to avoid that ASAN complains about
	    // uf_name[] only being 4 characters.
	    char_u *ufname = (char_u *)fp->uf_name;
	    if (STRNCMP(name, ufname, len) == 0 && ufname[len] == NUL)
	    {
		typval_T    argvars[MAX_FUNC_ARGS + 1];
		int	    argcount = 0;

		char_u *argp = name_end;
		int ret = get_func_arguments(&argp, evalarg, 0,
							   argvars, &argcount);
		if (ret == FAIL)
		    return FAIL;

		funcexe_T   funcexe;
		CLEAR_FIELD(funcexe);
		funcexe.fe_evaluate = TRUE;
		if (rettv->v_type == VAR_OBJECT)
		{
		    funcexe.fe_object = rettv->vval.v_object;
		    ++funcexe.fe_object->obj_refcount;
		}

		// Clear the class or object after calling the function, in
		// case the refcount is one.
		typval_T tv_tofree = *rettv;
		rettv->v_type = VAR_UNKNOWN;

		// Call the user function.  Result goes into rettv;
		int error = call_user_func_check(fp, argcount, argvars,
							rettv, &funcexe, NULL);

		// Clear the previous rettv and the arguments.
		clear_tv(&tv_tofree);
		for (int idx = 0; idx < argcount; ++idx)
		    clear_tv(&argvars[idx]);

		if (error != FCERR_NONE)
		{
		    user_func_error(error, printable_func_name(fp),
							 funcexe.fe_found_var);
		    return FAIL;
		}
		*arg = argp;
		return OK;
	    }
	}

	semsg(_(e_method_not_found_on_class_str_str), cl->class_name, name);
    }

    else if (rettv->v_type == VAR_OBJECT)
    {
	for (int i = 0; i < cl->class_obj_member_count; ++i)
	{
	    ocmember_T *m = &cl->class_obj_members[i];
	    if (STRNCMP(name, m->ocm_name, len) == 0 && m->ocm_name[len] == NUL)
	    {
		if (*name == '_')
		{
		    semsg(_(e_cannot_access_private_member_str), m->ocm_name);
		    return FAIL;
		}

		// The object only contains a pointer to the class, the member
		// values array follows right after that.
		object_T *obj = rettv->vval.v_object;
		typval_T *tv = (typval_T *)(obj + 1) + i;
		copy_tv(tv, rettv);
		object_unref(obj);

		*arg = name_end;
		return OK;
	    }
	}

	semsg(_(e_member_not_found_on_object_str_str), cl->class_name, name);
    }

    else if (rettv->v_type == VAR_CLASS)
    {
	// class member
	for (int i = 0; i < cl->class_class_member_count; ++i)
	{
	    ocmember_T *m = &cl->class_class_members[i];
	    if (STRNCMP(name, m->ocm_name, len) == 0 && m->ocm_name[len] == NUL)
	    {
		if (*name == '_')
		{
		    semsg(_(e_cannot_access_private_member_str), m->ocm_name);
		    return FAIL;
		}

		typval_T *tv = &cl->class_members_tv[i];
		copy_tv(tv, rettv);
		class_unref(cl);

		*arg = name_end;
		return OK;
	    }
	}

	semsg(_(e_member_not_found_on_class_str_str), cl->class_name, name);
    }

    return FAIL;
}

/*
 * If "arg" points to a class or object method, return it.
 * Otherwise return NULL.
 */
    ufunc_T *
find_class_func(char_u **arg)
{
    char_u *name = *arg;
    char_u *name_end = find_name_end(name, NULL, NULL, FNE_CHECK_START);
    if (name_end == name || *name_end != '.')
	return NULL;

    size_t len = name_end - name;
    typval_T tv;
    tv.v_type = VAR_UNKNOWN;
    if (eval_variable(name, (int)len,
				    0, &tv, NULL, EVAL_VAR_NOAUTOLOAD) == FAIL)
	return NULL;
    if (tv.v_type != VAR_CLASS && tv.v_type != VAR_OBJECT)
	goto fail_after_eval;

    class_T *cl = tv.v_type == VAR_CLASS ? tv.vval.v_class
						 : tv.vval.v_object->obj_class;
    if (cl == NULL)
	goto fail_after_eval;
    char_u *fname = name_end + 1;
    char_u *fname_end = find_name_end(fname, NULL, NULL, FNE_CHECK_START);
    if (fname_end == fname)
	goto fail_after_eval;
    len = fname_end - fname;

    int count = tv.v_type == VAR_CLASS ? cl->class_class_function_count
				       : cl->class_obj_method_count;
    ufunc_T **funcs = tv.v_type == VAR_CLASS ? cl->class_class_functions
					     : cl->class_obj_methods;
    for (int i = 0; i < count; ++i)
    {
	ufunc_T *fp = funcs[i];
	// Use a separate pointer to avoid that ASAN complains about
	// uf_name[] only being 4 characters.
	char_u *ufname = (char_u *)fp->uf_name;
	if (STRNCMP(fname, ufname, len) == 0 && ufname[len] == NUL)
	{
	    clear_tv(&tv);
	    return fp;
	}
    }

fail_after_eval:
    clear_tv(&tv);
    return NULL;
}

/*
 * If "name[len]" is a class member in cctx->ctx_ufunc->uf_class return the
 * index in class.class_class_members[].
 * If "cl_ret" is not NULL set it to the class.
 * Otherwise return -1;
 */
    int
class_member_index(char_u *name, size_t len, class_T **cl_ret, cctx_T *cctx)
{
    if (cctx == NULL || cctx->ctx_ufunc == NULL
					  || cctx->ctx_ufunc->uf_class == NULL)
	return -1;
    class_T *cl = cctx->ctx_ufunc->uf_class;

    for (int i = 0; i < cl->class_class_member_count; ++i)
    {
	ocmember_T *m = &cl->class_class_members[i];
	if (STRNCMP(name, m->ocm_name, len) == 0 && m->ocm_name[len] == NUL)
	{
	    if (cl_ret != NULL)
		*cl_ret = cl;
	    return i;
	}
    }
    return -1;
}

/*
 * Return TRUE if current context "cctx_arg" is inside class "cl".
 * Return FALSE if not.
 */
    int
inside_class(cctx_T *cctx_arg, class_T *cl)
{
    for (cctx_T *cctx = cctx_arg; cctx != NULL; cctx = cctx->ctx_outer)
	if (cctx->ctx_ufunc != NULL && cctx->ctx_ufunc->uf_class == cl)
	    return TRUE;
    return FALSE;
}

/*
 * Make a copy of an object.
 */
    void
copy_object(typval_T *from, typval_T *to)
{
    *to = *from;
    if (to->vval.v_object != NULL)
	++to->vval.v_object->obj_refcount;
}

/*
 * Free an object.
 */
    static void
object_clear(object_T *obj)
{
    class_T *cl = obj->obj_class;

    // the member values are just after the object structure
    typval_T *tv = (typval_T *)(obj + 1);
    for (int i = 0; i < cl->class_obj_member_count; ++i)
	clear_tv(tv + i);

    // Remove from the list headed by "first_object".
    object_cleared(obj);

    vim_free(obj);
    class_unref(cl);
}

/*
 * Unreference an object.
 */
    void
object_unref(object_T *obj)
{
    if (obj != NULL && --obj->obj_refcount <= 0)
	object_clear(obj);
}

/*
 * Make a copy of a class.
 */
    void
copy_class(typval_T *from, typval_T *to)
{
    *to = *from;
    if (to->vval.v_class != NULL)
	++to->vval.v_class->class_refcount;
}

/*
 * Unreference a class.  Free it when the reference count goes down to zero.
 */
    void
class_unref(class_T *cl)
{
    if (cl != NULL && --cl->class_refcount <= 0 && cl->class_name != NULL)
    {
	// Freeing what the class contains may recursively come back here.
	// Clear "class_name" first, if it is NULL the class does not need to
	// be freed.
	VIM_CLEAR(cl->class_name);

	class_unref(cl->class_extends);

	for (int i = 0; i < cl->class_interface_count; ++i)
	{
	    vim_free(((char_u **)cl->class_interfaces)[i]);
	    if (cl->class_interfaces_cl[i] != NULL)
		class_unref(cl->class_interfaces_cl[i]);
	}
	vim_free(cl->class_interfaces);
	vim_free(cl->class_interfaces_cl);

	itf2class_T *next;
	for (itf2class_T *i2c = cl->class_itf2class; i2c != NULL; i2c = next)
	{
	    next = i2c->i2c_next;
	    vim_free(i2c);
	}

	for (int i = 0; i < cl->class_class_member_count; ++i)
	{
	    ocmember_T *m = &cl->class_class_members[i];
	    vim_free(m->ocm_name);
	    vim_free(m->ocm_init);
	    if (cl->class_members_tv != NULL)
		clear_tv(&cl->class_members_tv[i]);
	}
	vim_free(cl->class_class_members);
	vim_free(cl->class_members_tv);

	for (int i = 0; i < cl->class_obj_member_count; ++i)
	{
	    ocmember_T *m = &cl->class_obj_members[i];
	    vim_free(m->ocm_name);
	    vim_free(m->ocm_init);
	}
	vim_free(cl->class_obj_members);

	for (int i = 0; i < cl->class_class_function_count; ++i)
	{
	    ufunc_T *uf = cl->class_class_functions[i];
	    func_clear_free(uf, FALSE);
	}
	vim_free(cl->class_class_functions);

	for (int i = 0; i < cl->class_obj_method_count; ++i)
	{
	    ufunc_T *uf = cl->class_obj_methods[i];
	    func_clear_free(uf, FALSE);
	}
	vim_free(cl->class_obj_methods);

	clear_type_list(&cl->class_type_list);

	vim_free(cl);
    }
}

static object_T *first_object = NULL;

/*
 * Call this function when an object has been created.  It will be added to the
 * list headed by "first_object".
 */
    void
object_created(object_T *obj)
{
    if (first_object != NULL)
    {
	obj->obj_next_used = first_object;
	first_object->obj_prev_used = obj;
    }
    first_object = obj;
}

/*
 * Call this function when an object has been cleared and is about to be freed.
 * It is removed from the list headed by "first_object".
 */
    void
object_cleared(object_T *obj)
{
    if (obj->obj_next_used != NULL)
	obj->obj_next_used->obj_prev_used = obj->obj_prev_used;
    if (obj->obj_prev_used != NULL)
	obj->obj_prev_used->obj_next_used = obj->obj_next_used;
    else if (first_object == obj)
	first_object = obj->obj_next_used;
}

/*
 * Go through the list of all objects and free items without "copyID".
 */
    int
object_free_nonref(int copyID)
{
    int		did_free = FALSE;
    object_T	*next_obj;

    for (object_T *obj = first_object; obj != NULL; obj = next_obj)
    {
	next_obj = obj->obj_next_used;
	if ((obj->obj_copyID & COPYID_MASK) != (copyID & COPYID_MASK))
	{
	    // Free the object and items it contains.
	    object_clear(obj);
	    did_free = TRUE;
	}
    }

    return did_free;
}


#endif // FEAT_EVAL
