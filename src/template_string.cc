// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Authors: Jay Crim, Craig Silverstein

#include "config.h"
#include "base/mutex.h"   // This has to come first to get _XOPEN_SOURCE
#include <assert.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>       // one place u_int32_t might live
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>     // another place u_int32_t might live
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>    // final place u_int32_t might live
#endif
#include HASH_SET_H
#include <ctemplate/template_string.h>
#include "base/arena.h"

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
#else
  // We could just define this in C, but might as well find a fast way to do it
# define BSWAP32(x)  Need_to_define_BSWAP32_for_your_architecture(x)
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

_START_GOOGLE_NAMESPACE_

// Based on public domain MurmurHashUnaligned2, by Austin Appleby.
// http://murmurhash.googlepages.com/
// This variation:
//   - interleaves odd/even 32-bit words to improve performance and
//     to generate more random bits,
//   - has a more complex final mix to combine the 32-bit hashes into
//     64-bits,
//   - uses a fixed seed
// This is not static because template_string_test accesses it directly.
uint64 MurmurHash64(const char* ptr, size_t len) {
  const uint32 kMultiplyVal = 0x5bd1e995;
  const int kShiftVal = 24;
  const uint32 kHashSeed1 = 0xc86b14f7;
  const uint32 kHashSeed2 = 0x650f5c4d;

  uint32 h1 = kHashSeed1 ^ len, h2 = kHashSeed2;
  while (len >= 8) {
    uint32 k1 = UNALIGNED_LOAD32(ptr);
    k1 *= kMultiplyVal;
    k1 ^= k1 >> kShiftVal;
    k1 *= kMultiplyVal;

    h1 *= kMultiplyVal;
    h1 ^= k1;
    ptr += 4;

    uint32 k2 = UNALIGNED_LOAD32(ptr);
    k2 *= kMultiplyVal;
    k2 ^= k2 >> kShiftVal;
    k2 *= kMultiplyVal;

    h2 *= kMultiplyVal;
    h2 ^= k2;
    ptr += 4;

    len -= 8;
  }

  if (len >= 4) {
    uint32 k1 = UNALIGNED_LOAD32(ptr);
    k1 *= kMultiplyVal;
    k1 ^= k1 >> kShiftVal;
    k1 *= kMultiplyVal;

    h1 *= kShiftVal;
    h1 ^= k1;

    ptr += 4;
    len -= 4;
  }

  switch(len) {
    case 3:
      h2 ^= ptr[2] << 16;  // fall through.
    case 2:
      h2 ^= ptr[1] << 8;  // fall through.
    case 1:
      h2 ^= ptr[0];  // fall through.
    default:
      h2 *= kMultiplyVal;
  }

  h1 ^= h2 >> 18;
  h1 *= kMultiplyVal;
  h2 ^= h1 >> 22;
  h2 *= kMultiplyVal;
  h1 ^= h2 >> 17;
  h1 *= kMultiplyVal;

  uint64 h = h1;
  h = (h << 32) | h2;
  return h;
}

// Unlike StaticTemplateString, it is not a good idea to have a
// default TemplateString::Hasher because TemplateString does not
// provide any lifetime guarantees.  The global template_string_set is
// an obvious exception.
struct TemplateStringHasher {
  size_t operator()(const TemplateString& ts) const {
    TemplateId id = ts.GetGlobalId();
    assert(IsTemplateIdInitialized(id));
    return hasher(id);
  }
  // Less operator for MSVC's hash containers.
  bool operator()(const TemplateString& a, const TemplateString& b) const {
    const TemplateId id_a = a.GetGlobalId();
    const TemplateId id_b = b.GetGlobalId();
    assert(IsTemplateIdInitialized(id_a));
    assert(IsTemplateIdInitialized(id_b));
    return hasher(id_a, id_b);
  }
  // static makes this compile under MSVC (shrug)
  static const TemplateIdHasher hasher;
  // These two public members are required by msvc.  4 and 8 are defaults.
  static const size_t bucket_size = 4;
  static const size_t min_buckets = 8;
};

/*static*/ const TemplateIdHasher TemplateStringHasher::hasher = {};

namespace {
Mutex mutex(Mutex::LINKER_INITIALIZED);

#ifdef HAVE_UNORDERED_MAP
typedef HASH_NAMESPACE::unordered_set<TemplateString, TemplateStringHasher>
    TemplateStringSet;
#else
typedef HASH_NAMESPACE::hash_set<TemplateString, TemplateStringHasher>
    TemplateStringSet;
#endif

TemplateStringSet* template_string_set;
UnsafeArena* arena;
}  // namespace


size_t StringHash::Hash(const char* s, size_t slen) const {
  return static_cast<size_t>(MurmurHash64(s, slen));
}

void TemplateString::AddToGlobalIdToNameMap() {
  // shouldn't be calling this if we don't have an id.
  assert(IsTemplateIdInitialized(id_));
  {
    // Check to see if it's already here.
    ReaderMutexLock reader_lock(&mutex);
    if (template_string_set) {
      TemplateStringSet::const_iterator iter =
          template_string_set->find(*this);
      if (iter != template_string_set->end()) {
        assert(length_ == iter->length_ &&
               memcmp(ptr_, iter->ptr_, length_) == 0);
        // "TemplateId collision!";
        return;
      }
    }
  }
  WriterMutexLock writer_lock(&mutex);
  // First initialize our data structures if we need to.
  if (!template_string_set)
    template_string_set = new TemplateStringSet;
  if (!arena)
    arena = new UnsafeArena(1024);  // 1024 was picked out of a hat.

  if (template_string_set->find(*this) != template_string_set->end()) {
    return;
  }
  // If we are immutable, we can store ourselves directly in the map.
  // Otherwise, we need to make an immutable copy.
  if (is_immutable()) {
    template_string_set->insert(*this);
  } else {
    const char* immutable_copy = arena->Memdup(ptr_, length_);
    template_string_set->insert(
        TemplateString(immutable_copy, length_, true, id_));
  }
}

TemplateId TemplateString::GetGlobalId() const {
  if (IsTemplateIdInitialized(id_)) {
    return id_;
  }
  // Initialize the id and sets the "initialized" flag.
  return static_cast<TemplateId>(MurmurHash64(ptr_, length_) |
                                 kTemplateStringInitializedFlag);
}

// static
TemplateString TemplateString::IdToString(TemplateId id) {
  ReaderMutexLock reader_lock(&mutex);
  if (!template_string_set)
    return TemplateString(kStsEmpty);
  // To search the set by TemplateId, we must first construct a dummy
  // TemplateString.  This may seem weird, but it lets us use a
  // hash_set instead of a hash_map.
  TemplateString id_as_template_string(NULL, 0, false, id);
  TemplateStringSet::const_iterator iter =
      template_string_set->find(id_as_template_string);
  if (iter == template_string_set->end()) {
    return TemplateString(kStsEmpty);
  }
  return *iter;
}

StaticTemplateStringInitializer::StaticTemplateStringInitializer(
    const StaticTemplateString* sts) {
  // Compute the sts's id if it wasn't specified at static-init
  // time.  If it was specified at static-init time, verify it's
  // correct.  This is necessary because static-init id's are, by
  // nature, pre-computed, and the id-generating algorithm may have
  // changed between the time they were computed and now.
  if (sts->do_not_use_directly_.id_ == 0)
    sts->do_not_use_directly_.id_ = TemplateString(*sts).GetGlobalId();
  else
    // Don't use the TemplateString(const StaticTemplateString& sts)
    // constructor below, since if we do, GetGlobalId will just return
    // sts->do_not_use_directly_.id_ and the check will be pointless.
    assert(TemplateString(sts->do_not_use_directly_.ptr_,
                          sts->do_not_use_directly_.length_).GetGlobalId()
           == sts->do_not_use_directly_.id_);

  // Now add this id/name pair to the backwards map from id to name.
  TemplateString ts_copy_of_sts(*sts);
  ts_copy_of_sts.AddToGlobalIdToNameMap();
}

_END_GOOGLE_NAMESPACE_
