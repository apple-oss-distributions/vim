" Test Vim9 type aliases

source check.vim
import './vim9.vim' as v9

" Test for :type command to create type aliases
def Test_typealias()
  # Use type alias at script level
  var lines =<< trim END
    vim9script
    type ListOfStrings = list<string>
    def Foo(a: ListOfStrings): ListOfStrings
      return a
    enddef
    var b: ListOfStrings = ['a', 'b']
    assert_equal(['a', 'b'], b)
    assert_equal(['e', 'f'], Foo(['e', 'f']))
    assert_equal('typealias<list<string>>', typename(ListOfStrings))
    assert_equal(v:t_typealias, type(ListOfStrings))
    assert_equal('ListOfStrings', string(ListOfStrings))
    assert_equal(false, null == ListOfStrings)
  END
  v9.CheckSourceSuccess(lines)

  # Use type alias at def function level
  lines =<< trim END
    vim9script
    type ListOfStrings = list<string>
    def Foo(a: ListOfStrings): ListOfStrings
      return a
    enddef
    def Bar()
      var c: ListOfStrings = ['c', 'd']
      assert_equal(['c', 'd'], c)
      assert_equal(['e', 'f'], Foo(['e', 'f']))
      assert_equal('typealias<list<string>>', typename(ListOfStrings))
      assert_equal(v:t_typealias, type(ListOfStrings))
      assert_equal('ListOfStrings', string(ListOfStrings))
      assert_equal(false, null == ListOfStrings)
    enddef
    Bar()
  END
  v9.CheckSourceSuccess(lines)

  # Use :type outside a Vim9 script
  lines =<< trim END
    type Index = number
  END
  v9.CheckSourceFailure(lines, 'E1393: Type can only be defined in Vim9 script', 1)

  # Use :type without any arguments
  lines =<< trim END
    vim9script
    type
  END
  v9.CheckSourceFailure(lines, 'E1397: Missing type alias name', 2)

  # Use :type with a name but no type
  lines =<< trim END
    vim9script
    type MyType
  END
  v9.CheckSourceFailure(lines, "E398: Missing '=': ", 2)

  # Use :type with a name but no type following "="
  lines =<< trim END
    vim9script
    type MyType =
  END
  v9.CheckSourceFailure(lines, 'E1398: Missing type alias type', 2)

  # No space before or after "="
  lines =<< trim END
    vim9script
    type MyType=number
  END
  v9.CheckSourceFailure(lines, 'E1315: White space required after name: MyType=number', 2)

  # No space after "="
  lines =<< trim END
    vim9script
    type MyType =number
  END
  v9.CheckSourceFailure(lines, "E1069: White space required after '=': =number", 2)

  # type alias without "="
  lines =<< trim END
    vim9script
    type Index number
  END
  v9.CheckSourceFailure(lines, "E398: Missing '=': number", 2)

  # type alias for a non-existing type
  lines =<< trim END
    vim9script
    type Index = integer
  END
  v9.CheckSourceFailure(lines, 'E1010: Type not recognized: integer', 2)

  # type alias starting with lower-case letter
  lines =<< trim END
    vim9script
    type index = number
  END
  v9.CheckSourceFailure(lines, 'E1394: Type name must start with an uppercase letter: index = number', 2)

  # No white space following the alias name
  lines =<< trim END
    vim9script
    type Index:number
  END
  v9.CheckSourceFailure(lines, 'E1315: White space required after name: Index:number', 2)

  # something following the type alias
  lines =<< trim END
    vim9script
    type ListOfNums = list<number> string
  END
  v9.CheckSourceFailure(lines, 'E488: Trailing characters:  string', 2)

  # type alias name collides with a variable name
  lines =<< trim END
    vim9script
    var ListOfNums: number = 10
    type ListOfNums = list<number>
  END
  v9.CheckSourceFailure(lines, 'E1041: Redefining script item: "ListOfNums"', 3)

  # duplicate type alias name
  lines =<< trim END
    vim9script
    type MyList = list<number>
    type MyList = list<string>
  END
  v9.CheckSourceFailure(lines, 'E1396: Type alias "MyList" already exists', 3)

  # def function argument name collision with a type alias
  lines =<< trim END
    vim9script
    type A = list<number>
    def Foo(A: number)
    enddef
  END
  v9.CheckSourceFailure(lines, 'E1168: Argument already declared in the script: A: number)', 3)

  # def function local variable name collision with a type alias
  lines =<< trim END
    vim9script
    type A = list<number>
    def Foo()
      var A: number = 10
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E1054: Variable already declared in the script: A', 1)

  # type alias a variable
  lines =<< trim END
    vim9script
    var A: list<number> = []
    type B = A
  END
  v9.CheckSourceFailure(lines, 'E1010: Type not recognized: A', 3)

  # type alias a class
  lines =<< trim END
    vim9script
    class C
    endclass
    type AC = C
    assert_equal('class<C>', typename(AC))
  END
  v9.CheckSourceSuccess(lines)

  # Sourcing a script twice (which will free script local variables)
  # Uses "lines" from the previous test
  new
  setline(1, lines)
  :source
  :source
  bw!

  # type alias a type alias
  lines =<< trim END
    vim9script
    type A = string
    type B = A
    var b: B = 'abc'
    assert_equal('abc', b)
    def Foo()
      var c: B = 'def'
      assert_equal('def', c)
    enddef
    Foo()
  END
  v9.CheckSourceSuccess(lines)

  # Assigning to a type alias (script level)
  lines =<< trim END
    vim9script
    type MyType = list<number>
    MyType = [1, 2, 3]
  END
  v9.CheckSourceFailure(lines, 'E1395: Type alias "MyType" cannot be modified', 3)

  # Assigning a type alias (def function level)
  lines =<< trim END
    vim9script
    type A = list<string>
    def Foo()
      var x = A
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E1403: Type alias "A" cannot be used as a value', 1)

  # Using type alias in an expression (script level)
  lines =<< trim END
    vim9script
    type MyType = list<number>
    assert_fails('var m = MyType', 'E1403: Type alias "MyType" cannot be used as a value')
    assert_fails('var i = MyType + 1', 'E1400: Using type alias "MyType" as a Number')
    assert_fails('var f = 1.0 + MyType', 'E1400: Using type alias "MyType" as a Number')
    assert_fails('MyType += 10', 'E1395: Type alias "MyType" cannot be modified')
    assert_fails('var x = $"-{MyType}-"', 'E1402: Using type alias "MyType" as a String')
    assert_fails('var x = MyType[1]', 'E909: Cannot index a special variable')
  END
  v9.CheckSourceSuccess(lines)

  # Using type alias in an expression (def function level)
  lines =<< trim END
    vim9script
    type MyType = list<number>
    def Foo()
      var x = MyType + 1
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E1051: Wrong argument type for +', 1)

  # Using type alias in an expression (def function level)
  lines =<< trim END
    vim9script
    type MyType = list<number>
    def Foo()
      MyType = list<string>
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E46: Cannot change read-only variable "MyType"', 1)

  # Using type alias in an expression (def function level)
  lines =<< trim END
    vim9script
    type MyType = list<number>
    def Foo()
      MyType += 10
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E46: Cannot change read-only variable "MyType"', 1)

  # Convert type alias to a string (def function level)
  lines =<< trim END
    vim9script
    type MyType = list<number>
    def Foo()
      var x = $"-{MyType}-"
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E1105: Cannot convert typealias to string', 1)

  # Using type alias as a float
  lines =<< trim END
    vim9script
    type B = number
    sort([1.1, B], 'f')
  END
  v9.CheckSourceFailure(lines, 'E1401: Using type alias "B" as a Float', 3)

  # Creating a typealias in a def function
  lines =<< trim END
    vim9script
    def Foo()
      var n: number = 10
      type A = list<string>
    enddef
    defcompile
  END
  v9.CheckSourceFailure(lines, 'E1399: Type can only be used in a script', 2)

  # json_encode should fail with a type alias
  lines =<< trim END
    vim9script
    type A = list<string>
    var x = json_encode(A)
  END
  v9.CheckSourceFailure(lines, 'E1161: Cannot json encode a typealias', 3)

  # Comparing type alias with a number (script level)
  lines =<< trim END
    vim9script
    type A = list<string>
    var n: number
    var x = A == n
  END
  v9.CheckSourceFailure(lines, 'E1072: Cannot compare typealias with number', 4)

  # Comparing type alias with a number (def function level)
  lines =<< trim END
    vim9script
    type A = list<string>
    def Foo()
      var n: number
      var x = A == n
    enddef
    Foo()
  END
  v9.CheckSourceFailure(lines, 'E1072: Cannot compare typealias with number', 2)

  # casting a number to a type alias (script level)
  lines =<< trim END
    vim9script
    type MyType = bool
    assert_equal(true, <MyType>1 == true)
  END
  v9.CheckSourceSuccess(lines)
enddef

" Test for exporting and importing type aliases
def Test_typealias_import()
  var lines =<< trim END
    vim9script
    export type MyType = list<number>
  END
  writefile(lines, 'Xtypeexport.vim', 'D')

  lines =<< trim END
    vim9script
    import './Xtypeexport.vim' as A

    var myList: A.MyType = [1, 2, 3]
    def Foo(l: A.MyType)
      assert_equal([1, 2, 3], l)
    enddef
    Foo(myList)
  END
  v9.CheckScriptSuccess(lines)

  # Use a non existing type alias
  lines =<< trim END
    vim9script
    import './Xtypeexport.vim' as A

    var myNum: A.SomeType = 10
  END
  v9.CheckScriptFailure(lines, 'E1010: Type not recognized: A.SomeType = 10', 4)

  # Use a type alias that is not exported
  lines =<< trim END
    vim9script
    type NewType = dict<string>
  END
  writefile(lines, 'Xtypeexport2.vim', 'D')
  lines =<< trim END
    vim9script
    import './Xtypeexport2.vim' as A

    var myDict: A.NewType = {}
  END
  v9.CheckScriptFailure(lines, 'E1049: Item not exported in script: NewType', 4)

  # Using the same name as an imported type alias
  lines =<< trim END
    vim9script
    export type MyType2 = list<number>
  END
  writefile(lines, 'Xtypeexport3.vim', 'D')
  lines =<< trim END
    vim9script
    import './Xtypeexport3.vim' as A

    type MyType2 = A.MyType2
    var myList1: A.MyType2 = [1, 2, 3]
    var myList2: MyType2 = [4, 5, 6]
    assert_equal([1, 2, 3], myList1)
    assert_equal([4, 5, 6], myList2)
  END
  v9.CheckScriptSuccess(lines)

  # Using an exported class to create a type alias
  lines =<< trim END
    vim9script
    export class MyClass
      this.val = 10
    endclass
  END
  writefile(lines, 'Xtypeexport4.vim', 'D')
  lines =<< trim END
    vim9script
    import './Xtypeexport4.vim' as T

    type MyType3 = T.MyClass
    var c: MyType3 = MyType3.new()
    assert_equal(10, c.val)
  END
  v9.CheckScriptSuccess(lines)
enddef

" Test for using typealias as a def function argument and return type
def Test_typealias_func_argument()
  var lines =<< trim END
    vim9script
    type A = list<number>
    def Foo(l: A): A
      assert_equal([1, 2], l)
      return l
    enddef
    var x: A = [1, 2]
    assert_equal([1, 2], Foo(x))
  END
  v9.CheckScriptSuccess(lines)

  # passing a type alias variable to a function expecting a specific type
  lines =<< trim END
    vim9script
    type A = list<number>
    def Foo(l: list<number>)
      assert_equal([1, 2], l)
    enddef
    var x: A = [1, 2]
    Foo(x)
  END
  v9.CheckScriptSuccess(lines)

  # passing a type alias variable to a function expecting any
  lines =<< trim END
    vim9script
    type A = list<number>
    def Foo(l: any)
      assert_equal([1, 2], l)
    enddef
    var x: A = [1, 2]
    Foo(x)
  END
  v9.CheckScriptSuccess(lines)
enddef

" Using a type alias with a builtin function
def Test_typealias_with_builtin_functions()
  var lines =<< trim END
    vim9script
    type A = list<func>
    assert_equal(0, empty(A))
  END
  v9.CheckScriptSuccess(lines)

  # Using a type alias with len()
  lines =<< trim END
    vim9script
    type A = list<func>
    var x = len(A)
  END
  v9.CheckScriptFailure(lines, 'E701: Invalid type for len()', 3)

  # Using a type alias with len()
  lines =<< trim END
    vim9script
    type A = list<func>
    def Foo()
      var x = len(A)
    enddef
    Foo()
  END
  v9.CheckScriptFailure(lines, 'E1013: Argument 1: type mismatch, expected list<any> but got typealias', 1)

  # Using a type alias with eval()
  lines =<< trim END
    vim9script
    type A = number
    def Foo()
      var x = eval("A")
    enddef
    Foo()
  END
  v9.CheckScriptFailure(lines, 'E1403: Type alias "A" cannot be used as a value', 1)
enddef

" Test for type alias refcount
def Test_typealias_refcount()
  var lines =<< trim END
    vim9script
    type A = list<func>
    assert_equal(1, test_refcount(A))
  END
  v9.CheckScriptSuccess(lines)

  lines =<< trim END
    vim9script
    type B = list<number>
    var x: B = []
    assert_equal(1, test_refcount(B))
  END
  v9.CheckScriptSuccess(lines)
enddef

" Test for using instanceof() with a type alias
def Test_typealias_instanceof()
  var lines =<< trim END
    vim9script
    class C
    endclass

    type Ctype = C
    var o = C.new()
    assert_equal(1, instanceof(o, Ctype))
    type Ntype = number
    assert_fails('instanceof(o, Ntype)', 'E693: List or Class required for argument 2')
    assert_equal(1, instanceof(o, [Ctype]))
  END
  v9.CheckScriptSuccess(lines)
enddef

" Test for type aliasing a class
def Test_typealias_class()
  var lines =<< trim END
    vim9script
    class C
      this.color = 'green'
    endclass
    type MyClass = C
    var o: MyClass = MyClass.new()
    assert_equal('green', o.color)
  END
  v9.CheckScriptSuccess(lines)
enddef

" vim: ts=8 sw=2 sts=2 expandtab tw=80 fdm=marker
