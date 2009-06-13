// Copyright (c) 2007, Google Inc.
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
// Author: Craig Silverstein
//
// Routines for dealing with filesystem paths.  Mostly to make porting
// to windows easier, though it's nice to have an API for this kind of
// thing.

#include "config.h"
#include <string>
#include <ctype.h>       // for isalpha, used on windows
#include <ctemplate/template_pathops.h>

using std::string;

#ifndef PATH_SEP
# ifdef _WIN32
#   define PATH_SEP  '\\'
# else
#   define PATH_SEP  '/'    // assume a unix-like system
# endif
#endif

_START_GOOGLE_NAMESPACE_

// ----------------------------------------------------------------------
// PathJoin()
//    Joins a and b together to form a path.  If 'b' starts with '/'
//    then we just return b, otherwise a + b.  If 'a' does not end in
//    a slash we put a slash in the middle.  Does *not* resolve ..'s
//    and stuff like that, for now.  Not very efficient.
//    Returns a string which is the joining.
// ----------------------------------------------------------------------

const char kCWD[] = { '.', PATH_SEP, '\0' };
const char kRootdir[] = { PATH_SEP, '\0' };

// Windows is bi-slashual: we always write separators using PATH_SEP (\),
// but accept either PATH_SEP or the unix / as a separator on input.
inline bool IsPathSep(char c) {
#ifdef _WIN32
  if (c == '/') return true;
#endif
  return c == PATH_SEP;
}


string PathJoin(const string& a, const string& b) {
  if (b.empty()) return a;                        // degenerate case 1
  if (a.empty()) return b;                        // degenerate case 2
  if (IsAbspath(b)) return b;                     // absolute path
  if (IsDirectory(a)) return a + b;               // 'well-formed' case
  return a + PATH_SEP + b;
}

bool IsAbspath(const string& path) {
#ifdef _WIN32
  if (path.size() > 2 &&          // c:\ is an absolute path on windows
      path[1] == ':' && IsPathSep(path[2]) && isalpha(path[0])) {
    return true;
  }
#endif
  return !path.empty() && IsPathSep(path[0]);
}

bool IsDirectory(const string& path) {
  return !path.empty() && IsPathSep(path[path.size()-1]);
}

void NormalizeDirectory(string* dir) {
  if (dir->empty()) return;   // I guess "" means 'current directory'
  if (!IsPathSep((*dir)[dir->size()-1]))
    *dir += PATH_SEP;
}

string Basename(const string& path) {
  for (const char* p = path.data() + path.size()-1; p >= path.data(); --p) {
    if (IsPathSep(*p))
      return string(p+1, path.data() + path.size() - (p+1));
  }
  return path;   // no path-separator found, so whole string is the basename
}


_END_GOOGLE_NAMESPACE_
