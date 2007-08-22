/* Copyright (c) 2007, Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Craig Silverstein
 *
 * These are some portability typedefs and defines to make it a bit
 * easier to compile this code under VC++.
 *
 * Several of these are taken from glib:
 *    http://developer.gnome.org/doc/API/glib/glib-windows-compatability-functions.html
 */

#ifndef CTEMPLATE_WINDOWS_PORT_H__
#define CTEMPLATE_WINDOWS_PORT_H__

#include "config.h"

#ifdef WIN32

#define WIN32_LEAN_AND_MEAN  /* We always want minimal includes */
#include <windows.h>
#include <io.h>              /* because we so often use open/close/etc */
#include <direct.h>          /* for _getcwd() */
#include <stdio.h>           /* read in vsnprintf decl. before redifining it */
#include <stdarg.h>          /* template_dictionary.cc uses va_copy */
#include <string.h>          /* for _strnicmp */
#include <string>
#include <vector>

// 4018: signed/unsigned mismatch is common (and ok for signed_i < unsigned_i)
// 4244: otherwise we get problems when substracting two size_t's to an int
// 4251: it's complaining about a private struct I've chosen not to dllexport
// 4715: for some reason VC++ stopped realizing you can't return after abort()
// 4996: Yes, we're ok using "unsafe" functions like fopen() and strerror()
#pragma warning(disable:4018 4244 4251 4715 4996)

// file I/O
#define PATH_MAX 1024
#define access  _access
#define getcwd  _getcwd
#define open    _open
#define read    _read
#define write   _write
#define lseek   _lseek
#define close   _close
#define popen   _popen
#define pclose  _pclose
#define R_OK    04           // read-only (for access())

// Not quite as lightweight as a hard-link, but more than good enough for us.
#define link(oldpath, newpath)  CopyFileA(oldpath, newpath, false)

#define strncasecmp  _strnicmp

// In windows-land, hash<> is called hash_compare<> (from xhash.h)
#define hash  hash_compare

// Sleep is in ms, on windows
#define sleep(secs)  Sleep((secs) * 1000)

// We can't just use _vsnprintf and _snprintf as drop-in-replacements,
// because they don't always NUL-terminate. :-(  We also can't use the
// name vsnprintf, since windows defines that (but not snprintf (!)).
extern CTEMPLATE_DLL_DECL int snprintf(char *str, size_t size,
                                       const char *format, ...);
extern int CTEMPLATE_DLL_DECL safe_vsnprintf(char *str, size_t size,
                                             const char *format, va_list ap);
#define vsnprintf(str, size, format, ap)  safe_vsnprintf(str, size, format, ap)
#define va_copy(dst, src)  (dst) = (src)

extern CTEMPLATE_DLL_DECL std::string TmpFile(const char* basename);

// Windows doesn't support specifying the number of buckets as a
// hash_map constructor arg, so we leave this blank.
#define CTEMPLATE_SMALL_HASHTABLE

#define DEFAULT_TEMPLATE_ROOTDIR  ".."


// These are functions we have to override because they're O/S-specific
void CTEMPLATE_DLL_DECL CleanTestDir(const std::string& dirname);
void CTEMPLATE_DLL_DECL GetNamelist(const char* testdata_dir,
                                    std::vector<std::string>* namelist);

#endif  /* WIN32 */

#endif  /* CTEMPLATE_WINDOWS_PORT_H__ */
