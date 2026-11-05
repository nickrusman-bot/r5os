/* Haiku has no sys/sysmacros.h. Mesa and libdrm call major, minor and makedev. */
#ifndef R5_HAIKU_SYSMACROS_H
#define R5_HAIKU_SYSMACROS_H

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
