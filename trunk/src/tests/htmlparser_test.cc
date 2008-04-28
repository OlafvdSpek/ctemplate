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
//
// ---
// Author: Filipe Almeida
//
// Verify at different points during HTML processing that the parser is in the
// correct state.
//
// The annotated file consists of regular html blocks and html processing
// instructions with a target name of "state" and a list of comma separated key
// value pairs describing the expected state or invoking a parser method.
// Example:
//
// <html><body><?state state=text, tag=body ?>
//
// For a more detailed explanation of the acceptable values please consult
// htmlparser_cpp.h. Following is a list of the possible keys:
//
// state: Current parser state as returned by HtmlParser::state().
//        Possible values: text, tag, attr, value, comment or error.
// tag: Current tag name as returned by HtmlParser::tag()
// attr: Current attribute name as returned by HtmlParser::attr()
// attr_type: Current attribute type as returned by HtmlParser::attr_type()
//            Possible values: none, regular, uri, js or style.
// attr_quoted: True if the attribute is quoted, false if it's not.
// in_js: True if currently processing javascript (either an attribute value
//        that expects javascript, a script block or the parser being in
//        MODE_JS)
// js_quoted: True if inside a javascript string literal.
// value_index: Integer value containing the current character index in the
//              current value starting from 0.
// reset: If true, resets the parser state to it's initial values.
// reset_mode: Similar to reset but receives an argument that changes the
//             parser mode into either mode html or mode js.
// insert_text: Executes HtmlParser::InsertText() if the argument is true.

#include "config_for_unittests.h"
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string>
#include <utility>
#include <vector>
#include "htmlparser/htmlparser_cpp.h"
#include "google/template_pathops.h"

using std::string;
using std::vector;
using std::pair;
using google::ctemplate::PathJoin;
using HTMLPARSER_NAMESPACE::HtmlParser;

// This default value is only used when the TEMPLATE_ROOTDIR envvar isn't set
#ifndef DEFAULT_TEMPLATE_ROOTDIR
# define DEFAULT_TEMPLATE_ROOTDIR  "."
#endif

#define CHECK(cond)  do {                                       \
  if (!(cond)) {                                                \
    printf("%s: %d: ASSERT FAILED: %s\n", __FILE__, __LINE__,   \
           #cond);                                              \
    assert(cond);                                               \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define EXPECT_TRUE CHECK

#define EXPECT_EQ(a, b)  do {                                             \
  if ((a) != (b)) {                                                       \
    printf("%s: %d: ASSERT FAILED: '%s' != '%s'\n", __FILE__, __LINE__,   \
           #a, #b);                                                       \
    assert((a) == (b));                                                   \
    exit(1);                                                              \
  }                                                                       \
} while (0)

#define PFATAL(s)  do { perror(s); exit(1); } while (0)


static int strcount(const string& str, char c) {
  int count = 0;
  for (char* p = strchr(str.c_str(), c); p; p = strchr(p+1, c))
    count++;
  return count;
}

static void LowerString(string* s) {
  for (size_t i = 0; i < s->length(); ++i)
    if ((*s)[i] >= 'A' && (*s)[i] <= 'Z')
      (*s)[i] += 'a' - 'A';
}

static void StripWhiteSpace(string* s) {
  const char* p = s->c_str(), *pend = s->c_str() + s->length();
  while (p < pend && isspace(*p))
    p++;
  while (p < pend && isspace(pend[-1]))
    pend--;
  string retval(p, pend - p);   // maybe assigning directly to s is unsafe
  std::swap(retval, *s);
}

  vector< pair< string, string > > pairs;
static void SplitStringIntoKeyValuePairs(const string& in,
                                         char after_key, char after_val,
                                         vector< pair<string,string> >* pairs) {
  pairs->clear();
  const char* const pend = in.c_str() + in.length();
  const char* key, *keyend;
  const char* val, *valend;
  for (const char* p = in.c_str(); p < pend; p = valend + 1) {
    key = p;
    keyend = (const char*)memchr(key, after_key, pend - key);
    if (keyend == NULL)
      break;
    val = keyend + 1;
    valend = (const char*)memchr(val, after_val, pend - val);
    if (valend == NULL)
      valend = pend;
    pairs->push_back(pair<string,string>(string(key, keyend - key),
                                         string(val, valend - val)));
    p = valend + 1;
  }
}

static void ReadToString(const char* filename, string* s) {
  const int bufsize = 8092;
  char buffer[bufsize];
  size_t n;
  FILE* fp = fopen(filename, "rb");
  if (!fp)  PFATAL(filename);
  while ((n=fread(buffer, 1, bufsize, fp)) > 0) {
    if (ferror(fp))  PFATAL(filename);
    s->append(string(buffer, n));
  }
  fclose(fp);
}


namespace {

class HtmlparserCppTest {
 public:
  // Reads the filename of an annotated html file and validates the
  // annotations against the html parser state.
  void ValidateFile(string filename);

 protected:
  // Structure that stores the mapping between an id and a name.
  struct IdNameMap {
    int id;
    const char *name;
  };

  // Mapping between the enum and the string representation of the state.
  static const struct IdNameMap kStateMap[];

  // Mapping between the enum and the string representation of the attribute
  // type.
  static const struct IdNameMap kAttributeTypeMap[];

  // Mapping between the enum and the string representation of the reset mode.
  static const struct IdNameMap kResetModeMap[];

  // Maximum file size limit.
  static const int kMaxFileSize;

  // String that marks the start of an annotation.
  static const char kDirectiveBegin[];

  // String that marks the end of an annotation.
  static const char kDirectiveEnd[];

  // Count the number of lines in a string.
  static int CountLines(const string &str);

  // Converts a string to a boolean.
  static bool StringToBool(const string &value);

  // Returns the name of the corresponding enum_id by consulting an array of
  // type IdNameMap.
  const char *IdToName(const struct IdNameMap *list, int enum_id);

  // Returns the enum_id of the correspondent name by consulting an array of
  // type IdNameMap.
  int NameToId(const struct IdNameMap *list, const string &name);

  // Validate an annotation string against the current parser state.
  void ProcessAnnotation(HtmlParser *parser, const string &dir);

  // Validate the parser state against the provided state.
  void ValidateState(const HtmlParser *parser, const string &tag);

  // Validate the parser tag name against the provided tag name.
  void ValidateTag(const HtmlParser *parser, const string &tag);

  // Validate the parser attribute name against the provided attribute name.
  void ValidateAttribute(const HtmlParser *parser, const string &attr);

  // Validate the parser attribute value contents against the provided string.
  void ValidateValue(const HtmlParser *parser, const string &contents);

  // Validate the parser attribute type against the provided attribute type.
  void ValidateAttributeType(const HtmlParser *parser, const string &attr);

  // Validate the parser attribute quoted state against the provided
  // boolean.
  void ValidateAttributeQuoted(const HtmlParser *parser, const string &quoted);

  // Validates the parser in javascript state against the provided boolean.
  void ValidateInJavascript(const HtmlParser *parser, const string &quoted);

  // Validate the current parser javascript quoted state against the provided
  // boolean.
  void ValidateJavascriptQuoted(const HtmlParser *parser, const string &quoted);

  // Validate the current parser value index against the provided index.
  void ValidateValueIndex(const HtmlParser *parser, const string &value_index);

  // Current line number.
  int line_number_;
};

const int HtmlparserCppTest::kMaxFileSize = 1000000;

const char HtmlparserCppTest::kDirectiveBegin[] = "<?state";
const char HtmlparserCppTest::kDirectiveEnd[] = "?>";

const struct HtmlparserCppTest::IdNameMap
             HtmlparserCppTest::kStateMap[] = {
  { HtmlParser::STATE_TEXT,    "text" },
  { HtmlParser::STATE_TAG,     "tag" },
  { HtmlParser::STATE_ATTR,    "attr" },
  { HtmlParser::STATE_VALUE,   "value" },
  { HtmlParser::STATE_COMMENT, "comment" },
  { HtmlParser::STATE_ERROR,   "error" },
  { 0, NULL }
};

const struct HtmlparserCppTest::IdNameMap
             HtmlparserCppTest::kAttributeTypeMap[] = {
  { HtmlParser::ATTR_NONE,    "none" },
  { HtmlParser::ATTR_REGULAR, "regular" },
  { HtmlParser::ATTR_URI,     "uri" },
  { HtmlParser::ATTR_JS,      "js" },
  { HtmlParser::ATTR_STYLE,   "style" },
  { 0, NULL }
};

const struct HtmlparserCppTest::IdNameMap
             HtmlparserCppTest::kResetModeMap[] = {
  { HtmlParser::MODE_HTML,    "html" },
  { HtmlParser::MODE_JS,      "js" },
  { 0, NULL }
};


// Count the number of lines in a string.
int HtmlparserCppTest::CountLines(const string &str) {
  return strcount(str, '\n');
}

// Converts a string to a boolean.
bool HtmlparserCppTest::StringToBool(const string &value) {
  string lowercase(value);
  LowerString(&lowercase);
  if (lowercase == "true") {
    return true;
  } else if (lowercase == "false") {
    return false;
  } else {
    CHECK("Unknown boolean value" == NULL);
  }
}

// Returns the name of the corresponding enum_id by consulting an array of
// type IdNameMap.
const char *HtmlparserCppTest::IdToName(const struct IdNameMap *list,
                                        int enum_id) {
  CHECK(list != NULL);
  while (list->name) {
    if (enum_id == list->id) {
      return list->name;
    }
    list++;
  }
  CHECK("Unknown id" != NULL);
  return NULL;  // Unreachable.
}

// Returns the enum_id of the correspondent name by consulting an array of
// type IdNameMap.
int HtmlparserCppTest::NameToId(const struct IdNameMap *list,
                                const string &name) {
  CHECK(list != NULL);
  while (list->name) {
    if (name.compare(list->name) == 0) {
      return list->id;
    }
    list++;
  }
  CHECK("Unknown name" != NULL);
  return -1;  // Unreachable.
}

// Validate the parser state against the provided state.
void HtmlparserCppTest::ValidateState(const HtmlParser *parser,
                                      const string &expected_state) {
  const char* parsed_state = IdToName(kStateMap, parser->state());
  EXPECT_TRUE(parsed_state != NULL);
  EXPECT_TRUE(!expected_state.empty());
  EXPECT_EQ(expected_state, string(parsed_state));
}

// Validate the parser tag name against the provided tag name.
void HtmlparserCppTest::ValidateTag(const HtmlParser *parser,
                                    const string &expected_tag) {
  EXPECT_TRUE(parser->tag() != NULL);
  EXPECT_TRUE(expected_tag == parser->tag());
}

// Validate the parser attribute name against the provided attribute name.
void HtmlparserCppTest::ValidateAttribute(const HtmlParser *parser,
                                     const string &expected_attr) {
  EXPECT_TRUE(parser->attribute() != NULL);
  EXPECT_TRUE(expected_attr == parser->attribute());
}

// Validate the parser attribute value contents against the provided string.
void HtmlparserCppTest::ValidateValue(const HtmlParser *parser,
                                     const string &expected_value) {
  EXPECT_TRUE(parser->value() != NULL);
  const string parsed_state(parser->value());
  EXPECT_EQ(expected_value, parsed_state);
}

// Validate the parser attribute type against the provided attribute type.
void HtmlparserCppTest::ValidateAttributeType(const HtmlParser *parser,
                                         const string &expected_attr_type) {
  const char *parsed_attr_type = IdToName(kAttributeTypeMap,
                                          parser->AttributeType());
  EXPECT_TRUE(parsed_attr_type != NULL);
  EXPECT_TRUE(!expected_attr_type.empty());
  EXPECT_EQ(expected_attr_type.compare(parsed_attr_type), 0);
}

// Validate the parser attribute quoted state against the provided
// boolean.
void HtmlparserCppTest::ValidateAttributeQuoted(const HtmlParser *parser,
                                           const string &expected_attr_quoted) {
  bool attr_quoted_bool = StringToBool(expected_attr_quoted);
  EXPECT_EQ(parser->IsAttributeQuoted(), attr_quoted_bool);
}

// Validates the parser in javascript state against the provided boolean.
void HtmlparserCppTest::ValidateInJavascript(const HtmlParser *parser,
                                     const string &expected_in_js) {
  bool in_js_bool = StringToBool(expected_in_js);
  EXPECT_EQ(parser->InJavascript(), in_js_bool);
}

// Validate the current parser javascript quoted state against the provided
// boolean.
void HtmlparserCppTest::ValidateJavascriptQuoted(const HtmlParser *parser,
                                           const string &expected_js_quoted) {
  bool js_quoted_bool = StringToBool(expected_js_quoted);
  EXPECT_EQ(parser->IsJavascriptQuoted(), js_quoted_bool);
}

// Validate the current parser value index against the provided index.
void HtmlparserCppTest::ValidateValueIndex(const HtmlParser *parser,
                                           const string &expected_value_index) {
  int index;
  index = atoi(expected_value_index.c_str());
  EXPECT_EQ(parser->ValueIndex(), index);
}

// Validate an annotation string against the current parser state.
//
// Split the annotation into a list of key value pairs and call the appropriate
// handler for each pair.
void HtmlparserCppTest::ProcessAnnotation(HtmlParser *parser,
                                         const string &annotation) {
  vector< pair< string, string > > pairs;
  SplitStringIntoKeyValuePairs(annotation, '=', ',', &pairs);

  vector< pair< string, string > >::iterator iter;

  iter = pairs.begin();
  for (iter = pairs.begin(); iter != pairs.end(); ++iter) {
    StripWhiteSpace(&iter->first);
    StripWhiteSpace(&iter->second);

    if (iter->first.compare("state") == 0) {
      ValidateState(parser, iter->second);
    } else if (iter->first.compare("tag") == 0) {
      ValidateTag(parser, iter->second);
    } else if (iter->first.compare("attr") == 0) {
      ValidateAttribute(parser, iter->second);
    } else if (iter->first.compare("value") == 0) {
      ValidateValue(parser, iter->second);
    } else if (iter->first.compare("attr_type") == 0) {
      ValidateAttributeType(parser, iter->second);
    } else if (iter->first.compare("attr_quoted") == 0) {
      ValidateAttributeQuoted(parser, iter->second);
    } else if (iter->first.compare("in_js") == 0) {
      ValidateInJavascript(parser, iter->second);
    } else if (iter->first.compare("js_quoted") == 0) {
      ValidateJavascriptQuoted(parser, iter->second);
    } else if (iter->first.compare("value_index") == 0) {
      ValidateValueIndex(parser, iter->second);
    } else if (iter->first.compare("reset") == 0) {
      if (StringToBool(iter->second)) {
        parser->Reset();
      }
    } else if (iter->first.compare("reset_mode") == 0) {
      HtmlParser::Mode mode =
           static_cast<HtmlParser::Mode>(NameToId(kResetModeMap, iter->second));
      parser->ResetMode(mode);
    } else if (iter->first.compare("insert_text") == 0) {
      if (StringToBool(iter->second)) {
        parser->InsertText();
      }
    } else {
      CHECK(false); // "Unknown test directive: iter->first"
    }
  }
}

// Validates an html annotated file against the parser state.
//
// It iterates over the html file splitting it into html blocks and annotation
// blocks. It sends the html block to the parser and uses the annotation block
// to validate the parser state.
void HtmlparserCppTest::ValidateFile(string filename) {
  HtmlParser parser;

  string buffer;
  ReadToString(filename.c_str(), &buffer);

  // Current line count.
  line_number_ = 0;

  // Start of the current html block.
  size_t start_html = 0;

  // Start of the next annotation.
  size_t start_annotation = buffer.find(kDirectiveBegin, 0);

  // Ending of the current annotation.
  size_t end_annotation = buffer.find(kDirectiveEnd, start_annotation);

  while (start_annotation != string::npos) {
    string html_block(buffer, start_html, start_annotation - start_html);
    parser.Parse(html_block);
    line_number_ += CountLines(html_block);

    start_annotation += strlen(kDirectiveBegin);

    string annotation_block(buffer, start_annotation,
                            end_annotation - start_annotation);
    ProcessAnnotation(&parser, annotation_block);
    line_number_ += CountLines(annotation_block);

    start_html = end_annotation + strlen(kDirectiveEnd);
    start_annotation = buffer.find(kDirectiveBegin, start_html);
    end_annotation = buffer.find(kDirectiveEnd, start_annotation);

    // Check for unclosed annotation.
    CHECK(!(start_annotation != string::npos &&
            end_annotation == string::npos));
  }
}

}  // namespace

int main(int argc, char **argv) {
  HtmlParser html;
  EXPECT_EQ(html.Parse("<a href='http://www.google.com' ''>\n"),
            HtmlParser::STATE_ERROR);

  HtmlparserCppTest tester;

  // If TEMPLATE_ROOTDIR is set in the environment, it overrides the
  // default of ".".  We use an env-var rather than argv because
  // that's what automake supports most easily.
  const char* template_rootdir = getenv("TEMPLATE_ROOTDIR");
  if (template_rootdir == NULL)
    template_rootdir = DEFAULT_TEMPLATE_ROOTDIR;   // probably "."
  string dir = PathJoin(template_rootdir, "src");
  dir = PathJoin(dir, "tests");
  dir = PathJoin(dir, "htmlparser_testdata");

  // TODO(csilvers): use readdir (and windows equivalent) instead.
  const char* file_list[] = { "simple.html",
                              "comments.html",
                              "javascript_block.html",
                              "javascript_attribute.html",
                              "tags.html",
                              "reset.html",
                              "cdata.html" };

  for (const char** pfile = file_list;
       pfile < file_list + sizeof(file_list)/sizeof(*file_list);
       ++pfile)
    tester.ValidateFile(PathJoin(dir, *pfile));

  printf("DONE.\n");
  return 0;
}
