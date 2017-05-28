
#include "../include/fmux.h"

#include <sys/select.h>
#include <sys/socket.h>
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
    //Read and write to sock[0] via fmux_read and fmux_write, but
    //read and write into sock[1] from the underlying fd (e.g. in
    //fmux_push and fmux_pop)
    int sock[2]; // { LOCAL, REMOTE }
    fmux_chantype type;
};

struct _fmux_handle {
    int fd;
    int max_channels;
    int sync_read;
    pthread_mutex_t lock;
    fmux_channel **channels;
};

struct _fmux_handle_link {
    fmux_handle* data;
    fmux_handle_link* next;
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
    ret->sync_read = 1;
    ret->channels = malloc(max_channels * sizeof(fmux_channel*));
    memset(ret->channels, 0, max_channels * sizeof(fmux_channel*));

    pthread_mutex_init(&(ret->lock), NULL);

    fmux_open_channel(ret, 0);

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
        return handle->channels[channel_id];
    }

    fmux_channel* chan = malloc(sizeof(fmux_channel));
    chan->id = channel_id;
    chan->handle = handle;
    chan->type = FMUX_CHANTYPE_TEXT; //Does this matter?
    socketpair(AF_LOCAL, SOCK_STREAM, 0, chan->sock);
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
    close(channel->sock[0]);
    close(channel->sock[1]);
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

int
fmux_channel_read_fd(fmux_channel* channel)
{
    if (fmux_channel_is_good(channel)) return channel->sock[0];
    return -1;
}

int
fmux_channel_write_fd(fmux_channel* channel)
{
    if (fmux_channel_is_good(channel)) return channel->sock[1];
    return -1;
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
    if (pthread_mutex_lock(&(handle->lock)) != 0) return -1;
    read(handle->fd, (*message)->data, len);
    pthread_mutex_unlock(&(handle->lock));
    return 1;
}

/* PRIVATE */ int
fmux_flush_reads(fmux_handle* handle)
{
    int m_read = 0;
    struct pollfd pfd = {.fd = handle->fd, .events = POLLIN };
    while (poll(&pfd, 1, 0) == 1) {
        fmux_message* mess = malloc(2 * sizeof(uint32_t));
        fmux_pop(handle, &mess);
        if (mess->channel_id < handle->max_channels && handle->channels[mess->channel_id] != NULL) {
            write(handle->channels[mess->channel_id]->sock[1], mess->data, mess->nbytes);
            m_read++;
        }
        free(mess);

        //Reset the struct
        pfd.fd = handle->fd;
        pfd.events = POLLIN;
    }
    return m_read;
}

int
fmux_select(fmux_handle* handle, fmux_channel** ready, struct timeval *restrict timeout)
{
    if (handle->sync_read)
        fmux_flush_reads(handle);

    fd_set fds;
    int nfds = 0;
    FD_ZERO(&fds);
    for (int i = 0; i < handle->max_channels; i++) {
        if (handle->channels[i] == NULL) continue;
        FD_SET(handle->channels[i]->sock[0], &fds);
        nfds = MAX(nfds, handle->channels[i]->sock[0]);
    }
    nfds = select(nfds + 1, &fds, NULL, NULL, timeout);
    if (nfds == -1 || ready == NULL) return nfds;

    for (int i = 0, j = 0; i < handle->max_channels && j < nfds; i++) {
        if (handle->channels[i] == NULL) continue;
        if (FD_ISSET(handle->channels[i]->sock[0], &fds)) {
            ready[j] = handle->channels[i];
            j++;
        }
    }

    return nfds;
}

int
fmux_read(fmux_channel* channel, void* buf, size_t nbyte)
{
    if (!fmux_channel_is_good(channel)) return 0;

    if (channel->handle->sync_read)
        fmux_flush_reads(channel->handle);
    return read(channel->sock[0], buf, nbyte);
}

/* Writing */

int
fmux_push(fmux_handle* handle, fmux_message* message)
{
    int id = message->channel_id;
    int bytes = message->nbytes;
    message->nbytes = htonl(bytes);
    message->channel_id = htonl(id);
    if (pthread_mutex_lock(&(handle->lock)) != 0) return -1;
    int err = write(handle->fd, message, bytes + 8);
    pthread_mutex_unlock(&(handle->lock));
    return err;
}

/* PRIVATE */ int
fmux_flush_writes(fmux_handle* handle)
{
    fd_set fds;
    int nfds = 0;
    FD_ZERO(&fds);
    for (int i = 0; i < handle->max_channels; i++) {
        if (handle->channels[i] == NULL) continue;
        FD_SET(handle->channels[i]->sock[1], &fds);
        nfds = MAX(nfds, handle->channels[i]->sock[1]);
    }
    struct timeval timeout;
    timeout.tv_sec = 0; timeout.tv_usec = 0;
    int n = select(nfds + 1, &fds, NULL, NULL, &timeout);
    int nwritten = 0;
    for (int i = 0; i < handle->max_channels && nwritten >= 0; i++) {
        if (handle->channels[i] == NULL) continue;
        if (FD_ISSET(handle->channels[i]->sock[1], &fds)) {
            char buf[1024];
            int bytes = read(handle->channels[i]->sock[1], buf, 1024);
            fmux_message* mess = malloc(bytes + 2*sizeof(uint32_t));
            memset(mess, 0, bytes + 2*sizeof(uint32_t));
            mess->channel_id = i;
            mess->nbytes = bytes;
            memcpy(mess->data, buf, bytes);
            nwritten = fmux_push(handle, mess);
            free(mess);
        }
    }
    return (nwritten >= 0);
}

int
fmux_write(fmux_channel* channel, const void* buf, size_t nbyte)
{
    if (!fmux_channel_is_good(channel)) return 0;

    int nwritten = write(channel->sock[0], buf, nbyte);
    if (!fmux_flush_writes(channel->handle)) return -1;
    return nwritten;
}

/* A background process (optional) for continuously flushing the socket.
 * This DOES NOT spawn its own thread; YOU should do that part.
 */


void
fmux_pump_init(fmux_pump* pump)
{
    pump->run = 1;
    pump->head = NULL;
    pump->length = 0;
    pthread_mutex_init(&(pump->lock), NULL);
}

int
fmux_pump_start(fmux_pump* pump)
{
    while (pump->run) {
        if (pump->head == NULL) {
            sleep(1); //Don't waste cycles busy-waiting
            //TODO: Replace this with a semaphore; if the semaphore allows this
            // code to execute and pump->head is still NULL, just continue because
            // we can assume that (maybe?) pump->run has also been set to 0.
            continue;
        }

        pthread_mutex_lock(&(pump->lock));
        struct pollfd* fds = calloc(pump->length, sizeof(struct pollfd));
        fmux_handle_link* cur = pump->head;
        int nfds = 0;
        while (cur != NULL && nfds < pump->length) {
            fds[nfds].fd = cur->data->fd;
            fds[nfds].events = POLLIN;
            nfds++;
            cur = cur->next;
        }

        int nready = poll(fds, nfds, -1); //Block until we have input

        if (nready > 0) {
            //Because of the lock, the number and position of the handles in the list
            //should still be the same.
            int idx = 0;
            nfds = 0;
            cur = pump->head;
            while (cur != NULL && idx < pump->length && nfds < nready) {
                if ((fds[idx].revents & POLLIN) > 0) {
                    fmux_flush_reads(cur->data);
                    nfds++;
                }
                idx++;
                cur = cur->next;
            }
        } else {
            //TODO: So...what happened
            sleep(1);
        }
        pthread_mutex_unlock(&(pump->lock));
    }
    //Just in case we accidentally introduce a break somewhere...
    pump->run = 0;

    fmux_handle_link* cur = pump->head;
    while (cur != NULL) {
        void* to_free = cur;
        cur = cur->next;
        free(to_free);
    }

    pthread_mutex_destroy(&(pump->lock));

    return 0;
}

int
fmux_pump_add_handle(fmux_pump* pump, fmux_handle* handle)
{
    if (!pump->run) return -1; //Can't add handles to a stopped pump!
    if (!handle->sync_read) return -1; //Can't add a handle to multiple pumps;

    pthread_mutex_lock(&(pump->lock));

    fmux_handle_link* cur = pump->head;
    int should_add = 1;
    while (cur != NULL) {
        if (cur->data == handle) {
            //Handle is already in the list. That's fine, but we shouldn't add
            //it again
            should_add = 0;
            break;
        }
        if (cur->next != NULL) {
            cur = cur->next;
        } else {
            break;
        }
    }
    if (should_add) {
        //cur points to the LAST item in the list because of the conditional at
        //the end of the while loop above
        fmux_handle_link* new_item = malloc(sizeof(fmux_handle_link));
        new_item->next = NULL;
        new_item->data = handle;
        handle->sync_read = 0;
        if (cur != NULL) { cur->next = new_item; } else { pump->head = new_item; }
        pump->length++;
    }

    pthread_mutex_unlock(&(pump->lock));

    return 0;
}

int
fmux_pump_remove_handle(fmux_pump* pump, fmux_handle* handle)
{
    if (!pump->run) return -1; //Can't remove handles from a stopped pump!

    pthread_mutex_lock(&(pump->lock));

    fmux_handle_link dummy = {.data = NULL, .next = pump->head};
    fmux_handle_link* cur = &dummy;
    while (cur->next != NULL) {
        if (cur->next->data == handle) {
            //Found it! Now let's remove it.
            //We WON'T reset any of the members yet, though
            fmux_handle_link* to_remove = cur->next;
            to_remove->data->sync_read = 1;
            cur->next = cur->next->next;
            //The special case. If it's the head node, we need to reset pump->head
            if (cur == &dummy) {
                pump->head = pump->head->next;
            }
            //And free the memory
            free(to_remove);
            pump->length--;
            break;
        }
        //Finally, advance pointer
        cur = cur->next;
    }

    pthread_mutex_unlock(&(pump->lock));

    return 0;
}

int
fmux_pump_stop(fmux_pump* pump)
{
    if (!pump->run) return -1;
    pump->run = 0;

    return 0;
}
