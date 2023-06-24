/* auto/config.h.  Generated from config.h.in by configure.  */
/*
 * config.h.in.  Originally generated automatically from configure.ac by
 * autoheader and manually changed after that.
 */

/* Define if we have EBCDIC code */
/* #undef EBCDIC */

/* Define unless no X support found */
/* #undef HAVE_X11 */

/* Define when terminfo support found */
#define TERMINFO 1

/* Define when termcap.h contains ospeed */
#define HAVE_OSPEED 1

/* Define when ospeed can be extern */
/* #undef OSPEED_EXTERN */

/* Define when termcap.h contains UP, BC and PC */
#define HAVE_UP_BC_PC 1

/* Define when UP, BC and PC can be extern */
/* #undef UP_BC_PC_EXTERN */

/* Define when termcap.h defines outfuntype */
/* #undef HAVE_OUTFUNTYPE */

/* Define when del_curterm() is available */
#define HAVE_DEL_CURTERM 1

/* Define when __DATE__ " " __TIME__ can be used */

/* Define when del_curterm() is available */
#define HAVE_DATE_TIME 1

/* Defined from $SOURCE_DATE_EPOCH, used as the build date */
/* #undef BUILD_DATE */

/* Define when __attribute__((unused)) can be used */
#define HAVE_ATTRIBUTE_UNUSED 1

/* defined always when using configure */
#define UNIX 1

/* Defined to the size of an int */
#define VIM_SIZEOF_INT 4

/* Defined to the size of a long */
#if __LP64__
#define VIM_SIZEOF_LONG 8
#else
#define VIM_SIZEOF_LONG 4
#endif

/* Defined to the size of off_t */
#define SIZEOF_OFF_T 8

/* Defined to the size of time_t */
#define VIM_SIZEOF_TIME_T VIM_SIZEOF_LONG

/* Define when wchar_t is only 2 bytes. */
/* #undef SMALL_WCHAR_T */

/*
 * If we cannot trust one of the following from the libraries, we use our
 * own safe but probably slower vim_memmove().
 */
/* #undef USEBCOPY */
#define USEMEMMOVE 1
/* #undef USEMEMCPY */

/* Define when "man -s 2" is to be used */
/* #undef USEMAN_S */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define to empty if the keyword does not work.  */
/* #undef volatile */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef mode_t */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef pid_t */

/* Define to `unsigned' if <sys/types.h> doesn't define.  */
/* #undef size_t */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef uid_t */

/* Define to `unsigned int' or other type that is 32 bit.  */
/* #undef uint32_t */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef gid_t */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef ino_t */

/* Define to `unsigned' if <sys/types.h> doesn't define.  */
/* #undef dev_t */

/* Define on big-endian machines */
/* #undef WORDS_BIGENDIAN */
#if __BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#endif

/* Define to `unsigned long' if <sys/types.h> doesn't define.  */
/* #undef rlim_t */

/* Define to `struct sigaltstack' if <signal.h> doesn't define.  */
/* #undef stack_t */

/* Define if stack_t has the ss_base field. */
/* #undef HAVE_SS_BASE */

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
#define TIME_WITH_SYS_TIME 1

/* Define if you can safely include both <sys/time.h> and <sys/select.h>.  */
#define SYS_SELECT_WITH_SYS_TIME 1

/* Define to a typecast for select() arguments 2, 3 and 4. */
#define SELECT_TYPE_ARG234 (fd_set *)

/* Define if you have /dev/ptc */
/* #undef HAVE_DEV_PTC */

/* Define if you have Sys4 ptys */
#define HAVE_SVR4_PTYS 1

/* Define to range of pty names to try */
#define PTYRANGE0 "pqrstuvw"
#define PTYRANGE1 "0123456789abcdef"

/* Define if struct sigcontext is present */
#define HAVE_SIGCONTEXT 1

/* Define if touuper/tolower only work on lower/upercase characters */
/* #undef BROKEN_TOUPPER */

/* Define if stat() ignores a trailing slash */
/* #undef STAT_IGNORES_SLASH */

/* Define to nanoseconds field of struct stat */
#define ST_MTIM_NSEC st_mtimespec.tv_nsec

/* Define if tgetstr() has a second argument that is (char *) */
/* #undef TGETSTR_CHAR_P */

/* Define if tgetent() returns zero for an error */
/* #undef TGETENT_ZERO_ERR */

/* Define if the getcwd() function should not be used.  */
/* #undef BAD_GETCWD */

/* Define if you the function: */
#define HAVE_FCHDIR 1
#define HAVE_FCHOWN 1
#define HAVE_FCHMOD 1
#define HAVE_FLOAT_FUNCS 1
#define HAVE_FSEEKO 1
#define HAVE_FSYNC 1
#define HAVE_FTRUNCATE 1
#define HAVE_GETCWD 1
#define HAVE_GETPGID 1
/* #undef HAVE_GETPSEUDOTTY */
#define HAVE_GETPWENT 1
#define HAVE_GETPWNAM 1
#define HAVE_GETPWUID 1
#define HAVE_GETRLIMIT 1
#define HAVE_GETTIMEOFDAY 1
/* #undef HAVE_GETWD */
#define HAVE_ICONV 1
#define HAVE_INET_NTOP 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LSTAT 1
#define HAVE_MEMSET 1
#define HAVE_MKDTEMP 1
#define HAVE_NANOSLEEP 1
#define HAVE_NL_LANGINFO_CODESET 1
#define HAVE_OPENDIR 1
#define HAVE_POSIX_OPENPT 1
#define HAVE_PUTENV 1
#define HAVE_QSORT 1
#define HAVE_READLINK 1
#define HAVE_RENAME 1
#define HAVE_SELECT 1
/* #undef HAVE_SELINUX */
#define HAVE_SETENV 1
#define HAVE_SETPGID 1
#define HAVE_SETSID 1
#define HAVE_SIGACTION 1
#define HAVE_SIGALTSTACK 1
#define HAVE_SIGSET 1
#define HAVE_SIGSETJMP 1
/* #undef HAVE_SIGSTACK */
#define HAVE_SIGPROCMASK 1
#define HAVE_SIGVEC 1
/* #undef HAVE_SMACK */
#define HAVE_STRCASECMP 1
#define HAVE_STRCOLL 1
#define HAVE_STRERROR 1
#define HAVE_STRFTIME 1
/* #undef HAVE_STRICMP */
#define HAVE_STRNCASECMP 1
/* #undef HAVE_STRNICMP */
#define HAVE_STRPBRK 1
#define HAVE_STRPTIME 1
#define HAVE_STRTOL 1
/* #undef HAVE_CANBERRA */
/* #undef HAVE_SODIUM */
#define HAVE_ST_BLKSIZE 1
#define HAVE_SYSCONF 1
#define HAVE_SYSCTL 1
/* #undef HAVE_SYSINFO */
/* #undef HAVE_SYSINFO_MEM_UNIT */
/* #undef HAVE_SYSINFO_UPTIME */
#define HAVE_TGETENT 1
#define HAVE_TOWLOWER 1
#define HAVE_TOWUPPER 1
#define HAVE_ISWUPPER 1
#define HAVE_TZSET 1
#define HAVE_UNSETENV 1
#define HAVE_USLEEP 1
#define HAVE_UTIME 1
/* #undef HAVE_BIND_TEXTDOMAIN_CODESET */
#define HAVE_MBLEN 1
/* #define HAVE_TIMER_CREATE 1 */  /* only for macOS via Xcode */

/* Define, if needed, for accessing large files. */
/* #undef _LARGE_FILES */
/* #undef _FILE_OFFSET_BITS */
/* #undef _LARGEFILE_SOURCE */

/* Define if you do not have utime(), but do have the utimes() function. */
#define HAVE_UTIMES 1

/* Define if you have the header file: */
#define HAVE_DIRENT_H 1
#define HAVE_DISPATCH_DISPATCH_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
/* #undef HAVE_FRAME_H */
#define HAVE_ICONV_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_LIBC_H 1
#define HAVE_LIBGEN_H 1
/* #undef HAVE_LIBINTL_H */
#define HAVE_LOCALE_H 1
#define HAVE_MATH_H 1
/* #undef HAVE_NDIR_H */
#define HAVE_POLL_H 1
/* #undef HAVE_PTHREAD_NP_H */
#define HAVE_PWD_H 1
#define HAVE_SETJMP_H 1
#define HAVE_SGTTY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRINGS_H 1
/* #undef HAVE_STROPTS_H */
/* #undef HAVE_SYS_ACCESS_H */
#define HAVE_SYS_ACL_H 1
/* #undef HAVE_SYS_DIR_H */
#define HAVE_SYS_IOCTL_H 1
/* #undef HAVE_SYS_NDIR_H */
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_POLL_H 1
/* #undef HAVE_SYS_PTEM_H */
/* #undef HAVE_SYS_PTMS_H */
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_SELECT_H 1
/* #undef HAVE_SYS_STATFS_H */
/* #undef HAVE_SYS_STREAM_H */
#define HAVE_SYS_SYSCTL_H 1
/* #undef HAVE_SYS_SYSINFO_H */
/* #undef HAVE_SYS_SYSTEMINFO_H */
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_TERMCAP_H 1
#define HAVE_TERMIOS_H 1
/* #undef HAVE_TERMIO_H */
#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1
#define HAVE_UNISTD_H 1
/* #undef HAVE_UTIL_DEBUG_H */
/* #undef HAVE_UTIL_MSGI18N_H */
#define HAVE_UTIME_H 1
/* #undef HAVE_X11_SUNKEYSYM_H */
/* #undef HAVE_XM_XM_H */
/* #undef HAVE_XM_XPMP_H */
/* #undef HAVE_XM_TRAITP_H */
/* #undef HAVE_XM_MANAGER_H */
/* #undef HAVE_XM_UNHIGHLIGHTT_H */
/* #undef HAVE_XM_JOINSIDET_H */
/* #undef HAVE_XM_NOTEBOOK_H */
/* #undef HAVE_X11_XPM_H */
/* #undef HAVE_X11_XMU_EDITRES_H */
#define HAVE_X11_SM_SMLIB_H 1

/* Define to the type of the XpmAttributes type. */
/* #undef XPMATTRIBUTES_TYPE */

/* Define if you have <sys/wait.h> that is POSIX.1 compatible.  */
#define HAVE_SYS_WAIT_H 1

/* Define if you have a <sys/wait.h> that is not POSIX.1 compatible. */
/* #undef HAVE_UNION_WAIT */

/* This is currently unused in vim: */
/* Define if you have the ANSI C header files. */
/* #undef STDC_HEADERS */

/* instead, we check a few STDC things ourselves */
#define HAVE_STDARG_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1

/* Define if strings.h cannot be included when strings.h already is */
#define NO_STRINGS_WITH_STRING_H 1

/* Define if you want tiny features. */
/* #undef FEAT_TINY */

/* Define if you want normal features. */
#define FEAT_NORMAL 1

/* Define if you want big features. */
/* #undef FEAT_BIG */

/* Define if you want huge features. */
/* #undef FEAT_HUGE */

/* Define if you want to include the Lua interpreter. */
/* #undef FEAT_LUA */

/* Define for linking via dlopen() or LoadLibrary() */
/* #undef DYNAMIC_LUA */

/* Define if you want to include the MzScheme interpreter. */
/* #undef FEAT_MZSCHEME */

/* Define if you want to include the Perl interpreter. */
/* #undef FEAT_PERL */

/* Define for linking via dlopen() or LoadLibrary() */
/* #undef DYNAMIC_PERL */

/* Define if you want to include the Python interpreter. */
/* #undef FEAT_PYTHON */

/* Define if you want to include the Python3 interpreter. */
/* #undef FEAT_PYTHON3 */

/* Define for linking via dlopen() or LoadLibrary() */
/* #undef DYNAMIC_PYTHON */

/* Define for linking via dlopen() or LoadLibrary() */
/* #undef DYNAMIC_PYTHON3 */

/* Define if dynamic python does not require RTLD_GLOBAL */
/* #undef PY_NO_RTLD_GLOBAL */

/* Define if dynamic python3 does not require RTLD_GLOBAL */
/* #undef PY3_NO_RTLD_GLOBAL */

/* Define if you want to include the Ruby interpreter. */
/* #undef FEAT_RUBY */

/* Define for linking via dlopen() or LoadLibrary() */
/* #undef DYNAMIC_RUBY */

/* Define if you want to include the Tcl interpreter. */
/* #undef FEAT_TCL */

/* Define for linking via dlopen() or LoadLibrary() */
/* #undef DYNAMIC_TCL */

/* Define if you want to add support for ACL */
#define HAVE_POSIX_ACL 1
/* #undef HAVE_SOLARIS_ZFS_ACL */
/* #undef HAVE_SOLARIS_ACL */
/* #undef HAVE_AIX_ACL */

/* Define if pango_shape_full() is available. */
/* #undef HAVE_PANGO_SHAPE_FULL */

/* Define if you want to add support of GPM (Linux console mouse daemon) */
/* #undef HAVE_GPM */

/* Define if you want to add support of sysmouse (*BSD console mouse) */
/* #undef HAVE_SYSMOUSE */

/* Define if you want to include the Cscope interface. */
#define FEAT_CSCOPE 1

/* Define if you want to include multibyte support. */
#define FEAT_MBYTE 1

/* Define if you want to include fontset support. */
/* #undef FEAT_XFONTSET */

/* Define if you want to include XIM support. */
/* #undef FEAT_XIM */

/* Define if you use GTK and want GNOME support. */
/* #undef FEAT_GUI_GNOME */

/* Define if you use KDE and want KDE Toolbar support. */
/* #undef FEAT_KDETOOLBAR */

/* Define if your X has own locale library */
/* #undef X_LOCALE */

/* Define if we have dlfcn.h. */
#define HAVE_DLFCN_H 1

/* Define if there is a working gettext(). */
/* #undef HAVE_GETTEXT */

/* Define if _nl_msg_cat_cntr is present. */
/* #undef HAVE_NL_MSG_CAT_CNTR */

/* Define if we have dlopen() */
#define HAVE_DLOPEN 1

/* Define if we have dlsym() */
#define HAVE_DLSYM 1

/* Define if we have dl.h. */
/* #undef HAVE_DL_H */

/* Define if we have shl_load() */
/* #undef HAVE_SHL_LOAD */

/* Define if we can use IPv6 networking. */
#define FEAT_IPV6 1

/* Define if you want to include NetBeans integration. */
#define FEAT_NETBEANS_INTG 1

/* Define if you want to include process communication. */
#define FEAT_JOB_CHANNEL 1

/* Define if you want to include terminal emulator support. */
#define FEAT_TERMINAL 1

// Define default global runtime path.
/* #undef RUNTIME_GLOBAL */

// Define default global runtime after path.
/* #undef RUNTIME_GLOBAL_AFTER */

/* Define name of who modified a released Vim */
/* #undef MODIFIED_BY */

/* Define if you want XSMP interaction as well as vanilla swapfile safety */
#define USE_XSMP_INTERACT 1

/* Define if fcntl()'s F_SETFD command knows about FD_CLOEXEC */
#define HAVE_FD_CLOEXEC 1

/* Define if /proc/self/exe or similar can be read */
/* #undef PROC_EXE_LINK */

/* Define if you want Cygwin to use the WIN32 clipboard, not compatible with X11*/
/* #undef FEAT_CYGWIN_WIN32_CLIPBOARD */

/* Define if we have AvailabilityMacros.h on Mac OS X */
#define HAVE_AVAILABILITYMACROS_H 1

/* Define if Xutf8SetWMProperties() is in an X library. */
/* #undef HAVE_XUTF8SETWMPROPERTIES */

/* Define if there is a copyfile() */
#define HAVE_COPYFILE 1

/* Define if there is a copyfile.h */
#define HAVE_COPYFILE_H 1

/* Define if GResource is used to load icons */
/* #undef USE_GRESOURCE */

/* Define if GTK+ GUI is to be linked against GTK+ 3 */
/* #undef USE_GTK3 */

/* Define if we have isinf() */
#define HAVE_ISINF 1

/* Define if we have isnan() */
#define HAVE_ISNAN 1

/* Define if we have dirfd() */
#define HAVE_DIRFD 1

/* Define if we have flock() */
#define HAVE_FLOCK 1

/* Define to inline symbol or empty */
/* #undef inline */

/* Define if _SC_SIGSTKSZ is available via sysconf() */
/* #undef HAVE_SYSCONF_SIGSTKSZ */

/* Define if you want to load libgpm dynamically */
/* #undef DYNAMIC_GPM */
