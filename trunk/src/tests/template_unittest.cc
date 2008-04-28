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

#include "config_for_unittests.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>     // for mkdir
#if defined(HAVE_PTHREAD) && !defined(NO_THREADS)
#include <pthread.h>
#endif
#ifdef HAVE_DIRENT_H
# include <dirent.h>      // for readdir
#else
# define dirent direct
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif
#include <string>
#include <vector>         // for MissingListType, SyntaxListType
#include HASH_SET_H       // (defined in config.h)  for NameListType
#include "google/template.h"
#include "google/template_pathops.h"
#include "google/template_emitter.h"
#include "google/template_dictionary.h"
#include "google/template_modifiers.h"
#include "google/template_namelist.h"

using std::vector;
using std::string;
using HASH_NAMESPACE::hash_set;
using GOOGLE_NAMESPACE::ExpandEmitter;
using GOOGLE_NAMESPACE::Template;
using GOOGLE_NAMESPACE::TemplateDictionary;
using GOOGLE_NAMESPACE::TemplateNamelist;
using GOOGLE_NAMESPACE::TemplateContext;
using GOOGLE_NAMESPACE::Strip;
using GOOGLE_NAMESPACE::DO_NOT_STRIP;
using GOOGLE_NAMESPACE::STRIP_BLANK_LINES;
using GOOGLE_NAMESPACE::STRIP_WHITESPACE;
using GOOGLE_NAMESPACE::TC_HTML;
using GOOGLE_NAMESPACE::TC_JS;
using GOOGLE_NAMESPACE::TC_JSON;
using GOOGLE_NAMESPACE::TC_XML;
using GOOGLE_NAMESPACE::TC_NONE;
namespace ctemplate = GOOGLE_NAMESPACE::ctemplate;   // an interior namespace
namespace template_modifiers = GOOGLE_NAMESPACE::template_modifiers;

// How many threads to use for our threading test.
// This is a #define instead of a const int so we can use it in array-sizes
// even on c++ compilers that don't support var-length arrays.
#define kNumThreads  10

#define PFATAL(s)  do { perror(s); exit(1); } while (0)

#define ASSERT(cond)  do {                                      \
  if (!(cond)) {                                                \
    printf("ASSERT FAILED, line %d: %s\n", __LINE__, #cond);    \
    assert(cond);                                               \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define ASSERT_STREQ_EXCEPT(a, b, except)  ASSERT(StreqExcept(a, b, except))
#define ASSERT_STREQ(a, b)                 ASSERT(strcmp(a, b) == 0)
#define ASSERT_NOT_STREQ(a, b)             ASSERT(strcmp(a, b) != 0)
#define ASSERT_STREQ_VERBOSE(a, b, c)      ASSERT(StrEqVerbose(a, b, c))

// First, (conceptually) remove all chars in "except" from both a and b.
// Then return true iff munged_a == munged_b.
bool StreqExcept(const char* a, const char* b, const char* except) {
  const char* pa = a, *pb = b;
  const size_t exceptlen = strlen(except);
  while (1) {
    // Use memchr isntead of strchr because memchr(foo, '\0') always fails
    while (memchr(except, *pa, exceptlen))  pa++;   // ignore "except" chars in a
    while (memchr(except, *pb, exceptlen))  pb++;   // ignore "except" chars in b
    if ((*pa == '\0') && (*pb == '\0'))
      return true;
    if (*pa++ != *pb++)                  // includes case where one is at \0
      return false;
  }
}

// If a and b do not match, print their values and that of text
// and return false.
static bool StrEqVerbose(const string& a, const string& b,
                         const string& text) {
  if (a != b) {
    printf("EXPECTED: %s\n", a.c_str());
    printf("ACTUAL: %s\n", b.c_str());
    printf("TEXT: %s\n", text.c_str());
    return false;
  }
  return true;
}

RegisterTemplateFilename(VALID1_FN, "template_unittest_test_valid1.in");
RegisterTemplateFilename(INVALID1_FN, "template_unittest_test_invalid1.in");
RegisterTemplateFilename(INVALID2_FN, "template_unittest_test_invalid2.in");
RegisterTemplateFilename(NONEXISTENT_FN, "nonexistent__file.tpl");

// deletes all files named *template* in dir
#ifndef WIN32   // windows will define its own version, in src/windows/port.cc
static void CleanTestDir(const string& dirname) {
  DIR* dir = opendir(dirname.c_str());
  if (!dir) {  // directory doesn't exist or something like that.
    if (errno == ENOENT)   // if dir doesn't exist, try to make it
      ASSERT(mkdir(dirname.c_str(), 0755) == 0);
    return;
  }
  while (struct dirent* d = readdir(dir)) {
    if (strstr(d->d_name, "template"))
      unlink(ctemplate::PathJoin(dirname, d->d_name).c_str());
  }
  closedir(dir);
}

static string TmpFile(const char* basename) {
  return string("/tmp/") + basename;
}
#endif

static const string FLAGS_test_tmpdir(TmpFile("template_unittest_dir"));


// This writes s to the given file
static void StringToFile(const string& s, const string& filename) {
  FILE* fp = fopen(filename.c_str(), "wb");
  ASSERT(fp);
  size_t r = fwrite(s.data(), 1, s.length(), fp);
  ASSERT(r == s.length());
  fclose(fp);
}

// This writes s to a file and returns the filename.
static string StringToTemplateFile(const string& s) {
  static int filenum = 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%03d", ++filenum);
  string filename = ctemplate::PathJoin(FLAGS_test_tmpdir,
                                        string("template.") + buf);
  StringToFile(s, filename);
  return filename;
}

// This writes s to a file and then loads it into a template object.
static Template* StringToTemplate(const string& s, Strip strip) {
  return Template::GetTemplate(StringToTemplateFile(s), strip);
}

// This writes s to a file and then loads it into a template object.
static Template* StringToTemplateWithAutoEscaping(const string& s,
                                                  Strip strip,
                                                  TemplateContext context) {
  return Template::GetTemplateWithAutoEscaping(StringToTemplateFile(s), strip,
                                               context);
}

// This is esp. useful for calling from within gdb.
// The gdb nice-ness is balanced by the need for the caller to delete the buf.

static const char* ExpandIs(Template* tpl, TemplateDictionary *dict,
                            bool expected) {
  string outstring;
  ASSERT(expected == tpl->Expand(&outstring, dict));

  char* buf = new char [outstring.size()+1];
  strcpy(buf, outstring.c_str());
  return buf;
}

static void AssertExpandIs(Template* tpl, TemplateDictionary *dict,
                           const string& is, bool expected) {
  const char* buf = ExpandIs(tpl, dict, expected);
  if (strcmp(buf, is.c_str())) {
    printf("expected = '%s'\n", is.c_str());
    printf("actual   = '%s'\n", buf);
  }
  ASSERT_STREQ(buf, is.c_str());
  delete [] buf;
}

// A helper method used by TestCorrectModifiersForAutoEscape.
// Populates out with lines of the form:
// VARNAME:mod1[=val1][:mod2[=val2]]...\n from the dump of the template
// and compares against the expected string.
// The template is initialized in Auto Escape mode in the given
// TemplateContext.
static void AssertCorrectModifiers(TemplateContext template_type,
                                   const string& text,
                                   const string& expected_out) {
  Strip strip = STRIP_WHITESPACE;
  Template *tpl = StringToTemplateWithAutoEscaping(text, strip, template_type);
  string dump_out, out;
  tpl->DumpToString("bogus_filename", &dump_out);
  string::size_type i, j;
  i = 0;
  while ((i = dump_out.find("Variable Node: ", i)) != string::npos) {
    i += strlen("Variable Node: ");
    j = dump_out.find("\n", i);
    out.append(dump_out.substr(i, j - i));   // should be safe.
    out.append("\n");
  }
  ASSERT_STREQ_VERBOSE(expected_out, out, text);
}

// A helper method used by TestCorrectModifiersForAutoEscape.
// Initializes the template in the Auto Escape mode with the
// given TemplateContext, expands it with the given dictionary
// and checks that the output matches the expected value.
static void AssertCorrectEscaping(TemplateContext template_type,
                                  const TemplateDictionary& dict,
                                  const string& text,
                                  const string& expected_out) {
  Strip strip = STRIP_WHITESPACE;
  Template *tpl = StringToTemplateWithAutoEscaping(text, strip, template_type);
  string outstring;
  tpl->Expand(&outstring, &dict);
  ASSERT_STREQ_VERBOSE(expected_out, outstring, text);
}

class DynamicModifier : public template_modifiers::TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const template_modifiers::ModifierData* per_expand_data,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
    assert(per_expand_data);
    const char* value = per_expand_data->LookupAsString("value");
    if (value)
      outbuf->Emit(value);
  }
};

// This is all in a single class just to make friendship easier:
// the TemplateUnittest class can be listed as a friend
// once, and access all the internals of Template.
class TemplateUnittest {
 public:

  // The following tests test various aspects of how Expand() should behave.
  static void TestVariable() {
    Template* tpl = StringToTemplate("hi {{VAR}} lo", STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "hi  lo", true);
    dict.SetValue("VAR", "yo");
    AssertExpandIs(tpl, &dict, "hi yo lo", true);
    dict.SetValue("VAR", "yoyo");
    AssertExpandIs(tpl, &dict, "hi yoyo lo", true);
    dict.SetValue("VA", "noyo");
    dict.SetValue("VAR ", "noyo2");
    dict.SetValue("var", "noyo3");
    AssertExpandIs(tpl, &dict, "hi yoyo lo", true);
  }

  static void TestVariableWithModifiers() {
    Template* tpl = StringToTemplate("hi {{VAR:html_escape}} lo",
                                     STRIP_WHITESPACE);
    TemplateDictionary dict("dict");

    // Test with no modifiers.
    dict.SetValue("VAR", "yo");
    AssertExpandIs(tpl, &dict, "hi yo lo", true);
    dict.SetValue("VAR", "yo&yo");
    AssertExpandIs(tpl, &dict, "hi yo&amp;yo lo", true);

    // Test with URL escaping.
    tpl = StringToTemplate("<a href=\"/servlet?param={{VAR:u}}\">",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "<a href=\"/servlet?param=yo%26yo\">", true);
    tpl = StringToTemplate("<a href='/servlet?param={{VAR:url_query_escape}}'>",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "<a href='/servlet?param=yo%26yo'>", true);

    // Test with multiple URL escaping.
    tpl = StringToTemplate("<a href=\"/servlet?param={{VAR:u:u}}\">",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "<a href=\"/servlet?param=yo%2526yo\">", true);

    // Test HTML escaping.
    tpl = StringToTemplate("hi {{VAR:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&amp;yo lo", true);

    tpl = StringToTemplate("hi {{VAR:h:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&amp;amp;yo lo", true);

    // Test special HTML escaping
    dict.SetValue("URL_VAR", "javascript:void");
    dict.SetValue("SNIPPET_VAR", "<b>foo & bar</b>");
    tpl = StringToTemplate("hi {{VAR:H=attribute}} {{URL_VAR:H=url}} "
                           "{{SNIPPET_VAR:H=snippet}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo_yo # <b>foo & bar</b> lo", true);

    // Test with custom modifier
    ASSERT(template_modifiers::AddModifier("x-test",
                                           &template_modifiers::html_escape));
    ASSERT(template_modifiers::AddModifier("x-test-arg=",
                                           &template_modifiers::html_escape));
    ASSERT(template_modifiers::AddModifier("x-test-arg=snippet",
                                           &template_modifiers::snippet_escape));

    tpl = StringToTemplate("hi {{VAR:x-test}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&amp;yo lo", true);
    tpl = StringToTemplate("hi {{SNIPPET_VAR:x-test-arg=snippet}} lo",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi <b>foo & bar</b> lo", true);
    tpl = StringToTemplate("hi {{VAR:x-unknown}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&yo lo", true);

    // Test with a modifier taking per-expand data
    DynamicModifier dynamic_modifier;
    ASSERT(template_modifiers::AddModifier("x-dynamic", &dynamic_modifier));
    tpl = StringToTemplate("hi {{VAR:x-dynamic}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi  lo", true);
    dict.SetModifierData("value", "foo");
    AssertExpandIs(tpl, &dict, "hi foo lo", true);
    dict.SetModifierData("value", "bar");
    AssertExpandIs(tpl, &dict, "hi bar lo", true);
    dict.SetModifierData("value", NULL);
    AssertExpandIs(tpl, &dict, "hi  lo", true);


    // Test with no modifiers.
    tpl = StringToTemplate("hi {{VAR}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&yo lo", true);

    // Check that ordering is right
    dict.SetValue("VAR", "yo\nyo");
    tpl = StringToTemplate("hi {{VAR:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo yo lo", true);
    tpl = StringToTemplate("hi {{VAR:p}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\nyo lo", true);
    tpl = StringToTemplate("hi {{VAR:j}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo", true);
    tpl = StringToTemplate("hi {{VAR:h:j}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo yo lo", true);
    tpl = StringToTemplate("hi {{VAR:j:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo", true);

    // Check more complicated modifiers using fullname
    tpl = StringToTemplate("hi {{VAR:javascript_escape:h}} lo",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo", true);
    tpl = StringToTemplate("hi {{VAR:j:html_escape}} lo",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo", true);
    tpl = StringToTemplate("hi {{VAR:pre_escape:j}} lo",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo", true);

    // Check that illegal modifiers are rejected
    tpl = StringToTemplate("hi {{VAR:j:h2}} lo", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
    tpl = StringToTemplate("hi {{VAR:html_ecap}} lo", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
    tpl = StringToTemplate("hi {{VAR:javascript_escaper}} lo",
                           STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
    tpl = StringToTemplate("hi {{VAR:js:j}} lo", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
    tpl = StringToTemplate("hi {{VAR:}} lo", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);

    // Check we reject modifier-values when we ought to
    tpl = StringToTemplate("hi {{VAR:j=4}} lo", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
    tpl = StringToTemplate("hi {{VAR:html_escape=yes}} lo", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
    tpl = StringToTemplate("hi {{VAR:url_query_escape=wombats}} lo",
                           STRIP_WHITESPACE);
    ASSERT(tpl == NULL);

    // Check we don't allow modifiers on sections
    tpl = StringToTemplate("hi {{#VAR:h}} lo {{/VAR}}", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);

    // Test when expanded grows by more than 12% per modifier.
    dict.SetValue("VAR", "http://a.com?b=c&d=e&f=g&q=a>b");
    tpl = StringToTemplate("{{VAR:u:j:h}}",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict,
                   "http%3A//a.com%3Fb%3Dc%26d%3De%26f%3Dg%26q%3Da%3Eb",
                   true);

    // As above with 4 modifiers.
    dict.SetValue("VAR", "http://a.com?b=c&d=e&f=g&q=a>b");
    tpl = StringToTemplate("{{VAR:u:j:h:h}}",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict,
                   "http%3A//a.com%3Fb%3Dc%26d%3De%26f%3Dg%26q%3Da%3Eb",
                   true);


  }

  static void TestSection() {
    Template* tpl = StringToTemplate(
        "boo!\nhi {{#SEC}}lo{{#SUBSEC}}jo{{/SUBSEC}}{{/SEC}} bar",
        STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "boo!hi  bar", true);
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "boo!hi lo bar", true);
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "boo!hi lo bar", true);
    // This should work even though subsec isn't a child of the main dict
    dict.ShowSection("SUBSEC");
    AssertExpandIs(tpl, &dict, "boo!hi lojo bar", true);

    TemplateDictionary dict2("dict2");
    dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lo bar", true);
    dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lolo bar", true);
    dict2.AddSectionDictionary("sec");
    AssertExpandIs(tpl, &dict2, "boo!hi lolo bar", true);
    dict2.ShowSection("SUBSEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lojolojo bar", true);
  }

  static void TestInclude() {
    string incname = StringToTemplateFile("include file\n");
    string incname2 = StringToTemplateFile("inc2a\ninc2b\n");
    string incname_bad = StringToTemplateFile("{{syntax_error");
    Template* tpl = StringToTemplate("hi {{>INC}} bar\n", STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "hi  bar", true);
    dict.AddIncludeDictionary("INC");
    AssertExpandIs(tpl, &dict, "hi  bar", true);   // noop: no filename was set
    dict.AddIncludeDictionary("INC")->SetFilename("/notarealfile ");
    AssertExpandIs(tpl, &dict, "hi  bar", false);   // noop: illegal filename
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl, &dict, "hi include file bar", false);
    dict.AddIncludeDictionary("INC")->SetFilename(incname_bad);
    AssertExpandIs(tpl, &dict, "hi include file bar",
                   false);  // noop: syntax error
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl, &dict, "hi include fileinclude file bar", false);
    dict.AddIncludeDictionary("inc")->SetFilename(incname);
    AssertExpandIs(tpl, &dict, "hi include fileinclude file bar", false);
    dict.AddIncludeDictionary("INC")->SetFilename(incname2);
    AssertExpandIs(tpl, &dict,
                   "hi include fileinclude fileinc2ainc2b bar", false);

    // Now test that includes preserve Strip
    Template* tpl2 = StringToTemplate("hi {{>INC}} bar", DO_NOT_STRIP);
    AssertExpandIs(tpl2, &dict,
                   "hi include file\ninclude file\ninc2a\ninc2b\n bar", false);

    // Test that if we indent the include, every line on the include
    // is indented.
    Template* tpl3 = StringToTemplate("hi\n  {{>INC}} bar", DO_NOT_STRIP);
    AssertExpandIs(tpl3, &dict,
                   "hi\n  include file\n  include file\n"
                   "  inc2a\n  inc2b\n   bar",
                   false);
    // But obviously, if we strip leading whitespace, no indentation.
    Template* tpl4 = StringToTemplate("hi\n  {{>INC}} bar", STRIP_WHITESPACE);
    AssertExpandIs(tpl4, &dict,
                   "hiinclude fileinclude fileinc2ainc2b bar", false);
    // And if it's not a whitespace indent, we don't indent either.
    Template* tpl5 = StringToTemplate("hi\n - {{>INC}} bar", DO_NOT_STRIP);
    AssertExpandIs(tpl5, &dict,
                   "hi\n - include file\ninclude file\n"
                   "inc2a\ninc2b\n bar",
                   false);
    // Make sure we indent properly at the beginning.
    Template* tpl6 = StringToTemplate("  {{>INC}}\nbar", DO_NOT_STRIP);
    AssertExpandIs(tpl6, &dict,
                   "  include file\n  include file\n"
                   "  inc2a\n  inc2b\n  \nbar",
                   false);
    // And deal correctly when we include twice in a row.
    Template* tpl7 = StringToTemplate("  {{>INC}}-{{>INC}}", DO_NOT_STRIP);
    AssertExpandIs(tpl7, &dict,
                   "  include file\n  include file\n  inc2a\n  inc2b\n  "
                   "-include file\ninclude file\ninc2a\ninc2b\n",
                   false);

  }

  static void TestIncludeWithModifiers() {
    string incname = StringToTemplateFile("include & print file\n");
    string incname2 = StringToTemplateFile("inc2\n");
    string incname3 = StringToTemplateFile("yo&yo");
    // Note this also tests that html-escape, but not javascript-escape or
    // pre-escape, escapes \n to <space>
    Template* tpl1 = StringToTemplate("hi {{>INC:h}} bar\n", DO_NOT_STRIP);
    Template* tpl2 = StringToTemplate("hi {{>INC:javascript_escape}} bar\n",
                                      DO_NOT_STRIP);
    Template* tpl3 = StringToTemplate("hi {{>INC:pre_escape}} bar\n",
                                      DO_NOT_STRIP);
    Template* tpl4 = StringToTemplate("hi {{>INC:u}} bar\n", DO_NOT_STRIP);
    // Test that if we include the same template twice, once with a modifer
    // and once without, they each get applied properly.
    Template* tpl5 = StringToTemplate("hi {{>INC:h}} bar {{>INC}} baz\n",
                                      DO_NOT_STRIP);

    TemplateDictionary dict("dict");
    AssertExpandIs(tpl1, &dict, "hi  bar\n", true);
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl1, &dict, "hi include &amp; print file  bar\n", true);
    dict.AddIncludeDictionary("INC")->SetFilename(incname2);
    AssertExpandIs(tpl1, &dict, "hi include &amp; print file inc2  bar\n",
                   true);
    AssertExpandIs(tpl2, &dict, "hi include \\x26 print file\\ninc2\\n bar\n",
                   true);
    AssertExpandIs(tpl3, &dict, "hi include &amp; print file\ninc2\n bar\n",
                   true);
    dict.AddIncludeDictionary("INC")->SetFilename(incname3);
    AssertExpandIs(tpl4, &dict,
                   "hi include+%26+print+file%0Ainc2%0Ayo%26yo bar\n",
                   true);
    AssertExpandIs(tpl5, &dict,
                   "hi include &amp; print file inc2 yo&amp;yo bar "
                   "include & print file\ninc2\nyo&yo baz\n",
                   true);

    // Don't test modifier syntax here; that's in TestVariableWithModifiers()
  }

  // Make sure we don't deadlock when a template includes itself.
  // This also tests we handle recursive indentation properly.
  static void TestRecursiveInclude() {
    string incname = StringToTemplateFile("hi {{>INC}} bar\n  {{>INC}}!");
    Template* tpl = Template::GetTemplate(incname, DO_NOT_STRIP);
    TemplateDictionary dict("dict");
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    // Note the last line is indented 4 spaces instead of 2.  This is
    // because the last sub-include is indented.
    AssertExpandIs(tpl, &dict, "hi hi  bar\n  ! bar\n  hi  bar\n    !!", true);
  }

  // Tests that vars inherit/override their parents properly
  static void TestInheritence() {
    Template* tpl = StringToTemplate("{{FOO}}{{#SEC}}{{FOO}}{{#SEC}}{{FOO}}{{/SEC}}{{/SEC}}",
                                     STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    dict.SetValue("FOO", "foo");
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "foofoofoo", true);

    TemplateDictionary dict2("dict2");
    dict2.SetValue("FOO", "foo");
    TemplateDictionary* sec = dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "foofoofoo", true);
    sec->SetValue("FOO", "bar");
    AssertExpandIs(tpl, &dict2, "foobarbar", true);
    TemplateDictionary* sec2 = sec->AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "foobarbar", true);
    sec2->SetValue("FOO", "baz");
    AssertExpandIs(tpl, &dict2, "foobarbaz", true);

    // Now test an include template, which shouldn't inherit from its parents
    tpl = StringToTemplate("{{FOO}}{{#SEC}}hi{{/SEC}}\n{{>INC}}",
                           STRIP_WHITESPACE);
    string incname = StringToTemplateFile(
        "include {{FOO}}{{#SEC}}invisible{{/SEC}}file\n");
    TemplateDictionary incdict("dict");
    incdict.ShowSection("SEC");
    incdict.SetValue("FOO", "foo");
    incdict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl, &incdict, "foohiinclude file", true);
  }

  // Tests that we append to the output string, rather than overwrite
  static void TestExpand() {
    Template* tpl = StringToTemplate("hi", STRIP_WHITESPACE);
    TemplateDictionary dict("test_expand");
    string output("premade");
    ASSERT(tpl->Expand(&output, &dict));
    ASSERT_STREQ(output.c_str(), "premadehi");

    tpl = StringToTemplate("   lo   ", STRIP_WHITESPACE);
    ASSERT(tpl->Expand(&output, &dict));
    ASSERT_STREQ(output.c_str(), "premadehilo");
  }

  // Tests annotation, in particular inheriting annotation among children
  // This should be called first, so the filenames don't change as we add
  // more tests.
  static void TestAnnotation() {
    string incname = StringToTemplateFile("include {{#ISEC}}file{{/ISEC}}\n");
    string incname2 = StringToTemplateFile("include #2\n");
    Template* tpl = StringToTemplate(
        "boo!\n{{>INC}}\nhi {{#SEC}}lo{{#SUBSEC}}jo{{/SUBSEC}}{{/SEC}} bar "
        "{{VAR:x-foo}}",
        DO_NOT_STRIP);
    TemplateDictionary dict("dict");
    dict.ShowSection("SEC");
    TemplateDictionary* incdict = dict.AddIncludeDictionary("INC");
    incdict->SetFilename(incname);
    incdict->ShowSection("ISEC");
    dict.AddIncludeDictionary("INC")->SetFilename(incname2);
    dict.SetValue("VAR", "var");

    // This string is equivalent to "/template." (at least on unix)
    string slash_tpl(ctemplate::PathJoin(ctemplate::kRootdir, "template."));
    dict.SetAnnotateOutput("");
    char expected[10240];           // 10k should be big enough!
    snprintf(expected, sizeof(expected),
             "{{#FILE=%s003}}{{#SEC=__{{MAIN}}__}}boo!\n"
             "{{#INC=INC}}{{#FILE=%s001}}"
             "{{#SEC=__{{MAIN}}__}}include {{#SEC=ISEC}}file{{/SEC}}\n"
             "{{/SEC}}{{/FILE}}{{/INC}}"
             "{{#INC=INC}}{{#FILE=%s002}}"
             "{{#SEC=__{{MAIN}}__}}include #2\n{{/SEC}}{{/FILE}}{{/INC}}"
             "\nhi {{#SEC=SEC}}lo{{/SEC}} bar "
             "{{#VAR=VAR:x-foo<not registered>}}var{{/VAR}}{{/SEC}}{{/FILE}}",
             (FLAGS_test_tmpdir + slash_tpl).c_str(),
             (FLAGS_test_tmpdir + slash_tpl).c_str(),
             (FLAGS_test_tmpdir + slash_tpl).c_str());
    AssertExpandIs(tpl, &dict, expected, true);

    dict.SetAnnotateOutput(slash_tpl.c_str());
    snprintf(expected, sizeof(expected),
             "{{#FILE=%s003}}{{#SEC=__{{MAIN}}__}}boo!\n"
             "{{#INC=INC}}{{#FILE=%s001}}"
             "{{#SEC=__{{MAIN}}__}}include {{#SEC=ISEC}}file{{/SEC}}\n"
             "{{/SEC}}{{/FILE}}{{/INC}}"
             "{{#INC=INC}}{{#FILE=%s002}}"
             "{{#SEC=__{{MAIN}}__}}include #2\n{{/SEC}}{{/FILE}}{{/INC}}"
             "\nhi {{#SEC=SEC}}lo{{/SEC}} bar "
             "{{#VAR=VAR:x-foo<not registered>}}var{{/VAR}}{{/SEC}}{{/FILE}}",
             (slash_tpl).c_str(),
             (slash_tpl).c_str(),
             (slash_tpl).c_str());
    AssertExpandIs(tpl, &dict, expected, true);

    dict.SetAnnotateOutput(NULL);   // should turn off annotations
    AssertExpandIs(tpl, &dict,
                   "boo!\ninclude file\ninclude #2\n\nhi lo bar var",
                   true);
  }

  static void TestGetTemplate() {
    // Tests the cache
    string filename = StringToTemplateFile("{This is perfectly valid} yay!");
    Template* tpl1 = Template::GetTemplate(filename, DO_NOT_STRIP);
    Template* tpl2 = Template::GetTemplate(filename.c_str(), DO_NOT_STRIP);
    Template* tpl3 = Template::GetTemplate(filename, STRIP_WHITESPACE);
    ASSERT(tpl1 && tpl2 && tpl3);
    ASSERT(tpl1 == tpl2);
    ASSERT(tpl1 != tpl3);

    // Tests that a nonexistent template returns NULL
    Template* tpl4 = Template::GetTemplate("/yakakak", STRIP_WHITESPACE);
    ASSERT(!tpl4);

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

  static void TestTemplateCache() {
    const string filename_a = StringToTemplateFile("Test template 1");
    const string filename_b = StringToTemplateFile("Test template 2.");

    Template *tpl, *tpl2;
    ASSERT(tpl = Template::GetTemplate(filename_a, DO_NOT_STRIP));

    // Tests with standard (non auto-escape) mode.
    ASSERT(tpl2 = Template::GetTemplate(filename_b, DO_NOT_STRIP));
    ASSERT(tpl2 != tpl);  // different filenames.
    ASSERT(tpl2 = Template::GetTemplate(filename_a, STRIP_BLANK_LINES));
    ASSERT(tpl2 != tpl);  // different strip.
    ASSERT(tpl2 = Template::GetTemplate(filename_b, STRIP_BLANK_LINES));
    ASSERT(tpl2 != tpl);  // different filenames and strip.
    ASSERT(tpl2 = Template::GetTemplate(filename_a, DO_NOT_STRIP));
    ASSERT(tpl2 == tpl);  // same filename and strip.

    // Test mixing standard and auto-escape mode.
    ASSERT(tpl = Template::GetTemplate(filename_a, DO_NOT_STRIP));
    ASSERT(tpl2 = Template::GetTemplateWithAutoEscaping(filename_a, DO_NOT_STRIP,
                                                        TC_HTML));
    ASSERT(tpl2 != tpl);  // different mode.

    // Tests with auto-escape mode.
    ASSERT(tpl = Template::GetTemplateWithAutoEscaping(filename_a, DO_NOT_STRIP,
                                                       TC_HTML));
    ASSERT(tpl2 = Template::GetTemplateWithAutoEscaping(filename_b, DO_NOT_STRIP,
                                                        TC_HTML));
    ASSERT(tpl2 != tpl);  // different filenames.
    ASSERT(tpl2 = Template::GetTemplateWithAutoEscaping(filename_a,
                                                        STRIP_BLANK_LINES,
                                                        TC_HTML));
    ASSERT(tpl2 != tpl);  // different strip.
    ASSERT(tpl2 = Template::GetTemplateWithAutoEscaping(filename_a, DO_NOT_STRIP,
                                                        TC_JS));
    ASSERT(tpl2 != tpl);  // different context.
    ASSERT(tpl2 = Template::GetTemplateWithAutoEscaping(filename_a, DO_NOT_STRIP,
                                                        TC_HTML));
    ASSERT(tpl2 == tpl);  // same filename and strip and context.
  }

  // Tests that the various strip values all do the expected thing.
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
      AssertExpandIs(tpl1, &dict, tests[i][1], true);
      AssertExpandIs(tpl2, &dict, tests[i][2], true);
      AssertExpandIs(tpl3, &dict, tests[i][3], true);
    }
  }

  // Tests both static and non-static versions
  static void TestReloadIfChanged() {
    TemplateDictionary dict("empty");

    string filename = StringToTemplateFile("{valid template}");
    string nonexistent = StringToTemplateFile("dummy");
    unlink(nonexistent.c_str());

    Template* tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);
    assert(tpl);
    Template* tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    assert(!tpl2);

    sleep(1);    // since mtime goes by 1-second increments
    ASSERT(!tpl->ReloadIfChanged());  // false: no reloading needed
    StringToFile("{valid template}", filename);   // no contentful change
    sleep(1);
    ASSERT(tpl->ReloadIfChanged());   // true: change, even if not contentful
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    AssertExpandIs(tpl, &dict, "{valid template}", true);

    StringToFile("exists now!", nonexistent);
    tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    ASSERT(!tpl2);                    // because it's cached as not existing
    Template::ReloadAllIfChanged();
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // force the reload
    tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    ASSERT(tpl2);                     // file exists now
    ASSERT(!tpl2->ReloadIfChanged()); // false: file hasn't changed
    sleep(1);
    ASSERT(!tpl2->ReloadIfChanged()); // false: file *still* hasn't changed

    unlink(nonexistent.c_str());      // here today...
    sleep(1);
    ASSERT(!tpl2->ReloadIfChanged()); // false: file has disappeared
    AssertExpandIs(tpl2, &dict, "exists now!", true);  // last non-error value

    StringToFile("lazarus", nonexistent);
    sleep(1);
    ASSERT(tpl2->ReloadIfChanged());  // true: file exists again

    tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    AssertExpandIs(tpl2, &dict, "lazarus", true);
    StringToFile("{new template}", filename);
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    AssertExpandIs(tpl, &dict, "{valid template}", true);   // haven't reloaded
    sleep(1);
    ASSERT(tpl->ReloadIfChanged());   // true: change, even if not contentful
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    AssertExpandIs(tpl, &dict, "{new template}", true);

    // Now change both tpl and tpl2
    StringToFile("{all-changed}", filename);
    StringToFile("lazarus2", nonexistent);
    Template::ReloadAllIfChanged();
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "{all-changed}", true);
    AssertExpandIs(tpl2, &dict, "lazarus2", true);
  }

  static void TestTemplateRootDirectory() {
    string filename = StringToTemplateFile("Test template");
    ASSERT(ctemplate::IsAbspath(filename));
    Template* tpl1 = Template::GetTemplate(filename, DO_NOT_STRIP);
    Template::SetTemplateRootDirectory(ctemplate::kRootdir);  // "/"
    // template-root shouldn't matter for absolute directories
    Template* tpl2 = Template::GetTemplate(filename, DO_NOT_STRIP);
    Template::SetTemplateRootDirectory("/sadfadsf/waerfsa/safdg");
    Template* tpl3 = Template::GetTemplate(filename, DO_NOT_STRIP);
    ASSERT(tpl1 != NULL);
    ASSERT(tpl1 == tpl2);
    ASSERT(tpl1 == tpl3);

    // Now test it actually works by breaking the abspath in various places.
    // We do it twice, since we don't know if the path-sep is "/" or "\".
    // NOTE: this depends on filename not using "/" or "\" except as a
    //       directory separator (so nothing like "/var/tmp/foo\a/weirdfile").
    const char* const kPathSeps = "/\\";
    for (const char* path_sep = kPathSeps; *path_sep; path_sep++) {
      for (string::size_type sep_pos = filename.find(*path_sep, 0);
           sep_pos != string::npos;
           sep_pos = filename.find(*path_sep, sep_pos + 1)) {
        Template::SetTemplateRootDirectory(filename.substr(0, sep_pos + 1));
        Template* tpl = Template::GetTemplate(filename.substr(sep_pos + 1),
                                              DO_NOT_STRIP);
        ASSERT(tpl == tpl1);
      }
    }
  }

  static void* RunThread(void* vfilename) {
    const char* filename = (const char*)vfilename;
    return Template::GetTemplate(filename, DO_NOT_STRIP);
  }

  static void TestThreadSafety() {
#if defined(HAVE_PTHREAD) && !defined(NO_THREADS)
    string filename = StringToTemplateFile("(testing thread-safety)");

    // GetTemplate() is the most thread-contended routine.  We get a
    // template in many threads, and assert we get the same template
    // from each.
    pthread_t thread_ids[kNumThreads];
    for (int i = 0; i < kNumThreads; ++i) {
      ASSERT(pthread_create(thread_ids+i, NULL, TemplateUnittest::RunThread,
                            (void*)filename.c_str())
             == 0);
    }

    // Wait for all the threads to terminate (should be very quick!)
    void* template_ptr = NULL;
    for (int i = 0; i < kNumThreads; ++i) {
      void* one_template_ptr;
      ASSERT(pthread_join(thread_ids[i], &one_template_ptr) == 0);
      if (template_ptr == NULL) {   // we're the first to return
        template_ptr = one_template_ptr;
      } else {
        ASSERT(template_ptr == one_template_ptr);
      }
    }
#endif
  }

  // Tests all the static methods in TemplateNamelist
  static void TestTemplateNamelist() {
    time_t before_time = time(NULL);
    string f1 = StringToTemplateFile("{{This has spaces in it}}");
    string f2 = StringToTemplateFile("{{#SEC}}foo");
    string f3 = StringToTemplateFile("{This is ok");
    // Where we'll copy f1 - f3 to: these are names known at compile-time
    string f1_copy = ctemplate::PathJoin(FLAGS_test_tmpdir, INVALID1_FN);
    string f2_copy = ctemplate::PathJoin(FLAGS_test_tmpdir, INVALID2_FN);
    string f3_copy = ctemplate::PathJoin(FLAGS_test_tmpdir, VALID1_FN);
    Template::SetTemplateRootDirectory(FLAGS_test_tmpdir);
    time_t after_time = time(NULL);   // f1, f2, f3 all written by now

    TemplateNamelist::NameListType names = TemplateNamelist::GetList();
    ASSERT(names.size() == 4);
    ASSERT(names.find(NONEXISTENT_FN) != names.end());
    ASSERT(names.find(INVALID1_FN) != names.end());
    ASSERT(names.find(INVALID2_FN) != names.end());
    ASSERT(names.find(VALID1_FN) != names.end());

    // Before creating the files INVALID1_FN, etc., all should be missing.
    for (int i = 0; i < 3; ++i) {   // should be consistent all 3 times
      const TemplateNamelist::MissingListType& missing =
        TemplateNamelist::GetMissingList(false);
      ASSERT(missing.size() == 4);
    }
    // Everyone is missing, but nobody should have bad syntax
    ASSERT(!TemplateNamelist::AllDoExist());
    ASSERT(TemplateNamelist::IsAllSyntaxOkay(DO_NOT_STRIP));

    // Now create those files
    link(f1.c_str(), f1_copy.c_str());
    link(f2.c_str(), f2_copy.c_str());
    link(f3.c_str(), f3_copy.c_str());
    // We also have to clear the template cache, since we created a new file.
    // ReloadAllIfChanged() would probably work, too.
    Template::ClearCache();

    // When GetMissingList is false, we don't reload, so you still get all-gone
    TemplateNamelist::MissingListType missing =
      TemplateNamelist::GetMissingList(false);
    ASSERT(missing.size() == 4);
    // But with true, we should have a different story
    missing = TemplateNamelist::GetMissingList(true);
    ASSERT(missing.size() == 1);
    missing = TemplateNamelist::GetMissingList(false);
    ASSERT(missing.size() == 1);
    ASSERT(missing[0] == NONEXISTENT_FN);
    ASSERT(!TemplateNamelist::AllDoExist());

    // IsAllSyntaxOK did a badsyntax check, before the files were created.
    // So with a false arg, should still say everything is ok
    TemplateNamelist::SyntaxListType badsyntax =
      TemplateNamelist::GetBadSyntaxList(false, DO_NOT_STRIP);
    ASSERT(badsyntax.size() == 0);
    // But IsAllSyntaxOK forces a refresh
    ASSERT(!TemplateNamelist::IsAllSyntaxOkay(DO_NOT_STRIP));
    badsyntax = TemplateNamelist::GetBadSyntaxList(false, DO_NOT_STRIP);
    ASSERT(badsyntax.size() == 2);
    ASSERT(badsyntax[0] == INVALID1_FN || badsyntax[1] == INVALID1_FN);
    ASSERT(badsyntax[0] == INVALID2_FN || badsyntax[1] == INVALID2_FN);
    ASSERT(!TemplateNamelist::IsAllSyntaxOkay(DO_NOT_STRIP));
    badsyntax = TemplateNamelist::GetBadSyntaxList(true, DO_NOT_STRIP);
    ASSERT(badsyntax.size() == 2);

    time_t modtime = TemplateNamelist::GetLastmodTime();
    ASSERT(modtime >= before_time && modtime <= after_time);
    // Now update a file and make sure lastmod time is updated
    sleep(1);
    FILE* fp = fopen(f1_copy.c_str(), "ab");
    ASSERT(fp);
    fwrite("\n", 1, 1, fp);
    fclose(fp);
    modtime = TemplateNamelist::GetLastmodTime();
    ASSERT(modtime > after_time);

    // Checking if we can register templates at run time.
    string f4 = StringToTemplateFile("{{ONE_GOOD_TEMPLATE}}");
    TemplateNamelist::RegisterTemplate(f4.c_str());
    names = TemplateNamelist::GetList();
    ASSERT(names.size() == 5);

    string f5 = StringToTemplateFile("{{ONE BAD TEMPLATE}}");
    TemplateNamelist::RegisterTemplate(f5.c_str());
    names = TemplateNamelist::GetList();
    ASSERT(names.size() == 6);
    badsyntax = TemplateNamelist::GetBadSyntaxList(false, DO_NOT_STRIP);
    ASSERT(badsyntax.size() == 2);  // we did not refresh the bad syntax list
    badsyntax = TemplateNamelist::GetBadSyntaxList(true, DO_NOT_STRIP);
    // After refresh, the file we just registerd also added in bad syntax list
    ASSERT(badsyntax.size() == 3);

    TemplateNamelist::RegisterTemplate("A_non_existant_file.tpl");
    names = TemplateNamelist::GetList();
    ASSERT(names.size() == 7);
    missing = TemplateNamelist::GetMissingList(false);
    ASSERT(missing.size() == 1);  // we did not refresh the missing list
    missing = TemplateNamelist::GetMissingList(true);
    // After refresh, the file we just registerd also added in missing list
    ASSERT(missing.size() == 2);
  }

  // This test is not "end-to-end", it doesn't use a dictionary
  // and only outputs what the template system thinks is the
  // correct modifier for variables.
  static void TestCorrectModifiersForAutoEscape() {
    string text, expected_out;

    // template with no variable, nothing to emit.
    text = "Static template.";
    AssertCorrectModifiers(TC_HTML, text, "");

    // Simple templates with one variable substitution.

    // 1. No in-template modifiers. Auto Escaper sets correct ones.
    text = "Hello {{USER}}";
    AssertCorrectModifiers(TC_HTML, text, "USER:h\n");

    // Complete URLs in different attributes that take URLs.
    text = "<a href=\"{{URL}}\">bla</a>";
    AssertCorrectModifiers(TC_HTML, text, "URL:U=html\n");
    text = "<script src=\"{{URL}}\"></script>";
    AssertCorrectModifiers(TC_HTML, text, "URL:U=html\n");
    text = "<img src=\"{{URL}}\">";
    AssertCorrectModifiers(TC_HTML, text, "URL:U=html\n");
    // URL fragment only so just html_escape.
    text = "<img src=\"/bla?q={{QUERY}}\">";
    AssertCorrectModifiers(TC_HTML, text, "QUERY:h\n");
    // URL fragment not quoted, so url_escape.
    text = "<img src=/bla?q={{QUERY}}>";
    AssertCorrectModifiers(TC_HTML, text, "QUERY:u\n");

    text = "<br class=\"{{CLASS}}\">";
    AssertCorrectModifiers(TC_HTML, text, "CLASS:h\n");
    text = "<br class={{CLASS}}>";
    AssertCorrectModifiers(TC_HTML, text, "CLASS:H=attribute\n");
    text = "<br {{CLASS}}>";   // CLASS here is name/value pair.
    AssertCorrectModifiers(TC_HTML, text, "CLASS:H=attribute\n");
    text = "<br style=\"display:{{DISPLAY}}\">";   // Style attribute.
    AssertCorrectModifiers(TC_HTML, text, "DISPLAY:c\n");

    // onMouseEvent and onKeyUp accept javascript.
    text = "<a href=\"url\" onkeyup=\"doX('{{ID}}');\">";  // ID quoted
    AssertCorrectModifiers(TC_HTML, text, "ID:j\n");
    text = "<a href=\"url\" onclick=\"doX({{ID}});\">";    // ID not quoted
    AssertCorrectModifiers(TC_HTML, text, "ID:J=number\n");
    text = "<a href=\"url\" onclick=\"'{{ID}}'\">";        // not common
    AssertCorrectModifiers(TC_HTML, text, "ID:j\n");
    // If ID is javascript code, J=number  will break it, for good and bad.
    text = "<a href=\"url\" onclick=\"{{ID}}\">";
    AssertCorrectModifiers(TC_HTML, text, "ID:J=number\n");

    // Target just needs html escaping.
    text = "<a href=\"url\" target=\"{{TARGET}}\">";
    AssertCorrectModifiers(TC_HTML, text, "TARGET:h\n");

    // Test a parsing corner case which uses TemplateDirective
    // call in the parser to change state properly. To reproduce
    // both variables should be unquoted and the first should
    // have no value except the variable substitution.
    text = "<img class={{CLASS}} src=/bla?q={{QUERY}}>";
    AssertCorrectModifiers(TC_HTML, text, "CLASS:H=attribute\nQUERY:u\n");

    // TODO(jad): Once we have a fix for it in code, fix me.
    // Javascript URL is not properly supported, we currently
    // apply :h which is not sufficient.
    text = "<a href=\"javascript:foo('{{VAR}}')>bla</a>";
    AssertCorrectModifiers(TC_HTML, text, "VAR:h\n");

    // Special handling for BI_SPACE and BI_NEWLINE.
    text = "{{BI_SPACE}}";
    AssertCorrectModifiers(TC_HTML, text, "BI_SPACE\n");      // Untouched.
    text = "{{BI_NEWLINE}}";
    AssertCorrectModifiers(TC_HTML, text, "BI_NEWLINE\n");    // Untouched.
    // Check that the parser is parsing BI_SPACE, if not, it would have failed.
    text = "<a href=/bla{{BI_SPACE}}style=\"{{VAR}}\">text</a>";
    AssertCorrectModifiers(TC_HTML, text, "BI_SPACE\nVAR:c\n");

    // Special handling for TC_NONE. No auto-escaping.
    text = "Hello {{USER}}";
    AssertCorrectModifiers(TC_NONE, text, "USER\n");

    // XML and JSON modes.
    text = "<PARAM name=\"{{VAL}}\">{{DATA}}";
    AssertCorrectModifiers(TC_XML, text, "VAL:xml_escape\nDATA:xml_escape\n");
    text = "{ x = \"{{VAL}}\"}";
    AssertCorrectModifiers(TC_JSON, text, "VAL:j\n");

    // 2. Escaping modifiers were set, handle them.

    // 2a: Modifier :none is honored whether the escaping is correct or not.
    text = "Hello {{USER:none}}";                   // :none on its own.
    AssertCorrectModifiers(TC_HTML, text, "USER:none\n");
    text = "Hello {{USER:h:none}}";                 // correct escaping.
    AssertCorrectModifiers(TC_HTML, text, "USER:h:none\n");
    text = "Hello {{USER:j:none}}";                 // incorrect escaping.
    AssertCorrectModifiers(TC_HTML, text, "USER:j:none\n");
    text = "<a href=\"url\" onkeyup=\"doX('{{ID:none}}');\">";
    AssertCorrectModifiers(TC_HTML, text, "ID:none\n");

    // 2b: Correct modifiers, nothing to change.
    text = "Hello {{USER:h}}";
    AssertCorrectModifiers(TC_HTML, text, "USER:h\n");
    text = "Hello {{USER:h:j}}";   // Extra :j, honor it.
    AssertCorrectModifiers(TC_HTML, text, "USER:h:j\n");
    text = "<a href=\"{{URL:U=html}}\">bla</a>";
    AssertCorrectModifiers(TC_HTML, text, "URL:U=html\n");
    text = "<a href=\"/bla?q={{QUERY:h}}\">bla</a>";  // :h is valid.
    AssertCorrectModifiers(TC_HTML, text, "QUERY:h\n");
    text = "<a href=\"/bla?q={{QUERY:u}}\">bla</a>";  // so is :u.
    AssertCorrectModifiers(TC_HTML, text, "QUERY:u\n");
    text = "<a href=\"url\" onclick=\"doX('{{ID:j}}');\">";
    AssertCorrectModifiers(TC_HTML, text, "ID:j\n");
    text = "<a href=\"url\" onclick=\"doX({{ID:J=number}});\">";
    AssertCorrectModifiers(TC_HTML, text, "ID:J=number\n");

    // 2c: Incorrect modifiers, add our own.
    text = "Hello {{USER:j}}";                          // Missing :h
    AssertCorrectModifiers(TC_HTML, text, "USER:j:h\n");
    text = "Hello {{USER:c:c:c:c:c:j}}";                // Still missing :h
    AssertCorrectModifiers(TC_HTML, text, "USER:c:c:c:c:c:j:h\n");
    text = "<script>var a = \"{{VAR:h}}\";</script>";   // Missing :j
    AssertCorrectModifiers(TC_HTML, text, "VAR:h:j\n");
    text = "<script>var a = \"{{VAR:j:h:j}}\";</script>";   // Extra :h:j
    AssertCorrectModifiers(TC_HTML, text, "VAR:j:h:j\n");
    text = "<a href=\"url\" onclick=\"doX({{ID:j}});\">";   // Unquoted
    AssertCorrectModifiers(TC_HTML, text, "ID:j:J=number\n");

    // 2d: Custom modifiers are maintained.
    text = "Hello {{USER:x-bla}}";                  // Missing :h
    AssertCorrectModifiers(TC_HTML, text, "USER:x-bla:h\n");
    text = "Hello {{USER:x-bla:h}}";                // Correct, accept it.
    AssertCorrectModifiers(TC_HTML, text, "USER:x-bla:h\n");
    text = "Hello {{USER:x-bla:x-foo}}";            // Missing :h
    AssertCorrectModifiers(TC_HTML, text, "USER:x-bla:x-foo:h\n");
    text = "Hello {{USER:x-bla:none}}";             // Complete due to :none
    AssertCorrectModifiers(TC_HTML, text, "USER:x-bla:none\n");
    text = "Hello {{USER:h:x-bla}}";                // Still missing :h.
    AssertCorrectModifiers(TC_HTML, text, "USER:h:x-bla:h\n");
    text = "Hello {{USER:x-bla:h:x-foo}}";          // Still missing :h
    AssertCorrectModifiers(TC_HTML, text, "USER:x-bla:h:x-foo:h\n");
    text = "Hello {{USER:x-bla:h:x-foo:h}}";        // Valid, accept it.
    AssertCorrectModifiers(TC_HTML, text, "USER:x-bla:h:x-foo:h\n");

    // 2e: Equivalent modifiers are honored. All HTML Escapes.
    text = "Hello {{USER:p}}";
    AssertCorrectModifiers(TC_HTML, text, "USER:p\n");
    text = "Hello {{USER:H=attribute}}";
    AssertCorrectModifiers(TC_HTML, text, "USER:H=attribute\n");
    text = "Hello {{USER:H=snippet}}";
    AssertCorrectModifiers(TC_HTML, text, "USER:H=snippet\n");
    text = "Hello {{USER:H=pre}}";
    AssertCorrectModifiers(TC_HTML, text, "USER:H=pre\n");
    // All URL + HTML Escapes.
    text = "<a href=\"{{URL:H=url}}\">bla</a>";
    AssertCorrectModifiers(TC_HTML, text, "URL:H=url\n");
    text = "<a href=\"{{URL:U=html}}\">bla</a>";
    AssertCorrectModifiers(TC_HTML, text, "URL:U=html\n");

    // 2f: Initialize template in Javascript Context.
    text = "var a = '{{VAR}}'";                     // Escaping not given.
    AssertCorrectModifiers(TC_JS, text, "VAR:j\n");
    text = "var a = '{{VAR:none}}'";                // Variable safe.
    AssertCorrectModifiers(TC_JS, text, "VAR:none\n");
    text = "var a = '{{VAR:j}}'";                   // Escaping correct.
    AssertCorrectModifiers(TC_JS, text, "VAR:j\n");
    text = "var a = '{{VAR:h}}'";                   // Escaping incorrect.
    AssertCorrectModifiers(TC_JS, text, "VAR:h:j\n");
    text = "var a = '{{VAR:J=number}}'";            // Not considered equiv.
    AssertCorrectModifiers(TC_JS, text, "VAR:J=number:j\n");

    // 2g: Honor any modifiers for BI_SPACE and BI_NEWLINE.
    text = "{{BI_NEWLINE:j}}";     // An invalid modifier for the context.
    AssertCorrectModifiers(TC_HTML, text, "BI_NEWLINE:j\n");
    text = "{{BI_SPACE:h}}";       // An otherwise valid modifier.
    AssertCorrectModifiers(TC_HTML, text, "BI_SPACE:h\n");
    text = "{{BI_SPACE:x-bla}}";   // Also support custom modifiers.
    AssertCorrectModifiers(TC_HTML, text, "BI_SPACE:x-bla\n");

    // 2h: In TC_NONE context, don't touch modifiers.
    text = "Hello {{USER:h}}";
    AssertCorrectModifiers(TC_NONE, text, "USER:h\n");
    text = "Hello {{USER:j}}";
    AssertCorrectModifiers(TC_NONE, text, "USER:j\n");
    text = "Hello {{USER:x-bla:none}}";
    AssertCorrectModifiers(TC_NONE, text, "USER:x-bla:none\n");

    // 2i: TC_XML and TC_JSON
    text = "<PARAM name=\"{{VAL:xml_escape}}\">";   // Correct escaping
    AssertCorrectModifiers(TC_XML, text, "VAL:xml_escape\n");
    text = "<PARAM name=\"{{VAL:H=attribute}}\">";   // XSS equivalent
    AssertCorrectModifiers(TC_XML, text, "VAL:H=attribute\n");
    text = "<PARAM name=\"{{VAL:h}}\">";   // XSS equivalent
    AssertCorrectModifiers(TC_XML, text, "VAL:h\n");
    text = "<PARAM name=\"{{VAL:H=pre}}\">";   // Not XSS equivalent
    AssertCorrectModifiers(TC_XML, text, "VAL:H=pre:xml_escape\n");
    text = "<PARAM name=\"{{VAL:c}}\">";   // Not XSS equivalent
    AssertCorrectModifiers(TC_XML, text, "VAL:c:xml_escape\n");
    text = "{user={{USER:j}}";   // Correct escaping
    AssertCorrectModifiers(TC_JSON, text, "USER:j\n");
    text = "{user={{USER:h}}";   // Not XSS equivalent
    AssertCorrectModifiers(TC_JSON, text, "USER:h:j\n");

    // 3. Larger test with close to every escaping case.

    text = "<h1>{{TITLE}}</h1>\n"
        "<img src=\"{{IMG_URL}}\">\n"
        "<form action=\"/search\">\n"
        "  <input name=\"hl\" value={{HL}}>\n"
        "  <input name=\"m\" value=\"{{FORM_MSG}}\">\n"
        "</form>\n"
        "<div style=\"background:{{BG_COLOR}}\">\n"
        "</div>\n"
        "<script>\n"
        "  var msg_text = '{{MSG_TEXT}}';\n"
        "</script>\n"
        "<a href=\"url\" onmouseover=\"'{{MOUSE}}'\">bla</a>\n"
        "Goodbye friend {{USER}}!\n";
    expected_out = "TITLE:h\n"
        "IMG_URL:U=html\n"
        "HL:H=attribute\n"
        "FORM_MSG:h\n"
        "BG_COLOR:c\n"
        "MSG_TEXT:j\n"
        "MOUSE:j\n"   // :j also escapes html entities
        "USER:h\n";
    AssertCorrectModifiers(TC_HTML, text, expected_out);
  }

  // More "end-to-end" test to ensure that variables are
  // escaped as expected with auto-escape mode enabled.
  // Obviously there is a lot more we can test.
  static void TestVariableWithAutoEscape() {
    string text, expected_out;
    TemplateDictionary dict("dict");
    string good_url("http://www.google.com/");
    string bad_url("javascript:alert();");

    text = "hi {{VAR}} lo";
    dict.SetValue("VAR", "<bad>yo");
    AssertCorrectEscaping(TC_HTML, dict, text, "hi &lt;bad&gt;yo lo");

    text = "<a href=\"{{URL}}\">bla</a>";
    dict.SetValue("URL", good_url);
    expected_out = "<a href=\"" + good_url + "\">bla</a>";
    AssertCorrectEscaping(TC_HTML, dict, text, expected_out);
    dict.SetValue("URL", bad_url);
    expected_out = "<a href=\"#\">bla</a>";
    AssertCorrectEscaping(TC_HTML, dict, text, expected_out);

    text = "<br style=\"display:{{DISPLAY}}\">";
    dict.SetValue("DISPLAY", "none");
    expected_out = "<br style=\"display:none\">";
    AssertCorrectEscaping(TC_HTML, dict, text, expected_out);
    // Bad characters are simply removed in CleanseCss.
    dict.SetValue("URL", "!#none_ ");
    expected_out = "<br style=\"display:none\">";
    AssertCorrectEscaping(TC_HTML, dict, text, expected_out);

    text = "<a href=\"url\" onkeyup=\"'{{EVENT}}'\">";
    dict.SetValue("EVENT", "safe");
    expected_out = "<a href=\"url\" onkeyup=\"'safe'\">";
    AssertCorrectEscaping(TC_HTML, dict, text, expected_out);
    dict.SetValue("EVENT", "f = 'y';");
    expected_out = "<a href=\"url\" onkeyup=\"'f \\x3d \\x27y\\x27;'\">";

    // Check special handling of BI_SPACE and BI_NEWLINE.
    text = "Hello\n{{BI_SPACE}}bla{{BI_NEWLINE}}foo.";
    expected_out = "Hello bla\nfoo.";
    AssertCorrectEscaping(TC_HTML, dict, text, expected_out);

    // TC_NONE, no escaping done.
    text = "hi {{VAR}} lo";
    dict.SetValue("VAR", "<bad>yo");
    AssertCorrectEscaping(TC_NONE, dict, text, "hi <bad>yo lo");

    // TC_XML and TC_JSON
    text = "<Q>{{DATA}}</Q>";
    dict.SetValue("DATA", "good-data");
    AssertCorrectEscaping(TC_XML, dict, text, "<Q>good-data</Q>");
    dict.SetValue("DATA", "<BAD>FOO</BAD>");
    AssertCorrectEscaping(TC_XML, dict, text,
                          "<Q>&lt;BAD&gt;FOO&lt;/BAD&gt;</Q>");
    text = "{user = \"{{USER}}\"}";
    dict.SetValue("USER", "good-user");
    AssertCorrectEscaping(TC_JSON, dict, text, "{user = \"good-user\"}");
    dict.SetValue("USER", "evil'<>\"");
    AssertCorrectEscaping(TC_JSON, dict, text,
                          "{user = \"evil\\x27\\x3c\\x3e\\x22\"}");
  }

  // Basic test that auto-escaping continues to work on an included
  // template.
  static void TestIncludeWithAutoEscape() {
    const string url = "http://www.google.com";
    const string user = "foo";
    const string owner = "webmaster";

    // Create a template with one included template.
    string top_text = "Hi {{>INC}}<p>Yours, truly: {{OWNER}}";
    string sub_text = "{{USER}}; <a href=\"{{URL}}\">bla</a>";
    Template *tpl = StringToTemplateWithAutoEscaping(top_text,
                                                     STRIP_WHITESPACE,
                                                     TC_HTML);
    string incname = StringToTemplateFile(sub_text);
    TemplateDictionary dict("dict");
    TemplateDictionary* sub_dict = dict.AddIncludeDictionary("INC");
    sub_dict->SetFilename(incname);

    // Fill both dictionaries with good values.
    dict.SetValue("OWNER", owner);
    sub_dict->SetValue("USER", user);
    sub_dict->SetValue("URL", url);

    string expected = "Hi " + user + "; <a href=\"" +
        url + "\">bla</a><p>Yours, truly: " + owner;
    AssertExpandIs(tpl, &dict, expected, true);

    // Repeat with bad values.
    dict.SetValue("OWNER", "<");
    sub_dict->SetValue("USER", "&>");
    sub_dict->SetValue("URL", "javascript:alert(1)");

    expected = "Hi &amp;&gt;; <a href=\"#\">bla</a><p>Yours, truly: &lt;";
    AssertExpandIs(tpl, &dict, expected, true);

    // Test modifiers at the template-include level.
    // The whole included template (static and dynamic data) will
    // be html escaped during expansion.
    string top_text2 = "Hi {{>INC:h}}<p>Yours, truly: {{OWNER}}";
    tpl = StringToTemplateWithAutoEscaping(top_text2, STRIP_WHITESPACE,
                                           TC_HTML);
    dict.SetValue("OWNER", "owner");
    sub_dict->SetValue("USER", "<b>BadUser</b>");
    sub_dict->SetValue("URL", "http://<>/bad");
    expected = "Hi "
        "&lt;b&gt;BadUser&lt;/b&gt;"   // {{USER}} html escaped
        "; &lt;a href=&quot;"          // <a href=\" html escaped
        "http://&lt;&gt;/bad"          // {{URL}} html escaped
        "&quot;&gt;bla&lt;/a&gt;"      // rest of child html escaped
        "<p>Yours, truly: owner";      // parent template, intact.
    AssertExpandIs(tpl, &dict, expected, true);
  }

  // Test that the template initialization fails in auto-escape
  // mode if the parser failed to parse.
  static void TestFailedInitWithAutoEscape() {
    Strip strip = STRIP_WHITESPACE;
    // Taken from HTML Parser test suite.
    string bad_html = "<a href='http://www.google.com' ''>\n";
    ASSERT(NULL == StringToTemplateWithAutoEscaping(bad_html, strip, TC_HTML));

    // Missing quotes around URL, not accepted in URL-taking attributes.
    bad_html = "<a href={{URL}}>bla</a>";
    ASSERT(NULL == StringToTemplateWithAutoEscaping(bad_html, strip, TC_HTML));

    // Missing quotes around STYLE, not accepted in style-taking attributes.
    bad_html = "<div style={{STYLE}}>";
    ASSERT(NULL == StringToTemplateWithAutoEscaping(bad_html, strip, TC_HTML));
  }
};

int main(int argc, char** argv) {
  CleanTestDir(FLAGS_test_tmpdir);

  // This goes first so that future tests don't mess up the filenames
  TemplateUnittest::TestAnnotation();

  TemplateUnittest::TestVariable();
  TemplateUnittest::TestVariableWithModifiers();
  TemplateUnittest::TestSection();
  TemplateUnittest::TestInclude();
  TemplateUnittest::TestIncludeWithModifiers();
  TemplateUnittest::TestRecursiveInclude();
  TemplateUnittest::TestInheritence();
  TemplateUnittest::TestExpand();

  TemplateUnittest::TestGetTemplate();
  TemplateUnittest::TestTemplateCache();
  TemplateUnittest::TestStrip();
  TemplateUnittest::TestReloadIfChanged();

  TemplateUnittest::TestTemplateRootDirectory();
  TemplateUnittest::TestTemplateNamelist();

  TemplateUnittest::TestThreadSafety();

  TemplateUnittest::TestCorrectModifiersForAutoEscape();
  TemplateUnittest::TestVariableWithAutoEscape();
  TemplateUnittest::TestIncludeWithAutoEscape();
  TemplateUnittest::TestFailedInitWithAutoEscape();

  printf("DONE\n");
  return 0;
}
