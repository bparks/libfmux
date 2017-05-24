.PHONY : all test install clean

all : libfmux.so libfmux.a

libfmux.so : include/fmux.h src/fmux.c
	gcc -shared -pthread -o libfmux.so -fPIC -I include src/*.c

libfmux.a : src/fmux.o
	ar rc libfmux.a src/fmux.o

%.o : %.c
	gcc -c -o $@ $<

test/test : test/test.c src/fmux.c
	gcc -o test/test -I include -L . -lfmux test/test.c

test : all test/test
	@test/test || echo "TESTS FAILED"

clean :
	rm -rf *.a *.so test/test src/*.o *.dSYM test/*.dSYM *.dtps

install : all
	install -m 444 include/fmux.h /usr/local/include/
	install -m 444 libfmux.so /usr/local/lib/
	install -m 444 libfmux.a /usr/local/lib/
