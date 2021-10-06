" Tests for various eval things.

function s:foo() abort
  try
    return [] == 0
  catch
    return 1
  endtry
endfunction

func Test_catch_return_with_error()
  call assert_equal(1, s:foo())
endfunc

func Test_nocatch_restore_silent_emsg()
  silent! try
    throw 1
  catch
  endtry
  echoerr 'wrong'
  let c1 = nr2char(screenchar(&lines, 1))
  let c2 = nr2char(screenchar(&lines, 2))
  let c3 = nr2char(screenchar(&lines, 3))
  let c4 = nr2char(screenchar(&lines, 4))
  let c5 = nr2char(screenchar(&lines, 5))
  call assert_equal('wrong', c1 . c2 . c3 . c4 . c5)
endfunc

func Test_mkdir_p()
  call mkdir('Xmkdir/nested', 'p')
  call assert_true(isdirectory('Xmkdir/nested'))
  try
    " Trying to make existing directories doesn't error
    call mkdir('Xmkdir', 'p')
    call mkdir('Xmkdir/nested', 'p')
  catch /E739:/
    call assert_report('mkdir(..., "p") failed for an existing directory')
  endtry
  " 'p' doesn't suppress real errors
  call writefile([], 'Xfile')
  call assert_fails('call mkdir("Xfile", "p")', 'E739')
  call delete('Xfile')
  call delete('Xmkdir', 'rf')
endfunc

func Test_line_continuation()
  let array = [5,
	"\ ignore this
	\ 6,
	"\ more to ignore
	"\ more moreto ignore
	\ ]
	"\ and some more
  call assert_equal([5, 6], array)
endfunc

func Test_E963()
  " These commands used to cause an internal error prior to vim 8.1.0563
  let v_e = v:errors
  let v_o = v:oldfiles
  call assert_fails("let v:errors=''", 'E963:')
  call assert_equal(v_e, v:errors)
  call assert_fails("let v:oldfiles=''", 'E963:')
  call assert_equal(v_o, v:oldfiles)
endfunc

func Test_for_invalid()
  call assert_fails("for x in 99", 'E714:')
  call assert_fails("for x in 'asdf'", 'E714:')
  call assert_fails("for x in {'a': 9}", 'E714:')
endfunc

func Test_readfile_binary()
  new
  call setline(1, ['one', 'two', 'three'])
  setlocal ff=dos
  silent write XReadfile
  let lines = 'XReadfile'->readfile()
  call assert_equal(['one', 'two', 'three'], lines)
  let lines = readfile('XReadfile', '', 2)
  call assert_equal(['one', 'two'], lines)
  let lines = readfile('XReadfile', 'b')
  call assert_equal(["one\r", "two\r", "three\r", ""], lines)
  let lines = readfile('XReadfile', 'b', 2)
  call assert_equal(["one\r", "two\r"], lines)

  bwipe!
  call delete('XReadfile')
endfunc

func Test_let_errmsg()
  call assert_fails('let v:errmsg = []', 'E730:')
  let v:errmsg = ''
  call assert_fails('let v:errmsg = []', 'E730:')
  let v:errmsg = ''
endfunc

func Test_string_concatenation()
  call assert_equal('ab', 'a'.'b')
  call assert_equal('ab', 'a' .'b')
  call assert_equal('ab', 'a'. 'b')
  call assert_equal('ab', 'a' . 'b')

  call assert_equal('ab', 'a'..'b')
  call assert_equal('ab', 'a' ..'b')
  call assert_equal('ab', 'a'.. 'b')
  call assert_equal('ab', 'a' .. 'b')

  let a = 'a'
  let b = 'b'
  let a .= b
  call assert_equal('ab', a)

  let a = 'a'
  let a.=b
  call assert_equal('ab', a)

  let a = 'a'
  let a ..= b
  call assert_equal('ab', a)

  let a = 'a'
  let a..=b
  call assert_equal('ab', a)
endfunc

" Test fix for issue #4507
func Test_skip_after_throw()
  try
    throw 'something'
    let x = wincol() || &ts
  catch /something/
  endtry
endfunc

scriptversion 2
func Test_string_concat_scriptversion2()
  call assert_true(has('vimscript-2'))
  let a = 'a'
  let b = 'b'

  call assert_fails('echo a . b', 'E15:')
  call assert_fails('let a .= b', 'E985:')
  call assert_fails('let vers = 1.2.3', 'E15:')

  if has('float')
    let f = .5
    call assert_equal(0.5, f)
  endif
endfunc

scriptversion 1
func Test_string_concat_scriptversion1()
  call assert_true(has('vimscript-1'))
  let a = 'a'
  let b = 'b'

  echo a . b
  let a .= b
  let vers = 1.2.3
  call assert_equal('123', vers)

  if has('float')
    call assert_fails('let f = .5', 'E15:')
  endif
endfunc

scriptversion 3
func Test_vvar_scriptversion3()
  call assert_true(has('vimscript-3'))
  call assert_fails('echo version', 'E121:')
  call assert_false(exists('version'))
  let version = 1
  call assert_equal(1, version)
endfunc

scriptversion 2
func Test_vvar_scriptversion2()
  call assert_true(exists('version'))
  echo version
  call assert_fails('let version = 1', 'E46:')
  call assert_equal(v:version, version)

  call assert_equal(v:version, v:versionlong / 10000)
  call assert_true(v:versionlong > 8011525)
endfunc

func Test_dict_access_scriptversion2()
  let l:x = {'foo': 1}

  call assert_false(0 && l:x.foo)
  call assert_true(1 && l:x.foo)
endfunc

scriptversion 4
func Test_vvar_scriptversion4()
  call assert_true(has('vimscript-4'))
  call assert_equal(17, 017)
  call assert_equal(18, 018)
  call assert_equal(64, 0b1'00'00'00)
  call assert_equal(1048576, 0x10'00'00)
  call assert_equal(1000000, 1'000'000)
  call assert_equal("1234", execute("echo 1'234")->trim())
  call assert_equal('1  234', execute("echo 1''234")->trim())
  call assert_fails("echo 1'''234", 'E115:')
endfunc

scriptversion 1
func Test_vvar_scriptversion1()
  call assert_equal(15, 017)
  call assert_equal(18, 018)
endfunc

func Test_scriptversion_fail()
  call writefile(['scriptversion 9'], 'Xversionscript')
  call assert_fails('source Xversionscript', 'E999:')
  call delete('Xversionscript')
endfunc
