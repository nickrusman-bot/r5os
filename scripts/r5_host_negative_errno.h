/* Haiku errno values are negative. Force that on the host test. */
#include <errno.h>
#undef ENOENT
#undef EINVAL
#undef ENODEV
#undef ENOMEM
#undef EPERM
#undef ENOSYS
#undef ETIMEDOUT
#undef ETIME
#define ENOENT (-2)
#define EINVAL (-22)
#define ENODEV (-19)
#define ENOMEM (-12)
#define EPERM (-1)
#define ENOSYS (-38)
#define ETIMEDOUT (-110)
#define ETIME (-62)
