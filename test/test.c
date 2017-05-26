#include <fmux.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>

int successes = 0, failures = 0, tests = 0;
#define SUCCESS tests++; fprintf(stderr, "."); successes++;
#define FAILURE tests++; fprintf(stderr, "F"); failures++;
#define ASSERT(x) \
    if (x) { SUCCESS } else { FAILURE ;; fprintf(stderr, "%s\n", #x); return; }

/* Private header stubs */
int
fmux_close_channel(fmux_handle* handle, int index);

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
    ASSERT((handle != NULL))
    fmux_channel* channel = fmux_open_channel(handle, 1);
    ASSERT((channel != NULL))
    fmux_write(channel, "Hello", 6);

    char buf[1024];
    int nbytes = read(fd[0], buf, 1024);

    ASSERT((nbytes == 14))
    //ASSERT((strcmp("\0\0\0\1\0\0\0\6Hello", buf) == 0))

    fmux_close(handle);
}

void
test_reading()
{
    int fd[2];
    int err = pipe(fd);
    if (err < -1) { perror("Pipe creation"); FAILURE }

    char * hello = "\0\0\0\1\0\0\0\6Hello";
    write(fd[1], hello, 14);

    fmux_handle* handle = fmux_open(fd[0], FMUX_RECOMMENDED_CHANS);
    fmux_channel* channel = fmux_open_channel(handle, 1);
    ASSERT((channel != NULL))
    char buf[1024];
    int nread = fmux_read(channel, buf, 1024);
    ASSERT((nread == 6))
    ASSERT((strcmp(buf, "Hello") == 0))

    char * goodbye = "\0\0\0\1\0\0\0\x8Goodbye";
    write(fd[1], goodbye, 16);
    nread = fmux_read(channel, buf, 1024);
    ASSERT((nread == 8))
    ASSERT((strcmp(buf, "Goodbye") == 0))

    fmux_close(handle);
}

void
test_writing_to_nonexistent_channel()
{
    int fd[2];
    int err = pipe(fd);
    if (err < -1) { perror("Pipe creation"); FAILURE }
    fmux_handle* handle = fmux_open(fd[1], FMUX_RECOMMENDED_CHANS);
    ASSERT((handle != NULL))
    fmux_channel* channel = fmux_open_channel(handle, 1);
    ASSERT((channel != NULL))
    int nbytes = fmux_write(channel, "Hello", 6);
    ASSERT((nbytes == 6))
    fmux_close_channel(handle, 1);
    nbytes = fmux_write(channel, "Hello", 6);
    ASSERT((nbytes == 0))

    char buf[1024];
    nbytes = read(fd[0], buf, 1024);

    ASSERT((nbytes == 14))
    //ASSERT((strcmp("\0\0\0\1\0\0\0\6Hello", buf) == 0))

    fmux_close(handle);
}

void
test_reading_from_nonexistent_channel()
{
    int fd[2];
    int err = pipe(fd);
    if (err < -1) { perror("Pipe creation"); FAILURE }

    char * hello = "\0\0\0\1\0\0\0\6Hello";
    write(fd[1], hello, 14);

    fmux_handle* handle = fmux_open(fd[0], FMUX_RECOMMENDED_CHANS);
    fmux_channel* channel = fmux_open_channel(handle, 1);
    ASSERT((channel != NULL))
    char buf[1024];
    int nread = fmux_read(channel, buf, 1024);
    ASSERT((nread == 6))
    ASSERT((strcmp(buf, "Hello") == 0))
    fmux_close_channel(handle, 1);

    //Closed, so nothing to read
    nread = fmux_read(channel, buf, 1024);
    ASSERT((nread == 0))

    //Writing to the channel should NOT automatically open a channel; the data
    //should be silently dropped
    char * goodbye = "\0\0\0\1\0\0\0\x8Goodbye";
    write(fd[1], goodbye, 16);

    //fmux_select should NOT list a channel object with id 1 (indeed,
    // there should be 0 channels ready to be read)
    //When the second argument to fmux_select is NULL, it acts like like a
    //poll indicating whether or not there is available data.
    struct timeval timeout;
    timeout.tv_sec = 0; timeout.tv_usec = 0;
    err = fmux_select(handle, NULL, &timeout);
    ASSERT((err == 0))

    fmux_close(handle);
}

void
test_writing_to_closed_socket()
{
    //Disable SIGPIPE for this test
    signal(SIGPIPE, SIG_IGN);

    int fd[2];
    int err = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
    if (err < -1) { perror("socketpair"); FAILURE }
    fmux_handle* handle = fmux_open(fd[1], FMUX_RECOMMENDED_CHANS);
    ASSERT((handle != NULL))
    fmux_channel* channel = fmux_open_channel(handle, 1);
    ASSERT((channel != NULL))
    int nbytes = fmux_write(channel, "Hello", 6);
    ASSERT((nbytes == 6))
    close(fd[0]);
    //nbytes = fmux_write(channel, "Hello", 6); //The indication that the socket is closed probably lags by a message (but we'd prefer not)
    nbytes = fmux_write(channel, "Hello", 6);
    ASSERT((nbytes == -1))

    fmux_close(handle);

    //Undisable SIGPIPE for this test
    signal(SIGPIPE, SIG_DFL);
}



void
test_reading_with_fmux_select()
{
    int fd[2];
    int err = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
    if (err < -1) { perror("socketpair"); FAILURE }

    char * hello = "\0\0\0\1\0\0\0\xA" "Channel 1";
    write(fd[1], hello, 18);
    char * goodbye = "\0\0\0\2\0\0\0\xA" "Channel 2";
    write(fd[1], goodbye, 18);

    fmux_handle* handle = fmux_open(fd[0], FMUX_RECOMMENDED_CHANS);
    fmux_channel* channel1 = fmux_open_channel(handle, 1);
    fmux_channel* channel2 = fmux_open_channel(handle, 2);

    ASSERT((channel1 != NULL))
    ASSERT((channel2 != NULL))

    fmux_channel** ready = calloc(FMUX_RECOMMENDED_CHANS, sizeof(fmux_channel*));
    struct timeval timeout;
    timeout.tv_sec = 0; timeout.tv_usec = 0;
    err = fmux_select(handle, ready, &timeout);

    ASSERT((err != -1))
    ASSERT((err != 0))
    ASSERT((err == 2))

    for (int i = 0; i < err; i++) {
        char buf[1024];
        int nread = fmux_read(ready[i], buf, 1024);
        ASSERT((nread == 10))
        char truth[1024];
        sprintf(truth, "Channel %d", i+1);
        ASSERT((strcmp(buf, truth) == 0))
    }

    free(ready);

    fmux_close(handle);
}

void
test_management_of_handle_lists()
{
    int fd[2];
    int err = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
    if (err < -1) { perror("socketpair"); FAILURE }

    fmux_handle* handle1 = fmux_open(fd[0], FMUX_RECOMMENDED_CHANS);
    fmux_handle* handle2 = fmux_open(fd[1], FMUX_RECOMMENDED_CHANS);
    fmux_pump pump;
    fmux_pump_init(&pump);

    fmux_pump_add_handle(&pump, handle1);
    ASSERT((pump.length == 1))

    fmux_pump_add_handle(&pump, handle1);
    ASSERT((pump.length == 1))

    fmux_pump_add_handle(&pump, handle2);
    ASSERT((pump.length == 2))

    fmux_pump_remove_handle(&pump, handle1);
    ASSERT((pump.length == 1))

    fmux_pump_add_handle(&pump, handle1);
    ASSERT((pump.length == 2))

    fmux_pump_remove_handle(&pump, handle1);
    ASSERT((pump.length == 1))
}

void*
fmux_pump_t_func(void* arg)
{
    fmux_pump_start(arg);
    return NULL;
}

void
test_using_pump()
{
    int fd[2];
    int err = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
    if (err < -1) { perror("socketpair"); FAILURE }

    fmux_handle* handle1 = fmux_open(fd[0], FMUX_RECOMMENDED_CHANS);
    fmux_handle* handle2 = fmux_open(fd[1], FMUX_RECOMMENDED_CHANS);
    fmux_channel* channel1 = fmux_open_channel(handle1, 0);
    fmux_channel* channel2 = fmux_open_channel(handle2, 0);

    char * hello = "\0\0\0\0\0\0\0\xA" "Channel 0";
    write(fd[1], hello, 18);
    write(fd[0], hello, 18);

    fmux_pump pump;
    fmux_pump_init(&pump);
    pthread_t thread;
    pthread_create(&thread, NULL, &fmux_pump_t_func, &pump);
    fmux_pump_add_handle(&pump, handle1);
    fmux_pump_add_handle(&pump, handle2);

    //sleep(3); //Give it enough time to process the messages above (which get
              //dropped because we didn't open any channels)

    char buf[1024];
    err = fmux_read(channel1, buf, 1024);
    ASSERT((err > 0))
    ASSERT((strcmp(buf, "Channel 0") == 0))
    err = fmux_read(channel2, buf, 1024);
    ASSERT((err > 0))
    ASSERT((strcmp(buf, "Channel 0") == 0))

    fmux_pump_stop(&pump);
    pthread_cancel(thread);
    pthread_join(thread, NULL);
}


int
main (int argc, char ** argv)
{
    test_basic_plumbing();
    test_writing();
    test_reading();
    test_writing_to_nonexistent_channel();
    test_reading_from_nonexistent_channel();
    test_writing_to_closed_socket();
    test_reading_with_fmux_select();
    //test_management_of_handle_lists();
    test_using_pump();

    printf("\n\nTests: %6d; Passed: %6d; Failed: %6d\n\n", tests, successes, failures);

    return (tests == successes) ? 0 : 1;
}
