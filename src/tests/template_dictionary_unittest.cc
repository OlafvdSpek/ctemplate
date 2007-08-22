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
#include <string.h>
#include <assert.h>
#include <vector>
#include <google/template_dictionary.h>
#include <google/template_modifiers.h>
#include "base/arena.h"
#include "tests/template_test_util.h"

using std::string;
using std::vector;

// This works in both debug mode and NDEBUG mode.
#define ASSERT(cond)  do {                                      \
  if (!(cond)) {                                                \
    printf("%s: %d: ASSERT FAILED: %s\n", __FILE__, __LINE__,   \
           #cond);                                              \
    assert(cond);                                               \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define ASSERT_STREQ(a, b)  do {                                          \
  if (strcmp((a), (b))) {                                                 \
    printf("%s: %d: ASSERT FAILED: '%s' != '%s'\n", __FILE__, __LINE__,   \
           (a), (b));                                                     \
    assert(!strcmp((a), (b)));                                            \
    exit(1);                                                              \
  }                                                                       \
} while (0)

#define ASSERT_STRSTR(text, substr)  do {                       \
  if (!strstr((text), (substr))) {                              \
    printf("%s: %d: ASSERT FAILED: '%s' not in '%s'\n",         \
           __FILE__, __LINE__, (substr), (text));               \
    assert(strstr((text), (substr)));                           \
    exit(1);                                                    \
  }                                                             \
} while (0)


_START_GOOGLE_NAMESPACE_

// test escape-functor that replaces all input with "foo"
class FooEscaper : public template_modifiers::TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const template_modifiers::ModifierData*,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
    outbuf->Emit("foo");
  }
};

// test escape-functor that replaces all input with ""
class NullEscaper : public template_modifiers::TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const template_modifiers::ModifierData*,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
  }
};

// first does javascript-escaping, then html-escaping
class DoubleEscaper : public template_modifiers::TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const template_modifiers::ModifierData* data,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
    string tmp = template_modifiers::javascript_escape(in, inlen);
    template_modifiers::html_escape.Modify(tmp.data(), tmp.size(),
                                           data, outbuf, "");
  }
};


// This is all in a single class just to make friendship easier:
// the TemplateDictionaryUnittest class can be listed as a friend
// once, and access all the internals of TemplateDictionary.
class TemplateDictionaryUnittest {
 private:
  // Some convenience routines
  static const TemplateDictionary* GetSectionDict(const TemplateDictionary* d,
                                                  const char* name, int i) {
    return d->GetDictionaries(name)[i];
  }
  static const TemplateDictionary* GetIncludeDict(const TemplateDictionary* d,
                                                  const char* name, int i) {
    return d->GetTemplateDictionaries(name)[i];
  }

 public:

  static void setUp() {
    TemplateDictionary::SetGlobalValue("GLOBAL", "top");
  }

  static void TestSetValueAndTemplateStringAndArena() {
    // Try both with the arena, and without.
    UnsafeArena arena(100);
    // We run the test with arena twice to double-check we don't ever delete it
    UnsafeArena* arenas[] = {&arena, &arena, NULL};
    for (int i = 0; i < sizeof(arenas)/sizeof(*arenas); ++i) {
      TemplateDictionary dict(string("test_arena") + char('0'+i), arenas[i]);

      // Test copying char*s, strings, and explicit TemplateStrings
      dict.SetValue("FOO", "foo");
      dict.SetValue(string("FOO2"), TemplateString("foo2andmore", 4));

      TemplateDictionaryPeer peer(&dict);
      // verify what happened
      ASSERT_STREQ(peer.GetSectionValue("FOO"), "foo");
      ASSERT_STREQ(peer.GetSectionValue("FOO2"), "foo2");
      string dump;
      dict.DumpToString(&dump);
      char expected[256];
      snprintf(expected, sizeof(expected),
               ("global dictionary {\n"
                "   BI_NEWLINE: >\n"
                "<\n"
                "   BI_SPACE: > <\n"
                "   GLOBAL: >top<\n"
                "};\n"
                "dictionary 'test_arena%d' {\n"
                "   FOO: >foo<\n"
                "   FOO2: >foo2<\n"
                "}\n"), i);
      ASSERT_STREQ(dump.c_str(), expected);
    }
  }

  static void TestSetValueWithNUL() {
    TemplateDictionary dict("test_SetValueWithNUL", NULL);
    TemplateDictionaryPeer peer(&dict);

    // Test copying char*s, strings, and explicit TemplateStrings
    dict.SetValue(string("FOO\0BAR", 7), string("QUX\0QUUX", 8));
    dict.SetGlobalValue(string("GOO\0GAR", 7), string("GUX\0GUUX", 8));

    // FOO should not match FOO\0BAR
    ASSERT_STREQ(peer.GetSectionValue("FOO"), "");
    ASSERT_STREQ(peer.GetSectionValue("GOO"), "");

    const char* r = peer.GetSectionValue(string("FOO\0BAR", 7));
    ASSERT(memcmp(r, "QUX\0QUUX", 8) == 0);
    r = peer.GetSectionValue(string("GOO\0GAR", 7));
    ASSERT(memcmp(r, "GUX\0GUUX", 8) == 0);

    string dump;
    dict.DumpToString(&dump);
    // We can't use ASSERT_STREQ here because of the embedded NULs.
    // They also require I count the length of the string by hand. :-(
    string expected(("global dictionary {\n"
                     "   BI_NEWLINE: >\n"
                     "<\n"
                     "   BI_SPACE: > <\n"
                     "   GLOBAL: >top<\n"
                     "   GOO\0GAR: >GUX\0GUUX<\n"
                     "};\n"
                     "dictionary 'test_SetValueWithNUL' {\n"
                     "   FOO\0BAR: >QUX\0QUUX<\n"
                     "}\n"),
                    160);
    ASSERT(dump == expected);
  }

  static void TestSetIntValue() {
    TemplateDictionary dict("test_SetIntValue", NULL);
    TemplateDictionaryPeer peer(&dict);

    dict.SetIntValue("INT", 5);
    // - is an illegal varname in templates, but perfectly fine in dicts
    dict.SetIntValue("-INT", -5);

    ASSERT_STREQ(peer.GetSectionValue("INT"), "5");
    ASSERT_STREQ(peer.GetSectionValue("-INT"), "-5");
    string dump;
    dict.DumpToString(&dump);
    ASSERT_STRSTR(dump.c_str(), "\n   INT: >5<\n");
    ASSERT_STRSTR(dump.c_str(), "\n   -INT: >-5<\n");
  }

  static void TestSetFormattedValue() {
    TemplateDictionary dict("test_SetFormattedValue", NULL);

    dict.SetFormattedValue(TemplateString("PRINTF", sizeof("PRINTF")-1),
                           "%s test %04d", "template test", 1);

    ASSERT_STREQ(dict.GetSectionValue("PRINTF"), "template test test 0001");
    string dump;
    dict.DumpToString(&dump);
    ASSERT_STRSTR(dump.c_str(), "\n   PRINTF: >template test test 0001<\n");

    // Now test something of size 4k or so, where we can't use scratchbuf
    dict.SetFormattedValue(TemplateString("PRINTF", sizeof("PRINTF")-1),
                           "%s test %04444d", "template test", 2);
    string expected("template test test ");
    for (int i = 0; i < 4443; ++i)
      expected.append("0");
    expected.append("2");
    ASSERT_STREQ(dict.GetSectionValue("PRINTF"), expected.c_str());
    string dump2;
    dict.DumpToString(&dump2);
    expected = string("\n   PRINTF: >") + expected + string("<\n");
    ASSERT_STRSTR(dump2.c_str(), expected.c_str());
  }

  static void TestSetEscapedValue() {
    TemplateDictionary dict("test_SetEscapedValue", NULL);

    dict.SetEscapedValue("hardest HTML",
                         "<A HREF='foo'\nid=\"bar\t\t&&\vbaz\">",
                         TemplateDictionary::html_escape);
    dict.SetEscapedValue("hardest JS",
                         ("f = 'foo';\r\n\tprint \"\\&foo = \b\", \"foo\""),
                         TemplateDictionary::javascript_escape);
    dict.SetEscapedValue("query escape 0", "",
                         TemplateDictionary::url_query_escape);

    ASSERT_STREQ(dict.GetSectionValue("hardest HTML"),
                 "&lt;A HREF=&#39;foo&#39; id=&quot;bar  &amp;&amp; "
                 "baz&quot;&gt;");
    ASSERT_STREQ(dict.GetSectionValue("hardest JS"),
                 "f \\x3d \\x27foo\\x27;\\r\\n\\tprint \\x22\\\\\\x26foo "
                 "\\x3d \\b\\x22, \\x22foo\\x22");
    ASSERT_STREQ(dict.GetSectionValue("query escape 0"), "");

    // Test using hand-made modifiers.
    FooEscaper foo_escaper;
    dict.SetEscapedValue("easy foo", "hello there!",
                         FooEscaper());
    dict.SetEscapedValue("harder foo", "so much to say\nso many foos",
                         foo_escaper);
    DoubleEscaper double_escaper;
    dict.SetEscapedValue("easy double", "doo",
                         double_escaper);
    dict.SetEscapedValue("harder double", "<A HREF='foo'>\n",
                         DoubleEscaper());
    dict.SetEscapedValue("hardest double",
                         "print \"<A HREF='foo'>\";\r\n\\1;",
                         double_escaper);

    ASSERT_STREQ(dict.GetSectionValue("easy foo"), "foo");
    ASSERT_STREQ(dict.GetSectionValue("harder foo"), "foo");
    ASSERT_STREQ(dict.GetSectionValue("easy double"), "doo");
    ASSERT_STREQ(dict.GetSectionValue("harder double"),
                 "\\x3cA HREF\\x3d\\x27foo\\x27\\x3e\\n");
    ASSERT_STREQ(dict.GetSectionValue("hardest double"),
                 "print \\x22\\x3cA HREF\\x3d\\x27foo\\x27\\x3e\\x22;"
                 "\\r\\n\\\\1;");
  }

  static void TestSetEscapedFormattedValue() {
    TemplateDictionary dict("test_SetEscapedFormattedValue", NULL);

    dict.SetEscapedFormattedValue("HTML", TemplateDictionary::html_escape,
                                  "This is <%s> #%.4f", "a & b", 1.0/3);
    dict.SetEscapedFormattedValue("PRE", TemplateDictionary::pre_escape,
                                  "if %s x = %.4f;", "(a < 1 && b > 2)\n\t", 1.0/3);
    dict.SetEscapedFormattedValue("URL", TemplateDictionary::url_query_escape,
                                  "pageviews-%s", "r?egex");
    dict.SetEscapedFormattedValue("XML", TemplateDictionary::xml_escape,
                                  "This&nbsp;is&nb%s -- ok?", "sp; #1&nbsp;");

    ASSERT_STREQ(dict.GetSectionValue("HTML"),
                 "This is &lt;a &amp; b&gt; #0.3333");
    ASSERT_STREQ(dict.GetSectionValue("PRE"),
                 "if (a &lt; 1 &amp;&amp; b &gt; 2)\n\t x = 0.3333;");
    ASSERT_STREQ(dict.GetSectionValue("URL"), "pageviews-r%3Fegex");

    ASSERT_STREQ(dict.GetSectionValue("XML"),
                 "This&#160;is&#160; #1&#160; -- ok?");
  }

  static void TestAddSectionDictionary() {
    TemplateDictionary dict("test_SetAddSectionDictionary", NULL);
    dict.SetValue("TOPLEVEL", "foo");
    dict.SetValue("TOPLEVEL2", "foo2");

    TemplateDictionary* subdict_1a = dict.AddSectionDictionary("section1");
    TemplateDictionary* subdict_1b = dict.AddSectionDictionary("section1");
    subdict_1a->SetValue("SUBLEVEL", "subfoo");
    subdict_1b->SetValue("SUBLEVEL", "subbar");

    TemplateDictionary* subdict_2 = dict.AddSectionDictionary("section2");
    subdict_2->SetValue("TOPLEVEL", "bar");    // overriding top dict
    TemplateDictionary* subdict_2_1 = subdict_2->AddSectionDictionary("sub");
    subdict_2_1->SetIntValue("GLOBAL", 21);    // overrides value in setUp()

    // Verify that all variables that should be look-up-able are, and that
    // we have proper precedence.
    ASSERT_STREQ(dict.GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(dict.GetSectionValue("TOPLEVEL"), "foo");
    ASSERT_STREQ(dict.GetSectionValue("TOPLEVEL2"), "foo2");
    ASSERT_STREQ(dict.GetSectionValue("SUBLEVEL"), "");

    ASSERT_STREQ(subdict_1a->GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(subdict_1a->GetSectionValue("TOPLEVEL"), "foo");
    ASSERT_STREQ(subdict_1a->GetSectionValue("TOPLEVEL2"), "foo2");
    ASSERT_STREQ(subdict_1a->GetSectionValue("SUBLEVEL"), "subfoo");

    ASSERT_STREQ(subdict_1b->GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(subdict_1b->GetSectionValue("TOPLEVEL"), "foo");
    ASSERT_STREQ(subdict_1b->GetSectionValue("TOPLEVEL2"), "foo2");
    ASSERT_STREQ(subdict_1b->GetSectionValue("SUBLEVEL"), "subbar");

    ASSERT_STREQ(subdict_2->GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(subdict_2->GetSectionValue("TOPLEVEL"), "bar");
    ASSERT_STREQ(subdict_2->GetSectionValue("TOPLEVEL2"), "foo2");
    ASSERT_STREQ(subdict_2->GetSectionValue("SUBLEVEL"), "");

    ASSERT_STREQ(subdict_2_1->GetSectionValue("GLOBAL"), "21");
    ASSERT_STREQ(subdict_2_1->GetSectionValue("TOPLEVEL"), "bar");
    ASSERT_STREQ(subdict_2_1->GetSectionValue("TOPLEVEL2"), "foo2");
    ASSERT_STREQ(subdict_2_1->GetSectionValue("SUBLEVEL"), "");

    // Verify that everyone knows about its sub-dictionaries, and also
    // that these go 'up the chain' on lookup failure
    ASSERT(!dict.IsHiddenSection("section1"));
    ASSERT(!dict.IsHiddenSection("section2"));
    ASSERT(dict.IsHiddenSection("section3"));
    ASSERT(dict.IsHiddenSection("sub"));
    ASSERT(!subdict_1a->IsHiddenSection("section1"));
    ASSERT(subdict_1a->IsHiddenSection("sub"));
    ASSERT(!subdict_2->IsHiddenSection("sub"));
    ASSERT(!subdict_2_1->IsHiddenSection("sub"));

    // We should get the dictionary-lengths right as well
    ASSERT(dict.GetDictionaries("section1").size() == 2);
    ASSERT(dict.GetDictionaries("section2").size() == 1);
    ASSERT(subdict_2->GetDictionaries("sub").size() == 1);
    // Test some of the values
    ASSERT_STREQ(GetSectionDict(&dict, "section1", 0)
                 ->GetSectionValue("SUBLEVEL"),
                 "subfoo");
    ASSERT_STREQ(GetSectionDict(&dict, "section1", 1)
                 ->GetSectionValue("SUBLEVEL"),
                 "subbar");
    ASSERT_STREQ(GetSectionDict(&dict, "section2", 0)
                 ->GetSectionValue("TOPLEVEL"),
                 "bar");
    ASSERT_STREQ(GetSectionDict(GetSectionDict(&dict, "section2", 0), "sub", 0)
                 ->GetSectionValue("TOPLEVEL"),
                 "bar");
    ASSERT_STREQ(GetSectionDict(GetSectionDict(&dict, "section2", 0), "sub", 0)
                 ->GetSectionValue("GLOBAL"),
                 "21");

    // Make sure we're making descriptive names
    ASSERT_STREQ(dict.name().c_str(),
                 "test_SetAddSectionDictionary");
    ASSERT_STREQ(subdict_1a->name().c_str(),
                 "test_SetAddSectionDictionary/section1#1");
    ASSERT_STREQ(subdict_1b->name().c_str(),
                 "test_SetAddSectionDictionary/section1#2");
    ASSERT_STREQ(subdict_2->name().c_str(),
                 "test_SetAddSectionDictionary/section2#1");
    ASSERT_STREQ(subdict_2_1->name().c_str(),
                 "test_SetAddSectionDictionary/section2#1/sub#1");

    // Finally, we can test the whole kit and kaboodle
    string dump;
    dict.DumpToString(&dump);
    const char* const expected =
      ("global dictionary {\n"
       "   BI_NEWLINE: >\n"
       "<\n"
       "   BI_SPACE: > <\n"
       "   GLOBAL: >top<\n"
       "};\n"
       "dictionary 'test_SetAddSectionDictionary' {\n"
       "   TOPLEVEL: >foo<\n"
       "   TOPLEVEL2: >foo2<\n"
       "   section section1 (dict 1 of 2) -->\n"
       "     dictionary 'test_SetAddSectionDictionary/section1#1' {\n"
       "       SUBLEVEL: >subfoo<\n"
       "     }\n"
       "   section section1 (dict 2 of 2) -->\n"
       "     dictionary 'test_SetAddSectionDictionary/section1#2' {\n"
       "       SUBLEVEL: >subbar<\n"
       "     }\n"
       "   section section2 (dict 1 of 1) -->\n"
       "     dictionary 'test_SetAddSectionDictionary/section2#1' {\n"
       "       TOPLEVEL: >bar<\n"
       "       section sub (dict 1 of 1) -->\n"
       "         dictionary 'test_SetAddSectionDictionary/section2#1/sub#1' {\n"
       "           GLOBAL: >21<\n"
       "         }\n"
       "     }\n"
       "}\n");
    ASSERT_STREQ(dump.c_str(), expected);
  }

  static void TestShowSection() {
    TemplateDictionary dict("test_SetShowSection", NULL);
    // Let's say what filename dict is associated with
    dict.SetFilename("bigmamainclude!.tpl");
    dict.SetValue("TOPLEVEL", "foo");
    dict.SetValue("TOPLEVEL2", "foo2");
    dict.ShowSection("section1");
    dict.ShowSection("section2");
    // Test calling ShowSection twice on the same section
    dict.ShowSection("section2");
    // Test that ShowSection is a no-op if called after AddSectionDictionary()
    TemplateDictionary* subdict = dict.AddSectionDictionary("section3");
    subdict->SetValue("TOPLEVEL", "bar");
    dict.ShowSection("section3");

    ASSERT_STREQ(subdict->GetSectionValue("TOPLEVEL"), "bar");

    // Since ShowSection() doesn't return a sub-dict, the only way to
    // probe what the dicts look like is via Dump()
    string dump;
    dict.DumpToString(&dump);
    const char* const expected =
      ("global dictionary {\n"
       "   BI_NEWLINE: >\n"
       "<\n"
       "   BI_SPACE: > <\n"
       "   GLOBAL: >top<\n"
       "};\n"
       "dictionary 'test_SetShowSection (intended for bigmamainclude!.tpl)' {\n"
       "   TOPLEVEL: >foo<\n"
       "   TOPLEVEL2: >foo2<\n"
       "   section section1 (dict 1 of 1) -->\n"
       "     dictionary 'empty dictionary' {\n"
       "     }\n"
       "   section section2 (dict 1 of 1) -->\n"
       "     dictionary 'empty dictionary' {\n"
       "     }\n"
       "   section section3 (dict 1 of 1) -->\n"
       "     dictionary 'test_SetShowSection/section3#1' {\n"
       "       TOPLEVEL: >bar<\n"
       "     }\n"
       "}\n");
    ASSERT_STREQ(dump.c_str(), expected);
  }

  static void TestSetValueAndShowSection() {
    TemplateDictionary dict("test_SetValueAndShowSection");
    dict.SetValue("TOPLEVEL", "foo");

    dict.SetValueAndShowSection("INSEC", "bar", "SEC1");
    dict.SetValueAndShowSection("NOTINSEC", "", "SEC2");
    dict.SetValueAndShowSection("NOTINSEC2", NULL, "SEC3");

    dict.SetEscapedValueAndShowSection("EINSEC", "a & b",
                                       TemplateDictionary::html_escape,
                                       "SEC4");
    dict.SetEscapedValueAndShowSection("EINSEC2", "a beautiful poem",
                                       FooEscaper(),
                                       "SEC5");
    dict.SetEscapedValueAndShowSection("NOTEINSEC", "a long string",
                                       NullEscaper(),
                                       "SEC6");

    ASSERT(!dict.IsHiddenSection("SEC1"));
    ASSERT(dict.IsHiddenSection("SEC2"));
    ASSERT(dict.IsHiddenSection("SEC3"));
    ASSERT(!dict.IsHiddenSection("SEC4"));
    ASSERT(!dict.IsHiddenSection("SEC5"));
    ASSERT(dict.IsHiddenSection("SEC6"));

    // Again, we don't get subdicts, so we have to dump to check values
    string dump;
    dict.DumpToString(&dump);
    const char* const expected =
      ("global dictionary {\n"
       "   BI_NEWLINE: >\n"
       "<\n"
       "   BI_SPACE: > <\n"
       "   GLOBAL: >top<\n"
       "};\n"
       "dictionary 'test_SetValueAndShowSection' {\n"
       "   TOPLEVEL: >foo<\n"
       "   section SEC1 (dict 1 of 1) -->\n"
       "     dictionary 'test_SetValueAndShowSection/SEC1#1' {\n"
       "       INSEC: >bar<\n"
       "     }\n"
       "   section SEC4 (dict 1 of 1) -->\n"
       "     dictionary 'test_SetValueAndShowSection/SEC4#1' {\n"
       "       EINSEC: >a &amp; b<\n"
       "     }\n"
       "   section SEC5 (dict 1 of 1) -->\n"
       "     dictionary 'test_SetValueAndShowSection/SEC5#1' {\n"
       "       EINSEC2: >foo<\n"
       "     }\n"
       "}\n");
    ASSERT_STREQ(dump.c_str(), expected);
  }

  static void TestSetTemplateGlobalValue() {
    // The functionality involving it passing across the included dictionaries
    // is also tested in TestAddIncludeDictionary
    TemplateDictionary dict("test_SetTemplateGlobalValue", NULL);
    TemplateDictionary* subdict = dict.AddSectionDictionary("section1");
    TemplateDictionary* subsubdict =
      subdict->AddSectionDictionary("section1's child");
    TemplateDictionary* includedict = dict.AddIncludeDictionary("include1");

    // Setting a template value after sub dictionaries are created should
    // affect the sub dictionaries as well.
    dict.SetTemplateGlobalValue("TEMPLATEVAL", "templateval");
    ASSERT_STREQ(dict.GetSectionValue("TEMPLATEVAL"), "templateval");
    ASSERT_STREQ(subdict->GetSectionValue("TEMPLATEVAL"), "templateval");
    ASSERT_STREQ(subsubdict->GetSectionValue("TEMPLATEVAL"), "templateval");
    ASSERT_STREQ(includedict->GetSectionValue("TEMPLATEVAL"), "templateval");

    // sub dictionaries after you set the template value should also
    // get the template value
    TemplateDictionary* subdict2 = dict.AddSectionDictionary("section2");
    TemplateDictionary* includedict2 = dict.AddIncludeDictionary("include2");
    ASSERT_STREQ(subdict2->GetSectionValue("TEMPLATEVAL"), "templateval");
    ASSERT_STREQ(includedict2->GetSectionValue("TEMPLATEVAL"), "templateval");

    // setting a template value on a sub dictionary should affect all the other
    // sub dictionaries and the parent as well
    subdict->SetTemplateGlobalValue("TEMPLATEVAL2", "templateval2");
    ASSERT_STREQ(dict.GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(subdict->GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(subsubdict->GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(includedict->GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(subdict2->GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(includedict2->GetSectionValue("TEMPLATEVAL2"), "templateval2");

    includedict->SetTemplateGlobalValue("TEMPLATEVAL3", "templateval3");
    ASSERT_STREQ(dict.GetSectionValue("TEMPLATEVAL3"), "templateval3");
    ASSERT_STREQ(subdict->GetSectionValue("TEMPLATEVAL3"), "templateval3");
    ASSERT_STREQ(subsubdict->GetSectionValue("TEMPLATEVAL3"), "templateval3");
    ASSERT_STREQ(includedict->GetSectionValue("TEMPLATEVAL3"), "templateval3");
    ASSERT_STREQ(subdict2->GetSectionValue("TEMPLATEVAL3"), "templateval3");
    ASSERT_STREQ(includedict2->GetSectionValue("TEMPLATEVAL3"), "templateval3");

    // you should be able to override a template value with a regular value
    // and the overwritten regular value should pass on to its children
    subdict->SetValue("TEMPLATEVAL2", "subdictval");
    includedict->SetValue("TEMPLATEVAL2", "includedictval");
    ASSERT_STREQ(dict.GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(subdict->GetSectionValue("TEMPLATEVAL2"), "subdictval");
    ASSERT_STREQ(subsubdict->GetSectionValue("TEMPLATEVAL2"), "subdictval");
    ASSERT_STREQ(includedict->GetSectionValue("TEMPLATEVAL2"),
                 "includedictval");
    ASSERT_STREQ(subdict2->GetSectionValue("TEMPLATEVAL2"), "templateval2");
    ASSERT_STREQ(includedict2->GetSectionValue("TEMPLATEVAL2"),
                 "templateval2");
  }

  static void TestAddIncludeDictionary() {
    TemplateDictionary dict("test_SetAddIncludeDictionary", NULL);
    dict.SetValue("TOPLEVEL", "foo");
    dict.SetValue("TOPLEVEL2", "foo2");
    dict.SetTemplateGlobalValue("TEMPLATELEVEL", "foo3");

    TemplateDictionary* subdict_1a = dict.AddIncludeDictionary("include1");
    subdict_1a->SetFilename("incfile1a");
    TemplateDictionary* subdict_1b = dict.AddIncludeDictionary("include1");
    // Let's try not calling SetFilename on this one.
    subdict_1a->SetValue("SUBLEVEL", "subfoo");
    subdict_1b->SetValue("SUBLEVEL", "subbar");

    TemplateDictionary* subdict_2 = dict.AddIncludeDictionary("include2");
    subdict_2->SetFilename("foo/bar");
    subdict_2->SetValue("TOPLEVEL", "bar");    // overriding top dict
    // overriding template dict
    subdict_2->SetValue("TEMPLATELEVEL", "subfoo3");
    TemplateDictionary* subdict_2_1 = subdict_2->AddIncludeDictionary("sub");
    subdict_2_1->SetFilename("baz");
    subdict_2_1->SetIntValue("GLOBAL", 21);    // overrides value in setUp()

    // Verify that all variables that should be look-up-able are, and that
    // we have proper precedence.  Unlike with sections, includes lookups
    // do not go 'up the chain'.
    ASSERT_STREQ(dict.GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(dict.GetSectionValue("TOPLEVEL"), "foo");
    ASSERT_STREQ(dict.GetSectionValue("TOPLEVEL2"), "foo2");
    ASSERT_STREQ(dict.GetSectionValue("TEMPLATELEVEL"), "foo3");
    ASSERT_STREQ(dict.GetSectionValue("SUBLEVEL"), "");

    ASSERT_STREQ(subdict_1a->GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(subdict_1a->GetSectionValue("TOPLEVEL"), "");
    ASSERT_STREQ(subdict_1a->GetSectionValue("TOPLEVEL2"), "");
    ASSERT_STREQ(subdict_1a->GetSectionValue("TEMPLATELEVEL"), "foo3");
    ASSERT_STREQ(subdict_1a->GetSectionValue("SUBLEVEL"), "subfoo");

    ASSERT_STREQ(subdict_1b->GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(subdict_1b->GetSectionValue("TOPLEVEL"), "");
    ASSERT_STREQ(subdict_1b->GetSectionValue("TOPLEVEL2"), "");
    ASSERT_STREQ(subdict_1b->GetSectionValue("SUBLEVEL"), "subbar");

    ASSERT_STREQ(subdict_2->GetSectionValue("GLOBAL"), "top");
    ASSERT_STREQ(subdict_2->GetSectionValue("TOPLEVEL"), "bar");
    ASSERT_STREQ(subdict_2->GetSectionValue("TOPLEVEL2"), "");
    ASSERT_STREQ(subdict_2->GetSectionValue("TEMPLATELEVEL"), "subfoo3");
    ASSERT_STREQ(subdict_2->GetSectionValue("SUBLEVEL"), "");

    ASSERT_STREQ(subdict_2_1->GetSectionValue("GLOBAL"), "21");
    ASSERT_STREQ(subdict_2_1->GetSectionValue("TOPLEVEL"), "");
    ASSERT_STREQ(subdict_2_1->GetSectionValue("TOPLEVEL2"), "");
    ASSERT_STREQ(subdict_2_1->GetSectionValue("SUBLEVEL"), "");

    // Verify that everyone knows about its sub-dictionaries, but that
    // these do not try to go 'up the chain' on lookup failure
    ASSERT(!dict.IsHiddenTemplate("include1"));
    ASSERT(!dict.IsHiddenTemplate("include2"));
    ASSERT(dict.IsHiddenTemplate("include3"));
    ASSERT(dict.IsHiddenTemplate("sub"));
    ASSERT(subdict_1a->IsHiddenTemplate("include1"));
    ASSERT(subdict_1a->IsHiddenTemplate("sub"));
    ASSERT(!subdict_2->IsHiddenTemplate("sub"));
    ASSERT(subdict_2_1->IsHiddenTemplate("sub"));

    // We should get the dictionary-lengths right as well
    ASSERT(dict.GetTemplateDictionaries("include1").size() == 2);
    ASSERT(dict.GetTemplateDictionaries("include2").size() == 1);
    ASSERT(subdict_2->GetTemplateDictionaries("sub").size() == 1);

    // We can also test the include-files are right
    ASSERT(dict.GetTemplateDictionaries("include1").size() == 2);
    ASSERT(dict.GetTemplateDictionaries("include2").size() == 1);
    ASSERT(subdict_2->GetTemplateDictionaries("sub").size() == 1);
    // Test some of the values
    ASSERT_STREQ(GetIncludeDict(&dict, "include1", 0)
                 ->GetSectionValue("SUBLEVEL"),
                 "subfoo");
    ASSERT_STREQ(GetIncludeDict(&dict, "include1", 1)
                 ->GetSectionValue("SUBLEVEL"),
                 "subbar");
    ASSERT_STREQ(GetIncludeDict(&dict, "include2", 0)
                 ->GetSectionValue("TOPLEVEL"),
                 "bar");
    ASSERT_STREQ(GetIncludeDict(GetIncludeDict(&dict, "include2", 0), "sub", 0)
                 ->GetSectionValue("TOPLEVEL"),
                 "");
    ASSERT_STREQ(GetIncludeDict(GetIncludeDict(&dict, "include2", 0), "sub", 0)
                 ->GetSectionValue("GLOBAL"),
                 "21");
    // We can test the include-names as well
    ASSERT_STREQ(dict.GetIncludeTemplateName("include1", 0), "incfile1a");
    ASSERT_STREQ(dict.GetIncludeTemplateName("include1", 1), "");
    ASSERT_STREQ(dict.GetIncludeTemplateName("include2", 0), "foo/bar");
    ASSERT_STREQ(GetIncludeDict(&dict, "include2", 0)
                 ->GetIncludeTemplateName("sub", 0),
                 "baz");

    // Make sure we're making descriptive names
    ASSERT_STREQ(dict.name().c_str(),
                 "test_SetAddIncludeDictionary");
    ASSERT_STREQ(subdict_1a->name().c_str(),
                 "test_SetAddIncludeDictionary/include1#1");
    ASSERT_STREQ(subdict_1b->name().c_str(),
                 "test_SetAddIncludeDictionary/include1#2");
    ASSERT_STREQ(subdict_2->name().c_str(),
                 "test_SetAddIncludeDictionary/include2#1");
    ASSERT_STREQ(subdict_2_1->name().c_str(),
                 "test_SetAddIncludeDictionary/include2#1/sub#1");

    // Finally, we can test the whole kit and kaboodle
    string dump;
    dict.DumpToString(&dump);
    const char* const expected =
      ("global dictionary {\n"
       "   BI_NEWLINE: >\n"
       "<\n"
       "   BI_SPACE: > <\n"
       "   GLOBAL: >top<\n"
       "};\n"
       "template dictionary {\n"
       "   TEMPLATELEVEL: >foo3<\n"
       "};\n"
       "dictionary 'test_SetAddIncludeDictionary' {\n"
       "   TOPLEVEL: >foo<\n"
       "   TOPLEVEL2: >foo2<\n"
       "   include-template include1 (dict 1 of 2, from incfile1a) -->\n"
       "     global dictionary {\n"
       "       BI_NEWLINE: >\n"
       "<\n"
       "       BI_SPACE: > <\n"
       "       GLOBAL: >top<\n"
       "     };\n"
       "     dictionary 'test_SetAddIncludeDictionary/include1#1 (intended for incfile1a)' {\n"
       "       SUBLEVEL: >subfoo<\n"
       "     }\n"
       "   include-template include1 (dict 2 of 2, **NO FILENAME SET; THIS DICT WILL BE IGNORED**) -->\n"
       "     global dictionary {\n"
       "       BI_NEWLINE: >\n"
       "<\n"
       "       BI_SPACE: > <\n"
       "       GLOBAL: >top<\n"
       "     };\n"
       "     dictionary 'test_SetAddIncludeDictionary/include1#2' {\n"
       "       SUBLEVEL: >subbar<\n"
       "     }\n"
       "   include-template include2 (dict 1 of 1, from foo/bar) -->\n"
       "     global dictionary {\n"
       "       BI_NEWLINE: >\n"
       "<\n"
       "       BI_SPACE: > <\n"
       "       GLOBAL: >top<\n"
       "     };\n"
       "     dictionary 'test_SetAddIncludeDictionary/include2#1 (intended for foo/bar)' {\n"
       "       TEMPLATELEVEL: >subfoo3<\n"
       "       TOPLEVEL: >bar<\n"
       "       include-template sub (dict 1 of 1, from baz) -->\n"
       "         global dictionary {\n"
       "           BI_NEWLINE: >\n"
       "<\n"
       "           BI_SPACE: > <\n"
       "           GLOBAL: >top<\n"
       "         };\n"
       "         dictionary 'test_SetAddIncludeDictionary/include2#1/sub#1 (intended for baz)' {\n"
       "           GLOBAL: >21<\n"
       "         }\n"
       "     }\n"
       "}\n");
    ASSERT_STREQ(dump.c_str(), expected);
  }

  static void TestMakeCopy(bool use_local_arena) {
    UnsafeArena local_arena(1024);
    UnsafeArena* arena = NULL;
    if (use_local_arena)
      arena = &local_arena;

    // First, let's make a non-trivial template dictionary (We use
    // 'new' because later we'll test deleting this dict but keeping
    // around the copy.)
    TemplateDictionary* dict = new TemplateDictionary("testdict", arena);

    dict->SetValue("TOPLEVEL", "foo");

    dict->SetTemplateGlobalValue("TEMPLATELEVEL", "foo3");

    TemplateDictionary* subdict_1a = dict->AddIncludeDictionary("include1");
    subdict_1a->SetFilename("incfile1a");
    subdict_1a->SetValue("SUBLEVEL", "subfoo");
    TemplateDictionary* subdict_1b = dict->AddIncludeDictionary("include1");
    // Let's try not calling SetFilename on this one.
    subdict_1b->SetValue("SUBLEVEL", "subbar");

    TemplateDictionary* subdict_2a = dict->AddSectionDictionary("section1");
    TemplateDictionary* subdict_2b = dict->AddSectionDictionary("section1");
    subdict_2a->SetValue("SUBLEVEL", "subfoo");
    subdict_2b->SetValue("SUBLEVEL", "subbar");
    TemplateDictionary* subdict_3 = dict->AddSectionDictionary("section2");
    subdict_3->SetValue("TOPLEVEL", "bar");    // overriding top dict
    TemplateDictionary* subdict_3_1 = subdict_3->AddSectionDictionary("sub");
    subdict_3_1->SetIntValue("GLOBAL", 21);    // overrides value in setUp()

    string orig;
    dict->DumpToString(&orig);

    // Make a copy
    TemplateDictionary* dict_copy = dict->MakeCopy("testdict", NULL);
    // Make sure it doesn't work to copy a sub-dictionary
    ASSERT(subdict_1a->MakeCopy("copy of subdict") == NULL);
    ASSERT(subdict_2a->MakeCopy("copy of subdict") == NULL);

    // Delete the original dict, to make sure the copy really is independent
    delete dict;
    dict = NULL;
    string copy;
    dict_copy->DumpToString(&copy);
    delete dict_copy;

    ASSERT_STREQ(orig.c_str(), copy.c_str());
  }

  static void TestSetModifierData() {
    TemplateDictionary dict("test_SetModifierData", NULL);
    const void* data = "test";
    dict.SetModifierData("a", data);
    ASSERT(data == dict.modifier_data()->Lookup("a"));
  }

};

_END_GOOGLE_NAMESPACE_

using GOOGLE_NAMESPACE::TemplateDictionaryUnittest;

int main(int argc, char** argv) {
  TemplateDictionaryUnittest::setUp();

  TemplateDictionaryUnittest::TestSetValueAndTemplateStringAndArena();
  TemplateDictionaryUnittest::TestSetIntValue();
  TemplateDictionaryUnittest::TestSetFormattedValue();
  TemplateDictionaryUnittest::TestSetEscapedValue();
  TemplateDictionaryUnittest::TestSetEscapedFormattedValue();

  TemplateDictionaryUnittest::TestAddSectionDictionary();
  TemplateDictionaryUnittest::TestShowSection();
  TemplateDictionaryUnittest::TestSetValueAndShowSection();
  TemplateDictionaryUnittest::TestSetTemplateGlobalValue();
  TemplateDictionaryUnittest::TestAddIncludeDictionary();

  TemplateDictionaryUnittest::TestMakeCopy(true);    // use our own arena
  TemplateDictionaryUnittest::TestMakeCopy(false);   // use fake arena

  TemplateDictionaryUnittest::TestSetModifierData();

  // We do this test last, because the NULs it inserts mess up all
  // the c-string-based tests that use strstr() and the like.
  TemplateDictionaryUnittest::TestSetValueWithNUL();

  printf("DONE.\n");
  return 0;
}
