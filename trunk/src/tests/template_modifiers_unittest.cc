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
// Author: Craig Silverstein

#include "config.h"
// This is for windows.  Even though we #include config.h, just like
// the files used to compile the dll, we are actually a *client* of
// the dll, so we don't get to decl anything.
#undef CTEMPLATE_DLL_DECL

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <google/template_dictionary.h>
#include <google/template_emitter.h>
#include <google/template_modifiers.h>
#include "tests/template_test_util.h"

using std::string;

_START_GOOGLE_NAMESPACE_

// This works in both debug mode and NDEBUG mode.
#define ASSERT_STREQ(a, b)  do {                                          \
  if (strcmp((a), (b))) {                                                 \
    printf("ASSERT FAILED, line %d: '%s' != '%s'\n", __LINE__, (a), (b)); \
    assert(!strcmp((a), (b)));                                            \
    exit(1);                                                              \
  }                                                                       \
} while (0)

class TemplateModifiersUnittest {
 public:
  static void TestHtmlEscape() {
    TemplateDictionary dict("TestHtmlEscape", NULL);
    dict.SetEscapedValue("easy HTML", "foo",
                         template_modifiers::html_escape);
    dict.SetEscapedValue("harder HTML", "foo & bar",
                         template_modifiers::html_escape);
    dict.SetEscapedValue("hardest HTML",
                         "<A HREF='foo'\nid=\"bar\t\t&&\vbaz\">",
                         template_modifiers::html_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy HTML"), "foo");
    ASSERT_STREQ(peer.GetSectionValue("harder HTML"), "foo &amp; bar");
    ASSERT_STREQ(peer.GetSectionValue("hardest HTML"),
                 "&lt;A HREF=&#39;foo&#39; id=&quot;bar  &amp;&amp; "
                 "baz&quot;&gt;");
  }

  static void TestSnippetEscape() {
    TemplateDictionary dict("TestSnippetEscape", NULL);
    dict.SetEscapedValue("easy snippet", "foo",
                         template_modifiers::snippet_escape);
    dict.SetEscapedValue("valid snippet",
                         "<b>foo<br> &amp; b<wbr>&shy;ar</b>",
                         template_modifiers::snippet_escape);
    dict.SetEscapedValue("invalid snippet",
                         "<b><A HREF='foo'\nid=\"bar\t\t&&{\vbaz\">",
                         template_modifiers::snippet_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy snippet"), "foo");
    ASSERT_STREQ(peer.GetSectionValue("valid snippet"),
                 "<b>foo<br> &amp; b<wbr>&shy;ar</b>");
    ASSERT_STREQ(peer.GetSectionValue("invalid snippet"),
                 "<b>&lt;A HREF=&#39;foo&#39; id=&quot;bar  &&amp;{ "
                 "baz&quot;&gt;</b>");
  }

  static void TestPreEscape() {
    TemplateDictionary dict("TestPreEscape", NULL);
    dict.SetEscapedValue("easy PRE", "foo",
                         template_modifiers::pre_escape);
    dict.SetEscapedValue("harder PRE", "foo & bar",
                         template_modifiers::pre_escape);
    dict.SetEscapedValue("hardest PRE",
                         " \"--\v--\f--\n--\t--&--<-->--'--\"",
                         template_modifiers::pre_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy PRE"), "foo");
    ASSERT_STREQ(peer.GetSectionValue("harder PRE"), "foo &amp; bar");
    ASSERT_STREQ(peer.GetSectionValue("hardest PRE"),
                 " &quot;--\v--\f--\n--\t--&amp;--&lt;--&gt;--&#39;--&quot;");
  }

  static void TestXmlEscape() {
    TemplateDictionary dict("TestXmlEscape", NULL);
    dict.SetEscapedValue("easy XML", "xoo",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("harder XML", "xoo & xar",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("harder XML 2", "&nbsp;",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("hardest XML", "&nbsp;xoo &nbsp;&nbsp; x&nbsp;x &nbsp",
                         template_modifiers::xml_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy XML"), "xoo");
    ASSERT_STREQ(peer.GetSectionValue("harder XML"), "xoo & xar");
    ASSERT_STREQ(peer.GetSectionValue("harder XML 2"), "&#160;");
    ASSERT_STREQ(peer.GetSectionValue("hardest XML"),
                 "&#160;xoo &#160;&#160; x&#160;x &nbsp");
  }

  static void TestValidateUrlEscape() {
    TemplateDictionary dict("TestValidateUrlEscape", NULL);
    dict.SetEscapedValue("easy http URL", "http://www.google.com",
                         template_modifiers::validate_url_and_html_escape);
    dict.SetEscapedValue("harder https URL",
                         "https://www.google.com/search?q=f&hl=en",
                         template_modifiers::validate_url_and_html_escape);
    dict.SetEscapedValue("easy javascript URL",
                         "javascript:alert(document.cookie)",
                         template_modifiers::validate_url_and_html_escape);
    dict.SetEscapedValue("harder javascript URL",
                         "javascript:alert(10/5)",
                         template_modifiers::validate_url_and_html_escape);
    dict.SetEscapedValue("easy relative URL",
                         "foobar.html",
                         template_modifiers::validate_url_and_html_escape);
    dict.SetEscapedValue("harder relative URL",
                         "/search?q=green flowers&hl=en",
                         template_modifiers::validate_url_and_html_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy http URL"),
                 "http://www.google.com");
    ASSERT_STREQ(peer.GetSectionValue("harder https URL"),
                 "https://www.google.com/search?q=f&amp;hl=en");
    ASSERT_STREQ(peer.GetSectionValue("easy javascript URL"),
                 "#");
    ASSERT_STREQ(peer.GetSectionValue("harder javascript URL"),
                 "#");
    ASSERT_STREQ(peer.GetSectionValue("easy relative URL"),
                 "foobar.html");
    ASSERT_STREQ(peer.GetSectionValue("harder relative URL"),
                 "/search?q=green flowers&amp;hl=en");
  }

  static void TestCleanseAttribute() {
    TemplateDictionary dict("TestCleanseAttribute", NULL);
    dict.SetEscapedValue("easy attribute", "top",
                         template_modifiers::cleanse_attribute);
    dict.SetEscapedValue("harder attribute", "foo & bar",
                         template_modifiers::cleanse_attribute);
    dict.SetEscapedValue("hardest attribute",
                         "top onclick='alert(document.cookie)'",
                         template_modifiers::cleanse_attribute);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy attribute"),
                  "top");
    ASSERT_STREQ(peer.GetSectionValue("harder attribute"),
                  "foo___bar");
    ASSERT_STREQ(peer.GetSectionValue("hardest attribute"),
                 "top_onclick__alert_document.cookie__");
  }

  static void TestJavascriptEscape() {
    TemplateDictionary dict("TestJavascriptEscape", NULL);
    dict.SetEscapedValue("easy JS", "joo",
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("harder JS", "f = 'joo';",
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("hardest JS",
                         ("f = 'foo';\r\n\tprint \"\\&foo = \b\", \"foo\""),
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("close script JS",
                         "//--></script><script>alert(123);</script>",
                         template_modifiers::javascript_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy JS"), "joo");
    ASSERT_STREQ(peer.GetSectionValue("harder JS"), "f \\x3d \\'joo\\';");
    ASSERT_STREQ(peer.GetSectionValue("hardest JS"),
                 "f \\x3d \\'foo\\';\\r\\n\tprint \\\"\\\\\\x26foo \\x3d "
                 "\\b\\\", \\\"foo\\\"");
    ASSERT_STREQ(peer.GetSectionValue("close script JS"),
                 "//--\\x3e\\x3c/script\\x3e\\x3cscript\\x3e"
                 "alert(123);\\x3c/script\\x3e");
  }

  static void TestJsonEscape() {
    TemplateDictionary dict("TestJsonEscape", NULL);
    dict.SetEscapedValue("easy JSON", "joo",
                         template_modifiers::json_escape);
    dict.SetEscapedValue("harder JSON", "f = \"joo\"; e = 'joo';",
                         template_modifiers::json_escape);
    dict.SetEscapedValue("hardest JSON",
                         ("f = 'foo';\r\n\t\fprint \"\\&foo = /\b\", \"foo\""),
                         template_modifiers::json_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy JSON"), "joo");
    ASSERT_STREQ(peer.GetSectionValue("harder JSON"), "f = \\\"joo\\\"; "
                 "e = 'joo';");
    ASSERT_STREQ(peer.GetSectionValue("hardest JSON"),
                 "f = 'foo';\\r\\n\\t\\fprint \\\"\\\\&foo = \\/\\b\\\", "
                 "\\\"foo\\\"");
  }

  static void TestUrlQueryEscape() {
    TemplateDictionary dict("TestUrlQueryEscape", NULL);
    // The first three tests do not need escaping.
    dict.SetEscapedValue("query escape 0", "",
                         template_modifiers::url_query_escape);
    dict.SetEscapedValue("query escape 1", "noop",
                         template_modifiers::url_query_escape);
    dict.SetEscapedValue("query escape 2",
                         "0123456789abcdefghjijklmnopqrstuvwxyz"
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-_*/~!(),",
                         template_modifiers::url_query_escape);
    dict.SetEscapedValue("query escape 3", " ?a=b;c#d ",
                         template_modifiers::url_query_escape);
    dict.SetEscapedValue("query escape 4", "#$%&+<=>?@[\\]^`{|}",
                         template_modifiers::url_query_escape);
    dict.SetEscapedValue("query escape 5", "\xDE\xAD\xCA\xFE",
                         template_modifiers::url_query_escape);
    dict.SetEscapedValue("query escape 6", "\"':",
                         template_modifiers::url_query_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("query escape 0"), "");
    ASSERT_STREQ(peer.GetSectionValue("query escape 1"), "noop");
    ASSERT_STREQ(peer.GetSectionValue("query escape 2"),
                 "0123456789abcdefghjijklmnopqrstuvwxyz"
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-_*/~!(),");
    ASSERT_STREQ(peer.GetSectionValue("query escape 3"), "+%3Fa%3Db%3Bc%23d+");
    ASSERT_STREQ(peer.GetSectionValue("query escape 4"),
                 "%23%24%25%26%2B%3C%3D%3E%3F%40%5B%5C%5D%5E%60%7B%7C%7D");
    ASSERT_STREQ(peer.GetSectionValue("query escape 5"), "%DE%AD%CA%FE");
    ASSERT_STREQ(peer.GetSectionValue("query escape 6"), "%22%27%3A");
  }

};

_END_GOOGLE_NAMESPACE_

using GOOGLE_NAMESPACE::TemplateModifiersUnittest;

int main(int argc, char** argv) {
  TemplateModifiersUnittest::TestHtmlEscape();
  TemplateModifiersUnittest::TestSnippetEscape();
  TemplateModifiersUnittest::TestPreEscape();
  TemplateModifiersUnittest::TestXmlEscape();
  TemplateModifiersUnittest::TestValidateUrlEscape();
  TemplateModifiersUnittest::TestCleanseAttribute();
  TemplateModifiersUnittest::TestJavascriptEscape();
  TemplateModifiersUnittest::TestJsonEscape();
  TemplateModifiersUnittest::TestUrlQueryEscape();

  printf("DONE\n");
  return 0;
}
