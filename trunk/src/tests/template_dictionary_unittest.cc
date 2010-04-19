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
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_modifiers.h>
#include <ctemplate/per_expand_data.h>
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
class FooEscaper : public TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const PerExpandData*,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
    outbuf->Emit("foo");
  }
};

// test escape-functor that replaces all input with ""
class NullEscaper : public TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const PerExpandData*,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
  }
};

// first does javascript-escaping, then html-escaping
class DoubleEscaper : public TemplateModifier {
 public:
  void Modify(const char* in, size_t inlen,
              const PerExpandData* data,
              ExpandEmitter* outbuf, const string& arg) const {
    assert(arg.empty());    // we don't take an argument
    string tmp = javascript_escape(in, inlen);
    html_escape.Modify(tmp.data(), tmp.size(), data, outbuf, "");
  }
};

static const TemplateDictionary* GetSectionDict(
    const TemplateDictionary* d, const char* name, int i) {
  TemplateDictionaryPeer peer(d);
  vector<const TemplateDictionary*> dicts;
  ASSERT(peer.GetSectionDictionaries(name, &dicts) >= i);
  return dicts[i];
}
static const TemplateDictionary* GetIncludeDict(
    const TemplateDictionary* d, const char* name, int i) {
  TemplateDictionaryPeer peer(d);
  vector<const TemplateDictionary*> dicts;
  ASSERT(peer.GetIncludeDictionaries(name, &dicts) >= i);
  return dicts[i];
}

static void SetUp() {
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
    ASSERT(peer.ValueIs("FOO", "foo"));
    ASSERT(peer.ValueIs("FOO2", "foo2"));
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

static void TestSetValueWithoutCopy() {
  UnsafeArena arena(100);
  TemplateDictionary dict("Test arena", &arena);

  char value[32];
  snprintf(value, sizeof(value), "%s", "value");

  const void* const ptr = arena.Alloc(0);
  dict.SetValueWithoutCopy("key", value);
  // We shouldn't have copied the value string.
  ASSERT(ptr == arena.Alloc(0));

  TemplateDictionaryPeer peer(&dict);
  ASSERT(peer.ValueIs("key", "value"));
  // If our content changes, so does what's in the dictionary -- but
  // only the contents of the buffer, not its length!
  snprintf(value, sizeof(value), "%s", "not_value");
  ASSERT(peer.ValueIs("key", "not_v"));   // sizeof("not_v") == sizeof("value")
}

static void TestSetValueWithNUL() {
  TemplateDictionary dict("test_SetValueWithNUL", NULL);
  TemplateDictionaryPeer peer(&dict);

  // Test copying char*s, strings, and explicit TemplateStrings
  dict.SetValue(string("FOO\0BAR", 7), string("QUX\0QUUX", 8));
  dict.SetGlobalValue(string("GOO\0GAR", 7), string("GUX\0GUUX", 8));

  // FOO should not match FOO\0BAR
  ASSERT(peer.ValueIs("FOO", ""));
  ASSERT(peer.ValueIs("GOO", ""));

  ASSERT(peer.ValueIs(string("FOO\0BAR", 7), string("QUX\0QUUX", 8)));
  ASSERT(peer.ValueIs(string("GOO\0GAR", 7), string("GUX\0GUUX", 8)));

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

  ASSERT(peer.ValueIs("INT", "5"));
  ASSERT(peer.ValueIs("-INT", "-5"));
  string dump;
  dict.DumpToString(&dump);
  ASSERT_STRSTR(dump.c_str(), "\n   INT: >5<\n");
  ASSERT_STRSTR(dump.c_str(), "\n   -INT: >-5<\n");
}

static void TestSetFormattedValue() {
  TemplateDictionary dict("test_SetFormattedValue", NULL);
  TemplateDictionaryPeer peer(&dict);

  dict.SetFormattedValue(TemplateString("PRINTF", sizeof("PRINTF")-1),
                         "%s test %04d", "template test", 1);

  ASSERT(peer.ValueIs("PRINTF", "template test test 0001"));
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
  ASSERT(peer.ValueIs("PRINTF", expected.c_str()));
  string dump2;
  dict.DumpToString(&dump2);
  expected = string("\n   PRINTF: >") + expected + string("<\n");
  ASSERT_STRSTR(dump2.c_str(), expected.c_str());
}

static void TestSetEscapedValue() {
  TemplateDictionary dict("test_SetEscapedValue", NULL);
  TemplateDictionaryPeer peer(&dict);

  dict.SetEscapedValue("hardest HTML",
                       "<A HREF='foo'\nid=\"bar\t\t&&\vbaz\">",
                       html_escape);
  dict.SetEscapedValue("hardest JS",
                       ("f = 'foo';\r\n\tprint \"\\&foo = \b\", \"foo\""),
                       javascript_escape);
  dict.SetEscapedValue("query escape 0", "",
                       url_query_escape);

  ASSERT(peer.ValueIs("hardest HTML",
                      "&lt;A HREF=&#39;foo&#39; id=&quot;bar  &amp;&amp; "
                      "baz&quot;&gt;"));
  ASSERT(peer.ValueIs("hardest JS",
                      "f \\x3d \\x27foo\\x27;\\r\\n\\tprint \\x22\\\\\\x26foo "
                      "\\x3d \\b\\x22, \\x22foo\\x22"));
  ASSERT(peer.ValueIs("query escape 0", ""));

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

  ASSERT(peer.ValueIs("easy foo", "foo"));
  ASSERT(peer.ValueIs("harder foo", "foo"));
  ASSERT(peer.ValueIs("easy double", "doo"));
  ASSERT(peer.ValueIs("harder double",
                      "\\x3cA HREF\\x3d\\x27foo\\x27\\x3e\\n"));
  ASSERT(peer.ValueIs("hardest double",
                      "print \\x22\\x3cA HREF\\x3d\\x27foo\\x27\\x3e\\x22;"
                      "\\r\\n\\\\1;"));
}

static void TestSetEscapedFormattedValue() {
  TemplateDictionary dict("test_SetEscapedFormattedValue", NULL);
  TemplateDictionaryPeer peer(&dict);

  dict.SetEscapedFormattedValue("HTML", html_escape,
                                "This is <%s> #%.4f", "a & b", 1.0/3);
  dict.SetEscapedFormattedValue("PRE", pre_escape,
                                "if %s x = %.4f;", "(a < 1 && b > 2)\n\t", 1.0/3);
  dict.SetEscapedFormattedValue("URL", url_query_escape,
                                "pageviews-%s", "r?egex");
  dict.SetEscapedFormattedValue("XML", xml_escape,
                                "This&is%s -- ok?", "just&");

  ASSERT(peer.ValueIs("HTML",
                      "This is &lt;a &amp; b&gt; #0.3333"));
  ASSERT(peer.ValueIs("PRE",
                      "if (a &lt; 1 &amp;&amp; b &gt; 2)\n\t x = 0.3333;"));
  ASSERT(peer.ValueIs("URL", "pageviews-r%3Fegex"));

  ASSERT(peer.ValueIs("XML", "This&amp;isjust&amp; -- ok?"));
}

static const StaticTemplateString kSectName =
    STS_INIT(kSectName, "test_SetAddSectionDictionary");

static void TestAddSectionDictionary() {
  // For fun, we'll make this constructor take a static template string.
  TemplateDictionary dict(kSectName, NULL);
  TemplateDictionaryPeer peer(&dict);
  dict.SetValue("TOPLEVEL", "foo");
  dict.SetValue("TOPLEVEL2", "foo2");

  TemplateDictionary* subdict_1a = dict.AddSectionDictionary("section1");
  TemplateDictionary* subdict_1b = dict.AddSectionDictionary("section1");
  TemplateDictionaryPeer subdict_1a_peer(subdict_1a);
  TemplateDictionaryPeer subdict_1b_peer(subdict_1b);
  subdict_1a->SetValue("SUBLEVEL", "subfoo");
  subdict_1b->SetValue("SUBLEVEL", "subbar");

  TemplateDictionary* subdict_2 = dict.AddSectionDictionary("section2");
  TemplateDictionaryPeer subdict_2_peer(subdict_2);
  subdict_2->SetValue("TOPLEVEL", "bar");    // overriding top dict
  TemplateDictionary* subdict_2_1 = subdict_2->AddSectionDictionary("sub");
  TemplateDictionaryPeer subdict_2_1_peer(subdict_2_1);
  subdict_2_1->SetIntValue("GLOBAL", 21);    // overrides value in setUp()

  // Verify that all variables that should be look-up-able are, and that
  // we have proper precedence.
  ASSERT(peer.ValueIs("GLOBAL", "top"));
  ASSERT(peer.ValueIs("TOPLEVEL", "foo"));
  ASSERT(peer.ValueIs("TOPLEVEL2", "foo2"));
  ASSERT(peer.ValueIs("SUBLEVEL", ""));

  ASSERT(subdict_1a_peer.ValueIs("GLOBAL", "top"));
  ASSERT(subdict_1a_peer.ValueIs("TOPLEVEL", "foo"));
  ASSERT(subdict_1a_peer.ValueIs("TOPLEVEL2", "foo2"));
  ASSERT(subdict_1a_peer.ValueIs("SUBLEVEL", "subfoo"));

  ASSERT(subdict_1b_peer.ValueIs("GLOBAL", "top"));
  ASSERT(subdict_1b_peer.ValueIs("TOPLEVEL", "foo"));
  ASSERT(subdict_1b_peer.ValueIs("TOPLEVEL2", "foo2"));
  ASSERT(subdict_1b_peer.ValueIs("SUBLEVEL", "subbar"));

  ASSERT(subdict_2_peer.ValueIs("GLOBAL", "top"));
  ASSERT(subdict_2_peer.ValueIs("TOPLEVEL", "bar"));
  ASSERT(subdict_2_peer.ValueIs("TOPLEVEL2", "foo2"));
  ASSERT(subdict_2_peer.ValueIs("SUBLEVEL", ""));

  ASSERT(subdict_2_1_peer.ValueIs("GLOBAL", "21"));
  ASSERT(subdict_2_1_peer.ValueIs("TOPLEVEL", "bar"));
  ASSERT(subdict_2_1_peer.ValueIs("TOPLEVEL2", "foo2"));
  ASSERT(subdict_2_1_peer.ValueIs("SUBLEVEL", ""));

  // Verify that everyone knows about its sub-dictionaries, and also
  // that these go 'up the chain' on lookup failure
  ASSERT(!peer.IsHiddenSection("section1"));
  ASSERT(!peer.IsHiddenSection("section2"));
  ASSERT(peer.IsHiddenSection("section3"));
  ASSERT(peer.IsHiddenSection("sub"));
  ASSERT(!subdict_1a_peer.IsHiddenSection("section1"));
  ASSERT(subdict_1a_peer.IsHiddenSection("sub"));
  ASSERT(!subdict_2_peer.IsHiddenSection("sub"));
  ASSERT(!subdict_2_1_peer.IsHiddenSection("sub"));

  // We should get the dictionary-lengths right as well
  vector<const TemplateDictionary*> dummy;
  ASSERT(peer.GetSectionDictionaries("section1", &dummy) == 2);
  ASSERT(peer.GetSectionDictionaries("section2", &dummy) == 1);
  ASSERT(subdict_2_peer.GetSectionDictionaries("sub", &dummy) == 1);
  // Test some of the values
  ASSERT(TemplateDictionaryPeer(GetSectionDict(&dict, "section1", 0))
         .ValueIs("SUBLEVEL", "subfoo"));
  ASSERT(TemplateDictionaryPeer(GetSectionDict(&dict, "section1", 1))
         .ValueIs("SUBLEVEL", "subbar"));
  ASSERT(TemplateDictionaryPeer(GetSectionDict(&dict, "section2", 0))
         .ValueIs("TOPLEVEL", "bar"));
  ASSERT(TemplateDictionaryPeer(
           GetSectionDict(GetSectionDict(&dict, "section2", 0), "sub", 0))
         .ValueIs("TOPLEVEL", "bar"));
  ASSERT(TemplateDictionaryPeer(
             GetSectionDict(GetSectionDict(&dict, "section2", 0), "sub", 0))
         .ValueIs("GLOBAL", "21"));

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
  TemplateDictionaryPeer subdict_peer(subdict);
  subdict->SetValue("TOPLEVEL", "bar");
  dict.ShowSection("section3");

  ASSERT(subdict_peer.ValueIs("TOPLEVEL", "bar"));

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
  TemplateDictionaryPeer peer(&dict);
  dict.SetValue("TOPLEVEL", "foo");

  dict.SetValueAndShowSection("INSEC", "bar", "SEC1");
  dict.SetValueAndShowSection("NOTINSEC", "", "SEC2");
  dict.SetValueAndShowSection("NOTINSEC2", NULL, "SEC3");

  dict.SetEscapedValueAndShowSection("EINSEC", "a & b",
                                     html_escape,
                                     "SEC4");
  dict.SetEscapedValueAndShowSection("EINSEC2", "a beautiful poem",
                                     FooEscaper(),
                                     "SEC5");
  dict.SetEscapedValueAndShowSection("NOTEINSEC", "a long string",
                                     NullEscaper(),
                                     "SEC6");

  ASSERT(!peer.IsHiddenSection("SEC1"));
  ASSERT(peer.IsHiddenSection("SEC2"));
  ASSERT(peer.IsHiddenSection("SEC3"));
  ASSERT(!peer.IsHiddenSection("SEC4"));
  ASSERT(!peer.IsHiddenSection("SEC5"));
  ASSERT(peer.IsHiddenSection("SEC6"));

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

  TemplateDictionaryPeer peer(&dict);
  TemplateDictionaryPeer subdict_peer(subdict);
  TemplateDictionaryPeer subsubdict_peer(subsubdict);
  TemplateDictionaryPeer includedict_peer(includedict);

  // Setting a template value after sub dictionaries are created should
  // affect the sub dictionaries as well.
  dict.SetTemplateGlobalValue("TEMPLATEVAL", "templateval");
  ASSERT(peer.ValueIs("TEMPLATEVAL", "templateval"));
  ASSERT(subdict_peer.ValueIs("TEMPLATEVAL", "templateval"));
  ASSERT(subsubdict_peer.ValueIs("TEMPLATEVAL", "templateval"));
  ASSERT(includedict_peer.ValueIs("TEMPLATEVAL", "templateval"));

  // sub dictionaries after you set the template value should also
  // get the template value
  TemplateDictionary* subdict2 = dict.AddSectionDictionary("section2");
  TemplateDictionary* includedict2 = dict.AddIncludeDictionary("include2");
  TemplateDictionaryPeer subdict2_peer(subdict2);
  TemplateDictionaryPeer includedict2_peer(includedict2);

  ASSERT(subdict2_peer.ValueIs("TEMPLATEVAL", "templateval"));
  ASSERT(includedict2_peer.ValueIs("TEMPLATEVAL", "templateval"));

  // setting a template value on a sub dictionary should affect all the other
  // sub dictionaries and the parent as well
  subdict->SetTemplateGlobalValue("TEMPLATEVAL2", "templateval2");
  ASSERT(peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(subdict_peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(subsubdict_peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(includedict_peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(subdict2_peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(includedict2_peer.ValueIs("TEMPLATEVAL2", "templateval2"));

  includedict->SetTemplateGlobalValue("TEMPLATEVAL3", "templateval3");
  ASSERT(peer.ValueIs("TEMPLATEVAL3", "templateval3"));
  ASSERT(subdict_peer.ValueIs("TEMPLATEVAL3", "templateval3"));
  ASSERT(subsubdict_peer.ValueIs("TEMPLATEVAL3", "templateval3"));
  ASSERT(includedict_peer.ValueIs("TEMPLATEVAL3", "templateval3"));
  ASSERT(subdict2_peer.ValueIs("TEMPLATEVAL3", "templateval3"));
  ASSERT(includedict2_peer.ValueIs("TEMPLATEVAL3", "templateval3"));

  // you should be able to override a template value with a regular value
  // and the overwritten regular value should pass on to its children
  subdict->SetValue("TEMPLATEVAL2", "subdictval");
  includedict->SetValue("TEMPLATEVAL2", "includedictval");
  ASSERT(peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(subdict_peer.ValueIs("TEMPLATEVAL2", "subdictval"));
  ASSERT(subsubdict_peer.ValueIs("TEMPLATEVAL2", "subdictval"));
  ASSERT(includedict_peer.ValueIs("TEMPLATEVAL2", "includedictval"));
  ASSERT(subdict2_peer.ValueIs("TEMPLATEVAL2", "templateval2"));
  ASSERT(includedict2_peer.ValueIs("TEMPLATEVAL2", "templateval2"));

  // A section shown template-globally will be shown in all its children.
  dict.ShowTemplateGlobalSection("ShownTemplateGlobalSection");
  ASSERT(!peer.IsHiddenSection("ShownTemplateGlobalSection"));

  ASSERT(!subdict2_peer.IsHiddenSection("ShownTemplateGlobalSection"));
  ASSERT(!subsubdict_peer.IsHiddenSection("ShownTemplateGlobalSection"));

  // Showing a template-global section in a child will show it in all templates
  // in the tree
  subdict->ShowTemplateGlobalSection("ShownFromAChild");
  ASSERT(!peer.IsHiddenSection("ShownFromAChild"));
  ASSERT(!subsubdict_peer.IsHiddenSection("ShownFromAChild"));

  // Asking for a section that doesn't exist shouldn't cause infinite recursion
  peer.IsHiddenSection("NAVBAR_SECTION");
}

static void TestSetTemplateGlobalValueWithoutCopy() {
  UnsafeArena arena(100);
  TemplateDictionary dict("Test arena", &arena);
  TemplateDictionaryPeer peer(&dict);

  char value[32];
  snprintf(value, sizeof(value), "%s", "value");

  const void* const ptr = arena.Alloc(0);
  dict.SetTemplateGlobalValueWithoutCopy("key", value);
  // We shouldn't have copied the value string.
  ASSERT(ptr == arena.Alloc(0));

  ASSERT(peer.ValueIs("key", "value"));
  // If our content changes, so does what's in the dictionary -- but
  // only the contents of the buffer, not its length!
  snprintf(value, sizeof(value), "%s", "not_value");
  ASSERT(peer.ValueIs("key", "not_v"));   // sizeof("not_v") == sizeof("value")
}

static void TestAddIncludeDictionary() {
  TemplateDictionary dict("test_SetAddIncludeDictionary", NULL);
  TemplateDictionaryPeer peer(&dict);
  dict.SetValue("TOPLEVEL", "foo");
  dict.SetValue("TOPLEVEL2", "foo2");
  dict.SetTemplateGlobalValue("TEMPLATELEVEL", "foo3");

  TemplateDictionary* subdict_1a = dict.AddIncludeDictionary("include1");
  TemplateDictionaryPeer subdict_1a_peer(subdict_1a);
  subdict_1a->SetFilename("incfile1a");
  TemplateDictionary* subdict_1b = dict.AddIncludeDictionary("include1");
  TemplateDictionaryPeer subdict_1b_peer(subdict_1b);
  // Let's try not calling SetFilename on this one.
  subdict_1a->SetValue("SUBLEVEL", "subfoo");
  subdict_1b->SetValue("SUBLEVEL", "subbar");

  TemplateDictionary* subdict_2 = dict.AddIncludeDictionary("include2");
  TemplateDictionaryPeer subdict_2_peer(subdict_2);
  subdict_2->SetFilename("foo/bar");
  subdict_2->SetValue("TOPLEVEL", "bar");    // overriding top dict
  // overriding template dict
  subdict_2->SetValue("TEMPLATELEVEL", "subfoo3");
  TemplateDictionary* subdict_2_1 = subdict_2->AddIncludeDictionary("sub");
  TemplateDictionaryPeer subdict_2_1_peer(subdict_2_1);
  subdict_2_1->SetFilename("baz");
  subdict_2_1->SetIntValue("GLOBAL", 21);    // overrides value in setUp()

  // Verify that all variables that should be look-up-able are, and that
  // we have proper precedence.  Unlike with sections, includes lookups
  // do not go 'up the chain'.
  ASSERT(peer.ValueIs("GLOBAL", "top"));
  ASSERT(peer.ValueIs("TOPLEVEL", "foo"));
  ASSERT(peer.ValueIs("TOPLEVEL2", "foo2"));
  ASSERT(peer.ValueIs("TEMPLATELEVEL", "foo3"));
  ASSERT(peer.ValueIs("SUBLEVEL", ""));

  ASSERT(subdict_1a_peer.ValueIs("GLOBAL", "top"));
  ASSERT(subdict_1a_peer.ValueIs("TOPLEVEL", ""));
  ASSERT(subdict_1a_peer.ValueIs("TOPLEVEL2", ""));
  ASSERT(subdict_1a_peer.ValueIs("TEMPLATELEVEL", "foo3"));
  ASSERT(subdict_1a_peer.ValueIs("SUBLEVEL", "subfoo"));

  ASSERT(subdict_1b_peer.ValueIs("GLOBAL", "top"));
  ASSERT(subdict_1b_peer.ValueIs("TOPLEVEL", ""));
  ASSERT(subdict_1b_peer.ValueIs("TOPLEVEL2", ""));
  ASSERT(subdict_1b_peer.ValueIs("SUBLEVEL", "subbar"));

  ASSERT(subdict_2_peer.ValueIs("GLOBAL", "top"));
  ASSERT(subdict_2_peer.ValueIs("TOPLEVEL", "bar"));
  ASSERT(subdict_2_peer.ValueIs("TOPLEVEL2", ""));
  ASSERT(subdict_2_peer.ValueIs("TEMPLATELEVEL", "subfoo3"));
  ASSERT(subdict_2_peer.ValueIs("SUBLEVEL", ""));

  ASSERT(subdict_2_1_peer.ValueIs("GLOBAL", "21"));
  ASSERT(subdict_2_1_peer.ValueIs("TOPLEVEL", ""));
  ASSERT(subdict_2_1_peer.ValueIs("TOPLEVEL2", ""));
  ASSERT(subdict_2_1_peer.ValueIs("SUBLEVEL", ""));

  // Verify that everyone knows about its sub-dictionaries, but that
  // these do not try to go 'up the chain' on lookup failure
  ASSERT(!peer.IsHiddenTemplate("include1"));
  ASSERT(!peer.IsHiddenTemplate("include2"));
  ASSERT(peer.IsHiddenTemplate("include3"));
  ASSERT(peer.IsHiddenTemplate("sub"));
  ASSERT(subdict_1a_peer.IsHiddenTemplate("include1"));
  ASSERT(subdict_1a_peer.IsHiddenTemplate("sub"));
  ASSERT(!subdict_2_peer.IsHiddenTemplate("sub"));
  ASSERT(subdict_2_1_peer.IsHiddenTemplate("sub"));

  // We should get the dictionary-lengths right as well
  vector<const TemplateDictionary*> dummy;
  ASSERT(peer.GetIncludeDictionaries("include1", &dummy) == 2);
  ASSERT(peer.GetIncludeDictionaries("include2", &dummy) == 1);
  ASSERT(subdict_2_peer.GetIncludeDictionaries("sub", &dummy) == 1);

  // We can also test the include-files are right
  ASSERT(peer.GetIncludeDictionaries("include1", &dummy) == 2);
  ASSERT(peer.GetIncludeDictionaries("include2", &dummy) == 1);
  ASSERT(subdict_2_peer.GetIncludeDictionaries("sub", &dummy) == 1);
  // Test some of the values
  ASSERT(TemplateDictionaryPeer(GetIncludeDict(&dict, "include1", 0))
         .ValueIs("SUBLEVEL", "subfoo"));
  ASSERT(TemplateDictionaryPeer(GetIncludeDict(&dict, "include1", 1))
         .ValueIs("SUBLEVEL", "subbar"));
  ASSERT(TemplateDictionaryPeer(GetIncludeDict(&dict, "include2", 0))
         .ValueIs("TOPLEVEL", "bar"));
  ASSERT(TemplateDictionaryPeer(
           GetIncludeDict(GetIncludeDict(&dict, "include2", 0), "sub", 0))
         .ValueIs("TOPLEVEL", ""));
  ASSERT(TemplateDictionaryPeer(
             GetIncludeDict(GetIncludeDict(&dict, "include2", 0), "sub", 0))
         .ValueIs("GLOBAL", "21"));
  // We can test the include-names as well
  ASSERT_STREQ(peer.GetIncludeTemplateName("include1", 0), "incfile1a");
  ASSERT_STREQ(peer.GetIncludeTemplateName("include1", 1), "");
  ASSERT_STREQ(peer.GetIncludeTemplateName("include2", 0), "foo/bar");
  ASSERT_STREQ(TemplateDictionaryPeer(GetIncludeDict(&dict, "include2", 0))
               .GetIncludeTemplateName("sub", 0),
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
  PerExpandData per_expand_data;
  const void* data = "test";
  per_expand_data.InsertForModifiers("a", data);
  ASSERT(data == per_expand_data.LookupForModifiers("a"));
}

static void TestIterator() {
  // Build up a nice community of TemplateDictionaries.
  TemplateDictionary farm("Farm");
  TemplateDictionaryPeer farm_peer(&farm);
  TemplateDictionaryInterface* grey_barn =
      farm.AddIncludeDictionary("BARN");
  TemplateDictionaryInterface* duck_pond =
      farm.AddIncludeDictionary("POND");
  TemplateDictionaryInterface* cattle_pond =
      farm.AddIncludeDictionary("POND");
  TemplateDictionaryInterface* irrigation_pond =
      farm.AddIncludeDictionary("POND");

  // A section name with repeated sections
  TemplateDictionaryInterface* lillies = farm.AddSectionDictionary("FLOWERS");
  TemplateDictionaryInterface* lilacs = farm.AddSectionDictionary("FLOWERS");
  TemplateDictionaryInterface* daisies = farm.AddSectionDictionary("FLOWERS");
  // A section name with one repeat
  TemplateDictionaryInterface* wheat = farm.AddSectionDictionary("WHEAT");
  // A section name, just shown
  farm.ShowSection("CORN");

  // Check that the iterators expose all of the dictionaries.
  TemplateDictionaryPeer::Iterator* barns =
      farm_peer.CreateTemplateIterator("BARN");
  ASSERT(barns->HasNext());
  ASSERT(&barns->Next() == grey_barn);
  ASSERT(!barns->HasNext());
  delete barns;

  TemplateDictionaryPeer::Iterator* ponds =
      farm_peer.CreateTemplateIterator("POND");
  ASSERT(ponds->HasNext());
  ASSERT(&ponds->Next() == duck_pond);
  ASSERT(ponds->HasNext());
  ASSERT(&ponds->Next() == cattle_pond);
  ASSERT(ponds->HasNext());
  ASSERT(&ponds->Next() == irrigation_pond);
  ASSERT(!ponds->HasNext());
  delete ponds;

  TemplateDictionaryPeer::Iterator* flowers =
      farm_peer.CreateSectionIterator("FLOWERS");
  ASSERT(flowers->HasNext());
  ASSERT(&flowers->Next() == lillies);
  ASSERT(flowers->HasNext());
  ASSERT(&flowers->Next() == lilacs);
  ASSERT(flowers->HasNext());
  ASSERT(&flowers->Next() == daisies);
  ASSERT(!flowers->HasNext());
  delete flowers;

  TemplateDictionaryPeer::Iterator* crop =
      farm_peer.CreateSectionIterator("WHEAT");
  ASSERT(crop->HasNext());
  ASSERT(&crop->Next() == wheat);
  ASSERT(!crop->HasNext());
  delete crop;

  TemplateDictionaryPeer::Iterator* corn_crop =
      farm_peer.CreateSectionIterator("CORN");
  ASSERT(corn_crop->HasNext());
  ASSERT(&corn_crop->Next());  // ShowSection doesn't give us the dict back
  ASSERT(!corn_crop->HasNext());
  delete corn_crop;
}

static void TestIsHiddenSectionDefault() {
  TemplateDictionary dict("dict");
  TemplateDictionaryPeer peer(&dict);
  ASSERT(peer.IsHiddenSection("UNDEFINED"));
  ASSERT(!peer.IsUnhiddenSection("UNDEFINED"));
  dict.ShowSection("VISIBLE");
  ASSERT(!peer.IsHiddenSection("VISIBLE"));
  ASSERT(peer.IsUnhiddenSection("VISIBLE"));
}

int Main() {
  SetUp();

  TestSetValueAndTemplateStringAndArena();
  TestSetValueWithoutCopy();
  TestSetIntValue();
  TestSetFormattedValue();
  TestSetEscapedValue();
  TestSetEscapedFormattedValue();

  TestAddSectionDictionary();
  TestShowSection();
  TestSetValueAndShowSection();
  TestSetTemplateGlobalValue();
  TestSetTemplateGlobalValueWithoutCopy();
  TestAddIncludeDictionary();
  TestIsHiddenSectionDefault();

  TestIterator();

  TestMakeCopy(true);    // use our own arena
  TestMakeCopy(false);   // use fake arena

  TestSetModifierData();

  // We do this test last, because the NULs it inserts mess up all
  // the c-string-based tests that use strstr() and the like.
  TestSetValueWithNUL();

  printf("DONE.\n");
  return 0;
}

_END_GOOGLE_NAMESPACE_

int main(int argc, char** argv) {
  return GOOGLE_NAMESPACE::Main();
}
