/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Haiku does not ship sys/sysmacros.h. The encoding matches the R5 libdrm
 * edit: the high byte is the major, the low byte is the minor.
 */
#ifndef R5_SYS_SYSMACROS_H
#define R5_SYS_SYSMACROS_H

static inline unsigned int
major(unsigned long dev)
{
	return (unsigned int)((dev >> 8) & 0xffu);
}

static inline unsigned int
minor(unsigned long dev)
{
	return (unsigned int)(dev & 0xffu);
}

static inline unsigned long
makedev(unsigned int maj, unsigned int minor_dev)
{
	return ((unsigned long)maj << 8) | minor_dev;
}

#endif
