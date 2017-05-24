#include <sys/types.h>
#include <stdint.h>

#ifndef _FMUX_H_
#define _FMUX_H_

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/*
#ifndef EXPORT
#define EXPORT extern
#endif //EXPORT
*/

typedef int fmux_chantype;

struct timeval;

#define FMUX_CHANTYPE_TEXT 1
#define FMUX_CHANTYPE_BIN 2

#define FMUX_RECOMMENDED_CHANS 32

struct _fmux_channel;
typedef struct _fmux_channel fmux_channel;

struct _fmux_handle;
typedef struct _fmux_handle fmux_handle;

typedef struct {
    uint32_t channel_id;
    uint32_t nbytes;
    char data[1];
} fmux_message;

/* Housekeeping */

fmux_handle*
fmux_open(int fd, int max_channels);

void
fmux_close(fmux_handle* handle);

fmux_channel*
fmux_open_channel(fmux_handle* handle, int channel_id);

/* Reading */

int
fmux_pop(fmux_handle* handle, fmux_message** message);

int
fmux_select(fmux_handle* handle, fmux_channel** ready, struct timeval *restrict timeout);

int
fmux_read(fmux_channel* channel, void* buf, size_t nbyte);

/* Writing */

int
fmux_push(fmux_handle* handle, fmux_message* message);

int
fmux_write(fmux_channel* channel, const void* buf, size_t nbyte);

/* A background process (optional) for continuously flushing the socket.
 * This DOES NOT spawn its own thread; YOU should do that part.
 */

//Returns "pump ID"
int
fmux_pump_start();

int
fmux_pump_add_handle(fmux_handle* handle);

int
fmux_pump_remove_handle(fmux_handle* handle);

int
fmux_pump_stop(int pump_id);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //_FMUX_H_
