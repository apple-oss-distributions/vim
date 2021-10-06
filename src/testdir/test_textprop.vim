" Tests for defining text property types and adding text properties to the
" buffer.

source check.vim
CheckFeature textprop

source screendump.vim

" test length zero

func Test_proptype_global()
  call prop_type_add('comment', {'highlight': 'Directory', 'priority': 123, 'start_incl': 1, 'end_incl': 1})
  let proptypes = prop_type_list()
  call assert_equal(1, len(proptypes))
  call assert_equal('comment', proptypes[0])

  let proptype = prop_type_get('comment')
  call assert_equal('Directory', proptype['highlight'])
  call assert_equal(123, proptype['priority'])
  call assert_equal(1, proptype['start_incl'])
  call assert_equal(1, proptype['end_incl'])

  call prop_type_delete('comment')
  call assert_equal(0, len(prop_type_list()))

  call prop_type_add('one', {})
  call assert_equal(1, len(prop_type_list()))
  let proptype = 'one'->prop_type_get()
  call assert_false(has_key(proptype, 'highlight'))
  call assert_equal(0, proptype['priority'])
  call assert_equal(0, proptype['start_incl'])
  call assert_equal(0, proptype['end_incl'])

  call prop_type_add('two', {})
  call assert_equal(2, len(prop_type_list()))
  call prop_type_delete('one')
  call assert_equal(1, len(prop_type_list()))
  call prop_type_delete('two')
  call assert_equal(0, len(prop_type_list()))
endfunc

func Test_proptype_buf()
  let bufnr = bufnr('')
  call prop_type_add('comment', {'bufnr': bufnr, 'highlight': 'Directory', 'priority': 123, 'start_incl': 1, 'end_incl': 1})
  let proptypes = prop_type_list({'bufnr': bufnr})
  call assert_equal(1, len(proptypes))
  call assert_equal('comment', proptypes[0])

  let proptype = prop_type_get('comment', {'bufnr': bufnr})
  call assert_equal('Directory', proptype['highlight'])
  call assert_equal(123, proptype['priority'])
  call assert_equal(1, proptype['start_incl'])
  call assert_equal(1, proptype['end_incl'])

  call prop_type_delete('comment', {'bufnr': bufnr})
  call assert_equal(0, len({'bufnr': bufnr}->prop_type_list()))

  call prop_type_add('one', {'bufnr': bufnr})
  let proptype = prop_type_get('one', {'bufnr': bufnr})
  call assert_false(has_key(proptype, 'highlight'))
  call assert_equal(0, proptype['priority'])
  call assert_equal(0, proptype['start_incl'])
  call assert_equal(0, proptype['end_incl'])

  call prop_type_add('two', {'bufnr': bufnr})
  call assert_equal(2, len(prop_type_list({'bufnr': bufnr})))
  call prop_type_delete('one', {'bufnr': bufnr})
  call assert_equal(1, len(prop_type_list({'bufnr': bufnr})))
  call prop_type_delete('two', {'bufnr': bufnr})
  call assert_equal(0, len(prop_type_list({'bufnr': bufnr})))

  call assert_fails("call prop_type_add('one', {'bufnr': 98764})", "E158:")
endfunc

func AddPropTypes()
  call prop_type_add('one', {})
  call prop_type_add('two', {})
  call prop_type_add('three', {})
  call prop_type_add('whole', {})
endfunc

func DeletePropTypes()
  call prop_type_delete('one')
  call prop_type_delete('two')
  call prop_type_delete('three')
  call prop_type_delete('whole')
endfunc

func SetupPropsInFirstLine()
  call setline(1, 'one two three')
  call prop_add(1, 1, {'length': 3, 'id': 11, 'type': 'one'})
  eval 1->prop_add(5, {'length': 3, 'id': 12, 'type': 'two'})
  call prop_add(1, 9, {'length': 5, 'id': 13, 'type': 'three'})
  call prop_add(1, 1, {'length': 13, 'id': 14, 'type': 'whole'})
endfunc

func Get_expected_props()
  return [
      \ {'col': 1, 'length': 13, 'id': 14, 'type': 'whole', 'start': 1, 'end': 1},
      \ {'col': 1, 'length': 3, 'id': 11, 'type': 'one', 'start': 1, 'end': 1},
      \ {'col': 5, 'length': 3, 'id': 12, 'type': 'two', 'start': 1, 'end': 1},
      \ {'col': 9, 'length': 5, 'id': 13, 'type': 'three', 'start': 1, 'end': 1},
      \ ]
endfunc

func Test_prop_add()
  new
  call AddPropTypes()
  call SetupPropsInFirstLine()
  let expected_props = Get_expected_props()
  call assert_equal(expected_props, prop_list(1))
  call assert_fails("call prop_add(10, 1, {'length': 1, 'id': 14, 'type': 'whole'})", 'E966:')
  call assert_fails("call prop_add(1, 22, {'length': 1, 'id': 14, 'type': 'whole'})", 'E964:')
 
  " Insert a line above, text props must still be there.
  call append(0, 'empty')
  call assert_equal(expected_props, prop_list(2))
  " Delete a line above, text props must still be there.
  1del
  call assert_equal(expected_props, prop_list(1))

  " Prop without length or end column is zero length
  call prop_clear(1)
  call prop_add(1, 5, {'type': 'two'})
  let expected = [{'col': 5, 'length': 0, 'type': 'two', 'id': 0, 'start': 1, 'end': 1}]
  call assert_equal(expected, prop_list(1))

  call assert_fails("call prop_add(1, 5, {'type': 'two', 'bufnr': 234343})", 'E158:')

  call DeletePropTypes()
  bwipe!
endfunc

func Test_prop_remove()
  new
  call AddPropTypes()
  call SetupPropsInFirstLine()
  let props = Get_expected_props()
  call assert_equal(props, prop_list(1))

  " remove by id
  call assert_equal(1, {'id': 12}->prop_remove(1))
  unlet props[2]
  call assert_equal(props, prop_list(1))

  " remove by type
  call assert_equal(1, prop_remove({'type': 'one'}, 1))
  unlet props[1]
  call assert_equal(props, prop_list(1))

  " remove from unknown buffer
  call assert_fails("call prop_remove({'type': 'one', 'bufnr': 123456}, 1)", 'E158:')

  call DeletePropTypes()
  bwipe!
endfunc

func SetupOneLine()
  call setline(1, 'xonex xtwoxx')
  normal gg0
  call AddPropTypes()
  call prop_add(1, 2, {'length': 3, 'id': 11, 'type': 'one'})
  call prop_add(1, 8, {'length': 3, 'id': 12, 'type': 'two'})
  let expected = [
	\ {'col': 2, 'length': 3, 'id': 11, 'type': 'one', 'start': 1, 'end': 1},
	\ {'col': 8, 'length': 3, 'id': 12, 'type': 'two', 'start': 1, 'end': 1},
	\]
  call assert_equal(expected, prop_list(1))
  return expected
endfunc

func Test_prop_add_remove_buf()
  new
  let bufnr = bufnr('')
  call AddPropTypes()
  for lnum in range(1, 4)
    call setline(lnum, 'one two three')
  endfor
  wincmd w
  for lnum in range(1, 4)
    call prop_add(lnum, 1, {'length': 3, 'id': 11, 'type': 'one', 'bufnr': bufnr})
    call prop_add(lnum, 5, {'length': 3, 'id': 12, 'type': 'two', 'bufnr': bufnr})
    call prop_add(lnum, 11, {'length': 3, 'id': 13, 'type': 'three', 'bufnr': bufnr})
  endfor

  let props = [
	\ {'col': 1, 'length': 3, 'id': 11, 'type': 'one', 'start': 1, 'end': 1},
	\ {'col': 5, 'length': 3, 'id': 12, 'type': 'two', 'start': 1, 'end': 1},
	\ {'col': 11, 'length': 3, 'id': 13, 'type': 'three', 'start': 1, 'end': 1},
	\]
  call assert_equal(props, prop_list(1, {'bufnr': bufnr}))

  " remove by id
  let before_props = deepcopy(props)
  unlet props[1]

  call prop_remove({'id': 12, 'bufnr': bufnr}, 1)
  call assert_equal(props, prop_list(1, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(2, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(3, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(4, {'bufnr': bufnr}))

  call prop_remove({'id': 12, 'bufnr': bufnr}, 3, 4)
  call assert_equal(props, prop_list(1, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(2, {'bufnr': bufnr}))
  call assert_equal(props, prop_list(3, {'bufnr': bufnr}))
  call assert_equal(props, prop_list(4, {'bufnr': bufnr}))

  call prop_remove({'id': 12, 'bufnr': bufnr})
  for lnum in range(1, 4)
    call assert_equal(props, prop_list(lnum, {'bufnr': bufnr}))
  endfor

  " remove by type
  let before_props = deepcopy(props)
  unlet props[0]

  call prop_remove({'type': 'one', 'bufnr': bufnr}, 1)
  call assert_equal(props, prop_list(1, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(2, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(3, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(4, {'bufnr': bufnr}))

  call prop_remove({'type': 'one', 'bufnr': bufnr}, 3, 4)
  call assert_equal(props, prop_list(1, {'bufnr': bufnr}))
  call assert_equal(before_props, prop_list(2, {'bufnr': bufnr}))
  call assert_equal(props, prop_list(3, {'bufnr': bufnr}))
  call assert_equal(props, prop_list(4, {'bufnr': bufnr}))

  call prop_remove({'type': 'one', 'bufnr': bufnr})
  for lnum in range(1, 4)
    call assert_equal(props, prop_list(lnum, {'bufnr': bufnr}))
  endfor

  call DeletePropTypes()
  wincmd w
  bwipe!
endfunc

func Test_prop_backspace()
  new
  set bs=2
  let expected = SetupOneLine() " 'xonex xtwoxx'

  exe "normal 0li\<BS>\<Esc>fxli\<BS>\<Esc>"
  call assert_equal('one xtwoxx', getline(1))
  let expected[0].col = 1
  let expected[1].col = 6
  call assert_equal(expected, prop_list(1))

  call DeletePropTypes()
  bwipe!
  set bs&
endfunc

func Test_prop_replace()
  new
  set bs=2
  let expected = SetupOneLine() " 'xonex xtwoxx'

  exe "normal 0Ryyy\<Esc>"
  call assert_equal('yyyex xtwoxx', getline(1))
  call assert_equal(expected, prop_list(1))

  exe "normal ftRyy\<BS>"
  call assert_equal('yyyex xywoxx', getline(1))
  call assert_equal(expected, prop_list(1))

  exe "normal 0fwRyy\<BS>"
  call assert_equal('yyyex xyyoxx', getline(1))
  call assert_equal(expected, prop_list(1))

  exe "normal 0foRyy\<BS>\<BS>"
  call assert_equal('yyyex xyyoxx', getline(1))
  call assert_equal(expected, prop_list(1))

  call DeletePropTypes()
  bwipe!
  set bs&
endfunc

func Test_prop_open_line()
  new

  " open new line, props stay in top line
  let expected = SetupOneLine() " 'xonex xtwoxx'
  exe "normal o\<Esc>"
  call assert_equal('xonex xtwoxx', getline(1))
  call assert_equal('', getline(2))
  call assert_equal(expected, prop_list(1))
  call DeletePropTypes()

  " move all props to next line
  let expected = SetupOneLine() " 'xonex xtwoxx'
  exe "normal 0i\<CR>\<Esc>"
  call assert_equal('', getline(1))
  call assert_equal('xonex xtwoxx', getline(2))
  call assert_equal(expected, prop_list(2))
  call DeletePropTypes()

  " split just before prop, move all props to next line
  let expected = SetupOneLine() " 'xonex xtwoxx'
  exe "normal 0li\<CR>\<Esc>"
  call assert_equal('x', getline(1))
  call assert_equal('onex xtwoxx', getline(2))
  let expected[0].col -= 1
  let expected[1].col -= 1
  call assert_equal(expected, prop_list(2))
  call DeletePropTypes()

  " split inside prop, split first prop
  let expected = SetupOneLine() " 'xonex xtwoxx'
  exe "normal 0lli\<CR>\<Esc>"
  call assert_equal('xo', getline(1))
  call assert_equal('nex xtwoxx', getline(2))
  let exp_first = [deepcopy(expected[0])]
  let exp_first[0].length = 1
  call assert_equal(exp_first, prop_list(1))
  let expected[0].col = 1
  let expected[0].length = 2
  let expected[1].col -= 2
  call assert_equal(expected, prop_list(2))
  call DeletePropTypes()

  " split just after first prop, second prop move to next line
  let expected = SetupOneLine() " 'xonex xtwoxx'
  exe "normal 0fea\<CR>\<Esc>"
  call assert_equal('xone', getline(1))
  call assert_equal('x xtwoxx', getline(2))
  let exp_first = expected[0:0]
  call assert_equal(exp_first, prop_list(1))
  let expected = expected[1:1]
  let expected[0].col -= 4
  call assert_equal(expected, prop_list(2))
  call DeletePropTypes()

  bwipe!
  set bs&
endfunc

func Test_prop_clear()
  new
  call AddPropTypes()
  call SetupPropsInFirstLine()
  call assert_equal(Get_expected_props(), prop_list(1))

  eval 1->prop_clear()
  call assert_equal([], 1->prop_list())

  call DeletePropTypes()
  bwipe!
endfunc

func Test_prop_clear_buf()
  new
  call AddPropTypes()
  call SetupPropsInFirstLine()
  let bufnr = bufnr('')
  wincmd w
  call assert_equal(Get_expected_props(), prop_list(1, {'bufnr': bufnr}))

  call prop_clear(1, 1, {'bufnr': bufnr})
  call assert_equal([], prop_list(1, {'bufnr': bufnr}))

  wincmd w
  call DeletePropTypes()
  bwipe!
endfunc

func Test_prop_setline()
  new
  call AddPropTypes()
  call SetupPropsInFirstLine()
  call assert_equal(Get_expected_props(), prop_list(1))

  call setline(1, 'foobar')
  call assert_equal([], prop_list(1))

  call DeletePropTypes()
  bwipe!
endfunc

func Test_prop_setbufline()
  new
  call AddPropTypes()
  call SetupPropsInFirstLine()
  let bufnr = bufnr('')
  wincmd w
  call assert_equal(Get_expected_props(), prop_list(1, {'bufnr': bufnr}))

  call setbufline(bufnr, 1, 'foobar')
  call assert_equal([], prop_list(1, {'bufnr': bufnr}))

  wincmd w
  call DeletePropTypes()
  bwipe!
endfunc

func Test_prop_substitute()
  new
  " Set first line to 'one two three'
  call AddPropTypes()
  call SetupPropsInFirstLine()
  let expected_props = Get_expected_props()
  call assert_equal(expected_props, prop_list(1))

  " Change "n" in "one" to XX: 'oXXe two three'
  s/n/XX/
  let expected_props[0].length += 1
  let expected_props[1].length += 1
  let expected_props[2].col += 1
  let expected_props[3].col += 1
  call assert_equal(expected_props, prop_list(1))

  " Delete "t" in "two" and "three" to XX: 'oXXe wo hree'
  s/t//g
  let expected_props[0].length -= 2
  let expected_props[2].length -= 1
  let expected_props[3].length -= 1
  let expected_props[3].col -= 1
  call assert_equal(expected_props, prop_list(1))

  " Split the line by changing w to line break: 'oXXe ', 'o hree'
  " The long prop is split and spans both lines.
  " The props on "two" and "three" move to the next line.
  s/w/\r/
  let new_props = [
	\ copy(expected_props[0]),
	\ copy(expected_props[2]),
	\ copy(expected_props[3]),
	\ ]
  let expected_props[0].length = 5
  unlet expected_props[3]
  unlet expected_props[2]
  call assert_equal(expected_props, prop_list(1))

  let new_props[0].length = 6
  let new_props[1].col = 1
  let new_props[1].length = 1
  let new_props[2].col = 3
  call assert_equal(new_props, prop_list(2))

  call DeletePropTypes()
  bwipe!
endfunc

func Test_prop_change_indent()
  call prop_type_add('comment', {'highlight': 'Directory'})
  new
  call setline(1, ['    xxx', 'yyyyy'])
  call prop_add(2, 2, {'length': 2, 'type': 'comment'})
  let expect = {'col': 2, 'length': 2, 'type': 'comment', 'start': 1, 'end': 1, 'id': 0}
  call assert_equal([expect], prop_list(2))

  set shiftwidth=3
  normal 2G>>
  call assert_equal('   yyyyy', getline(2))
  let expect.col += 3
  call assert_equal([expect], prop_list(2))

  normal 2G==
  call assert_equal('    yyyyy', getline(2))
  let expect.col = 6
  call assert_equal([expect], prop_list(2))

  call prop_clear(2)
  call prop_add(2, 2, {'length': 5, 'type': 'comment'})
  let expect.col = 2
  let expect.length = 5
  call assert_equal([expect], prop_list(2))

  normal 2G<<
  call assert_equal(' yyyyy', getline(2))
  let expect.length = 2
  call assert_equal([expect], prop_list(2))

  set shiftwidth&
  call prop_type_delete('comment')
endfunc

" Setup a three line prop in lines 2 - 4.
" Add short props in line 1 and 5.
func Setup_three_line_prop()
  new
  call setline(1, ['one', 'twotwo', 'three', 'fourfour', 'five'])
  call prop_add(1, 2, {'length': 1, 'type': 'comment'})
  call prop_add(2, 4, {'end_lnum': 4, 'end_col': 5, 'type': 'comment'})
  call prop_add(5, 2, {'length': 1, 'type': 'comment'})
endfunc

func Test_prop_multiline()
  eval 'comment'->prop_type_add({'highlight': 'Directory'})
  new
  call setline(1, ['xxxxxxx', 'yyyyyyyyy', 'zzzzzzzz'])

  " start halfway line 1, end halfway line 3
  call prop_add(1, 3, {'end_lnum': 3, 'end_col': 5, 'type': 'comment'})
  let expect1 = {'col': 3, 'length': 6, 'type': 'comment', 'start': 1, 'end': 0, 'id': 0}
  call assert_equal([expect1], prop_list(1))
  let expect2 = {'col': 1, 'length': 10, 'type': 'comment', 'start': 0, 'end': 0, 'id': 0}
  call assert_equal([expect2], prop_list(2))
  let expect3 = {'col': 1, 'length': 4, 'type': 'comment', 'start': 0, 'end': 1, 'id': 0}
  call assert_equal([expect3], prop_list(3))
  call prop_clear(1, 3)

  " include all three lines
  call prop_add(1, 1, {'end_lnum': 3, 'end_col': 999, 'type': 'comment'})
  let expect1.col = 1
  let expect1.length = 8
  call assert_equal([expect1], prop_list(1))
  call assert_equal([expect2], prop_list(2))
  let expect3.length = 9
  call assert_equal([expect3], prop_list(3))
  call prop_clear(1, 3)

  bwipe!

  " Test deleting the first line of a multi-line prop.
  call Setup_three_line_prop()
  let expect_short = {'col': 2, 'length': 1, 'type': 'comment', 'start': 1, 'end': 1, 'id': 0}
  call assert_equal([expect_short], prop_list(1))
  let expect2 = {'col': 4, 'length': 4, 'type': 'comment', 'start': 1, 'end': 0, 'id': 0}
  call assert_equal([expect2], prop_list(2))
  2del
  call assert_equal([expect_short], prop_list(1))
  let expect2 = {'col': 1, 'length': 6, 'type': 'comment', 'start': 1, 'end': 0, 'id': 0}
  call assert_equal([expect2], prop_list(2))
  bwipe!

  " Test deleting the last line of a multi-line prop.
  call Setup_three_line_prop()
  let expect3 = {'col': 1, 'length': 6, 'type': 'comment', 'start': 0, 'end': 0, 'id': 0}
  call assert_equal([expect3], prop_list(3))
  let expect4 = {'col': 1, 'length': 4, 'type': 'comment', 'start': 0, 'end': 1, 'id': 0}
  call assert_equal([expect4], prop_list(4))
  4del
  let expect3.end = 1
  call assert_equal([expect3], prop_list(3))
  call assert_equal([expect_short], prop_list(4))
  bwipe!

  " Test appending a line below the multi-line text prop start.
  call Setup_three_line_prop()
  let expect2 = {'col': 4, 'length': 4, 'type': 'comment', 'start': 1, 'end': 0, 'id': 0}
  call assert_equal([expect2], prop_list(2))
  call append(2, "new line")
  call assert_equal([expect2], prop_list(2))
  let expect3 = {'col': 1, 'length': 9, 'type': 'comment', 'start': 0, 'end': 0, 'id': 0}
  call assert_equal([expect3], prop_list(3))
  bwipe!

  call prop_type_delete('comment')
endfunc

func Test_prop_byteoff()
  call prop_type_add('comment', {'highlight': 'Directory'})
  new
  call setline(1, ['line1', 'second line', ''])
  set ff=unix
  call assert_equal(19, line2byte(3))
  call prop_add(1, 1, {'end_col': 3, 'type': 'comment'})
  call assert_equal(19, line2byte(3))

  bwipe!
  call prop_type_delete('comment')
endfunc

func Test_prop_undo()
  new
  call prop_type_add('comment', {'highlight': 'Directory'})
  call setline(1, ['oneone', 'twotwo', 'three'])
  " Set 'undolevels' to break changes into undo-able pieces.
  set ul&

  call prop_add(1, 3, {'end_col': 5, 'type': 'comment'})
  let expected = [{'col': 3, 'length': 2, 'id': 0, 'type': 'comment', 'start': 1, 'end': 1} ]
  call assert_equal(expected, prop_list(1))

  " Insert a character, then undo.
  exe "normal 0lllix\<Esc>"
  set ul&
  let expected[0].length = 3
  call assert_equal(expected, prop_list(1))
  undo
  let expected[0].length = 2
  call assert_equal(expected, prop_list(1))

  " Delete a character, then undo
  exe "normal 0lllx"
  set ul&
  let expected[0].length = 1
  call assert_equal(expected, prop_list(1))
  undo
  let expected[0].length = 2
  call assert_equal(expected, prop_list(1))

  " Delete the line, then undo
  1d
  set ul&
  call assert_equal([], prop_list(1))
  undo
  call assert_equal(expected, prop_list(1))

  " Insert a character, delete two characters, then undo with "U"
  exe "normal 0lllix\<Esc>"
  set ul&
  let expected[0].length = 3
  call assert_equal(expected, prop_list(1))
  exe "normal 0lllxx"
  set ul&
  let expected[0].length = 1
  call assert_equal(expected, prop_list(1))
  normal U
  let expected[0].length = 2
  call assert_equal(expected, prop_list(1))

  " substitute a word, then undo
  call setline(1, 'the number 123 is highlighted.')
  call prop_add(1, 12, {'length': 3, 'type': 'comment'})
  let expected = [{'col': 12, 'length': 3, 'id': 0, 'type': 'comment', 'start': 1, 'end': 1} ]
  call assert_equal(expected, prop_list(1))
  set ul&
  1s/number/foo
  let expected[0].col = 9
  call assert_equal(expected, prop_list(1))
  undo
  let expected[0].col = 12
  call assert_equal(expected, prop_list(1))
  call prop_clear(1)

  " substitute with backslash
  call setline(1, 'the number 123 is highlighted.')
  call prop_add(1, 12, {'length': 3, 'type': 'comment'})
  let expected = [{'col': 12, 'length': 3, 'id': 0, 'type': 'comment', 'start': 1, 'end': 1} ]
  call assert_equal(expected, prop_list(1))
  1s/the/\The
  call assert_equal(expected, prop_list(1))
  1s/^/\\
  let expected[0].col += 1
  call assert_equal(expected, prop_list(1))
  1s/^/\~
  let expected[0].col += 1
  call assert_equal(expected, prop_list(1))
  1s/123/12\\3
  let expected[0].length += 1
  call assert_equal(expected, prop_list(1))
  call prop_clear(1)

  bwipe!
  call prop_type_delete('comment')
endfunc

" screenshot test with textprop highlighting
func Test_textprop_screenshot_various()
  CheckScreendump
  " The Vim running in the terminal needs to use utf-8.
  if g:orig_encoding != 'utf-8'
    throw 'Skipped: not using utf-8'
  endif
  call writefile([
	\ "call setline(1, ["
	\	.. "'One two',"
	\	.. "'Numbér 123 änd thœn 4¾7.',"
	\	.. "'--aa--bb--cc--dd--',"
	\	.. "'// comment with error in it',"
	\	.. "'first line',"
	\	.. "'  second line  ',"
	\	.. "'third line',"
	\	.. "'   fourth line',"
	\	.. "])",
	\ "hi NumberProp ctermfg=blue",
	\ "hi LongProp ctermbg=yellow",
	\ "hi BackgroundProp ctermbg=lightgrey",
	\ "hi UnderlineProp cterm=underline",
	\ "call prop_type_add('number', {'highlight': 'NumberProp'})",
	\ "call prop_type_add('long', {'highlight': 'NumberProp'})",
	\ "call prop_type_change('long', {'highlight': 'LongProp'})",
	\ "call prop_type_add('start', {'highlight': 'NumberProp', 'start_incl': 1})",
	\ "call prop_type_add('end', {'highlight': 'NumberProp', 'end_incl': 1})",
	\ "call prop_type_add('both', {'highlight': 'NumberProp', 'start_incl': 1, 'end_incl': 1})",
	\ "call prop_type_add('background', {'highlight': 'BackgroundProp', 'combine': 0})",
	\ "call prop_type_add('backgroundcomb', {'highlight': 'NumberProp', 'combine': 1})",
	\ "eval 'backgroundcomb'->prop_type_change({'highlight': 'BackgroundProp'})",
	\ "call prop_type_add('error', {'highlight': 'UnderlineProp', 'combine': 1})",
	\ "call prop_add(1, 4, {'end_lnum': 3, 'end_col': 3, 'type': 'long'})",
	\ "call prop_add(2, 9, {'length': 3, 'type': 'number'})",
	\ "call prop_add(2, 24, {'length': 4, 'type': 'number'})",
	\ "call prop_add(3, 3, {'length': 2, 'type': 'number'})",
	\ "call prop_add(3, 7, {'length': 2, 'type': 'start'})",
	\ "call prop_add(3, 11, {'length': 2, 'type': 'end'})",
	\ "call prop_add(3, 15, {'length': 2, 'type': 'both'})",
	\ "call prop_add(4, 6, {'length': 3, 'type': 'background'})",
	\ "call prop_add(4, 12, {'length': 10, 'type': 'backgroundcomb'})",
	\ "call prop_add(4, 17, {'length': 5, 'type': 'error'})",
	\ "call prop_add(5, 7, {'length': 4, 'type': 'long'})",
	\ "call prop_add(6, 1, {'length': 8, 'type': 'long'})",
	\ "call prop_add(8, 1, {'length': 1, 'type': 'long'})",
	\ "call prop_add(8, 11, {'length': 4, 'type': 'long'})",
	\ "set number cursorline",
	\ "hi clear SpellBad",
	\ "set spell",
	\ "syn match Comment '//.*'",
	\ "hi Comment ctermfg=green",
	\ "normal 3G0llix\<Esc>lllix\<Esc>lllix\<Esc>lllix\<Esc>lllix\<Esc>lllix\<Esc>lllix\<Esc>lllix\<Esc>",
	\ "normal 3G0lli\<BS>\<Esc>",
	\ "normal 6G0i\<BS>\<Esc>",
	\ "normal 3J",
	\ "normal 3G",
	\], 'XtestProp')
  let buf = RunVimInTerminal('-S XtestProp', {'rows': 8})
  call VerifyScreenDump(buf, 'Test_textprop_01', {})

  " clean up
  call StopVimInTerminal(buf)
  call delete('XtestProp')
endfunc

func RunTestVisualBlock(width, dump)
  call writefile([
	\ "call setline(1, ["
	\	.. "'xxxxxxxxx 123 x',"
	\	.. "'xxxxxxxx 123 x',"
	\	.. "'xxxxxxx 123 x',"
	\	.. "'xxxxxx 123 x',"
	\	.. "'xxxxx 123 x',"
	\	.. "'xxxx 123 xx',"
	\	.. "'xxx 123 xxx',"
	\	.. "'xx 123 xxxx',"
	\	.. "'x 123 xxxxx',"
	\	.. "' 123 xxxxxx',"
	\	.. "])",
	\ "hi SearchProp ctermbg=yellow",
	\ "call prop_type_add('search', {'highlight': 'SearchProp'})",
	\ "call prop_add(1, 11, {'length': 3, 'type': 'search'})",
	\ "call prop_add(2, 10, {'length': 3, 'type': 'search'})",
	\ "call prop_add(3, 9, {'length': 3, 'type': 'search'})",
	\ "call prop_add(4, 8, {'length': 3, 'type': 'search'})",
	\ "call prop_add(5, 7, {'length': 3, 'type': 'search'})",
	\ "call prop_add(6, 6, {'length': 3, 'type': 'search'})",
	\ "call prop_add(7, 5, {'length': 3, 'type': 'search'})",
	\ "call prop_add(8, 4, {'length': 3, 'type': 'search'})",
	\ "call prop_add(9, 3, {'length': 3, 'type': 'search'})",
	\ "call prop_add(10, 2, {'length': 3, 'type': 'search'})",
	\ "normal 1G6|\<C-V>" .. repeat('l', a:width - 1) .. "10jx",
	\], 'XtestPropVis')
  let buf = RunVimInTerminal('-S XtestPropVis', {'rows': 12})
  call VerifyScreenDump(buf, 'Test_textprop_vis_' .. a:dump, {})

  " clean up
  call StopVimInTerminal(buf)
  call delete('XtestPropVis')
endfunc

" screenshot test with Visual block mode operations
func Test_textprop_screenshot_visual()
  CheckScreendump

  " Delete two columns while text props are three chars wide.
  call RunTestVisualBlock(2, '01')

  " Same, but delete four columns
  call RunTestVisualBlock(4, '02')
endfunc

func Test_textprop_after_tab()
  CheckScreendump

  let lines =<< trim END
       call setline(1, [
             \ "\txxx",
             \ "x\txxx",
             \ ])
       hi SearchProp ctermbg=yellow
       call prop_type_add('search', {'highlight': 'SearchProp'})
       call prop_add(1, 2, {'length': 3, 'type': 'search'})
       call prop_add(2, 3, {'length': 3, 'type': 'search'})
  END
  call writefile(lines, 'XtestPropTab')
  let buf = RunVimInTerminal('-S XtestPropTab', {'rows': 6})
  call VerifyScreenDump(buf, 'Test_textprop_tab', {})

  " clean up
  call StopVimInTerminal(buf)
  call delete('XtestPropTab')
endfunc

func Test_textprop_with_syntax()
  CheckScreendump

  let lines =<< trim END
       call setline(1, [
             \ "(abc)",
             \ ])
       syn match csParens "[()]" display
       hi! link csParens MatchParen

       call prop_type_add('TPTitle', #{ highlight: 'Title' })
       call prop_add(1, 2, #{type: 'TPTitle', end_col: 5})
  END
  call writefile(lines, 'XtestPropSyn')
  let buf = RunVimInTerminal('-S XtestPropSyn', {'rows': 6})
  call VerifyScreenDump(buf, 'Test_textprop_syn_1', {})

  " clean up
  call StopVimInTerminal(buf)
  call delete('XtestPropSyn')
endfunc

" Adding a text property to a new buffer should not fail
func Test_textprop_empty_buffer()
  call prop_type_add('comment', {'highlight': 'Search'})
  new
  call prop_add(1, 1, {'type': 'comment'})
  close
  call prop_type_delete('comment')
endfunc

" Adding a text property with invalid highlight should be ignored.
func Test_textprop_invalid_highlight()
  call assert_fails("call prop_type_add('dni', {'highlight': 'DoesNotExist'})", 'E970:')
  new
  call setline(1, ['asdf','asdf'])
  call prop_add(1, 1, {'length': 4, 'type': 'dni'})
  redraw
  bwipe!
  call prop_type_delete('dni')
endfunc

" Adding a text property to an empty buffer and then editing another
func Test_textprop_empty_buffer_next()
  call prop_type_add("xxx", {})
  call prop_add(1, 1, {"type": "xxx"})
  next X
  call prop_type_delete('xxx')
endfunc

func Test_textprop_remove_from_buf()
  new
  let buf = bufnr('')
  call prop_type_add('one', {'bufnr': buf})
  call prop_add(1, 1, {'type': 'one', 'id': 234})
  file x
  edit y
  call prop_remove({'id': 234, 'bufnr': buf}, 1)
  call prop_type_delete('one', {'bufnr': buf})
  bwipe! x
  close
endfunc

func Test_textprop_in_unloaded_buf()
  edit Xaaa
  call setline(1, 'aaa')
  write
  edit Xbbb
  call setline(1, 'bbb')
  write
  let bnr = bufnr('')
  edit Xaaa

  call prop_type_add('ErrorMsg', #{highlight:'ErrorMsg'})
  call assert_fails("call prop_add(1, 1, #{end_lnum: 1, endcol: 2, type: 'ErrorMsg', bufnr: bnr})", 'E275:')
  exe 'buf ' .. bnr
  call assert_equal('bbb', getline(1))
  call assert_equal(0, prop_list(1)->len())

  bwipe! Xaaa
  bwipe! Xbbb
  cal delete('Xaaa')
  cal delete('Xbbb')
endfunc

func Test_proptype_substitute2()
  new
  " text_prop.vim
  call setline(1, [
        \ 'The   num  123 is smaller than 4567.',
        \ '123 The number 123 is smaller than 4567.',
        \ '123 The number 123 is smaller than 4567.'])

  call prop_type_add('number', {'highlight': 'ErrorMsg'})

  call prop_add(1, 12, {'length': 3, 'type': 'number'})
  call prop_add(2, 1, {'length': 3, 'type': 'number'})
  call prop_add(3, 36, {'length': 4, 'type': 'number'})
  set ul&
  let expected = [{'id': 0, 'col': 13, 'end': 1, 'type': 'number', 'length': 3, 'start': 1}, 
        \ {'id': 0, 'col': 1, 'end': 1, 'type': 'number', 'length': 3, 'start': 1}, 
        \ {'id': 0, 'col': 50, 'end': 1, 'type': 'number', 'length': 4, 'start': 1}]
  " Add some text in between
  %s/\s\+/   /g
  call assert_equal(expected, prop_list(1) + prop_list(2) + prop_list(3)) 

  " remove some text
  :1s/[a-z]\{3\}//g
  let expected = [{'id': 0, 'col': 10, 'end': 1, 'type': 'number', 'length': 3, 'start': 1}]
  call assert_equal(expected, prop_list(1))
  bwipe!
endfunc
