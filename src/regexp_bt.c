/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * Backtracking regular expression implementation.
 *
 * This file is included in "regexp.c".
 *
 * NOTICE:
 *
 * This is NOT the original regular expression code as written by Henry
 * Spencer.  This code has been modified specifically for use with the VIM
 * editor, and should not be used separately from Vim.  If you want a good
 * regular expression library, get the original code.  The copyright notice
 * that follows is from the original.
 *
 * END NOTICE
 *
 *	Copyright (c) 1986 by University of Toronto.
 *	Written by Henry Spencer.  Not derived from licensed software.
 *
 *	Permission is granted to anyone to use this software for any
 *	purpose on any computer system, and to redistribute it freely,
 *	subject to the following restrictions:
 *
 *	1. The author is not responsible for the consequences of use of
 *		this software, no matter how awful, even if they arise
 *		from defects in it.
 *
 *	2. The origin of this software must not be misrepresented, either
 *		by explicit claim or by omission.
 *
 *	3. Altered versions must be plainly marked as such, and must not
 *		be misrepresented as being the original software.
 *
 * Beware that some of this code is subtly aware of the way operator
 * precedence is structured in regular expressions.  Serious changes in
 * regular-expression syntax might require a total rethink.
 *
 * Changes have been made by Tony Andrews, Olaf 'Rhialto' Seibert, Robert
 * Webb, Ciaran McCreesh and Bram Moolenaar.
 * Named character class support added by Walter Briscoe (1998 Jul 01)
 */

/*
 * The "internal use only" fields in regexp.h are present to pass info from
 * compile to execute that permits the execute phase to run lots faster on
 * simple cases.  They are:
 *
 * regstart	char that must begin a match; NUL if none obvious; Can be a
 *		multi-byte character.
 * reganch	is the match anchored (at beginning-of-line only)?
 * regmust	string (pointer into program) that match must include, or NULL
 * regmlen	length of regmust string
 * regflags	RF_ values or'ed together
 *
 * Regstart and reganch permit very fast decisions on suitable starting points
 * for a match, cutting down the work a lot.  Regmust permits fast rejection
 * of lines that cannot possibly match.  The regmust tests are costly enough
 * that vim_regcomp() supplies a regmust only if the r.e. contains something
 * potentially expensive (at present, the only such thing detected is * or +
 * at the start of the r.e., which can involve a lot of backup).  Regmlen is
 * supplied because the test in vim_regexec() needs it and vim_regcomp() is
 * computing it anyway.
 */

/*
 * Structure for regexp "program".  This is essentially a linear encoding
 * of a nondeterministic finite-state machine (aka syntax charts or
 * "railroad normal form" in parsing technology).  Each node is an opcode
 * plus a "next" pointer, possibly plus an operand.  "Next" pointers of
 * all nodes except BRANCH and BRACES_COMPLEX implement concatenation; a "next"
 * pointer with a BRANCH on both ends of it is connecting two alternatives.
 * (Here we have one of the subtle syntax dependencies:	an individual BRANCH
 * (as opposed to a collection of them) is never concatenated with anything
 * because of operator precedence).  The "next" pointer of a BRACES_COMPLEX
 * node points to the node after the stuff to be repeated.
 * The operand of some types of node is a literal string; for others, it is a
 * node leading into a sub-FSM.  In particular, the operand of a BRANCH node
 * is the first node of the branch.
 * (NB this is *not* a tree structure: the tail of the branch connects to the
 * thing following the set of BRANCHes.)
 *
 * pattern	is coded like:
 *
 *			  +-----------------+
 *			  |		    V
 * <aa>\|<bb>	BRANCH <aa> BRANCH <bb> --> END
 *		     |	    ^	 |	    ^
 *		     +------+	 +----------+
 *
 *
 *		       +------------------+
 *		       V		  |
 * <aa>*	BRANCH BRANCH <aa> --> BACK BRANCH --> NOTHING --> END
 *		     |	    |		    ^			   ^
 *		     |	    +---------------+			   |
 *		     +---------------------------------------------+
 *
 *
 *		       +----------------------+
 *		       V		      |
 * <aa>\+	BRANCH <aa> --> BRANCH --> BACK  BRANCH --> NOTHING --> END
 *		     |		     |		 ^			^
 *		     |		     +-----------+			|
 *		     +--------------------------------------------------+
 *
 *
 *					+-------------------------+
 *					V			  |
 * <aa>\{}	BRANCH BRACE_LIMITS --> BRACE_COMPLEX <aa> --> BACK  END
 *		     |				    |		     ^
 *		     |				    +----------------+
 *		     +-----------------------------------------------+
 *
 *
 * <aa>\@!<bb>	BRANCH NOMATCH <aa> --> END  <bb> --> END
 *		     |	     |		      ^       ^
 *		     |	     +----------------+       |
 *		     +--------------------------------+
 *
 *						      +---------+
 *						      |		V
 * \z[abc]	BRANCH BRANCH  a  BRANCH  b  BRANCH  c	BRANCH	NOTHING --> END
 *		     |	    |	       |	  |	^		    ^
 *		     |	    |	       |	  +-----+		    |
 *		     |	    |	       +----------------+		    |
 *		     |	    +---------------------------+		    |
 *		     +------------------------------------------------------+
 *
 * They all start with a BRANCH for "\|" alternatives, even when there is only
 * one alternative.
 */

/*
 * The opcodes are:
 */

// definition	number		   opnd?    meaning
#define END		0	//	End of program or NOMATCH operand.
#define BOL		1	//	Match "" at beginning of line.
#define EOL		2	//	Match "" at end of line.
#define BRANCH		3	// node Match this alternative, or the
				//	next...
#define BACK		4	//	Match "", "next" ptr points backward.
#define EXACTLY		5	// str	Match this string.
#define NOTHING		6	//	Match empty string.
#define STAR		7	// node Match this (simple) thing 0 or more
				//	times.
#define PLUS		8	// node Match this (simple) thing 1 or more
				//	times.
#define MATCH		9	// node match the operand zero-width
#define NOMATCH		10	// node check for no match with operand
#define BEHIND		11	// node look behind for a match with operand
#define NOBEHIND	12	// node look behind for no match with operand
#define SUBPAT		13	// node match the operand here
#define BRACE_SIMPLE	14	// node Match this (simple) thing between m and
				//	n times (\{m,n\}).
#define BOW		15	//	Match "" after [^a-zA-Z0-9_]
#define EOW		16	//	Match "" at    [^a-zA-Z0-9_]
#define BRACE_LIMITS	17	// nr nr  define the min & max for BRACE_SIMPLE
				//	and BRACE_COMPLEX.
#define NEWL		18	//	Match line-break
#define BHPOS		19	//	End position for BEHIND or NOBEHIND


// character classes: 20-48 normal, 50-78 include a line-break
#define ADD_NL		30
#define FIRST_NL	ANY + ADD_NL
#define ANY		20	//	Match any one character.
#define ANYOF		21	// str	Match any character in this string.
#define ANYBUT		22	// str	Match any character not in this
				//	string.
#define IDENT		23	//	Match identifier char
#define SIDENT		24	//	Match identifier char but no digit
#define KWORD		25	//	Match keyword char
#define SKWORD		26	//	Match word char but no digit
#define FNAME		27	//	Match file name char
#define SFNAME		28	//	Match file name char but no digit
#define PRINT		29	//	Match printable char
#define SPRINT		30	//	Match printable char but no digit
#define WHITE		31	//	Match whitespace char
#define NWHITE		32	//	Match non-whitespace char
#define DIGIT		33	//	Match digit char
#define NDIGIT		34	//	Match non-digit char
#define HEX		35	//	Match hex char
#define NHEX		36	//	Match non-hex char
#define OCTAL		37	//	Match octal char
#define NOCTAL		38	//	Match non-octal char
#define WORD		39	//	Match word char
#define NWORD		40	//	Match non-word char
#define HEAD		41	//	Match head char
#define NHEAD		42	//	Match non-head char
#define ALPHA		43	//	Match alpha char
#define NALPHA		44	//	Match non-alpha char
#define LOWER		45	//	Match lowercase char
#define NLOWER		46	//	Match non-lowercase char
#define UPPER		47	//	Match uppercase char
#define NUPPER		48	//	Match non-uppercase char
#define LAST_NL		NUPPER + ADD_NL
#define WITH_NL(op)	((op) >= FIRST_NL && (op) <= LAST_NL)

#define MOPEN		80  // -89	 Mark this point in input as start of
				//	 \( subexpr.  MOPEN + 0 marks start of
				//	 match.
#define MCLOSE		90  // -99	 Analogous to MOPEN.  MCLOSE + 0 marks
				//	 end of match.
#define BACKREF		100 // -109 node Match same string again \1-\9

#ifdef FEAT_SYN_HL
# define ZOPEN		110 // -119	 Mark this point in input as start of
				//	 \z( subexpr.
# define ZCLOSE		120 // -129	 Analogous to ZOPEN.
# define ZREF		130 // -139 node Match external submatch \z1-\z9
#endif

#define BRACE_COMPLEX	140 // -149 node Match nodes between m & n times

#define NOPEN		150	//	Mark this point in input as start of
				//	\%( subexpr.
#define NCLOSE		151	//	Analogous to NOPEN.

#define MULTIBYTECODE	200	// mbc	Match one multi-byte character
#define RE_BOF		201	//	Match "" at beginning of file.
#define RE_EOF		202	//	Match "" at end of file.
#define CURSOR		203	//	Match location of cursor.

#define RE_LNUM		204	// nr cmp  Match line number
#define RE_COL		205	// nr cmp  Match column number
#define RE_VCOL		206	// nr cmp  Match virtual column number

#define RE_MARK		207	// mark cmp  Match mark position
#define RE_VISUAL	208	//	Match Visual area
#define RE_COMPOSING	209	// any composing characters

/*
 * Flags to be passed up and down.
 */
#define HASWIDTH	0x1	// Known never to match null string.
#define SIMPLE		0x2	// Simple enough to be STAR/PLUS operand.
#define SPSTART		0x4	// Starts with * or +.
#define HASNL		0x8	// Contains some \n.
#define HASLOOKBH	0x10	// Contains "\@<=" or "\@<!".
#define WORST		0	// Worst case.

static int	num_complex_braces; // Complex \{...} count
static char_u	*regcode;	// Code-emit pointer, or JUST_CALC_SIZE
static long	regsize;	// Code size.
static int	reg_toolong;	// TRUE when offset out of range
static char_u	had_endbrace[NSUBEXP];	// flags, TRUE if end of () found
static long	brace_min[10];	// Minimums for complex brace repeats
static long	brace_max[10];	// Maximums for complex brace repeats
static int	brace_count[10]; // Current counts for complex brace repeats
static int	one_exactly = FALSE;	// only do one char for EXACTLY

// When making changes to classchars also change nfa_classcodes.
static char_u	*classchars = (char_u *)".iIkKfFpPsSdDxXoOwWhHaAlLuU";
static int	classcodes[] = {
    ANY, IDENT, SIDENT, KWORD, SKWORD,
    FNAME, SFNAME, PRINT, SPRINT,
    WHITE, NWHITE, DIGIT, NDIGIT,
    HEX, NHEX, OCTAL, NOCTAL,
    WORD, NWORD, HEAD, NHEAD,
    ALPHA, NALPHA, LOWER, NLOWER,
    UPPER, NUPPER
};

/*
 * When regcode is set to this value, code is not emitted and size is computed
 * instead.
 */
#define JUST_CALC_SIZE	((char_u *) -1)

// Values for rs_state in regitem_T.
typedef enum regstate_E
{
    RS_NOPEN = 0	// NOPEN and NCLOSE
    , RS_MOPEN		// MOPEN + [0-9]
    , RS_MCLOSE		// MCLOSE + [0-9]
#ifdef FEAT_SYN_HL
    , RS_ZOPEN		// ZOPEN + [0-9]
    , RS_ZCLOSE		// ZCLOSE + [0-9]
#endif
    , RS_BRANCH		// BRANCH
    , RS_BRCPLX_MORE	// BRACE_COMPLEX and trying one more match
    , RS_BRCPLX_LONG	// BRACE_COMPLEX and trying longest match
    , RS_BRCPLX_SHORT	// BRACE_COMPLEX and trying shortest match
    , RS_NOMATCH	// NOMATCH
    , RS_BEHIND1	// BEHIND / NOBEHIND matching rest
    , RS_BEHIND2	// BEHIND / NOBEHIND matching behind part
    , RS_STAR_LONG	// STAR/PLUS/BRACE_SIMPLE longest match
    , RS_STAR_SHORT	// STAR/PLUS/BRACE_SIMPLE shortest match
} regstate_T;

/*
 * Structure used to save the current input state, when it needs to be
 * restored after trying a match.  Used by reg_save() and reg_restore().
 * Also stores the length of "backpos".
 */
typedef struct
{
    union
    {
	char_u	*ptr;	// rex.input pointer, for single-line regexp
	lpos_T	pos;	// rex.input pos, for multi-line regexp
    } rs_u;
    int		rs_len;
} regsave_T;

// struct to save start/end pointer/position in for \(\)
typedef struct
{
    union
    {
	char_u	*ptr;
	lpos_T	pos;
    } se_u;
} save_se_T;

// used for BEHIND and NOBEHIND matching
typedef struct regbehind_S
{
    regsave_T	save_after;
    regsave_T	save_behind;
    int		save_need_clear_subexpr;
    save_se_T   save_start[NSUBEXP];
    save_se_T   save_end[NSUBEXP];
} regbehind_T;

/*
 * When there are alternatives a regstate_T is put on the regstack to remember
 * what we are doing.
 * Before it may be another type of item, depending on rs_state, to remember
 * more things.
 */
typedef struct regitem_S
{
    regstate_T	rs_state;	// what we are doing, one of RS_ above
    short	rs_no;		// submatch nr or BEHIND/NOBEHIND
    char_u	*rs_scan;	// current node in program
    union
    {
	save_se_T  sesave;
	regsave_T  regsave;
    } rs_un;			// room for saving rex.input
} regitem_T;


// used for STAR, PLUS and BRACE_SIMPLE matching
typedef struct regstar_S
{
    int		nextb;		// next byte
    int		nextb_ic;	// next byte reverse case
    long	count;
    long	minval;
    long	maxval;
} regstar_T;

// used to store input position when a BACK was encountered, so that we now if
// we made any progress since the last time.
typedef struct backpos_S
{
    char_u	*bp_scan;	// "scan" where BACK was encountered
    regsave_T	bp_pos;		// last input position
} backpos_T;

/*
 * "regstack" and "backpos" are used by regmatch().  They are kept over calls
 * to avoid invoking malloc() and free() often.
 * "regstack" is a stack with regitem_T items, sometimes preceded by regstar_T
 * or regbehind_T.
 * "backpos_T" is a table with backpos_T for BACK
 */
static garray_T	regstack = {0, 0, 0, 0, NULL};
static garray_T	backpos = {0, 0, 0, 0, NULL};

static regsave_T behind_pos;

/*
 * Both for regstack and backpos tables we use the following strategy of
 * allocation (to reduce malloc/free calls):
 * - Initial size is fairly small.
 * - When needed, the tables are grown bigger (8 times at first, double after
 *   that).
 * - After executing the match we free the memory only if the array has grown.
 *   Thus the memory is kept allocated when it's at the initial size.
 * This makes it fast while not keeping a lot of memory allocated.
 * A three times speed increase was observed when using many simple patterns.
 */
#define REGSTACK_INITIAL	2048
#define BACKPOS_INITIAL		64

/*
 * Opcode notes:
 *
 * BRANCH	The set of branches constituting a single choice are hooked
 *		together with their "next" pointers, since precedence prevents
 *		anything being concatenated to any individual branch.  The
 *		"next" pointer of the last BRANCH in a choice points to the
 *		thing following the whole choice.  This is also where the
 *		final "next" pointer of each individual branch points; each
 *		branch starts with the operand node of a BRANCH node.
 *
 * BACK		Normal "next" pointers all implicitly point forward; BACK
 *		exists to make loop structures possible.
 *
 * STAR,PLUS	'=', and complex '*' and '+', are implemented as circular
 *		BRANCH structures using BACK.  Simple cases (one character
 *		per match) are implemented with STAR and PLUS for speed
 *		and to minimize recursive plunges.
 *
 * BRACE_LIMITS	This is always followed by a BRACE_SIMPLE or BRACE_COMPLEX
 *		node, and defines the min and max limits to be used for that
 *		node.
 *
 * MOPEN,MCLOSE	...are numbered at compile time.
 * ZOPEN,ZCLOSE	...ditto
 */

/*
 * A node is one char of opcode followed by two chars of "next" pointer.
 * "Next" pointers are stored as two 8-bit bytes, high order first.  The
 * value is a positive offset from the opcode of the node containing it.
 * An operand, if any, simply follows the node.  (Note that much of the
 * code generation knows about this implicit relationship.)
 *
 * Using two bytes for the "next" pointer is vast overkill for most things,
 * but allows patterns to get big without disasters.
 */
#define OP(p)		((int)*(p))
#define NEXT(p)		(((*((p) + 1) & 0377) << 8) + (*((p) + 2) & 0377))
#define OPERAND(p)	((p) + 3)
// Obtain an operand that was stored as four bytes, MSB first.
#define OPERAND_MIN(p)	(((long)(p)[3] << 24) + ((long)(p)[4] << 16) \
			+ ((long)(p)[5] << 8) + (long)(p)[6])
// Obtain a second operand stored as four bytes.
#define OPERAND_MAX(p)	OPERAND_MIN((p) + 4)
// Obtain a second single-byte operand stored after a four bytes operand.
#define OPERAND_CMP(p)	(p)[7]

static char_u *reg(int paren, int *flagp);

#ifdef BT_REGEXP_DUMP
static void	regdump(char_u *, bt_regprog_T *);
#endif

static int	re_num_cmp(long_u val, char_u *scan);

#ifdef DEBUG
static char_u	*regprop(char_u *);

static int	regnarrate = 0;
#endif


/*
 * Setup to parse the regexp.  Used once to get the length and once to do it.
 */
    static void
regcomp_start(
    char_u	*expr,
    int		re_flags)	    // see vim_regcomp()
{
    initchr(expr);
    if (re_flags & RE_MAGIC)
	reg_magic = MAGIC_ON;
    else
	reg_magic = MAGIC_OFF;
    reg_string = (re_flags & RE_STRING);
    reg_strict = (re_flags & RE_STRICT);
    get_cpo_flags();

    num_complex_braces = 0;
    regnpar = 1;
    CLEAR_FIELD(had_endbrace);
#ifdef FEAT_SYN_HL
    regnzpar = 1;
    re_has_z = 0;
#endif
    regsize = 0L;
    reg_toolong = FALSE;
    regflags = 0;
#if defined(FEAT_SYN_HL) || defined(PROTO)
    had_eol = FALSE;
#endif
}

/*
 * Return TRUE if MULTIBYTECODE should be used instead of EXACTLY for
 * character "c".
 */
    static int
use_multibytecode(int c)
{
    return has_mbyte && (*mb_char2len)(c) > 1
		     && (re_multi_type(peekchr()) != NOT_MULTI
			     || (enc_utf8 && utf_iscomposing(c)));
}

/*
 * Emit (if appropriate) a byte of code
 */
    static void
regc(int b)
{
    if (regcode == JUST_CALC_SIZE)
	regsize++;
    else
	*regcode++ = b;
}

/*
 * Emit (if appropriate) a multi-byte character of code
 */
    static void
regmbc(int c)
{
    if (!has_mbyte && c > 0xff)
	return;
    if (regcode == JUST_CALC_SIZE)
	regsize += (*mb_char2len)(c);
    else
	regcode += (*mb_char2bytes)(c, regcode);
}

#define REGMBC(x) regmbc(x);
#define CASEMBC(x) case x:

/*
 * Produce the bytes for equivalence class "c".
 * Currently only handles latin1, latin9 and utf-8.
 * NOTE: When changing this function, also change nfa_emit_equi_class()
 */
    static void
reg_equi_class(int c)
{
    if (enc_utf8 || STRCMP(p_enc, "latin1") == 0
					 || STRCMP(p_enc, "iso-8859-15") == 0)
    {
#ifdef EBCDIC
	int i;

	// This might be slower than switch/case below.
	for (i = 0; i < 16; i++)
	{
	    if (vim_strchr(EQUIVAL_CLASS_C[i], c) != NULL)
	    {
		char *p = EQUIVAL_CLASS_C[i];

		while (*p != 0)
		    regmbc(*p++);
		return;
	    }
	}
#else
	/* <rdar://problem/54102067>
	 * POSIX or C locales require equivalance classes to only recognize
	 * the English alphabet
	 */
	const char *locale = setlocale(LC_COLLATE, NULL);
	if (STRCMP(locale, "POSIX") == 0 || STRCMP(locale, "C") == 0)
	{
	   regmbc(c);
	   return;
	}

	switch (c)
	{
	    // Do not use '\300' style, it results in a negative number.
	    case 'A': case 0xc0: case 0xc1: case 0xc2:
	    case 0xc3: case 0xc4: case 0xc5:
	    CASEMBC(0x100) CASEMBC(0x102) CASEMBC(0x104) CASEMBC(0x1cd)
	    CASEMBC(0x1de) CASEMBC(0x1e0) CASEMBC(0x1ea2)
		      regmbc('A'); regmbc(0xc0); regmbc(0xc1);
		      regmbc(0xc2); regmbc(0xc3); regmbc(0xc4);
		      regmbc(0xc5);
		      REGMBC(0x100) REGMBC(0x102) REGMBC(0x104)
		      REGMBC(0x1cd) REGMBC(0x1de) REGMBC(0x1e0)
		      REGMBC(0x1ea2)
		      return;
	    case 'B': CASEMBC(0x1e02) CASEMBC(0x1e06)
		      regmbc('B'); REGMBC(0x1e02) REGMBC(0x1e06)
		      return;
	    case 'C': case 0xc7:
	    CASEMBC(0x106) CASEMBC(0x108) CASEMBC(0x10a) CASEMBC(0x10c)
		      regmbc('C'); regmbc(0xc7);
		      REGMBC(0x106) REGMBC(0x108) REGMBC(0x10a)
		      REGMBC(0x10c)
		      return;
	    case 'D': CASEMBC(0x10e) CASEMBC(0x110) CASEMBC(0x1e0a)
	    CASEMBC(0x1e0e) CASEMBC(0x1e10)
		      regmbc('D'); REGMBC(0x10e) REGMBC(0x110)
		      REGMBC(0x1e0a) REGMBC(0x1e0e) REGMBC(0x1e10)
		      return;
	    case 'E': case 0xc8: case 0xc9: case 0xca: case 0xcb:
	    CASEMBC(0x112) CASEMBC(0x114) CASEMBC(0x116) CASEMBC(0x118)
	    CASEMBC(0x11a) CASEMBC(0x1eba) CASEMBC(0x1ebc)
		      regmbc('E'); regmbc(0xc8); regmbc(0xc9);
		      regmbc(0xca); regmbc(0xcb);
		      REGMBC(0x112) REGMBC(0x114) REGMBC(0x116)
		      REGMBC(0x118) REGMBC(0x11a) REGMBC(0x1eba)
		      REGMBC(0x1ebc)
		      return;
	    case 'F': CASEMBC(0x1e1e)
		      regmbc('F'); REGMBC(0x1e1e)
		      return;
	    case 'G': CASEMBC(0x11c) CASEMBC(0x11e) CASEMBC(0x120)
	    CASEMBC(0x122) CASEMBC(0x1e4) CASEMBC(0x1e6) CASEMBC(0x1f4)
	    CASEMBC(0x1e20)
		      regmbc('G'); REGMBC(0x11c) REGMBC(0x11e)
		      REGMBC(0x120) REGMBC(0x122) REGMBC(0x1e4)
		      REGMBC(0x1e6) REGMBC(0x1f4) REGMBC(0x1e20)
		      return;
	    case 'H': CASEMBC(0x124) CASEMBC(0x126) CASEMBC(0x1e22)
	    CASEMBC(0x1e26) CASEMBC(0x1e28)
		      regmbc('H'); REGMBC(0x124) REGMBC(0x126)
		      REGMBC(0x1e22) REGMBC(0x1e26) REGMBC(0x1e28)
		      return;
	    case 'I': case 0xcc: case 0xcd: case 0xce: case 0xcf:
	    CASEMBC(0x128) CASEMBC(0x12a) CASEMBC(0x12c) CASEMBC(0x12e)
	    CASEMBC(0x130) CASEMBC(0x1cf) CASEMBC(0x1ec8)
		      regmbc('I'); regmbc(0xcc); regmbc(0xcd);
		      regmbc(0xce); regmbc(0xcf);
		      REGMBC(0x128) REGMBC(0x12a) REGMBC(0x12c)
		      REGMBC(0x12e) REGMBC(0x130) REGMBC(0x1cf)
		      REGMBC(0x1ec8)
		      return;
	    case 'J': CASEMBC(0x134)
		      regmbc('J'); REGMBC(0x134)
		      return;
	    case 'K': CASEMBC(0x136) CASEMBC(0x1e8) CASEMBC(0x1e30)
	    CASEMBC(0x1e34)
		      regmbc('K'); REGMBC(0x136) REGMBC(0x1e8)
		      REGMBC(0x1e30) REGMBC(0x1e34)
		      return;
	    case 'L': CASEMBC(0x139) CASEMBC(0x13b) CASEMBC(0x13d)
	    CASEMBC(0x13f) CASEMBC(0x141) CASEMBC(0x1e3a)
		      regmbc('L'); REGMBC(0x139) REGMBC(0x13b)
		      REGMBC(0x13d) REGMBC(0x13f) REGMBC(0x141)
		      REGMBC(0x1e3a)
		      return;
	    case 'M': CASEMBC(0x1e3e) CASEMBC(0x1e40)
		      regmbc('M'); REGMBC(0x1e3e) REGMBC(0x1e40)
		      return;
	    case 'N': case 0xd1:
	    CASEMBC(0x143) CASEMBC(0x145) CASEMBC(0x147) CASEMBC(0x1e44)
	    CASEMBC(0x1e48)
		      regmbc('N'); regmbc(0xd1);
		      REGMBC(0x143) REGMBC(0x145) REGMBC(0x147)
		      REGMBC(0x1e44) REGMBC(0x1e48)
		      return;
	    case 'O': case 0xd2: case 0xd3: case 0xd4: case 0xd5:
	    case 0xd6: case 0xd8:
	    CASEMBC(0x14c) CASEMBC(0x14e) CASEMBC(0x150) CASEMBC(0x1a0)
	    CASEMBC(0x1d1) CASEMBC(0x1ea) CASEMBC(0x1ec) CASEMBC(0x1ece)
		      regmbc('O'); regmbc(0xd2); regmbc(0xd3);
		      regmbc(0xd4); regmbc(0xd5); regmbc(0xd6);
		      regmbc(0xd8);
		      REGMBC(0x14c) REGMBC(0x14e) REGMBC(0x150)
		      REGMBC(0x1a0) REGMBC(0x1d1) REGMBC(0x1ea)
		      REGMBC(0x1ec) REGMBC(0x1ece)
		      return;
	    case 'P': case 0x1e54: case 0x1e56:
		      regmbc('P'); REGMBC(0x1e54) REGMBC(0x1e56)
		      return;
	    case 'R': CASEMBC(0x154) CASEMBC(0x156) CASEMBC(0x158)
	    CASEMBC(0x1e58) CASEMBC(0x1e5e)
		      regmbc('R'); REGMBC(0x154) REGMBC(0x156) REGMBC(0x158)
		      REGMBC(0x1e58) REGMBC(0x1e5e)
		      return;
	    case 'S': CASEMBC(0x15a) CASEMBC(0x15c) CASEMBC(0x15e)
	    CASEMBC(0x160) CASEMBC(0x1e60)
		      regmbc('S'); REGMBC(0x15a) REGMBC(0x15c)
		      REGMBC(0x15e) REGMBC(0x160) REGMBC(0x1e60)
		      return;
	    case 'T': CASEMBC(0x162) CASEMBC(0x164) CASEMBC(0x166)
	    CASEMBC(0x1e6a) CASEMBC(0x1e6e)
		      regmbc('T'); REGMBC(0x162) REGMBC(0x164)
		      REGMBC(0x166) REGMBC(0x1e6a) REGMBC(0x1e6e)
		      return;
	    case 'U': case 0xd9: case 0xda: case 0xdb: case 0xdc:
	    CASEMBC(0x168) CASEMBC(0x16a) CASEMBC(0x16c) CASEMBC(0x16e)
	    CASEMBC(0x170) CASEMBC(0x172) CASEMBC(0x1af) CASEMBC(0x1d3)
	    CASEMBC(0x1ee6)
		      regmbc('U'); regmbc(0xd9); regmbc(0xda);
		      regmbc(0xdb); regmbc(0xdc);
		      REGMBC(0x168) REGMBC(0x16a) REGMBC(0x16c)
		      REGMBC(0x16e) REGMBC(0x170) REGMBC(0x172)
		      REGMBC(0x1af) REGMBC(0x1d3) REGMBC(0x1ee6)
		      return;
	    case 'V': CASEMBC(0x1e7c)
		      regmbc('V'); REGMBC(0x1e7c)
		      return;
	    case 'W': CASEMBC(0x174) CASEMBC(0x1e80) CASEMBC(0x1e82)
	    CASEMBC(0x1e84) CASEMBC(0x1e86)
		      regmbc('W'); REGMBC(0x174) REGMBC(0x1e80)
		      REGMBC(0x1e82) REGMBC(0x1e84) REGMBC(0x1e86)
		      return;
	    case 'X': CASEMBC(0x1e8a) CASEMBC(0x1e8c)
		      regmbc('X'); REGMBC(0x1e8a) REGMBC(0x1e8c)
		      return;
	    case 'Y': case 0xdd:
	    CASEMBC(0x176) CASEMBC(0x178) CASEMBC(0x1e8e) CASEMBC(0x1ef2)
	    CASEMBC(0x1ef6) CASEMBC(0x1ef8)
		      regmbc('Y'); regmbc(0xdd);
		      REGMBC(0x176) REGMBC(0x178) REGMBC(0x1e8e)
		      REGMBC(0x1ef2) REGMBC(0x1ef6) REGMBC(0x1ef8)
		      return;
	    case 'Z': CASEMBC(0x179) CASEMBC(0x17b) CASEMBC(0x17d)
	    CASEMBC(0x1b5) CASEMBC(0x1e90) CASEMBC(0x1e94)
		      regmbc('Z'); REGMBC(0x179) REGMBC(0x17b)
		      REGMBC(0x17d) REGMBC(0x1b5) REGMBC(0x1e90)
		      REGMBC(0x1e94)
		      return;
	    case 'a': case 0xe0: case 0xe1: case 0xe2:
	    case 0xe3: case 0xe4: case 0xe5:
	    CASEMBC(0x101) CASEMBC(0x103) CASEMBC(0x105) CASEMBC(0x1ce)
	    CASEMBC(0x1df) CASEMBC(0x1e1) CASEMBC(0x1ea3)
		      regmbc('a'); regmbc(0xe0); regmbc(0xe1);
		      regmbc(0xe2); regmbc(0xe3); regmbc(0xe4);
		      regmbc(0xe5);
		      REGMBC(0x101) REGMBC(0x103) REGMBC(0x105)
		      REGMBC(0x1ce) REGMBC(0x1df) REGMBC(0x1e1)
		      REGMBC(0x1ea3)
		      return;
	    case 'b': CASEMBC(0x1e03) CASEMBC(0x1e07)
		      regmbc('b'); REGMBC(0x1e03) REGMBC(0x1e07)
		      return;
	    case 'c': case 0xe7:
	    CASEMBC(0x107) CASEMBC(0x109) CASEMBC(0x10b) CASEMBC(0x10d)
		      regmbc('c'); regmbc(0xe7);
		      REGMBC(0x107) REGMBC(0x109) REGMBC(0x10b)
		      REGMBC(0x10d)
		      return;
	    case 'd': CASEMBC(0x10f) CASEMBC(0x111) CASEMBC(0x1e0b)
	    CASEMBC(0x1e0f) CASEMBC(0x1e11)
		      regmbc('d'); REGMBC(0x10f) REGMBC(0x111)
		      REGMBC(0x1e0b) REGMBC(0x1e0f) REGMBC(0x1e11)
		      return;
	    case 'e': case 0xe8: case 0xe9: case 0xea: case 0xeb:
	    CASEMBC(0x113) CASEMBC(0x115) CASEMBC(0x117) CASEMBC(0x119)
	    CASEMBC(0x11b) CASEMBC(0x1ebb) CASEMBC(0x1ebd)
		      regmbc('e'); regmbc(0xe8); regmbc(0xe9);
		      regmbc(0xea); regmbc(0xeb);
		      REGMBC(0x113) REGMBC(0x115) REGMBC(0x117)
		      REGMBC(0x119) REGMBC(0x11b) REGMBC(0x1ebb)
		      REGMBC(0x1ebd)
		      return;
	    case 'f': CASEMBC(0x1e1f)
		      regmbc('f'); REGMBC(0x1e1f)
		      return;
	    case 'g': CASEMBC(0x11d) CASEMBC(0x11f) CASEMBC(0x121)
	    CASEMBC(0x123) CASEMBC(0x1e5) CASEMBC(0x1e7) CASEMBC(0x1f5)
	    CASEMBC(0x1e21)
		      regmbc('g'); REGMBC(0x11d) REGMBC(0x11f)
		      REGMBC(0x121) REGMBC(0x123) REGMBC(0x1e5)
		      REGMBC(0x1e7) REGMBC(0x1f5) REGMBC(0x1e21)
		      return;
	    case 'h': CASEMBC(0x125) CASEMBC(0x127) CASEMBC(0x1e23)
	    CASEMBC(0x1e27) CASEMBC(0x1e29) CASEMBC(0x1e96)
		      regmbc('h'); REGMBC(0x125) REGMBC(0x127)
		      REGMBC(0x1e23) REGMBC(0x1e27) REGMBC(0x1e29)
		      REGMBC(0x1e96)
		      return;
	    case 'i': case 0xec: case 0xed: case 0xee: case 0xef:
	    CASEMBC(0x129) CASEMBC(0x12b) CASEMBC(0x12d) CASEMBC(0x12f)
	    CASEMBC(0x1d0) CASEMBC(0x1ec9)
		      regmbc('i'); regmbc(0xec); regmbc(0xed);
		      regmbc(0xee); regmbc(0xef);
		      REGMBC(0x129) REGMBC(0x12b) REGMBC(0x12d)
		      REGMBC(0x12f) REGMBC(0x1d0) REGMBC(0x1ec9)
		      return;
	    case 'j': CASEMBC(0x135) CASEMBC(0x1f0)
		      regmbc('j'); REGMBC(0x135) REGMBC(0x1f0)
		      return;
	    case 'k': CASEMBC(0x137) CASEMBC(0x1e9) CASEMBC(0x1e31)
	    CASEMBC(0x1e35)
		      regmbc('k'); REGMBC(0x137) REGMBC(0x1e9)
		      REGMBC(0x1e31) REGMBC(0x1e35)
		      return;
	    case 'l': CASEMBC(0x13a) CASEMBC(0x13c) CASEMBC(0x13e)
	    CASEMBC(0x140) CASEMBC(0x142) CASEMBC(0x1e3b)
		      regmbc('l'); REGMBC(0x13a) REGMBC(0x13c)
		      REGMBC(0x13e) REGMBC(0x140) REGMBC(0x142)
		      REGMBC(0x1e3b)
		      return;
	    case 'm': CASEMBC(0x1e3f) CASEMBC(0x1e41)
		      regmbc('m'); REGMBC(0x1e3f) REGMBC(0x1e41)
		      return;
	    case 'n': case 0xf1:
	    CASEMBC(0x144) CASEMBC(0x146) CASEMBC(0x148) CASEMBC(0x149)
	    CASEMBC(0x1e45) CASEMBC(0x1e49)
		      regmbc('n'); regmbc(0xf1);
		      REGMBC(0x144) REGMBC(0x146) REGMBC(0x148)
		      REGMBC(0x149) REGMBC(0x1e45) REGMBC(0x1e49)
		      return;
	    case 'o': case 0xf2: case 0xf3: case 0xf4: case 0xf5:
	    case 0xf6: case 0xf8:
	    CASEMBC(0x14d) CASEMBC(0x14f) CASEMBC(0x151) CASEMBC(0x1a1)
	    CASEMBC(0x1d2) CASEMBC(0x1eb) CASEMBC(0x1ed) CASEMBC(0x1ecf)
		      regmbc('o'); regmbc(0xf2); regmbc(0xf3);
		      regmbc(0xf4); regmbc(0xf5); regmbc(0xf6);
		      regmbc(0xf8);
		      REGMBC(0x14d) REGMBC(0x14f) REGMBC(0x151)
		      REGMBC(0x1a1) REGMBC(0x1d2) REGMBC(0x1eb)
		      REGMBC(0x1ed) REGMBC(0x1ecf)
		      return;
	    case 'p': CASEMBC(0x1e55) CASEMBC(0x1e57)
		      regmbc('p'); REGMBC(0x1e55) REGMBC(0x1e57)
		      return;
	    case 'r': CASEMBC(0x155) CASEMBC(0x157) CASEMBC(0x159)
	    CASEMBC(0x1e59) CASEMBC(0x1e5f)
		      regmbc('r'); REGMBC(0x155) REGMBC(0x157) REGMBC(0x159)
		      REGMBC(0x1e59) REGMBC(0x1e5f)
		      return;
	    case 's': CASEMBC(0x15b) CASEMBC(0x15d) CASEMBC(0x15f)
	    CASEMBC(0x161) CASEMBC(0x1e61)
		      regmbc('s'); REGMBC(0x15b) REGMBC(0x15d)
		      REGMBC(0x15f) REGMBC(0x161) REGMBC(0x1e61)
		      return;
	    case 't': CASEMBC(0x163) CASEMBC(0x165) CASEMBC(0x167)
	    CASEMBC(0x1e6b) CASEMBC(0x1e6f) CASEMBC(0x1e97)
		      regmbc('t'); REGMBC(0x163) REGMBC(0x165) REGMBC(0x167)
		      REGMBC(0x1e6b) REGMBC(0x1e6f) REGMBC(0x1e97)
		      return;
	    case 'u': case 0xf9: case 0xfa: case 0xfb: case 0xfc:
	    CASEMBC(0x169) CASEMBC(0x16b) CASEMBC(0x16d) CASEMBC(0x16f)
	    CASEMBC(0x171) CASEMBC(0x173) CASEMBC(0x1b0) CASEMBC(0x1d4)
	    CASEMBC(0x1ee7)
		      regmbc('u'); regmbc(0xf9); regmbc(0xfa);
		      regmbc(0xfb); regmbc(0xfc);
		      REGMBC(0x169) REGMBC(0x16b) REGMBC(0x16d)
		      REGMBC(0x16f) REGMBC(0x171) REGMBC(0x173)
		      REGMBC(0x1b0) REGMBC(0x1d4) REGMBC(0x1ee7)
		      return;
	    case 'v': CASEMBC(0x1e7d)
		      regmbc('v'); REGMBC(0x1e7d)
		      return;
	    case 'w': CASEMBC(0x175) CASEMBC(0x1e81) CASEMBC(0x1e83)
	    CASEMBC(0x1e85) CASEMBC(0x1e87) CASEMBC(0x1e98)
		      regmbc('w'); REGMBC(0x175) REGMBC(0x1e81)
		      REGMBC(0x1e83) REGMBC(0x1e85) REGMBC(0x1e87)
		      REGMBC(0x1e98)
		      return;
	    case 'x': CASEMBC(0x1e8b) CASEMBC(0x1e8d)
		      regmbc('x'); REGMBC(0x1e8b) REGMBC(0x1e8d)
		      return;
	    case 'y': case 0xfd: case 0xff:
	    CASEMBC(0x177) CASEMBC(0x1e8f) CASEMBC(0x1e99)
	    CASEMBC(0x1ef3) CASEMBC(0x1ef7) CASEMBC(0x1ef9)
		      regmbc('y'); regmbc(0xfd); regmbc(0xff);
		      REGMBC(0x177) REGMBC(0x1e8f) REGMBC(0x1e99)
		      REGMBC(0x1ef3) REGMBC(0x1ef7) REGMBC(0x1ef9)
		      return;
	    case 'z': CASEMBC(0x17a) CASEMBC(0x17c) CASEMBC(0x17e)
	    CASEMBC(0x1b6) CASEMBC(0x1e91) CASEMBC(0x1e95)
		      regmbc('z'); REGMBC(0x17a) REGMBC(0x17c)
		      REGMBC(0x17e) REGMBC(0x1b6) REGMBC(0x1e91)
		      REGMBC(0x1e95)
		      return;
	}
#endif
    }
    regmbc(c);
}

/*
 * Emit a node.
 * Return pointer to generated code.
 */
    static char_u *
regnode(int op)
{
    char_u  *ret;

    ret = regcode;
    if (ret == JUST_CALC_SIZE)
	regsize += 3;
    else
    {
	*regcode++ = op;
	*regcode++ = NUL;		// Null "next" pointer.
	*regcode++ = NUL;
    }
    return ret;
}

/*
 * Write a long as four bytes at "p" and return pointer to the next char.
 */
    static char_u *
re_put_long(char_u *p, long_u val)
{
    *p++ = (char_u) ((val >> 24) & 0377);
    *p++ = (char_u) ((val >> 16) & 0377);
    *p++ = (char_u) ((val >> 8) & 0377);
    *p++ = (char_u) (val & 0377);
    return p;
}

/*
 * regnext - dig the "next" pointer out of a node
 * Returns NULL when calculating size, when there is no next item and when
 * there is an error.
 */
    static char_u *
regnext(char_u *p)
{
    int	    offset;

    if (p == JUST_CALC_SIZE || reg_toolong)
	return NULL;

    offset = NEXT(p);
    if (offset == 0)
	return NULL;

    if (OP(p) == BACK)
	return p - offset;
    else
	return p + offset;
}

/*
 * Set the next-pointer at the end of a node chain.
 */
    static void
regtail(char_u *p, char_u *val)
{
    char_u	*scan;
    char_u	*temp;
    int		offset;

    if (p == JUST_CALC_SIZE)
	return;

    // Find last node.
    scan = p;
    for (;;)
    {
	temp = regnext(scan);
	if (temp == NULL)
	    break;
	scan = temp;
    }

    if (OP(scan) == BACK)
	offset = (int)(scan - val);
    else
	offset = (int)(val - scan);
    // When the offset uses more than 16 bits it can no longer fit in the two
    // bytes available.  Use a global flag to avoid having to check return
    // values in too many places.
    if (offset > 0xffff)
	reg_toolong = TRUE;
    else
    {
	*(scan + 1) = (char_u) (((unsigned)offset >> 8) & 0377);
	*(scan + 2) = (char_u) (offset & 0377);
    }
}

/*
 * Like regtail, on item after a BRANCH; nop if none.
 */
    static void
regoptail(char_u *p, char_u *val)
{
    // When op is neither BRANCH nor BRACE_COMPLEX0-9, it is "operandless"
    if (p == NULL || p == JUST_CALC_SIZE
	    || (OP(p) != BRANCH
		&& (OP(p) < BRACE_COMPLEX || OP(p) > BRACE_COMPLEX + 9)))
	return;
    regtail(OPERAND(p), val);
}

/*
 * Insert an operator in front of already-emitted operand
 *
 * Means relocating the operand.
 */
    static void
reginsert(int op, char_u *opnd)
{
    char_u	*src;
    char_u	*dst;
    char_u	*place;

    if (regcode == JUST_CALC_SIZE)
    {
	regsize += 3;
	return;
    }
    src = regcode;
    regcode += 3;
    dst = regcode;
    while (src > opnd)
	*--dst = *--src;

    place = opnd;		// Op node, where operand used to be.
    *place++ = op;
    *place++ = NUL;
    *place = NUL;
}

/*
 * Insert an operator in front of already-emitted operand.
 * Add a number to the operator.
 */
    static void
reginsert_nr(int op, long val, char_u *opnd)
{
    char_u	*src;
    char_u	*dst;
    char_u	*place;

    if (regcode == JUST_CALC_SIZE)
    {
	regsize += 7;
	return;
    }
    src = regcode;
    regcode += 7;
    dst = regcode;
    while (src > opnd)
	*--dst = *--src;

    place = opnd;		// Op node, where operand used to be.
    *place++ = op;
    *place++ = NUL;
    *place++ = NUL;
    re_put_long(place, (long_u)val);
}

/*
 * Insert an operator in front of already-emitted operand.
 * The operator has the given limit values as operands.  Also set next pointer.
 *
 * Means relocating the operand.
 */
    static void
reginsert_limits(
    int		op,
    long	minval,
    long	maxval,
    char_u	*opnd)
{
    char_u	*src;
    char_u	*dst;
    char_u	*place;

    if (regcode == JUST_CALC_SIZE)
    {
	regsize += 11;
	return;
    }
    src = regcode;
    regcode += 11;
    dst = regcode;
    while (src > opnd)
	*--dst = *--src;

    place = opnd;		// Op node, where operand used to be.
    *place++ = op;
    *place++ = NUL;
    *place++ = NUL;
    place = re_put_long(place, (long_u)minval);
    place = re_put_long(place, (long_u)maxval);
    regtail(opnd, place);
}

/*
 * Return TRUE if the back reference is legal. We must have seen the close
 * brace.
 * TODO: Should also check that we don't refer to something that is repeated
 * (+*=): what instance of the repetition should we match?
 */
    static int
seen_endbrace(int refnum)
{
    if (!had_endbrace[refnum])
    {
	char_u *p;

	// Trick: check if "@<=" or "@<!" follows, in which case
	// the \1 can appear before the referenced match.
	for (p = regparse; *p != NUL; ++p)
	    if (p[0] == '@' && p[1] == '<' && (p[2] == '!' || p[2] == '='))
		break;
	if (*p == NUL)
	{
	    emsg(_("E65: Illegal back reference"));
	    rc_did_emsg = TRUE;
	    return FALSE;
	}
    }
    return TRUE;
}

/*
 * Parse the lowest level.
 *
 * Optimization:  gobbles an entire sequence of ordinary characters so that
 * it can turn them into a single node, which is smaller to store and
 * faster to run.  Don't do this when one_exactly is set.
 */
    static char_u *
regatom(int *flagp)
{
    char_u	    *ret;
    int		    flags;
    int		    c;
    char_u	    *p;
    int		    extra = 0;
    int		    save_prev_at_start = prev_at_start;

    *flagp = WORST;		// Tentatively.

    c = getchr();
    switch (c)
    {
      case Magic('^'):
	ret = regnode(BOL);
	break;

      case Magic('$'):
	ret = regnode(EOL);
#if defined(FEAT_SYN_HL) || defined(PROTO)
	had_eol = TRUE;
#endif
	break;

      case Magic('<'):
	ret = regnode(BOW);
	break;

      case Magic('>'):
	ret = regnode(EOW);
	break;

      case Magic('_'):
	c = no_Magic(getchr());
	if (c == '^')		// "\_^" is start-of-line
	{
	    ret = regnode(BOL);
	    break;
	}
	if (c == '$')		// "\_$" is end-of-line
	{
	    ret = regnode(EOL);
#if defined(FEAT_SYN_HL) || defined(PROTO)
	    had_eol = TRUE;
#endif
	    break;
	}

	extra = ADD_NL;
	*flagp |= HASNL;

	// "\_[" is character range plus newline
	if (c == '[')
	    goto collection;

	// "\_x" is character class plus newline
	// FALLTHROUGH

	// Character classes.
      case Magic('.'):
      case Magic('i'):
      case Magic('I'):
      case Magic('k'):
      case Magic('K'):
      case Magic('f'):
      case Magic('F'):
      case Magic('p'):
      case Magic('P'):
      case Magic('s'):
      case Magic('S'):
      case Magic('d'):
      case Magic('D'):
      case Magic('x'):
      case Magic('X'):
      case Magic('o'):
      case Magic('O'):
      case Magic('w'):
      case Magic('W'):
      case Magic('h'):
      case Magic('H'):
      case Magic('a'):
      case Magic('A'):
      case Magic('l'):
      case Magic('L'):
      case Magic('u'):
      case Magic('U'):
	p = vim_strchr(classchars, no_Magic(c));
	if (p == NULL)
	    EMSG_RET_NULL(_("E63: invalid use of \\_"));

	// When '.' is followed by a composing char ignore the dot, so that
	// the composing char is matched here.
	if (enc_utf8 && c == Magic('.') && utf_iscomposing(peekchr()))
	{
	    c = getchr();
	    goto do_multibyte;
	}
	ret = regnode(classcodes[p - classchars] + extra);
	*flagp |= HASWIDTH | SIMPLE;
	break;

      case Magic('n'):
	if (reg_string)
	{
	    // In a string "\n" matches a newline character.
	    ret = regnode(EXACTLY);
	    regc(NL);
	    regc(NUL);
	    *flagp |= HASWIDTH | SIMPLE;
	}
	else
	{
	    // In buffer text "\n" matches the end of a line.
	    ret = regnode(NEWL);
	    *flagp |= HASWIDTH | HASNL;
	}
	break;

      case Magic('('):
	if (one_exactly)
	    EMSG_ONE_RET_NULL;
	ret = reg(REG_PAREN, &flags);
	if (ret == NULL)
	    return NULL;
	*flagp |= flags & (HASWIDTH | SPSTART | HASNL | HASLOOKBH);
	break;

      case NUL:
      case Magic('|'):
      case Magic('&'):
      case Magic(')'):
	if (one_exactly)
	    EMSG_ONE_RET_NULL;
	IEMSG_RET_NULL(_(e_internal));	// Supposed to be caught earlier.
	// NOTREACHED

      case Magic('='):
      case Magic('?'):
      case Magic('+'):
      case Magic('@'):
      case Magic('{'):
      case Magic('*'):
	c = no_Magic(c);
	EMSG3_RET_NULL(_("E64: %s%c follows nothing"),
		(c == '*' ? reg_magic >= MAGIC_ON : reg_magic == MAGIC_ALL), c);
	// NOTREACHED

      case Magic('~'):		// previous substitute pattern
	    if (reg_prev_sub != NULL)
	    {
		char_u	    *lp;

		ret = regnode(EXACTLY);
		lp = reg_prev_sub;
		while (*lp != NUL)
		    regc(*lp++);
		regc(NUL);
		if (*reg_prev_sub != NUL)
		{
		    *flagp |= HASWIDTH;
		    if ((lp - reg_prev_sub) == 1)
			*flagp |= SIMPLE;
		}
	    }
	    else
		EMSG_RET_NULL(_(e_nopresub));
	    break;

      case Magic('1'):
      case Magic('2'):
      case Magic('3'):
      case Magic('4'):
      case Magic('5'):
      case Magic('6'):
      case Magic('7'):
      case Magic('8'):
      case Magic('9'):
	    {
		int		    refnum;

		refnum = c - Magic('0');
		if (!seen_endbrace(refnum))
		    return NULL;
		ret = regnode(BACKREF + refnum);
	    }
	    break;

      case Magic('z'):
	{
	    c = no_Magic(getchr());
	    switch (c)
	    {
#ifdef FEAT_SYN_HL
		case '(': if ((reg_do_extmatch & REX_SET) == 0)
			      EMSG_RET_NULL(_(e_z_not_allowed));
			  if (one_exactly)
			      EMSG_ONE_RET_NULL;
			  ret = reg(REG_ZPAREN, &flags);
			  if (ret == NULL)
			      return NULL;
			  *flagp |= flags & (HASWIDTH|SPSTART|HASNL|HASLOOKBH);
			  re_has_z = REX_SET;
			  break;

		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': if ((reg_do_extmatch & REX_USE) == 0)
			      EMSG_RET_NULL(_(e_z1_not_allowed));
			  ret = regnode(ZREF + c - '0');
			  re_has_z = REX_USE;
			  break;
#endif

		case 's': ret = regnode(MOPEN + 0);
			  if (re_mult_next("\\zs") == FAIL)
			      return NULL;
			  break;

		case 'e': ret = regnode(MCLOSE + 0);
			  if (re_mult_next("\\ze") == FAIL)
			      return NULL;
			  break;

		default:  EMSG_RET_NULL(_("E68: Invalid character after \\z"));
	    }
	}
	break;

      case Magic('%'):
	{
	    c = no_Magic(getchr());
	    switch (c)
	    {
		// () without a back reference
		case '(':
		    if (one_exactly)
			EMSG_ONE_RET_NULL;
		    ret = reg(REG_NPAREN, &flags);
		    if (ret == NULL)
			return NULL;
		    *flagp |= flags & (HASWIDTH | SPSTART | HASNL | HASLOOKBH);
		    break;

		// Catch \%^ and \%$ regardless of where they appear in the
		// pattern -- regardless of whether or not it makes sense.
		case '^':
		    ret = regnode(RE_BOF);
		    break;

		case '$':
		    ret = regnode(RE_EOF);
		    break;

		case '#':
		    ret = regnode(CURSOR);
		    break;

		case 'V':
		    ret = regnode(RE_VISUAL);
		    break;

		case 'C':
		    ret = regnode(RE_COMPOSING);
		    break;

		// \%[abc]: Emit as a list of branches, all ending at the last
		// branch which matches nothing.
		case '[':
			  if (one_exactly)	// doesn't nest
			      EMSG_ONE_RET_NULL;
			  {
			      char_u	*lastbranch;
			      char_u	*lastnode = NULL;
			      char_u	*br;

			      ret = NULL;
			      while ((c = getchr()) != ']')
			      {
				  if (c == NUL)
				      EMSG2_RET_NULL(_(e_missing_sb),
						      reg_magic == MAGIC_ALL);
				  br = regnode(BRANCH);
				  if (ret == NULL)
				      ret = br;
				  else
				  {
				      regtail(lastnode, br);
				      if (reg_toolong)
					  return NULL;
				  }

				  ungetchr();
				  one_exactly = TRUE;
				  lastnode = regatom(flagp);
				  one_exactly = FALSE;
				  if (lastnode == NULL)
				      return NULL;
			      }
			      if (ret == NULL)
				  EMSG2_RET_NULL(_(e_empty_sb),
						      reg_magic == MAGIC_ALL);
			      lastbranch = regnode(BRANCH);
			      br = regnode(NOTHING);
			      if (ret != JUST_CALC_SIZE)
			      {
				  regtail(lastnode, br);
				  regtail(lastbranch, br);
				  // connect all branches to the NOTHING
				  // branch at the end
				  for (br = ret; br != lastnode; )
				  {
				      if (OP(br) == BRANCH)
				      {
					  regtail(br, lastbranch);
					  if (reg_toolong)
					      return NULL;
					  br = OPERAND(br);
				      }
				      else
					  br = regnext(br);
				  }
			      }
			      *flagp &= ~(HASWIDTH | SIMPLE);
			      break;
			  }

		case 'd':   // %d123 decimal
		case 'o':   // %o123 octal
		case 'x':   // %xab hex 2
		case 'u':   // %uabcd hex 4
		case 'U':   // %U1234abcd hex 8
			  {
			      long i;

			      switch (c)
			      {
				  case 'd': i = getdecchrs(); break;
				  case 'o': i = getoctchrs(); break;
				  case 'x': i = gethexchrs(2); break;
				  case 'u': i = gethexchrs(4); break;
				  case 'U': i = gethexchrs(8); break;
				  default:  i = -1; break;
			      }

			      if (i < 0 || i > INT_MAX)
				  EMSG2_RET_NULL(
					_("E678: Invalid character after %s%%[dxouU]"),
					reg_magic == MAGIC_ALL);
			      if (use_multibytecode(i))
				  ret = regnode(MULTIBYTECODE);
			      else
				  ret = regnode(EXACTLY);
			      if (i == 0)
				  regc(0x0a);
			      else
				  regmbc(i);
			      regc(NUL);
			      *flagp |= HASWIDTH;
			      break;
			  }

		default:
			  if (VIM_ISDIGIT(c) || c == '<' || c == '>'
								 || c == '\'')
			  {
			      long_u	n = 0;
			      int	cmp;

			      cmp = c;
			      if (cmp == '<' || cmp == '>')
				  c = getchr();
			      while (VIM_ISDIGIT(c))
			      {
				  n = n * 10 + (c - '0');
				  c = getchr();
			      }
			      if (c == '\'' && n == 0)
			      {
				  // "\%'m", "\%<'m" and "\%>'m": Mark
				  c = getchr();
				  ret = regnode(RE_MARK);
				  if (ret == JUST_CALC_SIZE)
				      regsize += 2;
				  else
				  {
				      *regcode++ = c;
				      *regcode++ = cmp;
				  }
				  break;
			      }
			      else if (c == 'l' || c == 'c' || c == 'v')
			      {
				  if (c == 'l')
				  {
				      ret = regnode(RE_LNUM);
				      if (save_prev_at_start)
					  at_start = TRUE;
				  }
				  else if (c == 'c')
				      ret = regnode(RE_COL);
				  else
				      ret = regnode(RE_VCOL);
				  if (ret == JUST_CALC_SIZE)
				      regsize += 5;
				  else
				  {
				      // put the number and the optional
				      // comparator after the opcode
				      regcode = re_put_long(regcode, n);
				      *regcode++ = cmp;
				  }
				  break;
			      }
			  }

			  EMSG2_RET_NULL(_("E71: Invalid character after %s%%"),
						      reg_magic == MAGIC_ALL);
	    }
	}
	break;

      case Magic('['):
collection:
	{
	    char_u	*lp;

	    // If there is no matching ']', we assume the '[' is a normal
	    // character.  This makes 'incsearch' and ":help [" work.
	    lp = skip_anyof(regparse);
	    if (*lp == ']')	// there is a matching ']'
	    {
		int	startc = -1;	// > 0 when next '-' is a range
		int	endc;

		// In a character class, different parsing rules apply.
		// Not even \ is special anymore, nothing is.
		if (*regparse == '^')	    // Complement of range.
		{
		    ret = regnode(ANYBUT + extra);
		    regparse++;
		}
		else
		    ret = regnode(ANYOF + extra);

		// At the start ']' and '-' mean the literal character.
		if (*regparse == ']' || *regparse == '-')
		{
		    startc = *regparse;
		    regc(*regparse++);
		}

		while (*regparse != NUL && *regparse != ']')
		{
		    if (*regparse == '-')
		    {
			++regparse;
			// The '-' is not used for a range at the end and
			// after or before a '\n'.
			if (*regparse == ']' || *regparse == NUL
				|| startc == -1
				|| (regparse[0] == '\\' && regparse[1] == 'n'))
			{
			    regc('-');
			    startc = '-';	// [--x] is a range
			}
			else
			{
			    // Also accept "a-[.z.]"
			    endc = 0;
			    if (*regparse == '[')
				endc = get_coll_element(&regparse);
			    if (endc == 0)
			    {
				if (has_mbyte)
				    endc = mb_ptr2char_adv(&regparse);
				else
				    endc = *regparse++;
			    }

			    // Handle \o40, \x20 and \u20AC style sequences
			    if (endc == '\\' && !reg_cpo_lit && !reg_cpo_bsl)
				endc = coll_get_char();

			    if (startc > endc)
				EMSG_RET_NULL(_(e_reverse_range));
			    if (has_mbyte && ((*mb_char2len)(startc) > 1
						 || (*mb_char2len)(endc) > 1))
			    {
				// Limit to a range of 256 chars.
				if (endc > startc + 256)
				    EMSG_RET_NULL(_(e_large_class));
				while (++startc <= endc)
				    regmbc(startc);
			    }
			    else
			    {
#ifdef EBCDIC
				int	alpha_only = FALSE;

				// for alphabetical range skip the gaps
				// 'i'-'j', 'r'-'s', 'I'-'J' and 'R'-'S'.
				if (isalpha(startc) && isalpha(endc))
				    alpha_only = TRUE;
#endif
				while (++startc <= endc)
#ifdef EBCDIC
				    if (!alpha_only || isalpha(startc))
#endif
					regc(startc);
			    }
			    startc = -1;
			}
		    }
		    // Only "\]", "\^", "\]" and "\\" are special in Vi.  Vim
		    // accepts "\t", "\e", etc., but only when the 'l' flag in
		    // 'cpoptions' is not included.
		    // Posix doesn't recognize backslash at all.
		    else if (*regparse == '\\'
			    && !reg_cpo_bsl
			    && (vim_strchr(REGEXP_INRANGE, regparse[1]) != NULL
				|| (!reg_cpo_lit
				    && vim_strchr(REGEXP_ABBR,
						       regparse[1]) != NULL)))
		    {
			regparse++;
			if (*regparse == 'n')
			{
			    // '\n' in range: also match NL
			    if (ret != JUST_CALC_SIZE)
			    {
				// Using \n inside [^] does not change what
				// matches. "[^\n]" is the same as ".".
				if (*ret == ANYOF)
				{
				    *ret = ANYOF + ADD_NL;
				    *flagp |= HASNL;
				}
				// else: must have had a \n already
			    }
			    regparse++;
			    startc = -1;
			}
			else if (*regparse == 'd'
				|| *regparse == 'o'
				|| *regparse == 'x'
				|| *regparse == 'u'
				|| *regparse == 'U')
			{
			    startc = coll_get_char();
			    if (startc == 0)
				regc(0x0a);
			    else
				regmbc(startc);
			}
			else
			{
			    startc = backslash_trans(*regparse++);
			    regc(startc);
			}
		    }
		    else if (*regparse == '[')
		    {
			int c_class;
			int cu;

			c_class = get_char_class(&regparse);
			startc = -1;
			// Characters assumed to be 8 bits!
			switch (c_class)
			{
			    case CLASS_NONE:
				c_class = get_equi_class(&regparse);
				if (c_class != 0)
				{
				    // produce equivalence class
				    reg_equi_class(c_class);
				}
				else if ((c_class =
					    get_coll_element(&regparse)) != 0)
				{
				    // produce a collating element
				    regmbc(c_class);
				}
				else
				{
				    // literal '[', allow [[-x] as a range
				    startc = *regparse++;
				    regc(startc);
				}
				break;
			    case CLASS_ALNUM:
				for (cu = 1; cu < 128; cu++)
				    if (isalnum(cu))
					regmbc(cu);
				break;
			    case CLASS_ALPHA:
				for (cu = 1; cu < 256; cu++)
				    if (isalpha(cu))
					regmbc(cu);
				break;
			    case CLASS_BLANK:
				regc(' ');
				regc('\t');
				break;
			    case CLASS_CNTRL:
				for (cu = 1; cu <= 127; cu++)
				    if (iscntrl(cu))
					regmbc(cu);
				break;
			    case CLASS_DIGIT:
				for (cu = 1; cu <= 127; cu++)
				    if (VIM_ISDIGIT(cu))
					regmbc(cu);
				break;
			    case CLASS_GRAPH:
				for (cu = 1; cu <= 127; cu++)
				    if (isgraph(cu))
					regmbc(cu);
				break;
			    case CLASS_LOWER:
				for (cu = 1; cu <= 255; cu++)
				    if (MB_ISLOWER(cu) && cu != 170
								 && cu != 186)
					regmbc(cu);
				break;
			    case CLASS_PRINT:
				for (cu = 1; cu <= 255; cu++)
				    if (vim_isprintc(cu))
					regmbc(cu);
				break;
			    case CLASS_PUNCT:
				for (cu = 1; cu < 128; cu++)
				    if (ispunct(cu))
					regmbc(cu);
				break;
			    case CLASS_SPACE:
				for (cu = 9; cu <= 13; cu++)
				    regc(cu);
				regc(' ');
				break;
			    case CLASS_UPPER:
				for (cu = 1; cu <= 255; cu++)
				    if (MB_ISUPPER(cu))
					regmbc(cu);
				break;
			    case CLASS_XDIGIT:
				for (cu = 1; cu <= 255; cu++)
				    if (vim_isxdigit(cu))
					regmbc(cu);
				break;
			    case CLASS_TAB:
				regc('\t');
				break;
			    case CLASS_RETURN:
				regc('\r');
				break;
			    case CLASS_BACKSPACE:
				regc('\b');
				break;
			    case CLASS_ESCAPE:
				regc('\033');
				break;
			    case CLASS_IDENT:
				for (cu = 1; cu <= 255; cu++)
				    if (vim_isIDc(cu))
					regmbc(cu);
				break;
			    case CLASS_KEYWORD:
				for (cu = 1; cu <= 255; cu++)
				    if (reg_iswordc(cu))
					regmbc(cu);
				break;
			    case CLASS_FNAME:
				for (cu = 1; cu <= 255; cu++)
				    if (vim_isfilec(cu))
					regmbc(cu);
				break;
			}
		    }
		    else
		    {
			if (has_mbyte)
			{
			    int	len;

			    // produce a multibyte character, including any
			    // following composing characters
			    startc = mb_ptr2char(regparse);
			    len = (*mb_ptr2len)(regparse);
			    if (enc_utf8 && utf_char2len(startc) != len)
				startc = -1;	// composing chars
			    while (--len >= 0)
				regc(*regparse++);
			}
			else
			{
			    startc = *regparse++;
			    regc(startc);
			}
		    }
		}
		regc(NUL);
		prevchr_len = 1;	// last char was the ']'
		if (*regparse != ']')
		    EMSG_RET_NULL(_(e_toomsbra));	// Cannot happen?
		skipchr();	    // let's be friends with the lexer again
		*flagp |= HASWIDTH | SIMPLE;
		break;
	    }
	    else if (reg_strict)
		EMSG2_RET_NULL(_(e_missingbracket), reg_magic > MAGIC_OFF);
	}
	// FALLTHROUGH

      default:
	{
	    int		len;

	    // A multi-byte character is handled as a separate atom if it's
	    // before a multi and when it's a composing char.
	    if (use_multibytecode(c))
	    {
do_multibyte:
		ret = regnode(MULTIBYTECODE);
		regmbc(c);
		*flagp |= HASWIDTH | SIMPLE;
		break;
	    }

	    ret = regnode(EXACTLY);

	    // Append characters as long as:
	    // - there is no following multi, we then need the character in
	    //   front of it as a single character operand
	    // - not running into a Magic character
	    // - "one_exactly" is not set
	    // But always emit at least one character.  Might be a Multi,
	    // e.g., a "[" without matching "]".
	    for (len = 0; c != NUL && (len == 0
			|| (re_multi_type(peekchr()) == NOT_MULTI
			    && !one_exactly
			    && !is_Magic(c))); ++len)
	    {
		c = no_Magic(c);
		if (has_mbyte)
		{
		    regmbc(c);
		    if (enc_utf8)
		    {
			int	l;

			// Need to get composing character too.
			for (;;)
			{
			    l = utf_ptr2len(regparse);
			    if (!UTF_COMPOSINGLIKE(regparse, regparse + l))
				break;
			    regmbc(utf_ptr2char(regparse));
			    skipchr();
			}
		    }
		}
		else
		    regc(c);
		c = getchr();
	    }
	    ungetchr();

	    regc(NUL);
	    *flagp |= HASWIDTH;
	    if (len == 1)
		*flagp |= SIMPLE;
	}
	break;
    }

    return ret;
}

/*
 * Parse something followed by possible [*+=].
 *
 * Note that the branching code sequences used for = and the general cases
 * of * and + are somewhat optimized:  they use the same NOTHING node as
 * both the endmarker for their branch list and the body of the last branch.
 * It might seem that this node could be dispensed with entirely, but the
 * endmarker role is not redundant.
 */
    static char_u *
regpiece(int *flagp)
{
    char_u	    *ret;
    int		    op;
    char_u	    *next;
    int		    flags;
    long	    minval;
    long	    maxval;

    ret = regatom(&flags);
    if (ret == NULL)
	return NULL;

    op = peekchr();
    if (re_multi_type(op) == NOT_MULTI)
    {
	*flagp = flags;
	return ret;
    }
    // default flags
    *flagp = (WORST | SPSTART | (flags & (HASNL | HASLOOKBH)));

    skipchr();
    switch (op)
    {
	case Magic('*'):
	    if (flags & SIMPLE)
		reginsert(STAR, ret);
	    else
	    {
		// Emit x* as (x&|), where & means "self".
		reginsert(BRANCH, ret); // Either x
		regoptail(ret, regnode(BACK));	// and loop
		regoptail(ret, ret);	// back
		regtail(ret, regnode(BRANCH));	// or
		regtail(ret, regnode(NOTHING)); // null.
	    }
	    break;

	case Magic('+'):
	    if (flags & SIMPLE)
		reginsert(PLUS, ret);
	    else
	    {
		// Emit x+ as x(&|), where & means "self".
		next = regnode(BRANCH); // Either
		regtail(ret, next);
		regtail(regnode(BACK), ret);	// loop back
		regtail(next, regnode(BRANCH)); // or
		regtail(ret, regnode(NOTHING)); // null.
	    }
	    *flagp = (WORST | HASWIDTH | (flags & (HASNL | HASLOOKBH)));
	    break;

	case Magic('@'):
	    {
		int	lop = END;
		long	nr;

		nr = getdecchrs();
		switch (no_Magic(getchr()))
		{
		    case '=': lop = MATCH; break;		  // \@=
		    case '!': lop = NOMATCH; break;		  // \@!
		    case '>': lop = SUBPAT; break;		  // \@>
		    case '<': switch (no_Magic(getchr()))
			      {
				  case '=': lop = BEHIND; break;   // \@<=
				  case '!': lop = NOBEHIND; break; // \@<!
			      }
		}
		if (lop == END)
		    EMSG2_RET_NULL(_("E59: invalid character after %s@"),
						      reg_magic == MAGIC_ALL);
		// Look behind must match with behind_pos.
		if (lop == BEHIND || lop == NOBEHIND)
		{
		    regtail(ret, regnode(BHPOS));
		    *flagp |= HASLOOKBH;
		}
		regtail(ret, regnode(END)); // operand ends
		if (lop == BEHIND || lop == NOBEHIND)
		{
		    if (nr < 0)
			nr = 0; // no limit is same as zero limit
		    reginsert_nr(lop, nr, ret);
		}
		else
		    reginsert(lop, ret);
		break;
	    }

	case Magic('?'):
	case Magic('='):
	    // Emit x= as (x|)
	    reginsert(BRANCH, ret);		// Either x
	    regtail(ret, regnode(BRANCH));	// or
	    next = regnode(NOTHING);		// null.
	    regtail(ret, next);
	    regoptail(ret, next);
	    break;

	case Magic('{'):
	    if (!read_limits(&minval, &maxval))
		return NULL;
	    if (flags & SIMPLE)
	    {
		reginsert(BRACE_SIMPLE, ret);
		reginsert_limits(BRACE_LIMITS, minval, maxval, ret);
	    }
	    else
	    {
		if (num_complex_braces >= 10)
		    EMSG2_RET_NULL(_("E60: Too many complex %s{...}s"),
						      reg_magic == MAGIC_ALL);
		reginsert(BRACE_COMPLEX + num_complex_braces, ret);
		regoptail(ret, regnode(BACK));
		regoptail(ret, ret);
		reginsert_limits(BRACE_LIMITS, minval, maxval, ret);
		++num_complex_braces;
	    }
	    if (minval > 0 && maxval > 0)
		*flagp = (HASWIDTH | (flags & (HASNL | HASLOOKBH)));
	    break;
    }
    if (re_multi_type(peekchr()) != NOT_MULTI)
    {
	// Can't have a multi follow a multi.
	if (peekchr() == Magic('*'))
	    EMSG2_RET_NULL(_("E61: Nested %s*"), reg_magic >= MAGIC_ON);
	EMSG3_RET_NULL(_("E62: Nested %s%c"), reg_magic == MAGIC_ALL,
							  no_Magic(peekchr()));
    }

    return ret;
}

/*
 * Parse one alternative of an | or & operator.
 * Implements the concatenation operator.
 */
    static char_u *
regconcat(int *flagp)
{
    char_u	*first = NULL;
    char_u	*chain = NULL;
    char_u	*latest;
    int		flags;
    int		cont = TRUE;

    *flagp = WORST;		// Tentatively.

    while (cont)
    {
	switch (peekchr())
	{
	    case NUL:
	    case Magic('|'):
	    case Magic('&'):
	    case Magic(')'):
			    cont = FALSE;
			    break;
	    case Magic('Z'):
			    regflags |= RF_ICOMBINE;
			    skipchr_keepstart();
			    break;
	    case Magic('c'):
			    regflags |= RF_ICASE;
			    skipchr_keepstart();
			    break;
	    case Magic('C'):
			    regflags |= RF_NOICASE;
			    skipchr_keepstart();
			    break;
	    case Magic('v'):
			    reg_magic = MAGIC_ALL;
			    skipchr_keepstart();
			    curchr = -1;
			    break;
	    case Magic('m'):
			    reg_magic = MAGIC_ON;
			    skipchr_keepstart();
			    curchr = -1;
			    break;
	    case Magic('M'):
			    reg_magic = MAGIC_OFF;
			    skipchr_keepstart();
			    curchr = -1;
			    break;
	    case Magic('V'):
			    reg_magic = MAGIC_NONE;
			    skipchr_keepstart();
			    curchr = -1;
			    break;
	    default:
			    latest = regpiece(&flags);
			    if (latest == NULL || reg_toolong)
				return NULL;
			    *flagp |= flags & (HASWIDTH | HASNL | HASLOOKBH);
			    if (chain == NULL)	// First piece.
				*flagp |= flags & SPSTART;
			    else
				regtail(chain, latest);
			    chain = latest;
			    if (first == NULL)
				first = latest;
			    break;
	}
    }
    if (first == NULL)		// Loop ran zero times.
	first = regnode(NOTHING);
    return first;
}

/*
 * Parse one alternative of an | operator.
 * Implements the & operator.
 */
    static char_u *
regbranch(int *flagp)
{
    char_u	*ret;
    char_u	*chain = NULL;
    char_u	*latest;
    int		flags;

    *flagp = WORST | HASNL;		// Tentatively.

    ret = regnode(BRANCH);
    for (;;)
    {
	latest = regconcat(&flags);
	if (latest == NULL)
	    return NULL;
	// If one of the branches has width, the whole thing has.  If one of
	// the branches anchors at start-of-line, the whole thing does.
	// If one of the branches uses look-behind, the whole thing does.
	*flagp |= flags & (HASWIDTH | SPSTART | HASLOOKBH);
	// If one of the branches doesn't match a line-break, the whole thing
	// doesn't.
	*flagp &= ~HASNL | (flags & HASNL);
	if (chain != NULL)
	    regtail(chain, latest);
	if (peekchr() != Magic('&'))
	    break;
	skipchr();
	regtail(latest, regnode(END)); // operand ends
	if (reg_toolong)
	    break;
	reginsert(MATCH, latest);
	chain = latest;
    }

    return ret;
}

/*
 * Parse regular expression, i.e. main body or parenthesized thing.
 *
 * Caller must absorb opening parenthesis.
 *
 * Combining parenthesis handling with the base level of regular expression
 * is a trifle forced, but the need to tie the tails of the branches to what
 * follows makes it hard to avoid.
 */
    static char_u *
reg(
    int		paren,	// REG_NOPAREN, REG_PAREN, REG_NPAREN or REG_ZPAREN
    int		*flagp)
{
    char_u	*ret;
    char_u	*br;
    char_u	*ender;
    int		parno = 0;
    int		flags;

    *flagp = HASWIDTH;		// Tentatively.

#ifdef FEAT_SYN_HL
    if (paren == REG_ZPAREN)
    {
	// Make a ZOPEN node.
	if (regnzpar >= NSUBEXP)
	    EMSG_RET_NULL(_("E50: Too many \\z("));
	parno = regnzpar;
	regnzpar++;
	ret = regnode(ZOPEN + parno);
    }
    else
#endif
	if (paren == REG_PAREN)
    {
	// Make a MOPEN node.
	if (regnpar >= NSUBEXP)
	    EMSG2_RET_NULL(_("E51: Too many %s("), reg_magic == MAGIC_ALL);
	parno = regnpar;
	++regnpar;
	ret = regnode(MOPEN + parno);
    }
    else if (paren == REG_NPAREN)
    {
	// Make a NOPEN node.
	ret = regnode(NOPEN);
    }
    else
	ret = NULL;

    // Pick up the branches, linking them together.
    br = regbranch(&flags);
    if (br == NULL)
	return NULL;
    if (ret != NULL)
	regtail(ret, br);	// [MZ]OPEN -> first.
    else
	ret = br;
    // If one of the branches can be zero-width, the whole thing can.
    // If one of the branches has * at start or matches a line-break, the
    // whole thing can.
    if (!(flags & HASWIDTH))
	*flagp &= ~HASWIDTH;
    *flagp |= flags & (SPSTART | HASNL | HASLOOKBH);
    while (peekchr() == Magic('|'))
    {
	skipchr();
	br = regbranch(&flags);
	if (br == NULL || reg_toolong)
	    return NULL;
	regtail(ret, br);	// BRANCH -> BRANCH.
	if (!(flags & HASWIDTH))
	    *flagp &= ~HASWIDTH;
	*flagp |= flags & (SPSTART | HASNL | HASLOOKBH);
    }

    // Make a closing node, and hook it on the end.
    ender = regnode(
#ifdef FEAT_SYN_HL
	    paren == REG_ZPAREN ? ZCLOSE + parno :
#endif
	    paren == REG_PAREN ? MCLOSE + parno :
	    paren == REG_NPAREN ? NCLOSE : END);
    regtail(ret, ender);

    // Hook the tails of the branches to the closing node.
    for (br = ret; br != NULL; br = regnext(br))
	regoptail(br, ender);

    // Check for proper termination.
    if (paren != REG_NOPAREN && getchr() != Magic(')'))
    {
#ifdef FEAT_SYN_HL
	if (paren == REG_ZPAREN)
	    EMSG_RET_NULL(_("E52: Unmatched \\z("));
	else
#endif
	    if (paren == REG_NPAREN)
	    EMSG2_RET_NULL(_(e_unmatchedpp), reg_magic == MAGIC_ALL);
	else
	    EMSG2_RET_NULL(_(e_unmatchedp), reg_magic == MAGIC_ALL);
    }
    else if (paren == REG_NOPAREN && peekchr() != NUL)
    {
	if (curchr == Magic(')'))
	    EMSG2_RET_NULL(_(e_unmatchedpar), reg_magic == MAGIC_ALL);
	else
	    EMSG_RET_NULL(_(e_trailing));	// "Can't happen".
	// NOTREACHED
    }
    // Here we set the flag allowing back references to this set of
    // parentheses.
    if (paren == REG_PAREN)
	had_endbrace[parno] = TRUE;	// have seen the close paren
    return ret;
}

/*
 * bt_regcomp() - compile a regular expression into internal code for the
 * traditional back track matcher.
 * Returns the program in allocated space.  Returns NULL for an error.
 *
 * We can't allocate space until we know how big the compiled form will be,
 * but we can't compile it (and thus know how big it is) until we've got a
 * place to put the code.  So we cheat:  we compile it twice, once with code
 * generation turned off and size counting turned on, and once "for real".
 * This also means that we don't allocate space until we are sure that the
 * thing really will compile successfully, and we never have to move the
 * code and thus invalidate pointers into it.  (Note that it has to be in
 * one piece because vim_free() must be able to free it all.)
 *
 * Whether upper/lower case is to be ignored is decided when executing the
 * program, it does not matter here.
 *
 * Beware that the optimization-preparation code in here knows about some
 * of the structure of the compiled regexp.
 * "re_flags": RE_MAGIC and/or RE_STRING.
 */
    static regprog_T *
bt_regcomp(char_u *expr, int re_flags)
{
    bt_regprog_T    *r;
    char_u	*scan;
    char_u	*longest;
    int		len;
    int		flags;

    if (expr == NULL)
	IEMSG_RET_NULL(_(e_null));

    init_class_tab();

    // First pass: determine size, legality.
    regcomp_start(expr, re_flags);
    regcode = JUST_CALC_SIZE;
    regc(REGMAGIC);
    if (reg(REG_NOPAREN, &flags) == NULL)
	return NULL;

    // Allocate space.
    r = alloc(offsetof(bt_regprog_T, program) + regsize);
    if (r == NULL)
	return NULL;
    r->re_in_use = FALSE;

    // Second pass: emit code.
    regcomp_start(expr, re_flags);
    regcode = r->program;
    regc(REGMAGIC);
    if (reg(REG_NOPAREN, &flags) == NULL || reg_toolong)
    {
	vim_free(r);
	if (reg_toolong)
	    EMSG_RET_NULL(_("E339: Pattern too long"));
	return NULL;
    }

    // Dig out information for optimizations.
    r->regstart = NUL;		// Worst-case defaults.
    r->reganch = 0;
    r->regmust = NULL;
    r->regmlen = 0;
    r->regflags = regflags;
    if (flags & HASNL)
	r->regflags |= RF_HASNL;
    if (flags & HASLOOKBH)
	r->regflags |= RF_LOOKBH;
#ifdef FEAT_SYN_HL
    // Remember whether this pattern has any \z specials in it.
    r->reghasz = re_has_z;
#endif
    scan = r->program + 1;	// First BRANCH.
    if (OP(regnext(scan)) == END)   // Only one top-level choice.
    {
	scan = OPERAND(scan);

	// Starting-point info.
	if (OP(scan) == BOL || OP(scan) == RE_BOF)
	{
	    r->reganch++;
	    scan = regnext(scan);
	}

	if (OP(scan) == EXACTLY)
	{
	    if (has_mbyte)
		r->regstart = (*mb_ptr2char)(OPERAND(scan));
	    else
		r->regstart = *OPERAND(scan);
	}
	else if ((OP(scan) == BOW
		    || OP(scan) == EOW
		    || OP(scan) == NOTHING
		    || OP(scan) == MOPEN + 0 || OP(scan) == NOPEN
		    || OP(scan) == MCLOSE + 0 || OP(scan) == NCLOSE)
		 && OP(regnext(scan)) == EXACTLY)
	{
	    if (has_mbyte)
		r->regstart = (*mb_ptr2char)(OPERAND(regnext(scan)));
	    else
		r->regstart = *OPERAND(regnext(scan));
	}

	// If there's something expensive in the r.e., find the longest
	// literal string that must appear and make it the regmust.  Resolve
	// ties in favor of later strings, since the regstart check works
	// with the beginning of the r.e. and avoiding duplication
	// strengthens checking.  Not a strong reason, but sufficient in the
	// absence of others.

	// When the r.e. starts with BOW, it is faster to look for a regmust
	// first. Used a lot for "#" and "*" commands. (Added by mool).
	if ((flags & SPSTART || OP(scan) == BOW || OP(scan) == EOW)
							  && !(flags & HASNL))
	{
	    longest = NULL;
	    len = 0;
	    for (; scan != NULL; scan = regnext(scan))
		if (OP(scan) == EXACTLY && STRLEN(OPERAND(scan)) >= (size_t)len)
		{
		    longest = OPERAND(scan);
		    len = (int)STRLEN(OPERAND(scan));
		}
	    r->regmust = longest;
	    r->regmlen = len;
	}
    }
#ifdef BT_REGEXP_DUMP
    regdump(expr, r);
#endif
    r->engine = &bt_regengine;
    return (regprog_T *)r;
}

#if defined(FEAT_SYN_HL) || defined(PROTO)
/*
 * Check if during the previous call to vim_regcomp the EOL item "$" has been
 * found.  This is messy, but it works fine.
 */
    int
vim_regcomp_had_eol(void)
{
    return had_eol;
}
#endif

/*
 * Get a number after a backslash that is inside [].
 * When nothing is recognized return a backslash.
 */
    static int
coll_get_char(void)
{
    long	nr = -1;

    switch (*regparse++)
    {
	case 'd': nr = getdecchrs(); break;
	case 'o': nr = getoctchrs(); break;
	case 'x': nr = gethexchrs(2); break;
	case 'u': nr = gethexchrs(4); break;
	case 'U': nr = gethexchrs(8); break;
    }
    if (nr < 0 || nr > INT_MAX)
    {
	// If getting the number fails be backwards compatible: the character
	// is a backslash.
	--regparse;
	nr = '\\';
    }
    return nr;
}

/*
 * Free a compiled regexp program, returned by bt_regcomp().
 */
    static void
bt_regfree(regprog_T *prog)
{
    vim_free(prog);
}

#define ADVANCE_REGINPUT() MB_PTR_ADV(rex.input)

/*
 * The arguments from BRACE_LIMITS are stored here.  They are actually local
 * to regmatch(), but they are here to reduce the amount of stack space used
 * (it can be called recursively many times).
 */
static long	bl_minval;
static long	bl_maxval;

/*
 * Save the input line and position in a regsave_T.
 */
    static void
reg_save(regsave_T *save, garray_T *gap)
{
    if (REG_MULTI)
    {
	save->rs_u.pos.col = (colnr_T)(rex.input - rex.line);
	save->rs_u.pos.lnum = rex.lnum;
    }
    else
	save->rs_u.ptr = rex.input;
    save->rs_len = gap->ga_len;
}

/*
 * Restore the input line and position from a regsave_T.
 */
    static void
reg_restore(regsave_T *save, garray_T *gap)
{
    if (REG_MULTI)
    {
	if (rex.lnum != save->rs_u.pos.lnum)
	{
	    // only call reg_getline() when the line number changed to save
	    // a bit of time
	    rex.lnum = save->rs_u.pos.lnum;
	    rex.line = reg_getline(rex.lnum);
	}
	rex.input = rex.line + save->rs_u.pos.col;
    }
    else
	rex.input = save->rs_u.ptr;
    gap->ga_len = save->rs_len;
}

/*
 * Return TRUE if current position is equal to saved position.
 */
    static int
reg_save_equal(regsave_T *save)
{
    if (REG_MULTI)
	return rex.lnum == save->rs_u.pos.lnum
				  && rex.input == rex.line + save->rs_u.pos.col;
    return rex.input == save->rs_u.ptr;
}

// Save the sub-expressions before attempting a match.
#define save_se(savep, posp, pp) \
    REG_MULTI ? save_se_multi((savep), (posp)) : save_se_one((savep), (pp))

// After a failed match restore the sub-expressions.
#define restore_se(savep, posp, pp) { \
    if (REG_MULTI) \
	*(posp) = (savep)->se_u.pos; \
    else \
	*(pp) = (savep)->se_u.ptr; }

/*
 * Tentatively set the sub-expression start to the current position (after
 * calling regmatch() they will have changed).  Need to save the existing
 * values for when there is no match.
 * Use se_save() to use pointer (save_se_multi()) or position (save_se_one()),
 * depending on REG_MULTI.
 */
    static void
save_se_multi(save_se_T *savep, lpos_T *posp)
{
    savep->se_u.pos = *posp;
    posp->lnum = rex.lnum;
    posp->col = (colnr_T)(rex.input - rex.line);
}

    static void
save_se_one(save_se_T *savep, char_u **pp)
{
    savep->se_u.ptr = *pp;
    *pp = rex.input;
}

/*
 * regrepeat - repeatedly match something simple, return how many.
 * Advances rex.input (and rex.lnum) to just after the matched chars.
 */
    static int
regrepeat(
    char_u	*p,
    long	maxcount)   // maximum number of matches allowed
{
    long	count = 0;
    char_u	*scan;
    char_u	*opnd;
    int		mask;
    int		testval = 0;

    scan = rex.input;	    // Make local copy of rex.input for speed.
    opnd = OPERAND(p);
    switch (OP(p))
    {
      case ANY:
      case ANY + ADD_NL:
	while (count < maxcount)
	{
	    // Matching anything means we continue until end-of-line (or
	    // end-of-file for ANY + ADD_NL), only limited by maxcount.
	    while (*scan != NUL && count < maxcount)
	    {
		++count;
		MB_PTR_ADV(scan);
	    }
	    if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
				      || rex.reg_line_lbr || count == maxcount)
		break;
	    ++count;		// count the line-break
	    reg_nextline();
	    scan = rex.input;
	    if (got_int)
		break;
	}
	break;

      case IDENT:
      case IDENT + ADD_NL:
	testval = TRUE;
	// FALLTHROUGH
      case SIDENT:
      case SIDENT + ADD_NL:
	while (count < maxcount)
	{
	    if (vim_isIDc(PTR2CHAR(scan)) && (testval || !VIM_ISDIGIT(*scan)))
	    {
		MB_PTR_ADV(scan);
	    }
	    else if (*scan == NUL)
	    {
		if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
							   || rex.reg_line_lbr)
		    break;
		reg_nextline();
		scan = rex.input;
		if (got_int)
		    break;
	    }
	    else if (rex.reg_line_lbr && *scan == '\n' && WITH_NL(OP(p)))
		++scan;
	    else
		break;
	    ++count;
	}
	break;

      case KWORD:
      case KWORD + ADD_NL:
	testval = TRUE;
	// FALLTHROUGH
      case SKWORD:
      case SKWORD + ADD_NL:
	while (count < maxcount)
	{
	    if (vim_iswordp_buf(scan, rex.reg_buf)
					  && (testval || !VIM_ISDIGIT(*scan)))
	    {
		MB_PTR_ADV(scan);
	    }
	    else if (*scan == NUL)
	    {
		if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
							   || rex.reg_line_lbr)
		    break;
		reg_nextline();
		scan = rex.input;
		if (got_int)
		    break;
	    }
	    else if (rex.reg_line_lbr && *scan == '\n' && WITH_NL(OP(p)))
		++scan;
	    else
		break;
	    ++count;
	}
	break;

      case FNAME:
      case FNAME + ADD_NL:
	testval = TRUE;
	// FALLTHROUGH
      case SFNAME:
      case SFNAME + ADD_NL:
	while (count < maxcount)
	{
	    if (vim_isfilec(PTR2CHAR(scan)) && (testval || !VIM_ISDIGIT(*scan)))
	    {
		MB_PTR_ADV(scan);
	    }
	    else if (*scan == NUL)
	    {
		if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
							   || rex.reg_line_lbr)
		    break;
		reg_nextline();
		scan = rex.input;
		if (got_int)
		    break;
	    }
	    else if (rex.reg_line_lbr && *scan == '\n' && WITH_NL(OP(p)))
		++scan;
	    else
		break;
	    ++count;
	}
	break;

      case PRINT:
      case PRINT + ADD_NL:
	testval = TRUE;
	// FALLTHROUGH
      case SPRINT:
      case SPRINT + ADD_NL:
	while (count < maxcount)
	{
	    if (*scan == NUL)
	    {
		if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
							   || rex.reg_line_lbr)
		    break;
		reg_nextline();
		scan = rex.input;
		if (got_int)
		    break;
	    }
	    else if (vim_isprintc(PTR2CHAR(scan)) == 1
					  && (testval || !VIM_ISDIGIT(*scan)))
	    {
		MB_PTR_ADV(scan);
	    }
	    else if (rex.reg_line_lbr && *scan == '\n' && WITH_NL(OP(p)))
		++scan;
	    else
		break;
	    ++count;
	}
	break;

      case WHITE:
      case WHITE + ADD_NL:
	testval = mask = RI_WHITE;
do_class:
	while (count < maxcount)
	{
	    int		l;

	    if (*scan == NUL)
	    {
		if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
							   || rex.reg_line_lbr)
		    break;
		reg_nextline();
		scan = rex.input;
		if (got_int)
		    break;
	    }
	    else if (has_mbyte && (l = (*mb_ptr2len)(scan)) > 1)
	    {
		if (testval != 0)
		    break;
		scan += l;
	    }
	    else if ((class_tab[*scan] & mask) == testval)
		++scan;
	    else if (rex.reg_line_lbr && *scan == '\n' && WITH_NL(OP(p)))
		++scan;
	    else
		break;
	    ++count;
	}
	break;

      case NWHITE:
      case NWHITE + ADD_NL:
	mask = RI_WHITE;
	goto do_class;
      case DIGIT:
      case DIGIT + ADD_NL:
	testval = mask = RI_DIGIT;
	goto do_class;
      case NDIGIT:
      case NDIGIT + ADD_NL:
	mask = RI_DIGIT;
	goto do_class;
      case HEX:
      case HEX + ADD_NL:
	testval = mask = RI_HEX;
	goto do_class;
      case NHEX:
      case NHEX + ADD_NL:
	mask = RI_HEX;
	goto do_class;
      case OCTAL:
      case OCTAL + ADD_NL:
	testval = mask = RI_OCTAL;
	goto do_class;
      case NOCTAL:
      case NOCTAL + ADD_NL:
	mask = RI_OCTAL;
	goto do_class;
      case WORD:
      case WORD + ADD_NL:
	testval = mask = RI_WORD;
	goto do_class;
      case NWORD:
      case NWORD + ADD_NL:
	mask = RI_WORD;
	goto do_class;
      case HEAD:
      case HEAD + ADD_NL:
	testval = mask = RI_HEAD;
	goto do_class;
      case NHEAD:
      case NHEAD + ADD_NL:
	mask = RI_HEAD;
	goto do_class;
      case ALPHA:
      case ALPHA + ADD_NL:
	testval = mask = RI_ALPHA;
	goto do_class;
      case NALPHA:
      case NALPHA + ADD_NL:
	mask = RI_ALPHA;
	goto do_class;
      case LOWER:
      case LOWER + ADD_NL:
	testval = mask = RI_LOWER;
	goto do_class;
      case NLOWER:
      case NLOWER + ADD_NL:
	mask = RI_LOWER;
	goto do_class;
      case UPPER:
      case UPPER + ADD_NL:
	testval = mask = RI_UPPER;
	goto do_class;
      case NUPPER:
      case NUPPER + ADD_NL:
	mask = RI_UPPER;
	goto do_class;

      case EXACTLY:
	{
	    int	    cu, cl;

	    // This doesn't do a multi-byte character, because a MULTIBYTECODE
	    // would have been used for it.  It does handle single-byte
	    // characters, such as latin1.
	    if (rex.reg_ic)
	    {
		cu = MB_TOUPPER(*opnd);
		cl = MB_TOLOWER(*opnd);
		while (count < maxcount && (*scan == cu || *scan == cl))
		{
		    count++;
		    scan++;
		}
	    }
	    else
	    {
		cu = *opnd;
		while (count < maxcount && *scan == cu)
		{
		    count++;
		    scan++;
		}
	    }
	    break;
	}

      case MULTIBYTECODE:
	{
	    int		i, len, cf = 0;

	    // Safety check (just in case 'encoding' was changed since
	    // compiling the program).
	    if ((len = (*mb_ptr2len)(opnd)) > 1)
	    {
		if (rex.reg_ic && enc_utf8)
		    cf = utf_fold(utf_ptr2char(opnd));
		while (count < maxcount && (*mb_ptr2len)(scan) >= len)
		{
		    for (i = 0; i < len; ++i)
			if (opnd[i] != scan[i])
			    break;
		    if (i < len && (!rex.reg_ic || !enc_utf8
					|| utf_fold(utf_ptr2char(scan)) != cf))
			break;
		    scan += len;
		    ++count;
		}
	    }
	}
	break;

      case ANYOF:
      case ANYOF + ADD_NL:
	testval = TRUE;
	// FALLTHROUGH

      case ANYBUT:
      case ANYBUT + ADD_NL:
	while (count < maxcount)
	{
	    int len;

	    if (*scan == NUL)
	    {
		if (!REG_MULTI || !WITH_NL(OP(p)) || rex.lnum > rex.reg_maxline
							   || rex.reg_line_lbr)
		    break;
		reg_nextline();
		scan = rex.input;
		if (got_int)
		    break;
	    }
	    else if (rex.reg_line_lbr && *scan == '\n' && WITH_NL(OP(p)))
		++scan;
	    else if (has_mbyte && (len = (*mb_ptr2len)(scan)) > 1)
	    {
		if ((cstrchr(opnd, (*mb_ptr2char)(scan)) == NULL) == testval)
		    break;
		scan += len;
	    }
	    else
	    {
		if ((cstrchr(opnd, *scan) == NULL) == testval)
		    break;
		++scan;
	    }
	    ++count;
	}
	break;

      case NEWL:
	while (count < maxcount
		&& ((*scan == NUL && rex.lnum <= rex.reg_maxline
				       && !rex.reg_line_lbr && REG_MULTI)
		    || (*scan == '\n' && rex.reg_line_lbr)))
	{
	    count++;
	    if (rex.reg_line_lbr)
		ADVANCE_REGINPUT();
	    else
		reg_nextline();
	    scan = rex.input;
	    if (got_int)
		break;
	}
	break;

      default:			// Oh dear.  Called inappropriately.
	iemsg(_(e_re_corr));
#ifdef DEBUG
	printf("Called regrepeat with op code %d\n", OP(p));
#endif
	break;
    }

    rex.input = scan;

    return (int)count;
}

/*
 * Push an item onto the regstack.
 * Returns pointer to new item.  Returns NULL when out of memory.
 */
    static regitem_T *
regstack_push(regstate_T state, char_u *scan)
{
    regitem_T	*rp;

    if ((long)((unsigned)regstack.ga_len >> 10) >= p_mmp)
    {
	emsg(_(e_maxmempat));
	return NULL;
    }
    if (ga_grow(&regstack, sizeof(regitem_T)) == FAIL)
	return NULL;

    rp = (regitem_T *)((char *)regstack.ga_data + regstack.ga_len);
    rp->rs_state = state;
    rp->rs_scan = scan;

    regstack.ga_len += sizeof(regitem_T);
    return rp;
}

/*
 * Pop an item from the regstack.
 */
    static void
regstack_pop(char_u **scan)
{
    regitem_T	*rp;

    rp = (regitem_T *)((char *)regstack.ga_data + regstack.ga_len) - 1;
    *scan = rp->rs_scan;

    regstack.ga_len -= sizeof(regitem_T);
}

/*
 * Save the current subexpr to "bp", so that they can be restored
 * later by restore_subexpr().
 */
    static void
save_subexpr(regbehind_T *bp)
{
    int i;

    // When "rex.need_clear_subexpr" is set we don't need to save the values,
    // only remember that this flag needs to be set again when restoring.
    bp->save_need_clear_subexpr = rex.need_clear_subexpr;
    if (!rex.need_clear_subexpr)
    {
	for (i = 0; i < NSUBEXP; ++i)
	{
	    if (REG_MULTI)
	    {
		bp->save_start[i].se_u.pos = rex.reg_startpos[i];
		bp->save_end[i].se_u.pos = rex.reg_endpos[i];
	    }
	    else
	    {
		bp->save_start[i].se_u.ptr = rex.reg_startp[i];
		bp->save_end[i].se_u.ptr = rex.reg_endp[i];
	    }
	}
    }
}

/*
 * Restore the subexpr from "bp".
 */
    static void
restore_subexpr(regbehind_T *bp)
{
    int i;

    // Only need to restore saved values when they are not to be cleared.
    rex.need_clear_subexpr = bp->save_need_clear_subexpr;
    if (!rex.need_clear_subexpr)
    {
	for (i = 0; i < NSUBEXP; ++i)
	{
	    if (REG_MULTI)
	    {
		rex.reg_startpos[i] = bp->save_start[i].se_u.pos;
		rex.reg_endpos[i] = bp->save_end[i].se_u.pos;
	    }
	    else
	    {
		rex.reg_startp[i] = bp->save_start[i].se_u.ptr;
		rex.reg_endp[i] = bp->save_end[i].se_u.ptr;
	    }
	}
    }
}

/*
 * regmatch - main matching routine
 *
 * Conceptually the strategy is simple: Check to see whether the current node
 * matches, push an item onto the regstack and loop to see whether the rest
 * matches, and then act accordingly.  In practice we make some effort to
 * avoid using the regstack, in particular by going through "ordinary" nodes
 * (that don't need to know whether the rest of the match failed) by a nested
 * loop.
 *
 * Returns TRUE when there is a match.  Leaves rex.input and rex.lnum just after
 * the last matched character.
 * Returns FALSE when there is no match.  Leaves rex.input and rex.lnum in an
 * undefined state!
 */
    static int
regmatch(
    char_u	*scan,		    // Current node.
    proftime_T	*tm UNUSED,	    // timeout limit or NULL
    int		*timed_out UNUSED)  // flag set on timeout or NULL
{
  char_u	*next;		// Next node.
  int		op;
  int		c;
  regitem_T	*rp;
  int		no;
  int		status;		// one of the RA_ values:
#ifdef FEAT_RELTIME
  int		tm_count = 0;
#endif

  // Make "regstack" and "backpos" empty.  They are allocated and freed in
  // bt_regexec_both() to reduce malloc()/free() calls.
  regstack.ga_len = 0;
  backpos.ga_len = 0;

  // Repeat until "regstack" is empty.
  for (;;)
  {
    // Some patterns may take a long time to match, e.g., "\([a-z]\+\)\+Q".
    // Allow interrupting them with CTRL-C.
    fast_breakcheck();

#ifdef DEBUG
    if (scan != NULL && regnarrate)
    {
	mch_errmsg((char *)regprop(scan));
	mch_errmsg("(\n");
    }
#endif

    // Repeat for items that can be matched sequentially, without using the
    // regstack.
    for (;;)
    {
	if (got_int || scan == NULL)
	{
	    status = RA_FAIL;
	    break;
	}
#ifdef FEAT_RELTIME
	// Check for timeout once in a 100 times to avoid overhead.
	if (tm != NULL && ++tm_count == 100)
	{
	    tm_count = 0;
	    if (profile_passed_limit(tm))
	    {
		if (timed_out != NULL)
		    *timed_out = TRUE;
		status = RA_FAIL;
		break;
	    }
	}
#endif
	status = RA_CONT;

#ifdef DEBUG
	if (regnarrate)
	{
	    mch_errmsg((char *)regprop(scan));
	    mch_errmsg("...\n");
# ifdef FEAT_SYN_HL
	    if (re_extmatch_in != NULL)
	    {
		int i;

		mch_errmsg(_("External submatches:\n"));
		for (i = 0; i < NSUBEXP; i++)
		{
		    mch_errmsg("    \"");
		    if (re_extmatch_in->matches[i] != NULL)
			mch_errmsg((char *)re_extmatch_in->matches[i]);
		    mch_errmsg("\"\n");
		}
	    }
# endif
	}
#endif
	next = regnext(scan);

	op = OP(scan);
	// Check for character class with NL added.
	if (!rex.reg_line_lbr && WITH_NL(op) && REG_MULTI
			     && *rex.input == NUL && rex.lnum <= rex.reg_maxline)
	{
	    reg_nextline();
	}
	else if (rex.reg_line_lbr && WITH_NL(op) && *rex.input == '\n')
	{
	    ADVANCE_REGINPUT();
	}
	else
	{
	  if (WITH_NL(op))
	      op -= ADD_NL;
	  if (has_mbyte)
	      c = (*mb_ptr2char)(rex.input);
	  else
	      c = *rex.input;
	  switch (op)
	  {
	  case BOL:
	    if (rex.input != rex.line)
		status = RA_NOMATCH;
	    break;

	  case EOL:
	    if (c != NUL)
		status = RA_NOMATCH;
	    break;

	  case RE_BOF:
	    // We're not at the beginning of the file when below the first
	    // line where we started, not at the start of the line or we
	    // didn't start at the first line of the buffer.
	    if (rex.lnum != 0 || rex.input != rex.line
				       || (REG_MULTI && rex.reg_firstlnum > 1))
		status = RA_NOMATCH;
	    break;

	  case RE_EOF:
	    if (rex.lnum != rex.reg_maxline || c != NUL)
		status = RA_NOMATCH;
	    break;

	  case CURSOR:
	    // Check if the buffer is in a window and compare the
	    // rex.reg_win->w_cursor position to the match position.
	    if (rex.reg_win == NULL
		    || (rex.lnum + rex.reg_firstlnum
						 != rex.reg_win->w_cursor.lnum)
		    || ((colnr_T)(rex.input - rex.line)
						 != rex.reg_win->w_cursor.col))
		status = RA_NOMATCH;
	    break;

	  case RE_MARK:
	    // Compare the mark position to the match position.
	    {
		int	mark = OPERAND(scan)[0];
		int	cmp = OPERAND(scan)[1];
		pos_T	*pos;

		pos = getmark_buf(rex.reg_buf, mark, FALSE);
		if (pos == NULL		     // mark doesn't exist
			|| pos->lnum <= 0    // mark isn't set in reg_buf
			|| (pos->lnum == rex.lnum + rex.reg_firstlnum
				? (pos->col == (colnr_T)(rex.input - rex.line)
				    ? (cmp == '<' || cmp == '>')
				    : (pos->col < (colnr_T)(rex.input - rex.line)
					? cmp != '>'
					: cmp != '<'))
				: (pos->lnum < rex.lnum + rex.reg_firstlnum
				    ? cmp != '>'
				    : cmp != '<')))
		    status = RA_NOMATCH;
	    }
	    break;

	  case RE_VISUAL:
	    if (!reg_match_visual())
		status = RA_NOMATCH;
	    break;

	  case RE_LNUM:
	    if (!REG_MULTI || !re_num_cmp((long_u)(rex.lnum + rex.reg_firstlnum),
									scan))
		status = RA_NOMATCH;
	    break;

	  case RE_COL:
	    if (!re_num_cmp((long_u)(rex.input - rex.line) + 1, scan))
		status = RA_NOMATCH;
	    break;

	  case RE_VCOL:
	    if (!re_num_cmp((long_u)win_linetabsize(
			    rex.reg_win == NULL ? curwin : rex.reg_win,
			    rex.line, (colnr_T)(rex.input - rex.line)) + 1, scan))
		status = RA_NOMATCH;
	    break;

	  case BOW:	// \<word; rex.input points to w
	    if (c == NUL)	// Can't match at end of line
		status = RA_NOMATCH;
	    else if (has_mbyte)
	    {
		int this_class;

		// Get class of current and previous char (if it exists).
		this_class = mb_get_class_buf(rex.input, rex.reg_buf);
		if (this_class <= 1)
		    status = RA_NOMATCH;  // not on a word at all
		else if (reg_prev_class() == this_class)
		    status = RA_NOMATCH;  // previous char is in same word
	    }
	    else
	    {
		if (!vim_iswordc_buf(c, rex.reg_buf) || (rex.input > rex.line
				&& vim_iswordc_buf(rex.input[-1], rex.reg_buf)))
		    status = RA_NOMATCH;
	    }
	    break;

	  case EOW:	// word\>; rex.input points after d
	    if (rex.input == rex.line)    // Can't match at start of line
		status = RA_NOMATCH;
	    else if (has_mbyte)
	    {
		int this_class, prev_class;

		// Get class of current and previous char (if it exists).
		this_class = mb_get_class_buf(rex.input, rex.reg_buf);
		prev_class = reg_prev_class();
		if (this_class == prev_class
			|| prev_class == 0 || prev_class == 1)
		    status = RA_NOMATCH;
	    }
	    else
	    {
		if (!vim_iswordc_buf(rex.input[-1], rex.reg_buf)
			|| (rex.input[0] != NUL
					   && vim_iswordc_buf(c, rex.reg_buf)))
		    status = RA_NOMATCH;
	    }
	    break; // Matched with EOW

	  case ANY:
	    // ANY does not match new lines.
	    if (c == NUL)
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case IDENT:
	    if (!vim_isIDc(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case SIDENT:
	    if (VIM_ISDIGIT(*rex.input) || !vim_isIDc(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case KWORD:
	    if (!vim_iswordp_buf(rex.input, rex.reg_buf))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case SKWORD:
	    if (VIM_ISDIGIT(*rex.input)
				    || !vim_iswordp_buf(rex.input, rex.reg_buf))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case FNAME:
	    if (!vim_isfilec(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case SFNAME:
	    if (VIM_ISDIGIT(*rex.input) || !vim_isfilec(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case PRINT:
	    if (!vim_isprintc(PTR2CHAR(rex.input)))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case SPRINT:
	    if (VIM_ISDIGIT(*rex.input) || !vim_isprintc(PTR2CHAR(rex.input)))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case WHITE:
	    if (!VIM_ISWHITE(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NWHITE:
	    if (c == NUL || VIM_ISWHITE(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case DIGIT:
	    if (!ri_digit(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NDIGIT:
	    if (c == NUL || ri_digit(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case HEX:
	    if (!ri_hex(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NHEX:
	    if (c == NUL || ri_hex(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case OCTAL:
	    if (!ri_octal(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NOCTAL:
	    if (c == NUL || ri_octal(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case WORD:
	    if (!ri_word(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NWORD:
	    if (c == NUL || ri_word(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case HEAD:
	    if (!ri_head(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NHEAD:
	    if (c == NUL || ri_head(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case ALPHA:
	    if (!ri_alpha(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NALPHA:
	    if (c == NUL || ri_alpha(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case LOWER:
	    if (!ri_lower(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NLOWER:
	    if (c == NUL || ri_lower(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case UPPER:
	    if (!ri_upper(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case NUPPER:
	    if (c == NUL || ri_upper(c))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case EXACTLY:
	    {
		int	len;
		char_u	*opnd;

		opnd = OPERAND(scan);
		// Inline the first byte, for speed.
		if (*opnd != *rex.input
			&& (!rex.reg_ic
			    || (!enc_utf8
			      && MB_TOLOWER(*opnd) != MB_TOLOWER(*rex.input))))
		    status = RA_NOMATCH;
		else if (*opnd == NUL)
		{
		    // match empty string always works; happens when "~" is
		    // empty.
		}
		else
		{
		    if (opnd[1] == NUL && !(enc_utf8 && rex.reg_ic))
		    {
			len = 1;	// matched a single byte above
		    }
		    else
		    {
			// Need to match first byte again for multi-byte.
			len = (int)STRLEN(opnd);
			if (cstrncmp(opnd, rex.input, &len) != 0)
			    status = RA_NOMATCH;
		    }
		    // Check for following composing character, unless %C
		    // follows (skips over all composing chars).
		    if (status != RA_NOMATCH
			    && enc_utf8
			    && UTF_COMPOSINGLIKE(rex.input, rex.input + len)
			    && !rex.reg_icombine
			    && OP(next) != RE_COMPOSING)
		    {
			// raaron: This code makes a composing character get
			// ignored, which is the correct behavior (sometimes)
			// for voweled Hebrew texts.
			status = RA_NOMATCH;
		    }
		    if (status != RA_NOMATCH)
			rex.input += len;
		}
	    }
	    break;

	  case ANYOF:
	  case ANYBUT:
	    if (c == NUL)
		status = RA_NOMATCH;
	    else if ((cstrchr(OPERAND(scan), c) == NULL) == (op == ANYOF))
		status = RA_NOMATCH;
	    else
		ADVANCE_REGINPUT();
	    break;

	  case MULTIBYTECODE:
	    if (has_mbyte)
	    {
		int	i, len;
		char_u	*opnd;
		int	opndc = 0, inpc;

		opnd = OPERAND(scan);
		// Safety check (just in case 'encoding' was changed since
		// compiling the program).
		if ((len = (*mb_ptr2len)(opnd)) < 2)
		{
		    status = RA_NOMATCH;
		    break;
		}
		if (enc_utf8)
		    opndc = utf_ptr2char(opnd);
		if (enc_utf8 && utf_iscomposing(opndc))
		{
		    // When only a composing char is given match at any
		    // position where that composing char appears.
		    status = RA_NOMATCH;
		    for (i = 0; rex.input[i] != NUL;
						i += utf_ptr2len(rex.input + i))
		    {
			inpc = utf_ptr2char(rex.input + i);
			if (!utf_iscomposing(inpc))
			{
			    if (i > 0)
				break;
			}
			else if (opndc == inpc)
			{
			    // Include all following composing chars.
			    len = i + utfc_ptr2len(rex.input + i);
			    status = RA_MATCH;
			    break;
			}
		    }
		}
		else
		    for (i = 0; i < len; ++i)
			if (opnd[i] != rex.input[i])
			{
			    status = RA_NOMATCH;
			    break;
			}
		rex.input += len;
	    }
	    else
		status = RA_NOMATCH;
	    break;
	  case RE_COMPOSING:
	    if (enc_utf8)
	    {
		// Skip composing characters.
		while (utf_iscomposing(utf_ptr2char(rex.input)))
		    MB_CPTR_ADV(rex.input);
	    }
	    break;

	  case NOTHING:
	    break;

	  case BACK:
	    {
		int		i;
		backpos_T	*bp;

		// When we run into BACK we need to check if we don't keep
		// looping without matching any input.  The second and later
		// times a BACK is encountered it fails if the input is still
		// at the same position as the previous time.
		// The positions are stored in "backpos" and found by the
		// current value of "scan", the position in the RE program.
		bp = (backpos_T *)backpos.ga_data;
		for (i = 0; i < backpos.ga_len; ++i)
		    if (bp[i].bp_scan == scan)
			break;
		if (i == backpos.ga_len)
		{
		    // First time at this BACK, make room to store the pos.
		    if (ga_grow(&backpos, 1) == FAIL)
			status = RA_FAIL;
		    else
		    {
			// get "ga_data" again, it may have changed
			bp = (backpos_T *)backpos.ga_data;
			bp[i].bp_scan = scan;
			++backpos.ga_len;
		    }
		}
		else if (reg_save_equal(&bp[i].bp_pos))
		    // Still at same position as last time, fail.
		    status = RA_NOMATCH;

		if (status != RA_FAIL && status != RA_NOMATCH)
		    reg_save(&bp[i].bp_pos, &backpos);
	    }
	    break;

	  case MOPEN + 0:   // Match start: \zs
	  case MOPEN + 1:   // \(
	  case MOPEN + 2:
	  case MOPEN + 3:
	  case MOPEN + 4:
	  case MOPEN + 5:
	  case MOPEN + 6:
	  case MOPEN + 7:
	  case MOPEN + 8:
	  case MOPEN + 9:
	    {
		no = op - MOPEN;
		cleanup_subexpr();
		rp = regstack_push(RS_MOPEN, scan);
		if (rp == NULL)
		    status = RA_FAIL;
		else
		{
		    rp->rs_no = no;
		    save_se(&rp->rs_un.sesave, &rex.reg_startpos[no],
							  &rex.reg_startp[no]);
		    // We simply continue and handle the result when done.
		}
	    }
	    break;

	  case NOPEN:	    // \%(
	  case NCLOSE:	    // \) after \%(
		if (regstack_push(RS_NOPEN, scan) == NULL)
		    status = RA_FAIL;
		// We simply continue and handle the result when done.
		break;

#ifdef FEAT_SYN_HL
	  case ZOPEN + 1:
	  case ZOPEN + 2:
	  case ZOPEN + 3:
	  case ZOPEN + 4:
	  case ZOPEN + 5:
	  case ZOPEN + 6:
	  case ZOPEN + 7:
	  case ZOPEN + 8:
	  case ZOPEN + 9:
	    {
		no = op - ZOPEN;
		cleanup_zsubexpr();
		rp = regstack_push(RS_ZOPEN, scan);
		if (rp == NULL)
		    status = RA_FAIL;
		else
		{
		    rp->rs_no = no;
		    save_se(&rp->rs_un.sesave, &reg_startzpos[no],
							     &reg_startzp[no]);
		    // We simply continue and handle the result when done.
		}
	    }
	    break;
#endif

	  case MCLOSE + 0:  // Match end: \ze
	  case MCLOSE + 1:  // \)
	  case MCLOSE + 2:
	  case MCLOSE + 3:
	  case MCLOSE + 4:
	  case MCLOSE + 5:
	  case MCLOSE + 6:
	  case MCLOSE + 7:
	  case MCLOSE + 8:
	  case MCLOSE + 9:
	    {
		no = op - MCLOSE;
		cleanup_subexpr();
		rp = regstack_push(RS_MCLOSE, scan);
		if (rp == NULL)
		    status = RA_FAIL;
		else
		{
		    rp->rs_no = no;
		    save_se(&rp->rs_un.sesave, &rex.reg_endpos[no],
							    &rex.reg_endp[no]);
		    // We simply continue and handle the result when done.
		}
	    }
	    break;

#ifdef FEAT_SYN_HL
	  case ZCLOSE + 1:  // \) after \z(
	  case ZCLOSE + 2:
	  case ZCLOSE + 3:
	  case ZCLOSE + 4:
	  case ZCLOSE + 5:
	  case ZCLOSE + 6:
	  case ZCLOSE + 7:
	  case ZCLOSE + 8:
	  case ZCLOSE + 9:
	    {
		no = op - ZCLOSE;
		cleanup_zsubexpr();
		rp = regstack_push(RS_ZCLOSE, scan);
		if (rp == NULL)
		    status = RA_FAIL;
		else
		{
		    rp->rs_no = no;
		    save_se(&rp->rs_un.sesave, &reg_endzpos[no],
							      &reg_endzp[no]);
		    // We simply continue and handle the result when done.
		}
	    }
	    break;
#endif

	  case BACKREF + 1:
	  case BACKREF + 2:
	  case BACKREF + 3:
	  case BACKREF + 4:
	  case BACKREF + 5:
	  case BACKREF + 6:
	  case BACKREF + 7:
	  case BACKREF + 8:
	  case BACKREF + 9:
	    {
		int		len;

		no = op - BACKREF;
		cleanup_subexpr();
		if (!REG_MULTI)		// Single-line regexp
		{
		    if (rex.reg_startp[no] == NULL || rex.reg_endp[no] == NULL)
		    {
			// Backref was not set: Match an empty string.
			len = 0;
		    }
		    else
		    {
			// Compare current input with back-ref in the same
			// line.
			len = (int)(rex.reg_endp[no] - rex.reg_startp[no]);
			if (cstrncmp(rex.reg_startp[no], rex.input, &len) != 0)
			    status = RA_NOMATCH;
		    }
		}
		else				// Multi-line regexp
		{
		    if (rex.reg_startpos[no].lnum < 0
						|| rex.reg_endpos[no].lnum < 0)
		    {
			// Backref was not set: Match an empty string.
			len = 0;
		    }
		    else
		    {
			if (rex.reg_startpos[no].lnum == rex.lnum
				&& rex.reg_endpos[no].lnum == rex.lnum)
			{
			    // Compare back-ref within the current line.
			    len = rex.reg_endpos[no].col
						    - rex.reg_startpos[no].col;
			    if (cstrncmp(rex.line + rex.reg_startpos[no].col,
							  rex.input, &len) != 0)
				status = RA_NOMATCH;
			}
			else
			{
			    // Messy situation: Need to compare between two
			    // lines.
			    int r = match_with_backref(
					    rex.reg_startpos[no].lnum,
					    rex.reg_startpos[no].col,
					    rex.reg_endpos[no].lnum,
					    rex.reg_endpos[no].col,
					    &len);

			    if (r != RA_MATCH)
				status = r;
			}
		    }
		}

		// Matched the backref, skip over it.
		rex.input += len;
	    }
	    break;

#ifdef FEAT_SYN_HL
	  case ZREF + 1:
	  case ZREF + 2:
	  case ZREF + 3:
	  case ZREF + 4:
	  case ZREF + 5:
	  case ZREF + 6:
	  case ZREF + 7:
	  case ZREF + 8:
	  case ZREF + 9:
	    {
		int	len;

		cleanup_zsubexpr();
		no = op - ZREF;
		if (re_extmatch_in != NULL
			&& re_extmatch_in->matches[no] != NULL)
		{
		    len = (int)STRLEN(re_extmatch_in->matches[no]);
		    if (cstrncmp(re_extmatch_in->matches[no],
							  rex.input, &len) != 0)
			status = RA_NOMATCH;
		    else
			rex.input += len;
		}
		else
		{
		    // Backref was not set: Match an empty string.
		}
	    }
	    break;
#endif

	  case BRANCH:
	    {
		if (OP(next) != BRANCH) // No choice.
		    next = OPERAND(scan);	// Avoid recursion.
		else
		{
		    rp = regstack_push(RS_BRANCH, scan);
		    if (rp == NULL)
			status = RA_FAIL;
		    else
			status = RA_BREAK;	// rest is below
		}
	    }
	    break;

	  case BRACE_LIMITS:
	    {
		if (OP(next) == BRACE_SIMPLE)
		{
		    bl_minval = OPERAND_MIN(scan);
		    bl_maxval = OPERAND_MAX(scan);
		}
		else if (OP(next) >= BRACE_COMPLEX
			&& OP(next) < BRACE_COMPLEX + 10)
		{
		    no = OP(next) - BRACE_COMPLEX;
		    brace_min[no] = OPERAND_MIN(scan);
		    brace_max[no] = OPERAND_MAX(scan);
		    brace_count[no] = 0;
		}
		else
		{
		    internal_error("BRACE_LIMITS");
		    status = RA_FAIL;
		}
	    }
	    break;

	  case BRACE_COMPLEX + 0:
	  case BRACE_COMPLEX + 1:
	  case BRACE_COMPLEX + 2:
	  case BRACE_COMPLEX + 3:
	  case BRACE_COMPLEX + 4:
	  case BRACE_COMPLEX + 5:
	  case BRACE_COMPLEX + 6:
	  case BRACE_COMPLEX + 7:
	  case BRACE_COMPLEX + 8:
	  case BRACE_COMPLEX + 9:
	    {
		no = op - BRACE_COMPLEX;
		++brace_count[no];

		// If not matched enough times yet, try one more
		if (brace_count[no] <= (brace_min[no] <= brace_max[no]
					     ? brace_min[no] : brace_max[no]))
		{
		    rp = regstack_push(RS_BRCPLX_MORE, scan);
		    if (rp == NULL)
			status = RA_FAIL;
		    else
		    {
			rp->rs_no = no;
			reg_save(&rp->rs_un.regsave, &backpos);
			next = OPERAND(scan);
			// We continue and handle the result when done.
		    }
		    break;
		}

		// If matched enough times, may try matching some more
		if (brace_min[no] <= brace_max[no])
		{
		    // Range is the normal way around, use longest match
		    if (brace_count[no] <= brace_max[no])
		    {
			rp = regstack_push(RS_BRCPLX_LONG, scan);
			if (rp == NULL)
			    status = RA_FAIL;
			else
			{
			    rp->rs_no = no;
			    reg_save(&rp->rs_un.regsave, &backpos);
			    next = OPERAND(scan);
			    // We continue and handle the result when done.
			}
		    }
		}
		else
		{
		    // Range is backwards, use shortest match first
		    if (brace_count[no] <= brace_min[no])
		    {
			rp = regstack_push(RS_BRCPLX_SHORT, scan);
			if (rp == NULL)
			    status = RA_FAIL;
			else
			{
			    reg_save(&rp->rs_un.regsave, &backpos);
			    // We continue and handle the result when done.
			}
		    }
		}
	    }
	    break;

	  case BRACE_SIMPLE:
	  case STAR:
	  case PLUS:
	    {
		regstar_T	rst;

		// Lookahead to avoid useless match attempts when we know
		// what character comes next.
		if (OP(next) == EXACTLY)
		{
		    rst.nextb = *OPERAND(next);
		    if (rex.reg_ic)
		    {
			if (MB_ISUPPER(rst.nextb))
			    rst.nextb_ic = MB_TOLOWER(rst.nextb);
			else
			    rst.nextb_ic = MB_TOUPPER(rst.nextb);
		    }
		    else
			rst.nextb_ic = rst.nextb;
		}
		else
		{
		    rst.nextb = NUL;
		    rst.nextb_ic = NUL;
		}
		if (op != BRACE_SIMPLE)
		{
		    rst.minval = (op == STAR) ? 0 : 1;
		    rst.maxval = MAX_LIMIT;
		}
		else
		{
		    rst.minval = bl_minval;
		    rst.maxval = bl_maxval;
		}

		// When maxval > minval, try matching as much as possible, up
		// to maxval.  When maxval < minval, try matching at least the
		// minimal number (since the range is backwards, that's also
		// maxval!).
		rst.count = regrepeat(OPERAND(scan), rst.maxval);
		if (got_int)
		{
		    status = RA_FAIL;
		    break;
		}
		if (rst.minval <= rst.maxval
			  ? rst.count >= rst.minval : rst.count >= rst.maxval)
		{
		    // It could match.  Prepare for trying to match what
		    // follows.  The code is below.  Parameters are stored in
		    // a regstar_T on the regstack.
		    if ((long)((unsigned)regstack.ga_len >> 10) >= p_mmp)
		    {
			emsg(_(e_maxmempat));
			status = RA_FAIL;
		    }
		    else if (ga_grow(&regstack, sizeof(regstar_T)) == FAIL)
			status = RA_FAIL;
		    else
		    {
			regstack.ga_len += sizeof(regstar_T);
			rp = regstack_push(rst.minval <= rst.maxval
					? RS_STAR_LONG : RS_STAR_SHORT, scan);
			if (rp == NULL)
			    status = RA_FAIL;
			else
			{
			    *(((regstar_T *)rp) - 1) = rst;
			    status = RA_BREAK;	    // skip the restore bits
			}
		    }
		}
		else
		    status = RA_NOMATCH;

	    }
	    break;

	  case NOMATCH:
	  case MATCH:
	  case SUBPAT:
	    rp = regstack_push(RS_NOMATCH, scan);
	    if (rp == NULL)
		status = RA_FAIL;
	    else
	    {
		rp->rs_no = op;
		reg_save(&rp->rs_un.regsave, &backpos);
		next = OPERAND(scan);
		// We continue and handle the result when done.
	    }
	    break;

	  case BEHIND:
	  case NOBEHIND:
	    // Need a bit of room to store extra positions.
	    if ((long)((unsigned)regstack.ga_len >> 10) >= p_mmp)
	    {
		emsg(_(e_maxmempat));
		status = RA_FAIL;
	    }
	    else if (ga_grow(&regstack, sizeof(regbehind_T)) == FAIL)
		status = RA_FAIL;
	    else
	    {
		regstack.ga_len += sizeof(regbehind_T);
		rp = regstack_push(RS_BEHIND1, scan);
		if (rp == NULL)
		    status = RA_FAIL;
		else
		{
		    // Need to save the subexpr to be able to restore them
		    // when there is a match but we don't use it.
		    save_subexpr(((regbehind_T *)rp) - 1);

		    rp->rs_no = op;
		    reg_save(&rp->rs_un.regsave, &backpos);
		    // First try if what follows matches.  If it does then we
		    // check the behind match by looping.
		}
	    }
	    break;

	  case BHPOS:
	    if (REG_MULTI)
	    {
		if (behind_pos.rs_u.pos.col != (colnr_T)(rex.input - rex.line)
			|| behind_pos.rs_u.pos.lnum != rex.lnum)
		    status = RA_NOMATCH;
	    }
	    else if (behind_pos.rs_u.ptr != rex.input)
		status = RA_NOMATCH;
	    break;

	  case NEWL:
	    if ((c != NUL || !REG_MULTI || rex.lnum > rex.reg_maxline
			     || rex.reg_line_lbr)
					   && (c != '\n' || !rex.reg_line_lbr))
		status = RA_NOMATCH;
	    else if (rex.reg_line_lbr)
		ADVANCE_REGINPUT();
	    else
		reg_nextline();
	    break;

	  case END:
	    status = RA_MATCH;	// Success!
	    break;

	  default:
	    iemsg(_(e_re_corr));
#ifdef DEBUG
	    printf("Illegal op code %d\n", op);
#endif
	    status = RA_FAIL;
	    break;
	  }
	}

	// If we can't continue sequentially, break the inner loop.
	if (status != RA_CONT)
	    break;

	// Continue in inner loop, advance to next item.
	scan = next;

    } // end of inner loop

    // If there is something on the regstack execute the code for the state.
    // If the state is popped then loop and use the older state.
    while (regstack.ga_len > 0 && status != RA_FAIL)
    {
	rp = (regitem_T *)((char *)regstack.ga_data + regstack.ga_len) - 1;
	switch (rp->rs_state)
	{
	  case RS_NOPEN:
	    // Result is passed on as-is, simply pop the state.
	    regstack_pop(&scan);
	    break;

	  case RS_MOPEN:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
		restore_se(&rp->rs_un.sesave, &rex.reg_startpos[rp->rs_no],
						  &rex.reg_startp[rp->rs_no]);
	    regstack_pop(&scan);
	    break;

#ifdef FEAT_SYN_HL
	  case RS_ZOPEN:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
		restore_se(&rp->rs_un.sesave, &reg_startzpos[rp->rs_no],
						 &reg_startzp[rp->rs_no]);
	    regstack_pop(&scan);
	    break;
#endif

	  case RS_MCLOSE:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
		restore_se(&rp->rs_un.sesave, &rex.reg_endpos[rp->rs_no],
						    &rex.reg_endp[rp->rs_no]);
	    regstack_pop(&scan);
	    break;

#ifdef FEAT_SYN_HL
	  case RS_ZCLOSE:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
		restore_se(&rp->rs_un.sesave, &reg_endzpos[rp->rs_no],
						   &reg_endzp[rp->rs_no]);
	    regstack_pop(&scan);
	    break;
#endif

	  case RS_BRANCH:
	    if (status == RA_MATCH)
		// this branch matched, use it
		regstack_pop(&scan);
	    else
	    {
		if (status != RA_BREAK)
		{
		    // After a non-matching branch: try next one.
		    reg_restore(&rp->rs_un.regsave, &backpos);
		    scan = rp->rs_scan;
		}
		if (scan == NULL || OP(scan) != BRANCH)
		{
		    // no more branches, didn't find a match
		    status = RA_NOMATCH;
		    regstack_pop(&scan);
		}
		else
		{
		    // Prepare to try a branch.
		    rp->rs_scan = regnext(scan);
		    reg_save(&rp->rs_un.regsave, &backpos);
		    scan = OPERAND(scan);
		}
	    }
	    break;

	  case RS_BRCPLX_MORE:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
	    {
		reg_restore(&rp->rs_un.regsave, &backpos);
		--brace_count[rp->rs_no];	// decrement match count
	    }
	    regstack_pop(&scan);
	    break;

	  case RS_BRCPLX_LONG:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
	    {
		// There was no match, but we did find enough matches.
		reg_restore(&rp->rs_un.regsave, &backpos);
		--brace_count[rp->rs_no];
		// continue with the items after "\{}"
		status = RA_CONT;
	    }
	    regstack_pop(&scan);
	    if (status == RA_CONT)
		scan = regnext(scan);
	    break;

	  case RS_BRCPLX_SHORT:
	    // Pop the state.  Restore pointers when there is no match.
	    if (status == RA_NOMATCH)
		// There was no match, try to match one more item.
		reg_restore(&rp->rs_un.regsave, &backpos);
	    regstack_pop(&scan);
	    if (status == RA_NOMATCH)
	    {
		scan = OPERAND(scan);
		status = RA_CONT;
	    }
	    break;

	  case RS_NOMATCH:
	    // Pop the state.  If the operand matches for NOMATCH or
	    // doesn't match for MATCH/SUBPAT, we fail.  Otherwise backup,
	    // except for SUBPAT, and continue with the next item.
	    if (status == (rp->rs_no == NOMATCH ? RA_MATCH : RA_NOMATCH))
		status = RA_NOMATCH;
	    else
	    {
		status = RA_CONT;
		if (rp->rs_no != SUBPAT)	// zero-width
		    reg_restore(&rp->rs_un.regsave, &backpos);
	    }
	    regstack_pop(&scan);
	    if (status == RA_CONT)
		scan = regnext(scan);
	    break;

	  case RS_BEHIND1:
	    if (status == RA_NOMATCH)
	    {
		regstack_pop(&scan);
		regstack.ga_len -= sizeof(regbehind_T);
	    }
	    else
	    {
		// The stuff after BEHIND/NOBEHIND matches.  Now try if
		// the behind part does (not) match before the current
		// position in the input.  This must be done at every
		// position in the input and checking if the match ends at
		// the current position.

		// save the position after the found match for next
		reg_save(&(((regbehind_T *)rp) - 1)->save_after, &backpos);

		// Start looking for a match with operand at the current
		// position.  Go back one character until we find the
		// result, hitting the start of the line or the previous
		// line (for multi-line matching).
		// Set behind_pos to where the match should end, BHPOS
		// will match it.  Save the current value.
		(((regbehind_T *)rp) - 1)->save_behind = behind_pos;
		behind_pos = rp->rs_un.regsave;

		rp->rs_state = RS_BEHIND2;

		reg_restore(&rp->rs_un.regsave, &backpos);
		scan = OPERAND(rp->rs_scan) + 4;
	    }
	    break;

	  case RS_BEHIND2:
	    // Looping for BEHIND / NOBEHIND match.
	    if (status == RA_MATCH && reg_save_equal(&behind_pos))
	    {
		// found a match that ends where "next" started
		behind_pos = (((regbehind_T *)rp) - 1)->save_behind;
		if (rp->rs_no == BEHIND)
		    reg_restore(&(((regbehind_T *)rp) - 1)->save_after,
								    &backpos);
		else
		{
		    // But we didn't want a match.  Need to restore the
		    // subexpr, because what follows matched, so they have
		    // been set.
		    status = RA_NOMATCH;
		    restore_subexpr(((regbehind_T *)rp) - 1);
		}
		regstack_pop(&scan);
		regstack.ga_len -= sizeof(regbehind_T);
	    }
	    else
	    {
		long limit;

		// No match or a match that doesn't end where we want it: Go
		// back one character.  May go to previous line once.
		no = OK;
		limit = OPERAND_MIN(rp->rs_scan);
		if (REG_MULTI)
		{
		    if (limit > 0
			    && ((rp->rs_un.regsave.rs_u.pos.lnum
						    < behind_pos.rs_u.pos.lnum
				    ? (colnr_T)STRLEN(rex.line)
				    : behind_pos.rs_u.pos.col)
				- rp->rs_un.regsave.rs_u.pos.col >= limit))
			no = FAIL;
		    else if (rp->rs_un.regsave.rs_u.pos.col == 0)
		    {
			if (rp->rs_un.regsave.rs_u.pos.lnum
					< behind_pos.rs_u.pos.lnum
				|| reg_getline(
					--rp->rs_un.regsave.rs_u.pos.lnum)
								  == NULL)
			    no = FAIL;
			else
			{
			    reg_restore(&rp->rs_un.regsave, &backpos);
			    rp->rs_un.regsave.rs_u.pos.col =
						 (colnr_T)STRLEN(rex.line);
			}
		    }
		    else
		    {
			if (has_mbyte)
			{
			    char_u *line =
				  reg_getline(rp->rs_un.regsave.rs_u.pos.lnum);

			    rp->rs_un.regsave.rs_u.pos.col -=
				(*mb_head_off)(line, line
				    + rp->rs_un.regsave.rs_u.pos.col - 1) + 1;
			}
			else
			    --rp->rs_un.regsave.rs_u.pos.col;
		    }
		}
		else
		{
		    if (rp->rs_un.regsave.rs_u.ptr == rex.line)
			no = FAIL;
		    else
		    {
			MB_PTR_BACK(rex.line, rp->rs_un.regsave.rs_u.ptr);
			if (limit > 0 && (long)(behind_pos.rs_u.ptr
				     - rp->rs_un.regsave.rs_u.ptr) > limit)
			    no = FAIL;
		    }
		}
		if (no == OK)
		{
		    // Advanced, prepare for finding match again.
		    reg_restore(&rp->rs_un.regsave, &backpos);
		    scan = OPERAND(rp->rs_scan) + 4;
		    if (status == RA_MATCH)
		    {
			// We did match, so subexpr may have been changed,
			// need to restore them for the next try.
			status = RA_NOMATCH;
			restore_subexpr(((regbehind_T *)rp) - 1);
		    }
		}
		else
		{
		    // Can't advance.  For NOBEHIND that's a match.
		    behind_pos = (((regbehind_T *)rp) - 1)->save_behind;
		    if (rp->rs_no == NOBEHIND)
		    {
			reg_restore(&(((regbehind_T *)rp) - 1)->save_after,
								    &backpos);
			status = RA_MATCH;
		    }
		    else
		    {
			// We do want a proper match.  Need to restore the
			// subexpr if we had a match, because they may have
			// been set.
			if (status == RA_MATCH)
			{
			    status = RA_NOMATCH;
			    restore_subexpr(((regbehind_T *)rp) - 1);
			}
		    }
		    regstack_pop(&scan);
		    regstack.ga_len -= sizeof(regbehind_T);
		}
	    }
	    break;

	  case RS_STAR_LONG:
	  case RS_STAR_SHORT:
	    {
		regstar_T	    *rst = ((regstar_T *)rp) - 1;

		if (status == RA_MATCH)
		{
		    regstack_pop(&scan);
		    regstack.ga_len -= sizeof(regstar_T);
		    break;
		}

		// Tried once already, restore input pointers.
		if (status != RA_BREAK)
		    reg_restore(&rp->rs_un.regsave, &backpos);

		// Repeat until we found a position where it could match.
		for (;;)
		{
		    if (status != RA_BREAK)
		    {
			// Tried first position already, advance.
			if (rp->rs_state == RS_STAR_LONG)
			{
			    // Trying for longest match, but couldn't or
			    // didn't match -- back up one char.
			    if (--rst->count < rst->minval)
				break;
			    if (rex.input == rex.line)
			    {
				// backup to last char of previous line
				--rex.lnum;
				rex.line = reg_getline(rex.lnum);
				// Just in case regrepeat() didn't count
				// right.
				if (rex.line == NULL)
				    break;
				rex.input = rex.line + STRLEN(rex.line);
				fast_breakcheck();
			    }
			    else
				MB_PTR_BACK(rex.line, rex.input);
			}
			else
			{
			    // Range is backwards, use shortest match first.
			    // Careful: maxval and minval are exchanged!
			    // Couldn't or didn't match: try advancing one
			    // char.
			    if (rst->count == rst->minval
				  || regrepeat(OPERAND(rp->rs_scan), 1L) == 0)
				break;
			    ++rst->count;
			}
			if (got_int)
			    break;
		    }
		    else
			status = RA_NOMATCH;

		    // If it could match, try it.
		    if (rst->nextb == NUL || *rex.input == rst->nextb
					     || *rex.input == rst->nextb_ic)
		    {
			reg_save(&rp->rs_un.regsave, &backpos);
			scan = regnext(rp->rs_scan);
			status = RA_CONT;
			break;
		    }
		}
		if (status != RA_CONT)
		{
		    // Failed.
		    regstack_pop(&scan);
		    regstack.ga_len -= sizeof(regstar_T);
		    status = RA_NOMATCH;
		}
	    }
	    break;
	}

	// If we want to continue the inner loop or didn't pop a state
	// continue matching loop
	if (status == RA_CONT || rp == (regitem_T *)
			     ((char *)regstack.ga_data + regstack.ga_len) - 1)
	    break;
    }

    // May need to continue with the inner loop, starting at "scan".
    if (status == RA_CONT)
	continue;

    // If the regstack is empty or something failed we are done.
    if (regstack.ga_len == 0 || status == RA_FAIL)
    {
	if (scan == NULL)
	{
	    // We get here only if there's trouble -- normally "case END" is
	    // the terminating point.
	    iemsg(_(e_re_corr));
#ifdef DEBUG
	    printf("Premature EOL\n");
#endif
	}
	return (status == RA_MATCH);
    }

  } // End of loop until the regstack is empty.

  // NOTREACHED
}

/*
 * regtry - try match of "prog" with at rex.line["col"].
 * Returns 0 for failure, number of lines contained in the match otherwise.
 */
    static long
regtry(
    bt_regprog_T	*prog,
    colnr_T		col,
    proftime_T		*tm,		// timeout limit or NULL
    int			*timed_out)	// flag set on timeout or NULL
{
    rex.input = rex.line + col;
    rex.need_clear_subexpr = TRUE;
#ifdef FEAT_SYN_HL
    // Clear the external match subpointers if necessary.
    rex.need_clear_zsubexpr = (prog->reghasz == REX_SET);
#endif

    if (regmatch(prog->program + 1, tm, timed_out) == 0)
	return 0;

    cleanup_subexpr();
    if (REG_MULTI)
    {
	if (rex.reg_startpos[0].lnum < 0)
	{
	    rex.reg_startpos[0].lnum = 0;
	    rex.reg_startpos[0].col = col;
	}
	if (rex.reg_endpos[0].lnum < 0)
	{
	    rex.reg_endpos[0].lnum = rex.lnum;
	    rex.reg_endpos[0].col = (int)(rex.input - rex.line);
	}
	else
	    // Use line number of "\ze".
	    rex.lnum = rex.reg_endpos[0].lnum;
    }
    else
    {
	if (rex.reg_startp[0] == NULL)
	    rex.reg_startp[0] = rex.line + col;
	if (rex.reg_endp[0] == NULL)
	    rex.reg_endp[0] = rex.input;
    }
#ifdef FEAT_SYN_HL
    // Package any found \z(...\) matches for export. Default is none.
    unref_extmatch(re_extmatch_out);
    re_extmatch_out = NULL;

    if (prog->reghasz == REX_SET)
    {
	int		i;

	cleanup_zsubexpr();
	re_extmatch_out = make_extmatch();
	if (re_extmatch_out == NULL)
	    return 0;
	for (i = 0; i < NSUBEXP; i++)
	{
	    if (REG_MULTI)
	    {
		// Only accept single line matches.
		if (reg_startzpos[i].lnum >= 0
			&& reg_endzpos[i].lnum == reg_startzpos[i].lnum
			&& reg_endzpos[i].col >= reg_startzpos[i].col)
		    re_extmatch_out->matches[i] =
			vim_strnsave(reg_getline(reg_startzpos[i].lnum)
						       + reg_startzpos[i].col,
				   reg_endzpos[i].col - reg_startzpos[i].col);
	    }
	    else
	    {
		if (reg_startzp[i] != NULL && reg_endzp[i] != NULL)
		    re_extmatch_out->matches[i] =
			    vim_strnsave(reg_startzp[i],
						reg_endzp[i] - reg_startzp[i]);
	    }
	}
    }
#endif
    return 1 + rex.lnum;
}

/*
 * Match a regexp against a string ("line" points to the string) or multiple
 * lines ("line" is NULL, use reg_getline()).
 * Returns 0 for failure, number of lines contained in the match otherwise.
 */
    static long
bt_regexec_both(
    char_u	*line,
    colnr_T	col,		// column to start looking for match
    proftime_T	*tm,		// timeout limit or NULL
    int		*timed_out)	// flag set on timeout or NULL
{
    bt_regprog_T    *prog;
    char_u	    *s;
    long	    retval = 0L;

    // Create "regstack" and "backpos" if they are not allocated yet.
    // We allocate *_INITIAL amount of bytes first and then set the grow size
    // to much bigger value to avoid many malloc calls in case of deep regular
    // expressions.
    if (regstack.ga_data == NULL)
    {
	// Use an item size of 1 byte, since we push different things
	// onto the regstack.
	ga_init2(&regstack, 1, REGSTACK_INITIAL);
	(void)ga_grow(&regstack, REGSTACK_INITIAL);
	regstack.ga_growsize = REGSTACK_INITIAL * 8;
    }

    if (backpos.ga_data == NULL)
    {
	ga_init2(&backpos, sizeof(backpos_T), BACKPOS_INITIAL);
	(void)ga_grow(&backpos, BACKPOS_INITIAL);
	backpos.ga_growsize = BACKPOS_INITIAL * 8;
    }

    if (REG_MULTI)
    {
	prog = (bt_regprog_T *)rex.reg_mmatch->regprog;
	line = reg_getline((linenr_T)0);
	rex.reg_startpos = rex.reg_mmatch->startpos;
	rex.reg_endpos = rex.reg_mmatch->endpos;
    }
    else
    {
	prog = (bt_regprog_T *)rex.reg_match->regprog;
	rex.reg_startp = rex.reg_match->startp;
	rex.reg_endp = rex.reg_match->endp;
    }

    // Be paranoid...
    if (prog == NULL || line == NULL)
    {
	iemsg(_(e_null));
	goto theend;
    }

    // Check validity of program.
    if (prog_magic_wrong())
	goto theend;

    // If the start column is past the maximum column: no need to try.
    if (rex.reg_maxcol > 0 && col >= rex.reg_maxcol)
	goto theend;

    // If pattern contains "\c" or "\C": overrule value of rex.reg_ic
    if (prog->regflags & RF_ICASE)
	rex.reg_ic = TRUE;
    else if (prog->regflags & RF_NOICASE)
	rex.reg_ic = FALSE;

    // If pattern contains "\Z" overrule value of rex.reg_icombine
    if (prog->regflags & RF_ICOMBINE)
	rex.reg_icombine = TRUE;

    // If there is a "must appear" string, look for it.
    if (prog->regmust != NULL)
    {
	int c;

	if (has_mbyte)
	    c = (*mb_ptr2char)(prog->regmust);
	else
	    c = *prog->regmust;
	s = line + col;

	// This is used very often, esp. for ":global".  Use three versions of
	// the loop to avoid overhead of conditions.
	if (!rex.reg_ic && !has_mbyte)
	    while ((s = vim_strbyte(s, c)) != NULL)
	    {
		if (cstrncmp(s, prog->regmust, &prog->regmlen) == 0)
		    break;		// Found it.
		++s;
	    }
	else if (!rex.reg_ic || (!enc_utf8 && mb_char2len(c) > 1))
	    while ((s = vim_strchr(s, c)) != NULL)
	    {
		if (cstrncmp(s, prog->regmust, &prog->regmlen) == 0)
		    break;		// Found it.
		MB_PTR_ADV(s);
	    }
	else
	    while ((s = cstrchr(s, c)) != NULL)
	    {
		if (cstrncmp(s, prog->regmust, &prog->regmlen) == 0)
		    break;		// Found it.
		MB_PTR_ADV(s);
	    }
	if (s == NULL)		// Not present.
	    goto theend;
    }

    rex.line = line;
    rex.lnum = 0;
    reg_toolong = FALSE;

    // Simplest case: Anchored match need be tried only once.
    if (prog->reganch)
    {
	int	c;

	if (has_mbyte)
	    c = (*mb_ptr2char)(rex.line + col);
	else
	    c = rex.line[col];
	if (prog->regstart == NUL
		|| prog->regstart == c
		|| (rex.reg_ic
		    && (((enc_utf8 && utf_fold(prog->regstart) == utf_fold(c)))
			|| (c < 255 && prog->regstart < 255 &&
			    MB_TOLOWER(prog->regstart) == MB_TOLOWER(c)))))
	    retval = regtry(prog, col, tm, timed_out);
	else
	    retval = 0;
    }
    else
    {
#ifdef FEAT_RELTIME
	int tm_count = 0;
#endif
	// Messy cases:  unanchored match.
	while (!got_int)
	{
	    if (prog->regstart != NUL)
	    {
		// Skip until the char we know it must start with.
		// Used often, do some work to avoid call overhead.
		if (!rex.reg_ic && !has_mbyte)
		    s = vim_strbyte(rex.line + col, prog->regstart);
		else
		    s = cstrchr(rex.line + col, prog->regstart);
		if (s == NULL)
		{
		    retval = 0;
		    break;
		}
		col = (int)(s - rex.line);
	    }

	    // Check for maximum column to try.
	    if (rex.reg_maxcol > 0 && col >= rex.reg_maxcol)
	    {
		retval = 0;
		break;
	    }

	    retval = regtry(prog, col, tm, timed_out);
	    if (retval > 0)
		break;

	    // if not currently on the first line, get it again
	    if (rex.lnum != 0)
	    {
		rex.lnum = 0;
		rex.line = reg_getline((linenr_T)0);
	    }
	    if (rex.line[col] == NUL)
		break;
	    if (has_mbyte)
		col += (*mb_ptr2len)(rex.line + col);
	    else
		++col;
#ifdef FEAT_RELTIME
	    // Check for timeout once in a twenty times to avoid overhead.
	    if (tm != NULL && ++tm_count == 20)
	    {
		tm_count = 0;
		if (profile_passed_limit(tm))
		{
		    if (timed_out != NULL)
			*timed_out = TRUE;
		    break;
		}
	    }
#endif
	}
    }

theend:
    // Free "reg_tofree" when it's a bit big.
    // Free regstack and backpos if they are bigger than their initial size.
    if (reg_tofreelen > 400)
	VIM_CLEAR(reg_tofree);
    if (regstack.ga_maxlen > REGSTACK_INITIAL)
	ga_clear(&regstack);
    if (backpos.ga_maxlen > BACKPOS_INITIAL)
	ga_clear(&backpos);

    return retval;
}

/*
 * Match a regexp against a string.
 * "rmp->regprog" is a compiled regexp as returned by vim_regcomp().
 * Uses curbuf for line count and 'iskeyword'.
 * if "line_lbr" is TRUE  consider a "\n" in "line" to be a line break.
 *
 * Returns 0 for failure, number of lines contained in the match otherwise.
 */
    static int
bt_regexec_nl(
    regmatch_T	*rmp,
    char_u	*line,	// string to match against
    colnr_T	col,	// column to start looking for match
    int		line_lbr)
{
    rex.reg_match = rmp;
    rex.reg_mmatch = NULL;
    rex.reg_maxline = 0;
    rex.reg_line_lbr = line_lbr;
    rex.reg_buf = curbuf;
    rex.reg_win = NULL;
    rex.reg_ic = rmp->rm_ic;
    rex.reg_icombine = FALSE;
    rex.reg_maxcol = 0;

    return bt_regexec_both(line, col, NULL, NULL);
}

/*
 * Match a regexp against multiple lines.
 * "rmp->regprog" is a compiled regexp as returned by vim_regcomp().
 * Uses curbuf for line count and 'iskeyword'.
 *
 * Return zero if there is no match.  Return number of lines contained in the
 * match otherwise.
 */
    static long
bt_regexec_multi(
    regmmatch_T	*rmp,
    win_T	*win,		// window in which to search or NULL
    buf_T	*buf,		// buffer in which to search
    linenr_T	lnum,		// nr of line to start looking for match
    colnr_T	col,		// column to start looking for match
    proftime_T	*tm,		// timeout limit or NULL
    int		*timed_out)	// flag set on timeout or NULL
{
    init_regexec_multi(rmp, win, buf, lnum);
    return bt_regexec_both(NULL, col, tm, timed_out);
}

/*
 * Compare a number with the operand of RE_LNUM, RE_COL or RE_VCOL.
 */
    static int
re_num_cmp(long_u val, char_u *scan)
{
    long_u  n = OPERAND_MIN(scan);

    if (OPERAND_CMP(scan) == '>')
	return val > n;
    if (OPERAND_CMP(scan) == '<')
	return val < n;
    return val == n;
}

#ifdef BT_REGEXP_DUMP

/*
 * regdump - dump a regexp onto stdout in vaguely comprehensible form
 */
    static void
regdump(char_u *pattern, bt_regprog_T *r)
{
    char_u  *s;
    int	    op = EXACTLY;	// Arbitrary non-END op.
    char_u  *next;
    char_u  *end = NULL;
    FILE    *f;

#ifdef BT_REGEXP_LOG
    f = fopen("bt_regexp_log.log", "a");
#else
    f = stdout;
#endif
    if (f == NULL)
	return;
    fprintf(f, "-------------------------------------\n\r\nregcomp(%s):\r\n", pattern);

    s = r->program + 1;
    // Loop until we find the END that isn't before a referred next (an END
    // can also appear in a NOMATCH operand).
    while (op != END || s <= end)
    {
	op = OP(s);
	fprintf(f, "%2d%s", (int)(s - r->program), regprop(s)); // Where, what.
	next = regnext(s);
	if (next == NULL)	// Next ptr.
	    fprintf(f, "(0)");
	else
	    fprintf(f, "(%d)", (int)((s - r->program) + (next - s)));
	if (end < next)
	    end = next;
	if (op == BRACE_LIMITS)
	{
	    // Two ints
	    fprintf(f, " minval %ld, maxval %ld", OPERAND_MIN(s), OPERAND_MAX(s));
	    s += 8;
	}
	else if (op == BEHIND || op == NOBEHIND)
	{
	    // one int
	    fprintf(f, " count %ld", OPERAND_MIN(s));
	    s += 4;
	}
	else if (op == RE_LNUM || op == RE_COL || op == RE_VCOL)
	{
	    // one int plus comparator
	    fprintf(f, " count %ld", OPERAND_MIN(s));
	    s += 5;
	}
	s += 3;
	if (op == ANYOF || op == ANYOF + ADD_NL
		|| op == ANYBUT || op == ANYBUT + ADD_NL
		|| op == EXACTLY)
	{
	    // Literal string, where present.
	    fprintf(f, "\nxxxxxxxxx\n");
	    while (*s != NUL)
		fprintf(f, "%c", *s++);
	    fprintf(f, "\nxxxxxxxxx\n");
	    s++;
	}
	fprintf(f, "\r\n");
    }

    // Header fields of interest.
    if (r->regstart != NUL)
	fprintf(f, "start `%s' 0x%x; ", r->regstart < 256
		? (char *)transchar(r->regstart)
		: "multibyte", r->regstart);
    if (r->reganch)
	fprintf(f, "anchored; ");
    if (r->regmust != NULL)
	fprintf(f, "must have \"%s\"", r->regmust);
    fprintf(f, "\r\n");

#ifdef BT_REGEXP_LOG
    fclose(f);
#endif
}
#endif	    // BT_REGEXP_DUMP

#ifdef DEBUG
/*
 * regprop - printable representation of opcode
 */
    static char_u *
regprop(char_u *op)
{
    char	    *p;
    static char	    buf[50];

    STRCPY(buf, ":");

    switch ((int) OP(op))
    {
      case BOL:
	p = "BOL";
	break;
      case EOL:
	p = "EOL";
	break;
      case RE_BOF:
	p = "BOF";
	break;
      case RE_EOF:
	p = "EOF";
	break;
      case CURSOR:
	p = "CURSOR";
	break;
      case RE_VISUAL:
	p = "RE_VISUAL";
	break;
      case RE_LNUM:
	p = "RE_LNUM";
	break;
      case RE_MARK:
	p = "RE_MARK";
	break;
      case RE_COL:
	p = "RE_COL";
	break;
      case RE_VCOL:
	p = "RE_VCOL";
	break;
      case BOW:
	p = "BOW";
	break;
      case EOW:
	p = "EOW";
	break;
      case ANY:
	p = "ANY";
	break;
      case ANY + ADD_NL:
	p = "ANY+NL";
	break;
      case ANYOF:
	p = "ANYOF";
	break;
      case ANYOF + ADD_NL:
	p = "ANYOF+NL";
	break;
      case ANYBUT:
	p = "ANYBUT";
	break;
      case ANYBUT + ADD_NL:
	p = "ANYBUT+NL";
	break;
      case IDENT:
	p = "IDENT";
	break;
      case IDENT + ADD_NL:
	p = "IDENT+NL";
	break;
      case SIDENT:
	p = "SIDENT";
	break;
      case SIDENT + ADD_NL:
	p = "SIDENT+NL";
	break;
      case KWORD:
	p = "KWORD";
	break;
      case KWORD + ADD_NL:
	p = "KWORD+NL";
	break;
      case SKWORD:
	p = "SKWORD";
	break;
      case SKWORD + ADD_NL:
	p = "SKWORD+NL";
	break;
      case FNAME:
	p = "FNAME";
	break;
      case FNAME + ADD_NL:
	p = "FNAME+NL";
	break;
      case SFNAME:
	p = "SFNAME";
	break;
      case SFNAME + ADD_NL:
	p = "SFNAME+NL";
	break;
      case PRINT:
	p = "PRINT";
	break;
      case PRINT + ADD_NL:
	p = "PRINT+NL";
	break;
      case SPRINT:
	p = "SPRINT";
	break;
      case SPRINT + ADD_NL:
	p = "SPRINT+NL";
	break;
      case WHITE:
	p = "WHITE";
	break;
      case WHITE + ADD_NL:
	p = "WHITE+NL";
	break;
      case NWHITE:
	p = "NWHITE";
	break;
      case NWHITE + ADD_NL:
	p = "NWHITE+NL";
	break;
      case DIGIT:
	p = "DIGIT";
	break;
      case DIGIT + ADD_NL:
	p = "DIGIT+NL";
	break;
      case NDIGIT:
	p = "NDIGIT";
	break;
      case NDIGIT + ADD_NL:
	p = "NDIGIT+NL";
	break;
      case HEX:
	p = "HEX";
	break;
      case HEX + ADD_NL:
	p = "HEX+NL";
	break;
      case NHEX:
	p = "NHEX";
	break;
      case NHEX + ADD_NL:
	p = "NHEX+NL";
	break;
      case OCTAL:
	p = "OCTAL";
	break;
      case OCTAL + ADD_NL:
	p = "OCTAL+NL";
	break;
      case NOCTAL:
	p = "NOCTAL";
	break;
      case NOCTAL + ADD_NL:
	p = "NOCTAL+NL";
	break;
      case WORD:
	p = "WORD";
	break;
      case WORD + ADD_NL:
	p = "WORD+NL";
	break;
      case NWORD:
	p = "NWORD";
	break;
      case NWORD + ADD_NL:
	p = "NWORD+NL";
	break;
      case HEAD:
	p = "HEAD";
	break;
      case HEAD + ADD_NL:
	p = "HEAD+NL";
	break;
      case NHEAD:
	p = "NHEAD";
	break;
      case NHEAD + ADD_NL:
	p = "NHEAD+NL";
	break;
      case ALPHA:
	p = "ALPHA";
	break;
      case ALPHA + ADD_NL:
	p = "ALPHA+NL";
	break;
      case NALPHA:
	p = "NALPHA";
	break;
      case NALPHA + ADD_NL:
	p = "NALPHA+NL";
	break;
      case LOWER:
	p = "LOWER";
	break;
      case LOWER + ADD_NL:
	p = "LOWER+NL";
	break;
      case NLOWER:
	p = "NLOWER";
	break;
      case NLOWER + ADD_NL:
	p = "NLOWER+NL";
	break;
      case UPPER:
	p = "UPPER";
	break;
      case UPPER + ADD_NL:
	p = "UPPER+NL";
	break;
      case NUPPER:
	p = "NUPPER";
	break;
      case NUPPER + ADD_NL:
	p = "NUPPER+NL";
	break;
      case BRANCH:
	p = "BRANCH";
	break;
      case EXACTLY:
	p = "EXACTLY";
	break;
      case NOTHING:
	p = "NOTHING";
	break;
      case BACK:
	p = "BACK";
	break;
      case END:
	p = "END";
	break;
      case MOPEN + 0:
	p = "MATCH START";
	break;
      case MOPEN + 1:
      case MOPEN + 2:
      case MOPEN + 3:
      case MOPEN + 4:
      case MOPEN + 5:
      case MOPEN + 6:
      case MOPEN + 7:
      case MOPEN + 8:
      case MOPEN + 9:
	sprintf(buf + STRLEN(buf), "MOPEN%d", OP(op) - MOPEN);
	p = NULL;
	break;
      case MCLOSE + 0:
	p = "MATCH END";
	break;
      case MCLOSE + 1:
      case MCLOSE + 2:
      case MCLOSE + 3:
      case MCLOSE + 4:
      case MCLOSE + 5:
      case MCLOSE + 6:
      case MCLOSE + 7:
      case MCLOSE + 8:
      case MCLOSE + 9:
	sprintf(buf + STRLEN(buf), "MCLOSE%d", OP(op) - MCLOSE);
	p = NULL;
	break;
      case BACKREF + 1:
      case BACKREF + 2:
      case BACKREF + 3:
      case BACKREF + 4:
      case BACKREF + 5:
      case BACKREF + 6:
      case BACKREF + 7:
      case BACKREF + 8:
      case BACKREF + 9:
	sprintf(buf + STRLEN(buf), "BACKREF%d", OP(op) - BACKREF);
	p = NULL;
	break;
      case NOPEN:
	p = "NOPEN";
	break;
      case NCLOSE:
	p = "NCLOSE";
	break;
#ifdef FEAT_SYN_HL
      case ZOPEN + 1:
      case ZOPEN + 2:
      case ZOPEN + 3:
      case ZOPEN + 4:
      case ZOPEN + 5:
      case ZOPEN + 6:
      case ZOPEN + 7:
      case ZOPEN + 8:
      case ZOPEN + 9:
	sprintf(buf + STRLEN(buf), "ZOPEN%d", OP(op) - ZOPEN);
	p = NULL;
	break;
      case ZCLOSE + 1:
      case ZCLOSE + 2:
      case ZCLOSE + 3:
      case ZCLOSE + 4:
      case ZCLOSE + 5:
      case ZCLOSE + 6:
      case ZCLOSE + 7:
      case ZCLOSE + 8:
      case ZCLOSE + 9:
	sprintf(buf + STRLEN(buf), "ZCLOSE%d", OP(op) - ZCLOSE);
	p = NULL;
	break;
      case ZREF + 1:
      case ZREF + 2:
      case ZREF + 3:
      case ZREF + 4:
      case ZREF + 5:
      case ZREF + 6:
      case ZREF + 7:
      case ZREF + 8:
      case ZREF + 9:
	sprintf(buf + STRLEN(buf), "ZREF%d", OP(op) - ZREF);
	p = NULL;
	break;
#endif
      case STAR:
	p = "STAR";
	break;
      case PLUS:
	p = "PLUS";
	break;
      case NOMATCH:
	p = "NOMATCH";
	break;
      case MATCH:
	p = "MATCH";
	break;
      case BEHIND:
	p = "BEHIND";
	break;
      case NOBEHIND:
	p = "NOBEHIND";
	break;
      case SUBPAT:
	p = "SUBPAT";
	break;
      case BRACE_LIMITS:
	p = "BRACE_LIMITS";
	break;
      case BRACE_SIMPLE:
	p = "BRACE_SIMPLE";
	break;
      case BRACE_COMPLEX + 0:
      case BRACE_COMPLEX + 1:
      case BRACE_COMPLEX + 2:
      case BRACE_COMPLEX + 3:
      case BRACE_COMPLEX + 4:
      case BRACE_COMPLEX + 5:
      case BRACE_COMPLEX + 6:
      case BRACE_COMPLEX + 7:
      case BRACE_COMPLEX + 8:
      case BRACE_COMPLEX + 9:
	sprintf(buf + STRLEN(buf), "BRACE_COMPLEX%d", OP(op) - BRACE_COMPLEX);
	p = NULL;
	break;
      case MULTIBYTECODE:
	p = "MULTIBYTECODE";
	break;
      case NEWL:
	p = "NEWL";
	break;
      default:
	sprintf(buf + STRLEN(buf), "corrupt %d", OP(op));
	p = NULL;
	break;
    }
    if (p != NULL)
	STRCAT(buf, p);
    return (char_u *)buf;
}
#endif	    // DEBUG
