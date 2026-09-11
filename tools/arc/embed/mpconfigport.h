#include <alloca.h>
#include <limits.h>
#include <stdint.h>
typedef long mp_off_t;
#define MP_SSIZE_MAX INTPTR_MAX
// wolfSSL also exports mp_init; keep the VM initializer separate.
#define mp_init arc_vm_init
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#define MICROPY_ENABLE_COMPILER (0)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#define MICROPY_HAS_FILE_READER (1)
#define MICROPY_KBD_EXCEPTION (0)
#define MICROPY_PY_BUILTINS_HELP (0)
#define MICROPY_PY_SYS_PLATFORM "arc"
#define MICROPY_READER_POSIX (0)
#define MICROPY_VFS (0)
// Use memory-mapped bytecode/constants; files still come from the pack reader.
#define MICROPY_VFS_ROM (1)
#define MICROPY_VFS_ROM_IOCTL (0)
#define MICROPY_PY_OS (0)
#define MICROPY_PY_IO (0)
#define MICROPY_PY_SYS_STDFILES (0)
#define MICROPY_PY_SYS_STDIO_BUFFER (0)
#define MICROPY_PY_BUILTINS_INPUT (0)
#define MICROPY_PY_BUILTINS_OPEN (0)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS (1)
#define MICROPY_PY_BUILTINS_SLICE_INDICES (1)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_ENABLE_SCHEDULER (0)
#define MICROPY_PY_THREAD (0)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_JSON (0)
#define MICROPY_PY_HASHLIB (0)
#define MICROPY_PY_RANDOM (0)
#define MICROPY_PY_TIME (0)
#define MICROPY_PY_STRUCT (0)
#define MICROPY_PY_BINASCII (0)
#define MICROPY_PY_RE (0)
#define MICROPY_PY_SELECT (0)
#define MICROPY_PY_SOCKET (0)
#define MICROPY_PY_ERRNO (0)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE (256)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_NLR_SETJMP (1)
#ifdef __XTENSA__
#define MICROPY_GCREGS_SETJMP (1)
#endif
#ifdef __cplusplus
extern "C" void arc_vm_poll(void);
#else
void arc_vm_poll(void);
#endif
#define MICROPY_VM_HOOK_LOOP arc_vm_poll();
#define MICROPY_VM_HOOK_RETURN arc_vm_poll();
#define MICROPY_USE_INTERNAL_PRINTF (0)
#define MICROPY_MPHALPORT_H "mphalport.h"
#define MODULE_ULAB_ENABLED (1)
#define ULAB_HAS_SCIPY (0)
#define ULAB_SUPPORTS_COMPLEX (0)
#define ULAB_NUMPY_HAS_LOAD (0)
#define ULAB_NUMPY_HAS_LOADTXT (0)
#define ULAB_NUMPY_HAS_SAVE (0)
#define ULAB_NUMPY_HAS_SAVETXT (0)
