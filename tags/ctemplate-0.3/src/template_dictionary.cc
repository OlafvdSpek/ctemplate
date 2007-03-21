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
// Author: Craig Silverstein
//
// Based on the 'old' TemplateDictionary by Frank Jernigan.

#include "config.h"

// Needed for pthread_rwlock_*.  If it causes problems, you could take
// it out, but then you'd have to unset HAVE_RWLOCK (at least on linux).
#define _XOPEN_SOURCE 500       // needed to get the rwlock calls

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>             // for varargs with StringAppendV
#if defined(HAVE_PTHREAD) && !defined(NO_THREADS)
# include <pthread.h>
#endif
#include <string>
#include <algorithm>            // for sort
#include <vector>
#include "google/ctemplate/hash_map.h"
#include "google/template_dictionary.h"
#include "base/arena.h"

_START_GOOGLE_NAMESPACE_

using std::vector;
using std::string;
using std::pair;
using HASH_NAMESPACE::hash_map;

#define SAFE_PTHREAD(fncall)  do { if ((fncall) != 0) abort(); } while (0)

#if defined(HAVE_PTHREAD) && defined(HAVE_RWLOCK) && !defined(NO_THREADS)
# define STATIC_ROLOCK  SAFE_PTHREAD(pthread_rwlock_rdlock(&g_static_mutex))
# define STATIC_RWLOCK  SAFE_PTHREAD(pthread_rwlock_wrlock(&g_static_mutex))
# define STATIC_UNLOCK  SAFE_PTHREAD(pthread_rwlock_unlock(&g_static_mutex))
// PTHREAD_RWLOCK_INITIALIZER isn't defined on OS X, at least as of 6/1/06.
// We use a static class instance to force initialization at program-start time.
static pthread_rwlock_t g_static_mutex;
namespace {   // keep this class name from polluting the global namespace
struct StaticMutexInit {
  StaticMutexInit() { SAFE_PTHREAD(pthread_rwlock_init(&g_static_mutex, NULL)); }
};
static StaticMutexInit g_static_mutex_initializer;  // constructs early
}
#elif defined(HAVE_PTHREAD) && !defined(NO_THREADS)
# define STATIC_ROLOCK  SAFE_PTHREAD(pthread_mutex_lock(&g_static_mutex))
# define STATIC_RWLOCK  SAFE_PTHREAD(pthread_mutex_lock(&g_static_mutex))
# define STATIC_UNLOCK  SAFE_PTHREAD(pthread_mutex_unlock(&g_static_mutex))
  static pthread_mutex_t g_static_mutex = PTHREAD_MUTEX_INITIALIZER;
#else
# define STATIC_ROLOCK
# define STATIC_RWLOCK
# define STATIC_UNLOCK
#endif

/*static*/ TemplateDictionary::GlobalDict* TemplateDictionary::global_dict_
  = NULL;
/*static*/ TemplateDictionary::HtmlEscape TemplateDictionary::html_escape;
/*static*/ TemplateDictionary::XmlEscape TemplateDictionary::xml_escape;
/*static*/ TemplateDictionary::JavascriptEscape TemplateDictionary::javascript_escape;
/*static*/ TemplateDictionary::JsonEscape TemplateDictionary::json_escape;


// ----------------------------------------------------------------------
// TemplateDictionary::TemplateDictionary()
// TemplateDictionary::~TemplateDictionary()
//    The only tricky thing is that we make sure the static vars are
//    set up properly.  This must be done at each construct time,
//    because it's the responsibility of the first dictionary created
//    in the program to set up the globals, and that could be us.
//       The UnsafeArena() argument is how big to make each arena
//    block.  Too big and space is wasted.  Too small and we spend
//    a lot of time allocating new arena blocks.  32k seems right.
// ----------------------------------------------------------------------

// caller must hold g_static_mutex
TemplateDictionary::GlobalDict* TemplateDictionary::SetupGlobalDictUnlocked() {
  TemplateDictionary::GlobalDict* retval =
    new TemplateDictionary::GlobalDict(3);
  // Initialize the built-ins
  static const char* const kBI_SPACE = "BI_SPACE";
  static const char* const kBI_NEWLINE = "BI_NEWLINE";
  (*retval)[kBI_SPACE] = " ";
  (*retval)[kBI_NEWLINE] = "\n";
  return retval;
}

TemplateDictionary::TemplateDictionary(const string& name, UnsafeArena* arena)
    : name_(name),
      arena_(arena ? arena : new UnsafeArena(32768)),
      should_delete_arena_(arena ? false : true),   // true if we called new
      // dicts are initialized to have 3 buckets (that's small).
      // TODO(csilvers): use the arena for these instead
      variable_dict_(new VariableDict(3)),
      section_dict_(new SectionDict(3)),
      include_dict_(new IncludeDict(3)),
      template_global_dict_(new VariableDict(3)),
      template_global_dict_owner_(true),
      parent_dict_(NULL),
      filename_(NULL),
      template_path_start_for_annotations_(NULL) {
  STATIC_RWLOCK;
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
  STATIC_UNLOCK;
}

TemplateDictionary::TemplateDictionary(const string& name, UnsafeArena* arena,
                                       TemplateDictionary* parent_dict,
                                       VariableDict* template_global_dict)
    : name_(name),
      arena_(arena), should_delete_arena_(false),  // parents own it
      // dicts are initialized to have 3 buckets (that's small).
      // TODO(csilvers): use the arena for these instead
      variable_dict_(new VariableDict(3)),
      section_dict_(new SectionDict(3)),
      include_dict_(new IncludeDict(3)),
      template_global_dict_(template_global_dict),
      template_global_dict_owner_(false),
      parent_dict_(parent_dict),
      filename_(NULL),
      template_path_start_for_annotations_(NULL) {
  STATIC_RWLOCK;
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
  STATIC_UNLOCK;
}

TemplateDictionary::~TemplateDictionary() {
  // First delete all the sub-dictionaries held in the section/template dicts
  for (SectionDict::iterator it = section_dict_->begin();
       it != section_dict_->end();  ++it) {
    for (DictVector::iterator it2 = it->second->begin();
         it2 != it->second->end(); ++it2) {
      delete *it2;               // delete each dictionary in the vector
    }
    delete it->second;           // delete the vector itself
  }
  for (IncludeDict::iterator it = include_dict_->begin();
       it != include_dict_->end();  ++it) {
    for (DictVector::iterator it2 = it->second->begin();
         it2 != it->second->end(); ++it2) {
      delete *it2;               // delete each dictionary in the vector
    }
    delete it->second;
  }

  // Then delete the three dictionaries this instance points to directly
  delete variable_dict_;
  delete section_dict_;
  delete include_dict_;

  // do not use whether there's a parent to determine if you own a template
  // dict. They are shared across included templates, even though included
  // templates have no parent dict.
  if (template_global_dict_owner_) {
    delete template_global_dict_;
  }
  if (should_delete_arena_)
    delete arena_;
}

// ----------------------------------------------------------------------
// TemplateDictionary::StringAppendV()
//    Does an snprintf to a string.  Idea is to grow string as needed.
//    Writes to space if possible -- caller must ensure space has
//    size at least 1024 -- and if not allocates a buffer of its
//    own which the caller must free.  Sets out to the buffer written
//    to (space or something else).  Returns the number of bytes
//    written into out.
// ----------------------------------------------------------------------

int TemplateDictionary::StringAppendV(char* space, char** out,
                                      const char* format, va_list ap) {
  // It's possible for methods that use a va_list to invalidate
  // the data in it upon use.  The fix is to make a copy
  // of the structure before using it and use that copy instead.
  va_list backup_ap;
  va_copy(backup_ap, ap);
  int result = vsnprintf(space, sizeof(space), format, backup_ap);
  va_end(backup_ap);

  if ((result >= 0) && (result < sizeof(space))) {
    *out = space;
    return result;  // It fit
  }

  // Repeatedly increase buffer size until it fits
  int length = 1024;    // sizeof(space)
  while (true) {
    if (result < 0) {
      // Older snprintf() behavior. :-(  Just try doubling the buffer size
      length *= 2;
    } else {
      // We need exactly "result+1" characters
      length = result+1;
    }
    char* buf = new char[length];

    // Restore the va_list before we use it again
    va_copy(backup_ap, ap);
    result = vsnprintf(buf, length, format, backup_ap);
    va_end(backup_ap);

    if ((result >= 0) && (result < length)) {
      *out = buf;
      return result;
    }
    delete[] buf;
  }
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetValue()
// TemplateDictionary::SetIntValue()
// TemplateDictionary::SetFormattedValue()
// TemplateDictionary::SetEscapedValue()
// TemplateDictionary::SetEscapedFormattedValue()
//    The functions to set the value of a variable.  For each,
//    I first define the char*+length version.  Then, after those
//    five definitions, I define a zillion alternate versions:
//    strings, char*s, etc.  The only non-obvious thing about
//    each function is I make sure to copy both key and value to
//    the arena, so we have our own, persistent copy of them.
// ----------------------------------------------------------------------

void TemplateDictionary::SetValue(const TemplateString variable,
                                  const TemplateString value) {
  (*variable_dict_)[Memdup(variable)] = Memdup(value);
}

void TemplateDictionary::SetIntValue(const TemplateString variable, int value) {
  char buffer[64];   // big enough for any int
  int valuelen = snprintf(buffer, sizeof(buffer), "%d", value);
  (*variable_dict_)[Memdup(variable)] = Memdup(buffer, valuelen);
}

void TemplateDictionary::SetFormattedValue(const TemplateString variable,
                                           const char* format, ...) {
  char *scratch, *buffer;

  scratch = arena_->Alloc(1024);  // StringAppendV requires >=1024 bytes
  va_list ap;
  va_start(ap, format);
  const int buflen = StringAppendV(scratch, &buffer, format, ap);
  va_end(ap);

  // If it fit into scratch, great, otherwise we need to copy into arena
  if (buffer == scratch) {
    scratch = arena_->Shrink(scratch, buflen+1);   // from 1024 to |value+\0|
    (*variable_dict_)[Memdup(variable)] = scratch;
  } else {
    arena_->Shrink(scratch, 0);   // reclaim arena space we didn't use
    (*variable_dict_)[Memdup(variable)] = Memdup(buffer, buflen);
    delete[] buffer;
  }
}

// SetEscapedValue()
// SetEscapedFormattedValue()
//    Defined in template_dictionary.h, as all good templatized
//    methods need to be.

// ----------------------------------------------------------------------
// TemplateDictionary::SetTemplateGlobalValue()
//    Sets a value in the template-global dict.  Unlike normal
//    variable lookups, these persist across sub-includes.
// ----------------------------------------------------------------------

void TemplateDictionary::SetTemplateGlobalValue(const TemplateString variable,
                                                const TemplateString value) {
  assert(template_global_dict_ != NULL);
  if (template_global_dict_) {
    (*template_global_dict_)[Memdup(variable)] = Memdup(value);
  }
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetGlobalValue()
//    Sets a value in the global dict.  Note this is a static method.
// ----------------------------------------------------------------------

/*static*/ void TemplateDictionary::SetGlobalValue(const TemplateString variable,
                                                   const TemplateString value) {
  // We can't use memdup here, since we're a static method.  We do a strdup,
  // which is fine, since global_dict_ lives the entire program anyway.
  char* variable_copy = new char[variable.length_ + 1];
  memcpy(variable_copy, variable.ptr_, variable.length_);
  variable_copy[variable.length_] = '\0';
  char* value_copy = new char[value.length_ + 1];
  memcpy(value_copy, value.ptr_, value.length_);
  value_copy[value.length_] = '\0';

  STATIC_RWLOCK;
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
  (*global_dict_)[variable_copy] = value_copy;
  STATIC_UNLOCK;
}

// ----------------------------------------------------------------------
// TemplateDictionary::AddSectionDictionary()
// TemplateDictionary::ShowSection()
//    The new dictionary starts out empty, with us as the parent.
//    It shares our arena.  The name is constructed out of our
//    name plus the section name.  ShowSection() is the equivalent
//    to AddSectionDictionary(empty_dict).
// ----------------------------------------------------------------------

TemplateDictionary* TemplateDictionary::AddSectionDictionary(
    const TemplateString section_name) {
  DictVector* dicts = NULL;
  // TODO(csilvers): make a string instead, or key on char*/length pairs
  if (section_dict_->find(section_name.ptr_) == section_dict_->end()) {
    dicts = new DictVector;
    // Since most lists will remain under 8 or 16 entries but will frequently
    // be more than four, this prevents copying from 1->2->4->8.
    dicts->reserve(8);
    (*section_dict_)[Memdup(section_name)] = dicts;
  } else {
    dicts = (*section_dict_)[section_name.ptr_];
  }
  assert(dicts != NULL);
  char dictsize[64];
  snprintf(dictsize, sizeof(dictsize), "%"PRIuS, dicts->size() + 1);
  string newname(name_ + "/" + section_name.ptr_ + "#" + dictsize);
  TemplateDictionary* retval = new TemplateDictionary(newname, arena_, this,
                                                      template_global_dict_);
  dicts->push_back(retval);
  return retval;
}

void TemplateDictionary::ShowSection(const TemplateString section_name) {
  // TODO(csilvers): make a string instead, or key on char*/length pairs
  if (section_dict_->find(section_name.ptr_) == section_dict_->end()) {
    TemplateDictionary* empty_dict =
      new TemplateDictionary("empty dictionary", arena_, this,
                             template_global_dict_);
    DictVector* sub_dict = new DictVector;
    sub_dict->push_back(empty_dict);
    (*section_dict_)[Memdup(section_name)] = sub_dict;
  }
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetValueAndShowSection()
// TemplateDictionary::SetEscapedValueAndShowSection()
//    If value is "", do nothing.  Otherwise, call AddSectionDictionary()
//    on the section and add exactly one entry to the sub-dictionary:
//    the given variable/value pair.
// ----------------------------------------------------------------------

void TemplateDictionary::SetValueAndShowSection(const TemplateString variable,
                                                const TemplateString value,
                                                const TemplateString section_name) {
  if (value.length_ == 0)    // no value: the do-nothing case
    return;
  TemplateDictionary* sub_dict = AddSectionDictionary(section_name);
  sub_dict->SetValue(variable, value);
}

// SetEscapedValueAndShowSection()
//    Defined in template_dictionary.h, as all good templatized
//    methods need to be.

// ----------------------------------------------------------------------
// TemplateDictionary::AddIncludeDictionary()
//    This is much like AddSectionDictionary().  One major difference
//    is that the new dictionary does not have a parent dictionary:
//    there's no automatic variable inclusion across template-file
//    boundaries.  Note there is no ShowTemplate() -- you must always
//    specify the dictionary to use explicitly.
// ----------------------------------------------------------------------

TemplateDictionary* TemplateDictionary::AddIncludeDictionary(
    const TemplateString include_name) {
  DictVector* dicts = NULL;
  // TODO(csilvers): make a string instead, or key on char*/length pairs
  if (include_dict_->find(include_name.ptr_) == include_dict_->end()) {
    dicts = new DictVector;
    (*include_dict_)[Memdup(include_name)] = dicts;
  } else {
    dicts = (*include_dict_)[include_name.ptr_];
  }
  assert(dicts != NULL);
  char dictsize[64];
  snprintf(dictsize, sizeof(dictsize), "%"PRIuS, dicts->size() + 1);
  string newname(name_ + "/" + include_name.ptr_ + "#" + dictsize);
  TemplateDictionary* retval =
    new TemplateDictionary(newname, arena_, NULL, template_global_dict_);
  dicts->push_back(retval);
  return retval;
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetFilename()
//    Sets the filename this dictionary is meant to be associated with.
//    When set, it's possible to expand a template with just the
//    template-dict; the template is loaded via SetFilename() (though
//    we'd have to assume a value for strip).  This is required for
//    dictionaries that are meant to be used with an include-template.
// ----------------------------------------------------------------------

void TemplateDictionary::SetFilename(const TemplateString filename) {
  filename_ = Memdup(filename);
}

// ----------------------------------------------------------------------
// TemplateDictionary::DumpToString()
// TemplateDictionary::Dump()
//    The values are shown in the following order:
//    - Scalar values
//    - Sub-dictionaries and their associated section names.
//    - Sub-dictionaries and their associated template names, with filename.
//
//    The various levels of sub-dictionaries are printed at
//    corresponding indention levels by using the setfill and setw io
//    manips as well as printing starting and ending markers around
//    each dictionary's entries
// ----------------------------------------------------------------------

static void IndentLine(string* out, int indent=0) {
  out->append(string(indent, ' ') + (indent ? " " : ""));
}

template<typename data_type>
struct LessByStringKey {
  typedef pair<const char*, data_type> argument_type;
  bool operator()(argument_type p1, argument_type p2) {
    const char* s1 = p1.first;
    const char* s2 = p2.first;
    return (s1 != s2) && (s2 == 0 || (s1 != 0 && strcmp(s1, s2) < 0));
  }
};

template<typename Dict, typename ResultType>
void SortByStringKeyInto(const Dict& dict, ResultType* result) {
  result->assign(dict.begin(), dict.end());
  sort(result->begin(), result->end(),
       LessByStringKey<typename Dict::data_type>());
}

void TemplateDictionary::DumpToString(string* out, int indent) const {
  const int kIndent = 2;            // num spaces to indent each level
  static const string kQuot("");    // could use " or empty string

  // Show globals if we're a top-level dictionary
  if (parent_dict_ == NULL) {
    IndentLine(out, indent);
    out->append("global dictionary {\n");

    vector<pair<const char*, const char*> > sorted_global_dict;
    {
      STATIC_ROLOCK;
      SortByStringKeyInto(*global_dict_, &sorted_global_dict);
      STATIC_UNLOCK;
    }
    for (vector<pair<const char*, const char*> >::const_iterator it
           = sorted_global_dict.begin();
         it != sorted_global_dict.end();  ++it) {
      IndentLine(out, indent + kIndent);
      out->append(kQuot + it->first + kQuot + ": >" + it->second + "<\n");
    }

    IndentLine(out, indent);
    out->append("};\n");
  }

  if (template_global_dict_owner_) {
    assert(template_global_dict_ != NULL);
    if (template_global_dict_ && !template_global_dict_->empty()) {
      IndentLine(out, indent);
      out->append("template dictionary {\n");
      vector<pair<const char*, const char*> > sorted_template_dict;
      SortByStringKeyInto(*template_global_dict_, &sorted_template_dict);
      for (vector<pair<const char*, const char*> >::const_iterator it
             = sorted_template_dict.begin();
           it != sorted_template_dict.end();  ++it) {
        IndentLine(out, indent + kIndent);
        out->append(kQuot + it->first + kQuot + ": >" + it->second + "<\n");
      }

      IndentLine(out, indent);
      out->append("};\n");
    }
  }

  IndentLine(out, indent);
  out->append("dictionary '" + name_);
  if (filename_ && filename_[0]) {
    out->append(" (intended for ");
    out->append(filename_);
    out->append(")");
  }
  out->append("' {\n");

  {  // Show variables
    vector<pair<const char*, const char*> > sorted_variable_dict;
    SortByStringKeyInto(*variable_dict_, &sorted_variable_dict);
    for (vector<pair<const char*, const char*> >::const_iterator it
           = sorted_variable_dict.begin();
         it != sorted_variable_dict.end();  ++it) {
      IndentLine(out, indent + kIndent);
      out->append(kQuot + it->first + kQuot + ": >" + it->second + "<\n");
    }
  }


  {  // Show section sub-dictionaries
    vector<pair<const char*, DictVector*> > sorted_section_dict;
    SortByStringKeyInto(*section_dict_, &sorted_section_dict);
    for (vector<pair<const char*, DictVector*> >::const_iterator it
           = sorted_section_dict.begin();
         it != sorted_section_dict.end();  ++it) {
      for (DictVector::const_iterator it2 = it->second->begin();
           it2 != it->second->end(); ++it2) {
        TemplateDictionary* dict = *it2;
        IndentLine(out, indent + kIndent);
        char dictnum[128];  // enough for two ints
        snprintf(dictnum, sizeof(dictnum), "dict %"PRIuS" of %"PRIuS,
                 it2 - it->second->begin() + 1, it->second->size());
        out->append("section ");
        out->append(it->first);
        out->append(" (");
        out->append(dictnum);
        out->append(") -->\n");
        dict->DumpToString(out, indent + kIndent + kIndent);
      }
    }
  }

  {  // Show template-include sub-dictionaries
    vector<pair<const char*, DictVector*> > sorted_include_dict;
    SortByStringKeyInto(*include_dict_, &sorted_include_dict);
    for (vector<pair<const char*, DictVector*> >::const_iterator it
           = sorted_include_dict.begin();
         it != sorted_include_dict.end();  ++it) {
      for (int i = 0; i < it->second->size(); ++i) {
        TemplateDictionary* dict = (*it->second)[i];
        IndentLine(out, indent + kIndent);
        char dictnum[128];  // enough for two ints
        snprintf(dictnum, sizeof(dictnum), "dict %d of %"PRIuS,
                 i + 1, it->second->size());
        out->append("include-template ");
        out->append(it->first);
        out->append(" (");
        out->append(dictnum);
        if (dict->filename_ && dict->filename_[0]) {
          out->append(", from ");
          out->append(dict->filename_);
        } else {
          out->append(", **NO FILENAME SET; THIS DICT WILL BE IGNORED**");
        }
        out->append(") -->\n");
        dict->DumpToString(out, indent + kIndent + kIndent);
      }
    }
  }

  IndentLine(out, indent);
  out->append("}\n");
}

void TemplateDictionary::Dump(int indent) const {
  string out;
  DumpToString(&out, indent);
  fputs(out.c_str(), stdout);
  fflush(stdout);
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetAnnotateOutput()
// TemplateDictionary::ShouldAnnotateOutput()
// TemplateDictionary::GetTemplatePathStart()
//    Set whether annotations should be inserted during template
//    expansion, which indicate why the template was expanded the way
//    it was.  template_path_start is used during annotation when we
//    get to the part where we annotate what filename we read an
//    include-template from.  Everything before and including
//    template_path_start in the filename, is deleted.  This is a
//    cheap way to 'relativize' a pathname, perhaps for consistency,
//    perhaps just to make it easier to read.  NULL turns off
//    annotation.
//       GetTemplatePathStart() returns NULL if
//    ShouldAnnotateOutput() is false.
// ----------------------------------------------------------------------

void TemplateDictionary::SetAnnotateOutput(const char* template_path_start) {
  if (template_path_start)
    template_path_start_for_annotations_ = Memdup(template_path_start,
                                                  strlen(template_path_start));
  else
    template_path_start_for_annotations_ = NULL;
}

bool TemplateDictionary::ShouldAnnotateOutput() const {
  return template_path_start_for_annotations_ != NULL;
}

const char* TemplateDictionary::GetTemplatePathStart() const {
  return template_path_start_for_annotations_;
}

// ----------------------------------------------------------------------
// TemplateDictionary::Memdup()
//    Copy the input into the arena, so we have a permanent copy of it.
//    Returns a pointer to the arena-copy.  Note we do not return
//    the length, so if the string might have internal NULs, you
//    should save the data-length from the input.
//       AddToVariableDict is a convenience routine which copies
//    key and value to the arena, then uses that to set the dict.
// ----------------------------------------------------------------------

const char* TemplateDictionary::Memdup(const char* s, int slen) {
  return arena_->MemdupPlusNUL(s, slen);  // add a \0, too
}


// ----------------------------------------------------------------------
// TemplateDictionary::GetSectionValue()
// TemplateDictionary::IsHiddenSection()
// TemplateDictionary::GetDictionaries()
// TemplateDictionary::IsHiddenTemplate()
// TemplateDictionary::GetTemplateDictionaries()
// TemplateDictionary::GetIncludeTemplateName()
//    The 'introspection' routines that tell Expand() what's in the
//    template dictionary.  GetSectionValue() does variable lookup:
//    first look in this dict, then in parent dicts, etc.  IsHidden*()
//    returns true iff the name is not present in the appropriate
//    dictionary.  None of these functions ever returns NULL.
// ----------------------------------------------------------------------

const char *TemplateDictionary::GetSectionValue(const string& variable) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    VariableDict::const_iterator it = d->variable_dict_->find(variable.c_str());
    if (it != d->variable_dict_->end())
      return it->second;
  }

  // No match in the dict tree. Check the template-global dict.
  assert(template_global_dict_ != NULL);
  if (template_global_dict_) {               // just being paranoid!
    VariableDict::const_iterator it =
      template_global_dict_->find(variable.c_str());
    if (it != template_global_dict_->end())
      return it->second;
  }

  // No match in dict tree or template-global dict.  Last chance: global dict.
  {
    STATIC_ROLOCK;
    GlobalDict::const_iterator it = global_dict_->find(variable.c_str());
    const char* retval = "";    // what we'll return if global lookup fails
    if (it != global_dict_->end())
      retval = it->second;
    STATIC_UNLOCK;
    return retval;
  }
}

bool TemplateDictionary::IsHiddenSection(const string& name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->section_dict_->find(name.c_str()) != d->section_dict_->end())
      return false;
  }
  return true;
}

const TemplateDictionary::DictVector& TemplateDictionary::GetDictionaries(
    const string& section_name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    SectionDict::const_iterator it = d->section_dict_->find(section_name.c_str());
    if (it != d->section_dict_->end())
      return *it->second;
  }
  assert("Call IsHiddenSection before GetDictionaries" == NULL);
  abort();
}

bool TemplateDictionary::IsHiddenTemplate(const string& name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->include_dict_->find(name.c_str()) != d->include_dict_->end())
      return false;
  }
  return true;
}

const TemplateDictionary::DictVector& TemplateDictionary::GetTemplateDictionaries(
    const string& variable) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    IncludeDict::const_iterator it = d->include_dict_->find(variable.c_str());
    if (it != d->include_dict_->end())
      return *it->second;
  }
  assert("Call IsHiddenTemplate before GetTemplateDictionaries" == NULL);
  abort();
}

const char *TemplateDictionary::GetIncludeTemplateName(const string& variable,
                                                       int dictnum) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    IncludeDict::const_iterator it = d->include_dict_->find(variable.c_str());
    if (it != d->include_dict_->end()) {
      TemplateDictionary* dict = (*it->second)[dictnum];
      return dict->filename_ ? dict->filename_ : "";   // map NULL to ""
    }
  }
  assert("Call IsHiddenTemplate before GetIncludeTemplateName" == NULL);
  abort();
}


// ----------------------------------------------------------------------
// HtmlEscape
// XMLEscape
// JavascriptEscape
//    Escape functors that can be used by SetEscapedValue().
//    Each takes a string as input and gives a string as output.
// ----------------------------------------------------------------------

// Escapes < > " & <non-space whitespace> to &lt; &gt; &quot; &amp; <space>
string TemplateDictionary::HtmlEscape::operator()(const string& in) const {
  string out;
  // we'll reserve some space in out to account for minimal escaping: say 12%
  out.reserve(in.size() + in.size()/8 + 16);
  for (int i = 0; i < in.length(); ++i) {
    switch (in[i]) {
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '\r': case '\n': case '\v': case '\f':
      case '\t': out += " "; break;      // non-space whitespace
      default: out += in[i];
    }
  }
  return out;
}

// Escapes &nbsp; to &#160;
// TODO(csilvers): have this do something more useful, once all callers have
//                 been fixed.  Dunno what 'more useful' might be, yet.
string TemplateDictionary::XmlEscape::operator()(const string& in) const {
  string out(in);

  string::size_type pos = 0;
  while (1) {
    pos = out.find("&nbsp;", pos);
    if ( pos == string::npos )
      break;
    out.replace(pos, sizeof("&nbsp;")-1, "&#160;");
    pos += sizeof("&#160;")-1;   // start searching again after the replacement
  };
  return out;
}

// Escapes " ' \ <CR> <LF> <BS> to \" \' \\ \r \n \b
string TemplateDictionary::JavascriptEscape::operator()(const string& in) const {
  string out;
  // we'll reserve some space in out to account for minimal escaping: say 1.5%
  out.reserve(in.size() + in.size()/64 + 2);
  for (int i = 0; i < in.length(); ++i) {
    switch (in[i]) {
      case '"': out += "\\\""; break;
      case '\'': out += "\\'"; break;
      case '\\': out += "\\\\"; break;
      case '\r': out += "\\r"; break;
      case '\n': out += "\\n"; break;
      case '\b': out += "\\b"; break;
      default: out += in[i];
    }
  }
  return out;
}

// Escapes " / \ <BS> <FF> <CR> <LF> <TAB> to \" \/ \\ \b \f \r \n \t
string TemplateDictionary::JsonEscape::operator()(const string& in) const {
  string out;
  // we'll reserve some space in out to account for minimal escaping: say 1.5%
  out.reserve(in.size() + in.size()/64 + 2);
  for (int i = 0; i < in.length(); ++i) {
    switch (in[i]) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '/': out += "\\/"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += in[i];
    }
  }
  return out;
}

_END_GOOGLE_NAMESPACE_
