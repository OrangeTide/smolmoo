# LambdaMOO Language Syntax Reference

Complete syntax reference for the LambdaMOO programming language, extracted from the LambdaMOO Programmer's Manual at https://lambda.moo.mud.org/pub/MOO/ProgrammersManual.html. This document serves as a reference for MooScript language design decisions.

## Table of Contents

1. [Comments](#comments)
2. [Types and Values](#types-and-values)
3. [Variables](#variables)
4. [Expression Syntax](#expression-syntax)
5. [Statement Syntax](#statement-syntax)
6. [Object-Oriented Features](#object-oriented-features)
7. [Control Flow](#control-flow)
8. [Error Handling](#error-handling)
9. [Built-in Functions (Selected)](#built-in-functions-selected)

---

## Comments

MOO supports C-style block comments that are stripped during compilation and not saved to the database:

```moo
/* This is a comment */

/* Multi-line
   comment
   example */
```

String literals can serve as persistent comments or documentation.

---

## Types and Values

MOO has six fundamental value types:

### Integers

Range: -2,147,483,648 to 2,147,483,647

Syntax: Optional minus sign followed by digits (no separators allowed)

```moo
0
-42
2147483647
```

### Floating-Point Numbers

IEEE 754 double precision floating-point format

Syntax: Optional sign, digits, decimal point, and/or scientific notation (E or e for exponent)

```moo
325.0
3.25e2
.0325e+4
-1.5
1e-10
```

### Strings

Arbitrary-length ASCII character sequences enclosed in double quotes

Special characters use backslash escaping:
- `\"` — literal double quote
- `\\` — literal backslash
- `\n` — newline
- `\t` — tab
- Other sequences: `\0` to `\377` for octal character codes

```moo
"hello"
"hello world"
"say \"hello\""
"path\\to\\file"
"tab\there"
```

### Objects

Object references use hash mark followed by integer

```moo
#495
#0          /* System object */
#-1         /* $nothing — special value */
#-2         /* $ambiguous_match — special value */
#-3         /* $failed_match — special value */
```

### Error Values

Error values use E_ prefix, representing runtime errors

```moo
E_TYPE      /* Type mismatch */
E_DIV       /* Division by zero */
E_PERM      /* Permission denied */
E_PROPNF    /* Property not found */
E_VERBNF    /* Verb not found */
E_VARNF     /* Variable not found */
E_RANGE     /* Index out of range */
E_ARGS      /* Wrong number of arguments */
E_MAXREC    /* Maximum recursion depth */
E_QUOTA     /* Storage quota exceeded */
E_FLOAT     /* Floating-point exception */
```

### Lists

Comma-separated values enclosed in braces; can contain mixed types and nested lists

```moo
{}                      /* empty list */
{1, 2, 3}               /* integer list */
{1, "foo", #17}         /* mixed types */
{1, {2, 3}, 4}          /* nested lists */
{"key", "value"}        /* string pairs */
```

---

## Variables

### Variable Names

Variable names consist of letters, digits, and underscores; cannot start with a digit. Case-insensitive.

```moo
x
my_variable
_private
counter42
```

### Variable Scope

Variables are local to verbs and scoped for the duration of verb execution. They do not persist across task invocations.

### Built-in Variables

Always available within verb execution context:

**Type codes** (used with typeof()):
- `INT` — integer type
- `FLOAT` — floating-point type
- `STR` — string type
- `LIST` — list type
- `OBJ` — object type
- `ERR` — error value type
- `NUM` — numeric type (INT or FLOAT)

**Command context**:
- `player` — the player who typed the command starting the task
- `this` — the object containing the currently executing verb
- `caller` — the object that called the current verb
- `verb` — the name of the currently executing verb
- `args` — list of arguments passed to the verb
- `argstr` — the original command string

**Object parsing** (from command input):
- `dobj` — direct object (as parsed by system)
- `dobjstr` — direct object string (user input)
- `prepstr` — preposition between dobj and iobj
- `iobj` — indirect object (as parsed by system)
- `iobjstr` — indirect object string (user input)

### Assignment Expression

Basic syntax: `variable = expression`

Returns the assigned value. Fails with E_PERM if variable is read-only.

```moo
x = 17              /* x is now 17; expression returns 17 */
name = "Alice"
count = count + 1
a = b = c = 0       /* chained assignment: evaluates right-to-left */
```

---

## Expression Syntax

### Arithmetic Operators

Binary operators:
- `+` — addition (or string/list concatenation)
- `-` — subtraction
- `*` — multiplication
- `/` — division (integer truncation for integers)
- `%` — modulo (remainder)
- `^` — exponentiation

Unary operator:
- `-expression` — negation

Type rules:
- Integer and floating-point types cannot be mixed in a single operation
- Use `tofloat()` to explicitly convert integers to floats before mixing types

```moo
2 + 3               /* 5 */
10 - 4              /* 6 */
3 * 4               /* 12 */
7 / 2               /* 3 (integer division) */
7.0 / 2.0           /* 3.5 (float division) */
7 % 2               /* 1 */
2 ^ 10              /* 1024 */
-5                  /* -5 */
"hello" + " " + "world"  /* "hello world" */
{1, 2} + {3}        /* {1, 2, 3} */
```

### Comparison Operators

Equality operators:
- `==` — equal (case-insensitive for strings)
- `!=` — not equal

Ordering operators (raise E_TYPE for mixed types or lists):
- `<` — less than
- `<=` — less than or equal
- `>` — greater than
- `>=` — greater than or equal

Special rules:
- Strings compared case-insensitively by default
- Use `strcmp()` for case-sensitive string comparison
- All objects and error values are non-equal to each other except within identity checks

```moo
5 == 5              /* 1 (true) */
5 != 3              /* 1 (true) */
"HELLO" == "hello"  /* 1 (case-insensitive) */
5 < 10              /* 1 (true) */
5 <= 5              /* 1 (true) */
strcmp("A", "a")    /* negative (case-sensitive) */
```

### Logical Operators

Negation:
- `! expression` — returns 1 if expression is false, 0 if true

AND operator (short-circuit evaluation):
- `expression-1 && expression-2` — returns expression-2 if expression-1 is true; otherwise 0

OR operator (short-circuit evaluation):
- `expression-1 || expression-2` — returns expression-1 if it is true; otherwise expression-2

```moo
!0                  /* 1 (true) */
!1                  /* 0 (false) */
!"string"           /* 0 (strings are truthy) */
1 && 2              /* 2 */
0 && expensive()    /* 0 (expensive() not evaluated) */
0 || 5              /* 5 */
1 || expensive()    /* 1 (expensive() not evaluated) */
```

### Conditional Expression (Ternary)

Syntax: `expression-1 ? expression-2 | expression-3`

Evaluates expression-1; if true, returns expression-2; otherwise returns expression-3.

```moo
x > 0 ? "positive" | "non-positive"
level >= 10 ? "advanced" | "beginner"
valid(obj) ? $object_name(obj) | "invalid"
```

### Truth Values

**True values**:
- Non-zero integers (1, -1, 100, etc.)
- Non-zero floats (1.5, -0.1, etc.)
- Non-empty strings ("a", "false", etc.)
- Non-empty lists ({1}, {"a", "b"}, etc.)

**False values**:
- Zero: `0` (integer) or `0.0` (float)
- Empty string: `""`
- Empty list: `{}`
- All objects (regardless of validity)
- All error values

```moo
if (1)          /* true */
if (0)          /* false */
if ("text")     /* true (even if "false" string) */
if ({})         /* false */
if (#123)       /* false (objects are always falsy) */
if (E_PERM)     /* false (errors are always falsy) */
```

### Indexing (Lists and Strings)

MOO uses 1-based indexing for both lists and strings.

Single element access:
```moo
sequence[index]     /* Get element at index */
sequence[1]         /* First element */
sequence[3]         /* Third element */
```

**The `$` operator in indexing**: Inside square brackets, `$` evaluates to the length of the sequence being indexed. It is only valid within `[]` on a list or string expression.

```moo
mylist = {10, 20, 30, 40, 50};
mylist[$]           /* 50 — same as mylist[length(mylist)] */
mylist[$ - 1]       /* 40 — second-to-last element */
mylist[$ - 2]       /* 30 — third-to-last element */

str = "hello";
str[$]              /* "o" — last character */
str[$ - 1]          /* "l" */
```

`$` can be used in range expressions as well:

```moo
mylist[2..$]        /* {20, 30, 40, 50} — second through last */
mylist[$-2..$]      /* {30, 40, 50} — last three elements */
str[2..$]           /* "ello" */
```

Range extraction (inclusive on both ends):
```moo
sequence[start..end]    /* Extract sublist/substring from start to end */
sequence[1..3]          /* First three elements */
{10, 20, 30, 40}[2..3] /* {20, 30} */
"hello"[2..4]           /* "ell" */
```

Element modification:
```moo
variable[index] = value        /* Replace element at index */
object.property[index] = value /* Replace element in property */
```

Range modification:
```moo
variable[start..end] = new_sequence   /* Replace range */
variable[2..2] = {}                   /* Delete element at index 2 */
variable[2..3] = {99}                 /* Replace two elements with one */
```

Examples combining `$` with modification:
```moo
mylist = {10, 20, 30};
mylist[$] = 99;         /* {10, 20, 99} — replace last element */
mylist[$..$] = {};      /* {10, 20} — delete last element */
```

### List Construction and Splicing

Basic list construction:
```moo
{expr-1, expr-2, ..., expr-N}
```

Splicing operator `@` (expands list elements into parent list):
```moo
mylist = {2, 3}
result = {1, @mylist, 4, 5}     /* {1, 2, 3, 4, 5} */
{@list1, @list2}                /* Concatenate two lists */
```

### List Membership

Syntax: `value in list`

Returns 1-based index of first occurrence for case-insensitive match, or 0 if not found. Case-insensitive for strings.

```moo
2 in {1, 2, 3}          /* 2 */
"key" in {"Key", "value"}  /* 1 (case-insensitive) */
#10 in {#10, #20}       /* 1 */
99 in {1, 2, 3}         /* 0 */
```

Case-sensitive membership:
```moo
is_member("Key", {"key", "value"})     /* 0 (case-sensitive) */
is_member("key", {"key", "value"})     /* 1 (case-sensitive) */
```

### Scattering Assignment

Syntax: `{target, ...} = expression`

Distributes list elements from the right-hand side to multiple targets on the left-hand side.

Target types:
- `variable` — required element (raises E_ARGS if missing)
- `?variable` — optional element (ignored if missing)
- `?variable = default_expr` — optional with default value
- `@variable` — collects remaining elements into a list

Raises E_ARGS if:
- Insufficient elements for required targets
- Excess elements without a rest target (@)

```moo
{a, b, c} = {1, 2, 3}           /* a=1, b=2, c=3 */
{a, ?b, c} = {1, 3}             /* a=1, b=0, c=3 */
{a, ?b, c} = {1, 2, 3, 4}       /* raises E_ARGS (excess) */
{a, ?b, c, @rest} = {1, 2, 3, 4, 5}  /* a=1, b=2, c=3, rest={4,5} */
{a, @rest, ?z} = {1, 2, 3}      /* a=1, rest={2}, z=3 */
{a, ?b = 99, c} = {1, 3}        /* a=1, b=99, c=3 */
```

### Property Access

Basic property syntax:
```moo
object.property         /* Read property */
object.property = value /* Write property (checks permissions) */
```

Computed property name:
```moo
object.(name_expression)        /* Property name from expression */
object.(name_expression) = value
```

System object shorthand:
```moo
$name           /* Equivalent to #0.name */
$property       /* Equivalent to #0.property */
```

Property permission and behavior:
- If property is read-only, assignment fails with E_PERM
- Computed properties must yield valid property names
- Missing properties raise E_PROPNF

```moo
room.description
player.name = "New Name"
obj.("dynamic_prop")
$utils               /* #0.utils */
#123.location = #456
```

### Verb Calls

Basic verb call syntax:
```moo
object:verb_name(arg1, arg2, ...)
```

Computed verb name:
```moo
object:(verb_expression)(arg1, arg2, ...)
```

System object shorthand:
```moo
$verb_name(arg1, arg2, ...)    /* Equivalent to #0:verb_name(...) */
```

Verb call with splicing operator `@`:
```moo
object:verb(@arglist)
myobj:process(@{1, "key"}, @extra_args)
```

Verb call behavior:
- Raises E_VERBNF if verb not found
- Raises E_PERM if caller lacks execution rights
- Returns the verb's return value
- Inherits from parent objects if not found on target

```moo
this:initialize(5, "test")
$do_cmd("say", "hello")
player:add_inventory(obj)
room:(get_action_verb())(player)
#123:get_name()
{object1, object2}:announce("Watch this!")
```

### Built-in Function Calls

Syntax: `function_name(arg1, arg2, ..., argN)`

Splicing operator `@` supported in argument lists.

```moo
typeof(x)
length(mylist)
append(list, item)
create(#1)
valid(obj)
tostr(a, b, c)
strcmp(str1, str2)
tofloat(integer_val)
notify(player, "message")
```

### Error Catching Expression

Syntax: `` `expression ! error_codes => alternate_expression` ``

Catches specified errors; returns alternate expression value if caught. Backticks are literal syntax delimiters.

Error codes specification:
- List of specific error codes: `` `expr ! {E_PERM, E_PROPNF} => ...` ``
- ANY keyword for any error: `` `expr ! ANY => ...` ``

Optional alternate expression:
- If `=> alternate_expression` omitted, the error value itself is returned

```moo
`create(parent) ! E_PERM => #-1`
  /* If create() raises E_PERM, return #-1; otherwise return created object */

`value / 0 ! E_DIV => 0`
  /* If division by zero occurs, return 0; otherwise return result */

`player:force("quit") ! ANY => #-1`
  /* If any error, return #-1 */

`lookup_obj(name) ! {E_VARNF, E_PROPNF}`
  /* If either error, return the error value itself */
```

### Operator Precedence (Highest to Lowest)

1. `!` (logical NOT), unary `-` (negation)
2. `^` (exponentiation)
3. `*`, `/`, `%` (multiplication, division, modulo)
4. `+`, `-` (addition/concatenation, subtraction)
5. `==`, `!=`, `<`, `<=`, `>`, `>=`, `in` (comparison)
6. `&&` (logical AND, short-circuit)
7. `||` (logical OR, short-circuit)
8. `... ? ... | ...` (conditional/ternary)
9. `=` (assignment)

Parentheses override precedence.

```moo
2 + 3 * 4           /* 14 (multiplication first) */
(2 + 3) * 4         /* 20 (parentheses override) */
!0 && 1             /* 1 (! has higher precedence than &&) */
x = y = 5 ? 1 | 2   /* x = (y = (5 ? 1 | 2)) = (y = 1) = 1 */
```

---

## Statement Syntax

### Null Statement

```moo
;
```

Does nothing. Often used as placeholder.

### Expression Statement

```moo
expression;
```

Evaluates expression for side effects; return value is discarded.

```moo
x = 5;
notify(player, "hello");
obj:method();
```

### Conditional Statements

**Simple if**:
```moo
if (expression)
  statements
endif
```

**if-else**:
```moo
if (expression)
  statements-1
else
  statements-2
endif
```

**if-elseif-else chain**:
```moo
if (expr-1)
  stmts-1
elseif (expr-2)
  stmts-2
elseif (expr-3)
  stmts-3
else
  stmts-4
endif
```

Each condition is evaluated only if previous conditions were false. The else clause is optional.

```moo
if (x > 0)
  notify(player, "positive");
endif

if (level == 1)
  title = "novice";
elseif (level == 2)
  title = "apprentice";
elseif (level == 3)
  title = "journeyman";
else
  title = "expert";
endif
```

### Loop Statements

**for-in list iteration**:
```moo
for variable in (list_expression)
  statements
endfor
```

Iterates over each element in list; variable takes each value in sequence.

**for-in range iteration**:
```moo
for variable in [expr-1..expr-2]
  statements
endfor
```

Iterates from expr-1 to expr-2 inclusive; variable takes each integer value.

**while loop**:
```moo
while (expression)
  statements
endwhile
```

Repeatedly evaluates condition; executes statements while true.

**Named loops** (optional for targeted break/continue):
```moo
for name (variable) in (list_expression)
  statements
endfor

while name (expression)
  statements
endwhile
```

Loop names allow break/continue to target specific loops in nested structures.

```moo
for item in (mylist)
  if (item == target)
    found = item;
  endif
endfor

for i in [1..10]
  if (i % 2 == 0)
    continue;
  endif
  sum = sum + i;
endfor

while (counter < 100)
  counter = counter + 1;
endwhile

for outer (x) in [1..10]
  for inner (y) in [1..10]
    if (x * y > 50)
      break outer;
    endif
  endfor
endfor
```

### Loop Control Statements

**break statement** (exit loop):
```moo
break;              /* Break from innermost loop */
break loop_name;    /* Break from named loop */
```

**continue statement** (skip to next iteration):
```moo
continue;               /* Continue innermost loop */
continue loop_name;     /* Continue named loop */
```

```moo
for i in [1..10]
  if (i == 5)
    break;          /* Exit loop entirely */
  endif
  sum = sum + i;
endfor

while (1)
  x = get_input();
  if (x < 0)
    continue;       /* Skip rest of loop body */
  endif
  process(x);
endwhile
```

### Return Statement

```moo
return;             /* Return from verb, return value 0 */
return expression;  /* Return from verb with expression value */
```

Immediately exits the verb with the specified return value (or 0 if not specified).

```moo
if (invalid_input)
  return E_ARGS;
endif

for item in (list)
  if (item == target)
    return item;
  endif
endfor

return;
```

---

## Object-Oriented Features

### Object Basics

Every object in MOO has:
- **Player flag** — indicates if object represents a player (wizard-settable)
- **Parent object** — inheritance hierarchy; objects inherit verbs and properties from parents
- **Children list** — list of objects with this as parent (property: `children`)

### Built-in Properties

Standard properties available on all objects (type-constrained by system):

| Property | Type | Meaning |
|----------|------|---------|
| `name` | string | Object's name |
| `owner` | object | Object owner |
| `location` | object | Object containing this object |
| `contents` | list | Objects contained by this object |
| `programmer` | bit | Has programmer permissions |
| `wizard` | bit | Has wizard permissions |
| `r` | bit | Property is publicly readable |
| `w` | bit | Property is publicly writable |
| `f` | bit | Fertile; allows child objects |

### Custom Properties

Defined per-object with permission bits:

- `r` — public read permission
- `w` — public write permission
- `c` — change ownership in descendants (inheritance)

```moo
obj.level = 5;          /* Define custom property */
room.exit_north = #100; /* Custom property assignment */
```

### Verb Definitions and Metadata

Verbs are named MOO programs stored on objects. Each verb has:

**Owner**: Determines execution permissions and ownership of verb code

**Permission bits**:
- `r` — non-owners can read program text
- `w` — non-owners can modify program
- `x` — callable from code (if omitted, only command-line callable)
- `d` — debug bit (controls error propagation)

**Argument specifiers** (for command-line parsing):
- Direct object type: none, "this", "any"
- Preposition: (see below)
- Indirect object type: none, "this", "any"

**Valid prepositions**:
- "with", "using"
- "at", "to"
- "in front of"
- "in", "inside", "into"
- "on top of", "on", "onto", "upon"
- "out of", "from inside", "from"
- "over"
- "through"
- "under", "underneath", "beneath"
- "behind"
- "beside"
- "for", "about"
- "is"
- "as"
- "off", "off of"

### Verb Naming

Verb names stored as space-separated strings with wildcard support:

- Exact match: `"examine"` — matches only `examine`
- Prefix with wildcard: `"foo*"` — matches `foo`, `foob`, `foobar`, etc.
- Suffix with wildcard: `"*bar"` — matches `bar`, `foobar`, `localbar`
- Mid-string wildcard: `"foo*bar"` — matches `foo`, `foob`, `foo_bar`, `foobar`
- Single `*` — matches any command

```moo
this:add_verb(player, {"look", "l", "examine", "x"}, "this none none", #0, {}, "");
  /* Verb matches: look, l, examine, x */

this:add_verb(owner, {"*help"}, "none none none", #0, {}, "");
  /* Verb matches any word ending in "help" */
```

---

## Control Flow

### Forking (Delayed Execution)

**Basic fork**:
```moo
fork (seconds_expression)
  statements
endfork
```

Creates a background task that executes statements after the specified delay. All variables retain their values from the fork point (copy-on-write semantics for lists and objects).

**Named fork** (for task management):
```moo
fork task_name (seconds_expression)
  statements
endfork
```

Variable `task_name` receives the task ID, which can be used with `kill_task()` to abort the forked task.

Fork behavior:
- Executes asynchronously after delay
- Inherits variable values from fork point
- Can reference global properties
- Returns immediately with task ID

```moo
fork (5)
  notify(player, "This message appears in 5 seconds");
endfork

fork mytask (10)
  for i in [1..100]
    if (i % 10 == 0)
      notify(player, "Progress: " + i);
    endif
  endfor
endfork

kill_task(mytask);  /* Abort the forked task */
```

---

## Error Handling

### try-except Statement

**Basic try-except**:
```moo
try
  statements-0
except variable (error_codes)
  statements-1
endtry
```

**Multiple exception handlers**:
```moo
try
  statements-0
except var-1 (error_codes-1)
  statements-1
except var-2 (error_codes-2)
  statements-2
endtry
```

Exception variable receives a 4-element list:
```
{error_code, error_message, error_value, error_traceback}
```

Error codes specification:
- List of error codes: `(E_PERM, E_PROPNF)`
- `ANY` keyword: catches any error

Variable is optional; omit to catch without capturing details.

Exception handling behavior:
- First matching handler executes
- If no handler matches, error propagates to caller
- Nested try-except blocks supported

```moo
try
  obj = create(parent);
except e (E_PERM)
  notify(player, "Permission denied");
  obj = #-1;
endtry

try
  result = a / b;
except (E_DIV)
  notify(player, "Division by zero");
  result = 0;
endtry

try
  value = obj.property;
except err (ANY)
  "Error: " + err[2];
endtry
```

### try-finally Statement

**try-finally**:
```moo
try
  statements-1
finally
  statements-2
endtry
```

Ensures statements-2 executes regardless of control transfer:
- Normal completion
- Error (with or without catch)
- Return statement
- Break/continue in loops
- Fork execution

The finally block executes in all cases before control transfers away.

```moo
try
  lock(obj);
  process(obj);
finally
  unlock(obj);
endtry

try
  file = open("/tmp/data");
  data = read(file);
finally
  close(file);
endtry
```

### raise Statement

```moo
raise(error_code);
raise(error_code, message);
raise(error_code, message, value);
```

Raises an error that propagates up the call stack. Can be caught by try-except or error catching expressions.

```moo
if (!valid(obj))
  raise(E_INVARG, "Invalid object reference");
endif

raise(E_PERM, "You don't have access", {player, this});
```

### Error Catching Expression (inline)

Already described in Expression Syntax section. Provides inline error handling without try-except blocks.

```moo
result = `dangerous_call() ! E_PERM => default_value`
```

---

## Built-in Functions (Selected)

This is a partial reference of frequently-used built-in functions. The LambdaMOO manual contains many more.

### Value Manipulation and Inspection

**typeof(value)**
- Returns type code: `INT`, `FLOAT`, `STR`, `LIST`, `OBJ`, `ERR`, or `NUM`

```moo
typeof(5)           /* INT */
typeof(3.14)        /* FLOAT */
typeof("hello")     /* STR */
typeof({1, 2})      /* LIST */
typeof(#100)        /* OBJ */
typeof(E_PERM)      /* ERR */
```

**tostr(value, ...)**
- Converts arguments to strings and concatenates them
- Returns single string

```moo
tostr("Value: ", x, " of ", total)
tostr(mylist)
tostr(#100)
```

**toliteral(value)**
- Returns MOO literal syntax representation of value
- Useful for debugging and persistence

```moo
toliteral(5)        /* "5" */
toliteral("hello")  /* "\"hello\"" */
toliteral({1, 2})   /* "{1, 2}" */
```

**toint(value), tonum(value)**
- Converts value to integer
- Truncates floats; parses strings

```moo
toint(3.7)          /* 3 */
toint("42")         /* 42 */
toint("3.5")        /* 3 */
```

**tofloat(value)**
- Converts integer or string to floating-point
- Required before mixing integer and float in operations

```moo
tofloat(5)          /* 5.0 */
tofloat("3.14")     /* 3.14 */
```

**equal(str1, str2)**
- Case-sensitive string equality
- Returns 1 if equal, 0 if not

```moo
equal("Hello", "hello")   /* 0 */
equal("Hello", "Hello")   /* 1 */
```

**strcmp(str1, str2)**
- Case-sensitive string comparison
- Returns < 0 if str1 < str2, 0 if equal, > 0 if str1 > str2

```moo
strcmp("apple", "banana")    /* negative */
strcmp("banana", "apple")    /* positive */
strcmp("hello", "hello")     /* 0 */
```

### Object Operations

**create(parent_object)**
- Creates new object as child of parent
- Returns object reference
- Raises E_PERM if no permission; E_QUOTA if quota exceeded

```moo
room = create(#10);    /* Create room based on parent #10 */
obj = create(player);  /* Create object based on player */
```

**recycle(object)**
- Destroys an object permanently
- Raises E_PERM if no permission

```moo
recycle(old_object);
```

**chparent(object, new_parent)**
- Changes object's parent (for property/verb inheritance)
- Raises E_PERM if no permission

```moo
chparent(my_object, new_parent_obj);
```

**valid(object)**
- Tests if object exists and is valid
- Returns 1 if valid, 0 if not

```moo
if (valid(obj))
  notify(obj, "message");
endif
```

**move(object, destination)**
- Moves object to destination
- Updates location and contents properties
- Raises E_PERM if no permission

```moo
move(item, #10);       /* Move item to room #10 */
move(player, room);    /* Move player to room */
```

**pass(arg, ...)**
- Calls parent object's version of current verb
- Allows verb overriding with parent call
- Cannot pass arguments of different types than parent expects

```moo
result = pass(argument);
pass(@args);
```

### List and String Operations

**length(sequence)**
- Returns length of list or string
- Returns 0 for empty sequence

```moo
length({1, 2, 3})      /* 3 */
length("hello")        /* 5 */
length({})             /* 0 */
```

**is_member(value, list)**
- Case-sensitive membership test
- Returns 1-based index if found, 0 if not found

```moo
is_member(5, {1, 2, 5})         /* 3 */
is_member("hello", {"Hello"})   /* 0 (case-sensitive) */
```

**listappend(list, value [, index])**
- Returns new list with value inserted after index (default: end)

```moo
listappend({1, 2, 3}, 99)       /* {1, 2, 3, 99} */
listappend({1, 2, 3}, 99, 1)    /* {1, 99, 2, 3} */
```

**listinsert(list, value [, index])**
- Returns new list with value inserted before index (default: beginning)

```moo
listinsert({1, 2, 3}, 99)       /* {99, 1, 2, 3} */
listinsert({1, 2, 3}, 99, 2)    /* {1, 99, 2, 3} */
```

**listdelete(list, index)**
- Returns new list with element at index removed
- Raises E_RANGE if index out of bounds

```moo
listdelete({1, 2, 3}, 2)        /* {1, 3} */
```

**listset(list, value, index)**
- Returns new list with element at index replaced
- Raises E_RANGE if index out of bounds

```moo
listset({1, 2, 3}, 99, 2)       /* {1, 99, 3} */
```

**setadd(list, value)**
- Returns new list with value added if not already present (set semantics)

```moo
setadd({1, 2, 3}, 4)            /* {1, 2, 3, 4} */
setadd({1, 2, 3}, 2)            /* {1, 2, 3} — already present */
```

**setremove(list, value)**
- Returns new list with first occurrence of value removed

```moo
setremove({1, 2, 3}, 2)         /* {1, 3} */
setremove({1, 2, 3}, 99)        /* {1, 2, 3} — not present */
```

### String Operations

**index(string, substring [, case-matters])**
- Returns 1-based index of first occurrence of substring, or 0 if not found
- Case-insensitive by default; pass 1 for case-sensitive

```moo
index("foobar", "oba")          /* 3 */
index("foobar", "OBA")          /* 3 (case-insensitive) */
index("foobar", "OBA", 1)       /* 0 (case-sensitive) */
```

**rindex(string, substring [, case-matters])**
- Like index(), but finds last occurrence

```moo
rindex("foobarfoo", "foo")      /* 7 */
```

**strsub(subject, what, with [, case-matters])**
- Replaces all occurrences of what with with in subject
- Case-insensitive by default

```moo
strsub("foobar", "OB", "**")           /* "fo**ar" */
strsub("foobar", "OB", "**", 1)        /* "foobar" (case-sensitive, no match) */
```

**substr(string, start, end)**
- Extracts substring (equivalent to string[start..end])

```moo
substr("hello world", 7, 11)    /* "world" */
```

**match(string, pattern [, case-matters])**
- Matches regular expression pattern against string
- Returns match info list, or 0 if no match
- MOO uses its own regex syntax (similar to POSIX but with `%` instead of `\`)

```moo
match("foobar", "o*b")          /* {4, 4, {{0, -1}}, "foobar"} */
```

**crypt(string [, salt])**
- One-way hash for password storage

```moo
hash = crypt("secret");
if (crypt("guess", hash) == hash) ... endif

### Task and Execution Control

**task_id()**
- Returns integer ID of current task

```moo
tid = task_id();
```

**kill_task(task_id)**
- Aborts forked task with given ID
- Stops execution; resources freed

```moo
kill_task(mytask);
```

**queued_tasks()**
- Returns list of all queued (forked/suspended) tasks
- Each entry: `{task_id, start_time, clock_id, ticks, programmer, verb_location, verb_name, line_number, this}`

```moo
for task in (queued_tasks())
  notify(player, tostr("Task ", task[1], ": ", task[6], ":", task[7]));
endfor
```

**suspend(seconds)**
- Suspends current task for specified seconds (0 for minimal yield)
- Task resumes after delay; other tasks may run in the meantime
- Returns no value

```moo
suspend(0);         /* Yield to other tasks, resume immediately */
suspend(5);         /* Pause for 5 seconds */
```

**callers()**
- Returns list of call stack frames
- Each frame is: `{this, verb, programmer, verb_location, player}`
- Useful for permission checks and debugging

```moo
stack = callers();
for frame in (stack)
  notify(player, tostr(frame[4], ":", frame[2]));
endfor
```

**seconds_left() / ticks_left()**
- Returns remaining execution time/ticks before task is forcibly aborted
- Used to self-limit long-running tasks

```moo
while (ticks_left() > 1000 && work_remaining)
  do_some_work();
endwhile
```

**read()**
- Reads a line of input from the current player's connection
- Suspends the task until input arrives
- Returns the input string

```moo
notify(player, "What is your name?");
name = read();
notify(player, "Hello, " + name);
```

### Communication

**notify(player, message)**
- Sends message to player object
- Appends newline automatically
- Message concatenated as string

```moo
notify(player, "You enter the room.");
notify(caller, tostr("Score: ", score));
```

**connected_players()**
- Returns list of all currently-connected player objects

```moo
for p in (connected_players())
  notify(p, "Server shutting down in 5 minutes.");
endfor
```

**boot_player(player)**
- Disconnects player from the server
- Requires wizard permissions

```moo
boot_player(troll);
```

### Reflection and Introspection

**properties(object)**
- Returns list of property names defined directly on object (not inherited)

```moo
props = properties(#123);       /* {"name", "level", "hp"} */
```

**verbs(object)**
- Returns list of verb names defined directly on object (not inherited)

```moo
vlist = verbs(#123);            /* {"look", "take", "drop"} */
```

**parent(object)**
- Returns parent object

```moo
parent(#123)                    /* #1 (generic thing) */
```

**children(object)**
- Returns list of direct child objects

```moo
children(#1)                    /* {#10, #20, #123, ...} */
```

### Server Control (Wizard-Restricted)

**set_player_flag(object, flag)**
- Marks object as player (1) or non-player (0)
- Requires wizard permissions

```moo
set_player_flag(new_user, 1);   /* Make object a player */
```

**set_task_perms(programmer)**
- Changes execution permissions for current task
- Programmer is an object reference

```moo
set_task_perms(#0);
```

**server_log(message)**
- Writes message to the server log file

```moo
server_log("Player " + tostr(player) + " triggered event");
```

---

## Summary of Key Distinctions

### Block Delimiters

MOO uses explicit block terminators (not braces):
- `if ... endif`
- `for ... endfor`
- `while ... endwhile`
- `fork ... endfork`
- `try ... endtry` (with except/finally)

### Type System

- **Weak typing**: Variables hold any type; types checked at runtime
- **No implicit conversions**: Integer and float operations must explicitly convert
- **Error values**: Distinct type for runtime errors
- **Objects always falsy**: Objects evaluate to false in boolean context

### Verb/Property Distinction

- **Properties**: Data stored on objects (`.` syntax for access)
- **Verbs**: Callable code on objects (`:` syntax for calls)
- **Inheritance**: Both verbs and properties inherited from parent objects
- **System object shorthand**: `$property` and `$verb()` reference `#0`

### String and List Operations

- **1-based indexing**: Lists and strings indexed from 1, not 0
- **`$` in index expressions**: Inside `[]`, `$` evaluates to the length of the sequence being indexed (`mylist[$]` = last element)
- **Concatenation with `+`**: Works for both strings and lists
- **Splicing operator `@`**: Expands lists into parent context
- **Set operations on lists**: `setadd()` and `setremove()` treat lists as unordered sets
- **Case-insensitive by default**: Strings use case-insensitive comparison (use `strcmp()` for case-sensitive)
- **Immutable values**: List and string operations return new values; originals are unchanged

### Error Handling Approaches

Two complementary approaches:
1. **try-except blocks**: Wrap large blocks; catch multiple errors per handler
2. **Error catching expressions**: Inline error handling with backtick syntax

---

## References

- Original source: https://lambda.moo.mud.org/pub/MOO/ProgrammersManual.html
- Language: LambdaMOO
- Context: Reference for MooScript language design decisions
