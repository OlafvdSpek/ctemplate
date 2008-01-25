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
//
// template_modifiers.h has a description of what each escape-routine does.
//
// When creating a new modifier, you must subclass TemplateModifier
// and define your own Modify() method.  This method takes the string
// to be modified as a char*/int pair.  It then emits the modified
// version of the string to outbuf.  Outbuf is an ExpandEmitter, as
// defined in template_modifiers.h.  It's a very simple type that
// supports appending to a data stream.
//
// Be very careful editing an existing modifier.  Subtle changes can
// introduce the possibility for cross-site scripting attacks.  If you
// do change a modifier, be careful that it does not affect
// the list of Safe XSS Alternatives.

#include "config.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <string>
#include <vector>
#include <google/template_modifiers.h>

using std::string;
using std::vector;


// A most-efficient way to append a string literal to the var named 'out'.
// The ""s ensure literal is actually a string literal
#define APPEND(literal)  out->Emit("" literal "", sizeof(literal)-1)

_START_GOOGLE_NAMESPACE_

namespace template_modifiers {

TemplateModifier::~TemplateModifier() {}


void NullModifier::Modify(const char* in, size_t inlen,
                          const ModifierData*,
                          ExpandEmitter* out, const string& arg) const {
  out->Emit(in, inlen);
}
NullModifier null_modifier;

void HtmlEscape::Modify(const char* in, size_t inlen,
                        const ModifierData*,
                        ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '&': APPEND("&amp;"); break;
      case '"': APPEND("&quot;"); break;
      case '\'': APPEND("&#39;"); break;
      case '<': APPEND("&lt;"); break;
      case '>': APPEND("&gt;"); break;
      case '\r': case '\n': case '\v': case '\f':
      case '\t': APPEND(" "); break;     // non-space whitespace
      default: out->Emit(in[i]);
    }
  }
}
HtmlEscape html_escape;

void PreEscape::Modify(const char* in, size_t inlen,
                       const ModifierData*,
                       ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '&': APPEND("&amp;"); break;
      case '"': APPEND("&quot;"); break;
      case '\'': APPEND("&#39;"); break;
      case '<': APPEND("&lt;"); break;
      case '>': APPEND("&gt;"); break;
      // All other whitespace we leave alone!
      default: out->Emit(in[i]);
    }
  }
}
PreEscape pre_escape;

void SnippetEscape::Modify(const char* in, size_t inlen,
                           const ModifierData*,
                           ExpandEmitter* out, const string& arg) const {
  bool inside_b = false;
  const char * const end = in + inlen;
  for (const char *c = in; c < end; ++c) {
    switch (*c) {
      case '"': {
        APPEND("&quot;");
        break;
      }
      case '\'': {
        APPEND("&#39;");
        break;
      }
      case '>': {
        APPEND("&gt;");
        break;
      }
      case '\r': case '\n': case '\v': case '\f':
      case '\t': {
        APPEND(" ");
        break;      // non-space whitespace
      }
      case '&': {
        if (c + 1 < end && c[1] == '{') {
          // Could be a javascript entity, so we need to escape.
          // (Javascript entities are an xss risk in Netscape 4.)
          APPEND("&amp;");
        } else {
          APPEND("&");
        }
        break;
      }
      case '<': {
        const char* valid_tag = NULL;
        if (!strncmp(c, "<b>", 3) && !inside_b) {
          inside_b = true;
          valid_tag = "<b>";
        } else if (!strncmp(c, "</b>", 4) && inside_b) {
          inside_b = false;
          valid_tag = "</b>";
        } else if (!strncmp(c, "<br>", 4)) {
          valid_tag = "<br>";
        } else if (!strncmp(c, "<wbr>", 5)) {
          valid_tag = "<wbr>";
        }
        if (valid_tag) {
          out->Emit(valid_tag);
          c += strlen(valid_tag) - 1;
        } else {
          APPEND("&lt;");
        }
        break;
      }
      default: {
        out->Emit(*c);
        break;
      }
    }
  }
  if (inside_b) {
    APPEND("</b>");
  }
}
SnippetEscape snippet_escape;

void CleanseAttribute::Modify(const char* in, size_t inlen,
                              const ModifierData*,
                              ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    char c = in[i];
    switch (c) {
      case '-':
      case '.':
      case '_':
      case ':': {
        out->Emit(c);
        break;
      }
      default: {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
          out->Emit(c);
        } else {
          APPEND("_");
        }
        break;
      }
    }
  }
}
CleanseAttribute cleanse_attribute;

void CleanseCss::Modify(const char* in, size_t inlen,
                              const ModifierData*,
                              ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    char c = in[i];
    switch (c) {
      case ' ':
      case '_':
      case '.':
      case ',':
      case '!':
      case '#':
      case '%':
      case '-': {
        out->Emit(c);
        break;
      }
      default: {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
          out->Emit(c);
        }
        break;
      }
    }
  }
}
CleanseCss cleanse_css;

void ValidateUrl::Modify(const char* in, size_t inlen,
                         const ModifierData* per_expand_data,
                         ExpandEmitter* out, const string& arg) const {
  const char* slashpos = (char*)memchr(in, '/', inlen);
  if (slashpos == NULL)
    slashpos = in + inlen;
  const void* colonpos = memchr(in, ':', slashpos - in);
  if (colonpos != NULL) {   // colon before first slash, could be a protocol
    if (inlen > sizeof("http://")-1 &&
        strncasecmp(in, "http://", sizeof("http://")-1) == 0) {
      // We're ok, it's an http protocol
    } else if (inlen > sizeof("https://")-1 &&
               strncasecmp(in, "https://", sizeof("https://")-1) == 0) {
      // https is ok as well
    } else {
      // It's a bad protocol, so return something safe
      chained_modifier_.Modify("#", 1, per_expand_data, out, "");
      return;
    }
  }
  // If we get here, it's a valid url, so just escape it
  chained_modifier_.Modify(in, inlen, per_expand_data, out, "");
}
ValidateUrl validate_url_and_html_escape(html_escape);
ValidateUrl validate_url_and_javascript_escape(javascript_escape);

void XmlEscape::Modify(const char* in, size_t inlen,
                       const ModifierData*,
                       ExpandEmitter* out, const string& arg) const {
  const char* end = in + inlen;
  const char* pos = in;
  for (const char* next_amp = static_cast<const char*>(memchr(in, '&', inlen));
       next_amp; next_amp = static_cast<const char*>(memchr(pos, '&', end-pos))) {
    out->Emit(pos, next_amp - pos);  // emit everything between the ampersands
    if (next_amp + sizeof("&nbsp;")-1 <= end &&
        memcmp(next_amp, "&nbsp;", sizeof("&nbsp;")-1) == 0) {
      out->Emit("&#160;");
      pos = next_amp + sizeof("&nbsp;")-1;
    } else {
      out->Emit('&');           // & == *next_amp
      pos = next_amp + 1;
    }
  }
  out->Emit(pos, end - pos);    // everything past the last ampersand
}
XmlEscape xml_escape;

void JavascriptEscape::Modify(const char* in, size_t inlen,
                              const ModifierData*,
                              ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '"': APPEND("\\x22"); break;
      case '\'': APPEND("\\x27"); break;
      case '\\': APPEND("\\\\"); break;
      case '\t': APPEND("\\t"); break;
      case '\r': APPEND("\\r"); break;
      case '\n': APPEND("\\n"); break;
      case '\b': APPEND("\\b"); break;
      case '&': APPEND("\\x26"); break;
      case '<': APPEND("\\x3c"); break;
      case '>': APPEND("\\x3e"); break;
      case '=': APPEND("\\x3d"); break;
      default: out->Emit(in[i]);
    }
  }
}
JavascriptEscape javascript_escape;

void UrlQueryEscape::Modify(const char* in, size_t inlen,
                            const ModifierData*,
                            ExpandEmitter* out, const string& arg) const {
  // Everything not matching [0-9a-zA-Z.,_*/~!()-] is escaped.
  static unsigned long _safe_characters[8] = {
    0x00000000L, 0x03fff702L, 0x87fffffeL, 0x47fffffeL,
    0x00000000L, 0x00000000L, 0x00000000L, 0x00000000L
  };

  for (size_t i = 0; i < inlen; ++i) {
    unsigned char c = in[i];
    if (c == ' ') {
      out->Emit('+');
    } else if ((_safe_characters[(c)>>5] & (1 << ((c) & 31)))) {
      out->Emit(c);
    } else {
      out->Emit('%');
      out->Emit(((c>>4) < 10 ? ((c>>4) + '0') : (((c>>4) - 10) + 'A')));
      out->Emit(((c&0xf) < 10 ? ((c&0xf) + '0') : (((c&0xf) - 10) + 'A')));
    }
  }
}
UrlQueryEscape url_query_escape;

void JsonEscape::Modify(const char* in, size_t inlen,
                        const ModifierData*,
                        ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '"': APPEND("\\\""); break;
      case '\\': APPEND("\\\\"); break;
      case '/': APPEND("\\/"); break;
      case '\b': APPEND("\\b"); break;
      case '\f': APPEND("\\f"); break;
      case '\n': APPEND("\\n"); break;
      case '\r': APPEND("\\r"); break;
      case '\t': APPEND("\\t"); break;
      default: out->Emit(in[i]);
    }
  }
}
JsonEscape json_escape;

void PrefixLine::Modify(const char* in, size_t inlen,
                        const ModifierData*,
                        ExpandEmitter* out, const string& arg) const {
  while (inlen > 0) {
    const char* nl = (const char*)memchr(in, '\n', inlen);
    const char* cr = (const char*)memchr(in, '\r', nl ? nl - in : inlen);
    size_t linelen;
    if (nl == NULL && cr == NULL) {
      // We're at the last line
      out->Emit(in, inlen);
      break;
    } else {
      // One or both of \r and \n is set; point to the first char past
      // the newline.  Note for \r\n, that's the char after the \n,
      // otherwise, it's the char past the \r or the \n we see.
      if ((nl == NULL) != (cr == NULL))     // one is set, the other is NULL
        linelen = (nl ? nl : cr) + 1 - in;
      else if (nl == cr + 1 || nl < cr)     // \r\n, or \n comes first
        linelen = nl + 1 - in;
      else
        linelen = cr + 1 - in;
    }
    out->Emit(in, linelen);
    out->Emit(arg);               // a new line, so emit the prefix
    in += linelen;
    inlen -= linelen;
    assert(inlen >= 0);
  }
}
PrefixLine prefix_line;


// Must be at least one more than the maximum number of alternative modifiers
// specified in any given element of g_modifiers.
# define MAX_SAFE_ALTERNATIVES 10  // If the compiler complains, increase it.

// Use the empty string if you want a modifier not to have a long-name.
// Use '\0' if you want a modifier not to have a short-name.
// Note: not all modifiers are in this array:
// 1) SnippetEscape: use html_escape_with_arg=snippet to get this
// 2) CleanseAttribute: use html_escape_with_arg=attribute to get this
// 3) ValidateUrl: use html_escape_with_arg=url to get this
//
// Some modifiers define other modifiers that are safe replacements
// from an XSS perspective. Replacements are not commutative so for
// example H=pre considers H=attribute a safe replacement to it
// but H=attribute has no safe replacements.
// This struct is not pretty but allows the definitions to be
// done without the need for a global initialization method.
// Be very careful making a change to g_modifiers as modifiers
// point to other ones within that same array so elements
// may not be re-ordered easily.
static struct ModifierWithAlternatives {
  ModifierInfo modifier_info;
  ModifierInfo* safe_alt_mods[MAX_SAFE_ALTERNATIVES];
} g_modifiers[] = {
  /* 0 */ { ModifierInfo("cleanse_css", 'c', &cleanse_css), {} },
  /* 1 */ { ModifierInfo("html_escape", 'h', &html_escape),
            {&g_modifiers[2].modifier_info,   // html_escape_with_arg=snippet
             &g_modifiers[3].modifier_info,   // html_escape_with_arg=pre
             &g_modifiers[4].modifier_info,   // html_escape_with_arg=attribute
             &g_modifiers[8].modifier_info} },  // pre_escape
  /* 2 */ { ModifierInfo("html_escape_with_arg=snippet", 'H', &snippet_escape),
            {&g_modifiers[1].modifier_info,   // html_escape
             &g_modifiers[3].modifier_info,   // html_escape_with_arg=pre
             &g_modifiers[4].modifier_info,   // html_escape_with_arg=attribute
             &g_modifiers[8].modifier_info} },  // pre_escape
  /* 3 */ { ModifierInfo("html_escape_with_arg=pre", 'H', &pre_escape),
            {&g_modifiers[1].modifier_info,   // html_escape
             &g_modifiers[2].modifier_info,   // html_escape_with_arg=snippet
             &g_modifiers[4].modifier_info,   // html_escape_with_arg=attribute
             &g_modifiers[8].modifier_info} },  // pre_escape
  /* 4 */ { ModifierInfo("html_escape_with_arg=attribute",
                         'H', &cleanse_attribute), {} },
  /* 5 */ { ModifierInfo("html_escape_with_arg=url",
                         'H', &validate_url_and_html_escape), {} },
  /* 6 */ { ModifierInfo("javascript_escape", 'j', &javascript_escape), {} },
  /* 7 */ { ModifierInfo("json_escape", 'o', &json_escape),
            {&g_modifiers[6].modifier_info} },  // javascript_escape
  /* 8 */ { ModifierInfo("pre_escape", 'p', &pre_escape),
            {&g_modifiers[1].modifier_info,     // html_escape
             &g_modifiers[2].modifier_info,     // html_escape_with_arg=snippet
             &g_modifiers[3].modifier_info,     // html_escape_with_arg=pre
             &g_modifiers[4].modifier_info} },  // html_escape_with_arg=attr...
  /* 9 */ { ModifierInfo("url_query_escape", 'u', &url_query_escape), {} },
  /* 10 */ { ModifierInfo("url_escape_with_arg=javascript", 'U',
                          &validate_url_and_javascript_escape), {} },
  /* 11 */ { ModifierInfo("url_escape_with_arg=html",
                          'U', &validate_url_and_html_escape), {} },
  /* 12 */ { ModifierInfo("url_escape_with_arg=query",
                          'U', &url_query_escape), {} },
  /* 13 */ { ModifierInfo("none", '\0', &null_modifier), {} },
};

static vector<ModifierInfo> g_extension_modifiers;
static vector<ModifierInfo> g_unknown_modifiers;

// Returns whether or not candidate can be safely (w.r.t XSS)
// used in lieu of our ModifierInfo. This is true iff:
//   1. Both have the same modifier function OR
//   2. Candidate's modifier function is in our ModifierInfo's
//      list (vector) of safe alternative modifier functions.
//
// This is used with the auto-escaping code, which automatically
// figures out which modifier to apply to a variable based on the
// variable's context (in an html "<A HREF", for instance).  Some
// built-in modifiers are considered safe alternatives from the perspective
// of preventing XSS (cross-site-scripting) attacks, in which case
// the auto-escaper should allow the choice of which to use in the
// template. This is intended only for internal use as it is dangerous
// and complicated to figure out which modifier is an XSS-safe
// replacement for a given one. Custom modifiers currently may not
// indicate safe replacements, only built-in ones may do so.
//
// Note that this function is not commutative therefore
// IsSafeXSSAlternative(a, b) may not be equal to IsSafeXSSAlternative(b, a).
bool IsSafeXSSAlternative(const ModifierInfo& our,
                          const ModifierInfo& candidate) {
  // Succeeds even for non built-in modifiers but no harm.
  if (our.modifier == candidate.modifier)
    return true;

  for (const ModifierWithAlternatives* mod_with_alts = g_modifiers;
       mod_with_alts < g_modifiers + sizeof(g_modifiers)/sizeof(*g_modifiers);
       ++mod_with_alts) {
    if (mod_with_alts->modifier_info.long_name == our.long_name)
      // We found our Modifier in the built-in array g_modifiers.
      for (int i = 0; mod_with_alts->safe_alt_mods[i] != NULL &&
               i < MAX_SAFE_ALTERNATIVES; ++i)
        if (mod_with_alts->safe_alt_mods[i]->long_name == candidate.long_name)
          // We found candidate in our Modifier's list of safe alternatives.
          return true;
  }
  // our is not built-in or candidate is not a safe replacement to our.
  return false;
}

static inline bool IsExtensionModifier(const char* long_name) {
  return memcmp(long_name, "x-", 2) == 0;
}

bool AddModifier(const char* long_name,
                 const TemplateModifier* modifier) {
  if (!IsExtensionModifier(long_name))
    return false;

  for (vector<ModifierInfo>::const_iterator mod = g_extension_modifiers.begin();
       mod != g_extension_modifiers.end();
       ++mod) {
    // Check if mod has the same name as us.  For modifiers that also take
    // values, this is everything before the =.  The only time it's ok to
    // have the same name is when we have different modval specializations:
    // "foo=bar" and "foo=baz" are both valid names.  Note "foo" and
    // "foo=bar" is not valid: foo has no modval, but "foo=bar" does.
    const size_t new_modifier_namelen = strcspn(long_name, "=");
    const size_t existing_modifier_namelen = strcspn(mod->long_name.c_str(), "=");
    if (new_modifier_namelen == existing_modifier_namelen &&
        memcmp(long_name, mod->long_name.c_str(), new_modifier_namelen) == 0) {
      if (long_name[new_modifier_namelen] == '=' &&
          mod->long_name[existing_modifier_namelen] == '=' &&
          mod->long_name != long_name) {
        // It's ok, we're different specializations!
      } else {
        // It's not ok: we have the same name and no good excuse.
        return false;
      }
    }
  }

  g_extension_modifiers.push_back(ModifierInfo(long_name, '\0', modifier));
  return true;
}

// If candidate_match is a better match for modname/modval than bestmatch,
// update bestmatch.  To be a better match, two conditions must be met:
//  1) The candidate's name must match modname
//  2) If the candidate is a specialization (that is, name is of the form
//     "foo=bar", then modval matches the specialization value).
//  3) If the candidate is not a specialization, bestmatch isn't a
//     specialization either.
// Condition (3) makes sure that if we match the ModifierInfo with name
// "foo=bar", we don't claim the ModifierInfo "foo=" is a better match.
// Recall that by definition, modval will always start with a '=' if present.
static void UpdateBestMatch(const char* modname, size_t modname_len,
                            const char* modval, size_t modval_len,
                            const ModifierInfo* candidate_match,
                            const ModifierInfo** best_match) {
  // It's easiest to handle the two case differently: (1) candidate_match
  // refers to a modifier that expects a modifier-value; (2) it doesn't.
  if (candidate_match->modval_required) {
    // To be a match, we have to fulfill three requirements: we have a
    // modval, our modname matches candidate_match's modname (either
    // shortname or longname), and our modval is consistent with the
    // value specified in the longname (whatever might follow the =).
    const char* const longname_start = candidate_match->long_name.c_str();
    const char* const equals = strchr(longname_start, '=');
    assert(equals != NULL);
    if (modval_len > 0 &&
        ((modname_len == 1 && *modname == candidate_match->short_name) ||
         (modname_len == equals - longname_start &&
          memcmp(modname, longname_start, modname_len) == 0)) &&
        ((equals[1] == '\0') ||  // name is "foo=" (not a specialization)
         (modval_len
          == longname_start + candidate_match->long_name.size() - equals &&
          memcmp(modval, equals, modval_len) == 0))) {
      // Condition (3) above is satisfied iff our longname is longer than
      // best-match's longname (so we prefer "foo=bar" to "foo=").
      if (*best_match == NULL ||
          candidate_match->long_name.size() > (*best_match)->long_name.size())
        *best_match = candidate_match;
    }
  } else {
    // In this case, to be a match: we must *not* have a modval.  Our
    // modname still must match modifno's modname (either short or long).
    if (modval_len == 0 &&
        ((modname_len == 1 && *modname == candidate_match->short_name) ||
         (modname_len == candidate_match->long_name.size() &&
          !memcmp(modname, candidate_match->long_name.data(), modname_len)))) {
      // In the no-modval case, only one match should exist.
      assert(*best_match == NULL);
      *best_match = candidate_match;
    }
  }
}

const ModifierInfo* FindModifier(const char* modname, size_t modname_len,
                                 const char* modval, size_t modval_len) {
  // More than one modifier can match, in the case of modval specializations
  // (e.g., the modifier "foo=" and "foo=bar" will both match on input of
  // modname="foo", modval="bar").  In that case, we take the ModifierInfo
  // with the longest longname, since that's the most specialized match.
  const ModifierInfo* best_match = NULL;
  if (modname_len >= 2 && IsExtensionModifier(modname)) {
    for (vector<ModifierInfo>::const_iterator mod = g_extension_modifiers.begin();
         mod != g_extension_modifiers.end();
         ++mod) {
      UpdateBestMatch(modname, modname_len, modval, modval_len,
                      &*mod, &best_match);
    }
    if (best_match != NULL)
      return best_match;

    for (vector<ModifierInfo>::const_iterator mod = g_unknown_modifiers.begin();
         mod != g_unknown_modifiers.end();
         ++mod) {
      UpdateBestMatch(modname, modname_len, modval, modval_len,
                      &*mod, &best_match);
    }
    if (best_match != NULL)
      return best_match;
    // This is the only situation where we can pass in a modifier of NULL.
    // It means "we don't know about this modifier-name."
    g_unknown_modifiers.push_back(ModifierInfo(string(modname, modname_len),
                                               '\0', NULL));
    return &g_unknown_modifiers.back();
  } else {
    for (const ModifierWithAlternatives* mod_with_alts = g_modifiers;
         mod_with_alts < g_modifiers + sizeof(g_modifiers)/sizeof(*g_modifiers);
         ++mod_with_alts) {
      UpdateBestMatch(modname, modname_len, modval, modval_len,
                      &mod_with_alts->modifier_info, &best_match);
    }
    return best_match;
  }
}

const void* ModifierData::Lookup(const char* key) const {
  DataMap::const_iterator it = map_.find(key);
  if (it != map_.end()) {
    return it->second;
  }
  return NULL;
}

void ModifierData::Insert(const char* key, const void* data) {
  map_[key] = data;
}

void ModifierData::CopyFrom(const ModifierData& other) {
  map_.insert(other.map_.begin(), other.map_.end());
}

}  // namespace template_modifiers

_END_GOOGLE_NAMESPACE_
