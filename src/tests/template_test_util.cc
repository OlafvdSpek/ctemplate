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

#include "config_for_unittests.h"
#include "tests/template_test_util.h"
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_namelist.h>
#include <ctemplate/template_string.h>

#include <string>
#include <vector>

using std::string;
using std::vector;

_START_GOOGLE_NAMESPACE_

TemporaryRegisterTemplate::TemporaryRegisterTemplate(const char* name) {
  old_namelist_ = TemplateNamelist::namelist_;
  if (old_namelist_) {
    namelist_ = *old_namelist_;
  }

  namelist_.insert(name);
  TemplateNamelist::namelist_ = &namelist_;
}

TemporaryRegisterTemplate::~TemporaryRegisterTemplate() {
  TemplateNamelist::namelist_ = old_namelist_;
}

const char* TemplateDictionaryPeer::GetSectionValue(
    const TemplateString& variable)
    const {
  return dict_->GetSectionValue(variable);
}

bool TemplateDictionaryPeer::IsHiddenSection(
    const TemplateString& name) const {
  return dict_->IsHiddenSection(name);
}

bool TemplateDictionaryPeer::IsHiddenTemplate(
    const TemplateString& name) const {
  return dict_->IsHiddenTemplate(name);
}

int TemplateDictionaryPeer::GetSectionDictionaries(
    const TemplateString& section_name,
    vector<const TemplateDictionary*>* dicts) const {
  dicts->clear();
  if (dict_->IsHiddenSection(section_name))
    return 0;

  TemplateDictionaryInterface::Iterator* di =
      dict_->CreateSectionIterator(section_name);
  while (di->HasNext())
    dicts->push_back(static_cast<const TemplateDictionary*>(&di->Next()));
  delete di;

  return static_cast<int>(dicts->size());
}

int TemplateDictionaryPeer::GetIncludeDictionaries(
    const TemplateString& section_name,
    vector<const TemplateDictionary*>* dicts) const {
  dicts->clear();
  if (dict_->IsHiddenTemplate(section_name))
    return 0;

  TemplateDictionaryInterface::Iterator* di =
      dict_->CreateTemplateIterator(section_name);
  while (di->HasNext())
    dicts->push_back(static_cast<const TemplateDictionary*>(&di->Next()));
  delete di;

  return static_cast<int>(dicts->size());
}

const char* TemplateDictionaryPeer::GetIncludeTemplateName(
    const TemplateString& variable, int dictnum) const {
  return dict_->GetIncludeTemplateName(variable, dictnum);
}

const char* TemplateDictionaryPeer::GetFilename() const {
  return dict_->filename_;
}

_END_GOOGLE_NAMESPACE_
