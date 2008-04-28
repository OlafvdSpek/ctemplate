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
//

#include "config.h"

#include "base/mutex.h"     // This must go first so we get _XOPEN_SOURCE
#include <assert.h>
#include <stdio.h>          // for fwrite, fflush
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <ctype.h>          // for isspace()
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>         // for stat() and open() and getcwd()
#endif
#include <string.h>
#include <iostream>         // for logging
#include <iomanip>          // for indenting in Dump()
#include <string>
#include <list>
#include <iterator>
#include <vector>
#include <utility>          // for pair
#include HASH_MAP_H         // defined in config.h
#include "htmlparser/htmlparser_cpp.h"
#include <google/template_pathops.h>
#include <google/template.h>
#include <google/template_modifiers.h>
#include <google/template_dictionary.h>

#ifndef PATH_MAX
#ifdef MAXPATHLEN
#define PATH_MAX        MAXPATHLEN
#else
#define PATH_MAX        4096         // seems conservative for max filename len!
#endif
#endif

_START_GOOGLE_NAMESPACE_

using std::endl;
using std::string;
using std::list;
using std::vector;
using std::pair;
using HASH_NAMESPACE::hash_map;
using HASH_NAMESPACE::hash;
using HTMLPARSER_NAMESPACE::HtmlParser;

const int kIndent = 2;            // num spaces to indent each level

namespace {

// Mutexes protecting the globals below.  First protects g_use_current_dict
// and template_root_directory_, second protects g_template_cache.
// Third protects vars_seen in WriteOneHeaderEntry, below.
// Lock priority invariant: you should never acquire a Template::mutex_
// while holding one of these mutexes.
// TODO(csilvers): assert this in the codebase.
static Mutex g_static_mutex;
static Mutex g_cache_mutex;
static Mutex g_header_mutex;

// It's not great to have a global variable with a constructor, but
// it's safe in this case: the constructor is trivial and does not
// depend on any other global constructors running first, and the
// variable is used in only one place below, always after main() has
// started.
// It is ok for this modifier to be in XssClass XSS_WEB_STANDARD because
// it only adds indentation characters - typically whitespace - iff these
// are already present in the text. If such characters were XSS-harmful
// in a given context, they would have already been escaped or replaced
// by earlier escaping such as H=attribute.
static const template_modifiers::ModifierInfo g_prefix_line_info(
    "", '\0', template_modifiers::XSS_WEB_STANDARD,
    &template_modifiers::prefix_line);

const char * const kDefaultTemplateDirectory = ctemplate::kCWD;   // "./"
// Note this name is syntactically impossible for a user to accidentally use.
const char * const kMainSectionName = "__{{MAIN}}__";
static vector<TemplateDictionary*>* g_use_current_dict;  // vector == {NULL}

// Type, var, and mutex used to store template objects in the internal cache
class TemplateCacheHash {
 public:
  hash<const char *> string_hash_;
  TemplateCacheHash() : string_hash_() {}
  size_t operator()(const pair<string, int>& p) const {
    // Using + here is silly, but should work ok in practice
    return string_hash_(p.first.c_str()) + p.second;
  }
  // Less operator for MSVC's hash containers.  We make int be the
  // primary key, unintuitively, because it's a bit faster.
  bool operator()(const pair<string, int>& a,
                  const pair<string, int>& b) const {
    return (a.second == b.second
            ? a.first < b.first
            : a.second < b.second);
  }
  // These two public members are required by msvc.  4 and 8 are defaults.
  static const size_t bucket_size = 4;
  static const size_t min_buckets = 8;
};
typedef hash_map<pair<string, int>, Template*, TemplateCacheHash>
  TemplateCache;
static TemplateCache *g_template_cache;

// A very simple logging system
static int kVerbosity = 0;   // you can change this by hand to get vlogs
#define LOG(level)   std::cerr << #level ": "
#define VLOG(level)  if (kVerbosity >= level)  std::cerr << "V" #level ": "

#define LOG_TEMPLATE_NAME(severity, template) \
   LOG(severity) << "Template " << template->template_file() << ": "

#define LOG_AUTO_ESCAPE_ERROR(error_msg, my_template) \
      LOG_TEMPLATE_NAME(ERROR, my_template); \
      LOG(ERROR) << "Auto-Escape: " << error_msg << endl;

// We are in auto-escape mode.
#define AUTO_ESCAPE_MODE(context) ((context) != TC_MANUAL)

// Auto-Escape contexts which utilize the HTML Parser.
#define AUTO_ESCAPE_PARSING_CONTEXT(context) \
  ((context) == TC_HTML || (context) == TC_JS || (context) == TC_CSS)

typedef pair<const template_modifiers::ModifierInfo*, string> ModifierAndValue;

// ----------------------------------------------------------------------
// AutoModifyDirective
// AddModval()
// InitializeGlobalModifiers()
// FilenameValidForContext()
// GetTemplateContext()
// GetModifierForContext()
// FindLongestMatch()
// PrettyPrintModifiers()
// CheckInHTMLProper()
//    Static methods for the auto-escape mode specifically.

// For escaping variables under the auto-escape mode:
// Each directive below maps to a distinct sequence of
// escaping directives (i.e a vector<ModifierAndValue>) applied
// to a variable during run-time substitution.
// The directives are stored in a global array (g_mods_ae)
// initialized under lock in InitializeGlobalModifiers.
enum AutoModifyDirective {
  AM_DIR_EMPTY,   // When no directives are needed.
  AM_DIR_HTML,
  AM_DIR_HTML_UNQUOTED,
  AM_DIR_JS,
  AM_DIR_JS_NUMBER,
  AM_DIR_URL_HTML,
  AM_DIR_URL_QUERY,
  AM_DIR_STYLE,
  AM_DIR_XML,
  AM_DIR_NULL,
};
static vector<ModifierAndValue>* g_mods_ae[AM_DIR_NULL];

// Convenience wrapper when initializing the global ModifiersAndNonces pointer
// corresponding to the directive given.
static void AddModval(AutoModifyDirective dir, const char *modval_name,
                      const string& value) {
  const template_modifiers::ModifierInfo *modval;
  modval = template_modifiers::FindModifier(modval_name, strlen(modval_name),
                                            value.c_str(), value.length());
  assert(modval);
  g_mods_ae[dir]->push_back(
      pair<const template_modifiers::ModifierInfo*, string>(modval, value));
}

// Performs a one-time initialization of the global ModifierAndValue vector
// (g_mods_ae) required for the auto-escape mode.
// . Only called under a mutex lock from AssureGlobalsInitialized.
// . Can assert if the modifiers in template_modifiers.cc were somehow
//   removed (which would be a bug there) or if called multiple times.
static void InitializeGlobalModifiers() {
  for (int i = 0; i < AM_DIR_NULL; i++) {
    assert(g_mods_ae[i] == NULL);
    g_mods_ae[i] = new vector<ModifierAndValue>();
  }
  AddModval(AM_DIR_HTML, "html_escape", "");
  AddModval(AM_DIR_HTML_UNQUOTED, "html_escape_with_arg", "=attribute");
  AddModval(AM_DIR_JS, "javascript_escape", "");
  AddModval(AM_DIR_JS_NUMBER, "javascript_escape_with_arg", "=number");
  AddModval(AM_DIR_URL_HTML, "url_escape_with_arg", "=html");
  AddModval(AM_DIR_URL_QUERY, "url_query_escape", "");
  // TODO(jad): cleanse_css may be too strict for names of style attributes.
  AddModval(AM_DIR_STYLE, "cleanse_css", "");
  AddModval(AM_DIR_XML, "xml_escape", "");
}

// Perfoms matching of filename against the TemplateContext
// and warns in the log on mismatch using "unwritten" filename
// conventions below for templates in our codebase:
//   1. If filename contains "css", "stylesheet" or "style"
//      check that it has type TC_CSS.
//   2. If filename contains "js" or "javascript" check that
//      it has type TC_JS.
// Returns false if there was a mismatch although currently
// we ignore it and just rely on the LOG(WARNING) in the logs.
static bool FilenameValidForContext(const string& filename,
                                    TemplateContext context) {
  // TODO(jad): Improve by also checking for "word" boundaries.
  if ( filename.find("css") != string::npos ||
       filename.find("stylesheet") != string::npos ||
       filename.find("style") != string::npos) {
    if (context != TC_CSS) {
      LOG(WARNING) << "Template filename " << filename
                   << " indicates CSS but given TemplateContext"
                   << " was not TC_CSS." << endl;
      return false;
    }
  } else if (filename.find("js") != string::npos ||
             filename.find("javascript") != string::npos) {
    if (context != TC_JS) {
      LOG(WARNING) << "Template filename " << filename
                   << " indicates javascript but given TemplateContext"
                   << " was not TC_JS." << endl;
      return false;
    }
  }
  return true;
}

// In Auto-Escape mode, when we encounter a template-include directive,
// we need to determine the type of the template to include to
// give as an argument to the GetTemplate factory method. This method
// determines that based on the initial context of the template it
// is being included in and where applicable the state of the parser.
// Only TC_HTML and TC_JS require a parser to determine context,
// for all other contexts, the context is the same as the initial one
// since no context transitions are possible.
static TemplateContext GetTemplateContext(TemplateContext my_context,
                                          HtmlParser *htmlparser) {
  if (my_context == TC_HTML || my_context == TC_JS) {
    assert(htmlparser);
    if (htmlparser->InJavascript())
      return TC_JS;
    return TC_HTML;
  }
  return my_context;
}

// Based on the state of the parser, determines the appropriate escaping
// directive and returns a pointer to the corresponding
// global ModifierAndValue vector. Called when a variable template node
// is traversed.
// Returns NULL if there is no suitable modifier for that context in
// which the case the caller is expected to fail the template initialization.
static const vector<ModifierAndValue>*
GetModifierForContext(TemplateContext my_context, HtmlParser *htmlparser,
                      const Template* my_template) {
  assert(AUTO_ESCAPE_MODE(my_context));

  // In these states, the initial template context is sufficient
  // to determine the context. There are no state transitions.
  if (my_context == TC_NONE)
    return g_mods_ae[AM_DIR_EMPTY];
  else if (my_context == TC_XML)
    return g_mods_ae[AM_DIR_XML];
  else if (my_context == TC_JSON)
    return g_mods_ae[AM_DIR_JS];

  // Remaining states all have a parser.
  assert(AUTO_ESCAPE_PARSING_CONTEXT(my_context));
  assert(htmlparser);

  // Two cases of being inside javascript:
  // 1. Inside raw javascript (within a <script> tag). If the value
  //    is quoted we apply javascript_escape, if not we have to coerce
  //    it to a safe value due to the risk of javascript code execution
  //    hence apply :J=number. If arbitrary code needs to be inserted
  //    at run-time, the developer must use :none.
  // 2. In the value of an attribute that takes javascript such
  //    as onmouseevent in '<a href="someUrl" onmousevent="{{EVENT}}">'.
  //    That will be covered in the STATE_VALUE state logic below.
  if (htmlparser->InJavascript() &&
      htmlparser->state() != HtmlParser::STATE_VALUE) {
    if (htmlparser->IsJavascriptQuoted())
      return g_mods_ae[AM_DIR_JS];
    else
      return g_mods_ae[AM_DIR_JS_NUMBER];
  }
  switch (htmlparser->state()) {
    case HtmlParser::STATE_VALUE:{
      AutoModifyDirective dir = AM_DIR_HTML;        // Initial value arbitrary.
      string attribute_name = htmlparser->attribute();
      switch (htmlparser->AttributeType()) {
        case HtmlParser::ATTR_URI:
          // Case 1: The URL is quoted:
          // . Apply :U=html if it is a complete URL or :h if it is a fragment.
          // Case 2: The URL is not quoted:
          // .  If it is a complete URL, we have no safe modifiers that
          //   won't break it so we have to fail.
          // .  If it is a URL fragment, then :u is safe and not likely to
          //   break the URL.
          if (!htmlparser->IsAttributeQuoted()) {
            if (htmlparser->ValueIndex() == 0) {   // Complete URL.
              string error_msg("Value of URL attribute \"" + attribute_name +
                               "\" must be enclosed in quotes.");
              LOG_AUTO_ESCAPE_ERROR(error_msg, my_template);
              return NULL;
            } else {                                // URL fragment.
              dir = AM_DIR_URL_QUERY;
            }
          } else {
            // Only validate the URL if we have a complete URL,
            // otherwise simply html_escape.
            if (htmlparser->ValueIndex() == 0)
              dir = AM_DIR_URL_HTML;                // Complete URL.
            else
              dir = AM_DIR_HTML;                    // URL fragment.
          }
          break;
        case HtmlParser::ATTR_REGULAR:
          // If the value is quoted, simply HTML escape, otherwise
          // apply stricter escaping using H=attribute.
          if (htmlparser->IsAttributeQuoted())
            dir = AM_DIR_HTML;
          else
            dir = AM_DIR_HTML_UNQUOTED;
          break;
        case HtmlParser::ATTR_STYLE:
          // If the value is quoted apply :c, otherwise fail.
          if (htmlparser->IsAttributeQuoted()) {
            dir = AM_DIR_STYLE;
          } else {
            string error_msg("Value of style attribute \"" + attribute_name +
                             "\" must be enclosed in quotes.");
            LOG_AUTO_ESCAPE_ERROR(error_msg, my_template);
            return NULL;
          }
          break;
        case HtmlParser::ATTR_JS:
          // We require javascript accepting attributes (such as onclick)
          // to be HTML quoted, otherwise they are vulnerable to
          // HTML attribute insertion via the use of whitespace.
          if (!htmlparser->IsAttributeQuoted()) {
            string error_msg("Value of javascript attribute \"" +
                             attribute_name + "\" must be enclosed in quotes.");
            LOG_AUTO_ESCAPE_ERROR(error_msg, my_template);
            return NULL;
          }
          // If the variable is quoted apply javascript_escape otherwise
          // apply javascript_number which will ensure it is safe against
          // code injection.
          // Note: We normally need to HTML escape after javascript escape
          // but the javascript escape implementation provided makes the
          // HTML escape redundant so simply javascript escape.
          if (htmlparser->IsJavascriptQuoted())
            dir = AM_DIR_JS;
          else
            dir = AM_DIR_JS_NUMBER;
          break;
        case HtmlParser::ATTR_NONE:
          assert("We should be in attribute!" == NULL);
        default:
          assert("Should not be able to get here." == NULL);
          return NULL;
      }
      // In STATE_VALUE particularly, the parser may get out of sync with
      // the correct state - that the browser sees - due to the fact that
      // it does not get to parse run-time content (variables). So we tell
      // the parser there is content that will be expanded here.
      // A good example is:
      //   <a href={{URL}} alt={{NAME}}>
      // The parser sees <a href= alt=> and interprets 'alt=' to be
      // the value of href.
      htmlparser->InsertText();  // Ignore return value.
      return g_mods_ae[dir];
    }
    case HtmlParser::STATE_TAG:{
      // Apply H=attribute to tag names since they are alphabetic.
      // Examples of tag names: TITLE, BODY, A and BR.
      return g_mods_ae[AM_DIR_HTML_UNQUOTED];
    }
    case HtmlParser::STATE_ATTR:{
      // Apply H=attribute to attribute names since they are alphabetic.
      // Examples of attribute names: HREF, SRC and WIDTH.
      return g_mods_ae[AM_DIR_HTML_UNQUOTED];
    }
    case HtmlParser::STATE_COMMENT:
    case HtmlParser::STATE_TEXT:{
      return g_mods_ae[AM_DIR_HTML];
    }
    default:{
      assert("Should not be able to get here." == NULL);
      return NULL;
    }
  }
  assert("Should not be able to get here." == NULL);
  return NULL;
}

// Returns the largest int N indicating how many XSS safe alternative
// modifiers are in the in-template modifiers already.
// . If N is equal to the number of modifiers determined by the Auto Escaper,
//   we have a full match and the in-template modifiers were safe. We leave
//   them untouched.
// . Otherwise, N is less (or zero) and we have a partial match (or none).
//   The in-template modifiers are not XSS safe and need the missing ones,
//   i.e. those in the auto escape modifiers which are not in the first N.
//
// We allow in-template modifiers to have extra modifiers than we deem
// necessary, for e.g. :j:h when :j would have sufficed. But to make sure
// these modifiers do not introduce XSS concerns we require that they
// be in the same XssClass as the modifier we had.
// For example :h:x-bla is not safe in HTML context because x-bla is
// in a different XssClass as our :h whereas :h:j would be safe.
static size_t FindLongestMatch(const vector<ModifierAndValue>& modvals_man,
                               const vector<ModifierAndValue>& modvals_auto) {
  if (modvals_auto.empty())
    return 0;

  // See if modvals_auto is "consistent" with the modifiers that are
  // already present (modvals_man).  This is true if all the
  // modifiers in auto also occur in man, and any gaps between them
  // (if any) are filled by "neutral" modifiers that do not affect
  // xss-safety. We go through the vectors backwards.
  // If all of modvals_auto is not consistent, maybe a prefix of it
  // is; that's better than nothing, since we only need to auto-apply
  // the suffix that's not already in modvals_man.
  typedef vector<ModifierAndValue>::const_reverse_iterator ModIterator;
  for (ModIterator end_of_prefix = modvals_auto.rbegin();
       end_of_prefix != modvals_auto.rend();
       ++end_of_prefix) {
    ModIterator curr_auto = end_of_prefix;
    ModIterator curr_man = modvals_man.rbegin();
    while (curr_auto != modvals_auto.rend() &&
           curr_man != modvals_man.rend()) {
      if (IsSafeXSSAlternative(*curr_auto->first, *curr_man->first)) {
        ++curr_auto;
        ++curr_man;
      } else if (curr_man->first->xss_class == curr_auto->first->xss_class &&
                 curr_man->first->xss_class != template_modifiers::XSS_UNIQUE) {
        ++curr_man;  // Ignore this modifier: it's harmless.
      } else {
        break;      // An incompatible modifier; we've failed
      }
    }
    if (curr_auto == modvals_auto.rend())  // got through them all, full match!
      return curr_auto - end_of_prefix;
  }
  return 0;
}

// Convenience function to dump the (zero or more) modifiers (and values)
// in the format:
// :modifier1[=val1][:modifier2][=val2]...
// If the modifier does not have a short_name, we print its long_name instead.
// Note that the '=' is the first character of the value when present.
static string PrettyPrintModifiers(const vector<ModifierAndValue>& modvals) {
  string out;
  for (vector<ModifierAndValue>::const_iterator it = modvals.begin();
       it != modvals.end();  ++it) {
    out.append(":");
    if (it->first->short_name)   // short_name is a char.
      out.append(1, it->first->short_name);
    else
      out.append(it->first->long_name);
    if (!it->second.empty())
      out.append(it->second.c_str());
  }
  return out;
}

// Checks that the current context - current state of the HTML Parser -
// indicates we are inside HTML text only. If instead, the parser
// indicates we are within an HTML comment or in an HTML tag
// (within the name of the tag, the name or value of an attribute)
// we log a warning.
// This is intended to be called when a template is being included
// because we only support inclusion of templates within HTML text proper.
// Currently only returns true as we just log the error.
static bool CheckInHTMLProper(HtmlParser *htmlparser, const string& filename) {
  assert(htmlparser);
  if (htmlparser->state() != HtmlParser::STATE_TEXT) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%d", htmlparser->state());
    LOG(WARNING) << "Template filename " << filename
                 << " ended in a non-expected state " << string(buf)
                 << ". This may prevent auto-escaping from working correctly."
                 << endl;
  }
  return true;
}

// ----------------------------------------------------------------------
// WriteOneHeaderEntry()
//    This dumps information about a template that is useful to
//    make_tpl_varnames_h -- information about the variable and
//    section names used in a template, so we can define constants
//    to refer to them instead of having to type them in by hand.
//    Output is *appended* to outstring.
// ----------------------------------------------------------------------

class HeaderEntryStringHash {   // not all STL implementations define this...
 public:
  hash<const char *> hash_;     // ...but they all seem to define this
  HeaderEntryStringHash() : hash_() {}
  size_t operator()(const string& s) const {
    return hash_(s.c_str());    // just convert the string to a const char*
  }
  // Less operator for MSVC's hash containers.
  bool operator()(const string& a, const string& b) const {
    return a < b;
  }
  // These two public members are required by msvc.  4 and 8 are defaults.
  static const size_t bucket_size = 4;
  static const size_t min_buckets = 8;
};

static void WriteOneHeaderEntry(string *outstring,
                                const string& variable,
                                const string& full_pathname) {
  MutexLock ml(&g_header_mutex);

  // we use hash_map instead of hash_set just to keep the stl size down
  static hash_map<string, bool, HeaderEntryStringHash> vars_seen;
  static string current_file;
  static string prefix;

  if (full_pathname != current_file) {
    // changed files so re-initialize the static variables
    vars_seen.clear();
    current_file = full_pathname;

    // remove the path before the filename
    string filename(ctemplate::Basename(full_pathname));

    prefix = "k";
    bool take_next = true;

    for (string::size_type i = 0; i < filename.length(); i++) {
      if (filename[i] == '.') {
        // stop when we find the dot
        break;
      }
      if (take_next) {
        if (filename.substr(i,4) == "post") {
          // stop before we process post...
          break;
        }
        prefix = prefix + filename[i];
        take_next = false;
      }
      if (filename[i] == '_') {
        take_next = true;
      }
    }
    prefix = prefix + "_";
  }

  // print out the variable, but only if we haven't seen it before.
  if (vars_seen.find(variable) == vars_seen.end()) {
    if (variable == kMainSectionName || variable.find("BI_") == 0) {
      // We don't want to write entries for __MAIN__ or the built-ins
    } else {
      *outstring += (string("const char * const ")
                     + prefix
                     + variable
                     + " = \""
                     + variable
                     + "\";\n");
    }
    vars_seen[variable] = true;
  }
}

// ----------------------------------------------------------------------
// TemplateToken
//    A TemplateToken is a string marked with a token type enum.  The string
//    has different meanings for different token types.  For text, the
//    string is the text itself.   For variable and template types, the
//    string is the name of the variable holding the value or the
//    template name, resp.  For section types, the string is the name
//    of the section, used to retrieve the hidden/visible state and
//    the associated list of dictionaries, if any.
// ----------------------------------------------------------------------

enum TemplateTokenType { TOKENTYPE_UNUSED,        TOKENTYPE_TEXT,
                         TOKENTYPE_VARIABLE,      TOKENTYPE_SECTION_START,
                         TOKENTYPE_SECTION_END,   TOKENTYPE_TEMPLATE,
                         TOKENTYPE_COMMENT,       TOKENTYPE_NULL };

}  // anonymous namespace

// A TemplateToken is a typed string. The semantics of the string depends on the
// token type, as follows:
//   TOKENTYPE_TEXT          - the text
//   TOKENTYPE_VARIABLE      - the name of the variable
//   TOKENTYPE_SECTION_START - the name of the section being started
//   TOKENTYPE_SECTION_END   - the name of the section being ended
//   TOKENTYPE_TEMPLATE      - the name of the variable whose value will be
//                             the template filename
//   TOKENTYPE_COMMENT       - the empty string, not used
//   TOKENTYPE_NULL          - the empty string
// All non-comment tokens may also have modifiers, which follow the name
// of the token: the syntax is {{<PREFIX><NAME>:<mod>:<mod>:<mod>...}}
// The modifiers are also stored as a string, starting with the first :
struct TemplateToken {
  TemplateTokenType type;
  const char* text;
  size_t textlen;
  vector<ModifierAndValue> modvals;
  TemplateToken(TemplateTokenType t, const char* txt, size_t len,
                const vector<ModifierAndValue>* m)
      : type(t), text(txt), textlen(len) {
    if (m) modvals = *m;
  }

  string ToString() const {   // used for debugging (annotations)
    string retval(text, textlen);
    for (vector<ModifierAndValue>::const_iterator it = modvals.begin();
         it != modvals.end();  ++it) {
      const string& modname = it->first->long_name;
      retval += string(":") + modname;
      if (!it->first->is_registered)
        retval += "<not registered>";
    }
    return retval;
  }

  // Updates the correct modifiers for the token (variable or template node)
  // based on our computed modifiers from the HTML parser context as well
  // as the in-template modifiers that may have been provided.
  // If the in-template modifiers are considered safe, we use them
  // without modification. This could happen in one of two cases:
  //   1. The token has the ":none" modifier as its last or only modifier.
  //   2. The escaping modifiers are XSS-equivalent to the ones we computed.
  //
  // If the in-template modifiers are not found to be safe, we add
  // the escaping modifiers we determine missing and issue a warning in the
  // logs. This is done based on a longest match search between the two
  // modifiers vectors, refer to comment in FindLongestMatch.
  void UpdateModifier(const vector<ModifierAndValue>* auto_modvals) {
    // Common case: no modifiers given in template. Assign our own. No warning.
    if (modvals.empty()) {
      modvals = *auto_modvals;
      return;
    }
    // Variable is considered safe, do not touch.
    if (modvals.back().first->long_name == "none") {
      return;
    }

    size_t longest_match = FindLongestMatch(modvals, *auto_modvals);
    if (longest_match == auto_modvals->size()) {
      return;             // We have a complete match, nothing to do.
    } else {              // Copy missing ones and issue warning.
      assert(longest_match >= 0 && longest_match < auto_modvals->size());
      const string before = PrettyPrintModifiers(modvals); // for logging.
      modvals.insert(modvals.end(), auto_modvals->begin() + longest_match,
                           auto_modvals->end());
      LOG(WARNING) << "Token: " << string(text, textlen)
                   << " has missing in-template modifiers. You gave " << before
                   << " and we computed " << PrettyPrintModifiers(*auto_modvals)
                   << ". We changed to " << PrettyPrintModifiers(modvals)
                   << endl;
    }
  }
};

// This applies the modifiers to the string in/inlen, and writes the end
// result directly to the end of outbuf.  Precondition: |modifiers| > 0.
static void EmitModifiedString(const vector<ModifierAndValue>& modifiers,
                               const char* in, size_t inlen,
                               const ModifierData* data,
                               ExpandEmitter* outbuf) {
  string result;
  if (modifiers.size() > 1) {
    // If there's more than one modifiers, we need to store the
    // intermediate results in a temp-buffer.  We use a string.
    // We'll assume that each modifier adds about 12% to the input
    // size.
    result.reserve((inlen + inlen/8) + 16);
    StringEmitter scratchbuf(&result);
    modifiers.front().first->modifier->Modify(in, inlen, data,
                                              &scratchbuf,
                                              modifiers.front().second);
    // Only used when modifiers.size() > 2
    for (vector<ModifierAndValue>::const_iterator it = modifiers.begin() + 1;
         it != modifiers.end()-1;  ++it) {
      string output_of_this_modifier;
      output_of_this_modifier.reserve(result.size() + result.size()/8 + 16);
      StringEmitter scratchbuf2(&output_of_this_modifier);
      it->first->modifier->Modify(result.c_str(), result.size(), data,
                                  &scratchbuf2, it->second);
      result.swap(output_of_this_modifier);
    }
    in = result.data();
    inlen = result.size();
  }
  // For the last modifier, we can write directly into outbuf
  assert(!modifiers.empty());
  modifiers.back().first->modifier->Modify(in, inlen, data, outbuf,
                                           modifiers.back().second);
}

static void AppendTokenWithIndent(int level, string *out, const string& before,
                                  const TemplateToken& token,
                                  const string& after) {
 out->append(string(level * kIndent, ' '));
 string token_string(token.text, token.textlen);
 out->append(before + token_string + after);
}

// ----------------------------------------------------------------------
// TemplateNode
//    When we read a template, we decompose it into its components:
//    variables, sections, include-templates, and runs of raw text.
//    Each of these we see becomes one TemplateNode.  TemplateNode
//    is the abstract base class; each component has its own type.
// ----------------------------------------------------------------------

class TemplateNode {
 public:
  TemplateNode() {}
  virtual ~TemplateNode() {}

  // Expands the template node using the supplied dictionary. The
  // result is placed into output_buffer.  If force_annotate_dictionary is
  // not NULL, and force_annotate_dictionary->ShoudlAnnotateOutput() is
  // true, the output is annotated, even if
  // dictionary->ShouldAnnotateOutput() is false.
  // Returns true iff all the template files load and parse correctly.
  virtual bool Expand(ExpandEmitter *output_buffer,
                      const TemplateDictionary *dictionary,
                      const TemplateDictionary *force_annotate) const = 0;

  // Writes entries to a header file to provide syntax checking at
  // compile time.
  virtual void WriteHeaderEntries(string *outstring,
                                  const string& filename) const = 0;

  // Appends a representation of the node and its subnodes to a string
  // as a debugging aid.
  virtual void DumpToString(int level, string *out) const = 0;

 public:
  // In addition to the pure-virtual API (above), we also define some
  // useful helper functions, as static methods.  These could have
  // been global methods, but what the hey, we'll put them here.

  // Return an opening template annotation, that can be emitted
  // in the Expanded template output.  Used for generating template
  // debugging information in the result document.
  static string OpenAnnotation(const string& name,
                               const string& value) {
    // The opening annotation looks like: {{#name=value}}
    return string("{{#") + name + string("=") + value + string("}}");
  }

  // Return a closing template annotation, that can be emitted
  // in the Expanded template output.  Used for generating template
  // debugging information in the result document.
  static string CloseAnnotation(const string& name) {
    // The closing annotation looks like: {{/name}}
    return string("{{/") + name + string("}}");
  }

 protected:
  typedef list<TemplateNode *> NodeList;

 private:
  TemplateNode(const TemplateNode&);   // disallow copying
  void operator=(const TemplateNode&);
};

// ----------------------------------------------------------------------
// TextTemplateNode
//    The simplest template-node: it holds runs of raw template text,
//    that should be emitted verbatim.  The text points into
//    template_text_.
// ----------------------------------------------------------------------

class TextTemplateNode : public TemplateNode {
 public:
  explicit TextTemplateNode(const TemplateToken& token)
      : token_(token) {
    VLOG(2) << "Constructing TextTemplateNode: "
            << string(token_.text, token_.textlen) << endl;
  }
  virtual ~TextTemplateNode() {
    VLOG(2) << "Deleting TextTemplateNode: "
            << string(token_.text, token_.textlen) << endl;
  }

  // Expands the text node by simply outputting the text string. This
  // virtual method does not use TemplateDictionary or force_annotate.
  // Returns true iff all the template files load and parse correctly.
  virtual bool Expand(ExpandEmitter *output_buffer,
                      const TemplateDictionary *,
                      const TemplateDictionary *) const {
    output_buffer->Emit(token_.text, token_.textlen);
    return true;
  }

  // A noop for text nodes
  virtual void WriteHeaderEntries(string *outstring,
                                  const string& filename) const {
    return;
  }

  // Appends a representation of the text node to a string.
  virtual void DumpToString(int level, string *out) const {
    assert(out);
    AppendTokenWithIndent(level, out, "Text Node: -->|", token_, "|<--\n");
  }

 private:
  TemplateToken token_;  // The text held by this node.
};

// ----------------------------------------------------------------------
// VariableTemplateNode
//    Holds a variable to be replaced when the template is expanded.
//    The variable is stored in a token object, which has a char*
//    that points into template_text_.  There may also be modifiers,
//    which are applied at Expand time.
// ----------------------------------------------------------------------

class VariableTemplateNode : public TemplateNode {
 public:
  explicit VariableTemplateNode(const TemplateToken& token)
      : token_(token) {
    VLOG(2) << "Constructing VariableTemplateNode: "
            << string(token_.text, token_.textlen) << endl;
  }
  virtual ~VariableTemplateNode() {
    VLOG(2) << "Deleting VariableTemplateNode: "
            << string(token_.text, token_.textlen) << endl;
  }

  // Expands the variable node by outputting the value (if there is one)
  // of the node variable which is retrieved from the dictionary
  // Returns true iff all the template files load and parse correctly.
  virtual bool Expand(ExpandEmitter *output_buffer,
                      const TemplateDictionary *dictionary,
                      const TemplateDictionary *force_annotate) const;

  virtual void WriteHeaderEntries(string *outstring,
                                  const string& filename) const {
    WriteOneHeaderEntry(outstring, string(token_.text, token_.textlen),
                        filename);
  }

  // Appends a representation of the variable node to a string. We
  // also append the modifiers for that variable in the form:
  // :modifier1[=val1][:modifier2][=val2]...\n
  virtual void DumpToString(int level, string *out) const {
    assert(out);
    AppendTokenWithIndent(level, out, "Variable Node: ", token_,
                          PrettyPrintModifiers(token_.modvals) + "\n");
  }

 protected:
  const TemplateToken token_;
};

bool VariableTemplateNode::Expand(ExpandEmitter *output_buffer,
                                  const TemplateDictionary *dictionary,
                                  const TemplateDictionary *force_annotate)
    const {
  if (force_annotate->ShouldAnnotateOutput()) {
    output_buffer->Emit(OpenAnnotation("VAR", token_.ToString()));
  }

  const string var(token_.text, token_.textlen);
  const char *value = dictionary->GetSectionValue(var);

  if (token_.modvals.empty()) {   // no need to modify value
    output_buffer->Emit(value);               // so just emit it
  } else {
    EmitModifiedString(token_.modvals, value, strlen(value),
                       force_annotate->modifier_data(), output_buffer);
  }

  if (force_annotate->ShouldAnnotateOutput()) {
    output_buffer->Emit(CloseAnnotation("VAR"));
  }

  return true;
}

// ----------------------------------------------------------------------
// TemplateTemplateNode
//    Holds a variable to be replaced by an expanded (included)
//    template whose filename is the value of the variable in the
//    dictionary.
//    Also holds the corresponding TemplateContext as follows:
//    . When not in the Auto Escape mode, TemplateContext is
//      simply TC_MANUAL.
//    . In Auto Escape mode, TemplateContext is determined based on the
//      parent's context and possibly the state of the parser when we
//      encounter the template-include directive. It is then passed on to
//      GetTemplateWithAutoEscape when this included template is initialized.
// ----------------------------------------------------------------------

class TemplateTemplateNode : public TemplateNode {
 public:
  explicit TemplateTemplateNode(const TemplateToken& token, Strip strip,
                                TemplateContext context)
      : token_(token), strip_(strip), initial_context_(context) {
    VLOG(2) << "Constructing TemplateTemplateNode: "
            << string(token_.text, token_.textlen) << endl;
  }
  virtual ~TemplateTemplateNode() {
    VLOG(2) << "Deleting TemplateTemplateNode: "
            << string(token_.text, token_.textlen) << endl;
  }

  // Expands the template node by retrieving the name of a template
  // file from the supplied dictionary, expanding it (using this
  // dictionary if none other is provided in the TemplateDictionary),
  // and then outputting this newly expanded template in place of the
  // original variable.
  // Returns true iff all the template files load and parse correctly.
  virtual bool Expand(ExpandEmitter *output_buffer,
                      const TemplateDictionary *dictionary,
                      const TemplateDictionary *force_annotate) const;

  virtual void WriteHeaderEntries(string *outstring,
                                  const string& filename) const {
    WriteOneHeaderEntry(outstring, string(token_.text, token_.textlen),
                        filename);
  }

  virtual void DumpToString(int level, string *out) const {
    assert(out);
    AppendTokenWithIndent(level, out, "Template Node: ", token_, "\n");
  }

 protected:
  const TemplateToken token_; // text is the name of a template file.
  Strip strip_;       // Flag to pass from parent template to included template.
  const enum TemplateContext initial_context_;  // for auto-escaping.
};

// If no value is found in the dictionary for the template variable
// in this node, then no output is generated in place of this variable.
bool TemplateTemplateNode::Expand(ExpandEmitter *output_buffer,
                                  const TemplateDictionary *dictionary,
                                  const TemplateDictionary *force_annotate)
    const {
  bool error_free = true;

  const string variable(token_.text, token_.textlen);
  if (dictionary->IsHiddenTemplate(variable)) {
    // if this "template include" section is "hidden", do nothing
    return true;
  }

  // see if there is a vector of dictionaries for this template
  const vector<TemplateDictionary*> *dv =
    &dictionary->GetTemplateDictionaries(variable);
  if (dv->empty())     // empty dict means 'expand once using containing dict'
    dv = g_use_current_dict;  // a vector with one element: NULL

  vector<TemplateDictionary*>::const_iterator dv_iter = dv->begin();
  for (; dv_iter != dv->end(); ++dv_iter) {
    // We do this in the loop, because maybe one day we'll support
    // each expansion having its own template dictionary.  That's also
    // why we pass in the dictionary-index as an argument.
    const char* const filename = dictionary->GetIncludeTemplateName(
        variable, dv_iter - dv->begin());
    // if it wasn't set then treat it as if it were "hidden", i.e, do nothing
    if (!filename || filename[0] == '\0')
      continue;

    Template *included_template;
    // pass the flag values from the parent template to the included template
    if (AUTO_ESCAPE_MODE(initial_context_)) {
      included_template = Template::GetTemplateWithAutoEscaping(filename,
                                                                strip_,
                                                                initial_context_);
    } else {
      included_template = Template::GetTemplate(filename, strip_);
    }

    // if there was a problem retrieving the template, bail!
    if (!included_template) {
      LOG(ERROR) << "Failed to load included template: " << filename << endl;
      error_free = false;
      continue;
    }

    // Expand the included template once for each "template specific"
    // dictionary.  Normally this will only iterate once, but it's
    // possible to supply a list of more than one sub-dictionary and
    // then the template explansion will be iterative, just as though
    // the included template were an iterated section.
    if (force_annotate->ShouldAnnotateOutput()) {
      output_buffer->Emit(OpenAnnotation("INC", token_.ToString()));
    }

    // sub-dictionary NULL means 'just use the current dictionary instead'.
    // We force children to annotate the output if we have to.
    // If the include-template has modifiers, we need to expand to a string,
    // modify the string, and append to output_buffer.  Otherwise (common
    // case), we can just expand into the output-buffer directly.
    if (token_.modvals.empty()) {  // no need to modify sub-template
      error_free &= included_template->Expand(
          output_buffer,
          *dv_iter ? *dv_iter : dictionary,
          force_annotate);
    } else {
      string sub_template;
      StringEmitter subtemplate_buffer(&sub_template);
      error_free &= included_template->Expand(
          &subtemplate_buffer,
          *dv_iter ? *dv_iter : dictionary,
          force_annotate);
      EmitModifiedString(token_.modvals,
                         sub_template.data(), sub_template.size(),
                         force_annotate->modifier_data(), output_buffer);
    }
    if (force_annotate->ShouldAnnotateOutput()) {
      output_buffer->Emit(CloseAnnotation("INC"));
    }
  }

  return error_free;
}

// ----------------------------------------------------------------------
// SectionTemplateNode
//    Holds the name of a section and a list of subnodes contained
//    in that section.
// ----------------------------------------------------------------------

class SectionTemplateNode : public TemplateNode {
 public:
  explicit SectionTemplateNode(const TemplateToken& token);
  virtual ~SectionTemplateNode();

  // The highest level parsing method. Reads a single token from the
  // input -- taken from my_template->parse_state_ -- and adds the
  // corresponding type of node to the template's parse
  // tree.  It may add a node of any type, whether text, variable,
  // section, or template to the list of nodes contained in this
  // section.  Returns true iff we really added a node and didn't just
  // end a section or hit a syntax error in the template file.
  // You should hold a write-lock on my_template->mutex_ when calling this.
  // (unless you're calling it from a constructor).
  bool AddSubnode(Template *my_template);

  // Expands a section node as follows:
  //   - Checks to see if the section is hidden and if so, does nothing but
  //     return
  //   - Tries to retrieve a list of dictionaries from the supplied dictionary
  //     stored under this section's name
  //   - If it finds a non-empty list of dictionaries, it iterates over the
  //     list and calls itself recursively to expand the section once for
  //     each dictionary
  //   - If there is no dictionary list (or an empty dictionary list somehow)
  //     is found, then the section is expanded once using the supplied
  //     dictionary. (This is the mechanism used to expand each single
  //     iteration of the section as well as to show a non-hidden section,
  //     allowing the section template syntax to be used for both conditional
  //     and iterative text).
  // Returns true iff all the template files load and parse correctly.
  virtual bool Expand(ExpandEmitter *output_buffer,
                      const TemplateDictionary *dictionary,
                      const TemplateDictionary* force_annotate) const;

  // Writes a header entry for the section name and calls the same
  // method on all the nodes in the section
  virtual void WriteHeaderEntries(string *outstring,
                                  const string& filename) const;

  virtual void DumpToString(int level, string *out) const;

 protected:
  const TemplateToken token_;   // text is the name of the section
  NodeList node_list_;  // The list of subnodes in the section
  // When the last node read was literal text that ends with "\n? +"
  // (that is, leading whitespace on a line), this stores the leading
  // whitespace.  This is used to properly indent included
  // sub-templates.
  string indentation_;


  // A protected method used in parsing the template file
  // Finds the next token in the file and return it. Anything not inside
  // a template marker is just text. Each template marker type, delimited
  // by "{{" and "}}" is a different type of token. The first character
  // inside the opening curly braces indicates the type of the marker,
  // as follows:
  //    # - Start a section
  //    / - End a section
  //    > - A template file variable (the "include" directive)
  //    ! - A template comment
  //    <alnum or _> - A scalar variable
  // One more thing. Before a name token is returned, if it happens to be
  // any type other than a scalar variable, and if the next character after
  // the closing curly braces is a newline, then the newline is eliminated
  // from the output. This reduces the number of extraneous blank
  // lines in the output. If the template author desires a newline to be
  // retained after a final marker on a line, they must add a space character
  // between the marker and the linefeed character.
  TemplateToken GetNextToken(Template* my_template);

  // The specific methods called used by AddSubnode to add the
  // different types of nodes to this section node.
  // Currently only reason to fail (return false) is if the
  // HTML parser failed to parse in auto-escape mode. Note that we do
  // not attempt to parse if the HTML Parser state shows an error already.
  bool AddTextNode(const TemplateToken* token, Template* my_template);
  bool AddVariableNode(TemplateToken* token, Template* my_template);
  bool AddTemplateNode(TemplateToken* token, Template* my_template);
  bool AddSectionNode(const TemplateToken* token, Template* my_template);
};

// --- constructor and destructor, Expand, Dump, and WriteHeaderEntries

SectionTemplateNode::SectionTemplateNode(const TemplateToken& token)
    : token_(token), indentation_("\n") {
  VLOG(2) << "Constructing SectionTemplateNode: "
          << string(token_.text, token_.textlen) << endl;
}

SectionTemplateNode::~SectionTemplateNode() {
  VLOG(2) << "Deleting SectionTemplateNode: "
          << string(token_.text, token_.textlen) << " and its subnodes"
          << endl;

  // Need to delete the member of the list because the list is a list
  // of pointers to these instances.
  NodeList::iterator iter = node_list_.begin();
  for (; iter != node_list_.end(); ++iter) {
    delete (*iter);
  }
  VLOG(2) << "Finished deleting subnodes of SectionTemplateNode: "
          << string(token_.text, token_.textlen) << endl;
}

bool SectionTemplateNode::Expand(ExpandEmitter *output_buffer,
                                 const TemplateDictionary *dictionary,
                                 const TemplateDictionary *force_annotate)
    const {
  bool error_free = true;

  const vector<TemplateDictionary*> *dv;

  const string variable(token_.text, token_.textlen);

  // The section named __{{MAIN}}__ is special: you always expand it
  // exactly once using the containing (main) dictionary.
  if (token_.text == kMainSectionName) {
    dv = g_use_current_dict;   // 'expand once, using the passed in dictionary'
  } else {
    if (dictionary->IsHiddenSection(variable)) {
      return true;      // if this section is "hidden", do nothing
    }
    dv = &dictionary->GetDictionaries(variable);
    if (dv->empty())    // empty dict means 'expand once using containing dict'
      dv = g_use_current_dict;  // a vector with one element: NULL
  }

  vector<TemplateDictionary*>::const_iterator dv_iter = dv->begin();
  for (; dv_iter != dv->end(); ++dv_iter) {
    if (force_annotate->ShouldAnnotateOutput()) {
      output_buffer->Emit(OpenAnnotation("SEC", token_.ToString()));
    }

    // Expand using the section-specific dictionary.  A sub-dictionary
    // of NULL means 'just use the current dictionary instead'.
    // We force children to annotate the output if we have to.
    NodeList::const_iterator iter = node_list_.begin();
    for (; iter != node_list_.end(); ++iter) {
      error_free &=
        (*iter)->Expand(output_buffer,
                        *dv_iter ? *dv_iter : dictionary, force_annotate);
    }

    if (force_annotate->ShouldAnnotateOutput()) {
      output_buffer->Emit(CloseAnnotation("SEC"));
    }
  }

  return error_free;
}

void SectionTemplateNode::WriteHeaderEntries(string *outstring,
                                             const string& filename) const {
  WriteOneHeaderEntry(outstring, string(token_.text, token_.textlen),
                      filename);

  NodeList::const_iterator iter = node_list_.begin();
  for (; iter != node_list_.end(); ++iter) {
    (*iter)->WriteHeaderEntries(outstring, filename);
  }
}

void SectionTemplateNode::DumpToString(int level, string *out) const {
  assert(out);
  AppendTokenWithIndent(level, out, "Section Start: ", token_, "\n");
  NodeList::const_iterator iter = node_list_.begin();
  for (; iter != node_list_.end(); ++iter) {
    (*iter)->DumpToString(level + 1, out);
  }
  AppendTokenWithIndent(level, out, "Section End: ", token_, "\n");
}

// --- AddSubnode and its sub-routines

// Under auto-escape (and parsing-enabled modes) advance the parser state.
// TextTemplateNode is the only TemplateNode type that can change
// the state of the parser.
// Returns false only if the HTML parser failed to parse in
// auto-escape mode.
bool SectionTemplateNode::AddTextNode(const TemplateToken* token,
                                      Template* my_template) {
  assert(token);
  bool success = true;
  HtmlParser *htmlparser = my_template->htmlparser_;

  if (token->textlen > 0) {  // ignore null text sections
    node_list_.push_back(new TextTemplateNode(*token));
    if (AUTO_ESCAPE_PARSING_CONTEXT(my_template->initial_context_)) {
      assert(htmlparser);
      if (htmlparser->state() == HtmlParser::STATE_ERROR ||
          htmlparser->Parse(token->text, static_cast<int>(token->textlen)) ==
          HtmlParser::STATE_ERROR) {
        string error_msg =  "Failed parsing: " +
            string(token->text, token->textlen) +
            "\nIn: " + string(token_.text, token_.textlen);
        LOG_AUTO_ESCAPE_ERROR(error_msg, my_template);
        success = false;
      }
    }
  }
  return success;
}

// In Auto Escape mode (htmlparser not null), we update the variable
// modifiers baseed on what is specified in the template and what the
// parser computes for that context.
// Returns false only if the HTML parser failed to parse in
// auto-escape mode.
//
// We also have special logic for BI_SPACE and BI_NEWLINE.
// Even though they look like variables, they're really not: the user
// is expected to use them in situations where they'd normally put
// a space character or a newline character, but can't for technical
// reasons (namely, that the template parser would strip these
// characters because of the STRIP mode it's in).  So unlike other
// variables, we want to treat these variables as literal text.  This
// means that we never add modifiers to them, but we do let the
// htmlparser know about them in order to update its state. Existing
// modifiers will be honored.
bool SectionTemplateNode::AddVariableNode(TemplateToken* token,
                                          Template* my_template) {
  assert(token);
  bool success = true;
  HtmlParser *htmlparser = my_template->htmlparser_;
  TemplateContext initial_context = my_template->initial_context_;

  if (AUTO_ESCAPE_MODE(initial_context)) {
      // Determines modifiers for the variable in auto escape mode.
      string variable_name(token->text, token->textlen);
      // We declare in the documentation that if the user changes the
      // value of these variables, they must only change it to a value
      // that's "equivalent" from the point of view of an html parser.
      // So it's ok to hard-code in that these are " " and "\n",
      // respectively, even though in theory the user could change them
      // (to say, BI_NEWLINE == "\r\n").
      if (variable_name == "BI_SPACE" || variable_name == "BI_NEWLINE") {
        if (AUTO_ESCAPE_PARSING_CONTEXT(initial_context)) {
          assert(htmlparser);
          if (htmlparser->state() == HtmlParser::STATE_ERROR ||
              htmlparser->Parse(variable_name == "BI_SPACE" ? " " : "\n") ==
              HtmlParser::STATE_ERROR)
            success = false;
        }
      } else {
        const vector<ModifierAndValue>* modvals =
            GetModifierForContext(initial_context, htmlparser, my_template);
        if (modvals == NULL)
          success = false;
        else
          token->UpdateModifier(modvals);
      }
  }
  node_list_.push_back(new VariableTemplateNode(*token));
  return success;
}

// AddSectionNode
bool SectionTemplateNode::AddSectionNode(const TemplateToken* token,
                                         Template* my_template) {
  assert(token);
  SectionTemplateNode *new_node = new SectionTemplateNode(*token);

  // Not only create a new section node, but fill it with all *its*
  // subnodes by repeatedly calling AddSubNode until it returns false
  // (indicating either the end of the section or a syntax error)
  while (new_node->AddSubnode(my_template)) {
    // Found a new subnode to add
  }
  node_list_.push_back(new_node);
  return true;
}

bool SectionTemplateNode::AddTemplateNode(TemplateToken* token,
                                          Template* my_template) {
  assert(token);
  bool success = true;
  TemplateContext initial_context = my_template->initial_context_;
  // Safe to call even outside auto-escape (TC_MANUAL).
  TemplateContext context = GetTemplateContext(initial_context,
                                               my_template->htmlparser_);

  if (AUTO_ESCAPE_MODE(initial_context)) {
    // Auto-Escape supports specifying modifiers at the template-include
    // level in which case it checks them for XSS-correctness and modifies
    // them as needed. Unlike the case of Variable nodes, we only modify
    // them if they are present, we do not add any otherwise.
    // If there are modifiers, we give the included template the context
    // TC_NONE so that no further modifiers are applied to any variables
    // or templates it may in turn include. Otherwise we end up escaping
    // multiple times.
    if (!token->modvals.empty()) {
      const vector<ModifierAndValue>* modvals =
          GetModifierForContext(initial_context, my_template->htmlparser_,
                                my_template);
      if (modvals == NULL)
        success = false;
      else
        token->UpdateModifier(modvals);
      context = TC_NONE;
    }
    if (AUTO_ESCAPE_PARSING_CONTEXT(initial_context)) {
      assert(my_template->htmlparser_);
      // In Auto-Escape context where HTML parsing happens we require that
      // an included template be only included within regular text not
      // inside a tag or attribute of a tag (say). There are three reasons:
      // 1. We currently do not have a way to initialize the HTML Parser
      //    with that level of granularity.
      // 2. We currently do not have a way for an included template to
      //    relay its ending state back to its parent so it is easier
      //    to impose this restriction and enforce it.
      // 3. It seems really bad form to do anyways. The demand may prove
      //    us wrong but no harm in starting with extra requirements.
      CheckInHTMLProper(my_template->htmlparser_,
                        string(token->text, token->textlen));
    }
  }
  // pass the flag values from my_template to the new node
  node_list_.push_back(new TemplateTemplateNode(*token, my_template->strip_,
                                                context));
  return success;
}

// If "text" ends with a newline followed by whitspace, returns a
// string holding that whitespace.  Otherwise, returns the empty
// string.  If implicit_newline is true, also consider the text to be
// an indentation if it consists entirely of whitespace; this is set
// when we know that right before this text there was a newline, or
// this text is the beginning of a document.
static string GetIndentation(const char* text, size_t textlen,
                             bool implicit_newline) {
  const char* nextline;    // points to one char past the last newline
  for (nextline = text + textlen; nextline > text; --nextline)
    if (nextline[-1] == '\n') break;
  if (nextline == text && !implicit_newline)
    return "";                // no newline found, so no indentation

  bool prefix_is_whitespace = true;
  for (const char* p = nextline; p < text + textlen; ++p) {
    if (*p != ' ' && *p != '\t') {
      prefix_is_whitespace = false;
      break;
    }
  }
  if (prefix_is_whitespace && text + textlen > nextline)
    return string(nextline, text + textlen - nextline);
  else
    return "";
}

bool SectionTemplateNode::AddSubnode(Template *my_template) {
  bool auto_escape_success = true;
  // Don't proceed if we already found an error
  if (my_template->state() == TS_ERROR) {
    return false;
  }

  // Stop when the buffer is empty.
  if (my_template->parse_state_.bufstart >= my_template->parse_state_.bufend) {
    // running out of file contents ends the section too
    if (token_.text != kMainSectionName) {
      // if we are not in the main section, we have a syntax error in the file
      LOG_TEMPLATE_NAME(ERROR, my_template);
      LOG(ERROR) << "File ended before all sections were closed" << endl;
      my_template->set_state(TS_ERROR);
    }
    return false;
  }

  TemplateToken token = GetNextToken(my_template);

  switch (token.type) {
    case TOKENTYPE_TEXT:
      auto_escape_success = this->AddTextNode(&token, my_template);
      // Store the indentation (trailing whitespace after a newline), if any.
      this->indentation_ = GetIndentation(token.text, token.textlen,
                                          indentation_ == "\n");
      break;
    case TOKENTYPE_VARIABLE:
      auto_escape_success = this->AddVariableNode(&token, my_template);
      this->indentation_.clear(); // clear whenever last read wasn't whitespace
      break;
    case TOKENTYPE_SECTION_START:
      auto_escape_success = this->AddSectionNode(&token, my_template);
      this->indentation_.clear(); // clear whenever last read wasn't whitespace
      break;
    case TOKENTYPE_SECTION_END:
      // Don't add a node. Just make sure we are ending the right section
      // and return false to indicate the section is complete
      if (token.textlen != token_.textlen ||
          memcmp(token.text, token_.text, token.textlen)) {
        LOG_TEMPLATE_NAME(ERROR, my_template);
        LOG(ERROR) << "Found end of different section than the one I am in"
                   << "\nFound: " << string(token.text, token.textlen)
                   << "\nIn: " << string(token_.text, token_.textlen) << endl;
        my_template->set_state(TS_ERROR);
      }
      this->indentation_.clear(); // clear whenever last read wasn't whitespace
      return false;
      break;
    case TOKENTYPE_TEMPLATE:
      // If this template is indented (eg, " {{>SUBTPL}}"), make sure
      // every line of the expanded template is indented, not just the
      // first one.  We do this by adding a modifier that applies to
      // the entire template node, that inserts spaces after newlines.
      if (!this->indentation_.empty()) {
        token.modvals.push_back(ModifierAndValue(&g_prefix_line_info,
                                                 this->indentation_));
      }
      auto_escape_success = this->AddTemplateNode(&token, my_template);
      this->indentation_.clear(); // clear whenever last read wasn't whitespace
      break;
    case TOKENTYPE_COMMENT:
      // Do nothing. Comments just drop out of the file altogether.
      break;
    case TOKENTYPE_NULL:
      // GetNextToken either hit the end of the file or a syntax error
      // in the file. Do nothing more here. Just return false to stop
      // processing.
      return false;
      break;
    default:
      // This shouldn't happen. If it does, it's a programmer error.
      LOG_TEMPLATE_NAME(ERROR, my_template);
      LOG(ERROR) << "Invalid token type returned from GetNextToken" << endl;
  }

  if (!auto_escape_success) {
    // The error is logged where it happens. Here indicate
    // the initialization failed.
    my_template->set_state(TS_ERROR);
    return false;
  }

  // for all the cases where we did not return false
  return true;
}

// --- GetNextToken and its subroutines

// A valid marker name is made up of alphanumerics and underscores...
// nothing else.
static bool IsValidName(const char* name, int namelen) {
  for (const char *cur_char = name; cur_char - name <  namelen; ++cur_char) {
    if (!isalnum(*cur_char) && *cur_char != '_')
      return false;
  }
  return true;
}

// If we're pointing to the end of a line, and in a high enough strip mode,
// pass over the newline.  If the line ends in a \, we skip over the \ and
// keep the newline.  Returns a pointer to the new 'start' location, which
// is either 'start' or after a newline.
static const char* MaybeEatNewline(const char* start, const char* end,
                                   Strip strip) {
  // first, see if we have the escaped linefeed sequence
  if (end - start >= 2 && start[0] == '\\' && start[1] == '\n') {
    ++start;    // skip over the \, which keeps the \n
  } else if (end - start >= 1 && start[0] == '\n' &&
             strip >= STRIP_WHITESPACE) {
    ++start;    // skip over the \n in high strip_ modes
  }
  return start;
}

// The root directory of templates
string *Template::template_root_directory_ = NULL;

// When the parse fails, we take several actions.  msg is a stream
#define FAIL(msg)   do {                                                  \
  LOG_TEMPLATE_NAME(ERROR, my_template);                                  \
  LOG(ERROR) << msg << endl;                                              \
  my_template->set_state(TS_ERROR);                                       \
  /* make extra-sure we never try to parse anything more */               \
  my_template->parse_state_.bufstart = my_template->parse_state_.bufend;  \
  return TemplateToken(TOKENTYPE_NULL, "", 0, NULL);                      \
} while (0)

// Parses the text of the template file in the input_buffer as
// follows: If the buffer is empty, return the null token.  If getting
// text, search for the next "{{" sequence. If one is found, return
// all the text collected up to that sequence in a TextToken and
// change the token-parsing phase variable to GETTING_NAME, so the next
// call will know to look for a named marker, instead of more text.
// If getting a name, read the next character to learn what kind of
// marker it is. Then collect the characters of the name up to the
// "}}" sequence. If the "name" is a template comment, then we do not
// return the text of the comment in the token. If it is any other
// valid type of name, we return the token with the appropriate type
// and the name.  If any syntax errors are discovered (like
// inappropriate characters in a name, not finding the closing curly
// braces, etc.) an error message is logged, the error state of the
// template is set, and a NULL token is returned.  Updates parse_state_.
// You should hold a write-lock on my_template->mutex_ when calling this.
// (unless you're calling it from a constructor).
TemplateToken SectionTemplateNode::GetNextToken(Template *my_template) {
  Template::ParseState* ps = &my_template->parse_state_;   // short abbrev.
  const char* token_start = ps->bufstart;

  if (ps->bufstart >= ps->bufend) {    // at end of buffer
    return TemplateToken(TOKENTYPE_NULL, "", 0, NULL);
  }

  switch (ps->phase) {
    case Template::ParseState::GETTING_TEXT: {
      const char* token_end = (const char*)memchr(ps->bufstart, '{',
                                                  ps->bufend - ps->bufstart);
      if (!token_end) {
        // Didn't find a '{' so just grab all the rest of the buffer
        token_end = ps->bufend;
        ps->bufstart = ps->bufend;   // next token will start at EOF
      } else {
        // see if the next char is also a '{' and remove it if it is
        // ...but NOT if the next TWO characters are "{{"
        if ((token_end+1 < ps->bufend && token_end[1] == '{') &&
            !(token_end+2 < ps->bufend && token_end[2] == '{')) {
          // Next is a {{.  Eat that up and move to GETTING_NAME mode
          ps->phase = Template::ParseState::GETTING_NAME;
          ps->bufstart = token_end+2;    // next token will start past {{
        } else {
          ++token_end;               // the { is part of our text we're reading
          ps->bufstart = token_end;  // next token starts just after the {
        }
      }
      return TemplateToken(TOKENTYPE_TEXT, token_start,
                           token_end - token_start, NULL);
    }

    case Template::ParseState::GETTING_NAME: {
      TemplateTokenType ttype;
      // Find out what type of name we are getting
      switch (token_start[0]) {
        case '#':
          ttype = TOKENTYPE_SECTION_START;
          ++token_start;
          break;
        case '/':
          ttype = TOKENTYPE_SECTION_END;
          ++token_start;
          break;
        case '!':
          ttype = TOKENTYPE_COMMENT;
          ++token_start;
          break;
        case '>':
          ttype = TOKENTYPE_TEMPLATE;
          ++token_start;
          break;
        default:
          // the assumption that the next char is alnum or _ will be
          // tested below in the call to IsValidName().
          ttype = TOKENTYPE_VARIABLE;
      }

      // Now get the name (or the comment, as the case may be)
      const char* token_end = (const char*)memchr(token_start, '}',
                                                  ps->bufend - token_start);

      if (!token_end) {   // Didn't find a '}', so name never ended.  Error!
        FAIL("No ending '}' when parsing name starting with '"
             << string(token_start, ps->bufend-token_start) << "'");
      }

      // We found a }.  Next char should be a } too, or name isn't ended; error!
      if (token_end+1 == ps->bufend || token_end[1] != '}') {
        if (ttype == TOKENTYPE_COMMENT) {
          FAIL("Illegal to use the '}' character in template "
               << "comment that starts with '"
               << string(token_start, token_end+1 - token_start) << "'");
        } else {
          FAIL("Invalid name in template starting with '"
               << string(token_start, token_end+1 - token_start) << "'");
        }
      }

      // Comments are a special case, since they don't have a name or action
      if (ttype == TOKENTYPE_COMMENT) {
        ps->phase = Template::ParseState::GETTING_TEXT;
        ps->bufstart = token_end + 2;   // read past the }}
        // If requested, remove any unescaped linefeed following a comment
        ps->bufstart = MaybeEatNewline(ps->bufstart, ps->bufend,
                                       my_template->strip_);
        // For comments, don't bother returning the text
        return TemplateToken(ttype, "", 0, NULL);
      }

      // Now we have the name, possibly with following modifiers.
      // Find the modifier-start.
      const char* mod_start = (const char*)memchr(token_start, ':',
                                                  token_end - token_start);
      if (mod_start == NULL)
        mod_start = token_end;

      // Make sure the name is legal.
      if (ttype != TOKENTYPE_COMMENT &&
          !IsValidName(token_start, mod_start - token_start)) {
        FAIL("Illegal name in template '"
             << string(token_start, mod_start-token_start) << "'");
      }

      // Figure out what all the modifiers are.  Mods are colon-separated.
      vector<ModifierAndValue> modifiers;
      const char* mod_end;
      for (const char* mod = mod_start; mod < token_end; mod = mod_end) {
        assert(*mod == ':');
        ++mod;   // skip past the starting colon
        mod_end = (const char*)memchr(mod, ':', token_end - mod);
        if (mod_end == NULL)
          mod_end = token_end;
        // Modifiers can be of the form :modname=value.  Extract out value
        const char* value = (const char*)memchr(mod, '=', mod_end - mod);
        if (value == NULL)
          value = mod_end;
        string value_string(value, mod_end - value);
        // Convert the string to a functor, and error out if we can't.
        const template_modifiers::ModifierInfo* modstruct =
            template_modifiers::FindModifier(mod, value - mod,
                                             value, mod_end - value);
        // There are various ways a modifier syntax can be illegal.
        if (modstruct == NULL) {
          FAIL("Unknown modifier for variable "
               << string(token_start, mod_start - token_start) << ": "
               << "'" << string(mod, value - mod) << "'");
        } else if (!modstruct->modval_required && value < mod_end) {
          FAIL("Modifier for variable "
               << string(token_start, mod_start - token_start) << ":"
               << string(mod, value - mod) << " "
               << "has illegal mod-value '" << value_string << "'");
        } else if (modstruct->modval_required && value == mod_end) {
          FAIL("Modifier for variable "
               << string(token_start, mod_start - token_start) << ":"
               << string(mod, value - mod) << " "
               << "is missing a required mod-value");
        }

        modifiers.push_back(
            pair<const template_modifiers::ModifierInfo*, string>(
                modstruct, value_string));
      }

      // For now, we only allow variable and include nodes to have
      // modifiers.  I think it's better not to have this for
      // sections, but instead to modify all the text and vars in the
      // section appropriately, but I could be convinced otherwise.
      if (!modifiers.empty() &&
          ttype != TOKENTYPE_VARIABLE && ttype != TOKENTYPE_TEMPLATE) {
        FAIL(string(token_start, token_end - token_start)
             << "malformed: only variables and template-includes "
             << "are allowed to have modifiers");
      }

      // Whew!  We passed the guantlet.  Get ready for the next token
      ps->phase = Template::ParseState::GETTING_TEXT;
      ps->bufstart = token_end + 2;   // read past the }}
      // If requested, remove any linefeed following a comment,
      // or section start or end, or template marker, unless
      // it is escaped by '\'
      if (ttype != TOKENTYPE_VARIABLE) {
        ps->bufstart = MaybeEatNewline(ps->bufstart, ps->bufend,
                                       my_template->strip_);
      }

      // create and return the TEXT token that we found
      return TemplateToken(ttype, token_start, mod_start - token_start,
                           &modifiers);
    }

    default: {
      FAIL("Programming error: Unexpected parse phase while "
           << "parsing template: " << ps->phase);
    }
  }
}


// ----------------------------------------------------------------------
// Template::Template()
// Template::~Template()
// Template::AssureGlobalsInitialized()
// Template::GetTemplate()
// Template::GetTemplateWithAutoEscaping()
// Template::TemplateCacheKey()
// Template::GetTemplateCommon()
//   Calls ReloadIfChanged to load the template the first time.
//   The constructor is private; GetTemplate() is the factory
//   method used to actually construct a new template if needed.
// ----------------------------------------------------------------------

Template::Template(const string& filename, Strip strip,
                   TemplateContext context)
    : filename_(filename), filename_mtime_(0), strip_(strip),
      state_(TS_EMPTY), template_text_(NULL), template_text_len_(0),
      tree_(NULL), parse_state_(), mutex_(new Mutex),
      initial_context_(context), htmlparser_(NULL) {
  // Make sure g_use_current_dict, etc. are initted before any possbility
  // of calling Expand() or other Template classes that access globals.
  AssureGlobalsInitialized();

  VLOG(2) << "Constructing Template for " << template_file() << endl;

  // Preserve whitespace in Javascript files because carriage returns
  // can convey meaning for comment termination and closures
  if ( strip_ == STRIP_WHITESPACE && filename.length() >= 3 &&
       !strcmp(filename.c_str() + filename.length() - 3, ".js") ) {
    strip_ = STRIP_BLANK_LINES;
  }

  // TC_MANUAL and remaining Auto-Escape contexts don't need a parser.
  if (AUTO_ESCAPE_PARSING_CONTEXT(initial_context_)) {
    htmlparser_ = new HtmlParser();
    if (initial_context_ == TC_JS)
      htmlparser_->ResetMode(HtmlParser::MODE_JS);
    FilenameValidForContext(filename_, initial_context_);
  }
  ReloadIfChangedLocked();
}

Template::~Template() {
  VLOG(2) << endl << "Deleting Template for " << template_file() << endl;
  delete mutex_;
  delete tree_;
  // Delete this last, since tree has pointers into template_text_
  delete[] template_text_;
  delete htmlparser_;
}

// NOTE: This function must be called by any static function that
// accesses any of the variables set here.
void Template::AssureGlobalsInitialized() {
  MutexLock ml(&g_static_mutex);   // protects all the vars defined here
  if (template_root_directory_ == NULL) {  // only need to run this once!
    template_root_directory_ = new string(kDefaultTemplateDirectory);
    // this_dict is a dictionary with a single NULL entry in it
    g_use_current_dict = new vector<TemplateDictionary*>;
    g_use_current_dict->push_back(NULL);
    InitializeGlobalModifiers();   // initializes g_mods_ae.
  }
}

// This factory method disables auto-escape mode for backwards compatibility.
Template *Template::GetTemplate(const string& filename, Strip strip) {
  return GetTemplateCommon(filename, strip, TC_MANUAL);
}

// This factory method is called only when the auto-escape mode
// should be enabled for that template (and included templates).
Template *Template::GetTemplateWithAutoEscaping(const string& filename,
                                                Strip strip,
                                                TemplateContext context) {
  // Must provide a valid context to enable auto-escaping.
  assert(AUTO_ESCAPE_MODE(context));
  return GetTemplateCommon(filename, strip, context);
}

// With the addition of the auto-escape mode, the cache key includes
// the TemplateContext as well to allow the same template to
// be initialized in both modes and have their copies separate. It also
// allows for the less likely case of the same template initialized
// with different TemplateContexts.
// For simplicity, the TemplateContext is folded in with Strip to
// form a single integer. Both are very small and definitely fit in the
// first 16 bits [lower 8 for Strip, upper 8 for TemplateContext].
Template::TemplateCacheKey
Template::GetTemplateCacheKey(const string& name,
                              Strip strip, TemplateContext context) {
  int strip_and_context  = strip + (context << 8);
  return pair<string, int>(name, strip_and_context);
}

// Protected factory method.
Template *Template::GetTemplateCommon(const string& filename, Strip strip,
                                      TemplateContext context) {
  // No need to have the cache-mutex acquired for this step
  string abspath(ctemplate::PathJoin(template_root_directory(), filename));

  Template* tpl = NULL;
  {
    MutexLock ml(&g_cache_mutex);
    if (g_template_cache == NULL)
      g_template_cache = new TemplateCache;

    TemplateCacheKey template_cache_key = GetTemplateCacheKey(abspath, strip,
                                                              context);
    tpl = (*g_template_cache)[template_cache_key];
    if (!tpl) {
      tpl = new Template(abspath, strip, context);
      (*g_template_cache)[template_cache_key] = tpl;
    }
  }

  // TODO(csilvers): acquire a lock here, because we're looking at
  // state().  The problem is when GetTemplate is called during
  // Expand(), the expanding template already holds the read-lock,
  // so if the expanding template tried to include itself, that
  // would lead to deadlock.

  // Note: if the status is TS_ERROR here, we don't attempt to reload
  // the template file, but we don't return the template object
  // either.  If the state is TS_EMPTY, it means tpl was just constructed
  // and doesn't have *any* content yet, so we should certainly reload.
  if (tpl->state() == TS_SHOULD_RELOAD || tpl->state() == TS_EMPTY) {
    tpl->ReloadIfChangedLocked();
  }

  // If the state is TS_ERROR, we leave the state as is, but return
  // NULL.  We won't try to load the template file again until the
  // state gets changed to TS_SHOULD_RELOAD by another call to
  // ReloadAllIfChanged.
  if (tpl->state() != TS_READY) {
    return NULL;
  } else {
    return tpl;
  }
}

// ----------------------------------------------------------------------
// Template::BuildTree()
// Template::WriteHeaderEntry()
// Template::Dump()
//    These kick off their various parsers -- BuildTree for the
//    main task of parsing a Template when it's read from memory,
//    WriteHeaderEntry for parsing for make_tpl_varnames_h, and
//    Dump() for when Dump() is called by the caller.
// ----------------------------------------------------------------------

// NOTE: BuildTree takes over ownership of input_buffer, and will delete it.
//       It should have been created via new[].
// You should hold a write-lock on mutex_ before calling this
// (unless you're calling it from a constructor).
// In auto-escape mode, the HTML context is tracked as the tree is being
// built, in a single pass. When this function completes, all variables
// will have the proper modifiers set.
bool Template::BuildTree(const char* input_buffer,
                         const char* input_buffer_end) {
  // Assign an arbitrary name to the top-level node
  parse_state_.bufstart = input_buffer;
  parse_state_.bufend = input_buffer_end;
  parse_state_.phase = ParseState::GETTING_TEXT;
  SectionTemplateNode *top_node = new SectionTemplateNode(
      TemplateToken(TOKENTYPE_SECTION_START,
                    kMainSectionName, strlen(kMainSectionName), NULL));
  while (top_node->AddSubnode(this)) {
    // Add the rest of the template in.
  }

  // get rid of the old tree, whenever we try to build a new one.
  delete tree_;
  delete[] template_text_;
  tree_ = top_node;
  template_text_ = input_buffer;
  template_text_len_ = input_buffer_end - input_buffer;

  // TS_ERROR can also be set by the auto-escape mode, at the point
  // where the parser failed to parse.
  if (state() != TS_ERROR) {
    set_state(TS_READY);
    return true;
  } else {
    delete tree_;
    tree_ = NULL;
    delete[] template_text_;
    template_text_ = NULL;
    template_text_len_ = 0;
    return false;
  }
}

void Template::WriteHeaderEntries(string *outstring) const {
  if (state() == TS_READY) {   // only write header entries for 'good' tpls
    tree_->WriteHeaderEntries(outstring, template_file());
  }
}

// Dumps the parsed structure of the template for debugging assistance.
// It goes to stdout instead of LOG to avoid possible truncation due to size.
void Template::Dump(const char *filename) const {
  string out;
  DumpToString(filename, &out);
  fwrite(out.data(), 1, out.length(), stdout);
  fflush(stdout);
}

void Template::DumpToString(const char *filename, string *out) const {
  if (!out)
    return;
  out->append("------------Start Template Dump [" + string(filename) +
                        "]--------------\n");
  if (tree_) {
    tree_->DumpToString(1, out);
  } else {
    out->append("No parse tree has been produced for this template\n");
  }
  out->append("------------End Template Dump----------------\n");
}

// ----------------------------------------------------------------------
// Template::SetTemplateRootDirectory()
// Template::template_root_directory()
// Template::state()
// Template::set_state()
// Template::template_file()
//    Various introspection or functionality-modifier methods.
//    The template-root-directory is where we look for template
//    files (in GetTemplate and include templates) when they're
//    given with a relative rather than absolute name.  state()
//    is the parse-state (success, error).  template_file() is
//    the filename of a given template object's input.
// ----------------------------------------------------------------------

bool Template::SetTemplateRootDirectory(const string& directory) {
  // Make sure template_root_directory_ has been initialized
  AssureGlobalsInitialized();

  // This is needed since we access/modify template_root_directory_
  MutexLock ml(&g_static_mutex);
  *template_root_directory_ = directory;
  // make sure it ends with '/'
  ctemplate::NormalizeDirectory(template_root_directory_);
  // Make the directory absolute if it isn't already.  This makes code
  // safer if client later does a chdir.
  if (!ctemplate::IsAbspath(*template_root_directory_)) {
    char* cwdbuf = new char[PATH_MAX];   // new to avoid stack overflow
    const char* cwd = getcwd(cwdbuf, PATH_MAX);
    if (!cwd) {   // probably not possible, but best to be defensive
      LOG(WARNING) << "Unable to convert '" << *template_root_directory_
                   << "' to an absolute path, with cwd=" << cwdbuf;
    } else {
      *template_root_directory_ = ctemplate::PathJoin(cwd,
                                                      *template_root_directory_);
    }
    delete[] cwdbuf;
  }

  VLOG(2) << "Setting Template directory to " << *template_root_directory_
          << endl;
  return true;
}

// It's not safe to return a string& in threaded contexts
string Template::template_root_directory() {
  // Make sure template_root_directory_ has been initialized
  AssureGlobalsInitialized();
  MutexLock ml(&g_static_mutex);   // protects the static var t_r_d_
  return *template_root_directory_;
}

void Template::set_state(TemplateState new_state) {
  state_ = new_state;
}

TemplateState Template::state() const {
  return state_;
}

const char *Template::template_file() const {
  return filename_.c_str();
}

// ----------------------------------------------------------------------
// Template::ReloadIfChanged()
// Template::ReloadIfChangedLocked()
// Template::ReloadAllIfChanged()
//    If one template, try immediately to reload it from disk.  If
//    all templates, just set all their statuses to TS_SHOULD_RELOAD,
//    so next time GetTemplate() is called on the template, it will
//    be reloaded from disk if the disk version is newer than the
//    one currently in memory.  ReloadIfChanged() returns true
//    if the file changed and disk *and* we successfully reloaded
//    and parsed it.  It never returns true if filename_ is "".
// ----------------------------------------------------------------------

// Besides being called when locked, it's also ok to call this from
// the constructor, when you know nobody else will be messing with
// this object.
bool Template::ReloadIfChangedLocked() {
  if (filename_.empty()) return false;

  struct stat statbuf;
  if (stat(filename_.c_str(), &statbuf) != 0) {
    LOG(WARNING) << "Unable to stat file " << filename_ << endl;
    // We keep the old tree if there is one, otherwise we're in error
    set_state(tree_ ? TS_READY : TS_ERROR);
    return false;
  }
  if (statbuf.st_mtime == filename_mtime_ && filename_mtime_ > 0
      && tree_) {   // force a reload if we don't already have a tree_
    VLOG(1) << "Not reloading file " << filename_ << ": no new mod-time" << endl;
    set_state(TS_READY);
    return false;   // file's timestamp hasn't changed, so no need to reload
  }

  FILE* fp = fopen(filename_.c_str(), "rb");
  if (fp == NULL) {
    LOG(ERROR) << "Can't find file " << filename_ << "; skipping" << endl;
    // We keep the old tree if there is one, otherwise we're in error
    set_state(tree_ ? TS_READY : TS_ERROR);
    return false;
  }
  char* file_buffer = new char[statbuf.st_size];
  if ( fread(file_buffer, 1, statbuf.st_size, fp) != statbuf.st_size ) {
    LOG(ERROR) << "Error reading file " << filename_
               << ": " << strerror(errno) << endl;
    fclose(fp);
    delete[] file_buffer;
    // We could just keep the old tree, but probably safer to say 'error'
    set_state(TS_ERROR);
    return false;
  }
  fclose(fp);

  // Now that we know we've read the file ok, mark the new mtime
  filename_mtime_ = statbuf.st_mtime;

  // Parse the input one line at a time to get the "stripped" input.
  // Note stripping only makes smaller, so st_size is a safe upper bound.
  char* input_buffer = new char[statbuf.st_size];
  const size_t buflen = InsertFile(file_buffer, statbuf.st_size, input_buffer);
  delete[] file_buffer;

  // Now parse the template we just read.  BuildTree takes over ownership
  // of input_buffer in every case, and will eventually delete it.
  if ( BuildTree(input_buffer, input_buffer + buflen) ) {
    assert(state() == TS_READY);
    return true;
  } else {
    assert(state() != TS_READY);
    return false;
  }
}

bool Template::ReloadIfChanged() {
  // ReloadIfChanged() is protected by mutex_ so when it's called from
  // different threads, they don't stomp on tree_ and state_.
  WriterMutexLock ml(mutex_);
  return ReloadIfChangedLocked();
}

void Template::ReloadAllIfChanged() {
  // This is slightly annoying: we copy all the template-pointers to
  // a vector, so we don't have to hold g_cache_mutex while messing
  // with the templates (which would violate our lock invariant).
  vector<Template*> templates_in_cache;
  {
    MutexLock ml(&g_cache_mutex);   // this protects the static g_template_cache
    if (g_template_cache == NULL) {
      return;
    }
    for (TemplateCache::const_iterator iter = g_template_cache->begin();
         iter != g_template_cache->end();
         ++iter) {
      templates_in_cache.push_back(iter->second);
    }
  }
  for (vector<Template*>::iterator iter = templates_in_cache.begin();
       iter != templates_in_cache.end();
       ++iter) {
    WriterMutexLock ml((*iter)->mutex_);
    (*iter)->set_state(TS_SHOULD_RELOAD);
  }
}

// ----------------------------------------------------------------------
// Template::ClearCache()
//   Deletes all the objects in the template cache.  Note: it's
//   dangerous to clear the cache if other threads are still
//   referencing the templates that are stored in it!
// ----------------------------------------------------------------------

void Template::ClearCache() {
  // We clear the cache by swapping it with an empty cache.  This lets
  // us delete the items in the cache at our leisure without needing
  // to hold g_cache_mutex.
  TemplateCache tmp_cache;
  {
    MutexLock ml(&g_cache_mutex);  // this protects the static g_template_cache
    if (g_template_cache == NULL) {
      return;
    }
    g_template_cache->swap(tmp_cache);   // now g_template_cache is empty
  }
  // Now delete everything we've removed from the cache.
  for (TemplateCache::const_iterator iter = tmp_cache.begin();
       iter != tmp_cache.end();
       ++iter) {
    delete iter->second;
  }
}

// ----------------------------------------------------------------------
// IsBlankOrOnlyHasOneRemovableMarker()
// Template::InsertLine()
// Template::InsertFile()
//    InsertLine() is called by ReloadIfChanged for each line of
//    the input.  In general we just add it to a char buffer so we can
//    parse the entire file at once in a slurp.  However, we do one
//    per-line check: see if the line is either all white space or has
//    exactly one "removable" marker on it and nothing else. A marker
//    is "removable" if it is either a comment, a start section, an
//    end section, or a template include marker.  This affects whether
//    we add a newline or not, in certain Strip modes.
//       InsertFile() takes an entire file in, as a string, and calls
//    InsertLine() on each line, building up the output buffer line
//    by line.
//       Both functions require an output buffer big enough to hold
//    the potential output (for InsertFile(), the output is guaranteed
//    to be no bigger than the input).  They both return the number
//    of bytes they wrote to the output buffer.
// ----------------------------------------------------------------------

// We define our own version rather than using the one in strtuil, mostly
// so we can take a size_t instead of an int.  The code is simple enough.
static void StripTemplateWhiteSpace(const char** str, size_t* len) {
  // Strip off trailing whitespace.
  while ((*len) > 0 && isspace((*str)[(*len)-1])) {
    (*len)--;
  }

  // Strip off leading whitespace.
  while ((*len) > 0 && isspace((*str)[0])) {
    (*len)--;
    (*str)++;
  }
}

// Adjusts line and length iff condition is met, and RETURNS true.
static bool IsBlankOrOnlyHasOneRemovableMarker(const char** line, size_t* len) {
  const char *clean_line = *line;
  size_t new_len = *len;
  StripTemplateWhiteSpace(&clean_line, &new_len);

  // If there was only white space on the line, new_len will now be zero.
  // In that case the line should be removed, so return true.
  if (new_len == 0) {
    *line = clean_line;
    *len = new_len;
    return true;
  }

  // The smallest removable marker is {{!}}, which requires five characters.
  // If there aren't enough characters, then keep the line by returning false.
  if (new_len < 5) {
    return false;
  }

  if (clean_line[0] != '{'            // if first two chars are not "{{"
      || clean_line[1] != '{'
      || (clean_line[2] != '#'        // or next char marks not section start
          && clean_line[2] != '/'     // nor section end
          && clean_line[2] != '>'     // nor template include
          && clean_line[2] != '!')) { // nor comment
    return false;                     // then not what we are looking for.
  }

  const char *end_marker = strstr(clean_line, "}}");

  if (!end_marker                   // if didn't find "}}"
      || end_marker != &clean_line[new_len - 2]) { // or it wasn't at the end
    return false;
  }

  // else return the line stripped of its white space chars so when the
  // marker is removed in expansion, no white space is left from the line
  // that has now been removed
  *line = clean_line;
  *len = new_len;
  return true;
}

size_t Template::InsertLine(const char *line, size_t len, char* buffer) {
  bool add_newline = (len > 0 && line[len-1] == '\n');
  if (add_newline)
    len--;                 // so we ignore the newline from now on

  if (strip_ >= STRIP_WHITESPACE) {
    StripTemplateWhiteSpace(&line, &len);
    add_newline = false;
  }

  // IsBlankOrOnlyHasOneRemovableMarker may modify the two input
  // parameters if the line contains only spaces or only one input
  // marker.  This modification must be done before the line is
  // written to the input buffer. Hence the need for the boolean flag
  // add_newline to be referenced after the Write statement.
  if (strip_ >= STRIP_BLANK_LINES
      && IsBlankOrOnlyHasOneRemovableMarker(&line, &len)) {
    add_newline = false;
  }

  memcpy(buffer, line, len);

  if (add_newline) {
    buffer[len++] = '\n';
  }
  return len;
}

size_t Template::InsertFile(const char *file, size_t len, char* buffer) {
  const char* prev_pos = file;
  const char* next_pos;
  char* write_pos = buffer;

  while ( (next_pos=(char*)memchr(prev_pos, '\n', file+len - prev_pos)) ) {
    next_pos++;      // include the newline
    write_pos += InsertLine(prev_pos, next_pos - prev_pos, write_pos);
    assert(write_pos >= buffer && static_cast<size_t>(write_pos-buffer) <= len);
    prev_pos = next_pos;
  }
  if (prev_pos < file + len) {          // last line doesn't end in a newline
    write_pos += InsertLine(prev_pos, file+len - prev_pos, write_pos);
    assert(write_pos >= buffer && static_cast<size_t>(write_pos-buffer) <= len);
  }
  assert(write_pos >= buffer);
  return static_cast<size_t>(write_pos - buffer);
}

// ----------------------------------------------------------------------
// Template::Expand()
//    This is the main function clients call: it expands a template
//    by expanding its parse tree (which starts with a top-level
//    section node).  For each variable/section/include-template it
//    sees, it replaces the name stored in the parse-tree with the
//    appropriate value from the passed-in dictionary.
// ----------------------------------------------------------------------

bool Template::Expand(ExpandEmitter *expand_emitter,
                      const TemplateDictionary *dict,
                      const TemplateDictionary *force_annotate_output) const {
  // Accumulator for the results of Expand for each sub-tree.
  bool error_free = true;

  // We hold mutex_ the entire time we expand, because
  // ReloadIfChanged(), which also holds mutex_, is allowed to delete
  // tree_, and we want to make sure it doesn't do that (in another
  // thread) while we're expanding.  We also protect state_, etc.
  // Note we only need a read-lock here, so many expands can go on at once.
  ReaderMutexLock ml(mutex_);

  if (state() != TS_READY) {
    // We'd like to reload if state_ == TS_SHOULD_RELOAD, but Expand() is const
    return false;
  }

  const bool should_annotate = (dict->ShouldAnnotateOutput() ||
                                (force_annotate_output &&
                                 force_annotate_output->ShouldAnnotateOutput()));
  if (should_annotate) {
    // Remove the machine dependent prefix from the template file name.
    const char* file = template_file();
    const char* short_file;
    if (dict->ShouldAnnotateOutput()) {
      short_file = strstr(file, dict->GetTemplatePathStart());
    } else {
      short_file = strstr(file, force_annotate_output->GetTemplatePathStart());
    }
    if (short_file != NULL) {
      file = short_file;
    }
    expand_emitter->Emit(TemplateNode::OpenAnnotation("FILE", file));
  }

  // We force our sub-tree to annotate output if we annotate output
  error_free &= tree_->Expand(expand_emitter, dict, force_annotate_output);

  if (should_annotate) {
    expand_emitter->Emit(TemplateNode::CloseAnnotation("FILE"));
  }

  return error_free;
}

bool Template::Expand(string *output_buffer,
                      const TemplateDictionary *dict) const {
  StringEmitter e(output_buffer);
  return Expand(&e, dict, dict);
}

_END_GOOGLE_NAMESPACE_
