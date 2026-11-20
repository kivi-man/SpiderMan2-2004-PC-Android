#pragma once
// PSL1GHT has no ioctl: the socket layer's FIONREAD asks fail, the console's system link is not used on the PS3
#include <errno.h>
#ifndef FIONREAD
#define FIONREAD 0x4004667F
#endif
static inline int ioctl(int, unsigned long, ...) {
    errno = ENOSYS;
    return -1;
}
