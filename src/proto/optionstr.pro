/* optionstr.c */
void didset_string_options(void);
void trigger_optionset_string(int opt_idx, int opt_flags, char_u *oldval, char_u *oldval_l, char_u *oldval_g, char_u *newval);
void check_buf_options(buf_T *buf);
void free_string_option(char_u *p);
void clear_string_option(char_u **pp);
void check_string_option(char_u **pp);
void set_string_option_direct(char_u *name, int opt_idx, char_u *val, int opt_flags, int set_sid);
void set_string_option_direct_in_win(win_T *wp, char_u *name, int opt_idx, char_u *val, int opt_flags, int set_sid);
void set_string_option_direct_in_buf(buf_T *buf, char_u *name, int opt_idx, char_u *val, int opt_flags, int set_sid);
char *set_string_option(int opt_idx, char_u *value, int opt_flags, char *errbuf);
char *did_set_string_option(int opt_idx, char_u **varp, char_u *oldval, char *errbuf, int opt_flags, int *value_checked);
int check_ff_value(char_u *p);
void save_clear_shm_value(void);
void restore_shm_value(void);
/* vim: set ft=c : */
