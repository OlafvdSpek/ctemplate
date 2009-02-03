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
// Most other tests use "config_for_unittests.h" to make testing easier.
// This brings in some code most users won't use.  This test is meant
// entirely to use ctemplate as users will, just #including the public
// .h files directly.  It does hardly any work, and is mainly intended
// as a compile check for the .h files.

// These are all the .h files that we export
#include <google/per_expand_data.h>
#include <google/template.h>
#include <google/template_dictionary.h>
#include <google/template_dictionary_interface.h>
#include <google/template_emitter.h>
#include <google/template_enums.h>
#include <google/template_from_string.h>
#include <google/template_modifiers.h>
#include <google/template_namelist.h>
#include <google/template_pathops.h>
#include <google/template_string.h>
#include <stdio.h>
#include <string>

int main() {
  google::Template* tpl = google::Template::StringToTemplate(
      "example", google::DO_NOT_STRIP, google::TC_MANUAL);
  google::TemplateDictionary dict("my dict");
  std::string nothing_will_come_of_nothing;
  tpl->Expand(&nothing_will_come_of_nothing, &dict);
  printf("PASS.\n");
  return 0;
}
