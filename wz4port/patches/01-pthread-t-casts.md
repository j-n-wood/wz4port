# Patch 01 — pthread_t casts in system_linux.cpp

**File:** `altona_wz4/altona/main/base/system_linux.cpp`
**Lines touched:** 4
**Phase:** 1 (toolchain)
**Status:** applied

## Why

`system_linux.cpp` assumes `pthread_t` is an integer. On glibc/Linux it is
`unsigned long`, so the assumptions hold and the file compiles. On macOS
`pthread_t` is `struct _opaque_pthread_t *` — a pointer — and four sites fail
to compile:

```
system_linux.cpp:976  cannot initialize return object of type 'sInt' with an rvalue of type 'pthread_t'
system_linux.cpp:1019 cannot initialize a variable of type 'sU64' with an rvalue of type 'pthread_t'
system_linux.cpp:1022 no matching function for call to 'pthread_equal'
system_linux.cpp:1060 incompatible pointer to integer conversion assigning to 'sU64' from 'pthread_t'
```

These are genuine type errors. No macro or shim can fix them, which is why
this is a patch and not part of `compat/include/wz4port_posix_compat.h` —
that header handles the surrounding glibc-isms (`lseek64`, `strdupa`,
`pthread_yield`, …) with no upstream change at all.

## Why not fork the file

`system_linux.cpp` is 2,274 lines. Copying it to `compat/system_osx.cpp`
would duplicate upstream wholesale and guarantee drift, to fix four lines.
Patching in place is the smaller and more honest change. The plan
(`docs/03-phase-toolchain.md`, stage 1.3) originally anticipated a full
copy; measurement showed it unnecessary.

## The change

`sThread::ThreadId` is declared `sU64` on this platform
(`base/system.hpp:370`), so round-tripping a `pthread_t` through it is fine
with explicit casts via `sDInt` (Altona's pointer-sized integer).

```cpp
// :976
- return pthread_self();
+ return (sInt)(sDInt)pthread_self();

// :1019-1022
- sU64 self = pthread_self();
- sLogF(L"sys",L"New sThread started. 0x%x, id is 0x%x\n", self, th->ThreadId);
- sVERIFY( pthread_equal( self, th->ThreadId ) );
+ pthread_t self = pthread_self();
+ sLogF(L"sys",L"New sThread started. 0x%x, id is 0x%x\n", (sU64)(sDInt)self, th->ThreadId);
+ sVERIFY( pthread_equal( self, (pthread_t)(sDInt)th->ThreadId ) );

// :1060
- ThreadId = *(pthread_t*)ThreadHandle;
+ ThreadId = (sU64)(sDInt)*(pthread_t*)ThreadHandle;
```

Keeping `self` as a real `pthread_t` means `pthread_equal` is still called
with the correct type rather than being cast around, which is what it is for.

## Behaviour

Unchanged on Linux: the casts are no-ops where `pthread_t` is already an
integer of the same width.

## Known wart, not introduced here

`sGetCurrentThreadId()` returns `sInt` (32-bit) while a macOS `pthread_t` is
a 64-bit pointer, so the value is truncated. This is pre-existing API design
— the return type is `sInt` upstream — and the result is only used as an
opaque identity token for logging and comparison. Truncation could in
principle collide, but two live `pthread_t` pointers colliding in their low
32 bits is not a realistic concern, and nothing in the headless path uses
this function. Flagged here so it is not rediscovered as a mystery later.
