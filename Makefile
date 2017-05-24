.PHONY : all test

all : libfmux.so libfmux.a

libfmux.so : include/fmux.h src/fmux.c
	gcc -shared -pthread -g -o libfmux.so -fPIC -I include src/*.c

libfmux.a : src/fmux.o
	ar rc fmux.a src/fmux.o

%.o : %.c
	gcc -c -o $@ $<

test/test : test/test.c src/fmux.c
	gcc -g -o test/test -I include -L . -lfmux test/test.c

test : all test/test
	@test/test || echo "TESTS FAILED"
