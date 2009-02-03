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
// Author: Craig Silverstein
//
// In addition to a TemplateDictionary, there is also a PerExpandData
// dictionary.  This dictionary holds information that applies to one
// call to Expand, such as whether to annotate the template expansion
// output.  A template dictionary is associated with a template (.tpl)
// file; a per-expand dictionary is associated to a particular call to
// Expand() in a .cc file.
//
// For (many) more details, see the doc/ directory.

#ifndef TEMPLATE_PER_EXPAND_DATA_H_
#define TEMPLATE_PER_EXPAND_DATA_H_

#include <stdlib.h>   // for NULL
#include <string.h>   // for strcmp
#include <sys/types.h>
#include <hash_map>

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google {

namespace template_modifiers {
class TemplateModifier;
}

namespace ctemplate {

class CTEMPLATE_DLL_DECL PerExpandData {
 public:
  PerExpandData() : annotate_path_(NULL), expand_modifier_(NULL) { }

  // Indicate that annotations should be inserted during template expansion.
  // template_path_start - the start of a template path.  When
  // printing the filename for template-includes, anything before and
  // including template_path_start is elided.  This can make the
  // output less dependent on filesystem location for template files.
  void SetAnnotateOutput(const char* template_path_start) {
    annotate_path_ = template_path_start;
  }

  // Whether to annotate the expanded output.
  bool annotate() const { return annotate_path_ != NULL; }

  // The annotate-path; undefined if annotate() != true
  const char* annotate_path() const { return annotate_path_; }

  // This is a TemplateModifier to be applied to all templates
  // expanded via this call to Expand().  That is, this modifier is
  // applies to the template (.tpl) file we expand, as well as
  // sub-templates that are expanded due to {{>INCLUDE}} directives.
  // Caller is responsible for ensuring that modifier exists for the
  // lifetime of this object.
  void SetTemplateExpansionModifier(
      const template_modifiers::TemplateModifier* modifier) {
    expand_modifier_ = modifier;
  }

  const template_modifiers::TemplateModifier* template_expansion_modifier()
      const {
    return expand_modifier_;
  }

  // Store data in this structure, to be used by template modifiers
  // (see template_modifiers.h).  Call with value set to NULL to clear
  // any value previously set.  Caller is responsible for ensuring key
  // and value point to valid data for the lifetime of this object.
  void InsertForModifiers(const char* key, const void* value) {
    map_[key] = value;
  }

  // Retrieve data specific to this Expand call. Returns NULL if key
  // is not found.  This should only be used by template modifiers.
  const void* LookupForModifiers(const char* key) const {
    const DataMap::const_iterator it = map_.find(key);
    return it == map_.end() ? NULL : it->second;
  }

  // Same as Lookup, but casts the result to a c string.
  const char* LookupForModifiersAsString(const char* key) const {
    return static_cast<const char*>(LookupForModifiers(key));
  }

 private:
#ifdef _MSC_VER
  struct DataHash {
    size_t operator()(const char* s1) const {
      return stdext::hash_compare<const char*>()(s1);
    }
    bool operator()(const char* s1, const char* s2) const {  // less-than
      return (s2 == 0 ? false : s1 == 0 ? true : strcmp(s1, s2) < 0);
    }
    // These two public members are required by msvc.  4 and 8 are defaults.
    static const size_t bucket_size = 4;
    static const size_t min_buckets = 8;
  };

  typedef stdext::hash_map<const char*, const void*, DataHash> DataMap;
#else
  struct DataEq {
    bool operator()(const char* s1, const char* s2) const {
      return ((s1 == 0 && s2 == 0) ||
              (s1 && s2 && *s1 == *s2 && strcmp(s1, s2) == 0));
    }
  };
  typedef stdext::hash_map<const char*, const void*, stdext::hash_compare<const char*>, DataEq>
    DataMap;
#endif

  const char* annotate_path_;
  const template_modifiers::TemplateModifier* expand_modifier_;
  DataMap map_;

  PerExpandData(const PerExpandData&);    // disallow evil copy constructor
  void operator=(const PerExpandData&);   // disallow evil operator=
};

}  // namespace ctemplate

}

#endif  // #ifndef TEMPLATE_PER_EXPAND_DATA_H_
