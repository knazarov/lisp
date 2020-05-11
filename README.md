# A simple lisp dialect written in plain C

This lisp was written for fun, and so completely lacks any practical
purpose.

Features:

- basic lisp operations
- lambdas
- closures
- lexical scope
- loading code from files
- simple mark & sweep garbage collector

The implementation consists of a classic list-structured memory, and a
recursive evaluator.

## Compiling

You should have a C compiler and `make`. To compile, just run:

```sh
make
```

And you should get the `lisp` binary in current directory.

## Running

Run the example `test.lisp` file like this:

```sh
./lisp test.lisp
```
