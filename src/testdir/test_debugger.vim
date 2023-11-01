" Tests for the Vim script debug commands

source shared.vim
source screendump.vim
source check.vim

CheckRunVimInTerminal

func CheckCWD()
  " Check that the longer lines don't wrap due to the length of the script name
  " in cwd
  let script_len = len( getcwd() .. '/Xtest1.vim' )
  let longest_line = len( 'Breakpoint in "" line 1' )
  if script_len > ( 75 - longest_line )
    throw 'Skipped: Your CWD has too many characters'
  endif
endfunc
command! -nargs=0 -bar CheckCWD call CheckCWD()

" "options" argument can contain:
" 'msec' - time to wait for a match
" 'match' - "pattern" to use "lines" as pattern instead of text
def s:CheckDbgOutput(buf: number, lines: list<string>, options = {})
  # Verify the expected output
  var lnum = 20 - len(lines)
  var msec = get(options, 'msec', 1000)
  for l in lines
    if get(options, 'match', 'equal') ==# 'pattern'
      g:WaitForAssert(() => assert_match(l, term_getline(buf, lnum)), msec)
    else
      g:WaitForAssert(() => assert_equal(l, term_getline(buf, lnum)), msec)
    endif
    lnum += 1
  endfor
enddef

" Run a Vim debugger command
" If the expected output argument is supplied, then check for it.
def s:RunDbgCmd(buf: number, cmd: string, ...extra: list<any>)
  term_sendkeys(buf, cmd .. "\r")
  g:TermWait(buf)

  if len(extra) > 0
    var options = {match: 'equal'}
    if len(extra) > 1
      extend(options, extra[1])
    endif
    s:CheckDbgOutput(buf, extra[0], options)
  endif
enddef

" Debugger tests
def Test_Debugger()
  # Create a Vim script with some functions
  var lines =<< trim END
	func Foo()
	  let var1 = 1
	  let var2 = Bar(var1) + 9
	  return var2
	endfunc
	func Bar(var)
	  let var1 = 2 + a:var
	  let var2 = Bazz(var1) + 4
	  return var2
	endfunc
	func Bazz(var)
	  try
	    let var1 = 3 + a:var
	    let var3 = "another var"
	    let var3 = "value2"
	  catch
	    let var4 = "exception"
	  endtry
	  return var1
	endfunc
        def Vim9Func()
          for cmd in ['confirm', 'xxxxxxx']
            for _ in [1, 2]
              echo cmd
            endfor
          endfor
        enddef
  END
  writefile(lines, 'XtestDebug.vim', 'D')

  # Start Vim in a terminal
  var buf = g:RunVimInTerminal('-S XtestDebug.vim', {})

  # Start the Vim debugger
  s:RunDbgCmd(buf, ':debug echo Foo()', ['cmd: echo Foo()'])

  # Create a few stack frames by stepping through functions
  s:RunDbgCmd(buf, 'step', ['line 1: let var1 = 1'])
  s:RunDbgCmd(buf, 'step', ['line 2: let var2 = Bar(var1) + 9'])
  s:RunDbgCmd(buf, 'step', ['line 1: let var1 = 2 + a:var'])
  s:RunDbgCmd(buf, 'step', ['line 2: let var2 = Bazz(var1) + 4'])
  s:RunDbgCmd(buf, 'step', ['line 1: try'])
  s:RunDbgCmd(buf, 'step', ['line 2: let var1 = 3 + a:var'])
  s:RunDbgCmd(buf, 'step', ['line 3: let var3 = "another var"'])

  # check backtrace
  s:RunDbgCmd(buf, 'backtrace', [
	      '  2 function Foo[2]',
	      '  1 Bar[2]',
	      '->0 Bazz',
	      'line 3: let var3 = "another var"'])

  # Check variables in different stack frames
  s:RunDbgCmd(buf, 'echo var1', ['6'])

  s:RunDbgCmd(buf, 'up')
  s:RunDbgCmd(buf, 'back', [
	      '  2 function Foo[2]',
	      '->1 Bar[2]',
	      '  0 Bazz',
	      'line 3: let var3 = "another var"'])
  s:RunDbgCmd(buf, 'echo var1', ['3'])

  s:RunDbgCmd(buf, 'u')
  s:RunDbgCmd(buf, 'bt', [
	      '->2 function Foo[2]',
	      '  1 Bar[2]',
	      '  0 Bazz',
	      'line 3: let var3 = "another var"'])
  s:RunDbgCmd(buf, 'echo var1', ['1'])

  # Undefined variables
  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, 'frame 2')
  s:RunDbgCmd(buf, 'echo var3', [
	'Error detected while processing function Foo[2]..Bar[2]..Bazz:',
	'line    4:',
	'E121: Undefined variable: var3'])

  # var3 is defined in this level with some other value
  s:RunDbgCmd(buf, 'fr 0')
  s:RunDbgCmd(buf, 'echo var3', ['another var'])

  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, '')
  s:RunDbgCmd(buf, '')
  s:RunDbgCmd(buf, '')
  s:RunDbgCmd(buf, '')
  s:RunDbgCmd(buf, 'step', [
	      'function Foo[2]..Bar',
	      'line 3: End of function'])
  s:RunDbgCmd(buf, 'up')

  # Undefined var2
  s:RunDbgCmd(buf, 'echo var2', [
	      'Error detected while processing function Foo[2]..Bar:',
	      'line    3:',
	      'E121: Undefined variable: var2'])

  # Var2 is defined with 10
  s:RunDbgCmd(buf, 'down')
  s:RunDbgCmd(buf, 'echo var2', ['10'])

  # Backtrace movements
  s:RunDbgCmd(buf, 'b', [
	      '  1 function Foo[2]',
	      '->0 Bar',
	      'line 3: End of function'])

  # next command cannot go down, we are on bottom
  s:RunDbgCmd(buf, 'down', ['frame is zero'])
  s:RunDbgCmd(buf, 'up')

  # next command cannot go up, we are on top
  s:RunDbgCmd(buf, 'up', ['frame at highest level: 1'])
  s:RunDbgCmd(buf, 'where', [
	      '->1 function Foo[2]',
	      '  0 Bar',
	      'line 3: End of function'])

  # fil is not frame or finish, it is file
  s:RunDbgCmd(buf, 'fil', ['"[No Name]" --No lines in buffer--'])

  # relative backtrace movement
  s:RunDbgCmd(buf, 'fr -1')
  s:RunDbgCmd(buf, 'frame', [
	      '  1 function Foo[2]',
	      '->0 Bar',
	      'line 3: End of function'])

  s:RunDbgCmd(buf, 'fr +1')
  s:RunDbgCmd(buf, 'fram', [
	      '->1 function Foo[2]',
	      '  0 Bar',
	      'line 3: End of function'])

  # go beyond limits does not crash
  s:RunDbgCmd(buf, 'fr 100', ['frame at highest level: 1'])
  s:RunDbgCmd(buf, 'fra', [
	      '->1 function Foo[2]',
	      '  0 Bar',
	      'line 3: End of function'])

  s:RunDbgCmd(buf, 'frame -40', ['frame is zero'])
  s:RunDbgCmd(buf, 'fram', [
	      '  1 function Foo[2]',
	      '->0 Bar',
	      'line 3: End of function'])

  # final result 19
  s:RunDbgCmd(buf, 'cont', ['19'])

  # breakpoints tests

  # Start a debug session, so that reading the last line from the terminal
  # works properly.
  s:RunDbgCmd(buf, ':debug echo Foo()', ['cmd: echo Foo()'])

  # No breakpoints
  s:RunDbgCmd(buf, 'breakl', ['No breakpoints defined'])

  # Place some breakpoints
  s:RunDbgCmd(buf, 'breaka func Bar')
  s:RunDbgCmd(buf, 'breaklis', ['  1  func Bar  line 1'])
  s:RunDbgCmd(buf, 'breakadd func 3 Bazz')
  s:RunDbgCmd(buf, 'breaklist', ['  1  func Bar  line 1',
	      '  2  func Bazz  line 3'])

  # Check whether the breakpoints are hit
  s:RunDbgCmd(buf, 'cont', [
	      'Breakpoint in "Bar" line 1',
	      'function Foo[2]..Bar',
	      'line 1: let var1 = 2 + a:var'])
  s:RunDbgCmd(buf, 'cont', [
	      'Breakpoint in "Bazz" line 3',
	      'function Foo[2]..Bar[2]..Bazz',
	      'line 3: let var3 = "another var"'])

  # Delete the breakpoints
  s:RunDbgCmd(buf, 'breakd 1')
  s:RunDbgCmd(buf, 'breakli', ['  2  func Bazz  line 3'])
  s:RunDbgCmd(buf, 'breakdel func 3 Bazz')
  s:RunDbgCmd(buf, 'breakl', ['No breakpoints defined'])

  s:RunDbgCmd(buf, 'cont')

  # Make sure the breakpoints are removed
  s:RunDbgCmd(buf, ':echo Foo()', ['19'])

  # Delete a non-existing breakpoint
  s:RunDbgCmd(buf, ':breakdel 2', ['E161: Breakpoint not found: 2'])

  # Expression breakpoint
  s:RunDbgCmd(buf, ':breakadd func 2 Bazz')
  s:RunDbgCmd(buf, ':echo Bazz(1)', [
	      'Entering Debug mode.  Type "cont" to continue.',
	      'function Bazz',
	      'line 2: let var1 = 3 + a:var'])
  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, 'breaka expr var3')
  s:RunDbgCmd(buf, 'breakl', ['  3  func Bazz  line 2',
	      \ '  4  expr var3'])
  s:RunDbgCmd(buf, 'cont', ['Breakpoint in "Bazz" line 5',
	      'Oldval = "''another var''"',
	      'Newval = "''value2''"',
	      'function Bazz',
	      'line 5: catch'])

  s:RunDbgCmd(buf, 'breakdel *')
  s:RunDbgCmd(buf, 'breakl', ['No breakpoints defined'])

  # Check for error cases
  s:RunDbgCmd(buf, 'breakadd abcd', [
	      'Error detected while processing function Bazz:',
	      'line    5:',
	      'E475: Invalid argument: abcd'])
  s:RunDbgCmd(buf, 'breakadd func', ['E475: Invalid argument: func'])
  s:RunDbgCmd(buf, 'breakadd func 2', ['E475: Invalid argument: func 2'])
  s:RunDbgCmd(buf, 'breaka func a()', ['E475: Invalid argument: func a()'])
  s:RunDbgCmd(buf, 'breakd abcd', ['E475: Invalid argument: abcd'])
  s:RunDbgCmd(buf, 'breakd func', ['E475: Invalid argument: func'])
  s:RunDbgCmd(buf, 'breakd func a()', ['E475: Invalid argument: func a()'])
  s:RunDbgCmd(buf, 'breakd func a', ['E161: Breakpoint not found: func a'])
  s:RunDbgCmd(buf, 'breakd expr', ['E475: Invalid argument: expr'])
  s:RunDbgCmd(buf, 'breakd expr x', ['E161: Breakpoint not found: expr x'])

  # finish the current function
  s:RunDbgCmd(buf, 'finish', [
	      'function Bazz',
	      'line 8: End of function'])
  s:RunDbgCmd(buf, 'cont')

  # Test for :next
  s:RunDbgCmd(buf, ':debug echo Bar(1)')
  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, 'next')
  s:RunDbgCmd(buf, '', [
	      'function Bar',
	      'line 3: return var2'])
  s:RunDbgCmd(buf, 'c')

  # Test for :interrupt
  s:RunDbgCmd(buf, ':debug echo Bazz(1)')
  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, 'step')
  s:RunDbgCmd(buf, 'interrupt', [
	      'Exception thrown: Vim:Interrupt',
	      'function Bazz',
	      'line 5: catch'])
  s:RunDbgCmd(buf, 'c')

  # Test showing local variable in :def function
  s:RunDbgCmd(buf, ':breakadd func 2 Vim9Func')
  s:RunDbgCmd(buf, ':call Vim9Func()', ['line 2:             for _ in [1, 2]'])
  s:RunDbgCmd(buf, 'next', ['line 2: for _ in [1, 2]'])
  s:RunDbgCmd(buf, 'echo cmd', ['confirm'])
  s:RunDbgCmd(buf, 'breakdel *')
  s:RunDbgCmd(buf, 'cont')

  # Test for :quit
  s:RunDbgCmd(buf, ':debug echo Foo()')
  s:RunDbgCmd(buf, 'breakdel *')
  s:RunDbgCmd(buf, 'breakadd func 3 Foo')
  s:RunDbgCmd(buf, 'breakadd func 3 Bazz')
  s:RunDbgCmd(buf, 'cont', [
	      'Breakpoint in "Bazz" line 3',
	      'function Foo[2]..Bar[2]..Bazz',
	      'line 3: let var3 = "another var"'])
  s:RunDbgCmd(buf, 'quit', [
	      'Breakpoint in "Foo" line 3',
	      'function Foo',
	      'line 3: return var2'])
  s:RunDbgCmd(buf, 'breakdel *')
  s:RunDbgCmd(buf, 'quit')
  s:RunDbgCmd(buf, 'enew! | only!')

  g:StopVimInTerminal(buf)
enddef

func Test_Debugger_breakadd()
  " Tests for :breakadd file and :breakadd here
  " Breakpoints should be set before sourcing the file

  let lines =<< trim END
	let var1 = 10
	let var2 = 20
	let var3 = 30
	let var4 = 40
  END
  call writefile(lines, 'XdebugBreakadd.vim', 'D')

  " Start Vim in a terminal
  let buf = RunVimInTerminal('XdebugBreakadd.vim', {})
  call s:RunDbgCmd(buf, ':breakadd file 2 XdebugBreakadd.vim')
  call s:RunDbgCmd(buf, ':4 | breakadd here')
  call s:RunDbgCmd(buf, ':source XdebugBreakadd.vim', ['line 2: let var2 = 20'])
  call s:RunDbgCmd(buf, 'cont', ['line 4: let var4 = 40'])
  call s:RunDbgCmd(buf, 'cont')

  call StopVimInTerminal(buf)

  %bw!

  call assert_fails('breakadd here', 'E32:')
  call assert_fails('breakadd file Xtest.vim /\)/', 'E55:')
endfunc

" Test for expression breakpoint set using ":breakadd expr <expr>"
func Test_Debugger_breakadd_expr()
  CheckCWD

  let lines =<< trim END
    let g:Xtest_var += 1
  END
  call writefile(lines, 'XdebugBreakExpr.vim', 'D')

  " Start Vim in a terminal
  let buf = RunVimInTerminal('XdebugBreakExpr.vim', {})
  call s:RunDbgCmd(buf, ':let g:Xtest_var = 10')
  call s:RunDbgCmd(buf, ':breakadd expr g:Xtest_var')
  call s:RunDbgCmd(buf, ':source %')
  let expected =<< trim eval END
    Oldval = "10"
    Newval = "11"
    {fnamemodify('XdebugBreakExpr.vim', ':p')}
    line 1: let g:Xtest_var += 1
  END
  call s:RunDbgCmd(buf, ':source %', expected)
  call s:RunDbgCmd(buf, 'cont')
  let expected =<< trim eval END
    Oldval = "11"
    Newval = "12"
    {fnamemodify('XdebugBreakExpr.vim', ':p')}
    line 1: let g:Xtest_var += 1
  END
  call s:RunDbgCmd(buf, ':source %', expected)

  call StopVimInTerminal(buf)
endfunc

def Test_Debugger_breakadd_vim9_expr()
  var lines =<< trim END
      vim9script
      func g:EarlyFunc()
      endfunc
      breakadd expr DoesNotExist()
      func g:LaterFunc()
      endfunc
      breakdel *
  END
  writefile(lines, 'XdebugBreak9expr.vim', 'D')

  # Start Vim in a terminal
  var buf = g:RunVimInTerminal('-S XdebugBreak9expr.vim', {wait_for_ruler: 0})
  call g:TermWait(buf, g:RunningWithValgrind() ? 1000 : 50)

  # Despite the failure the functions are defined
  s:RunDbgCmd(buf, ':function g:EarlyFunc',
     ['function EarlyFunc()', 'endfunction'], {match: 'pattern'})
  s:RunDbgCmd(buf, ':function g:LaterFunc',
     ['function LaterFunc()', 'endfunction'], {match: 'pattern'})

  call g:StopVimInTerminal(buf)
enddef

def Test_Debugger_break_at_return()
  var lines =<< trim END
      vim9script
      def g:GetNum(): number
        return 1
          + 2
          + 3
      enddef
      breakadd func GetNum
  END
  writefile(lines, 'XdebugBreakRet.vim', 'D')

  # Start Vim in a terminal
  var buf = g:RunVimInTerminal('-S XdebugBreakRet.vim', {wait_for_ruler: 0})
  call g:TermWait(buf, g:RunningWithValgrind() ? 1000 : 50)

  s:RunDbgCmd(buf, ':call GetNum()',
     ['line 1: return 1  + 2  + 3'], {match: 'pattern'})

  call g:StopVimInTerminal(buf)
enddef

func Test_Backtrace_Through_Source()
  CheckCWD
  let file1 =<< trim END
    func SourceAnotherFile()
      source Xtest2.vim
    endfunc

    func CallAFunction()
      call SourceAnotherFile()
      call File2Function()
    endfunc

    func GlobalFunction()
      call CallAFunction()
    endfunc
  END
  call writefile(file1, 'Xtest1.vim', 'D')

  let file2 =<< trim END
    func DoAThing()
      echo "DoAThing"
    endfunc

    func File2Function()
      call DoAThing()
    endfunc

    call File2Function()
  END
  call writefile(file2, 'Xtest2.vim', 'D')

  let buf = RunVimInTerminal('-S Xtest1.vim', {})

  call s:RunDbgCmd(buf,
                \ ':debug call GlobalFunction()',
                \ ['cmd: call GlobalFunction()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: call CallAFunction()'])

  call s:RunDbgCmd(buf, 'backtrace', ['>backtrace',
                                    \ '->0 function GlobalFunction',
                                    \ 'line 1: call CallAFunction()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: call SourceAnotherFile()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: source Xtest2.vim'])

  call s:RunDbgCmd(buf, 'backtrace', ['>backtrace',
                                    \ '  2 function GlobalFunction[1]',
                                    \ '  1 CallAFunction[1]',
                                    \ '->0 SourceAnotherFile',
                                    \ 'line 1: source Xtest2.vim'])

  " Step into the 'source' command. Note that we print the full trace all the
  " way though the source command.
  call s:RunDbgCmd(buf, 'step', ['line 1: func DoAThing()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()'])

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '->1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '->2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '->3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up', [ 'frame at highest level: 3' ] )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '->3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '->2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '->1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down', [ 'frame is zero' ] )

  " step until we have another meaningful trace
  call s:RunDbgCmd(buf, 'step', ['line 5: func File2Function()'])
  call s:RunDbgCmd(buf, 'step', ['line 9: call File2Function()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 9: call File2Function()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: call DoAThing()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: echo "DoAThing"'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  5 function GlobalFunction[1]',
        \ '  4 CallAFunction[1]',
        \ '  3 SourceAnotherFile[1]',
        \ '  2 script ' .. getcwd() .. '/Xtest2.vim[9]',
        \ '  1 function File2Function[1]',
        \ '->0 DoAThing',
        \ 'line 1: echo "DoAThing"'])

  " Now, step (back to Xfile1.vim), and call the function _in_ Xfile2.vim
  call s:RunDbgCmd(buf, 'step', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'step', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'step', ['line 10: End of sourced file'])
  call s:RunDbgCmd(buf, 'step', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'step', ['line 2: call File2Function()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  1 function GlobalFunction[1]',
        \ '->0 CallAFunction',
        \ 'line 2: call File2Function()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: call DoAThing()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  2 function GlobalFunction[1]',
        \ '  1 CallAFunction[2]',
        \ '->0 File2Function',
        \ 'line 1: call DoAThing()'])

  call StopVimInTerminal(buf)
endfunc

func Test_Backtrace_Autocmd()
  CheckCWD
  let file1 =<< trim END
    func SourceAnotherFile()
      source Xtest2.vim
    endfunc

    func CallAFunction()
      call SourceAnotherFile()
      call File2Function()
    endfunc

    func GlobalFunction()
      call CallAFunction()
    endfunc

    au User TestGlobalFunction :call GlobalFunction() | echo "Done"
  END
  call writefile(file1, 'Xtest1.vim', 'D')

  let file2 =<< trim END
    func DoAThing()
      echo "DoAThing"
    endfunc

    func File2Function()
      call DoAThing()
    endfunc

    call File2Function()
  END
  call writefile(file2, 'Xtest2.vim', 'D')

  let buf = RunVimInTerminal('-S Xtest1.vim', {})

  call s:RunDbgCmd(buf,
                \ ':debug doautocmd User TestGlobalFunction',
                \ ['cmd: doautocmd User TestGlobalFunction'])
  call s:RunDbgCmd(buf, 'step', ['cmd: call GlobalFunction() | echo "Done"'])

  " At this point the only thing in the stack is the autocommand
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '->0 User Autocommands for "TestGlobalFunction"',
        \ 'cmd: call GlobalFunction() | echo "Done"'])

  " And now we're back into the call stack
  call s:RunDbgCmd(buf, 'step', ['line 1: call CallAFunction()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  1 User Autocommands for "TestGlobalFunction"',
        \ '->0 function GlobalFunction',
        \ 'line 1: call CallAFunction()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: call SourceAnotherFile()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: source Xtest2.vim'])

  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 User Autocommands for "TestGlobalFunction"',
        \ '  2 function GlobalFunction[1]',
        \ '  1 CallAFunction[1]',
        \ '->0 SourceAnotherFile',
        \ 'line 1: source Xtest2.vim'])

  " Step into the 'source' command. Note that we print the full trace all the
  " way though the source command.
  call s:RunDbgCmd(buf, 'step', ['line 1: func DoAThing()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()'])

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '->1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '->2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '->3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '->4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'up', [ 'frame at highest level: 4' ] )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '->4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '->3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )


  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '->2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '->1 SourceAnotherFile[1]',
        \ '  0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down' )
  call s:RunDbgCmd( buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 1: func DoAThing()' ] )

  call s:RunDbgCmd( buf, 'down', [ 'frame is zero' ] )

  " step until we have another meaningful trace
  call s:RunDbgCmd(buf, 'step', ['line 5: func File2Function()'])
  call s:RunDbgCmd(buf, 'step', ['line 9: call File2Function()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  4 User Autocommands for "TestGlobalFunction"',
        \ '  3 function GlobalFunction[1]',
        \ '  2 CallAFunction[1]',
        \ '  1 SourceAnotherFile[1]',
        \ '->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ 'line 9: call File2Function()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: call DoAThing()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: echo "DoAThing"'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  6 User Autocommands for "TestGlobalFunction"',
        \ '  5 function GlobalFunction[1]',
        \ '  4 CallAFunction[1]',
        \ '  3 SourceAnotherFile[1]',
        \ '  2 script ' .. getcwd() .. '/Xtest2.vim[9]',
        \ '  1 function File2Function[1]',
        \ '->0 DoAThing',
        \ 'line 1: echo "DoAThing"'])

  " Now, step (back to Xfile1.vim), and call the function _in_ Xfile2.vim
  call s:RunDbgCmd(buf, 'step', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'step', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'step', ['line 10: End of sourced file'])
  call s:RunDbgCmd(buf, 'step', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'step', ['line 2: call File2Function()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  2 User Autocommands for "TestGlobalFunction"',
        \ '  1 function GlobalFunction[1]',
        \ '->0 CallAFunction',
        \ 'line 2: call File2Function()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: call DoAThing()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 User Autocommands for "TestGlobalFunction"',
        \ '  2 function GlobalFunction[1]',
        \ '  1 CallAFunction[2]',
        \ '->0 File2Function',
        \ 'line 1: call DoAThing()'])


  " Now unwind so that we get back to the original autocommand (and the second
  " cmd echo "Done")
  call s:RunDbgCmd(buf, 'finish', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  3 User Autocommands for "TestGlobalFunction"',
        \ '  2 function GlobalFunction[1]',
        \ '  1 CallAFunction[2]',
        \ '->0 File2Function',
        \ 'line 1: End of function'])

  call s:RunDbgCmd(buf, 'finish', ['line 2: End of function'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  2 User Autocommands for "TestGlobalFunction"',
        \ '  1 function GlobalFunction[1]',
        \ '->0 CallAFunction',
        \ 'line 2: End of function'])

  call s:RunDbgCmd(buf, 'finish', ['line 1: End of function'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  1 User Autocommands for "TestGlobalFunction"',
        \ '->0 function GlobalFunction',
        \ 'line 1: End of function'])

  call s:RunDbgCmd(buf, 'step', ['cmd: echo "Done"'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '->0 User Autocommands for "TestGlobalFunction"',
        \ 'cmd: echo "Done"'])

  call StopVimInTerminal(buf)
endfunc

func Test_Backtrace_CmdLine()
  CheckCWD
  let file1 =<< trim END
    func SourceAnotherFile()
      source Xtest2.vim
    endfunc

    func CallAFunction()
      call SourceAnotherFile()
      call File2Function()
    endfunc

    func GlobalFunction()
      call CallAFunction()
    endfunc

    au User TestGlobalFunction :call GlobalFunction() | echo "Done"
  END
  call writefile(file1, 'Xtest1.vim', 'D')

  let file2 =<< trim END
    func DoAThing()
      echo "DoAThing"
    endfunc

    func File2Function()
      call DoAThing()
    endfunc

    call File2Function()
  END
  call writefile(file2, 'Xtest2.vim', 'D')

  let buf = RunVimInTerminal(
        \ '-S Xtest1.vim -c "debug call GlobalFunction()"',
        \ {'wait_for_ruler': 0})

  " Need to wait for the vim-in-terminal to be ready.
  " With valgrind this can take quite long.
  call s:CheckDbgOutput(buf, ['command line',
                            \ 'cmd: call GlobalFunction()'], #{msec: 5000})

  " At this point the only thing in the stack is the cmdline
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '->0 command line',
        \ 'cmd: call GlobalFunction()'])

  " And now we're back into the call stack
  call s:RunDbgCmd(buf, 'step', ['line 1: call CallAFunction()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '>backtrace',
        \ '  1 command line',
        \ '->0 function GlobalFunction',
        \ 'line 1: call CallAFunction()'])

  call StopVimInTerminal(buf)
endfunc

func Test_Backtrace_DefFunction()
  CheckCWD
  let file1 =<< trim END
    vim9script
    import './Xtest2.vim' as imp

    def SourceAnotherFile()
      source Xtest2.vim
    enddef

    def CallAFunction()
      SourceAnotherFile()
      imp.File2Function()
    enddef

    def g:GlobalFunction()
      var some = "some var"
      CallAFunction()
    enddef

    defcompile
  END
  call writefile(file1, 'Xtest1.vim', 'D')

  let file2 =<< trim END
    vim9script

    def DoAThing(): number
      var a = 100 * 2
      a += 3
      return a
    enddef

    export def File2Function()
      DoAThing()
    enddef

    defcompile
    File2Function()
  END
  call writefile(file2, 'Xtest2.vim', 'D')

  let buf = RunVimInTerminal('-S Xtest1.vim', {})

  call s:RunDbgCmd(buf,
                \ ':debug call GlobalFunction()',
                \ ['cmd: call GlobalFunction()'])

  call s:RunDbgCmd(buf, 'step', ['line 1: var some = "some var"'])
  call s:RunDbgCmd(buf, 'step', ['line 2: CallAFunction()'])
  call s:RunDbgCmd(buf, 'echo some', ['some var'])

  call s:RunDbgCmd(buf, 'backtrace', [
        \ '\V>backtrace',
        \ '\V->0 function GlobalFunction',
        \ '\Vline 2: CallAFunction()',
        \ ],
        \ #{match: 'pattern'})

  call s:RunDbgCmd(buf, 'step', ['line 1: SourceAnotherFile()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: source Xtest2.vim'])
  " Repeated line, because we first are in the compiled function before the
  " EXEC and then in do_cmdline() before the :source command.
  call s:RunDbgCmd(buf, 'step', ['line 1: source Xtest2.vim'])
  call s:RunDbgCmd(buf, 'step', ['line 1: vim9script'])
  call s:RunDbgCmd(buf, 'step', ['line 3: def DoAThing(): number'])
  call s:RunDbgCmd(buf, 'step', ['line 9: export def File2Function()'])
  call s:RunDbgCmd(buf, 'step', ['line 13: defcompile'])
  call s:RunDbgCmd(buf, 'step', ['line 14: File2Function()'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '\V>backtrace',
        \ '\V  3 function GlobalFunction[2]',
        \ '\V  2 <SNR>\.\*_CallAFunction[1]',
        \ '\V  1 <SNR>\.\*_SourceAnotherFile[1]',
        \ '\V->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ '\Vline 14: File2Function()'],
        \ #{match: 'pattern'})

  " Don't step into compiled functions...
  call s:RunDbgCmd(buf, 'next', ['line 15: End of sourced file'])
  call s:RunDbgCmd(buf, 'backtrace', [
        \ '\V>backtrace',
        \ '\V  3 function GlobalFunction[2]',
        \ '\V  2 <SNR>\.\*_CallAFunction[1]',
        \ '\V  1 <SNR>\.\*_SourceAnotherFile[1]',
        \ '\V->0 script ' .. getcwd() .. '/Xtest2.vim',
        \ '\Vline 15: End of sourced file'],
        \ #{match: 'pattern'})

  call StopVimInTerminal(buf)
endfunc

func Test_DefFunction_expr()
  CheckCWD
  let file3 =<< trim END
      vim9script
      g:someVar = "foo"
      def g:ChangeVar()
        g:someVar = "bar"
        echo "changed"
      enddef
      defcompile
  END
  call writefile(file3, 'Xtest3.vim', 'D')
  let buf = RunVimInTerminal('-S Xtest3.vim', {})

  call s:RunDbgCmd(buf, ':breakadd expr g:someVar')
  call s:RunDbgCmd(buf, ':call g:ChangeVar()', ['Oldval = "''foo''"', 'Newval = "''bar''"', 'function ChangeVar', 'line 2: echo "changed"'])

  call StopVimInTerminal(buf)
endfunc

func Test_debug_def_and_legacy_function()
  CheckCWD
  let file =<< trim END
    vim9script
    def g:SomeFunc()
      echo "here"
      echo "and"
      echo "there"
      breakadd func 2 LocalFunc
      LocalFunc()
    enddef

    def LocalFunc()
      echo "first"
      echo "second"
      breakadd func LegacyFunc
      LegacyFunc()
    enddef

    func LegacyFunc()
      echo "legone"
      echo "legtwo"
    endfunc

    breakadd func 2 g:SomeFunc
  END
  call writefile(file, 'XtestDebug.vim', 'D')

  let buf = RunVimInTerminal('-S XtestDebug.vim', {})

  call s:RunDbgCmd(buf,':call SomeFunc()', ['line 2: echo "and"'])
  call s:RunDbgCmd(buf,'next', ['line 3: echo "there"'])
  call s:RunDbgCmd(buf,'next', ['line 4: breakadd func 2 LocalFunc'])

  " continue, next breakpoint is in LocalFunc()
  call s:RunDbgCmd(buf,'cont', ['line 2: echo "second"'])

  " continue, next breakpoint is in LegacyFunc()
  call s:RunDbgCmd(buf,'cont', ['line 1: echo "legone"'])

  call s:RunDbgCmd(buf, 'cont')

  call StopVimInTerminal(buf)
endfunc

func Test_debug_def_function()
  CheckCWD
  let file =<< trim END
    vim9script
    def g:Func()
      var n: number
      def Closure(): number
          return n + 3
      enddef
      n += Closure()
      echo 'result: ' .. n
    enddef

    def g:FuncWithArgs(text: string, nr: number, ...items: list<number>)
      echo text .. nr
      for it in items
        echo it
      endfor
      echo "done"
    enddef

    def g:FuncWithDict()
      var d = {
         a: 1,
         b: 2,
         }
         # comment
         def Inner()
           eval 1 + 2
         enddef
    enddef

    def g:FuncComment()
      # comment
      echo "first"
         .. "one"
      # comment
      echo "second"
    enddef

    def g:FuncForLoop()
      eval 1 + 2
      for i in [11, 22, 33]
        eval i + 2
      endfor
      echo "done"
    enddef

    def g:FuncWithSplitLine()
        eval 1 + 2
           | eval 2 + 3
    enddef
  END
  call writefile(file, 'Xtest.vim', 'D')

  let buf = RunVimInTerminal('-S Xtest.vim', {})

  call s:RunDbgCmd(buf,
                \ ':debug call Func()',
                \ ['cmd: call Func()'])
  call s:RunDbgCmd(buf, 'next', ['result: 3'])
  call term_sendkeys(buf, "\r")
  call s:RunDbgCmd(buf, 'cont')

  call s:RunDbgCmd(buf,
                \ ':debug call FuncWithArgs("asdf", 42, 1, 2, 3)',
                \ ['cmd: call FuncWithArgs("asdf", 42, 1, 2, 3)'])
  call s:RunDbgCmd(buf, 'step', ['line 1: echo text .. nr'])
  call s:RunDbgCmd(buf, 'echo text', ['asdf'])
  call s:RunDbgCmd(buf, 'echo nr', ['42'])
  call s:RunDbgCmd(buf, 'echo items', ['[1, 2, 3]'])
  call s:RunDbgCmd(buf, 'step', ['asdf42', 'function FuncWithArgs', 'line 2:   for it in items'])
  call s:RunDbgCmd(buf, 'step', ['function FuncWithArgs', 'line 2: for it in items'])
  call s:RunDbgCmd(buf, 'echo it', ['0'])
  call s:RunDbgCmd(buf, 'step', ['line 3: echo it'])
  call s:RunDbgCmd(buf, 'echo it', ['1'])
  call s:RunDbgCmd(buf, 'step', ['1', 'function FuncWithArgs', 'line 4: endfor'])
  call s:RunDbgCmd(buf, 'step', ['line 2: for it in items'])
  call s:RunDbgCmd(buf, 'echo it', ['1'])
  call s:RunDbgCmd(buf, 'step', ['line 3: echo it'])
  call s:RunDbgCmd(buf, 'step', ['2', 'function FuncWithArgs', 'line 4: endfor'])
  call s:RunDbgCmd(buf, 'step', ['line 2: for it in items'])
  call s:RunDbgCmd(buf, 'echo it', ['2'])
  call s:RunDbgCmd(buf, 'step', ['line 3: echo it'])
  call s:RunDbgCmd(buf, 'step', ['3', 'function FuncWithArgs', 'line 4: endfor'])
  call s:RunDbgCmd(buf, 'step', ['line 2: for it in items'])
  call s:RunDbgCmd(buf, 'step', ['line 5: echo "done"'])
  call s:RunDbgCmd(buf, 'cont')

  call s:RunDbgCmd(buf,
                \ ':debug call FuncWithDict()',
                \ ['cmd: call FuncWithDict()'])
  call s:RunDbgCmd(buf, 'step', ['line 1: var d = {  a: 1,  b: 2,  }'])
  call s:RunDbgCmd(buf, 'step', ['line 6: def Inner()'])
  call s:RunDbgCmd(buf, 'cont')

  call s:RunDbgCmd(buf, ':breakadd func 1 FuncComment')
  call s:RunDbgCmd(buf, ':call FuncComment()', ['function FuncComment', 'line 2: echo "first"  .. "one"'])
  call s:RunDbgCmd(buf, ':breakadd func 3 FuncComment')
  call s:RunDbgCmd(buf, 'cont', ['function FuncComment', 'line 5: echo "second"'])
  call s:RunDbgCmd(buf, 'cont')

  call s:RunDbgCmd(buf, ':breakadd func 2 FuncForLoop')
  call s:RunDbgCmd(buf, ':call FuncForLoop()', ['function FuncForLoop', 'line 2:   for i in [11, 22, 33]'])
  call s:RunDbgCmd(buf, 'step', ['line 2: for i in [11, 22, 33]'])
  call s:RunDbgCmd(buf, 'next', ['function FuncForLoop', 'line 3: eval i + 2'])
  call s:RunDbgCmd(buf, 'echo i', ['11'])
  call s:RunDbgCmd(buf, 'next', ['function FuncForLoop', 'line 4: endfor'])
  call s:RunDbgCmd(buf, 'next', ['function FuncForLoop', 'line 2: for i in [11, 22, 33]'])
  call s:RunDbgCmd(buf, 'next', ['line 3: eval i + 2'])
  call s:RunDbgCmd(buf, 'echo i', ['22'])

  call s:RunDbgCmd(buf, 'breakdel *')
  call s:RunDbgCmd(buf, 'cont')

  call s:RunDbgCmd(buf, ':breakadd func FuncWithSplitLine')
  call s:RunDbgCmd(buf, ':call FuncWithSplitLine()', ['function FuncWithSplitLine', 'line 1: eval 1 + 2 | eval 2 + 3'])

  call s:RunDbgCmd(buf, 'cont')
  call StopVimInTerminal(buf)
endfunc

func Test_debug_def_function_with_lambda()
  CheckCWD
  let lines =<< trim END
     vim9script
     def g:Func()
       var s = 'a'
       ['b']->map((_, v) => s)
       echo "done"
     enddef
     breakadd func 2 g:Func
  END
  call writefile(lines, 'XtestLambda.vim', 'D')

  let buf = RunVimInTerminal('-S XtestLambda.vim', {})

  call s:RunDbgCmd(buf,
                \ ':call g:Func()',
                \ ['function Func', 'line 2: [''b'']->map((_, v) => s)'])
  call s:RunDbgCmd(buf,
                \ 'next',
                \ ['function Func', 'line 3: echo "done"'])

  call s:RunDbgCmd(buf, 'cont')
  call StopVimInTerminal(buf)
endfunc

def Test_debug_backtrace_level()
  CheckCWD
  var lines =<< trim END
    let s:file1_var = 'file1'
    let g:global_var = 'global'

    func s:File1Func( arg )
      let s:file1_var .= a:arg
      let local_var = s:file1_var .. ' test1'
      let g:global_var .= local_var
      source Xtest2.vim
    endfunc

    call s:File1Func( 'arg1' )
  END
  writefile(lines, 'Xtest1.vim', 'D')

  lines =<< trim END
    let s:file2_var = 'file2'

    func s:File2Func( arg )
      let s:file2_var .= a:arg
      let local_var = s:file2_var .. ' test2'
      let g:global_var .= local_var
    endfunc

    call s:File2Func( 'arg2' )
  END
  writefile(lines, 'Xtest2.vim', 'D')

  var file1 = getcwd() .. '/Xtest1.vim'
  var file2 = getcwd() .. '/Xtest2.vim'

  # set a breakpoint and source file1.vim
  var buf = g:RunVimInTerminal(
        '-c "breakadd file 1 Xtest1.vim" -S Xtest1.vim',
        {wait_for_ruler: 0})

  s:CheckDbgOutput(buf, [
        'Breakpoint in "' .. file1 .. '" line 1',
        'Entering Debug mode.  Type "cont" to continue.',
        'command line..script ' .. file1,
        'line 1: let s:file1_var = ''file1'''
        ], {msec: 5000})

  # step through the initial declarations
  s:RunDbgCmd(buf, 'step', [ 'line 2: let g:global_var = ''global''' ] )
  s:RunDbgCmd(buf, 'step', [ 'line 4: func s:File1Func( arg )' ] )
  s:RunDbgCmd(buf, 'echo s:file1_var', [ 'file1' ] )
  s:RunDbgCmd(buf, 'echo g:global_var', [ 'global' ] )
  s:RunDbgCmd(buf, 'echo global_var', [ 'global' ] )

  # step in to the first function
  s:RunDbgCmd(buf, 'step', [ 'line 11: call s:File1Func( ''arg1'' )' ] )
  s:RunDbgCmd(buf, 'step', [ 'line 1: let s:file1_var .= a:arg' ] )
  s:RunDbgCmd(buf, 'echo a:arg', [ 'arg1' ] )
  s:RunDbgCmd(buf, 'echo s:file1_var', [ 'file1' ] )
  s:RunDbgCmd(buf, 'echo g:global_var', [ 'global' ] )
  s:RunDbgCmd(buf,
                'echo global_var',
                [ 'E121: Undefined variable: global_var' ] )
  s:RunDbgCmd(buf,
                'echo local_var',
                [ 'E121: Undefined variable: local_var' ] )
  s:RunDbgCmd(buf,
                'echo l:local_var',
                [ 'E121: Undefined variable: l:local_var' ] )

  # backtrace up
  s:RunDbgCmd(buf, 'backtrace', [
        '\V>backtrace',
        '\V  2 command line',
        '\V  1 script ' .. file1 .. '[11]',
        '\V->0 function <SNR>\.\*_File1Func',
        '\Vline 1: let s:file1_var .= a:arg',
        ],
        { match: 'pattern' } )
  s:RunDbgCmd(buf, 'up', [ '>up' ] )

  s:RunDbgCmd(buf, 'backtrace', [
        '\V>backtrace',
        '\V  2 command line',
        '\V->1 script ' .. file1 .. '[11]',
        '\V  0 function <SNR>\.\*_File1Func',
        '\Vline 1: let s:file1_var .= a:arg',
        ],
        { match: 'pattern' } )

  # Expression evaluation in the script frame (not the function frame)
  # FIXME: Unexpected in this scope (a: should not be visible)
  s:RunDbgCmd(buf, 'echo a:arg', [ 'arg1' ] )
  s:RunDbgCmd(buf, 'echo s:file1_var', [ 'file1' ] )
  s:RunDbgCmd(buf, 'echo g:global_var', [ 'global' ] )
  # FIXME: Unexpected in this scope (global should be found)
  s:RunDbgCmd(buf,
                'echo global_var',
                [ 'E121: Undefined variable: global_var' ] )
  s:RunDbgCmd(buf,
                'echo local_var',
                [ 'E121: Undefined variable: local_var' ] )
  s:RunDbgCmd(buf,
                'echo l:local_var',
                [ 'E121: Undefined variable: l:local_var' ] )


  # step while backtraced jumps to the latest frame
  s:RunDbgCmd(buf, 'step', [
        'line 2: let local_var = s:file1_var .. '' test1''' ] )
  s:RunDbgCmd(buf, 'backtrace', [
        '\V>backtrace',
        '\V  2 command line',
        '\V  1 script ' .. file1 .. '[11]',
        '\V->0 function <SNR>\.\*_File1Func',
        '\Vline 2: let local_var = s:file1_var .. '' test1''',
        ],
        { match: 'pattern' } )

  s:RunDbgCmd(buf, 'step', [ 'line 3: let g:global_var .= local_var' ] )
  s:RunDbgCmd(buf, 'echo local_var', [ 'file1arg1 test1' ] )
  s:RunDbgCmd(buf, 'echo l:local_var', [ 'file1arg1 test1' ] )

  s:RunDbgCmd(buf, 'step', [ 'line 4: source Xtest2.vim' ] )
  s:RunDbgCmd(buf, 'step', [ 'line 1: let s:file2_var = ''file2''' ] )
  s:RunDbgCmd(buf, 'backtrace', [
        '\V>backtrace',
        '\V  3 command line',
        '\V  2 script ' .. file1 .. '[11]',
        '\V  1 function <SNR>\.\*_File1Func[4]',
        '\V->0 script ' .. file2,
        '\Vline 1: let s:file2_var = ''file2''',
        ],
        { match: 'pattern' } )

  # Expression evaluation in the script frame file2 (not the function frame)
  s:RunDbgCmd(buf, 'echo a:arg', [ 'E121: Undefined variable: a:arg' ] )
  s:RunDbgCmd(buf,
        'echo s:file1_var',
        [ 'E121: Undefined variable: s:file1_var' ] )
  s:RunDbgCmd(buf, 'echo g:global_var', [ 'globalfile1arg1 test1' ] )
  s:RunDbgCmd(buf, 'echo global_var', [ 'globalfile1arg1 test1' ] )
  s:RunDbgCmd(buf,
                'echo local_var',
                [ 'E121: Undefined variable: local_var' ] )
  s:RunDbgCmd(buf,
                'echo l:local_var',
                [ 'E121: Undefined variable: l:local_var' ] )
  s:RunDbgCmd(buf,
                'echo s:file2_var',
                [ 'E121: Undefined variable: s:file2_var' ] )

  s:RunDbgCmd(buf, 'step', [ 'line 3: func s:File2Func( arg )' ] )
  s:RunDbgCmd(buf, 'echo s:file2_var', [ 'file2' ] )

  # Up the stack to the other script context
  s:RunDbgCmd(buf, 'up')
  s:RunDbgCmd(buf, 'backtrace', [
        '\V>backtrace',
        '\V  3 command line',
        '\V  2 script ' .. file1 .. '[11]',
        '\V->1 function <SNR>\.\*_File1Func[4]',
        '\V  0 script ' .. file2,
        '\Vline 3: func s:File2Func( arg )',
        ],
        { match: 'pattern' } )
  # FIXME: Unexpected. Should see the a: and l: dicts from File1Func
  s:RunDbgCmd(buf, 'echo a:arg', [ 'E121: Undefined variable: a:arg' ] )
  s:RunDbgCmd(buf,
        'echo l:local_var',
        [ 'E121: Undefined variable: l:local_var' ] )

  s:RunDbgCmd(buf, 'up')
  s:RunDbgCmd(buf, 'backtrace', [
        '\V>backtrace',
        '\V  3 command line',
        '\V->2 script ' .. file1 .. '[11]',
        '\V  1 function <SNR>\.\*_File1Func[4]',
        '\V  0 script ' .. file2,
        '\Vline 3: func s:File2Func( arg )',
        ],
        { match: 'pattern' } )

  # FIXME: Unexpected (wrong script vars are used)
  s:RunDbgCmd(buf,
        'echo s:file1_var',
        [ 'E121: Undefined variable: s:file1_var' ] )
  s:RunDbgCmd(buf, 'echo s:file2_var', [ 'file2' ] )

  s:RunDbgCmd(buf, 'cont')
  g:StopVimInTerminal(buf)
enddef

" Test for setting a breakpoint on a :endif where the :if condition is false
" and then quit the script. This should generate an interrupt.
func Test_breakpt_endif_intr()
  func F()
    let g:Xpath ..= 'a'
    if v:false
      let g:Xpath ..= 'b'
    endif
    invalid_command
  endfunc

  let g:Xpath = ''
  breakadd func 4 F
  try
    let caught_intr = 0
    debuggreedy
    call feedkeys(":call F()\<CR>quit\<CR>", "xt")
  catch /^Vim:Interrupt$/
    call assert_match('\.F, line 4', v:throwpoint)
    let caught_intr = 1
  endtry
  0debuggreedy
  call assert_equal(1, caught_intr)
  call assert_equal('a', g:Xpath)
  breakdel *
  delfunc F
endfunc

" Test for setting a breakpoint on a :else where the :if condition is false
" and then quit the script. This should generate an interrupt.
func Test_breakpt_else_intr()
  func F()
    let g:Xpath ..= 'a'
    if v:false
      let g:Xpath ..= 'b'
    else
      invalid_command
    endif
    invalid_command
  endfunc

  let g:Xpath = ''
  breakadd func 4 F
  try
    let caught_intr = 0
    debuggreedy
    call feedkeys(":call F()\<CR>quit\<CR>", "xt")
  catch /^Vim:Interrupt$/
    call assert_match('\.F, line 4', v:throwpoint)
    let caught_intr = 1
  endtry
  0debuggreedy
  call assert_equal(1, caught_intr)
  call assert_equal('a', g:Xpath)
  breakdel *
  delfunc F
endfunc

" Test for setting a breakpoint on a :endwhile where the :while condition is
" false and then quit the script. This should generate an interrupt.
func Test_breakpt_endwhile_intr()
  func F()
    let g:Xpath ..= 'a'
    while v:false
      let g:Xpath ..= 'b'
    endwhile
    invalid_command
  endfunc

  let g:Xpath = ''
  breakadd func 4 F
  try
    let caught_intr = 0
    debuggreedy
    call feedkeys(":call F()\<CR>quit\<CR>", "xt")
  catch /^Vim:Interrupt$/
    call assert_match('\.F, line 4', v:throwpoint)
    let caught_intr = 1
  endtry
  0debuggreedy
  call assert_equal(1, caught_intr)
  call assert_equal('a', g:Xpath)
  breakdel *
  delfunc F
endfunc

" Test for setting a breakpoint on a script local function
func Test_breakpt_scriptlocal_func()
  let g:Xpath = ''
  func s:G()
    let g:Xpath ..= 'a'
  endfunc

  let funcname = expand("<SID>") .. "G"
  exe "breakadd func 1 " .. funcname
  debuggreedy
  redir => output
  call feedkeys(":call " .. funcname .. "()\<CR>c\<CR>", "xt")
  redir END
  0debuggreedy
  call assert_match('Breakpoint in "' .. funcname .. '" line 1', output)
  call assert_equal('a', g:Xpath)
  breakdel *
  exe "delfunc " .. funcname
endfunc

" vim: shiftwidth=2 sts=2 expandtab
