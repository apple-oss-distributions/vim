
func Test_yank_shows_register()
    enew
    set report=0
    call setline(1, ['foo', 'bar'])
    " Line-wise
    exe 'norm! yy'
    call assert_equal('1 line yanked', v:statusmsg)
    exe 'norm! "zyy'
    call assert_equal('1 line yanked into "z', v:statusmsg)
    exe 'norm! yj'
    call assert_equal('2 lines yanked', v:statusmsg)
    exe 'norm! "zyj'
    call assert_equal('2 lines yanked into "z', v:statusmsg)

    " Block-wise
    exe "norm! \<C-V>y"
    call assert_equal('block of 1 line yanked', v:statusmsg)
    exe "norm! \<C-V>\"zy"
    call assert_equal('block of 1 line yanked into "z', v:statusmsg)
    exe "norm! \<C-V>jy"
    call assert_equal('block of 2 lines yanked', v:statusmsg)
    exe "norm! \<C-V>j\"zy"
    call assert_equal('block of 2 lines yanked into "z', v:statusmsg)

    bwipe!
endfunc

func Test_display_registers()
    e file1
    e file2
    call setline(1, ['foo', 'bar'])
    /bar
    exe 'norm! y2l"axx'
    call feedkeys("i\<C-R>=2*4\n\<esc>")
    call feedkeys(":ls\n", 'xt')

    let a = execute('display')
    let b = execute('registers')

    call assert_equal(a, b)
    call assert_match('^\nType Name Content\n'
          \ .         '  c  ""   a\n'
          \ .         '  c  "0   ba\n'
          \ .         '  c  "a   b\n'
          \ .         '.*'
          \ .         '  c  "-   a\n'
          \ .         '.*'
          \ .         '  c  ":   ls\n'
          \ .         '  c  "%   file2\n'
          \ .         '  c  "#   file1\n'
          \ .         '  c  "/   bar\n'
          \ .         '  c  "=   2\*4', a)

    let a = execute('registers a')
    call assert_match('^\nType Name Content\n'
          \ .         '  c  "a   b', a)

    let a = execute('registers :')
    call assert_match('^\nType Name Content\n'
          \ .         '  c  ":   ls', a)

    bwipe!
endfunc

func Test_register_one()
  " delete a line goes into register one
  new
  call setline(1, "one")
  normal dd
  call assert_equal("one\n", @1)

  " delete a word does not change register one, does change "-
  call setline(1, "two")
  normal de
  call assert_equal("one\n", @1)
  call assert_equal("two", @-)

  " delete a word with a register does not change register one
  call setline(1, "three")
  normal "ade
  call assert_equal("three", @a)
  call assert_equal("one\n", @1)

  " delete a word with register DOES change register one with one of a list of
  " operators
  " %
  call setline(1, ["(12)3"])
  normal "ad%
  call assert_equal("(12)", @a)
  call assert_equal("(12)", @1)

  " (
  call setline(1, ["first second"])
  normal $"ad(
  call assert_equal("first secon", @a)
  call assert_equal("first secon", @1)

  " )
  call setline(1, ["First Second."])
  normal gg0"ad)
  call assert_equal("First Second.", @a)
  call assert_equal("First Second.", @1)

  " `
  call setline(1, ["start here."])
  normal gg0fhmx0"ad`x
  call assert_equal("start ", @a)
  call assert_equal("start ", @1)

  " /
  call setline(1, ["searchX"])
  exe "normal gg0\"ad/X\<CR>"
  call assert_equal("search", @a)
  call assert_equal("search", @1)

  " ?
  call setline(1, ["Ysearch"])
  exe "normal gg$\"ad?Y\<CR>"
  call assert_equal("Ysearc", @a)
  call assert_equal("Ysearc", @1)

  " n
  call setline(1, ["Ynext"])
  normal gg$"adn
  call assert_equal("Ynex", @a)
  call assert_equal("Ynex", @1)

  " N
  call setline(1, ["prevY"])
  normal gg0"adN
  call assert_equal("prev", @a)
  call assert_equal("prev", @1)

  " }
  call setline(1, ["one", ""])
  normal gg0"ad}
  call assert_equal("one\n", @a)
  call assert_equal("one\n", @1)

  " {
  call setline(1, ["", "two"])
  normal 2G$"ad{
  call assert_equal("\ntw", @a)
  call assert_equal("\ntw", @1)

  bwipe!
endfunc

" Check that replaying a typed sequence does not use an Esc and following
" characters as an escape sequence.
func Test_recording_esc_sequence()
  new
  try
    let save_F2 = &t_F2
  catch
  endtry
  let t_F2 = "\<Esc>OQ"
  call feedkeys("qqiTest\<Esc>", "xt")
  call feedkeys("OQuirk\<Esc>q", "xt")
  call feedkeys("Go\<Esc>@q", "xt")
  call assert_equal(['Quirk', 'Test', 'Quirk', 'Test'], getline(1, 4))
  bwipe!
  if exists('save_F2')
    let &t_F2 = save_F2
  else
    set t_F2=
  endif
endfunc
