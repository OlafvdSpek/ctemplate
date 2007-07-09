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

#include "config.h"
// This is for windows.  Even though we #include config.h, just like
// the files used to compile the dll, we are actually a *client* of
// the dll, so we don't get to decl anything.
#undef CTEMPLATE_DLL_DECL

#include "tests/template_test_util.h"
#include <google/template_dictionary.h>
#include <google/template_namelist.h>

using std::string;

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

const char* TemplateDictionaryPeer::GetSectionValue(const string& variable)
    const {
  return dict_->GetSectionValue(variable);
}

bool TemplateDictionaryPeer::IsHiddenSection(const string& name) const {
  return dict_->IsHiddenSection(name);
}

_END_GOOGLE_NAMESPACE_
