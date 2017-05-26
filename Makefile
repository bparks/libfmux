.PHONY : all debug test install clean

CFLAGS = -I include

debug: CFLAGS += -DDEBUG -g
debug: all

all : libfmux.so libfmux.a

libfmux.so : include/fmux.h src/fmux.c
	gcc -shared -pthread -o libfmux.so -fPIC $(CFLAGS) src/*.c

libfmux.a : src/fmux.o
	ar rc libfmux.a src/fmux.o

%.o : %.c
	gcc -c -o $@ $<

test/test : test/test.c src/fmux.c
	gcc -o test/test $(CFLAGS) -L . -lfmux test/test.c

test : debug test/test
	@test/test || echo "TESTS FAILED"

clean :
	rm -rf *.a *.so test/test src/*.o *.dSYM test/*.dSYM *.dtps

install : all
	install -m 444 include/fmux.h /usr/local/include/
	install -m 444 libfmux.so /usr/local/lib/
	install -m 444 libfmux.a /usr/local/lib/
