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
#include "base/mutex.h"         // This must go first so we get _XOPEN_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>             // for varargs with StringAppendV
#include <string>
#include <algorithm>            // for sort()
#include <utility>              // for pair<>
#include <vector>
#include <map>
#include HASH_MAP_H             // defined in config.h
#include "google/template_dictionary.h"
#include "google/template_modifiers.h"
#include "base/mutex.h"
#include "base/arena.h"

_START_GOOGLE_NAMESPACE_

using std::vector;
using std::string;
using std::pair;
using std::map;
using HASH_NAMESPACE::hash_map;
using template_modifiers::TemplateModifier;

static Mutex g_static_mutex;

/*static*/ TemplateDictionary::GlobalDict* TemplateDictionary::global_dict_
  = NULL;

// Define the modifiers
/*static*/ const template_modifiers::HtmlEscape&
  TemplateDictionary::html_escape = template_modifiers::html_escape;
/*static*/ const template_modifiers::PreEscape&
  TemplateDictionary::pre_escape = template_modifiers::pre_escape;
/*static*/ const template_modifiers::XmlEscape&
  TemplateDictionary::xml_escape = template_modifiers::xml_escape;
/*static*/ const template_modifiers::JavascriptEscape&
  TemplateDictionary::javascript_escape = template_modifiers::javascript_escape;
/*static*/ const template_modifiers::UrlQueryEscape&
  TemplateDictionary::url_query_escape = template_modifiers::url_query_escape;
/*static*/ const template_modifiers::JsonEscape&
  TemplateDictionary::json_escape = template_modifiers::json_escape;

static const char* const kAnnotateOutput = "__ctemplate_annotate_output__";

// We use this to declare that the hashtable we construct should be
// small: it should have few buckets (because we expect few items to
// be inserted).  It's a macro with an #ifdef guard so we can easily
// change it for different hash_map implementations.
#ifndef CTEMPLATE_SMALL_HASHTABLE
# define CTEMPLATE_SMALL_HASHTABLE  3   // 3 buckets by default
#endif


// ----------------------------------------------------------------------
// TemplateDictionary::HashInsert()
//    A convenience function that's equivalent to m[key] = value, but
//    without needing key to have a default constructor like operator[]
//    does.
// ----------------------------------------------------------------------

#ifdef WIN32
# define TS_HASH_MAP \
  hash_map<TemplateString, ValueType, TemplateStringHash>
#else
# define TS_HASH_MAP \
  hash_map<TemplateString, ValueType, TemplateStringHash, TemplateStringEqual>
#endif

template<typename ValueType>   // ValueType should be small (pointer, int)
void TemplateDictionary::HashInsert(TS_HASH_MAP* m,
                                    TemplateString key, ValueType value) {
  // Unfortunately, insert() doesn't actually replace if key is already
  // in the map.  Thus, in that case (insert().second == false), we need
  // to overwrite the old value.  Since we don't define operator=, the
  // easiest legal way to overwrite is to use the copy-constructor with
  // placement-new.
  pair<typename TS_HASH_MAP::iterator,
       bool> r = m->insert(pair<TemplateString,ValueType>(key, value));
  if (r.second == false) {   // key already exists, so overwrite
    new (&r.first->second) ValueType(value);
  }
}


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
    new TemplateDictionary::GlobalDict(CTEMPLATE_SMALL_HASHTABLE);
  // Initialize the built-ins
  HashInsert(retval, TemplateString("BI_SPACE"), TemplateString(" "));
  HashInsert(retval, TemplateString("BI_NEWLINE"), TemplateString("\n"));
  return retval;
}

TemplateDictionary::TemplateDictionary(const string& name, UnsafeArena* arena)
    : name_(name),
      arena_(arena ? arena : new UnsafeArena(32768)),
      should_delete_arena_(arena ? false : true),   // true if we called new
      // dicts are initialized to have 3 buckets (that's small).
      // TODO(csilvers): use the arena for these instead
      variable_dict_(new VariableDict(CTEMPLATE_SMALL_HASHTABLE)),
      section_dict_(new SectionDict(CTEMPLATE_SMALL_HASHTABLE)),
      include_dict_(new IncludeDict(CTEMPLATE_SMALL_HASHTABLE)),
      template_global_dict_(new VariableDict(CTEMPLATE_SMALL_HASHTABLE)),
      template_global_dict_owner_(true),
      parent_dict_(NULL),
      filename_(NULL) {
  MutexLock ml(&g_static_mutex);
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
}

TemplateDictionary::TemplateDictionary(const string& name, UnsafeArena* arena,
                                       TemplateDictionary* parent_dict,
                                       VariableDict* template_global_dict)
    : name_(name),
      arena_(arena), should_delete_arena_(false),  // parents own it
      // dicts are initialized to have 3 buckets (that's small).
      // TODO(csilvers): use the arena for these instead
      variable_dict_(new VariableDict(CTEMPLATE_SMALL_HASHTABLE)),
      section_dict_(new SectionDict(CTEMPLATE_SMALL_HASHTABLE)),
      include_dict_(new IncludeDict(CTEMPLATE_SMALL_HASHTABLE)),
      template_global_dict_(template_global_dict),
      template_global_dict_owner_(false),
      parent_dict_(parent_dict),
      filename_(NULL) {
  MutexLock ml(&g_static_mutex);
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
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
  if (should_delete_arena_) {
    delete arena_;
  }
}

// ----------------------------------------------------------------------
// TemplateDictionary::MakeCopy()
//    Makes a recursive copy: so we copy any include dictionaries and
//    section dictionaries we see as well.  InternalMakeCopy() is
//    needed just so we can ensure that if we're doing a copy of a
//    subtree, it's due to a recursive call.
// ----------------------------------------------------------------------

TemplateDictionary* TemplateDictionary::InternalMakeCopy(
    const string& name_of_copy, UnsafeArena* arena) {
  TemplateDictionary* newdict;
  if (is_rootlevel_template()) {    // rootlevel uses public constructor
    newdict = new TemplateDictionary(name_of_copy, arena);
  } else {                          // recursve calls use private contructor
    // Note: we always use our own arena, even when we have a parent
    newdict = new TemplateDictionary(name_of_copy, arena,
                                     parent_dict_, template_global_dict_);
  }

  // Copy the variable dictionary
  for (VariableDict::const_iterator it = variable_dict_->begin();
       it != variable_dict_->end(); ++it) {
    newdict->SetValue(it->first, it->second);
  }
  // ...and the template-global-dict, if we're the owner of it
  if (template_global_dict_owner_) {
    for (VariableDict::const_iterator it = template_global_dict_->begin();
         it != template_global_dict_->end(); ++it) {
      newdict->SetTemplateGlobalValue(it->first, it->second);
    }
  }
  // Copy the section dictionary
  for (SectionDict::iterator it = section_dict_->begin();
       it != section_dict_->end();  ++it) {
    DictVector* dicts = new DictVector;
    HashInsert(newdict->section_dict_, newdict->Memdup(it->first), dicts);
    for (DictVector::iterator it2 = it->second->begin();
         it2 != it->second->end(); ++it2) {
      TemplateDictionary* subdict = *it2;
      dicts->push_back(subdict->InternalMakeCopy(subdict->name(),
                                                 newdict->arena_));
    }
  }
  // Copy the includes-dictionary
  for (IncludeDict::iterator it = include_dict_->begin();
       it != include_dict_->end();  ++it) {
    DictVector* dicts = new DictVector;
    HashInsert(newdict->include_dict_, newdict->Memdup(it->first), dicts);
    for (DictVector::iterator it2 = it->second->begin();
         it2 != it->second->end(); ++it2) {
      TemplateDictionary* subdict = *it2;
      dicts->push_back(subdict->InternalMakeCopy(subdict->name(),
                                                 newdict->arena_));
    }
  }

  // Copy the Expand data
  newdict->modifier_data_.CopyFrom(modifier_data_);

  // Finally, copy everything else not set properly by the constructor
  newdict->filename_ = newdict->Memdup(filename_).ptr_;

  return newdict;
}

TemplateDictionary* TemplateDictionary::MakeCopy(const string& name_of_copy,
                                                 UnsafeArena* arena) {
  if (!is_rootlevel_template()) {  // we're not at the root, which is illegal
    return NULL;
  }
  return InternalMakeCopy(name_of_copy, arena);
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
  HashInsert(variable_dict_, Memdup(variable), Memdup(value));
}

void TemplateDictionary::SetIntValue(const TemplateString variable, int value) {
  char buffer[64];   // big enough for any int
  int valuelen = snprintf(buffer, sizeof(buffer), "%d", value);
  HashInsert(variable_dict_, Memdup(variable), Memdup(buffer, valuelen));
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
    HashInsert(variable_dict_, Memdup(variable), TemplateString(scratch, buflen));
  } else {
    arena_->Shrink(scratch, 0);   // reclaim arena space we didn't use
    HashInsert(variable_dict_, Memdup(variable), Memdup(buffer, buflen));
    delete[] buffer;
  }
}

void TemplateDictionary::SetEscapedValue(TemplateString variable,
                                         TemplateString value,
                                         const TemplateModifier& escfn) {
  string escaped_string(escfn(value.ptr_, value.length_));
  SetValue(variable, escaped_string);
}

void TemplateDictionary::SetEscapedFormattedValue(TemplateString variable,
                                                  const TemplateModifier& escfn,
                                                  const char* format, ...) {
  char *scratch, *buffer;

  scratch = arena_->Alloc(1024);  // StringAppendV requires >=1024 bytes
  va_list ap;
  va_start(ap, format);
  const int buflen = StringAppendV(scratch, &buffer, format, ap);
  va_end(ap);

  string escaped_string(escfn(buffer, buflen));
  // Reclaim the arena space: the value we care about is now in escaped_string
  arena_->Shrink(scratch, 0);   // reclaim arena space we didn't use
  if (buffer != scratch)
    delete[] buffer;

  SetValue(variable, escaped_string);
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetTemplateGlobalValue()
//    Sets a value in the template-global dict.  Unlike normal
//    variable lookups, these persist across sub-includes.
// ----------------------------------------------------------------------

void TemplateDictionary::SetTemplateGlobalValue(const TemplateString variable,
                                                const TemplateString value) {
  assert(template_global_dict_ != NULL);
  if (template_global_dict_) {
    HashInsert(template_global_dict_, Memdup(variable), Memdup(value));
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

  MutexLock ml(&g_static_mutex);
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();

  HashInsert(global_dict_,
             TemplateString(variable_copy, variable.length_),
             TemplateString(value_copy, value.length_));
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
  const SectionDict::iterator it = section_dict_->find(section_name);
  if (it == section_dict_->end()) {
    dicts = new DictVector;
    // Since most lists will remain under 8 or 16 entries but will frequently
    // be more than four, this prevents copying from 1->2->4->8.
    dicts->reserve(8);
    HashInsert(section_dict_, Memdup(section_name), dicts);
  } else {
    dicts = it->second;
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
  if (section_dict_->find(section_name) == section_dict_->end()) {
    TemplateDictionary* empty_dict =
      new TemplateDictionary("empty dictionary", arena_, this,
                             template_global_dict_);
    DictVector* sub_dict = new DictVector;
    sub_dict->push_back(empty_dict);
    HashInsert(section_dict_, Memdup(section_name), sub_dict);
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
  if (value.length_ == 0)        // no value: the do-nothing case
    return;
  TemplateDictionary* sub_dict = AddSectionDictionary(section_name);
  sub_dict->SetValue(variable, value);
}

void TemplateDictionary::SetEscapedValueAndShowSection(
    const TemplateString variable, const TemplateString value,
    const TemplateModifier& escfn, const TemplateString section_name) {
  string escaped_string(escfn(value.ptr_, value.length_));
  if (escaped_string.empty())    // no value: the do-nothing case
    return;
  TemplateDictionary* sub_dict = AddSectionDictionary(section_name);
  sub_dict->SetValue(variable, escaped_string);
}

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
  const IncludeDict::iterator it = include_dict_->find(include_name);
  if (it == include_dict_->end()) {
    dicts = new DictVector;
    HashInsert(include_dict_, Memdup(include_name), dicts);
  } else {
    dicts = it->second;
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
  filename_ = Memdup(filename).ptr_;
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

void TemplateDictionary::DumpToString(string* out, int indent) const {
  const int kIndent = 2;            // num spaces to indent each level
  static const string kQuot("");    // could use " or empty string

  // Show globals if we're a top-level dictionary
  if (parent_dict_ == NULL) {
    IndentLine(out, indent);
    out->append("global dictionary {\n");

    // We could be faster than converting every TemplateString into a
    // string and inserted into an ordered data structure, but why bother?
    map<string, string> sorted_global_dict;
    {
      ReaderMutexLock ml(&g_static_mutex);
      for (GlobalDict::const_iterator it = global_dict_->begin();
           it != global_dict_->end();  ++it) {
        sorted_global_dict[string(it->first.ptr_, it->first.length_)] =
            string(it->second.ptr_, it->second.length_);
      }
    }
    for (map<string, string>::const_iterator it = sorted_global_dict.begin();
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
      map<string, string> sorted_template_dict;
      for (VariableDict::const_iterator it = template_global_dict_->begin();
           it != template_global_dict_->end();  ++it) {
        sorted_template_dict[string(it->first.ptr_, it->first.length_)] =
            string(it->second.ptr_, it->second.length_);
      }
      for (map<string,string>::const_iterator it = sorted_template_dict.begin();
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
    map<string, string> sorted_variable_dict;
    for (VariableDict::const_iterator it = variable_dict_->begin();
         it != variable_dict_->end();  ++it) {
      sorted_variable_dict[string(it->first.ptr_, it->first.length_)] =
          string(it->second.ptr_, it->second.length_);
    }
    for (map<string,string>::const_iterator it = sorted_variable_dict.begin();
         it != sorted_variable_dict.end();  ++it) {
      IndentLine(out, indent + kIndent);
      out->append(kQuot + it->first + kQuot + ": >" + it->second + "<\n");
    }
  }

  {  // Show section sub-dictionaries
    map<string, const DictVector*> sorted_section_dict;
    for (SectionDict::const_iterator it = section_dict_->begin();
         it != section_dict_->end();  ++it) {
      sorted_section_dict[string(it->first.ptr_, it->first.length_)] =
          it->second;
    }
    for (map<string, const DictVector*>::const_iterator it =
             sorted_section_dict.begin();
         it != sorted_section_dict.end();  ++it) {
      for (DictVector::const_iterator it2 = it->second->begin();
           it2 != it->second->end(); ++it2) {
        TemplateDictionary* dict = *it2;
        IndentLine(out, indent + kIndent);
        char dictnum[128];  // enough for two ints
        snprintf(dictnum, sizeof(dictnum), "dict %"PRIuS" of %"PRIuS,
                 it2 - it->second->begin() + 1, it->second->size());
        out->append(string("section ") + it->first + " ("+dictnum+") -->\n");
        dict->DumpToString(out, indent + kIndent + kIndent);
      }
    }
  }

  {  // Show template-include sub-dictionaries
    map<string, const DictVector*> sorted_include_dict;
    for (IncludeDict::const_iterator it = include_dict_->begin();
         it != include_dict_->end();  ++it) {
      sorted_include_dict[string(it->first.ptr_, it->first.length_)] =
          it->second;
    }
    for (map<string, const DictVector*>::const_iterator it =
             sorted_include_dict.begin();
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
  fwrite(out.data(), 1, out.length(), stdout);
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
    SetModifierData(kAnnotateOutput, Memdup(template_path_start).ptr_);
  else
    SetModifierData(kAnnotateOutput, NULL);
}

bool TemplateDictionary::ShouldAnnotateOutput() const {
  return modifier_data_.Lookup(kAnnotateOutput) != NULL;
}

const char* TemplateDictionary::GetTemplatePathStart() const {
  return modifier_data_.LookupAsString(kAnnotateOutput);
}

// ----------------------------------------------------------------------
// TemplateDictionary::Memdup()
//    Copy the input into the arena, so we have a permanent copy of
//    it.  Returns a pointer to the arena-copy, as a TemplateString
//    (in case the input has internal NULs).
//    ----------------------------------------------------------------------

TemplateString TemplateDictionary::Memdup(const char* s, size_t slen) {
  return TemplateString(arena_->MemdupPlusNUL(s, slen), slen);  // add a \0 too
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
    VariableDict::const_iterator it = d->variable_dict_->find(variable);
    if (it != d->variable_dict_->end())
      return it->second.ptr_;
  }

  // No match in the dict tree. Check the template-global dict.
  assert(template_global_dict_ != NULL);
  if (template_global_dict_) {               // just being paranoid!
    VariableDict::const_iterator it = template_global_dict_->find(variable);
    if (it != template_global_dict_->end())
      return it->second.ptr_;
  }

  // No match in dict tree or template-global dict.  Last chance: global dict.
  {
    ReaderMutexLock ml(&g_static_mutex);
    GlobalDict::const_iterator it = global_dict_->find(variable);
    const char* retval = "";    // what we'll return if global lookup fails
    if (it != global_dict_->end())
      retval = it->second.ptr_;
    return retval;
  }
}

bool TemplateDictionary::IsHiddenSection(const string& name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->section_dict_->find(name) != d->section_dict_->end())
      return false;
  }
  return true;
}

const TemplateDictionary::DictVector& TemplateDictionary::GetDictionaries(
    const string& section_name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    SectionDict::const_iterator it = d->section_dict_->find(section_name);
    if (it != d->section_dict_->end())
      return *it->second;
  }
  assert("Call IsHiddenSection before GetDictionaries" == NULL);
  abort();
}

bool TemplateDictionary::IsHiddenTemplate(const string& name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->include_dict_->find(name) != d->include_dict_->end())
      return false;
  }
  return true;
}

const TemplateDictionary::DictVector& TemplateDictionary::GetTemplateDictionaries(
    const string& variable) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    IncludeDict::const_iterator it = d->include_dict_->find(variable);
    if (it != d->include_dict_->end())
      return *it->second;
  }
  assert("Call IsHiddenTemplate before GetTemplateDictionaries" == NULL);
  abort();
}

const char *TemplateDictionary::GetIncludeTemplateName(const string& variable,
                                                       int dictnum) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    IncludeDict::const_iterator it = d->include_dict_->find(variable);
    if (it != d->include_dict_->end()) {
      TemplateDictionary* dict = (*it->second)[dictnum];
      return dict->filename_ ? dict->filename_ : "";   // map NULL to ""
    }
  }
  assert("Call IsHiddenTemplate before GetIncludeTemplateName" == NULL);
  abort();
}

void TemplateDictionary::SetModifierData(const char* key, const void* data) {
  modifier_data_.Insert(Memdup(key).ptr_, data);
}

_END_GOOGLE_NAMESPACE_
