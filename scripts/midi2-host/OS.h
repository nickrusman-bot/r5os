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

enum {
	B_OK = 0,
	B_ERROR = -1,
	B_BAD_VALUE = -2,
	B_BAD_DATA = -3,
	B_NAME_NOT_FOUND = -4
};

#endif
