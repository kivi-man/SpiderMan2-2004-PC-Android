// Included before every Android host source: what the Linux runtime takes from glibc that bionic has only on newer
// Android versions (memfd_create from API 30) or in other headers.
#pragma once
#ifdef __cplusplus
#include <sys/syscall.h>
#include <unistd.h>

#if __ANDROID_API__ < 30
inline int memfd_create(const char* name, unsigned int flags) { return int(syscall(__NR_memfd_create, name, flags)); }
#endif
#endif
