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

#include "config.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <string>
#include <google/template_modifiers.h>

using std::string;


// A most-efficient way to append a string literal to the var named 'out'.
// The ""s ensure literal is actually a string literal
#define APPEND(literal)  out->Emit("" literal "", sizeof(literal)-1)

_START_GOOGLE_NAMESPACE_

namespace template_modifiers {

void NullModifier::Modify(const char* in, int inlen,
                          ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  out->Emit(in, inlen);
}
NullModifier null_modifier;

void HtmlEscape::Modify(const char* in, int inlen,
                        ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  for (int i = 0; i < inlen; ++i) {
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

void PreEscape::Modify(const char* in, int inlen,
                       ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  for (int i = 0; i < inlen; ++i) {
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

void SnippetEscape::Modify(const char* in, int inlen,
                           ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
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

void CleanseAttribute::Modify(const char* in, int inlen,
                              ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  for (int i = 0; i < inlen; ++i) {
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

void ValidateUrl::Modify(const char* in, int inlen,
                         ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  const char* slashpos = (char*)memchr(in, '/', inlen);
  if (slashpos == NULL)
    slashpos = in + inlen;
  void* colonpos = memchr(in, ':', slashpos - in);
  if (colonpos != NULL) {   // colon before first slash, could be a protocol
    if (inlen > sizeof("http://")-1 &&
        memcmp(in, "http://", sizeof("http://")-1) == 0) {
      // We're ok, it's an http protocol
    } else if (inlen > sizeof("https://")-1 &&
               memcmp(in, "https://", sizeof("https://")-1) == 0) {
      // https is ok as well
    } else {
      // It's a bad protocol, so return something safe
      out->Emit("#");
      return;
    }
  }
  // If we get here, it's a valid url, so just escape it
  html_escape.Modify(in, inlen, out, "");
}
ValidateUrl validate_url_and_html_escape;

void XmlEscape::Modify(const char* in, int inlen,
                       ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  const char* end = in + inlen;
  const char* pos = in;
  for (const char* next_amp = static_cast<char*>(memchr(in, '&', inlen));
       next_amp;  next_amp = static_cast<char*>(memchr(pos, '&', end-pos))) {
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

void JavascriptEscape::Modify(const char* in, int inlen,
                              ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  for (int i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '"': APPEND("\\\""); break;
      case '\'': APPEND("\\'"); break;
      case '\\': APPEND("\\\\"); break;
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

void UrlQueryEscape::Modify(const char* in, int inlen,
                            ExpandEmitter* out, const string& arg) const {
  // Everything not matching [0-9a-zA-Z.,_*/~!()-] is escaped.
  static unsigned long _safe_characters[8] = {
    0x00000000L, 0x03fff702L, 0x87fffffeL, 0x47fffffeL,
    0x00000000L, 0x00000000L, 0x00000000L, 0x00000000L
  };

  assert(arg.empty());  // We're a no-arg modifier
  for (int i = 0; i < inlen; ++i) {
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

void JsonEscape::Modify(const char* in, int inlen,
                        ExpandEmitter* out, const string& arg) const {
  assert(arg.empty());  // We're a no-arg modifier
  for (int i = 0; i < inlen; ++i) {
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

void HtmlEscapeWithArg::Modify(const char* in, int inlen,
                               ExpandEmitter* out, const string& arg) const {
  if (!arg.empty()) {
    switch (arg[1]) {
      case 's':
        return snippet_escape.Modify(in, inlen, out, "");
      case 'p':
        return pre_escape.Modify(in, inlen, out, "");
      case 'a':
        return cleanse_attribute.Modify(in, inlen, out, "");
      case 'u':
        return validate_url_and_html_escape.Modify(in, inlen, out, "");
      default:
        break;
    }
  }
  return html_escape.Modify(in, inlen, out, "");
}
HtmlEscapeWithArg html_escape_with_arg;


// Use the empty string if you want a modifier not to have a long-name.
// Use '\0' if you want a modifier not to have a short-name.
// Note: not all modifiers are in this array:
// 1) SnippetEscape: use html_escape_with_arg=snippet to get this
// 2) CleanseAttribute: use html_escape_with_arg=attribute to get this
// 3) ValidateUrl: use html_escape_with_arg=url to get this
static ModifierInfo g_modifiers[] = {
  { "html_escape", 'h', MODVAL_FORBIDDEN, &html_escape },
  { "html_escape_with_arg", 'H', MODVAL_REQUIRED, &html_escape_with_arg },
  { "javascript_escape", 'j', MODVAL_FORBIDDEN, &javascript_escape },
  { "json_escape", 'o', MODVAL_FORBIDDEN, &json_escape },
  { "pre_escape", 'p', MODVAL_FORBIDDEN, &pre_escape },
  { "url_query_escape", 'u', MODVAL_FORBIDDEN, &url_query_escape },
  { "none", '\0', MODVAL_FORBIDDEN, &null_modifier },
};

const ModifierInfo* FindModifier(const char* modname, int modname_len) {
  for (ModifierInfo* mod = g_modifiers;
       mod < g_modifiers + sizeof(g_modifiers)/sizeof(*g_modifiers);
       ++mod) {
    if ((modname_len == 1 && *modname == mod->short_name) ||
        (modname_len == strlen(mod->long_name) &&
         !memcmp(modname, mod->long_name, modname_len))) {
      return mod;
    }
  }
  return NULL;
}

// Find the long-name associated with a given modifier, or NULL if not found.
const char* FindModifierName(const TemplateModifier* modifier) {
  for (ModifierInfo* mod = g_modifiers;
       mod < g_modifiers + sizeof(g_modifiers)/sizeof(*g_modifiers);
       ++mod) {
    if (mod->modifier == modifier)
      return mod->long_name;
  }
  return NULL;
}

}  // namespace template_modifiers

_END_GOOGLE_NAMESPACE_
