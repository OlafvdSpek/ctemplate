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
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_modifiers.h>
#include "base/mutex.h"
#include "base/arena.h"
#include "base/small_map.h"

_START_GOOGLE_NAMESPACE_

using std::vector;
using std::string;
using std::map;
using std::pair;
using std::make_pair;

static Mutex g_static_mutex;

/*static*/ UnsafeArena* const TemplateDictionary::NO_ARENA = NULL;
/*static*/ TemplateDictionary::GlobalDict* TemplateDictionary::global_dict_
  = NULL;

static const char* const kAnnotateOutput = "__ctemplate_annotate_output__";

// ----------------------------------------------------------------------
// ArenaAllocator::allocate()
// ArenaAllocator::deallocate()
//    These are in the .cc file because that's where we #include arena.h
// ----------------------------------------------------------------------

template<class T>
typename ArenaAllocator<T>::pointer ArenaAllocator<T>::allocate(
    size_type n, std::allocator<void>::const_pointer /*hint*/) {
  return reinterpret_cast<T*>(arena_->AllocAligned(n * sizeof(T), kAlignment));
}

template<class T> void ArenaAllocator<T>::deallocate(pointer p, size_type n) {
  arena_->Free(p, n * sizeof(T));
}

// TODO(csilvers): sizeof(void*) may be too big.  But is probably ok.
/*static*/ template<class T> const int ArenaAllocator<T>::kAlignment =
    (1 == sizeof(T) ? 1 : sizeof(void*));

// ----------------------------------------------------------------------
// TemplateDictionary::map_arena_init
//    This class is what small_map<> uses to create a new
//    arena-allocated map<> when it decides it needs to do that.
// ----------------------------------------------------------------------

class TemplateDictionary::map_arena_init {
 public:
  map_arena_init(UnsafeArena* arena) : arena_(arena) { }
  template<typename T> void operator ()(ManualConstructor<T>* map) const {
    map->Init(typename T::key_compare(), arena_);
  }
 private:
  UnsafeArena* arena_;
};

// ----------------------------------------------------------------------
// TemplateDictionary::LazilyCreateDict()
// TemplateDictionary::CreateDictVector()
// TemplateDictionary::CreateTemplateSubdict()
//    These routines allocate the objects that TemplateDictionary
//    allocates (sub-dictionaries, variable maps, etc).  Each
//    allocates memory on the arena, and instructs the STL objects
//    to use the arena for their own internal allocations as well.
// ----------------------------------------------------------------------

template<typename T>
inline void TemplateDictionary::LazilyCreateDict(T** dict) {
  if (*dict != NULL)
    return;
  // Placement new: construct the map in the memory used by *dict.
  char* buffer = arena_->Alloc(sizeof(**dict));
  new (buffer) T(arena_);
  *dict = reinterpret_cast<T*>(buffer);
}

inline TemplateDictionary::DictVector* TemplateDictionary::CreateDictVector() {
  char* buffer = arena_->Alloc(sizeof(DictVector));
  // Placement new: construct the vector in the memory used by buffer.
  new (buffer) DictVector(arena_);
  return reinterpret_cast<DictVector*>(buffer);
}

inline TemplateDictionary* TemplateDictionary::CreateTemplateSubdict(
    const TemplateString& name,
    UnsafeArena* arena,
    TemplateDictionary* parent_dict,
    TemplateDictionary* template_global_dict_owner) {
  char* buffer = arena->Alloc(sizeof(TemplateDictionary));
  // Placement new: construct the sub-tpl in the memory used by tplbuf.
  new (buffer) TemplateDictionary(name, arena, parent_dict,
                                  template_global_dict_owner);
  return reinterpret_cast<TemplateDictionary*>(buffer);
}

// ----------------------------------------------------------------------
// TemplateDictionary::HashInsert()
//    A convenience function that's equivalent to m[key] = value, but
//    converting the key to an id first, and without necessarily needing
//    key to have a default constructor like operator[] does.  It also
//    inserts (key, id(key)) into a map to allow for id->key mapping.
// ----------------------------------------------------------------------

// By default, prefer the m[key] = value construct.  We do something
// more complex for TemplateString, though, since m[key] requires a
// zero-arg constructor, which TemplateString doesn't have.  We could
// do the more complex thing everywhere, but that seems to trigger a
// bug in in gcc 4.1.2 (at least) when compiled with -O2.  Shrug.
namespace {
template<typename MapType, typename ValueType>
inline void DoHashInsert(MapType* m, TemplateId id, ValueType value) {
  (*m)[id] = value;
}

template<typename MapType>
inline void DoHashInsert(MapType* m, TemplateId id, TemplateString value) {
  pair<typename MapType::iterator, bool> r
      = m->insert(typename MapType::value_type(id, value));
  // Unfortunately, insert() doesn't actually replace if key is
  // already in the map.  Thus, in that case (insert().second == false),
  // we need to overwrite the old value.  Since TemplateString
  // doesn't define operator=, the easiest legal way to overwrite is
  // to use the copy-constructor with placement-new.  Note that since
  // TemplateString has no destructor, we don't need to call the
  // destructor to 'clear out' the old value.
  if (r.second == false) {
    new (&r.first->second) TemplateString(value);
  }
}
}

template<typename MapType, typename ValueType>
void TemplateDictionary::HashInsert(MapType* m,
                                    TemplateString key, ValueType value) {
  const TemplateId id = key.GetGlobalId();
  DoHashInsert(m, id, value);
  AddToIdToNameMap(id, key);  // allows us to do the hash-key -> name mapping
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
  TemplateDictionary::GlobalDict* retval = new TemplateDictionary::GlobalDict;
  // Initialize the built-ins
  HashInsert(retval, TemplateString("BI_SPACE"), TemplateString(" "));
  HashInsert(retval, TemplateString("BI_NEWLINE"), TemplateString("\n"));
  return retval;
}

TemplateDictionary::TemplateDictionary(const TemplateString& name,
                                       UnsafeArena* arena)
    : arena_(arena ? arena : new UnsafeArena(32768)),
      should_delete_arena_(arena ? false : true),   // true if we called new
      name_(Memdup(name)),    // arena must have been set up first
      variable_dict_(NULL),
      section_dict_(NULL),
      include_dict_(NULL),
      template_global_dict_(NULL),
      template_global_dict_owner_(this),
      parent_dict_(NULL),
      filename_(NULL) {
  MutexLock ml(&g_static_mutex);
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
}

TemplateDictionary::TemplateDictionary(
    const TemplateString& name,
    UnsafeArena* arena,
    TemplateDictionary* parent_dict,
    TemplateDictionary* template_global_dict_owner)
    : arena_(arena), should_delete_arena_(false),  // parents own it
      name_(Memdup(name)),    // arena must have been set up first
      variable_dict_(NULL),
      section_dict_(NULL),
      include_dict_(NULL),
      template_global_dict_(NULL),
      template_global_dict_owner_(template_global_dict_owner),
      parent_dict_(parent_dict),
      filename_(NULL) {
  assert(template_global_dict_owner_ != NULL);
  MutexLock ml(&g_static_mutex);
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();
}

TemplateDictionary::~TemplateDictionary() {
  // Everything we allocate, we allocate on the arena, so we
  // don't need to free anything here.
  if (should_delete_arena_) {
    delete arena_;
  }
}

// ----------------------------------------------------------------------
// TemplateDictionary::MakeCopy()
//    Makes a recursive copy: so we copy any include dictionaries and
//    section dictionaries we see as well.  InternalMakeCopy() is
//    needed just so we can ensure that if we're doing a copy of a
//    subtree, it's due to a recursive call.  Returns NULL if there
//    is an error copying.
// ----------------------------------------------------------------------

TemplateDictionary* TemplateDictionary::InternalMakeCopy(
    const TemplateString& name_of_copy,
    UnsafeArena* arena,
    TemplateDictionary* parent_dict,
    TemplateDictionary* template_global_dict_owner) {
  TemplateDictionary* newdict;
  if (template_global_dict_owner_ == this) {
    // We're a root-level template.  We want the copy to be just like
    // us, and have its own template_global_dict_, that it owns.
    // We use the normal global new, since newdict will be returned
    // to thse user.
    newdict = new TemplateDictionary(name_of_copy, arena);
  } else {                          // recursve calls use private contructor
    // We're not a root-level template, so we want the copy to refer to the
    // same template_global_dict_ owner that we do.
    // Note: we always use our own arena, even when we have a parent
    //       (though we have the same arena as our parent when we have one).
    assert(arena);
    assert(parent_dict ? arena == parent_dict->arena_ : true);
    newdict = CreateTemplateSubdict(name_of_copy, arena,
                                    parent_dict, template_global_dict_owner);
  }

  // Copy the variable dictionary
  if (variable_dict_) {
    newdict->LazilyCreateDict(&newdict->variable_dict_);
    for (VariableDict::const_iterator it = variable_dict_->begin();
         it != variable_dict_->end(); ++it) {
      newdict->variable_dict_->insert(make_pair(it->first,
                                                newdict->Memdup(it->second)));
    }
  }
  // ...and the template-global-dict, if we have one (only root-level tpls do)
  if (template_global_dict_) {
    newdict->LazilyCreateDict(&newdict->template_global_dict_);
    for (VariableDict::const_iterator it = template_global_dict_->begin();
         it != template_global_dict_->end(); ++it) {
      newdict->template_global_dict_->insert(
          make_pair(it->first,
                    newdict->Memdup(it->second)));
    }
  }
  if (section_dict_) {
    newdict->LazilyCreateDict(&newdict->section_dict_);
    for (SectionDict::iterator it = section_dict_->begin();
         it != section_dict_->end(); ++it) {
      DictVector* dicts = newdict->CreateDictVector();
      newdict->section_dict_->insert(make_pair(it->first, dicts));
      for (DictVector::iterator it2 = it->second->begin();
           it2 != it->second->end(); ++it2) {
        TemplateDictionary* subdict = *it2;
        // In this case, we pass in newdict as the parent of our new dict.
        dicts->push_back(subdict->InternalMakeCopy(
                             subdict->name(), newdict->arena_,
                             newdict, newdict->template_global_dict_owner_));
      }
    }
  }
  // Copy the includes-dictionary
  if (include_dict_) {
    newdict->LazilyCreateDict(&newdict->include_dict_);
    for (IncludeDict::iterator it = include_dict_->begin();
         it != include_dict_->end();  ++it) {
      DictVector* dicts = newdict->CreateDictVector();
      newdict->include_dict_->insert(make_pair(it->first, dicts));
      for (DictVector::iterator it2 = it->second->begin();
           it2 != it->second->end(); ++it2) {
        TemplateDictionary* subdict = *it2;
        // In this case, we pass in NULL as the parent of our new dict:
        // parents are not inherited across include-dictionaries.
        dicts->push_back(subdict->InternalMakeCopy(
                             subdict->name(), newdict->arena_,
                             NULL, newdict->template_global_dict_owner_));
      }
    }
  }

  // Finally, copy everything else not set properly by the constructor
  newdict->filename_ = newdict->Memdup(filename_).ptr_;

  return newdict;
}

TemplateDictionary* TemplateDictionary::MakeCopy(
    const TemplateString& name_of_copy, UnsafeArena* arena) {
  if (template_global_dict_owner_ != this) {
    // We're not at the root, which is illegal.
    return NULL;
  }
  return InternalMakeCopy(name_of_copy, arena,
                          NULL, template_global_dict_owner_);
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
  LazilyCreateDict(&variable_dict_);
  HashInsert(variable_dict_, variable, Memdup(value));
}

void TemplateDictionary::SetValueWithoutCopy(const TemplateString variable,
                                             const TemplateString value) {
  LazilyCreateDict(&variable_dict_);
  // Don't memdup value - the caller will manage memory.
  HashInsert(variable_dict_, variable, value);
}

void TemplateDictionary::SetIntValue(const TemplateString variable, int value) {
  char buffer[64];   // big enough for any int
  int valuelen = snprintf(buffer, sizeof(buffer), "%d", value);
  LazilyCreateDict(&variable_dict_);
  HashInsert(variable_dict_, variable, Memdup(buffer, valuelen));
}

void TemplateDictionary::SetFormattedValue(const TemplateString variable,
                                           const char* format, ...) {
  char *scratch, *buffer;

  scratch = arena_->Alloc(1024);  // StringAppendV requires >=1024 bytes
  va_list ap;
  va_start(ap, format);
  const int buflen = StringAppendV(scratch, &buffer, format, ap);
  va_end(ap);

  LazilyCreateDict(&variable_dict_);

  // If it fit into scratch, great, otherwise we need to copy into arena
  if (buffer == scratch) {
    scratch = arena_->Shrink(scratch, buflen+1);   // from 1024 to |value+\0|
    HashInsert(variable_dict_, variable, TemplateString(scratch, buflen));
  } else {
    arena_->Shrink(scratch, 0);   // reclaim arena space we didn't use
    HashInsert(variable_dict_, variable, Memdup(buffer, buflen));
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
  assert(template_global_dict_owner_ != NULL);
  template_global_dict_owner_->LazilyCreateDict(
      &template_global_dict_owner_->template_global_dict_);
  HashInsert(template_global_dict_owner_->template_global_dict_,
             variable, Memdup(value));
}

void TemplateDictionary::SetTemplateGlobalValueWithoutCopy(
    const TemplateString variable,
    const TemplateString value) {
  assert(template_global_dict_owner_ != NULL);
  template_global_dict_owner_->LazilyCreateDict(
      &template_global_dict_owner_->template_global_dict_);
  // Don't memdup value - the caller will manage memory.
  HashInsert(template_global_dict_owner_->template_global_dict_,
             variable, value);
}

// ----------------------------------------------------------------------
// TemplateDictionary::SetGlobalValue()
//    Sets a value in the global dict.  Note this is a static method.
// ----------------------------------------------------------------------

/*static*/ void TemplateDictionary::SetGlobalValue(
    const TemplateString variable,
    const TemplateString value) {
  // We can't use memdup here, since we're a static method.  We do a strdup,
  // which is fine, since global_dict_ lives the entire program anyway.
  // It's unnecessary to copy the variable, since HashInsert takes care of
  // that for us.
  char* value_copy = new char[value.length_ + 1];
  memcpy(value_copy, value.ptr_, value.length_);
  value_copy[value.length_] = '\0';

  MutexLock ml(&g_static_mutex);
  if (global_dict_ == NULL)
    global_dict_ = SetupGlobalDictUnlocked();

  HashInsert(global_dict_,
             variable,
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
  LazilyCreateDict(&section_dict_);
  const SectionDict::iterator it = section_dict_->find(
      section_name.GetGlobalId());
  if (it == section_dict_->end()) {
    dicts = CreateDictVector();
    // Since most lists will remain under 8 or 16 entries but will frequently
    // be more than four, this prevents copying from 1->2->4->8.
    dicts->reserve(8);
    HashInsert(section_dict_, section_name, dicts);
  } else {
    dicts = it->second;
  }
  assert(dicts != NULL);
  char dictsize[64];
  snprintf(dictsize, sizeof(dictsize), "%"PRIuS, dicts->size() + 1);
  string newname(string(name_.ptr_, name_.length_) + "/" + section_name.ptr_
                 + "#" + dictsize);
  TemplateDictionary* retval = CreateTemplateSubdict(
      newname, arena_, this, template_global_dict_owner_);
  dicts->push_back(retval);
  return retval;
}

void TemplateDictionary::ShowSection(const TemplateString section_name) {
  LazilyCreateDict(&section_dict_);
  if (section_dict_->find(section_name.GetGlobalId()) == section_dict_->end()) {
    TemplateDictionary* empty_dict = CreateTemplateSubdict(
        "empty dictionary", arena_, this, template_global_dict_owner_);
    DictVector* sub_dict = CreateDictVector();
    sub_dict->push_back(empty_dict);
    HashInsert(section_dict_, section_name, sub_dict);
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
  LazilyCreateDict(&include_dict_);
  const IncludeDict::iterator it = include_dict_->find(
      include_name.GetGlobalId());
  if (it == include_dict_->end()) {
    dicts = CreateDictVector();
    HashInsert(include_dict_, include_name, dicts);
  } else {
    dicts = it->second;
  }
  assert(dicts != NULL);
  char dictsize[64];
  snprintf(dictsize, sizeof(dictsize), "%"PRIuS, dicts->size() + 1);
  string newname(string(name_.ptr_, name_.length_) + "/" + include_name.ptr_
                 + "#" + dictsize);
  TemplateDictionary* retval = CreateTemplateSubdict(
      newname, arena_, NULL, template_global_dict_owner_);
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
// TemplateDictionary::AddToIdToNameMap()
//    We have a problem when we try to dump the contents of the
//    dictionary, because instead of storing the keys to global_dict_
//    etc as strings, we store them as integer id's.  We need this
//    map, from id->string, to be able to dump.  This should be called
//    every time we add a string to a TemplateDictionary hashtable.
// ----------------------------------------------------------------------

/*static*/ void TemplateDictionary::AddToIdToNameMap(TemplateId id,
                                                     const TemplateString& str) {
  // If str.id_ is set, that means we were added to the id-to-name map
  // at TemplateString constructor time, when the id_ was set.  So we
  // don't need to bother again here.
  if (str.id_ != 0) {
    return;
  }
  // Verify that if this id is already in the map, it's there with our
  // contents.  If not, that would mean a hash collision (since our
  // id's are hash values).
  assert(TemplateString::IdToString(id) == kStsEmpty ||
         memcmp(str.ptr_, TemplateString::IdToString(id).ptr_,
                str.length_) == 0);

  TemplateString str_with_id(str.ptr_, str.length_, str.is_immutable(), id);
  str_with_id.AddToGlobalIdToNameMap();
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
        const TemplateString key = TemplateString::IdToString(it->first);
        assert(key.ptr_ != NULL);
        sorted_global_dict[string(key.ptr_, key.length_)] =
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

  if (template_global_dict_ && !template_global_dict_->empty()) {
    IndentLine(out, indent);
    out->append("template dictionary {\n");
    map<string, string> sorted_template_dict;
    for (VariableDict::const_iterator it = template_global_dict_->begin();
         it != template_global_dict_->end();  ++it) {
      const TemplateString key = TemplateString::IdToString(it->first);
      assert(key.ptr_ != NULL);
      sorted_template_dict[string(key.ptr_, key.length_)] =
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

  IndentLine(out, indent);
  out->append(string("dictionary '") + string(name_.ptr_, name_.length_));
  if (filename_ && filename_[0]) {
    out->append(" (intended for ");
    out->append(filename_);
    out->append(")");
  }
  out->append("' {\n");

  if (variable_dict_) {  // Show variables
    map<string, string> sorted_variable_dict;
    for (VariableDict::const_iterator it = variable_dict_->begin();
         it != variable_dict_->end();  ++it) {
      const TemplateString key = TemplateString::IdToString(it->first);
      assert(key.ptr_ != NULL);
      sorted_variable_dict[string(key.ptr_, key.length_)] =
          string(it->second.ptr_, it->second.length_);
    }
    for (map<string,string>::const_iterator it = sorted_variable_dict.begin();
         it != sorted_variable_dict.end();  ++it) {
      IndentLine(out, indent + kIndent);
      out->append(kQuot + it->first + kQuot + ": >" + it->second + "<\n");
    }
  }

  if (section_dict_) {  // Show section sub-dictionaries
    map<string, const DictVector*> sorted_section_dict;
    for (SectionDict::const_iterator it = section_dict_->begin();
         it != section_dict_->end();  ++it) {
      const TemplateString key = TemplateString::IdToString(it->first);
      assert(key.ptr_ != NULL);
      sorted_section_dict[string(key.ptr_, key.length_)] = it->second;
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

  if (include_dict_) {  // Show template-include sub-dictionaries
    map<string, const DictVector*> sorted_include_dict;
    for (IncludeDict::const_iterator it = include_dict_->begin();
         it != include_dict_->end();  ++it) {
      const TemplateString key = TemplateString::IdToString(it->first);
      assert(key.ptr_ != NULL);
      sorted_include_dict[string(key.ptr_, key.length_)] = it->second;
    }
    for (map<string, const DictVector*>::const_iterator it =
             sorted_include_dict.begin();
         it != sorted_include_dict.end();  ++it) {
      for (vector<TemplateDictionary*>::size_type i = 0;
           i < it->second->size();  ++i) {
        TemplateDictionary* dict = (*it->second)[i];
        IndentLine(out, indent + kIndent);
        char dictnum[128];  // enough for two ints
        snprintf(dictnum, sizeof(dictnum), "dict %d of %"PRIuS,
                 static_cast<int>(i + 1), it->second->size());
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
// TemplateDictionary::Memdup()
//    Copy the input into the arena, so we have a permanent copy of
//    it.  Returns a pointer to the arena-copy, as a TemplateString
//    (in case the input has internal NULs).
// ----------------------------------------------------------------------

TemplateString TemplateDictionary::Memdup(const char* s, size_t slen) {
  return TemplateString(arena_->MemdupPlusNUL(s, slen), slen);  // add a \0 too
}


// ----------------------------------------------------------------------
// TemplateDictionary::GetSectionValue()
// TemplateDictionary::IsHiddenSection()
// TemplateDictionary::IsHiddenTemplate()
// TemplateDictionary::GetIncludeTemplateName()
//    The 'introspection' routines that tell Expand() what's in the
//    template dictionary.  GetSectionValue() does variable lookup:
//    first look in this dict, then in parent dicts, etc.  IsHidden*()
//    returns true iff the name is not present in the appropriate
//    dictionary.  None of these functions ever returns NULL.
// ----------------------------------------------------------------------

const char *TemplateDictionary::GetSectionValue(
    const TemplateString& variable) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->variable_dict_) {
      VariableDict::const_iterator it
          = d->variable_dict_->find(variable.GetGlobalId());
      if (it != d->variable_dict_->end())
        return it->second.ptr_;
    }
  }

  // No match in the dict tree. Check the template-global dict.
  assert(template_global_dict_owner_ != NULL);
  if (template_global_dict_owner_->template_global_dict_) {
    VariableDict::const_iterator it =
        template_global_dict_owner_->template_global_dict_->find(
            variable.GetGlobalId());
    if (it != template_global_dict_owner_->template_global_dict_->end())
      return it->second.ptr_;
  }

  // No match in dict tree or template-global dict.  Last chance: global dict.
  {
    ReaderMutexLock ml(&g_static_mutex);
    GlobalDict::const_iterator it = global_dict_->find(variable.GetGlobalId());
    const char* retval = "";    // what we'll return if global lookup fails
    if (it != global_dict_->end())
      retval = it->second.ptr_;
    return retval;
  }
}

bool TemplateDictionary::IsHiddenSection(const TemplateString& name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->section_dict_ &&
        d->section_dict_->find(name.GetGlobalId()) != d->section_dict_->end())
      return false;
  }
  return true;
}

bool TemplateDictionary::IsHiddenTemplate(const TemplateString& name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->include_dict_ &&
        d->include_dict_->find(name.GetGlobalId()) != d->include_dict_->end())
      return false;
  }
  return true;
}

const char *TemplateDictionary::GetIncludeTemplateName(
    const TemplateString& variable, int dictnum) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->include_dict_) {
      IncludeDict::const_iterator it = d->include_dict_->find(
          variable.GetGlobalId());
      if (it != d->include_dict_->end()) {
        TemplateDictionary* dict = (*it->second)[dictnum];
        return dict->filename_ ? dict->filename_ : "";   // map NULL to ""
      }
    }
  }
  assert("Call IsHiddenTemplate before GetIncludeTemplateName" == NULL);
  abort();
}

// ----------------------------------------------------------------------
// TemplateDictionary::CreateSectionIterator()
// TemplateDictionary::CreateTemplateIterator()
// TemplateDictionary::Iterator::HasNext()
// TemplateDictionary::Iterator::Next()
//    Iterator framework.
// ----------------------------------------------------------------------

template <typename T> bool TemplateDictionary::Iterator<T>::HasNext() const {
  return begin_ != end_;
}

template <typename T> const TemplateDictionaryInterface&
TemplateDictionary::Iterator<T>::Next() {
  return **(begin_++);
}

TemplateDictionaryInterface::Iterator*
TemplateDictionary::CreateTemplateIterator(
    const TemplateString& section_name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->include_dict_) {
      IncludeDict::const_iterator it = d->include_dict_->find(
          section_name.GetGlobalId());
      if (it != d->include_dict_->end()) {
        // Found it!  Return it as an Iterator
        return MakeIterator(*it->second);
      }
    }
  }
  assert("Call IsHiddenTemplate before CreateTemplateIterator" == NULL);
  abort();
}

TemplateDictionaryInterface::Iterator*
TemplateDictionary::CreateSectionIterator(
    const TemplateString& section_name) const {
  for (const TemplateDictionary* d = this; d; d = d->parent_dict_) {
    if (d->section_dict_) {
      SectionDict::const_iterator it = d->section_dict_->find(
          section_name.GetGlobalId());
      if (it != d->section_dict_->end()) {
        // Found it!  Return it as an Iterator
        return MakeIterator(*it->second);
      }
    }
  }
  assert("Call IsHiddenSection before GetDictionaries" == NULL);
  abort();
}

_END_GOOGLE_NAMESPACE_
