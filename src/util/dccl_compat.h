#ifndef GOBY_UTIL_DCCL_COMPAT_H
#define GOBY_UTIL_DCCL_COMPAT_H

#include <dccl/version.h>
#if (DCCL_VERSION_MAJOR > 4) || (DCCL_VERSION_MAJOR == 4 && DCCL_VERSION_MINOR >= 1)
#define DCCL_VERSION_4_1_OR_NEWER
#endif

#endif
