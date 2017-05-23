
#include "../include/fmux.h"

#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

//For debugging
#include <stdio.h>
#include <errno.h>

#define MAX(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

struct _fmux_channel {
    int id;
    fmux_handle* handle;
    /* Confusing as it is, both pipes below are read and written
     * read from pipe[0] and write to pipe[1]
     * The names, however, come from the fact that the client
     * process reads from pipe_read[0] and writes to pipe_write[1].
     * The underlying fd, therefore, reads from pipe_write[0] and
     * writes to pipe_read[1] */
    int pipe_read[2];
    int pipe_write[2];
    fmux_chantype type;
};

struct _fmux_handle {
    int fd;
    int max_channels;
    pthread_mutex_t lock;
    fmux_channel **channels;
};


int
fmux_close_channel(fmux_handle* handle, int index);

fmux_handle*
fmux_open(int fd, int max_channels)
{
    fmux_handle* ret = malloc(sizeof(fmux_handle));
    memset(ret, 0, sizeof(fmux_handle));

    ret->fd = fd;
    ret->max_channels = max_channels;
    ret->channels = calloc(max_channels, sizeof(fmux_channel*));

    pthread_mutex_init(&(ret->lock), NULL);

    return ret;
}

void
fmux_close(fmux_handle* handle)
{
    for (int i = 0; i < handle->max_channels; i++) {
        if (handle->channels[i] == NULL)
            continue;
        fmux_close_channel(handle, i);
    }
    free(handle->channels);
    handle->channels = NULL;
    int err = pthread_mutex_destroy(&(handle->lock));
    if (err < 0) perror("Destroying mutex");
    close(handle->fd); //Should I do this? I don't open this file descriptor...
    free(handle);
}

fmux_channel*
fmux_open_channel(fmux_handle* handle, int channel_id)
{
    if (handle == NULL) return NULL;
    if (channel_id >= handle->max_channels) return NULL;

    fmux_channel* chan = malloc(sizeof(fmux_channel));
    chan->id = channel_id;
    chan->handle = handle;
    chan->type = FMUX_CHANTYPE_TEXT; //Does this matter?
    pipe(chan->pipe_read);
    pipe(chan->pipe_write);
    handle->channels[channel_id] = chan;

    return chan;
}

int
fmux_close_channel(fmux_handle* handle, int index)
{
    if (handle == NULL) return -1;
    if (index >= handle->max_channels) return -1;

    fmux_channel* channel = handle->channels[index];
    handle->channels[index] = NULL;
    close(channel->pipe_read[0]);
    close(channel->pipe_read[1]);
    close(channel->pipe_write[0]);
    close(channel->pipe_write[1]);
    channel->handle = NULL;
    free(channel);
}

/* Reading */

int
fmux_pop(fmux_handle* handle, fmux_message* message)
{

}

int
fmux_select(fmux_handle* handle, fmux_channel** ready, struct timeval *restrict timeout)
{

}

int
fmux_read(fmux_channel* channel, void* buf, size_t nbyte)
{

}

/* Writing */

int
fmux_push(fmux_handle* handle, fmux_message* message)
{
    int id = message->channel_id;
    int bytes = message->nbytes;
    message->nbytes = htonl(bytes);
    message->channel_id = htonl(id);
    write(handle->fd, message, bytes + 8);
}

/* PRIVATE */ int
fmux_flush_writes(fmux_handle* handle)
{
    fd_set fds;
    int nfds;
    FD_ZERO(&fds);
    for (int i = 0; i < handle->max_channels; i++) {
        if (handle->channels[i] == NULL) continue;
        FD_SET(handle->channels[i]->pipe_write[0], &fds);
        nfds = MAX(nfds, handle->channels[i]->pipe_write[0]);
    }
    struct timeval timeout;
    timeout.tv_sec = 0; timeout.tv_usec = 0;
    int n = select(nfds + 1, &fds, NULL, NULL, &timeout);
    for (int i = 0; i < handle->max_channels; i++) {
        if (handle->channels[i] == NULL) continue;
        if (FD_ISSET(handle->channels[i]->pipe_write[0], &fds)) {
            char buf[1024];
            int bytes = read(handle->channels[i]->pipe_write[0], buf, 1024);
            fmux_message* mess = calloc(bytes + 8, 1);
            mess->channel_id = i;
            mess->nbytes = bytes;
            memcpy(mess->data, buf, bytes);
            fmux_push(handle, mess);
            free(mess);
        }
    }
}

int
fmux_write(fmux_channel* channel, const void* buf, size_t nbyte)
{
    write(channel->pipe_write[1], buf, nbyte);
    fmux_flush_writes(channel->handle);
}

/* A background process (optional) for continuously flushing the socket.
 * This DOES NOT spawn its own thread; YOU should do that part.
 */

//Returns "pump ID"
int
fmux_pump_start()
{

}

int
fmux_pump_add_handle(fmux_handle* handle)
{

}

int
fmux_pump_remove_handle(fmux_handle* handle)
{

}

int
fmux_pump_stop(int pump_id);
