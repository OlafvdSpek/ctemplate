// Copyright 2011 Google Inc. All Rights Reserved.
// Author: csilvers@google.com (Craig Silverstein)
//
// Provides macros and typedefs based on config.h settings.
// Provides the following macros:
//    UNALIGNED_LOAD32   (may be an inline function on some architectures)
// and the following typedefs:
//    uint32
//    uint64

#ifndef CTEMPLATE_MACROS_H_
#define CTEMPLATE_MACROS_H_

#include <config.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>         // the normal place uint32_t is defined
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>      // the normal place u_int32_t is defined
#endif
#ifdef HAVE_INTTYPES_H
#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif       // a third place for uint32_t or u_int32_t
#endif

#if defined(HAVE_U_INT32_T)
typedef u_int32_t uint32;
#elif defined(HAVE_UINT32_T)
typedef uint32_t uint32;
#elif defined(HAVE___INT32)
typedef unsigned __int32 uint32;
#endif

#if defined(HAVE_U_INT64_T)
typedef u_int64_t uint64;
#elif defined(HAVE_UINT64_T)
typedef uint64_t uint64;
#elif defined(HAVE___INT64)
typedef unsigned __int64 uint64;
#endif


// This is all to figure out endian-ness and byte-swapping on various systems
#if defined(HAVE_ENDIAN_H)
#include <endian.h>           // for the __BYTE_ORDER use below
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>       // location on FreeBSD
#elif defined(HAVE_MACHINE_ENDIAN_H)
#include <machine/endian.h>   // location on OS X
#endif
#if defined(HAVE_SYS_BYTEORDER_H)
#include <sys/byteorder.h>    // BSWAP_32 on Solaris 10
#endif
#ifdef HAVE_SYS_ISA_DEFS_H
#include <sys/isa_defs.h>     // _BIG_ENDIAN/_LITTLE_ENDIAN on Solaris 10
#endif

// MurmurHash does a lot of 4-byte unaligned integer access.  It
// interprets these integers in little-endian order.  This is perfect
// on x86, for which this is a natural memory access; for other systems
// we do what we can to make this as efficient as possible.
#if defined(HAVE_BYTESWAP_H)
# include <byteswap.h>              // GNU (especially linux)
# define BSWAP32(x)  bswap_32(x)
#elif defined(HAVE_LIBKERN_OSBYTEORDER_H)
# include <libkern/OSByteOrder.h>   // OS X
# define BSWAP32(x)  OSSwapInt32(x)
#elif defined(bswap32)              // FreeBSD
  // FreeBSD defines bswap32 as a macro in sys/endian.h (already #included)
# define BSWAP32(x)  bswap32(x)
#elif defined(BSWAP_32)             // Solaris 10
  // Solaris defines BSWSAP_32 as a macro in sys/byteorder.h (already #included)
# define BSWAP32(x)  BSWAP_32(x)
#elif !defined(BSWAP32)
# define BSWAP32(x)  ((((x) & 0x000000ff) << 24) |      \
                      (((x) & 0x0000ff00) << 8)  |      \
                      (((x) & 0x00ff0000) >> 8)  |      \
                      (((x) & 0xff000000) >> 24));
#else
# define CTEMPLATE_BSWAP32_ALREADY_DEFINED
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
  // We know they allow unaligned memory access and are little-endian
# define UNALIGNED_LOAD32(_p) (*reinterpret_cast<const uint32 *>(_p))
#elif defined(__ppc__) || defined(__ppc64__)
  // We know they allow unaligned memory access and are big-endian
# define UNALIGNED_LOAD32(_p) BSWAP32(*reinterpret_cast<const uint32 *>(_p))
#elif (BYTE_ORDER == 1234) || (_BYTE_ORDER == 1234) || defined(_LITTLE_ENDIAN)
  // Use memcpy to align the memory properly
  inline uint32 UNALIGNED_LOAD32(const void *p) {
    uint32 t;
    memcpy(&t, p, sizeof(t));
    return t;
  }
#elif (BYTE_ORDER == 4321) || (_BYTE_ORDER == 4321) || defined(_BIG_ENDIAN)
  inline uint32 UNALIGNED_LOAD32(const void *p) {
    uint32 t;
    memcpy(&t, p, sizeof(t));
    return BSWAP32(t);
  }
#else
  // Means we can't find find endian.h on this machine:
# error Need to define UNALIGNED_LOAD32 for this architecture
#endif

#ifndef CTEMPLATE_BSWAP32_ALREADY_DEFINED
# undef BSWAP32                             // don't leak outside this file
#else
# undef CTEMPLATE_BSWAP32_ALREADY_DEFINED   // just cleaning up
#endif

#endif  // CTEMPLATE_MACROS_H_
