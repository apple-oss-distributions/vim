" Test for :cd and chdir()

func Test_cd_large_path()
  " This used to crash with a heap write overflow.
  call assert_fails('cd ' . repeat('x', 5000), 'E472:')
endfunc

func Test_cd_up_and_down()
  let path = getcwd()
  cd ..
  call assert_notequal(path, getcwd())
  exe 'cd ' .. fnameescape(path)
  call assert_equal(path, getcwd())
endfunc

func Test_cd_no_arg()
  if has('unix')
    " Test that cd without argument goes to $HOME directory on Unix systems.
    let path = getcwd()
    cd
    call assert_equal($HOME, getcwd())
    call assert_notequal(path, getcwd())
    exe 'cd ' .. fnameescape(path)
    call assert_equal(path, getcwd())
  else
    " Test that cd without argument echoes cwd on non-Unix systems.
    call assert_match(getcwd(), execute('cd'))
  endif
endfunc

func Test_cd_minus()
  " Test the  :cd -  goes back to the previous directory.
  let path = getcwd()
  cd ..
  let path_dotdot = getcwd()
  call assert_notequal(path, path_dotdot)
  cd -
  call assert_equal(path, getcwd())
  cd -
  call assert_equal(path_dotdot, getcwd())
  cd -
  call assert_equal(path, getcwd())
endfunc

func Test_cd_with_cpo_chdir()
  e Xfoo
  call setline(1, 'foo')
  let path = getcwd()
  set cpo+=.

  " :cd should fail when buffer is modified and 'cpo' contains dot.
  call assert_fails('cd ..', 'E747:')
  call assert_equal(path, getcwd())

  " :cd with exclamation mark should succeed.
  cd! ..
  call assert_notequal(path, getcwd())

  " :cd should succeed when buffer has been written.
  w!
  exe 'cd ' .. fnameescape(path)
  call assert_equal(path, getcwd())

  call delete('Xfoo')
  set cpo&
  bw!
endfunc

" Test for chdir()
func Test_chdir_func()
  let topdir = getcwd()
  call mkdir('Xdir/y/z', 'p')

  " Create a few tabpages and windows with different directories
  new
  cd Xdir
  tabnew
  tcd y
  below new
  below new
  lcd z

  tabfirst
  call chdir('..')
  call assert_equal('y', fnamemodify(getcwd(1, 2), ':t'))
  call assert_equal('z', fnamemodify(3->getcwd(2), ':t'))
  tabnext | wincmd t
  eval '..'->chdir()
  call assert_equal('Xdir', fnamemodify(getcwd(1, 2), ':t'))
  call assert_equal('Xdir', fnamemodify(getcwd(2, 2), ':t'))
  call assert_equal('z', fnamemodify(getcwd(3, 2), ':t'))
  call assert_equal('testdir', fnamemodify(getcwd(1, 1), ':t'))
  3wincmd w
  call chdir('..')
  call assert_equal('Xdir', fnamemodify(getcwd(1, 2), ':t'))
  call assert_equal('Xdir', fnamemodify(getcwd(2, 2), ':t'))
  call assert_equal('y', fnamemodify(getcwd(3, 2), ':t'))
  call assert_equal('testdir', fnamemodify(getcwd(1, 1), ':t'))

  " Error case
  call assert_fails("call chdir('dir-abcd')", 'E472:')
  silent! let d = chdir("dir_abcd")
  call assert_equal("", d)

  only | tabonly
  call chdir(topdir)
  call delete('Xdir', 'rf')
endfunc
