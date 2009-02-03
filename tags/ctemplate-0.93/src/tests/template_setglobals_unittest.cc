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

#include "config_for_unittests.h"
#include <assert.h>
#include <stdio.h>
#include <google/template.h>
#include <google/template_dictionary.h>

#define ASSERT(cond)  do {                                      \
  if (!(cond)) {                                                \
    printf("ASSERT FAILED, line %d: '" #cond "'\n", __LINE__);  \
    assert(cond);                                               \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define ASSERT_STREQ(a, b)  do {                                          \
  if (strcmp((a), (b))) {                                                 \
    printf("ASSERT FAILED, line %d: '%s' != '%s'\n", __LINE__, (a), (b)); \
    assert(!strcmp((a), (b)));                                            \
    exit(1);                                                              \
  }                                                                       \
} while (0)

_START_GOOGLE_NAMESPACE_

// This is just to make friendship easier
class TemplateSetGlobalsUnittest {
 public:
  static void TestTemplateDictionarySetGlobalValue() {
    // Test to see that the global dictionary object gets created when you
    // first call the static function TemplateDictionary::SetGlobalValue().
    TemplateDictionary::SetGlobalValue("TEST_GLOBAL_VAR", "test_value");
    TemplateDictionary tpl("empty");
    ASSERT_STREQ(tpl.GetSectionValue("TEST_GLOBAL_VAR"),
                "test_value");
  }

  static void TestTemplateSetRootDirectory() {
    // Test to see that the Template static variables get created when you
    // first call the static function Template::SetRootDirectory().
    Template::SetTemplateRootDirectory("/some/directory/path");
    // We don't know if we appended a / or a \, so we test indirectly
    ASSERT(Template::template_root_directory().size() ==
           strlen("/some/directory/path")+1);   // assert they added a char
    ASSERT(memcmp(Template::template_root_directory().c_str(),
                  "/some/directory/path",
                  strlen("/some/directory/path")) == 0);
  }
};

_END_GOOGLE_NAMESPACE_

using GOOGLE_NAMESPACE::TemplateSetGlobalsUnittest;

int main(int argc, char **argv) {
  TemplateSetGlobalsUnittest::TestTemplateDictionarySetGlobalValue();
  TemplateSetGlobalsUnittest::TestTemplateSetRootDirectory();

  printf("PASS\n");
  return 0;
}
