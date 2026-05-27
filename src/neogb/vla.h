#if defined(_MSC_VER)
#include <malloc.h>
#define VLA(name, size, type) type *name = _alloca(sizeof(type) * (size))
#else
#define VLA(name, size, type) type name[size]
#endif
