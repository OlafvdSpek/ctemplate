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
// This steals a lot of ideas from template_unittest.cc, but mostly
// checks the functions that differ between Template and
// TemplateFromString.  A lot of the testing, including end-to-end
// testing, is done in template_regtest.cc.

#include "config_for_unittests.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <vector>
#include <string>
#include <algorithm>      // for sort
#include <sys/types.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>       // for readdir
#endif
#include <google/template_from_string.h>
#include <google/template_dictionary.h>

using std::vector;
using std::string;
using GOOGLE_NAMESPACE::Template;
using GOOGLE_NAMESPACE::TemplateFromString;
using GOOGLE_NAMESPACE::TemplateDictionary;
using GOOGLE_NAMESPACE::Strip;
using GOOGLE_NAMESPACE::DO_NOT_STRIP;
using GOOGLE_NAMESPACE::STRIP_BLANK_LINES;
using GOOGLE_NAMESPACE::STRIP_WHITESPACE;

#define ASSERT(cond)  do {                                      \
  if (!(cond)) {                                                \
    printf("ASSERT FAILED, line %d: %s\n", __LINE__, #cond);    \
    assert(cond);                                               \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define ASSERT_STREQ(a, b)                 ASSERT(strcmp(a, b) == 0)

static Template* StringToTemplate(const string& s, Strip strip) {
  static int filenum = 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%03d", ++filenum);   // unique name for each call
  return TemplateFromString::GetTemplate(buf, s, strip);
}

// This is esp. useful for calling from within gdb.
// The gdb nice-ness is balanced by the need for the caller to delete the buf.
static const char* ExpandIs(Template* tpl, TemplateDictionary *dict) {
  string outstring;
  tpl->Expand(&outstring, dict);
  char* buf = new char [outstring.size()+1];
  strcpy(buf, outstring.c_str());
  return buf;
}

static void AssertExpandIs(Template* tpl, TemplateDictionary *dict,
                           const string& is) {
  const char* buf = ExpandIs(tpl, dict);
  ASSERT_STREQ(buf, is.c_str());
  delete [] buf;
}


// This is all in a single class just to make friendship easier:
// the TemplateFromStringUnittest class can be listed as a friend
// once, and access all the internals of TemplateFromString.
class TemplateFromStringUnittest {
 public:

  // The following tests test various aspects of how Expand() should behave.
  static void TestVariable() {
    Template* tpl = StringToTemplate("hi {{VAR}} lo", STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "hi  lo");
    dict.SetValue("VAR", "yo");
    AssertExpandIs(tpl, &dict, "hi yo lo");
    dict.SetValue("VAR", "yoyo");
    AssertExpandIs(tpl, &dict, "hi yoyo lo");
    dict.SetValue("VA", "noyo");
    dict.SetValue("VAR ", "noyo2");
    dict.SetValue("var", "noyo3");
    AssertExpandIs(tpl, &dict, "hi yoyo lo");
  }

  static void TestSection() {
    Template* tpl = StringToTemplate("boo!\nhi {{#SEC}}lo{{/SEC}} bar",
                                     STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "boo!hi  bar");
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "boo!hi lo bar");

    TemplateDictionary dict2("dict2");
    dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lo bar");
    dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lolo bar");
    dict2.AddSectionDictionary("sec");
    AssertExpandIs(tpl, &dict2, "boo!hi lolo bar");
  }

  static void TestGetTemplate() {
    TemplateDictionary dict("dict");

    // Tests the cache
    const char* const tpltext = "{This is perfectly valid} yay!";
    const char* const tpltext2 = "This will be ignored";
    Template* tpl1 = TemplateFromString::GetTemplate(
        "tgt", tpltext, DO_NOT_STRIP);
    Template* tpl2 = TemplateFromString::GetTemplate(
        string("tgt"), tpltext, DO_NOT_STRIP);
    Template* tpl3 = TemplateFromString::GetTemplate(
        string("tgt"), tpltext, STRIP_WHITESPACE);
    Template* tpl4 = TemplateFromString::GetTemplate(
        string("tgt"), tpltext2, STRIP_WHITESPACE);
    ASSERT(tpl1 && tpl2 && tpl3 && tpl4);
    ASSERT(tpl1 == tpl2);
    ASSERT(tpl3 == tpl4);
    ASSERT(tpl1 != tpl3);
    AssertExpandIs(tpl1, &dict, tpltext);
    AssertExpandIs(tpl2, &dict, tpltext);
    AssertExpandIs(tpl3, &dict, tpltext);
    AssertExpandIs(tpl4, &dict, tpltext);

    // Tests our mechanism for ignoring the cache (first arg is empty-string)
    Template* tpl1b = TemplateFromString::GetTemplate(
        "", tpltext, DO_NOT_STRIP);
    Template* tpl2b = TemplateFromString::GetTemplate(
        "", tpltext2, DO_NOT_STRIP);
    ASSERT(tpl1b != tpl2b);
    AssertExpandIs(tpl1b, &dict, tpltext);
    AssertExpandIs(tpl2b, &dict, tpltext2);
    // When you don't cache the template, you have to delete it!
    delete tpl1b;
    delete tpl2b;

    // Tests that syntax errors cause us to return NULL
    Template* tpl5 = StringToTemplate("{{This has spaces in it}}", DO_NOT_STRIP);
    ASSERT(!tpl5);
    Template* tpl6 = StringToTemplate("{{#SEC}}foo", DO_NOT_STRIP);
    ASSERT(!tpl6);
    Template* tpl7 = StringToTemplate("{{#S1}}foo{{/S2}}", DO_NOT_STRIP);
    ASSERT(!tpl7);
    Template* tpl8 = StringToTemplate("{{#S1}}foo{{#S2}}bar{{/S1}{{/S2}",
                                      DO_NOT_STRIP);
    ASSERT(!tpl8);
    Template* tpl9 = StringToTemplate("{{noend", DO_NOT_STRIP);
    ASSERT(!tpl9);
  }

  // Tests that the various strip values all do the expected thing.
  // This also tests TemplateFromString's parsing of newlines in its
  // input.
  static void TestStrip() {
    TemplateDictionary dict("dict");
    dict.SetValue("FOO", "foo");

    const char* tests[][4] = {  // 0: in, 1: do-not-strip, 2: blanklines, 3: ws
      {"hi!\n", "hi!\n", "hi!\n", "hi!"},
      {"hi!", "hi!", "hi!", "hi!"},
      // These test strip-blank-lines, primarily
      {"{{FOO}}\n\n{{FOO}}", "foo\n\nfoo", "foo\nfoo", "foofoo"},
      {"{{FOO}}\r\n\r\n{{FOO}}", "foo\r\n\r\nfoo", "foo\r\nfoo", "foofoo"},
      {"{{FOO}}\n   \n{{FOO}}\n", "foo\n   \nfoo\n", "foo\nfoo\n", "foofoo"},
      {"{{FOO}}\n{{BI_NEWLINE}}\nb", "foo\n\n\nb", "foo\n\n\nb", "foo\nb"},
      // These test strip-whitespace
      {"foo\nbar\n", "foo\nbar\n", "foo\nbar\n", "foobar"},
      {"{{FOO}}\nbar\n", "foo\nbar\n", "foo\nbar\n", "foobar"},
      {"  {{FOO}}  {{!comment}}\nb", "  foo  \nb", "  foo  \nb", "foo  b"},
      {"  {{FOO}}  {{BI_SPACE}}\n", "  foo   \n", "  foo   \n", "foo   "},
      {"  \t \f\v  \n\r\n  ", "  \t \f\v  \n\r\n  ", "", ""},
    };

    for (int i = 0; i < sizeof(tests)/sizeof(*tests); ++i) {
      Template* tpl1 = StringToTemplate(tests[i][0], DO_NOT_STRIP);
      Template* tpl2 = StringToTemplate(tests[i][0], STRIP_BLANK_LINES);
      Template* tpl3 = StringToTemplate(tests[i][0], STRIP_WHITESPACE);
      AssertExpandIs(tpl1, &dict, tests[i][1]);
      AssertExpandIs(tpl2, &dict, tests[i][2]);
      AssertExpandIs(tpl3, &dict, tests[i][3]);
    }
  }

};

int main(int argc, char** argv) {
  TemplateFromStringUnittest::TestVariable();
  TemplateFromStringUnittest::TestSection();
  TemplateFromStringUnittest::TestGetTemplate();
  TemplateFromStringUnittest::TestStrip();

  printf("DONE\n");
  return 0;
}
