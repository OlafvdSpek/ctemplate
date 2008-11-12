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
#include <string>

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google {

// Most methods of TemplateDictionary take a TemplateString rather than a
// C++ string.
// This is for efficiency: it can avoid extra string copies.
// For any argument that takes a TemplateString, you can pass in any of:
//    * A C++ string
//    * A char*
//    * TemplateString(char*, length)
// The last of these is the most efficient, though it requires more work
// on the call site (you have to create the TemplateString explicitly).
class CTEMPLATE_DLL_DECL TemplateString {
 private:
  const char*  ptr_;
  size_t       length_;

 public:
  TemplateString(const char* s) : ptr_(s ? s : ""), length_(strlen(ptr_)) {}
  TemplateString(const std::string& s) : ptr_(s.data()), length_(s.size()) {}
  TemplateString(const char* s, size_t slen) : ptr_(s), length_(slen) {}
  TemplateString(const TemplateString& s) : ptr_(s.ptr_), length_(s.length_) {}

  // STL requires this to be public for hash_map, though I'd rather not.
  bool operator==(const TemplateString& x) const {
    return (length_ == x.length_ && memcmp(ptr_, x.ptr_, length_) == 0);
  }

 private:
  TemplateString();    // no empty constructor allowed
  void operator=(const TemplateString&);   // or assignment

  bool operator<(const TemplateString& x) const {
    size_t min_length = length_ < x.length_ ? length_ : x.length_;
    const int r = memcmp(ptr_, x.ptr_, min_length);
    return ((r < 0) || ((r == 0) && length_ < x.length_));
  }

  struct Hash {
    size_t operator()(const TemplateString& s) const {
      // This is a terrible hash_compare-function, but it's fast.
      size_t r = 0;
      for (size_t i = 0; i < s.length_; i++)
        r = 5 * r + s.ptr_[i];
      return r;
    }
    // Less operator for MSVC's hash_compare containers.
    bool operator()(const TemplateString& a, const TemplateString& b) const {
      return a < b;
    }
    // These two public members are required by msvc.  4 and 8 are defaults.
    static const size_t bucket_size = 4;
    static const size_t min_buckets = 8;
  };

  // Only TemplateDictionaries can read these.
  friend class TemplateDictionary;
};

}

#endif  // TEMPLATE_TEMPLATE_STRING_H_
