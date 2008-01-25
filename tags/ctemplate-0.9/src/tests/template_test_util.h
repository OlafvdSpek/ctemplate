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
// Author: Jim Morrison

#ifndef TEMPLATE_TEMPLATE_TEST_UTIL_H__
#define TEMPLATE_TEMPLATE_TEST_UTIL_H__

#include "config_for_unittests.h"
#include <string>
#include HASH_MAP_H           // defined in config.h
#include <google/template_namelist.h>

_START_GOOGLE_NAMESPACE_

class TemplateDictionary;

class TemporaryRegisterTemplate {
 public:
  explicit TemporaryRegisterTemplate(const char* name);
  ~TemporaryRegisterTemplate();
 private:
  TemplateNamelist::NameListType* old_namelist_;
  TemplateNamelist::NameListType namelist_;

  // disallow copy constructor and assignment
  TemporaryRegisterTemplate(const TemporaryRegisterTemplate&);
  void operator=(const TemporaryRegisterTemplate&);
};

// This class is meant for use in unittests.  This class wraps the
// TemplateDictionary and provides access to internal data that should not
// be used in production code.

// Example Usage:
//  TemplateDictionary dict("test dictionary");
//  FillDictionaryValues(&dict);
//
//  TemplateDictionaryPeer peer(&dict);
//  EXPECT_EQ("5", peer.GetSectionValue("width"));
class TemplateDictionaryPeer {
 public:
  explicit TemplateDictionaryPeer(TemplateDictionary* dict) : dict_(dict) { }

  const char* GetSectionValue(const std::string& variable) const;

  bool IsHiddenSection(const std::string& name) const;

 private:
  TemplateDictionary* dict_;  // Not owned.

  // disallow copy constructor and assignment
  TemplateDictionaryPeer(const TemplateDictionaryPeer&);
  void operator=(const TemplateDictionaryPeer&);
};

_END_GOOGLE_NAMESPACE_

#endif  // TEMPLATE_TEMPLATE_TEST_UTIL_H__
