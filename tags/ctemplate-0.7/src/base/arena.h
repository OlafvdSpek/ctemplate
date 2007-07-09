// Copyright (c) 2006, Google Inc.
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
// Author: Daniel Dulitz
// Reorganized by Craig Silverstein
//
// Sometimes it is necessary to allocate a large number of small
// objects.  Doing this the usual way (malloc, new) is slow,
// especially for multithreaded programs.  A BaseArena provides a
// mark/release method of memory management: it asks for a large chunk
// from the operating system and doles it out bit by bit as required.
// Then you free all the memory at once by calling BaseArena::Reset().
//
// Use SafeArena for multi-threaded programs where multiple threads
// could access the same arena at once.  (TODO.)
// Use UnsafeArena otherwise.  Usually you'll want UnsafeArena.
//
// To use, just create an arena, and whenever you need a block of
// memory to put something in, call BaseArena::Alloc().  eg
//        s = arena.Alloc(100);
//        snprintf(s, 100, "%s:%d", host, port);
//        arena.Shrink(strlen(s)+1);     // optional; see below for use
//
// You'll probably use the convenience routines more often:
//        s = arena.Strdup(host);        // a copy of host lives in the arena
//        s = arena.Strndup(host, 100);  // we guarantee to NUL-terminate!
//        s = arena.Memdup(protobuf, sizeof(protobuf);
//
// If you go the Alloc() route, you'll probably allocate too-much-space.
// You can reclaim the extra space by calling Shrink() before the next
// Alloc() (or Strdup(), or whatever), with the #bytes you actually used.
//    If you use this method, memory management is easy: just call Alloc()
// and friends a lot, and call Reset() when you're done with the data.

#ifndef _GOOGLE_ARENA_H_
#define _GOOGLE_ARENA_H_

#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Annoying stuff for windows -- make sure clients (in this case
// unittests) can import the class definitions and variables.
#ifndef CTEMPLATE_DLL_DECL
# ifdef WIN32
#   define CTEMPLATE_DLL_DECL  __declspec(dllimport)
# else
#   define CTEMPLATE_DLL_DECL  /* should be the empty string for non-windows */
# endif
#endif

_START_GOOGLE_NAMESPACE_

class CTEMPLATE_DLL_DECL BaseArena {
 protected:         // You can't make an arena directly; only a subclass of one
  BaseArena(char* first_block, const size_t block_size);
 public:
  virtual ~BaseArena();

  virtual void Reset();

  // they're "slow" only 'cause they're virtual (subclasses define "fast" ones)
  virtual char* SlowAlloc(size_t size) = 0;
  virtual void  SlowFree(void* memory, size_t size) = 0;
  virtual char* SlowRealloc(char* memory, size_t old_size, size_t new_size) =0;

  class Status {
   private:
    friend class BaseArena;
    size_t bytes_allocated_;
   public:
    Status() : bytes_allocated_(0) { }
    size_t bytes_allocated() const {
      return bytes_allocated_;
    }
  };

  // Accessors and stats counters
  // This accessor isn't so useful here, but is included so we can be
  // type-compatible with ArenaAllocator (in arena-inl.h).  That is,
  // we define arena() because ArenaAllocator does, and that way you
  // can template on either of these and know it's safe to call arena().
  virtual BaseArena* arena()  { return this; }
  size_t block_size() const   { return block_size_; }
  int block_count() const;
  bool is_empty() const {
    // must check block count in case we allocated a block larger than blksize
    return freestart_ == freestart_when_empty_ && 1 == block_count();
  }

  // This should be the worst-case alignment for any type.  This is
  // good for IA-32, SPARC version 7 (the last one I know), and
  // supposedly Alpha.  i386 would be more time-efficient with a
  // default alignment of 8, but ::operator new() uses alignment of 4,
  // and an assertion will fail below after the call to MakeNewBlock()
  // if you try to use a larger alignment.
#ifdef __i386__
  static const int kDefaultAlignment = 4;
#else
  static const int kDefaultAlignment = 8;
#endif

 protected:
  void MakeNewBlock();
  void* GetMemoryFallback(const size_t size, const int align);
  void* GetMemory(const size_t size, const int align) {
    assert(remaining_ <= block_size_);          // an invariant
    if ( size > 0 && size < remaining_ && align == 1 ) {       // common case
      last_alloc_ = freestart_;
      freestart_ += size;
      remaining_ -= size;
      return reinterpret_cast<void*>(last_alloc_);
    }
    return GetMemoryFallback(size, align);
  }

  // This doesn't actually free any memory
  void ReturnMemory(void* memory, const size_t size) {
  }

  // This is used by Realloc() -- usually we Realloc just by copying to a
  // bigger space, but for the last alloc we can realloc by growing the region.
  bool AdjustLastAlloc(void* last_alloc, const size_t newsize);

  Status status_;

 private:
  const int first_block_we_own_;   // 1 if they pass in 1st block, 0 else
  const size_t block_size_;
  char* freestart_;         // beginning of the free space in most recent block
  char* freestart_when_empty_; // beginning of the free space when we're empty
  char* last_alloc_;         // used to make sure ReturnBytes() is safe
  size_t remaining_;
  // STL vector isn't as efficient as it could be, so we use an array at first
  int blocks_alloced_;       // how many of the first_blocks_ have been alloced
  char* first_blocks_[16];   // the length of this array is arbitrary
  std::vector<char*>* overflow_blocks_;  // if the first_blocks_ aren't enough

  void FreeBlocks();         // Frees all except first block

  BaseArena(const BaseArena&);   // disallow copying
  void operator=(const BaseArena&);
};

class CTEMPLATE_DLL_DECL UnsafeArena : public BaseArena {
 public:
  // Allocates a non-thread-safe arena with the specified block size.
  explicit UnsafeArena(const size_t block_size)
    : BaseArena(NULL, block_size) { }

  // Allocates a non-thread-safe arena with the specified block size.
  // "first_block" must have size "block_size".  Memory is allocated
  // from "first_block" until it is exhausted; after that memory is
  // allocated by allocating new blocks from the heap.
  UnsafeArena(char* first_block, const size_t block_size)
    : BaseArena(first_block, block_size) { }

  char* Alloc(const size_t size) {
    return reinterpret_cast<char*>(GetMemory(size, 1));
  }
  void* AllocAligned(const size_t size, const int align) {
    return GetMemory(size, align);
  }
  char* Calloc(const size_t size) {
    void* return_value = Alloc(size);
    memset(return_value, 0, size);
    return reinterpret_cast<char*>(return_value);
  }
  void* CallocAligned(const size_t size, const int align) {
    void* return_value = AllocAligned(size, align);
    memset(return_value, 0, size);
    return return_value;
  }
  void Free(void* memory, size_t size) {
    ReturnMemory(memory, size);
  }
  virtual char* SlowAlloc(size_t size) { // "slow" 'cause it's virtual
    return Alloc(size);
  }
  virtual void SlowFree(void* memory, size_t size) { // "slow" 'cause it's virt
    Free(memory, size);
  }
  virtual char* SlowRealloc(char* memory, size_t old_size, size_t new_size) {
    return Realloc(memory, old_size, new_size);
  }

  char* Memdup(const char* s, size_t bytes) {
    char* newstr = Alloc(bytes);
    memcpy(newstr, s, bytes);
    return newstr;
  }
  char* MemdupPlusNUL(const char* s, size_t bytes) {  // like "string(s, len)"
    char* newstr = Alloc(bytes+1);
    memcpy(newstr, s, bytes);
    newstr[bytes] = '\0';
    return newstr;
  }
  char* Strdup(const char* s) {
    return Memdup(s, strlen(s) + 1);
  }
  // Unlike libc's strncpy, I always NUL-terminate.  libc's semantics are dumb.
  // This will allocate at most n+1 bytes (+1 is for the NULL terminator).
  char* Strndup(const char* s, size_t n) {
    // Use memchr so we don't walk past n.
    // We can't use the one in //strings since this is the base library,
    // so we have to reinterpret_cast from the libc void *.
    const char* eos = reinterpret_cast<const char*>(memchr(s, '\0', n));
    // if no null terminator found, use full n
    const size_t bytes = (eos == NULL) ? n + 1 : eos - s + 1;
    char* ret = Memdup(s, bytes);
    ret[bytes-1] = '\0';           // make sure the string is NUL-terminated
    return ret;
  }

  // You can realloc a previously-allocated string either bigger or smaller.
  // We can be more efficient if you realloc a string right after you allocate
  // it (eg allocate way-too-much space, fill it, realloc to just-big-enough)
  char* Realloc(char* s, size_t oldsize, size_t newsize);
  // If you know the new size is smaller (or equal), you don't need to know
  // oldsize.  We don't check that newsize is smaller, so you'd better be sure!
  char* Shrink(char* s, size_t newsize) {
    AdjustLastAlloc(s, newsize);       // reclaim space if we can
    return s;                          // never need to move if we go smaller
  }

  // We make a copy so you can keep track of status at a given point in time
  Status status() const { return status_; }

 private:
  UnsafeArena(const UnsafeArena&);     // disallow copying
  void operator=(const UnsafeArena&);
};

_END_GOOGLE_NAMESPACE_

#endif  /* #ifndef _GOOGLE_ARENA_H_ */
