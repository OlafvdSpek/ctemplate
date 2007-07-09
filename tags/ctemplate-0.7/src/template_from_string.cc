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
// Author:   Frank H. Jernigan
//

#include "config.h"
#include "base/mutex.h"     // This must go first so we get _XOPEN_SOURCE
#include <assert.h>
#include <string>
#include HASH_MAP_H         // defined in config.h
#include <google/template_from_string.h>
#include <google/template.h>

_START_GOOGLE_NAMESPACE_

// Lock priority invariant: you should never acquire a
// TemplateFromString::mutex_ while holding this mutex.
// TODO(csilvers): assert this in the codebase.
static Mutex g_cache_mutex;

using std::string;
using std::pair;
using HASH_NAMESPACE::hash_map;

// TemplateFromString Constructor
// Calls its parent with an empty string for filename so the parent's
// constructor will not try to "load" the template from a file. Instead,
// the template text is taken from the second parameter. After that, the
// object is identical to a Template object, except that it cannot be
// "reloaded."
TemplateFromString::TemplateFromString(const string& cache_key,
                                       const string& template_text,
                                       Strip strip)
    : Template("", strip) {
  filename_ = cache_key;    // for cache and reporting purposes only

  // We know that InsertFile never writes more output than it gets input.
  // While we allocate buffer here, BuildTree takes ownership and deletes it.
  char* buffer = new char[template_text.size()];
  const int buflen = InsertFile(template_text.data(), template_text.size(),
                                buffer);
  if ( BuildTree(buffer, buffer + buflen) ) {
    assert(state() == TS_READY);
  } else {
    assert(state() != TS_READY);
  }
}

// This is the only function that references the cache, so we can put all
// the cache stuff locally here.

class TemplateCacheHash {
 public:
  HASH_NAMESPACE::hash<const char *> string_hash_;
  TemplateCacheHash() : string_hash_() {}
  size_t operator()(const pair<string, Strip>& p) const {
    // Using + here is silly, but should work ok in practice
    return string_hash_(p.first.c_str()) + static_cast<int>(p.second);
  }
  // Less operator for MSVC's hash containers.  We make Strip be the
  // primary key, unintuitively, because it's a bit faster.
  bool operator()(const pair<string, Strip>& a,
                  const pair<string, Strip>& b) const {
    return (a.second == b.second
            ? a.first < b.first
            : static_cast<int>(a.second) < static_cast<int>(b.second));
  }
  // These two public members are required by msvc.  4 and 8 are defaults.
  static const size_t bucket_size = 4;
  static const size_t min_buckets = 8;
};

// The template cache.  Note that we don't define a ClearCache() in this
// class, so there's no way to delete the entries from this cache!
typedef hash_map<pair<string, Strip>, TemplateFromString*, TemplateCacheHash>
  TemplateFromStringCache;

static TemplateFromStringCache *g_template_from_string_cache = NULL;


// TemplateFromString::GetTemplate
// Makes sure the template cache has been created and then tries to
// retrieve a TemplateFromString object from it via the cache_key.
TemplateFromString *TemplateFromString::GetTemplate(const string& cache_key,
                                                    const string& template_text,
                                                    Strip strip) {
  TemplateFromString *tpl = NULL;
  if (cache_key.empty()) {   // user doesn't want to use the cache
    tpl = new TemplateFromString(cache_key, template_text, strip);
  } else {
    MutexLock ml(&g_cache_mutex);
    if (g_template_from_string_cache == NULL) {
      g_template_from_string_cache = new TemplateFromStringCache;
    }
    // If the object isn't really a TemplateFromString this will be a cache miss
    tpl = (*g_template_from_string_cache)[pair<string,Strip>(cache_key, strip)];

    // If we didn't find one, then create one and store it in the cache
    if (!tpl) {
      tpl = new TemplateFromString(cache_key, template_text, strip);
      (*g_template_from_string_cache)[pair<string, Strip>(cache_key, strip)] =
          tpl;
    }
  }

  // TODO(csilvers): make sure this is safe and can never deadlock
  //WriterMutexLock ml(tpl->mutex_);   // to access state()

  // state_ can be TS_SHOULD_RELOAD if ReloadAllIfChanged() touched this
  // file That's fine; we'll just ignore the reload directive for this guy.
  if (tpl->state() == TS_SHOULD_RELOAD)
    tpl->set_state(TS_READY);

  // if the statis is not TS_READY, then it is TS_ERROR at this
  // point. If it is TS_ERROR, we leave the state as is,
  // but return NULL. TS_ERROR means there was a syntax error in the
  // string parsed (template_text).
  if (tpl->state() != TS_READY) {
    return NULL;
  } else {
    return tpl;
  }
}

// InvalidMethodCall
// A static function yank the developer's chain
static void InvalidMethodCall(const char* method_name) {
  assert(false);  // Can't call this method on TemplateFromString.
}

// These last three methods merely call attention to the developer's error
// Since they are private, the compiler will detect such calls everywhere
// except in modifications to the other methods in this class. It is
// therefore very unlikely these will ever be called.
Template *TemplateFromString::GetTemplate(const string& filename, Strip strip) {
  InvalidMethodCall("GetTemplate");
  return NULL;
}

bool TemplateFromString::ReloadIfChanged() {
  InvalidMethodCall("ReloadIfChanged");
  return false;
}

void TemplateFromString::Reload() {
  InvalidMethodCall("Reload");
}

_END_GOOGLE_NAMESPACE_
