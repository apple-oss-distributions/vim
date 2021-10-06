" Tests for 'listchars' display with 'list' and :list

source view_util.vim

func Test_listchars()
  enew!
  set ff=unix
  set list

  set listchars+=tab:>-,space:.,trail:<
  call append(0, [
	      \ '	aa	',
	      \ '  bb	  ',
	      \ '   cccc	 ',
	      \ 'dd        ee  	',
	      \ ' '
	      \ ])
  let expected = [
	      \ '>-------aa>-----$',
	      \ '..bb>---<<$',
	      \ '...cccc><$',
	      \ 'dd........ee<<>-$',
	      \ '<$'
	      \ ]
  redraw!
  for i in range(1, 5)
    call cursor(i, 1)
    call assert_equal([expected[i - 1]], ScreenLines(i, '$'->virtcol()))
  endfor

  set listchars-=trail:<
  let expected = [
	      \ '>-------aa>-----$',
	      \ '..bb>---..$',
	      \ '...cccc>.$',
	      \ 'dd........ee..>-$',
	      \ '.$'
	      \ ]
  redraw!
  for i in range(1, 5)
    call cursor(i, 1)
    call assert_equal([expected[i - 1]], ScreenLines(i, virtcol('$')))
  endfor

  " tab with 3rd character.
  set listchars-=tab:>-
  set listchars+=tab:<=>,trail:-
  let expected = [
	      \ '<======>aa<====>$',
	      \ '..bb<==>--$',
	      \ '...cccc>-$',
	      \ 'dd........ee--<>$',
	      \ '-$'
	      \ ]
  redraw!
  for i in range(1, 5)
    call cursor(i, 1)
    call assert_equal([expected[i - 1]], ScreenLines(i, virtcol('$')))
  endfor

  " tab with 3rd character and linebreak set
  set listchars-=tab:<=>
  set listchars+=tab:<·>
  set linebreak
  let expected = [
	      \ '<······>aa<····>$',
	      \ '..bb<··>--$',
	      \ '...cccc>-$',
	      \ 'dd........ee--<>$',
	      \ '-$'
	      \ ]
  redraw!
  for i in range(1, 5)
    call cursor(i, 1)
    call assert_equal([expected[i - 1]], ScreenLines(i, virtcol('$')))
  endfor
  set nolinebreak
  set listchars-=tab:<·>
  set listchars+=tab:<=>

  set listchars-=trail:-
  let expected = [
	      \ '<======>aa<====>$',
	      \ '..bb<==>..$',
	      \ '...cccc>.$',
	      \ 'dd........ee..<>$',
	      \ '.$'
	      \ ]
  redraw!
  for i in range(1, 5)
    call cursor(i, 1)
    call assert_equal([expected[i - 1]], ScreenLines(i, virtcol('$')))
  endfor

  set listchars-=tab:<=>
  set listchars+=tab:>-
  set listchars+=trail:<
  set nolist
  normal ggdG
  call append(0, [
	      \ '  fff	  ',
	      \ '	gg	',
	      \ '     h	',
	      \ 'iii    	  ',
	      \ ])
  let l = split(execute("%list"), "\n")
  call assert_equal([
	      \ '..fff>--<<$',
	      \ '>-------gg>-----$',
	      \ '.....h>-$',
	      \ 'iii<<<<><<$', '$'], l)


  " test nbsp
  normal ggdG
  set listchars=nbsp:X,trail:Y
  set list
  " Non-breaking space
  let nbsp = nr2char(0xa0)
  call append(0, [ ">".nbsp."<" ])

  let expected = '>X< '

  redraw!
  call cursor(1, 1)
  call assert_equal([expected], ScreenLines(1, virtcol('$')))

  set listchars=nbsp:X
  redraw!
  call cursor(1, 1)
  call assert_equal([expected], ScreenLines(1, virtcol('$')))

  " test extends
  normal ggdG
  set listchars=extends:Z
  set nowrap
  set nolist
  call append(0, [ repeat('A', &columns + 1) ])

  let expected = repeat('A', &columns)

  redraw!
  call cursor(1, 1)
  call assert_equal([expected], ScreenLines(1, &columns))

  set list
  let expected = expected[:-2] . 'Z'
  redraw!
  call cursor(1, 1)
  call assert_equal([expected], ScreenLines(1, &columns))

  enew!
  set listchars& ff&
endfunc

" Test that unicode listchars characters get properly inserted
func Test_listchars_unicode()
  enew!
  let oldencoding=&encoding
  set encoding=utf-8
  set ff=unix

  set listchars=eol:⇔,space:␣,nbsp:≠,tab:←↔→
  set list

  let nbsp = nr2char(0xa0)
  call append(0, [
        \ "a\tb c".nbsp."d"
        \ ])
  let expected = [
        \ 'a←↔↔↔↔↔→b␣c≠d⇔'
        \ ]
  redraw!
  call cursor(1, 1)
  call assert_equal(expected, ScreenLines(1, virtcol('$')))
  let &encoding=oldencoding
  enew!
  set listchars& ff&
endfunction

" Tests that space characters following composing character won't get replaced
" by listchars.
func Test_listchars_composing()
  enew!
  let oldencoding=&encoding
  set encoding=utf-8
  set ff=unix
  set list

  set listchars=eol:$,space:_,nbsp:=
  
  let nbsp1 = nr2char(0xa0)
  let nbsp2 = nr2char(0x202f)
  call append(0, [
        \ "  \u3099\t \u309A".nbsp1.nbsp1."\u0302".nbsp2.nbsp2."\u0302",
        \ ])
  let expected = [
        \ "_ \u3099^I \u309A=".nbsp1."\u0302=".nbsp2."\u0302$"
        \ ]
  redraw!
  call cursor(1, 1)
  call assert_equal(expected, ScreenLines(1, virtcol('$')))
  let &encoding=oldencoding
  enew!
  set listchars& ff&
endfunction
