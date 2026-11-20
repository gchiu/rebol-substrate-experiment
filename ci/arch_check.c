/* ci/arch_check.c - host architecture and memory-boundary invariants.
 *
 * This is a CI probe, not part of the runtime.  It checks the assumptions the
 * frozen flat-cell machine makes on every supported 64-bit host: 8-byte
 * intptr_t cells, natural alignment, a 65,536-cell map and the documented
 * ordering of the managed heap, loader heap and M1 regions.
 */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include "s1.h"
#include "r0_s1.h"
#include "m1_layout.h"

_Static_assert(sizeof(cell) == 8, "Glon requires a 64-bit intptr_t cell");
_Static_assert(sizeof(cell) == sizeof(void *), "cell and pointer must have equal width");
_Static_assert(_Alignof(cell) >= 8, "cell alignment must be at least 8 bytes");
_Static_assert(MEM_CELLS == 65536, "the frozen map is 65,536 cells");
_Static_assert(GC_HEAP_BASE < GC_HEAP_LIMIT, "managed heap must be non-empty");
_Static_assert(R0S1_LOAD_BASE >= GC_HEAP_LIMIT, "loader heap must not overlap the managed heap");
_Static_assert(R0S1_HEAP_LIMIT >= R0S1_LOAD_BASE, "loader heap must be non-empty");
_Static_assert(M1_ARENA_BASE >= R0S1_HEAP_LIMIT, "M1 arena must not overlap the loader heap");
_Static_assert(M1_TASK_TABLE >= M1_ARENA_BASE, "M1 task table must be above its arena");
_Static_assert(M1_TASK_TABLE <= MEM_CELLS, "M1 task table must fit in the cell map");

static const char *platform_name(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

static const char *arch_name(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

int main(void) {
    uint16_t probe = 1;
    int little = *(const unsigned char *)&probe == 1;
    uintptr_t base = (uintptr_t)&M[0];
    size_t align = _Alignof(cell);

    printf("platform=%s\n", platform_name());
    printf("arch=%s\n", arch_name());
#if defined(__clang__)
    printf("compiler=clang-%d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    printf("compiler=gcc-%d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    printf("compiler=unknown\n");
#endif
    printf("cell_bytes=%zu\n", sizeof(cell));
    printf("cell_align=%zu\n", align);
    printf("pointer_bytes=%zu\n", sizeof(void *));
    printf("char_bits=%d\n", CHAR_BIT);
    printf("endian=%s\n", little ? "little" : "big");
    printf("intptr_max=%lld\n", (long long)INTPTR_MAX);
    printf("int64_max=%lld\n", (long long)INT64_MAX);
    printf("mem_cells=%d\n", MEM_CELLS);
    printf("mem_bytes=%zu\n", sizeof M);
    printf("map_base=%zu\n", (size_t)base);
    printf("map_aligned=%d\n", base % align == 0);
    printf("gc_heap=%ld..%ld\n", (long)GC_HEAP_BASE, (long)GC_HEAP_LIMIT);
    printf("loader=%ld..%ld\n", (long)R0S1_LOAD_BASE, (long)R0S1_HEAP_LIMIT);
    printf("m1_arena=%ld\n", (long)M1_ARENA_BASE);
    printf("m1_task_table=%ld\n", (long)M1_TASK_TABLE);
    printf("worklist_cap=%ld\n", (long)GC_WORKLIST_CAP);
    if (base % align != 0 || sizeof M != (size_t)MEM_CELLS * sizeof(cell)) return 1;
    printf("ARCH_CHECK PASS\n");
    return 0;
}
