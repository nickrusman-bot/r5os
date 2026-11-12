#ifndef _MIDI2_HOST_OS_H
#define _MIDI2_HOST_OS_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t status_t;
typedef int32_t int32;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef unsigned char uchar;
typedef int64_t int64;
typedef uint64_t uint64;
typedef uintptr_t addr_t;
typedef int64_t bigtime_t;
typedef int32_t thread_id;

/* What Midi2Library.cpp uses for a new MUID. */
#include <time.h>
static inline bigtime_t
system_time()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (bigtime_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static inline thread_id
find_thread(const char*)
{
	return 1;
}

static inline int32
atomic_add(int32* value, int32 add)
{
	return __atomic_fetch_add(value, add, __ATOMIC_SEQ_CST);
}

enum {
	B_OK = 0,
	B_ERROR = -1,
	B_BAD_VALUE = -2,
	B_BAD_DATA = -3,
	B_NAME_NOT_FOUND = -4
};

#endif
