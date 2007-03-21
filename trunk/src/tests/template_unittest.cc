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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>     // for mkdir
#if HAVE_DIRENT_H
# include <dirent.h>      // for readdir
#else
# define dirent direct
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif
#include <vector>         // for MissingListType, SyntaxListType
#include <google/ctemplate/hash_set.h>       // for NameListType
#include "google/template.h"
#include "google/template_dictionary.h"
#include "google/template_namelist.h"

using std::vector;
using std::string;
using HASH_NAMESPACE::hash_set;
using GOOGLE_NAMESPACE::Template;
using GOOGLE_NAMESPACE::TemplateDictionary;
using GOOGLE_NAMESPACE::TemplateNamelist;
using GOOGLE_NAMESPACE::Strip;
using GOOGLE_NAMESPACE::DO_NOT_STRIP;
using GOOGLE_NAMESPACE::STRIP_BLANK_LINES;
using GOOGLE_NAMESPACE::STRIP_WHITESPACE;

static const string FLAGS_test_tmpdir("/tmp/template_unittest_dir");

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

// First, (conceptually) remove all chars in "except" from both a and b.
// Then return true iff munged_a == munged_b.
bool StreqExcept(const char* a, const char* b, const char* except) {
  const char* pa = a, *pb = b;
  const int exceptlen = strlen(except);
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


RegisterTemplateFilename(VALID1_FN, "template_unittest_test_valid1.in");
RegisterTemplateFilename(INVALID1_FN, "template_unittest_test_invalid1.in");
RegisterTemplateFilename(INVALID2_FN, "template_unittest_test_invalid2.in");
RegisterTemplateFilename(NONEXISTENT_FN, "nonexistent__file.tpl");

// deletes all files named *template* in dir
static void CleanTestDir(const string& dirname) {
  DIR* dir = opendir(dirname.c_str());
  if (!dir) {  // directory doesn't exist or something like that.
    if (errno == ENOENT)   // if dir doesn't exist, try to make it
      ASSERT(mkdir(dirname.c_str(), 0755) == 0);
    return;
  }
  while (struct dirent* d = readdir(dir)) {
    if (strstr(d->d_name, "template"))
      unlink((dirname + "/" + d->d_name).c_str());
  }
  closedir(dir);
}

// This writes s to the given file
static void StringToFile(const string& s, const string& filename) {
  FILE* fp = fopen(filename.c_str(), "w");
  ASSERT(fp);
  int r = fwrite(s.data(), 1, s.length(), fp);
  ASSERT(r == s.length());
  fclose(fp);
}

// This writes s to a file and returns the filename.
static string StringToTemplateFile(const string& s) {
  static int filenum = 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%03d", ++filenum);
  string filename = FLAGS_test_tmpdir + "/template." + buf;
  StringToFile(s, filename);
  return filename;
}

// This writes s to a file and then loads it into a template object.
static Template* StringToTemplate(const string& s, Strip strip) {
  return Template::GetTemplate(StringToTemplateFile(s), strip);
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
// the TemplateUnittest class can be listed as a friend
// once, and access all the internals of Template.
class TemplateUnittest {
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

  static void TestVariableWithModifiers() {
    Template* tpl = StringToTemplate("hi {{VAR:html_escape}} lo",
                                     STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    dict.SetValue("VAR", "yo");
    AssertExpandIs(tpl, &dict, "hi yo lo");
    dict.SetValue("VAR", "yo&yo");
    AssertExpandIs(tpl, &dict, "hi yo&amp;yo lo");

    tpl = StringToTemplate("hi {{VAR:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&amp;yo lo");

    tpl = StringToTemplate("hi {{VAR:h:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&amp;amp;yo lo");

    tpl = StringToTemplate("hi {{VAR}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo&yo lo");

    // Check that ordering is right
    dict.SetValue("VAR", "yo\nyo");
    tpl = StringToTemplate("hi {{VAR:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo yo lo");
    tpl = StringToTemplate("hi {{VAR:j}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo");
    tpl = StringToTemplate("hi {{VAR:h:j}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo yo lo");
    tpl = StringToTemplate("hi {{VAR:j:h}} lo", STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo");

    // Check more complicated modifiers using fullname
    tpl = StringToTemplate("hi {{VAR:javascript_escape:h}} lo",
                           STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "hi yo\\nyo lo");
    tpl = StringToTemplate("hi {{VAR:j:html_escape}} lo",
                           STRIP_WHITESPACE);

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

    // Check we don't allow modifiers on sections
    tpl = StringToTemplate("hi {{#VAR:h}} lo {{/VAR}}", STRIP_WHITESPACE);
    ASSERT(tpl == NULL);
  }

  static void TestSection() {
    Template* tpl = StringToTemplate(
        "boo!\nhi {{#SEC}}lo{{#SUBSEC}}jo{{/SUBSEC}}{{/SEC}} bar",
        STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "boo!hi  bar");
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "boo!hi lo bar");
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "boo!hi lo bar");
    // This should work even though subsec isn't a child of the main dict
    dict.ShowSection("SUBSEC");
    AssertExpandIs(tpl, &dict, "boo!hi lojo bar");

    TemplateDictionary dict2("dict2");
    dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lo bar");
    dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lolo bar");
    dict2.AddSectionDictionary("sec");
    AssertExpandIs(tpl, &dict2, "boo!hi lolo bar");
    dict2.ShowSection("SUBSEC");
    AssertExpandIs(tpl, &dict2, "boo!hi lojolojo bar");
  }

  static void TestInclude() {
    string incname = StringToTemplateFile("include file\n");
    string incname2 = StringToTemplateFile("inc2\n");
    string incname_bad = StringToTemplateFile("{{syntax_error");
    Template* tpl = StringToTemplate("hi {{>INC}} bar\n", STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl, &dict, "hi  bar");
    dict.AddIncludeDictionary("INC");
    AssertExpandIs(tpl, &dict, "hi  bar");   // noop: no filename was set
    dict.AddIncludeDictionary("INC")->SetFilename("/notarealfile ");
    AssertExpandIs(tpl, &dict, "hi  bar");   // noop: illegal filename
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl, &dict, "hi include file bar");
    dict.AddIncludeDictionary("INC")->SetFilename(incname_bad);
    AssertExpandIs(tpl, &dict, "hi include file bar");  // noop: syntax error
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl, &dict, "hi include fileinclude file bar");
    dict.AddIncludeDictionary("inc")->SetFilename(incname);
    AssertExpandIs(tpl, &dict, "hi include fileinclude file bar");
    dict.AddIncludeDictionary("INC")->SetFilename(incname2);
    AssertExpandIs(tpl, &dict, "hi include fileinclude fileinc2 bar");

    // Now test that includes preserve Strip
    Template* tpl2 = StringToTemplate("hi {{>INC}} bar", DO_NOT_STRIP);
    AssertExpandIs(tpl2, &dict, "hi include file\ninclude file\ninc2\n bar");
  }

  static void TestIncludeWithModifiers() {
    string incname = StringToTemplateFile("include & print file\n");
    string incname2 = StringToTemplateFile("inc2\n");
    // Note this also tests that html-escape, but not javascript-escape,
    // escapes \n to <space>
    Template* tpl1 = StringToTemplate("hi {{>INC:h}} bar\n", DO_NOT_STRIP);
    Template* tpl2 = StringToTemplate("hi {{>INC:javascript_escape}} bar\n",
                                      DO_NOT_STRIP);
    TemplateDictionary dict("dict");
    AssertExpandIs(tpl1, &dict, "hi  bar\n");
    dict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl1, &dict, "hi include &amp; print file  bar\n");
    dict.AddIncludeDictionary("INC")->SetFilename(incname2);
    AssertExpandIs(tpl1, &dict, "hi include &amp; print file inc2  bar\n");
    AssertExpandIs(tpl2, &dict, "hi include & print file\\ninc2\\n bar\n");

    // Don't test modifier syntax here; that's in TestVariableWithModifiers()
  }

  // Tests that vars inherit/override their parents properly
  static void TestInheritence() {
    Template* tpl = StringToTemplate("{{FOO}}{{#SEC}}{{FOO}}{{#SEC}}{{FOO}}{{/SEC}}{{/SEC}}",
                                     STRIP_WHITESPACE);
    TemplateDictionary dict("dict");
    dict.SetValue("FOO", "foo");
    dict.ShowSection("SEC");
    AssertExpandIs(tpl, &dict, "foofoofoo");

    TemplateDictionary dict2("dict2");
    dict2.SetValue("FOO", "foo");
    TemplateDictionary* sec = dict2.AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "foofoofoo");
    sec->SetValue("FOO", "bar");
    AssertExpandIs(tpl, &dict2, "foobarbar");
    TemplateDictionary* sec2 = sec->AddSectionDictionary("SEC");
    AssertExpandIs(tpl, &dict2, "foobarbar");
    sec2->SetValue("FOO", "baz");
    AssertExpandIs(tpl, &dict2, "foobarbaz");

    // Now test an include template, which shouldn't inherit from its parents
    tpl = StringToTemplate("{{FOO}}{{#SEC}}hi{{/SEC}}\n{{>INC}}",
                           STRIP_WHITESPACE);
    string incname = StringToTemplateFile(
        "include {{FOO}}{{#SEC}}invisible{{/SEC}}file\n");
    TemplateDictionary incdict("dict");
    incdict.ShowSection("SEC");
    incdict.SetValue("FOO", "foo");
    incdict.AddIncludeDictionary("INC")->SetFilename(incname);
    AssertExpandIs(tpl, &incdict, "foohiinclude file");
  }

  // Tests that we append to the output string, rather than overwrite
  static void TestExpand() {
    Template* tpl = StringToTemplate("hi", STRIP_WHITESPACE);
    TemplateDictionary dict("test_expand");
    string output("premade");
    tpl->Expand(&output, &dict);
    ASSERT_STREQ(output.c_str(), "premadehi");

    tpl = StringToTemplate("   lo   ", STRIP_WHITESPACE);
    tpl->Expand(&output, &dict);
    ASSERT_STREQ(output.c_str(), "premadehilo");
  }

  // Tests annotation, in particular inheriting annotation among children
  // This should be called first, so the filenames don't change as we add
  // more tests.
  static void TestAnnotation() {
    string incname = StringToTemplateFile("include {{#ISEC}}file{{/ISEC}}\n");
    string incname2 = StringToTemplateFile("include #2\n");
    Template* tpl = StringToTemplate(
        "boo!\n{{>INC}}\nhi {{#SEC}}lo{{#SUBSEC}}jo{{/SUBSEC}}{{/SEC}} bar",
        DO_NOT_STRIP);
    TemplateDictionary dict("dict");
    dict.ShowSection("SEC");
    TemplateDictionary* incdict = dict.AddIncludeDictionary("INC");
    incdict->SetFilename(incname);
    incdict->ShowSection("ISEC");
    dict.AddIncludeDictionary("INC")->SetFilename(incname2);

    dict.SetAnnotateOutput("");
    char expected[10240];           // 10k should be big enough!
    snprintf(expected, sizeof(expected),
             "{{#FILE=%s/template.003}}{{#SEC=__MAIN__}}boo!\n"
             "{{#INC=INC}}{{#FILE=%s/template.001}}"
             "{{#SEC=__MAIN__}}include {{#SEC=ISEC}}file{{/SEC}}\n"
             "{{/SEC}}{{/FILE}}{{/INC}}"
             "{{#INC=INC}}{{#FILE=%s/template.002}}"
             "{{#SEC=__MAIN__}}include #2\n{{/SEC}}{{/FILE}}{{/INC}}"
             "\nhi {{#SEC=SEC}}lo{{/SEC}} bar{{/SEC}}{{/FILE}}",
             FLAGS_test_tmpdir.c_str(), FLAGS_test_tmpdir.c_str(),
             FLAGS_test_tmpdir.c_str());
    AssertExpandIs(tpl, &dict, expected);

    dict.SetAnnotateOutput("/template.");
    AssertExpandIs(tpl, &dict,
                   "{{#FILE=/template.003}}{{#SEC=__MAIN__}}boo!\n"
                   "{{#INC=INC}}{{#FILE=/template.001}}"
                   "{{#SEC=__MAIN__}}include {{#SEC=ISEC}}file{{/SEC}}\n"
                   "{{/SEC}}{{/FILE}}{{/INC}}"
                   "{{#INC=INC}}{{#FILE=/template.002}}"
                   "{{#SEC=__MAIN__}}include #2\n{{/SEC}}{{/FILE}}{{/INC}}"
                   "\nhi {{#SEC=SEC}}lo{{/SEC}} bar{{/SEC}}{{/FILE}}");

    dict.SetAnnotateOutput(NULL);   // should turn off annotations
    AssertExpandIs(tpl, &dict, "boo!\ninclude file\ninclude #2\n\nhi lo bar");
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
      AssertExpandIs(tpl1, &dict, tests[i][1]);
      AssertExpandIs(tpl2, &dict, tests[i][2]);
      AssertExpandIs(tpl3, &dict, tests[i][3]);
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
    AssertExpandIs(tpl, &dict, "{valid template}");

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
    AssertExpandIs(tpl2, &dict, "exists now!");  // last non-error value

    StringToFile("lazarus", nonexistent);
    sleep(1);
    ASSERT(tpl2->ReloadIfChanged());  // true: file exists again

    tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    AssertExpandIs(tpl2, &dict, "lazarus");
    StringToFile("{new template}", filename);
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    AssertExpandIs(tpl, &dict, "{valid template}");   // haven't reloaded
    sleep(1);
    ASSERT(tpl->ReloadIfChanged());   // true: change, even if not contentful
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    AssertExpandIs(tpl, &dict, "{new template}");

    // Now change both tpl and tpl2
    StringToFile("{all-changed}", filename);
    StringToFile("lazarus2", nonexistent);
    Template::ReloadAllIfChanged();
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);  // needed
    tpl2 = Template::GetTemplate(nonexistent, STRIP_WHITESPACE);
    AssertExpandIs(tpl, &dict, "{all-changed}");
    AssertExpandIs(tpl2, &dict, "lazarus2");
  }

  static void TestTemplateRootDirectory() {
    string filename = StringToTemplateFile("Test template");
    ASSERT(filename[0] == '/');    // absolute filename
    Template* tpl1 = Template::GetTemplate(filename, DO_NOT_STRIP);
    Template::SetTemplateRootDirectory("/");
    // template-root shouldn't matter for absolute directories
    Template* tpl2 = Template::GetTemplate(filename, DO_NOT_STRIP);
    Template::SetTemplateRootDirectory("/sadfadsf/waerfsa/safdg");
    Template* tpl3 = Template::GetTemplate(filename, DO_NOT_STRIP);
    ASSERT(tpl1 != NULL);
    ASSERT(tpl1 == tpl2);
    ASSERT(tpl1 == tpl3);

    // Now test it actually works by breaking the abspath in various places
    for (int s = 0; s != string::npos; s = filename.find('/', s+1)) {
      Template::SetTemplateRootDirectory(filename.substr(0, s+1));
      Template* tpl = Template::GetTemplate(filename.substr(s+1), DO_NOT_STRIP);
      ASSERT(tpl == tpl1);
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
    link(f1.c_str(), (FLAGS_test_tmpdir + "/" + INVALID1_FN).c_str());
    link(f2.c_str(), (FLAGS_test_tmpdir + "/" + INVALID2_FN).c_str());
    link(f3.c_str(), (FLAGS_test_tmpdir + "/" + VALID1_FN).c_str());
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
    FILE* fp = fopen(f1.c_str(), "a");
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
    ASSERT(badsyntax.size() == 3);  // 

    TemplateNamelist::RegisterTemplate("A_non_existant_file.tpl");
    names = TemplateNamelist::GetList();
    ASSERT(names.size() == 7);
    missing = TemplateNamelist::GetMissingList(false);
    ASSERT(missing.size() == 1);  // we did not refresh the missing list
    missing = TemplateNamelist::GetMissingList(true);
    // After refresh, the file we just registerd also added in missing list
    ASSERT(missing.size() == 2);
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
  TemplateUnittest::TestInheritence();
  TemplateUnittest::TestExpand();

  TemplateUnittest::TestGetTemplate();
  TemplateUnittest::TestStrip();
  TemplateUnittest::TestReloadIfChanged();

  TemplateUnittest::TestTemplateRootDirectory();
  TemplateUnittest::TestTemplateNamelist();

  TemplateUnittest::TestThreadSafety();

  printf("DONE\n");
  return 0;
}
