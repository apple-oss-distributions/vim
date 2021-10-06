" Test for shortpathname ':8' extension.
" Only for use on Win32 systems!

set encoding=utf-8
scriptencoding utf-8

source check.vim
CheckMSWindows

func TestIt(file, bits, expected)
  let res = fnamemodify(a:file, a:bits)
  if a:expected != ''
    call assert_equal(substitute(a:expected, '/', '\\', 'g'),
		\ substitute(res, '/', '\\', 'g'),
		\ "'" . a:file . "'->(" . a:bits . ")->'" . res . "'")
  endif
endfunc

func Test_ColonEight()
  let save_dir = getcwd()

  " This could change for CygWin to //cygdrive/c
  let dir1 = 'c:/x.x.y'
  if filereadable(dir1) || isdirectory(dir1)
    call assert_report("Fatal: '" . dir1 . "' exists, cannot run test")
    return
  endif

  let file1 = dir1 . '/zz.y.txt'
  let nofile1 = dir1 . '/z.y.txt'
  let dir2 = dir1 . '/VimIsTheGreatestSinceSlicedBread'
  let file2 = dir2 . '/z.txt'
  let nofile2 = dir2 . '/zz.txt'

  call mkdir(dir1)
  let resdir1 = substitute(fnamemodify(dir1, ':p:8'), '/$', '', '')
  call assert_match('\V\^c:/XX\x\x\x\x~1.Y\$', resdir1)

  let resfile1 = resdir1 . '/ZZY~1.TXT'
  let resnofile1 = resdir1 . '/z.y.txt'
  let resdir2 = resdir1 . '/VIMIST~1'
  let resfile2 = resdir2 . '/z.txt'
  let resnofile2 = resdir2 . '/zz.txt'

  call mkdir(dir2)
  call writefile([], file1)
  call writefile([], file2)

  call TestIt(file1, ':p:8', resfile1)
  call TestIt(nofile1, ':p:8', resnofile1)
  call TestIt(file2, ':p:8', resfile2)
  call TestIt(nofile2, ':p:8', resnofile2)
  call TestIt(nofile2, ':p:8:h', fnamemodify(resnofile2, ':h'))
  call chdir(dir1)
  call TestIt(file1, ':.:8', strpart(resfile1, strlen(resdir1)+1))
  call TestIt(nofile1, ':.:8', strpart(resnofile1, strlen(resdir1)+1))
  call TestIt(file2, ':.:8', strpart(resfile2, strlen(resdir1)+1))
  call TestIt(nofile2, ':.:8', strpart(resnofile2, strlen(resdir1)+1))
  let $HOME=dir1
  call TestIt(file1, ':~:8', '~' . strpart(resfile1, strlen(resdir1)))
  call TestIt(nofile1, ':~:8', '~' . strpart(resnofile1, strlen(resdir1)))
  call TestIt(file2, ':~:8', '~' . strpart(resfile2, strlen(resdir1)))
  call TestIt(nofile2, ':~:8', '~' . strpart(resnofile2, strlen(resdir1)))

  cd c:/
  call delete(file2)
  call delete(file1)
  call delete(dir2, 'd')
  call delete(dir1, 'd')

  call chdir(save_dir)
endfunc

func Test_ColonEight_MultiByte()
  let dir = 'Xtest'

  let file = dir . '/日本語のファイル.txt'

  call mkdir(dir)
  call writefile([], file)

  let sfile = fnamemodify(file, ':8')

  call assert_notequal(file, sfile)
  call assert_match('\~', sfile)

  call delete(file)
  call delete(dir, 'd')
endfunc
