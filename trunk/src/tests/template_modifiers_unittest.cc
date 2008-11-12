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

#include "config_for_unittests.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "template_modifiers_internal.h"
#include <google/template_dictionary.h>
#include <google/template_emitter.h>
#include <google/template_modifiers.h>
#include "tests/template_test_util.h"

using std::string;

_START_GOOGLE_NAMESPACE_

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
    dict.SetEscapedValue("no XML", "",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("easy XML", "xoo",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("harder XML-1", "<>&'\"",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("harder XML-2", "Hello<script>alert('&')</script>",
                         template_modifiers::xml_escape);
    dict.SetEscapedValue("hardest XML", "<<b>>&!''\"\"foo",
                         template_modifiers::xml_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("no XML"), "");
    ASSERT_STREQ(peer.GetSectionValue("easy XML"), "xoo");
    ASSERT_STREQ(peer.GetSectionValue("harder XML-1"),
                 "&lt;&gt;&amp;&#39;&quot;");
    ASSERT_STREQ(peer.GetSectionValue("harder XML-2"),
                 "Hello&lt;script&gt;alert(&#39;&amp;&#39;)&lt;/script&gt;");
    ASSERT_STREQ(peer.GetSectionValue("hardest XML"),
                 "&lt;&lt;b&gt;&gt;&amp;!&#39;&#39;&quot;&quot;foo");
  }

  static void TestValidateUrlHtmlEscape() {
    TemplateDictionary dict("TestValidateUrlHtmlEscape", NULL);
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

  static void TestValidateUrlJavascriptEscape() {
    TemplateDictionary dict("TestValidateUrlJavascriptEscape", NULL);
    dict.SetEscapedValue(
        "easy http URL", "http://www.google.com",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "harder https URL",
        "https://www.google.com/search?q=f&hl=en",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "mangled http URL", "HTTP://www.google.com",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "easy javascript URL",
        "javascript:alert(document.cookie)",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "harder javascript URL",
        "javascript:alert(10/5)",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "easy relative URL",
        "foobar.html",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "harder relative URL",
        "/search?q=green flowers&hl=en",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "data URL",
        "data: text/html",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "mangled javascript URL",
        "javaSCRIPT:alert(5)",
        template_modifiers::validate_url_and_javascript_escape);
    dict.SetEscapedValue(
        "harder mangled javascript URL",
        "java\nSCRIPT:alert(5)",
        template_modifiers::validate_url_and_javascript_escape);


    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy http URL"),
                 "http://www.google.com");
    ASSERT_STREQ(peer.GetSectionValue("harder https URL"),
                 "https://www.google.com/search?q\\x3df\\x26hl\\x3den");
    ASSERT_STREQ(peer.GetSectionValue("mangled http URL"),
                 "HTTP://www.google.com");
    ASSERT_STREQ(peer.GetSectionValue("easy javascript URL"),
                 "#");
    ASSERT_STREQ(peer.GetSectionValue("harder javascript URL"),
                 "#");
    ASSERT_STREQ(peer.GetSectionValue("easy relative URL"),
                 "foobar.html");
    ASSERT_STREQ(peer.GetSectionValue("harder relative URL"),
                 "/search?q\\x3dgreen flowers\\x26hl\\x3den");
    ASSERT_STREQ(peer.GetSectionValue("data URL"),
                 "#");
    ASSERT_STREQ(peer.GetSectionValue("mangled javascript URL"),
                 "#");
    ASSERT_STREQ(peer.GetSectionValue("harder mangled javascript URL"),
                 "#");
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
    dict.SetEscapedValue("equal in middle", "foo = bar",
                         template_modifiers::cleanse_attribute);
    dict.SetEscapedValue("leading equal", "=foo",
                         template_modifiers::cleanse_attribute);
    dict.SetEscapedValue("trailing equal", "foo=",
                         template_modifiers::cleanse_attribute);
    dict.SetEscapedValue("all equals", "===foo===bar===",
                         template_modifiers::cleanse_attribute);
    dict.SetEscapedValue("just equals", "===",
                         template_modifiers::cleanse_attribute);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy attribute"), "top");
    ASSERT_STREQ(peer.GetSectionValue("harder attribute"), "foo___bar");
    ASSERT_STREQ(peer.GetSectionValue("hardest attribute"),
                 "top_onclick=_alert_document.cookie__");

    ASSERT_STREQ(peer.GetSectionValue("equal in middle"), "foo_=_bar");
    ASSERT_STREQ(peer.GetSectionValue("leading equal"), "_foo");
    ASSERT_STREQ(peer.GetSectionValue("trailing equal"), "foo_");
    ASSERT_STREQ(peer.GetSectionValue("just equals"), "_=_");
    ASSERT_STREQ(peer.GetSectionValue("all equals"), "_==foo===bar==_");
  }

  static void TestCleanseCss() {
    TemplateDictionary dict("TestCleanseCss", NULL);
    dict.SetEscapedValue("easy css", "top",
                         template_modifiers::cleanse_css);
    dict.SetEscapedValue("harder css", "foo & bar",
                         template_modifiers::cleanse_css);
    dict.SetEscapedValue("hardest css",
                         ";width:expression(document.cookie)",
                         template_modifiers::cleanse_css);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy css"),
                  "top");
    ASSERT_STREQ(peer.GetSectionValue("harder css"),
                  "foo  bar");
    ASSERT_STREQ(peer.GetSectionValue("hardest css"),
                 "widthexpressiondocument.cookie");
  }

  static void TestJavascriptEscape() {
    TemplateDictionary dict("TestJavascriptEscape", NULL);
    dict.SetEscapedValue("easy JS", "joo",
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("harder JS", "f = 'joo';",
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("hardest JS",
                         ("f = 'foo\f';\r\n\tprint \"\\&foo = \b\", \"foo\""),
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("close script JS",
                         "//--></script><script>alert(123);</script>",
                         template_modifiers::javascript_escape);
    dict.SetEscapedValue("unicode codepoints",
                         ("line1" "\xe2\x80\xa8" "line2" "\xe2\x80\xa9" "line3"
                          /* \u2027 */ "\xe2\x80\xa7"
                          /* \u202A */ "\xe2\x80\xaa"
                          /* malformed */ "\xe2" "\xe2\x80\xa8"
                          /* truncated */ "\xe2\x80"),
                         template_modifiers::javascript_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy JS"), "joo");
    ASSERT_STREQ(peer.GetSectionValue("harder JS"), "f \\x3d \\x27joo\\x27;");
    ASSERT_STREQ(peer.GetSectionValue("hardest JS"),
                 "f \\x3d \\x27foo\\f\\x27;\\r\\n\\tprint \\x22\\\\\\x26foo "
                 "\\x3d \\b\\x22, \\x22foo\\x22");
    ASSERT_STREQ(peer.GetSectionValue("close script JS"),
                 "//--\\x3e\\x3c/script\\x3e\\x3cscript\\x3e"
                 "alert(123);\\x3c/script\\x3e");
    ASSERT_STREQ(peer.GetSectionValue("unicode codepoints"),
                 "line1" "\\u2028" "line2" "\\u2029" "line3"
                 "\xe2\x80\xa7"
                 "\xe2\x80\xaa"
                 "\xe2" "\\u2028"
                 "\xe2\x80");
  }

  static void TestJavascriptNumber() {
    TemplateDictionary dict("TestJavascriptNumber", NULL);
    dict.SetEscapedValue("empty string", "",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("boolean true", "true",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("boolean false", "false",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad boolean 1", "tfalse",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad boolean 2", "tru",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad boolean 3", "truee",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad boolean 4", "invalid",
                         template_modifiers::javascript_number);

    // Check that our string comparisons for booleans do not
    // assume input is null terminated.
    dict.SetEscapedValue("good boolean 5", TemplateString("truee", 4),
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad boolean 6", TemplateString("true", 3),
                         template_modifiers::javascript_number);

    dict.SetEscapedValue("hex number 1", "0x123456789ABCDEF",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("hex number 2", "0X123456789ABCDEF",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad hex number 1", "0x123GAC",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("bad hex number 2", "0x",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("number zero", "0",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("invalid number", "A9",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("decimal zero", "0.0",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("octal number", "01234567",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("decimal number", "799.123",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("negative number", "-244",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("positive number", "+244",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("valid float 1", ".55",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("valid float 2", "8.55e-12",
                         template_modifiers::javascript_number);
    dict.SetEscapedValue("invalid float", "8.55ABC",
                         template_modifiers::javascript_number);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("empty string"), "");
    ASSERT_STREQ(peer.GetSectionValue("boolean true"), "true");
    ASSERT_STREQ(peer.GetSectionValue("boolean false"), "false");
    ASSERT_STREQ(peer.GetSectionValue("bad boolean 1"), "null");
    ASSERT_STREQ(peer.GetSectionValue("bad boolean 2"), "null");
    ASSERT_STREQ(peer.GetSectionValue("bad boolean 3"), "null");
    ASSERT_STREQ(peer.GetSectionValue("bad boolean 4"), "null");
    ASSERT_STREQ(peer.GetSectionValue("good boolean 5"), "true");
    ASSERT_STREQ(peer.GetSectionValue("bad boolean 6"), "null");
    ASSERT_STREQ(peer.GetSectionValue("hex number 1"), "0x123456789ABCDEF");
    ASSERT_STREQ(peer.GetSectionValue("hex number 2"), "0X123456789ABCDEF");
    ASSERT_STREQ(peer.GetSectionValue("bad hex number 1"), "null");
    ASSERT_STREQ(peer.GetSectionValue("bad hex number 2"), "null");
    ASSERT_STREQ(peer.GetSectionValue("number zero"), "0");
    ASSERT_STREQ(peer.GetSectionValue("invalid number"), "null");
    ASSERT_STREQ(peer.GetSectionValue("decimal zero"), "0.0");
    ASSERT_STREQ(peer.GetSectionValue("octal number"), "01234567");
    ASSERT_STREQ(peer.GetSectionValue("decimal number"), "799.123");
    ASSERT_STREQ(peer.GetSectionValue("negative number"), "-244");
    ASSERT_STREQ(peer.GetSectionValue("positive number"), "+244");
    ASSERT_STREQ(peer.GetSectionValue("valid float 1"), ".55");
    ASSERT_STREQ(peer.GetSectionValue("valid float 2"), "8.55e-12");
    ASSERT_STREQ(peer.GetSectionValue("invalid float"), "null");
  }

  static void TestJsonEscape() {
    TemplateDictionary dict("TestJsonEscape", NULL);
    dict.SetEscapedValue("easy JSON", "joo",
                         template_modifiers::json_escape);
    dict.SetEscapedValue("harder JSON", "f = \"joo\"; e = 'joo';",
                         template_modifiers::json_escape);
    dict.SetEscapedValue("hardest JSON",
                         "f = 'foo<>';\r\n\t\fprint \"\\&foo = /\b\", \"foo\"",
                         template_modifiers::json_escape);
    dict.SetEscapedValue("html in JSON", "<html>&nbsp;</html>",
                         template_modifiers::json_escape);

    TemplateDictionaryPeer peer(&dict);  // peer can look inside the dict
    ASSERT_STREQ(peer.GetSectionValue("easy JSON"), "joo");
    ASSERT_STREQ(peer.GetSectionValue("harder JSON"), "f = \\\"joo\\\"; "
                 "e = 'joo';");
    ASSERT_STREQ(peer.GetSectionValue("html in JSON"),
                 "\\u003Chtml\\u003E\\u0026nbsp;\\u003C\\/html\\u003E");
    // There's a bug in MSVC 7.1 where you can't pass a literal string
    // with more than one \" in it to a macro (!) -- see
    //    http://marc.info/?t=110853662500001&r=1&w=2
    // We work around this by assigning the string to a variable first.
    const char* expected = ("f = 'foo\\u003C\\u003E';\\r\\n\\t\\fprint \\\""
                            "\\\\\\u0026foo = \\/\\b\\\", \\\"foo\\\"");
    ASSERT_STREQ(peer.GetSectionValue("hardest JSON"), expected);
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

  static void TestPrefixLine() {
    TemplateDictionary dict("TestPrefixLine", NULL);
    // These don't escape: we don't put the prefix before the first line
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1", "   ").c_str(),
                 "pt 1");
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1", "::").c_str(),
                 "pt 1");

    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\npt 2", ":").c_str(),
                 "pt 1\n:pt 2");
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\npt 2", " ").c_str(),
                 "pt 1\n pt 2");
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\npt 2", "\n").c_str(),
                 "pt 1\n\npt 2");
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\npt 2\n", "  ").c_str(),
                 "pt 1\n  pt 2\n  ");

    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\rpt 2\n", ":").c_str(),
                 "pt 1\r:pt 2\n:");
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\npt 2\r", ":").c_str(),
                 "pt 1\n:pt 2\r:");
    ASSERT_STREQ(template_modifiers::prefix_line("pt 1\r\npt 2\r", ":").c_str(),
                 "pt 1\r\n:pt 2\r:");
  }

  static void TestFindModifier() {
    const template_modifiers::ModifierInfo* info;
    ASSERT(info = template_modifiers::FindModifier("html_escape", 11, "", 0));
    ASSERT(info->modifier == &template_modifiers::html_escape);
    ASSERT(info = template_modifiers::FindModifier("h", 1, "", 0));
    ASSERT(info->modifier == &template_modifiers::html_escape);

    ASSERT(info = template_modifiers::FindModifier("html_escape_with_arg", 20,
                                                   "=pre", 4));
    ASSERT(info->modifier == &template_modifiers::pre_escape);
    ASSERT(info = template_modifiers::FindModifier("H", 1, "=pre", 4));
    ASSERT(info->modifier == &template_modifiers::pre_escape);

    ASSERT(info = template_modifiers::FindModifier("javascript_escape_with_arg",
                                                   26, "=number", 7));
    ASSERT(info = template_modifiers::FindModifier("J", 1, "=number", 7));
    ASSERT(info->modifier == &template_modifiers::javascript_number);

    // html_escape_with_arg doesn't have a default value, so these should fail.
    ASSERT(!template_modifiers::FindModifier("H", 1, "=pre", 2));  // "=p"
    ASSERT(!template_modifiers::FindModifier("H", 1, "=pree", 5));
    ASSERT(!template_modifiers::FindModifier("H", 1, "=notpresent", 11));

    // If we don't have a modifier-value when we ought, we should fail.
    ASSERT(!template_modifiers::FindModifier("html_escape", 11, "=p", 2));
    ASSERT(!template_modifiers::FindModifier("h", 1, "=p", 2));

    ASSERT(!template_modifiers::FindModifier("html_escape_with_arg", 20,
                                             "", 0));
    ASSERT(!template_modifiers::FindModifier("H", 1, "", 0));

    // Test with added modifiers as well.
    template_modifiers::NullModifier foo_modifier1;
    template_modifiers::NullModifier foo_modifier2;
    template_modifiers::NullModifier foo_modifier3;
    template_modifiers::NullModifier foo_modifier4;
    ASSERT(template_modifiers::AddModifier("x-test", &foo_modifier1));
    ASSERT(template_modifiers::AddModifier("x-test-arg=", &foo_modifier2));
    ASSERT(template_modifiers::AddModifier("x-test-arg=h", &foo_modifier3));
    ASSERT(template_modifiers::AddModifier("x-test-arg=json", &foo_modifier4));

    ASSERT(info = template_modifiers::FindModifier("x-test", 6, "", 0));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier1);
    ASSERT(info = template_modifiers::FindModifier("x-test", 6, "=h", 2));
    ASSERT(!info->is_registered);
    // This tests default values
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10, "=p", 2));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier2);
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10, "=h", 2));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier3);
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10,
                                                   "=html", 5));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier2);
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10,
                                                   "=json", 5));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier4);
    // The value is required to start with an '=' to match the
    // specialization.  If it doesn't, it will match the default.
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10,
                                                   "json", 4));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier2);
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10,
                                                   "=jsonnabbe", 5));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier4);
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10,
                                                   "=jsonnabbe", 6));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier2);
    ASSERT(info = template_modifiers::FindModifier("x-test-arg", 10,
                                                   "=jsonnabbe", 4));
    ASSERT(info->is_registered);
    ASSERT(info->modifier == &foo_modifier2);

    // If we try to find an x- modifier that wasn't added, we should get
    // a legit but "unknown" modifier back.
    ASSERT(info = template_modifiers::FindModifier("x-foo", 5, "", 0));
    ASSERT(!info->is_registered);
    ASSERT(info = template_modifiers::FindModifier("x-bar", 5, "=p", 2));
    ASSERT(!info->is_registered);
  }

  static void TestAddModifier() {
    ASSERT(template_modifiers::AddModifier("x-atest",
                                           &template_modifiers::html_escape));
    ASSERT(template_modifiers::AddModifier("x-atest-arg=",
                                           &template_modifiers::html_escape));
    ASSERT(template_modifiers::AddModifier("x-atest-arg=h",
                                           &template_modifiers::html_escape));
    ASSERT(template_modifiers::AddModifier("x-atest-arg=html",
                                           &template_modifiers::html_escape));
    ASSERT(template_modifiers::AddModifier("x-atest-arg=json",
                                           &template_modifiers::json_escape));
    ASSERT(template_modifiers::AddModifier("x-atest-arg=j",
                                           &template_modifiers::json_escape));
    ASSERT(template_modifiers::AddModifier("x-atest-arg=J",
                                           &template_modifiers::json_escape));

    // Make sure AddModifier fails with an invalid name.
    ASSERT(!template_modifiers::AddModifier("test",
                                            &template_modifiers::html_escape));

    // Make sure AddModifier fails with a duplicate name.
    ASSERT(!template_modifiers::AddModifier("x-atest",
                                            &template_modifiers::html_escape));
    ASSERT(!template_modifiers::AddModifier("x-atest-arg=",
                                            &template_modifiers::html_escape));
    ASSERT(!template_modifiers::AddModifier("x-atest-arg=h",
                                            &template_modifiers::html_escape));
    ASSERT(!template_modifiers::AddModifier("x-atest-arg=html",
                                            &template_modifiers::html_escape));

    const template_modifiers::ModifierInfo* info;
    ASSERT(info = template_modifiers::FindModifier("x-atest", 7, "", 0));
    ASSERT(info->modval_required == false);

    // Make sure we can still add a modifier after having already
    // searched for it.
    ASSERT(info = template_modifiers::FindModifier("x-foo", 5, "", 0));
    ASSERT(!info->is_registered);

    template_modifiers::NullModifier foo_modifier;
    ASSERT(template_modifiers::AddModifier("x-foo", &foo_modifier));
    ASSERT(info = template_modifiers::FindModifier("x-foo", 5, "", 0));
    ASSERT(info->modifier == &foo_modifier);
  }

  // Helper function. Determines whether the Modifier specified by
  // alt_modname/alt_modval is a safe XSS alternative to
  // the Modifier specified by modname/modval.
  static bool CheckXSSAlternative(const string& modname, const string& modval,
                                  const string& alt_modname,
                                  const string& alt_modval) {
    const template_modifiers::ModifierInfo *mod, *alt_mod;
    mod = template_modifiers::FindModifier(modname.c_str(), modname.length(),
                                           modval.c_str(), modval.length());
    alt_mod = template_modifiers::FindModifier(alt_modname.c_str(),
                                               alt_modname.length(),
                                               alt_modval.c_str(),
                                               alt_modval.length());
    ASSERT(mod != NULL && alt_mod != NULL);
    return IsSafeXSSAlternative(*mod, *alt_mod);
  }

  static void TestXSSAlternatives() {
    // A modifier is always a safe replacement to itself, even non built-in.
    ASSERT(CheckXSSAlternative("h", "", "h", ""));
    ASSERT(CheckXSSAlternative("url_escape_with_arg", "=javascript",
                               "url_escape_with_arg", "=javascript"));
    ASSERT(CheckXSSAlternative("x-bla", "", "x-bla", ""));

    // A built-in modifier is always a safe replacement to
    // another with the same function.
    ASSERT(CheckXSSAlternative("H", "=pre", "p", ""));
    ASSERT(CheckXSSAlternative("url_query_escape", "",
                               "url_escape_with_arg", "=query"));

    // H=(pre|snippet|attribute), p, u and U=query are all alternatives to h.
    ASSERT(CheckXSSAlternative("h", "", "H", "=pre"));
    ASSERT(CheckXSSAlternative("h", "", "H", "=snippet"));
    ASSERT(CheckXSSAlternative("h", "", "H", "=attribute"));
    ASSERT(CheckXSSAlternative("h", "", "p", ""));
    ASSERT(CheckXSSAlternative("h", "", "u", ""));
    ASSERT(CheckXSSAlternative("h", "", "U", "=query"));

    // But h is not an alternative to H=attribute
    // nor are U=html (yet) or json_escape alternatives to h.
    ASSERT(!CheckXSSAlternative("H", "=attribute", "h", ""));
    ASSERT(!CheckXSSAlternative("h", "", "U", "=html"));
    ASSERT(!CheckXSSAlternative("h", "", "json_escape", ""));

    // H=snippet and H=attribute are alternatives to H=pre
    // But H=pre is not an alternative to H=attribute.
    ASSERT(CheckXSSAlternative("H", "=pre", "H", "=snippet"));
    ASSERT(CheckXSSAlternative("H", "=pre", "H", "=attribute"));
    ASSERT(!CheckXSSAlternative("H", "=attribute", "H", "=pre"));

    // javascript_escape is an alternative to json_escape and vice versa
    ASSERT(CheckXSSAlternative("json_escape", "", "javascript_escape", ""));
    ASSERT(CheckXSSAlternative("javascript_escape", "", "json_escape", ""));

    // Extended modifier should not match any other except itself.
    ASSERT(!CheckXSSAlternative("x-bla", "", "x-foo", ""));
  }
};

_END_GOOGLE_NAMESPACE_

using GOOGLE_NAMESPACE::TemplateModifiersUnittest;

int main(int argc, char** argv) {
  TemplateModifiersUnittest::TestHtmlEscape();
  TemplateModifiersUnittest::TestSnippetEscape();
  TemplateModifiersUnittest::TestPreEscape();
  TemplateModifiersUnittest::TestXmlEscape();
  TemplateModifiersUnittest::TestValidateUrlHtmlEscape();
  TemplateModifiersUnittest::TestValidateUrlJavascriptEscape();
  TemplateModifiersUnittest::TestCleanseAttribute();
  TemplateModifiersUnittest::TestCleanseCss();
  TemplateModifiersUnittest::TestJavascriptEscape();
  TemplateModifiersUnittest::TestJavascriptNumber();
  TemplateModifiersUnittest::TestJsonEscape();
  TemplateModifiersUnittest::TestUrlQueryEscape();
  TemplateModifiersUnittest::TestPrefixLine();
  TemplateModifiersUnittest::TestFindModifier();
  TemplateModifiersUnittest::TestAddModifier();
  TemplateModifiersUnittest::TestXSSAlternatives();

  printf("DONE\n");
  return 0;
}
