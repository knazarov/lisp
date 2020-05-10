all: lisp

lisp: lisp.c
	cc -std=c99 -O0 -o lisp lisp.c -g

clean:
	rm -f lisp
