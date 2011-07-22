// Copyright (c) 2011, Google Inc.
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
//
// Provides the ALIGNED_CHAR_ARRAY macro, which defines a char array
// that is aligned properly to hold an object of a given size for a
// given architecture.

#ifndef ALIGNED_CHAR_ARRAY_H_
#define ALIGNED_CHAR_ARRAY_H_

// Because MSVC and older GCCs require that the argument to their alignment
// construct to be a literal constant integer, we use a template instantiated
// at all the possible powers of two.
#ifndef SWIG
#include <config.h>
template<int alignment, int size> struct AlignType { };
template<int size> struct AlignType<0, size> { typedef char result[size]; };
#if defined(_MSC_VER)
#define BASE_PORT_H_ALIGN_ATTRIBUTE(X) __declspec(align(X))
#define BASE_PORT_H_ALIGN_OF(T) __alignof(T)
#elif defined(HAVE___ATTRIBUTE__)
#define BASE_PORT_H_ALIGN_ATTRIBUTE(X) __attribute__((aligned(X)))
#define BASE_PORT_H_ALIGN_OF(T) __alignof__(T)
#endif

#if defined(BASE_PORT_H_ALIGN_ATTRIBUTE)

#define BASE_PORT_H_ALIGNTYPE_TEMPLATE(X) \
  template<int size> struct AlignType<X, size> { \
    typedef BASE_PORT_H_ALIGN_ATTRIBUTE(X) char result[size]; \
  }

BASE_PORT_H_ALIGNTYPE_TEMPLATE(1);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(2);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(4);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(8);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(16);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(32);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(64);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(128);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(256);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(512);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(1024);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(2048);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(4096);
BASE_PORT_H_ALIGNTYPE_TEMPLATE(8192);
// Any larger and MSVC++ will complain.

#define ALIGNED_CHAR_ARRAY(T, Size) \
  typename AlignType<BASE_PORT_H_ALIGN_OF(T), sizeof(T) * Size>::result

#undef BASE_PORT_H_ALIGNTYPE_TEMPLATE
#undef BASE_PORT_H_ALIGN_ATTRIBUTE

#else  // defined(BASE_PORT_H_ALIGN_ATTRIBUTE)
#define ALIGNED_CHAR_ARRAY you_must_define_ALIGNED_CHAR_ARRAY_for_your_compiler
#endif // defined(BASE_PORT_H_ALIGN_ATTRIBUTE)

#else  // !SWIG

// SWIG can't represent alignment and doesn't care about alignment on data
// members (it works fine without it).
template<typename Size>
struct AlignType { typedef char result[Size]; };
#define ALIGNED_CHAR_ARRAY(T, Size) AlignType<Size * sizeof(T)>::result

#endif // !SWIG

#undef BASE_PORT_H_ALIGNTYPE_TEMPLATE
#undef BASE_PORT_H_ALIGN_ATTRIBUTE

#endif  // ALIGNED_CHAR_ARRAY_H_
