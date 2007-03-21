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
// Author: Frank H. Jernigan

#include "config.h"
#include <stdlib.h>
#include <unistd.h>              // for access()
#include <time.h>                // for time_t
#include <sys/stat.h>            // for stat()
#include <vector>
#include <string>
#include <iostream>              // for cerr
#include <algorithm>             // for find()
#include <google/template_namelist.h>
#include <google/template.h>     // for Strip, GetTemplate(), etc.

_START_GOOGLE_NAMESPACE_

using std::string;
using std::vector;

/*static*/ vector<string> *TemplateNamelist::namelist_ = NULL;
/*static*/ vector<string> *TemplateNamelist::missing_list_ = NULL;
/*static*/ vector<string> *TemplateNamelist::bad_syntax_list_ = NULL;

// PathJoin
//    Joins a and b together to form a path.  If 'b' starts with '/'
//    then we just return b, otherwise a + b.  If 'a' does not end in
//    a slash we put a slash in the middle.  Does *not* resolve ..'s
//    and stuff like that, for now.  Not very efficient.
//    Returns a string which is the joining.

static string PathJoin(const string& a, const string& b) {
  if (b.empty()) return a;                    // degenerate case 1
  if (a.empty()) return b;                    // degenerate case 2
  if (b[0] == '/') return b;                  // absolute path
  if (a[a.length()-1] == '/') return a + b;   // 'well-formed' case
  return a + '/' + b;
}


// constructor
//   Make sure there is a namelist_ and then push the name onto it
TemplateNamelist::TemplateNamelist(const string& name) {
  if (!namelist_) {
    namelist_ = new vector<string>;
  }

  // TODO namelist_ should be a hash_map
  if (find(namelist_->begin(), namelist_->end(), name) == namelist_->end()) {
    namelist_->push_back(name);
  }
}

// GetList
// Make sure there is a namelist_ and then return a reference to it
const vector<string>& TemplateNamelist::GetList() {
  if (!namelist_) {
    namelist_ = new vector<string>;
  }
  return *namelist_;
}

// GetMissingList
//   On the first invocation, it creates a new missing list and sets
//   refresh to true.
//   If refresh is true, whether from being passed to the function
//   or being set when the list is created the first time, it iterates
//   through the complete list of registered template files
//   and adds to the list any that are missing
//   On subsequent calls, if refresh is false it merely returns the
//   list created in the prior call that refreshed the list.
const vector<string>& TemplateNamelist::GetMissingList(bool refresh) {
  if (!missing_list_) {
    missing_list_ = new vector<string>;
    refresh = true; // always refresh the first time
  }

  if (refresh) {
    const string& root_dir = Template::template_root_directory();

    // Make sure the root directory ends with a '/' (which is also required
    // by the method SetTemplateRootDirectory anyway)
    assert(root_dir.at(root_dir.length()-1) == '/');

    const vector<string>& the_list = TemplateNamelist::GetList();
    missing_list_->clear();

    for (vector<string>::const_iterator iter = the_list.begin();
         iter != the_list.end();
         ++iter) {
      // Only prepend root_dir if *iter isn't an absolute path:
      string path = PathJoin(root_dir, *iter);
      if (access(path.c_str(), R_OK) != 0) {
        missing_list_->push_back(*iter);
        std::cerr << "ERROR: Template file missing: " << path << std::endl;
      }
    }
  }
  return *missing_list_;
}

// GetBadSyntaxList
//   On the first invocation, it creates a new "bad syntax" list and
//   sets refresh to true.
//   If refresh is true, whether from being passed to the function
//   or being set when the list is created the first time, it
//   iterates through the complete list of registered template files
//   and adds to the list any that cannot be loaded. In the process, it
//   calls GetMissingList, refreshing it. It does not include any
//   files in the bad syntax list which are in the missing list.
//   On subsequent calls, if refresh is false it merely returns the
//   list created in the prior call that refreshed the list.
const vector<string>& TemplateNamelist::GetBadSyntaxList(bool refresh,
                                                         Strip strip) {
  if (!bad_syntax_list_) {
    bad_syntax_list_ = new vector<string>;
    refresh = true; // always refresh the first time
  }

  if (refresh) {
    const vector<string>& the_list = TemplateNamelist::GetList();

    bad_syntax_list_->clear();

    const vector<string>& missing_list = GetMissingList(true);
    for (vector<string>::const_iterator iter = the_list.begin();
         iter != the_list.end();
         ++iter) {
      Template *tpl = Template::GetTemplate((*iter), strip);
      if (!tpl) {
        vector<string>::const_iterator pos =
          find(missing_list.begin(), missing_list.end(), (*iter));
        // If it's not in the missing list, then we're here because it caused
        // an error during parsing
        if (pos == missing_list.end()) {
          bad_syntax_list_->push_back(*iter);
          std::cerr << "ERROR loading template: " << (*iter) << std::endl;
        }
      }
    }
  }
  return *bad_syntax_list_;
}

// Look at all the existing template files, and get their lastmod time via stat()
time_t TemplateNamelist::GetLastmodTime() {
  time_t retval = -1;

  const string& root_dir = Template::template_root_directory();
  assert(root_dir.at(root_dir.length()-1) == '/');
  const vector<string>& the_list = TemplateNamelist::GetList();
  for (vector<string>::const_iterator iter = the_list.begin();
       iter != the_list.end();
       ++iter) {
    // Only prepend root_dir if *iter isn't an absolute path:
    string path = PathJoin(root_dir, *iter);
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0)       // ignore files we can't find
      continue;
    retval = retval > statbuf.st_mtime ? retval : statbuf.st_mtime;
  }
  return retval;
}

// AllDoExist
bool TemplateNamelist::AllDoExist() {
  // AllDoExist always refreshes the list, hence the "true"
  const vector<string>& missing_list = TemplateNamelist::GetMissingList(true);
  return missing_list.empty();
}

// IsAllSyntaxOkay
bool TemplateNamelist::IsAllSyntaxOkay(Strip strip) {
  // IsAllSyntaxOkay always refreshes the list, hence the "true"
  const vector<string>& bad_syntax_list =
    TemplateNamelist::GetBadSyntaxList(true, strip);
  return bad_syntax_list.empty();
}

_END_GOOGLE_NAMESPACE_
