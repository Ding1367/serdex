# serdex
serdex is a basic data interchange format with serialization and deserialization programs.

# Syntax
serdex is very similar to JSON5, permitting trailing commas, comments, and allowing unquoted keys.

There are many differences, such as:
- no single quote strings
- more basic floating point processing
- integers as keys in objects and arrays
- schematic arrays (programs)
- comments are only single line

There are 8 types: bool, integer, float, string, null, object, array, and program.

### Objects
Objects use curly braces (`{}`). Here is an example of an object:
```json5
{
  name: "Alice",
  age: 25
}
```

### Comments
There are only single line comments and they start with `//`.

### Arrays
Arrays use square braces (`[]`). Here is an example of an array:
```json5
[
  42,
  "devirtualization",
  false
]
```

They can also use integers as keys, like so:
```json5
[
  42,
  3: "hello",
  10: false
]
```

### Booleans and null
A null value is simply `null`, while a boolean is either `true` or `false`.

### Numbers (and integers)
You are allowed to use as many signs (`+` acts as a no op). These are the only valid forms of numbers after all signs:
```json5
[
  42, // integer
  3.14159265, // double
  .41421, // double, no leading 0
]
```

### Strings
Strings are surrounded by double quotes (`""`) and can escape any character to simply put that character (including before a new line).

Characters can also produce control characters, such as:
- `\t` (tab)
- `\b` (backspace)
- `\n` (newline)

### Programs
Programs are very basic. They start with `@[` and end with `]`. Expressions have no precedence when parsed, they are simply evaluated left to right.

The only place where you can use expressions is in an if. If statements do not have an else or elseif case, and they are ended with endif. If and endif must still end with a comma if needed.

Variables in a program can be defined by setting a string key on a value.

```
@[
  has_death_location: "bool",
  if has_death_location == true,
    "position",
  endif
]
```

# API
### Types
All names live in the namespace `serdex`.

* Any value is represented by and should be accessed with `value`
* Objects are represented by `object`
* Arrays can be either an `out_of_order_array` or `array`.
    - Out of order arrays are arrays which use integer indices that make the array no longer contiguous.
        - Iterating over them does not have a guaranteed order.
* Programs are represented by `program`.
* Null is represented by a default initialized `value` or `nullptr`.

Programs cannot be iterated over. You must use the `evaluator`. To parse an `std::istream` containing `serdex` format, you need a `parser`.

`value` supports `[]`, `==`, `!=`. The available methods are:
* `.at()`
* `.emplace()`
* `.emplace_back()`, only available on arrays
* `.holds<T>()`
* `.get<T>()`
* `.size()`, returns 1 for non containers (program is not considered a container because it is dynamic)

## Evaluators
To initialize an evaluator, you simply create one then call `init()` with a pointer to your program. Repeatedly call `next()` and break early if `done() == false`. Provide feedback on what you parsed by assigning the `feedback` member, simply the value that's going in or out.