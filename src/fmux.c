
#include "../include/fmux.h"

#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <poll.h>

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
    ret->channels = malloc(max_channels * sizeof(fmux_channel*));
    memset(ret->channels, 0, max_channels * sizeof(fmux_channel*));

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

    if (handle->channels[channel_id] != NULL) {
        fprintf(stderr, "Channel %d has already been allocated!\n", channel_id);
        return handle->channels[channel_id];
    }

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
    return 0;
}

int
fmux_channel_is_good(fmux_channel* channel)
{
    if (channel == NULL) return 0;
    if (channel->handle == NULL) return 0;
    if (channel->id < 0 || channel->id > channel->handle->max_channels) return 0;

    return 1;
}

/* Reading */

int
fmux_pop(fmux_handle* handle, fmux_message** message)
{
    int channel_id, len, tmp[2];
    read(handle->fd, tmp, 8);
    channel_id = ntohl(tmp[0]);
    len = ntohl(tmp[1]);
    *message = realloc(*message, len + 2*sizeof(uint32_t));
    memset(*message, 0, len + 2*sizeof(int));
    (*message)->channel_id = channel_id;
    (*message)->nbytes = len;
    read(handle->fd, (*message)->data, len);
    return 1;
}

/* PRIVATE */ int
fmux_flush_reads(fmux_handle* handle)
{
    int m_read = 0;
    struct pollfd pfd = {.fd = handle->fd, .events = POLLRDNORM };
    while (poll(&pfd, 1, 0) == 1) {
        fmux_message* mess = malloc(2 * sizeof(uint32_t));
        fmux_pop(handle, &mess);
        if (mess->channel_id < handle->max_channels && handle->channels[mess->channel_id] != NULL) {
            write(handle->channels[mess->channel_id]->pipe_read[1], mess->data, mess->nbytes);
            m_read++;
        }
        free(mess);

        //Reset the struct
        pfd.fd = handle->fd;
        pfd.events = POLLRDNORM;
    }
    return m_read;
}

int
fmux_select(fmux_handle* handle, fmux_channel** ready, struct timeval *restrict timeout)
{
    return 0;
}

int
fmux_read(fmux_channel* channel, void* buf, size_t nbyte)
{
    if (!fmux_channel_is_good(channel)) return 0;

    fmux_flush_reads(channel->handle);
    return read(channel->pipe_read[0], buf, nbyte);
}

/* Writing */

int
fmux_push(fmux_handle* handle, fmux_message* message)
{
    int id = message->channel_id;
    int bytes = message->nbytes;
    message->nbytes = htonl(bytes);
    message->channel_id = htonl(id);
    return write(handle->fd, message, bytes + 8);
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
            fmux_message* mess = malloc(bytes + 2*sizeof(uint32_t));
            memset(mess, 0, bytes + 2*sizeof(uint32_t));
            mess->channel_id = i;
            mess->nbytes = bytes;
            memcpy(mess->data, buf, bytes);
            fmux_push(handle, mess);
            free(mess);
        }
    }
    return 0;
}

int
fmux_write(fmux_channel* channel, const void* buf, size_t nbyte)
{
    if (!fmux_channel_is_good(channel)) return 0;

    int nwritten = write(channel->pipe_write[1], buf, nbyte);
    fmux_flush_writes(channel->handle);
    return nwritten;
}

/* A background process (optional) for continuously flushing the socket.
 * This DOES NOT spawn its own thread; YOU should do that part.
 */

//Returns "pump ID"
int
fmux_pump_start()
{
    return 0;
}

int
fmux_pump_add_handle(fmux_handle* handle)
{
    return 0;
}

int
fmux_pump_remove_handle(fmux_handle* handle)
{
    return 0;
}

int
fmux_pump_stop(int pump_id);
