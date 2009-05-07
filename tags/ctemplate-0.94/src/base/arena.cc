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
// "Handles" by Ilan Horn
//
// This approach to arenas overcomes many of the limitations described
// in the "Specialized allocators" section of
//     http://www.pdos.lcs.mit.edu/~dm/c++-new.html
//
// A somewhat similar approach to Gladiator, but for heap-detection, was
// suggested by Ron van der Wal and Scott Meyers at
//     http://www.aristeia.com/BookErrata/M27Comments_frames.html

#include "config.h"
#include <sys/types.h>         // one place uintptr_t might be
#ifdef HAVE_INTTYPES_H
# include <inttypes.h>         // another place uintptr_t might be
#endif
#ifdef HAVE_UNISTD_H           // last place uintptr_t might be
# include <unistd.h>           // also, for getpagesize()
#endif
#include "base/arena.h"

_START_GOOGLE_NAMESPACE_

#if defined(HAVE_U_INT64_T)
typedef u_int64_t uint64;
#elif defined(HAVE_UINT64_T)
typedef uint64_t uint64;
#elif defined(HAVE___INT64)
typedef unsigned __int64 uint64;
#endif

using std::vector;

// We used to only keep track of how much space has been allocated in
// debug mode. Now we track this for optimized builds, as well. If you
// want to play with the old scheme to see if this helps performance,
// change this ARENASET() macro to a NOP. However, NOTE: some
// applications of arenas depend on this space information (exported
// via bytes_allocated()).
#define ARENASET(x) (x)

// Starting with Visual C++ 2005, WinNT.h includes ARRAYSIZE.
#if !defined(_MSC_VER) || _MSC_VER < 1400
#define ARRAYSIZE(a) \
  ((sizeof(a) / sizeof(*(a))) / \
   static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
#endif

// These could be made more elaborate.
#define CHECK(x) assert(x);
#define DCHECK(x) assert(x);
#define CHECK_LT(x, y) assert(x < y);
#define DCHECK_LT(x, y) assert(x < y);
#define CHECK_GE(x, y) assert(x >= y);
#define DCHECK_GE(x, y) assert(x >= y);

// ----------------------------------------------------------------------
// BaseArena::BaseArena()
// BaseArena::~BaseArena()
//    Destroying the arena automatically calls Reset()
// ----------------------------------------------------------------------


BaseArena::BaseArena(char* first, const size_t block_size)
  : remaining_(0),
    first_block_we_own_(first ? 1 : 0),
    block_size_(block_size),
    freestart_(NULL),                   // set for real in Reset()
    last_alloc_(NULL),
    blocks_alloced_(1),
    overflow_blocks_(NULL),
    handle_alignment_(1) {
  assert(block_size > kDefaultAlignment);
  if (first)
    first_blocks_[0].mem = first;
  else
    first_blocks_[0].mem = reinterpret_cast<char*>(malloc(block_size_));
  first_blocks_[0].size = block_size_;

  Reset();
}

BaseArena::~BaseArena() {
  FreeBlocks();
  assert(overflow_blocks_ == NULL);    // FreeBlocks() should do that
  // The first X blocks stay allocated always by default.  Delete them now.
  for ( int i = first_block_we_own_; i < blocks_alloced_; ++i )
    free(first_blocks_[i].mem);
}

// ----------------------------------------------------------------------
// BaseArena::block_count()
//    Only reason this is in .cc file is because it involves STL.
// ----------------------------------------------------------------------

int BaseArena::block_count() const {
  return (blocks_alloced_ +
          (overflow_blocks_ ? static_cast<int>(overflow_blocks_->size()) : 0));
}

// ----------------------------------------------------------------------
// BaseArena::Reset()
//    Clears all the memory an arena is using.
// ----------------------------------------------------------------------

void BaseArena::Reset() {
  FreeBlocks();
  freestart_ = first_blocks_[0].mem;
  remaining_ = first_blocks_[0].size;
  last_alloc_ = NULL;

  ARENASET(status_.bytes_allocated_ = block_size_);

  // We do not know for sure whether or not the first block is aligned,
  // so we fix that right now.
  const int overage = reinterpret_cast<uintptr_t>(freestart_) &
                      (kDefaultAlignment-1);
  if (overage > 0) {
    const int waste = kDefaultAlignment - overage;
    freestart_ += waste;
    remaining_ -= waste;
  }
  freestart_when_empty_ = freestart_;
  assert(!(reinterpret_cast<uintptr_t>(freestart_)&(kDefaultAlignment-1)));
}

// ----------------------------------------------------------------------
// BaseArena::MakeNewBlock()
//    Our sbrk() equivalent.  We always make blocks of the same size
//    (though GetMemory() can also make a new block for really big
//    data.
// ----------------------------------------------------------------------

void BaseArena::MakeNewBlock() {
  AllocatedBlock *block = AllocNewBlock(block_size_);
  freestart_ = block->mem;
  remaining_ = block->size;
}

// -------------------------------------------------------------
// BaseArena::AllocNewBlock()
//    Adds and returns an AllocatedBlock.
//    The returned AllocatedBlock* is valid until the next call
//    to AllocNewBlock or Reset.  (i.e. anything that might
//    affect overflow_blocks_).
// -------------------------------------------------------------

BaseArena::AllocatedBlock*  BaseArena::AllocNewBlock(const size_t block_size) {
  AllocatedBlock *block;
  // Find the next block.
  if ( blocks_alloced_ < ARRAYSIZE(first_blocks_) ) {
    // Use one of the pre-allocated blocks
    block = &first_blocks_[blocks_alloced_++];
  } else {                   // oops, out of space, move to the vector
    if (overflow_blocks_ == NULL) overflow_blocks_ = new vector<AllocatedBlock>;
    // Adds another block to the vector.
    overflow_blocks_->resize(overflow_blocks_->size()+1);
    // block points to the last block of the vector.
    block = &overflow_blocks_->back();
  }

  block->mem = reinterpret_cast<char*>(malloc(block_size));
  block->size = block_size;

  ARENASET(status_.bytes_allocated_ += block_size);

  return block;
}

// ----------------------------------------------------------------------
// BaseArena::IndexToBlock()
//    Index encoding is as follows:
//    For blocks in the first_blocks_ array, we use index of the block in
//    the array.
//    For blocks in the overflow_blocks_ vector, we use the index of the
//    block in iverflow_blocks_, plus the size of the first_blocks_ array.
// ----------------------------------------------------------------------

const BaseArena::AllocatedBlock *BaseArena::IndexToBlock(int index) const {
  if (index < ARRAYSIZE(first_blocks_)) {
    return &first_blocks_[index];
  }
  CHECK(overflow_blocks_ != NULL);
  int index_in_overflow_blocks = index - ARRAYSIZE(first_blocks_);
  CHECK_GE(index_in_overflow_blocks, 0);
  CHECK_LT(static_cast<size_t>(index_in_overflow_blocks),
           overflow_blocks_->size());
  return &(*overflow_blocks_)[index_in_overflow_blocks];
}

// ----------------------------------------------------------------------
// BaseArena::GetMemoryFallback()
//    We take memory out of our pool, aligned on the byte boundary
//    requested.  If we don't have space in our current pool, we
//    allocate a new block (wasting the remaining space in the
//    current block) and give you that.  If your memory needs are
//    too big for a single block, we make a special your-memory-only
//    allocation -- this is equivalent to not using the arena at all.
// ----------------------------------------------------------------------

void* BaseArena::GetMemoryFallback(const size_t size, const int align_as_int) {
  if (0 == size) {
    return NULL;             // stl/stl_alloc.h says this is okay
  }
  // This makes the type-checker happy.
  const size_t align = static_cast<size_t>(align_as_int);

  assert(align_as_int > 0 && 0 == (align & (align - 1))); // must be power of 2

  // If the object is more than a quarter of the block size, allocate
  // it separately to avoid wasting too much space in leftover bytes
  if (block_size_ == 0 || size > block_size_/4) {
    // then it gets its own block in the arena
    assert(align <= kDefaultAlignment);   // because that's what new gives us
    return AllocNewBlock(size)->mem;
  }

  const size_t overage =
    (reinterpret_cast<uintptr_t>(freestart_) & (align-1));
  if (overage) {
    const size_t waste = align - overage;
    freestart_ += waste;
    if (waste < remaining_) {
      remaining_ -= waste;
    } else {
      remaining_ = 0;
    }
  }
  if (size > remaining_) {
    MakeNewBlock();
  }
  remaining_ -= size;
  last_alloc_ = freestart_;
  freestart_ += size;
  assert(0 == (reinterpret_cast<uintptr_t>(last_alloc_) & (align-1)));
  return reinterpret_cast<void*>(last_alloc_);
}

// ----------------------------------------------------------------------
// BaseArena::ReturnMemoryFallback()
// BaseArena::FreeBlocks()
//    Unlike GetMemory(), which does actual work, ReturnMemory() is a
//    no-op: we don't "free" memory until Reset() is called.  We do
//    update some stats, though.  Note we do no checking that the
//    pointer you pass in was actually allocated by us, or that it
//    was allocated for the size you say, so be careful here!
//       FreeBlocks() does the work for Reset(), actually freeing all
//    memory allocated in one fell swoop.
// ----------------------------------------------------------------------

void BaseArena::FreeBlocks() {
  for ( int i = 1; i < blocks_alloced_; ++i ) {  // keep first block alloced
    free(first_blocks_[i].mem);
    first_blocks_[i].mem = NULL;
    first_blocks_[i].size = 0;
  }
  blocks_alloced_ = 1;
  if (overflow_blocks_ != NULL) {
    vector<AllocatedBlock>::iterator it;
    for (it = overflow_blocks_->begin(); it != overflow_blocks_->end(); ++it) {
      free(it->mem);
    }
    delete overflow_blocks_;             // These should be used very rarely
    overflow_blocks_ = NULL;
  }
}

// ----------------------------------------------------------------------
// BaseArena::AdjustLastAlloc()
//    If you realize you didn't want your last alloc to be for
//    the size you asked, after all, you can fix it by calling
//    this.  We'll grow or shrink the last-alloc region if we
//    can (we can always shrink, but we might not be able to
//    grow if you want to grow too big.
//      RETURNS true if we successfully modified the last-alloc
//    region, false if the pointer you passed in wasn't actually
//    the last alloc or if you tried to grow bigger than we could.
// ----------------------------------------------------------------------

bool BaseArena::AdjustLastAlloc(void *last_alloc, const size_t newsize) {
  // It's only legal to call this on the last thing you alloced.
  if (last_alloc == NULL || last_alloc != last_alloc_)  return false;
  // last_alloc_ should never point into a "big" block, w/ size >= block_size_
  assert(freestart_ >= last_alloc_ && freestart_ <= last_alloc_ + block_size_);
  assert(remaining_ >= 0);   // should be: it's a size_t!
  if (newsize > (freestart_ - last_alloc_) + remaining_)
    return false;  // not enough room, even after we get back last_alloc_ space
  const char* old_freestart = freestart_;   // where last alloc used to end
  freestart_ = last_alloc_ + newsize;       // where last alloc ends now
  remaining_ -= (freestart_ - old_freestart); // how much new space we've taken
  return true;
}

// ----------------------------------------------------------------------
// BaseArena::GetMemoryWithHandle()
//    First, memory is allocated using GetMemory, using handle_alignment_.
//    Since using different alignments for different handles would make
//    the handles incompatible (e.g., we could end up with the same handle
//    value referencing two different allocations, the alignment is not passed
//    as an argument to GetMemoryWithHandle, and handle_alignment_ is used
//    automatically for all GetMemoryWithHandle calls.
//    Then we go about building a handle to reference the allocated memory.
//    The block index used for the allocation, along with the offset inside
//    the block, are encoded into the handle as follows:
//      (block_index*block_size)+offset
//    offset is simply the difference between the pointer returned by
//    GetMemory and the starting pointer of the block.
//    The above value is then divided by the alignment. As we know that
//    both offset and the block_size are divisable by the alignment (this is
//    enforced by set_handle_alignment() for block_size, and by GetMemory()
//    for the offset), this does not lose any information, but allows to cram
//    more into the limited space in handle.
//    If the result does not fit into an unsigned 32-bit integer, we
//    have run out of space that the handle can represent, and return
//    an invalid handle. Note that the returned pointer is still usable,
//    but this allocation cannot be referenced by a handle.
// ----------------------------------------------------------------------

void* BaseArena::GetMemoryWithHandle(
    const size_t size, BaseArena::Handle* handle) {
  CHECK(handle != NULL);
  void* p = GetMemory(size, handle_alignment_);
  // Find the index of the block the memory was allocated from. In most
  // cases, this will be the last block, so the following loop will
  // iterate exactly once.
  int block_index;
  const AllocatedBlock* block = NULL;
  for (block_index = block_count() - 1; block_index >= 0; --block_index) {
    block = IndexToBlock(block_index);
    if ((p >= block->mem) && (p < (block->mem + block->size))) {
      break;
    }
  }
  CHECK_GE(block_index, 0); // "Failed to find block that was allocated from"
  CHECK(block != NULL);  // "Failed to find block that was allocated from"
  const uint64 offset = reinterpret_cast<char*>(p) - block->mem;
  DCHECK_LT(offset, block_size_);
  DCHECK((offset % handle_alignment_) == 0);
  DCHECK((block_size_ % handle_alignment_) == 0);
  uint64 handle_value =
      (static_cast<uint64>(block_index) * block_size_ + offset) /
      handle_alignment_;
  if (handle_value >= static_cast<uint64>(0xFFFFFFFF)) {
    // We ran out of space to be able to return a handle, so return an invalid
    // handle.
    handle_value = Handle::kInvalidValue;
  }
  handle->handle_ = static_cast<uint32>(handle_value);
  return p;
}

// ----------------------------------------------------------------------
// BaseArena::HandleToPointer()
//    First, the handle value needs to gain back the alignment factor that
//    was divided out of it by GetMemoryWithHandle. Once this is done, it
//    becomes trivial to extract the block index and offset in the block out
//    of it, and calculate the pointer.
// ----------------------------------------------------------------------

void* BaseArena::HandleToPointer(const Handle& h) const {
  CHECK(h.valid());
  uint64 handle = static_cast<uint64>(h.handle_) * handle_alignment_;
  int block_index = static_cast<int>(handle / block_size_);
  size_t block_offset = static_cast<size_t>(handle % block_size_);
  const AllocatedBlock* block = IndexToBlock(block_index);
  CHECK(block != NULL);
  return reinterpret_cast<void*>(block->mem + block_offset);
}

// ----------------------------------------------------------------------
// UnsafeArena::Realloc()
//    If you decide you want to grow -- or shrink -- a memory region,
//    we'll do it for you here.  Typically this will involve copying
//    the existing memory to somewhere else on the arena that has
//    more space reserved.  But if you're reallocing the last-allocated
//    block, we may be able to accomodate you just by updating a
//    pointer.  In any case, we return a pointer to the new memory
//    location, which may be the same as the pointer you passed in.
//       Here's an example of how you might use Realloc():
//
//    compr_buf = arena->Alloc(uncompr_size);  // get too-much space
//    int compr_size;
//    zlib.Compress(uncompr_buf, uncompr_size, compr_buf, &compr_size);
//    compr_buf = arena->Realloc(compr_buf, uncompr_size, compr_size);
// ----------------------------------------------------------------------

char* UnsafeArena::Realloc(char* s, size_t oldsize, size_t newsize) {
  assert(oldsize >= 0 && newsize >= 0);
  if ( AdjustLastAlloc(s, newsize) )             // in case s was last alloc
    return s;
  if ( newsize <= oldsize ) {
    return s;  // no need to do anything; we're ain't reclaiming any memory!
  }
  char * newstr = Alloc(newsize);
  memcpy(newstr, s, oldsize < newsize ? oldsize : newsize);
  return newstr;
}

_END_GOOGLE_NAMESPACE_
