" Tests for decoding escape sequences sent by the terminal.

" This only works for Unix in a terminal
source check.vim
CheckNotGui
CheckUnix

source shared.vim

" xterm2 and sgr always work, urxvt is optional.
let s:ttymouse_values = ['xterm2', 'sgr']
if has('mouse_urxvt')
  call add(s:ttymouse_values, 'urxvt')
endif

" dec doesn't support all the functionality
if has('mouse_dec')
  let s:ttymouse_dec = ['dec']
else
  let s:ttymouse_dec = []
endif

" netterm only supports left click
if has('mouse_netterm')
  let s:ttymouse_netterm = ['netterm']
else
  let s:ttymouse_netterm = []
endif

" Helper function to emit a terminal escape code.
func TerminalEscapeCode(code, row, col, m)
  if &ttymouse ==# 'xterm2'
    " need to use byte encoding here.
    let str = list2str([a:code + 0x20, a:col + 0x20, a:row + 0x20])
    if has('iconv')
      let bytes = str->iconv('utf-8', 'latin1')
    else
      " Hopefully the numbers are not too big.
      let bytes = str
    endif
    call feedkeys("\<Esc>[M" .. bytes, 'Lx!')
  elseif &ttymouse ==# 'sgr'
    call feedkeys(printf("\<Esc>[<%d;%d;%d%s", a:code, a:col, a:row, a:m), 'Lx!')
  elseif &ttymouse ==# 'urxvt'
    call feedkeys(printf("\<Esc>[%d;%d;%dM", a:code + 0x20, a:col, a:row), 'Lx!')
  endif
endfunc

func DecEscapeCode(code, down, row, col)
    call feedkeys(printf("\<Esc>[%d;%d;%d;%d&w", a:code, a:down, a:row, a:col), 'Lx!')
endfunc

func NettermEscapeCode(row, col)
    call feedkeys(printf("\<Esc>}%d,%d\r", a:row, a:col), 'Lx!')
endfunc

func MouseLeftClick(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(2, 4, a:row, a:col)
  elseif &ttymouse ==# 'netterm'
    call NettermEscapeCode(a:row, a:col)
  else
    call TerminalEscapeCode(0, a:row, a:col, 'M')
  endif
endfunc

func MouseMiddleClick(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(4, 2, a:row, a:col)
  else
    call TerminalEscapeCode(1, a:row, a:col, 'M')
  endif
endfunc

func MouseRightClick(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(6, 1, a:row, a:col)
  else
    call TerminalEscapeCode(2, a:row, a:col, 'M')
  endif
endfunc

func MouseCtrlLeftClick(row, col)
  let ctrl = 0x10
  call TerminalEscapeCode(0 + ctrl, a:row, a:col, 'M')
endfunc

func MouseCtrlRightClick(row, col)
  let ctrl = 0x10
  call TerminalEscapeCode(2 + ctrl, a:row, a:col, 'M')
endfunc

func MouseLeftRelease(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(3, 0, a:row, a:col)
  elseif &ttymouse ==# 'netterm'
    " send nothing
  else
    call TerminalEscapeCode(3, a:row, a:col, 'm')
  endif
endfunc

func MouseMiddleRelease(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(5, 0, a:row, a:col)
  else
    call TerminalEscapeCode(3, a:row, a:col, 'm')
  endif
endfunc

func MouseRightRelease(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(7, 0, a:row, a:col)
  else
    call TerminalEscapeCode(3, a:row, a:col, 'm')
  endif
endfunc

func MouseLeftDrag(row, col)
  if &ttymouse ==# 'dec'
    call DecEscapeCode(1, 4, a:row, a:col)
  else
    call TerminalEscapeCode(0x20, a:row, a:col, 'M')
  endif
endfunc

func MouseWheelUp(row, col)
  call TerminalEscapeCode(0x40, a:row, a:col, 'M')
endfunc

func MouseWheelDown(row, col)
  call TerminalEscapeCode(0x41, a:row, a:col, 'M')
endfunc

func Test_term_mouse_left_click()
  new
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  set mouse=a term=xterm
  call setline(1, ['line 1', 'line 2', 'line 3 is a bit longer'])

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec + s:ttymouse_netterm
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    go
    call assert_equal([0, 1, 1, 0], getpos('.'), msg)
    let row = 2
    let col = 6
    call MouseLeftClick(row, col)
    call MouseLeftRelease(row, col)
    call assert_equal([0, 2, 6, 0], getpos('.'), msg)
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  bwipe!
endfunc

func Test_xterm_mouse_right_click_extends_visual()
  if has('mac')
    throw "Skipped: test right click in visual mode does not work on macOs (why?)"
  endif
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  set mouse=a term=xterm

  for visual_mode in ["v", "V", "\<C-V>"]
    for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
      let msg = 'visual=' .. visual_mode .. ' ttymouse=' .. ttymouse_val
      exe 'set ttymouse=' .. ttymouse_val

      call setline(1, repeat([repeat('-', 7)], 7))
      call MouseLeftClick(4, 4)
      call MouseLeftRelease(4, 4)
      exe  "norm! " .. visual_mode

      " Right click extends top left of visual area.
      call MouseRightClick(2, 2)
      call MouseRightRelease(2, 2)

      " Right click extends bottom bottom right of visual area.
      call MouseRightClick(6, 6)
      call MouseRightRelease(6, 6)
      norm! r1gv

      " Right click shrinks top left of visual area.
      call MouseRightClick(3, 3)
      call MouseRightRelease(3, 3)

      " Right click shrinks bottom right of visual area.
      call MouseRightClick(5, 5)
      call MouseRightRelease(5, 5)
      norm! r2

      if visual_mode ==# 'v'
        call assert_equal(['-------',
              \            '-111111',
              \            '1122222',
              \            '2222222',
              \            '2222211',
              \            '111111-',
              \            '-------'], getline(1, '$'), msg)
      elseif visual_mode ==# 'V'
        call assert_equal(['-------',
              \            '1111111',
              \            '2222222',
              \            '2222222',
              \            '2222222',
              \            '1111111',
              \            '-------'], getline(1, '$'), msg)
      else
        call assert_equal(['-------',
              \            '-11111-',
              \            '-12221-',
              \            '-12221-',
              \            '-12221-',
              \            '-11111-',
              \            '-------'], getline(1, '$'), msg)
      endif
    endfor
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  bwipe!
endfunc

" Test that <C-LeftMouse> jumps to help tag and <C-RightMouse> jumps back.
func Test_xterm_mouse_ctrl_click()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  set mouse=a term=xterm

  for ttymouse_val in s:ttymouse_values
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    help
    /usr_02.txt
    norm! zt
    let row = 1
    let col = 1
    call MouseCtrlLeftClick(row, col)
    call MouseLeftRelease(row, col)
    call assert_match('usr_02.txt$', bufname('%'), msg)
    call assert_equal('*usr_02.txt*', expand('<cWORD>'), msg)

    call MouseCtrlRightClick(row, col)
    call MouseRightRelease(row, col)
    call assert_match('help.txt$', bufname('%'), msg)
    call assert_equal('|usr_02.txt|', expand('<cWORD>'), msg)

    helpclose
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
endfunc

func Test_term_mouse_middle_click()
  CheckFeature clipboard_working

  new
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  let save_quotestar = @*
  let @* = 'abc'
  set mouse=a term=xterm

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    call setline(1, ['123456789', '123456789'])

    " Middle-click in the middle of the line pastes text where clicked.
    let row = 1
    let col = 6
    call MouseMiddleClick(row, col)
    call MouseMiddleRelease(row, col)
    call assert_equal(['12345abc6789', '123456789'], getline(1, '$'), msg)

    " Middle-click beyond end of the line pastes text at the end of the line.
    let col = 20
    call MouseMiddleClick(row, col)
    call MouseMiddleRelease(row, col)
    call assert_equal(['12345abc6789abc', '123456789'], getline(1, '$'), msg)

    " Middle-click beyond the last line pastes in the last line.
    let row = 5
    let col = 3
    call MouseMiddleClick(row, col)
    call MouseMiddleRelease(row, col)
    call assert_equal(['12345abc6789abc', '12abc3456789'], getline(1, '$'), msg)
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  let @* = save_quotestar
  bwipe!
endfunc

" TODO: for unclear reasons this test fails if it comes after
" Test_xterm_mouse_ctrl_click()
func Test_1xterm_mouse_wheel()
  new
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  set mouse=a term=xterm
  call setline(1, range(1, 100))

  for ttymouse_val in s:ttymouse_values
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    go
    call assert_equal(1, line('w0'), msg)
    call assert_equal([0, 1, 1, 0], getpos('.'), msg)

    call MouseWheelDown(1, 1)
    call assert_equal(4, line('w0'), msg)
    call assert_equal([0, 4, 1, 0], getpos('.'), msg)

    call MouseWheelDown(1, 1)
    call assert_equal(7, line('w0'), msg)
    call assert_equal([0, 7, 1, 0], getpos('.'), msg)

    call MouseWheelUp(1, 1)
    call assert_equal(4, line('w0'), msg)
    call assert_equal([0, 7, 1, 0], getpos('.'), msg)

    call MouseWheelUp(1, 1)
    call assert_equal(1, line('w0'), msg)
    call assert_equal([0, 7, 1, 0], getpos('.'), msg)
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  bwipe!
endfunc

" Test that dragging beyond the window (at the bottom and at the top)
" scrolls window content by the number of of lines beyond the window.
func Test_term_mouse_drag_beyond_window()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  set mouse=a term=xterm
  let col = 1
  call setline(1, range(1, 100))

  " Split into 3 windows, and go into the middle window
  " so we test dragging mouse below and above the window.
  2split
  wincmd j
  2split

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val

    " Line #10 at the top.
    norm! 10zt
    redraw
    call assert_equal(10, winsaveview().topline, msg)
    call assert_equal(2, winheight(0), msg)

    let row = 4
    call MouseLeftClick(row, col)
    call assert_equal(10, winsaveview().topline, msg)

    " Drag downwards. We're still in the window so topline should
    " not change yet.
    let row += 1
    call MouseLeftDrag(row, col)
    call assert_equal(10, winsaveview().topline, msg)

    " We now leave the window at the bottom, so the window content should
    " scroll by 1 line, then 2 lines (etc) as we drag further away.
    let row += 1
    call MouseLeftDrag(row, col)
    call assert_equal(11, winsaveview().topline, msg)

    let row += 1
    call MouseLeftDrag(row, col)
    call assert_equal(13, winsaveview().topline, msg)

    " Now drag upwards.
    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(14, winsaveview().topline, msg)

    " We're now back in the window so the topline should not change.
    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(14, winsaveview().topline, msg)

    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(14, winsaveview().topline, msg)

    " We now leave the window at the top so the window content should
    " scroll by 1 line, then 2, then 3 (etc) in the opposite direction.
    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(13, winsaveview().topline, msg)

    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(11, winsaveview().topline, msg)

    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(8, winsaveview().topline, msg)

    call MouseLeftRelease(row, col)
    call assert_equal(8, winsaveview().topline, msg)
    call assert_equal(2, winheight(0), msg)
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  bwipe!
endfunc

func Test_term_mouse_drag_window_separator()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  set mouse=a term=xterm

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val

    " Split horizontally and test dragging the horizontal window separator.
    split
    let rowseparator = winheight(0) + 1
    let row = rowseparator
    let col = 1

    " When 'ttymouse' is 'xterm2', row/col bigger than 223 are not supported.
    if ttymouse_val !=# 'xterm2' || row <= 223
      call MouseLeftClick(row, col)
      let row -= 1
      call MouseLeftDrag(row, col)
      call assert_equal(rowseparator - 1, winheight(0) + 1, msg)
      let row += 1
      call MouseLeftDrag(row, col)
      call assert_equal(rowseparator, winheight(0) + 1, msg)
      call MouseLeftRelease(row, col)
      call assert_equal(rowseparator, winheight(0) + 1, msg)
    endif
    bwipe!

    " Split vertically and test dragging the vertical window separator.
    vsplit
    let colseparator = winwidth(0) + 1
    let row = 1
    let col = colseparator

    " When 'ttymouse' is 'xterm2', row/col bigger than 223 are not supported.
    if ttymouse_val !=# 'xterm2' || col <= 223
      call MouseLeftClick(row, col)
      let col -= 1
      call MouseLeftDrag(row, col)
      call assert_equal(colseparator - 1, winwidth(0) + 1, msg)
      let col += 1
      call MouseLeftDrag(row, col)
      call assert_equal(colseparator, winwidth(0) + 1, msg)
      call MouseLeftRelease(row, col)
      call assert_equal(colseparator, winwidth(0) + 1, msg)
    endif
    bwipe!
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
endfunc

func Test_term_mouse_drag_statusline()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  let save_laststatus = &laststatus
  set mouse=a term=xterm laststatus=2

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val

    call assert_equal(1, &cmdheight, msg)
    let rowstatusline = winheight(0) + 1
    let row = rowstatusline
    let col = 1

    if ttymouse_val ==# 'xterm2' && row > 223
      " When 'ttymouse' is 'xterm2', row/col bigger than 223 are not supported.
      continue
    endif

    call MouseLeftClick(row, col)
    let row -= 1
    call MouseLeftDrag(row, col)
    call assert_equal(2, &cmdheight, msg)
    call assert_equal(rowstatusline - 1, winheight(0) + 1, msg)
    let row += 1
    call MouseLeftDrag(row, col)
    call assert_equal(1, &cmdheight, msg)
    call assert_equal(rowstatusline, winheight(0) + 1, msg)
    call MouseLeftRelease(row, col)
    call assert_equal(1, &cmdheight, msg)
    call assert_equal(rowstatusline, winheight(0) + 1, msg)
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  let &laststatus = save_laststatus
endfunc

func Test_term_mouse_click_tab()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  set mouse=a term=xterm
  let row = 1

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec + s:ttymouse_netterm
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    e Xfoo
    tabnew Xbar

    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xfoo',
        \              'Tab page 2',
        \              '>   Xbar'], a, msg)

    " Test clicking on tab names in the tabline at the top.
    let col = 2
    redraw
    call MouseLeftClick(row, col)
    call MouseLeftRelease(row, col)
    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '>   Xfoo',
        \              'Tab page 2',
        \              '    Xbar'], a, msg)

    let col = 9
    call MouseLeftClick(row, col)
    call MouseLeftRelease(row, col)
    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xfoo',
        \              'Tab page 2',
        \              '>   Xbar'], a, msg)

    %bwipe!
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
endfunc

func Test_term_mouse_click_X_to_close_tab()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  set mouse=a term=xterm
  let row = 1
  let col = &columns

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec + s:ttymouse_netterm
    if ttymouse_val ==# 'xterm2' && col > 223
      " When 'ttymouse' is 'xterm2', row/col bigger than 223 are not supported.
      continue
    endif
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    e Xtab1
    tabnew Xtab2
    tabnew Xtab3
    tabn 2

    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xtab1',
        \              'Tab page 2',
        \              '>   Xtab2',
        \              'Tab page 3',
        \              '    Xtab3'], a, msg)

    " Click on "X" in tabline to close current tab i.e. Xtab2.
    redraw
    call MouseLeftClick(row, col)
    call MouseLeftRelease(row, col)
    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xtab1',
        \              'Tab page 2',
        \              '>   Xtab3'], a, msg)

    %bwipe!
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
endfunc

func Test_term_mouse_drag_to_move_tab()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  " Set 'mousetime' to 1 to avoid recognizing a double-click in the loop
  set mouse=a term=xterm mousetime=1
  let row = 1

  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    e Xtab1
    tabnew Xtab2

    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xtab1',
        \              'Tab page 2',
        \              '>   Xtab2'], a, msg)
    redraw

    " Click in tab2 and drag it to tab1.
    " Check getcharmod() to verify that click is not
    " interpreted as a spurious double-click.
    call MouseLeftClick(row, 10)
    call assert_equal(0, getcharmod(), msg)
    for col in [9, 8, 7, 6]
      call MouseLeftDrag(row, col)
    endfor
    call MouseLeftRelease(row, col)
    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '>   Xtab2',
        \              'Tab page 2',
        \              '    Xtab1'], a, msg)

    " brief sleep to avoid causing a double-click
    sleep 20m
    %bwipe!
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  set mousetime&
endfunc

func Test_term_mouse_double_click_to_create_tab()
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  call test_override('no_query_mouse', 1)
  " Set 'mousetime' to a small value, so that double-click works but we don't
  " have to wait long to avoid a triple-click.
  set mouse=a term=xterm mousetime=100
  let row = 1
  let col = 10

  let round = 0
  for ttymouse_val in s:ttymouse_values + s:ttymouse_dec
    let msg = 'ttymouse=' .. ttymouse_val
    exe 'set ttymouse=' .. ttymouse_val
    e Xtab1
    tabnew Xtab2

    if round > 0
      " We need to sleep, or else the first MouseLeftClick() will be
      " interpreted as a spurious triple-click.
      sleep 100m
    endif
    let round += 1

    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xtab1',
        \              'Tab page 2',
        \              '>   Xtab2'], a, msg)

    redraw
    call MouseLeftClick(row, col)
    " Check getcharmod() to verify that first click is not
    " interpreted as a spurious double-click.
    call assert_equal(0, getcharmod(), msg)
    call MouseLeftRelease(row, col)
    call MouseLeftClick(row, col)
    call assert_equal(32, getcharmod(), msg) " double-click
    call MouseLeftRelease(row, col)
    let a = split(execute(':tabs'), "\n")
    call assert_equal(['Tab page 1',
        \              '    Xtab1',
        \              'Tab page 2',
        \              '>   [No Name]',
        \              'Tab page 3',
        \              '    Xtab2'], a, msg)

    %bwipe!
  endfor

  let &mouse = save_mouse
  let &term = save_term
  let &ttymouse = save_ttymouse
  call test_override('no_query_mouse', 0)
  set mousetime&
endfunc

func Test_xterm_mouse_click_in_fold_columns()
  new
  let save_mouse = &mouse
  let save_term = &term
  let save_ttymouse = &ttymouse
  let save_foldcolumn = &foldcolumn
  set mouse=a term=xterm foldcolumn=3 ttymouse=xterm2

  " Create 2 nested folds.
  call setline(1, range(1, 7))
  2,6fold
  norm! zR
  4,5fold
  call assert_equal([-1, -1, -1, 4, 4, -1, -1],
        \           map(range(1, 7), 'foldclosed(v:val)'))

  " Click in "+" of inner fold in foldcolumn should open it.
  redraw
  let row = 4
  let col = 2
  call MouseLeftClick(row, col)
  call MouseLeftRelease(row, col)
  call assert_equal([-1, -1, -1, -1, -1, -1, -1],
        \           map(range(1, 7), 'foldclosed(v:val)'))

  " Click in "-" of outer fold in foldcolumn should close it.
  redraw
  let row = 2
  let col = 1
  call MouseLeftClick(row, col)
  call MouseLeftRelease(row, col)
  call assert_equal([-1, 2, 2, 2, 2, 2, -1],
        \           map(range(1, 7), 'foldclosed(v:val)'))
  norm! zR

  " Click in "|" of inner fold in foldcolumn should close it.
  redraw
  let row = 5
  let col = 2
  call MouseLeftClick(row, col)
  call MouseLeftRelease(row, col)
  call assert_equal([-1, -1, -1, 4, 4, -1, -1],
        \           map(range(1, 7), 'foldclosed(v:val)'))

  let &foldcolumn = save_foldcolumn
  let &ttymouse = save_ttymouse
  let &term = save_term
  let &mouse = save_mouse
  bwipe!
endfunc

" This only checks if the sequence is recognized.
func Test_term_rgb_response()
  set t_RF=x
  set t_RB=y

  " response to t_RF, 4 digits
  let red = 0x12
  let green = 0x34
  let blue = 0x56
  let seq = printf("\<Esc>]10;rgb:%02x00/%02x00/%02x00\x07", red, green, blue)
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termrfgresp)

  " response to t_RF, 2 digits
  let red = 0x78
  let green = 0x9a
  let blue = 0xbc
  let seq = printf("\<Esc>]10;rgb:%02x/%02x/%02x\x07", red, green, blue)
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termrfgresp)

  " response to t_RB, 4 digits, dark
  set background=light
  eval 'background'->test_option_not_set()
  let red = 0x29
  let green = 0x4a
  let blue = 0x6b
  let seq = printf("\<Esc>]11;rgb:%02x00/%02x00/%02x00\x07", red, green, blue)
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termrbgresp)
  call assert_equal('dark', &background)

  " response to t_RB, 4 digits, light
  set background=dark
  call test_option_not_set('background')
  let red = 0x81
  let green = 0x63
  let blue = 0x65
  let seq = printf("\<Esc>]11;rgb:%02x00/%02x00/%02x00\x07", red, green, blue)
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termrbgresp)
  call assert_equal('light', &background)

  " response to t_RB, 2 digits, dark
  set background=light
  call test_option_not_set('background')
  let red = 0x47
  let green = 0x59
  let blue = 0x5b
  let seq = printf("\<Esc>]11;rgb:%02x/%02x/%02x\x07", red, green, blue)
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termrbgresp)
  call assert_equal('dark', &background)
  
  " response to t_RB, 2 digits, light
  set background=dark
  call test_option_not_set('background')
  let red = 0x83
  let green = 0xa4
  let blue = 0xc2
  let seq = printf("\<Esc>]11;rgb:%02x/%02x/%02x\x07", red, green, blue)
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termrbgresp)
  call assert_equal('light', &background)
  
  set t_RF= t_RB=
endfunc

" This only checks if the sequence is recognized.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx01_term_style_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  " send the termresponse to trigger requesting the XT codes
  let seq = "\<Esc>[>41;337;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)

  let seq = "\<Esc>P1$r2 q\<Esc>\\"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termstyleresp)

  set t_RV=
endfunc

" This checks the iTerm2 version response.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx02_iTerm2_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  " Old versions of iTerm2 used a different style term response.
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>0;95;c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('xterm', &ttymouse)

  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>0;95;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  set t_RV=
endfunc

" This checks the libvterm version response.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx03_libvterm_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>0;100;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  set t_RV=
endfunc

" This checks the Mac Terminal.app version response.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx04_Mac_Terminal_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>1;95;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  " Reset is_not_xterm and is_mac_terminal.
  set t_RV=
  set term=xterm
  set t_RV=x
endfunc

" This checks the mintty version response.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx05_mintty_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>77;20905;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  set t_RV=
endfunc

" This checks the screen version response.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx06_screen_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  " Old versions of screen don't support SGR mouse mode.
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>83;40500;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('xterm', &ttymouse)

  " screen supports SGR mouse mode starting in version 4.7.
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>83;40700;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  set t_RV=
endfunc

" This checks the xterm version response.
" This must be after other tests, because it has side effects to xterm
" properties.
func Test_xx07_xterm_response()
  " Termresponse is only parsed when t_RV is not empty.
  set t_RV=x

  " Do Terminal.app first to check that is_mac_terminal is reset.
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>1;95;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  " xterm < 95: "xterm" (actually unmodified)
  set t_RV=
  set term=xterm
  set t_RV=x
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>0;94;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('xterm', &ttymouse)

  " xterm >= 95 < 277 "xterm2"
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>0;267;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('xterm2', &ttymouse)

  " xterm >= 277: "sgr"
  set ttymouse=xterm
  call test_option_not_set('ttymouse')
  let seq = "\<Esc>[>0;277;0c"
  call feedkeys(seq, 'Lx!')
  call assert_equal(seq, v:termresponse)
  call assert_equal('sgr', &ttymouse)

  set t_RV=
endfunc

func Test_get_termcode()
  try
    let k1 = &t_k1
  catch /E113/
    throw 'Skipped: Unable to query termcodes'
  endtry
  set t_k1=
  set t_k1&
  call assert_equal(k1, &t_k1)

  " use external termcap first
  set nottybuiltin
  set t_k1=
  set t_k1&
  " when using external termcap may get something else, but it must not be
  " empty, since we would fallback to the builtin one.
  call assert_notequal('', &t_k1)

  if &term =~ 'xterm'
    " use internal termcap first
    let term_save = &term
    let &term = 'builtin_' .. &term
    set t_k1=
    set t_k1&
    call assert_equal(k1, &t_k1)
    let &term = term_save
  endif

  set ttybuiltin
endfunc

func GetEscCodeCSI27(key, modifier)
  let key = printf("%d", char2nr(a:key))
  let mod = printf("%d", a:modifier)
  return "\<Esc>[27;" .. mod .. ';' .. key .. '~'
endfunc

func GetEscCodeCSIu(key, modifier)
  let key = printf("%d", char2nr(a:key))
  let mod = printf("%d", a:modifier)
  return "\<Esc>[" .. key .. ';' .. mod .. 'u'
endfunc

" This checks the CSI sequences when in modifyOtherKeys mode.
" The mode doesn't need to be enabled, the codes are always detected.
func RunTest_modifyOtherKeys(func)
  new
  set timeoutlen=10

  " Shift-X is send as 'X' with the shift modifier
  call feedkeys('a' .. a:func('X', 2) .. "\<Esc>", 'Lx!')
  call assert_equal('X', getline(1))

  " Ctrl-i is Tab
  call setline(1, '')
  call feedkeys('a' .. a:func('i', 5) .. "\<Esc>", 'Lx!')
  call assert_equal("\t", getline(1))

  " Ctrl-I is also Tab
  call setline(1, '')
  call feedkeys('a' .. a:func('I', 5) .. "\<Esc>", 'Lx!')
  call assert_equal("\t", getline(1))

  " Alt-x is ø
  call setline(1, '')
  call feedkeys('a' .. a:func('x', 3) .. "\<Esc>", 'Lx!')
  call assert_equal("ø", getline(1))

  " Meta-x is also ø
  call setline(1, '')
  call feedkeys('a' .. a:func('x', 9) .. "\<Esc>", 'Lx!')
  call assert_equal("ø", getline(1))

  " Alt-X is Ø
  call setline(1, '')
  call feedkeys('a' .. a:func('X', 3) .. "\<Esc>", 'Lx!')
  call assert_equal("Ø", getline(1))

  " Meta-X is ø
  call setline(1, '')
  call feedkeys('a' .. a:func('X', 9) .. "\<Esc>", 'Lx!')
  call assert_equal("Ø", getline(1))

  bwipe!
  set timeoutlen&
endfunc

func Test_modifyOtherKeys_basic()
  call RunTest_modifyOtherKeys(function('GetEscCodeCSI27'))
  call RunTest_modifyOtherKeys(function('GetEscCodeCSIu'))
endfunc

func RunTest_mapping_shift(key, func)
  call setline(1, '')
  if a:key == '|'
    exe 'inoremap \| xyz'
  else
    exe 'inoremap ' .. a:key .. ' xyz'
  endif
  call feedkeys('a' .. a:func(a:key, 2) .. "\<Esc>", 'Lx!')
  call assert_equal("xyz", getline(1))
  if a:key == '|'
    exe 'iunmap \|'
  else
    exe 'iunmap ' .. a:key
  endif
endfunc

func RunTest_mapping_works_with_shift(func)
  new
  set timeoutlen=10

  call RunTest_mapping_shift('@', a:func)
  call RunTest_mapping_shift('A', a:func)
  call RunTest_mapping_shift('Z', a:func)
  call RunTest_mapping_shift('^', a:func)
  call RunTest_mapping_shift('_', a:func)
  call RunTest_mapping_shift('{', a:func)
  call RunTest_mapping_shift('|', a:func)
  call RunTest_mapping_shift('}', a:func)
  call RunTest_mapping_shift('~', a:func)

  bwipe!
  set timeoutlen&
endfunc

func Test_mapping_works_with_shift_plain()
  call RunTest_mapping_works_with_shift(function('GetEscCodeCSI27'))
  call RunTest_mapping_works_with_shift(function('GetEscCodeCSIu'))
endfunc

func RunTest_mapping_mods(map, key, func, code)
  call setline(1, '')
  exe 'inoremap ' .. a:map .. ' xyz'
  call feedkeys('a' .. a:func(a:key, a:code) .. "\<Esc>", 'Lx!')
  call assert_equal("xyz", getline(1))
  exe 'iunmap ' .. a:map
endfunc

func RunTest_mapping_works_with_mods(func, mods, code)
  new
  set timeoutlen=10

  if a:mods !~ 'S'
    " Shift by itself has no effect
    call RunTest_mapping_mods('<' .. a:mods .. '-@>', '@', a:func, a:code)
  endif
  call RunTest_mapping_mods('<' .. a:mods .. '-A>', 'A', a:func, a:code)
  call RunTest_mapping_mods('<' .. a:mods .. '-Z>', 'Z', a:func, a:code)
  if a:mods !~ 'S'
    " with Shift code is always upper case
    call RunTest_mapping_mods('<' .. a:mods .. '-a>', 'a', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-z>', 'z', a:func, a:code)
  endif
  if a:mods != 'A'
    " with Alt code is not in upper case
    call RunTest_mapping_mods('<' .. a:mods .. '-a>', 'A', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-z>', 'Z', a:func, a:code)
  endif
  call RunTest_mapping_mods('<' .. a:mods .. '-á>', 'á', a:func, a:code)
  if a:mods !~ 'S'
    " Shift by itself has no effect
    call RunTest_mapping_mods('<' .. a:mods .. '-^>', '^', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-_>', '_', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-{>', '{', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-\|>', '|', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-}>', '}', a:func, a:code)
    call RunTest_mapping_mods('<' .. a:mods .. '-~>', '~', a:func, a:code)
  endif

  bwipe!
  set timeoutlen&
endfunc

func Test_mapping_works_with_shift()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'S', 2)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'S', 2)
endfunc
  
func Test_mapping_works_with_ctrl()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'C', 5)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'C', 5)
endfunc

func Test_mapping_works_with_shift_ctrl()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'C-S', 6)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'C-S', 6)
endfunc

" Below we also test the "u" code with Alt, This works, but libvterm would not
" send the Alt key like this but by prefixing an Esc.
  
func Test_mapping_works_with_alt()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'A', 3)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'A', 3)
endfunc

func Test_mapping_works_with_shift_alt()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'S-A', 4)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'S-A', 4)
endfunc

func Test_mapping_works_with_ctrl_alt()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'C-A', 7)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'C-A', 7)
endfunc

func Test_mapping_works_with_shift_ctrl_alt()
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSI27'), 'C-S-A', 8)
  call RunTest_mapping_works_with_mods(function('GetEscCodeCSIu'), 'C-S-A', 8)
endfunc
