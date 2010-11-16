/* gui.c */
void gui_start __ARGS((void));
void gui_prepare __ARGS((int *argc, char **argv));
int gui_init_check __ARGS((void));
void gui_init __ARGS((void));
void gui_exit __ARGS((int rc));
void gui_shell_closed __ARGS((void));
int gui_init_font __ARGS((char_u *font_list, int fontset));
int gui_get_wide_font __ARGS((void));
void gui_set_cursor __ARGS((int row, int col));
void gui_update_cursor __ARGS((int force, int clear_selection));
void gui_position_menu __ARGS((void));
int gui_get_base_width __ARGS((void));
int gui_get_base_height __ARGS((void));
void gui_resize_shell __ARGS((int pixel_width, int pixel_height));
void gui_may_resize_shell __ARGS((void));
int gui_get_shellsize __ARGS((void));
void gui_set_shellsize __ARGS((int mustset, int fit_to_display, int direction));
void gui_new_shellsize __ARGS((void));
void gui_reset_scroll_region __ARGS((void));
void gui_start_highlight __ARGS((int mask));
void gui_stop_highlight __ARGS((int mask));
void gui_clear_block __ARGS((int row1, int col1, int row2, int col2));
void gui_update_cursor_later __ARGS((void));
void gui_write __ARGS((char_u *s, int len));
void gui_dont_update_cursor __ARGS((void));
void gui_can_update_cursor __ARGS((void));
int gui_outstr_nowrap __ARGS((char_u *s, int len, int flags, guicolor_T fg, guicolor_T bg, int back));
void gui_undraw_cursor __ARGS((void));
void gui_redraw __ARGS((int x, int y, int w, int h));
int gui_redraw_block __ARGS((int row1, int col1, int row2, int col2, int flags));
int gui_wait_for_chars __ARGS((long wtime));
void gui_send_mouse_event __ARGS((int button, int x, int y, int repeated_click, int_u modifiers));
int gui_xy2colrow __ARGS((int x, int y, int *colp));
void gui_menu_cb __ARGS((vimmenu_T *menu));
void gui_init_which_components __ARGS((char_u *oldval));
int gui_use_tabline __ARGS((void));
void gui_update_tabline __ARGS((void));
void get_tabline_label __ARGS((tabpage_T *tp, int tooltip));
int send_tabline_event __ARGS((int nr));
void send_tabline_menu_event __ARGS((int tabidx, int event));
void gui_remove_scrollbars __ARGS((void));
void gui_create_scrollbar __ARGS((scrollbar_T *sb, int type, win_T *wp));
scrollbar_T *gui_find_scrollbar __ARGS((long ident));
void gui_drag_scrollbar __ARGS((scrollbar_T *sb, long value, int still_dragging));
void gui_may_update_scrollbars __ARGS((void));
void gui_update_scrollbars __ARGS((int force));
int gui_do_scroll __ARGS((void));
int gui_do_horiz_scroll __ARGS((long_u leftcol, int compute_longest_lnum));
void gui_check_colors __ARGS((void));
guicolor_T gui_get_color __ARGS((char_u *name));
int gui_get_lightness __ARGS((guicolor_T pixel));
void gui_new_scrollbar_colors __ARGS((void));
void gui_focus_change __ARGS((int in_focus));
void gui_mouse_moved __ARGS((int x, int y));
void gui_mouse_correct __ARGS((void));
void ex_gui __ARGS((exarg_T *eap));
int gui_find_bitmap __ARGS((char_u *name, char_u *buffer, char *ext));
void gui_find_iconfile __ARGS((char_u *name, char_u *buffer, char *ext));
void display_errors __ARGS((void));
int no_console_input __ARGS((void));
void gui_update_screen __ARGS((void));
char_u *get_find_dialog_text __ARGS((char_u *arg, int *wwordp, int *mcasep));
int gui_do_findrepl __ARGS((int flags, char_u *find_text, char_u *repl_text, int down));
void gui_handle_drop __ARGS((int x, int y, int_u modifiers, char_u **fnames, int count));
/* vim: set ft=c : */
