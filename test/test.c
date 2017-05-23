#include <fmux.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int successes = 0, failures = 0, tests = 0;
#define SUCCESS tests++; fprintf(stderr, "."); successes++;
#define FAILURE tests++; fprintf(stderr, "F"); failures++;
#define ASSERT(x) \
    if (x) { SUCCESS } else { FAILURE ;; return; }

void
test_basic_plumbing()
{
    int fd = open("/dev/null", O_RDONLY);
    fmux_handle* handle = fmux_open(fd, FMUX_RECOMMENDED_CHANS);
    ASSERT((handle != NULL))
    fmux_close(handle);
    ASSERT((1))
}

void
test_writing()
{
    int fd[2];
    int err = pipe(fd);
    if (err < -1) { perror("Pipe creation"); FAILURE }
    fmux_handle* handle = fmux_open(fd[1], FMUX_RECOMMENDED_CHANS);
    fmux_channel* channel = fmux_open_channel(handle, 1);
    ASSERT((channel != NULL))
    fmux_write(channel, "Hello", 6);

    char buf[1024];
    int nbytes = read(fd[0], buf, 1024);

    ASSERT((nbytes == 14))
    //ASSERT((strcmp("\0\0\0\1\0\0\0\6Hello", buf) == 0))

    fmux_close(handle);
}


int
main (int argc, char ** argv)
{
    test_basic_plumbing();
    test_writing();

    printf("\n\nTests: %6d; Passed: %6d; Failed: %6d\n\n", tests, successes, failures);

    return 0;
}
