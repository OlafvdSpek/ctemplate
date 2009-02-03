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

#ifndef TEMPLATE_TEMPLATE_STRING_H_
#define TEMPLATE_TEMPLATE_STRING_H_

#include <string.h>      // for memcmp() and size_t
#include <assert.h>
#include <hash_map>
#include <string>
#include <vector>

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google {

// Call this to initialize a StaticTemplateString in your class.  You
// should do this for any TemplateString you pass in to
// TemplateDictionary routines more than once; that is, all the time.
// Using a StaticTemplateString allows you to avoid string copies and
// recomputing the hash_compare.  Use it like like this (at global scope):
// static const kMyVarName = STS_INIT(kMyVarName, "MY_VALUE");
#define STS_INIT(name, str)  STS_INIT_WITH_HASH(name, str, 0)

// Don't use this.  This is used only in auto-generated .varnames.h files.
#define STS_INIT_WITH_HASH(name, str, hash_compare)                             \
   { { str, sizeof(""str"")-1, hash_compare } };                                \
   namespace ctemplate_sts_init { static const google::StaticTemplateStringInitializer name##_init(&name); }

typedef unsigned __int64 TemplateId;

namespace ctemplate {
// Use the low-bit from TemplateId as the "initialized" flag.
const TemplateId kTemplateStringInitializedFlag = 1;

inline bool IsTemplateIdInitialized(TemplateId id) {
  return id & kTemplateStringInitializedFlag;
}

struct TemplateIdHasher {
  size_t operator()(TemplateId id) const {
    // The shift has two effects: it randomizes the "initialized" flag,
    // and slightly improves the randomness of the low bits.  This is
    // slightly useful when size_t is 32 bits, or when using a small
    // hash_compare tables with power-of-2 sizes.
    return static_cast<size_t>(id ^ (id >> 33));
  }

  // Less operator for MSVC's hash_compare containers.
  bool operator()(TemplateId a, TemplateId b) const {
    return a < b;
  }
  // These two public members are required by msvc.  4 and 8 are defaults.
  static const size_t bucket_size = 4;
  static const size_t min_buckets = 8;
};

}  // namespace

// You should declare static template strings in the global scope, as
//    static const StaticTemplateString <var> = STS_INIT(<var>, <value>);
struct StaticTemplateString {
  // Do not define a constructor!  We use only brace-initialization,
  // so the data is constructed at static-initialization time.
  // Anything you want to put in a constructor, put in
  // StaticTemplateStringInitializer instead.

  // These members shouldn't be accessed directly, except in the
  // internals of the template code.  They are public because that is
  // the only way we can brace-initialize them.  NOTE: MSVC (at least
  // up to 8.0) has a bug where it ignores 'mutable' when it's buried
  // in an internal struct.  To fix that, we have to make this whole
  // internal struct mutable.  We only do this on MSVC, so on other
  // compilers we get the full constness we want.
#ifdef _MSC_VER
  mutable
#endif
  struct {
    const char* ptr_;
    size_t length_;
    mutable TemplateId id_;  // sometimes lazily-initialized.
  } do_not_use_directly_;

  // Hasher is not safe to use before StaticTemplateStringInitializer
  // does its job, but it could happen even if you use the STS_INIT
  // macro.  There is no reliable static initialization ordering for
  // objects in different .cc files, and such objects may reference a
  // StaticTemplateString before the corresponding
  // StaticTemplateStringInitializer sets the id.
  struct Hasher {
    size_t operator()(const StaticTemplateString& sts) const {
      TemplateId id = sts.do_not_use_directly_.id_;
      assert(ctemplate::IsTemplateIdInitialized(id));
      return hasher(id);
    }
    const ctemplate::TemplateIdHasher hasher;

    // Less operator for MSVC's hash_compare containers.
    bool operator()(const StaticTemplateString& a,
                    const StaticTemplateString& b) const {
      const TemplateId id_a = a.do_not_use_directly_.id_;
      const TemplateId id_b = b.do_not_use_directly_.id_;
      assert(ctemplate::IsTemplateIdInitialized(id_a));
      assert(ctemplate::IsTemplateIdInitialized(id_b));
      return hasher(id_a, id_b);
    }
    // These two public members are required by msvc.  4 and 8 are defaults.
    static const size_t bucket_size = 4;
    static const size_t min_buckets = 8;
  };

  // The following conversion operators are here so we don't break builds
  // of people relying on auto-generated variables having const char* type.
  // TODO(jcrim): remove them after existing code is updated.
  operator const char * () const {
    return do_not_use_directly_.ptr_;
  }

  // Allows comparisons of StaticTemplateString objects as if they were
  // strings.  This is useful for STL.
  bool operator==(const StaticTemplateString& x) const {
    return (do_not_use_directly_.length_ == x.do_not_use_directly_.length_ &&
            (do_not_use_directly_.ptr_ == x.do_not_use_directly_.ptr_ ||
             memcmp(do_not_use_directly_.ptr_, x.do_not_use_directly_.ptr_,
                    do_not_use_directly_.length_) == 0));
  }

  bool operator!=(const StaticTemplateString& x) const {
    return !(*this == x);
  }
};

// TODO(csilvers): declare this class to be a POD, which may make
//                 some operations on it more efficient.

// We set up as much of StaticTemplateString as we can at
// static-initialization time (using brace-initialization), but some
// things can't be set up then.  This class is for those things; it
// runs at dynamic-initialization time.  If you add logic here, only
// do so as an optimization: this may be called rather late (though
// before main), so other code should not depend on this being called
// before them.
class CTEMPLATE_DLL_DECL StaticTemplateStringInitializer {
 public:
  // This constructor operates on a const StaticTemplateString - we should
  // only change those things that are mutable.
  explicit StaticTemplateStringInitializer(const StaticTemplateString* sts);
 private:   // disallow default copy/assign constructors
  StaticTemplateStringInitializer(const StaticTemplateStringInitializer&);
  void operator=(const StaticTemplateStringInitializer&);
};

// The hash_compare value is precomputed offline, and checked at runtime in
// debug builds (i.e.  as if it were auto-generated via
// make_tpl_varnames_h). Since this value is defined in a header, it
// is declared non-static and we rely on the linker to handle the
// POD-with-internal-linkage magic.
const StaticTemplateString kStsEmpty =
    STS_INIT_WITH_HASH(kStsEmpty, "", 1457976849674613049ULL);

// Most methods of TemplateDictionary take a TemplateString rather than a
// C++ string.  This is for efficiency: it can avoid extra string copies.
// For any argument that takes a TemplateString, you can pass in any of:
//    * A C++ string
//    * A char*
//    * TemplateString(char*, length)
// The last of these is the most efficient, though it requires more work
// on the call site (you have to create the TemplateString explicitly).
class CTEMPLATE_DLL_DECL TemplateString {
 public:
  TemplateString(const char* s)
      : ptr_(s ? s : ""), length_(strlen(ptr_)), is_immutable_(false), id_(0) {
  }
  TemplateString(const std::string& s)
      : ptr_(s.data()), length_(s.size()), is_immutable_(false), id_(0) {
  }
  TemplateString(const char* s, size_t slen)
      : ptr_(s), length_(slen), is_immutable_(false), id_(0) {
  }
  TemplateString(const TemplateString& s)
      : ptr_(s.ptr_), length_(s.length_),
        is_immutable_(s.is_immutable_), id_(s.id_) {
  }
  TemplateString(const StaticTemplateString& s)
      : ptr_(s.do_not_use_directly_.ptr_),
        length_(s.do_not_use_directly_.length_),
        is_immutable_(true), id_(s.do_not_use_directly_.id_) {
  }

  // STL requires this to be public for hash_map, though I'd rather not.
  bool operator==(const TemplateString& x) const {
    return (GetGlobalId() == x.GetGlobalId());
  }

  // Returns the global id, computing it for the first time if
  // necessary.  Note that since this is a const method, we don't
  // store the computed value in id_, even if id_ is 0.
  TemplateId GetGlobalId() const;

 protected:
  void CacheGlobalId() {
    id_ = GetGlobalId();
  }

 private:
  // Only TemplateDictionaries and template expansion code can read these.
  friend class TemplateDictionary;
  friend class StaticTemplateStringInitializer;  // for AddToGlobalIdToNameMap
  friend class TemplateStringTest;

  TemplateString();    // no empty constructor allowed
  TemplateString(const char* s, size_t slen, bool is_immutable, TemplateId id)
      : ptr_(s), length_(slen), is_immutable_(is_immutable), id_(id) {
  }

  bool is_immutable() const { return is_immutable_; }

  // Adds this TemplateString to the map from global-id to name.
  void AddToGlobalIdToNameMap();

  // Does the reverse map from TemplateId to TemplateString contents.
  // Returns a TemplateString(kStsEmpty) if id isn't found.  Note that
  // the TemplateString returned is not necessarily NUL terminated.
  static TemplateString IdToString(TemplateId id);

  const char* ptr_;
  size_t length_;
  // Do we need to manage memory for this string?
  bool is_immutable_;
  // Id for hash_compare lookups. If 0, we don't have one and it should be
  // computed as-needed.
  TemplateId id_;
};

}

#endif  // TEMPLATE_TEMPLATE_STRING_H_
