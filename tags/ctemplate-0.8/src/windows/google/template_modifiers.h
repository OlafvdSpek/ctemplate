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
// We allow template variables to have modifiers, each possibly with a
// value associated with it.  Format is
//    {{VARNAME:modname[=modifier-value]:modname[=modifier-value]:...}}
// Modname refers to a functor that takes the variable's value
// and modifier-value (empty-string if no modifier-value was
// specified), and returns a munged value.  Modifiers are applied
// left-to-right.  We define the legal modnames here, and the
// functors they refer to.
//
// Modifiers have a long-name, an optional short-name (one char;
// may be \0 if you don't want a shortname), and a functor that's
// applied to the variable.
//
// In addition to the list of modifiers hard-coded in the source code
// here, it is possible to dynamicly register modifiers using a long
// name starting with "x-".
//
// In addition to using a modifier within a template, you can also
// pass a modifier object to TemplateDictionary::SetEscapedValue() and
// similar methods.  The built-in modifier objects are defined in this
// file (some are also exported in template_dictionary.h for backwards
// compatibility).  If you wish to define your own modifier class, in
// your own source code, just subclass TemplateModifier -- see
// template_modifiers.cc for details of how to do that.
//
// Adding a new built-in modifier, to this file, takes several steps,
// both in this .h file and in the corresponding .cc file:
// 1) .h file: Define a struct for the modifier.  It must subclass
//     TemplateModifier.
// 2) .h file: declare a variable that's an instance of the struct.
//    This is used for people who want to modify the string themselves,
//    via TemplateDictionary::SetEscapedValue.
// 5) .cc file: define the new modifier's Modify method.
// 6) .cc file: give storage for the variable declared in the .h file (in 2).
// 7) .cc file: add the modifier to the g_modifiers array.

#ifndef TEMPLATE_TEMPLATE_MODIFIERS_H__
#define TEMPLATE_TEMPLATE_MODIFIERS_H__

#include <sys/types.h>   // for size_t
#include <hash_map>
#include <string>
#include <google/template_emitter.h>   // so we can inline operator()

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google {

class Template;

namespace template_modifiers {

class ModifierData;

#define MODIFY_SIGNATURE_                                                    \
 public:                                                                     \
  virtual void Modify(const char* in, size_t inlen,                          \
                      const ModifierData*, ExpandEmitter* outbuf,            \
                      const std::string& arg) const

// If you wish to write your own modifier, it should subclass this
// method.  Your subclass should only define Modify(); for efficiency,
// we do not make operator() virtual.
class CTEMPLATE_DLL_DECL TemplateModifier {
 public:
  // This function takes a string as input, a char*/size_t pair, and
  // appends the modified version to the end of outbuf.  "arg" is
  // used for modifiers that take a modifier-value argument; for
  // modifiers that take no argument, arg will always be "".
  virtual void Modify(const char* in, size_t inlen,
                      const ModifierData* per_expand_data,
                      ExpandEmitter* outbuf,
                      const std::string& arg) const = 0;

  // We support both modifiers that take an argument, and those that don't.
  // We also support passing in a string, or a char*/int pair.
  std::string operator()(const char* in, size_t inlen, const std::string& arg="") const {
    std::string out;
    // we'll reserve some space to account for minimal escaping: say 12%
    out.reserve(inlen + inlen/8 + 16);
    StringEmitter outbuf(&out);
    Modify(in, inlen, NULL, &outbuf, arg);
    return out;
  }
  std::string operator()(const std::string& in, const std::string& arg="") const {
    return operator()(in.data(), in.size(), arg);
  }

  virtual ~TemplateModifier();   // always need a virtual destructor!
};


// Returns the input verbatim (for testing)
class CTEMPLATE_DLL_DECL NullModifier : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL NullModifier null_modifier;

// Escapes < > " ' & <non-space whitespace> to &lt; &gt; &quot;
// &#39; &amp; <space>
class CTEMPLATE_DLL_DECL HtmlEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL HtmlEscape html_escape;

// Same as HtmlEscape but leaves all whitespace alone. Eg. for <pre>..</pre>
class CTEMPLATE_DLL_DECL PreEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL PreEscape pre_escape;

// Like HtmlEscape but allows HTML entities, <br> tags, <wbr> tags, and
// matched <b> and </b> tags.
class CTEMPLATE_DLL_DECL SnippetEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL SnippetEscape snippet_escape;

// Replaces characters not safe for an unquoted attribute with underscore.
// Safe characters are alphanumeric, underscore, dash, period, and colon.
class CTEMPLATE_DLL_DECL CleanseAttribute : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL CleanseAttribute cleanse_attribute;

// Removes characters not safe for a CSS value. Safe characters are
// alphanumeric, space, underscore, period, coma, exclamation mark,
// pound, percent, and dash.
class CTEMPLATE_DLL_DECL CleanseCss : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL CleanseCss cleanse_css;

// Checks that a url is either an absolute http(s) URL or a relative
// url that doesn't have a protocol hidden in it (ie [foo.html] is
// fine, but not [javascript:foo]) and then performs another type of
// escaping. Returns the url escaped with the specified modifier if
// good, otherwise returns "#".
class CTEMPLATE_DLL_DECL ValidateUrl : public TemplateModifier {
 public:
  explicit ValidateUrl(const TemplateModifier& chained_modifier)
      : chained_modifier_(chained_modifier) { }
  MODIFY_SIGNATURE_;
 private:
  const TemplateModifier& chained_modifier_;
};
extern CTEMPLATE_DLL_DECL ValidateUrl validate_url_and_html_escape;
extern CTEMPLATE_DLL_DECL ValidateUrl validate_url_and_javascript_escape;

// Escapes &nbsp; to &#160;
// TODO(csilvers): have this do something more useful, once all callers have
//                 been fixed.  Dunno what 'more useful' might be, yet.
class CTEMPLATE_DLL_DECL XmlEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL XmlEscape xml_escape;

// Escapes " ' \ <CR> <LF> <BS> to \" \' \\ \r \n \b
class CTEMPLATE_DLL_DECL JavascriptEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL JavascriptEscape javascript_escape;

// Escapes characters not in [0-9a-zA-Z.,_:*/~!()-] as %-prefixed hex.
// Space is encoded as a +.
class CTEMPLATE_DLL_DECL UrlQueryEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL UrlQueryEscape url_query_escape;

// Escapes " \ / <FF> <CR> <LF> <BS> <TAB> to \" \\ \/ \f \r \n \b \t
class CTEMPLATE_DLL_DECL JsonEscape : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL JsonEscape json_escape;

// A dispatch routine that calls pre_escape, snippet_escape,
// cleanse_attribute, or validate_url, depending on the value of the arg.
class CTEMPLATE_DLL_DECL HtmlEscapeWithArg : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL HtmlEscapeWithArg html_escape_with_arg;

// A similar dispatch routine for URLS.
// Calls validate_url_and_javascript_escape, validate_url_and_html_escape,
// or url_query_escape depending on the value of the argument..
class CTEMPLATE_DLL_DECL UrlEscapeWithArg : public TemplateModifier { MODIFY_SIGNATURE_; };
extern CTEMPLATE_DLL_DECL UrlEscapeWithArg url_escape_with_arg;


#undef MODIFY_SIGNATURE_

// -----------------------------------------------------------------
// These are used by template.cc and when registering new modifiers.
// They are not intended for any other users.

// Does this modifier take an argument?  Note we do not have
// MODVAL_OPTIONAL: we prefer the clarity of an arg either always
// taking an argument, or never (ie, no "default arguments").
// MODVAL_UNKNOWN is only for internal use and cannot be used when
// registering a new modifier.
enum ModvalStatus { MODVAL_FORBIDDEN, MODVAL_REQUIRED, MODVAL_UNKNOWN };

// TODO(csilvers): collapse this into the TemplateModifier class?
struct ModifierInfo {
  ModifierInfo(std::string ln, char sn, ModvalStatus vs,
               const TemplateModifier *m)
      : long_name(ln), short_name(sn),
        value_status(vs), modifier(m) { }
  std::string long_name;
  char short_name;
  ModvalStatus value_status;
  const TemplateModifier* modifier;
};

// Registers a new template modifier.
// long_name must start with "x-".
extern CTEMPLATE_DLL_DECL bool AddModifier(const char* long_name,
                         ModvalStatus value_status,
                         const TemplateModifier* modifier);

// modname is the name of the modifier (shortname or longname).
// Returns a pointer into g_modifiers, or NULL if not found.
extern CTEMPLATE_DLL_DECL const ModifierInfo* FindModifier(const char* modname,
                                         size_t modname_len);

// This class holds per-expand data which is available to
// custom modifiers.
class CTEMPLATE_DLL_DECL ModifierData  {
 public:
  ModifierData() { }

  // Retrieve data specific to this Expand call. Returns NULL if key
  // is not found.
  // This should only be used by modifiers.
  const void* Lookup(const char* key) const;

  // Same as Lookup, but casts the result to a c string.
  const char* LookupAsString(const char* key) const {
    return static_cast<const char*>(Lookup(key));
  }

  // Store data for a later call to expand. Call with value set to
  // NULL to clear any value previously set.  Caller is responsible
  // for ensuring key and value point to valid data for the lifetime
  // of 'this'.  This should only be used by TemplateDictionary.
  void Insert(const char* key, const void* value);

  // Copy another ModifierData.  Only used by TemplateDictionary.
  void CopyFrom(const ModifierData& other);

 private:
#ifdef WIN32
  struct DataHash {
    size_t operator()(const char* s1) const {
      return stdext::hash_compare<const char*>()(s1);
    }
    bool operator()(const char* s1, const char* s2) const {  // less-than
      return (s2 == 0 ? false : s1 == 0 ? true : strcmp(s1, s2) < 0);
    }
    // These two public members are required by msvc.  4 and 8 are defaults.
    static const size_t bucket_size = 4;
    static const size_t min_buckets = 8;
  };

  typedef stdext::hash_map<const char*, const void*, DataHash> DataMap;
#else
  struct DataEq {
    bool operator()(const char* s1, const char* s2) const {
      return ((s1 == 0 && s2 == 0) ||
              (s1 && s2 && *s1 == *s2 && strcmp(s1, s2) == 0));
    }
  };
  typedef stdext::hash_map<const char*, const void*, stdext::hash_compare<const char*>, DataEq>
    DataMap;
#endif
  DataMap map_;

  ModifierData(const ModifierData&);    // disallow evil copy constructor
  void operator=(const ModifierData&);  // disallow evil operator=
};

}  // namespace template_modifiers

}

#endif  // TEMPLATE_TEMPLATE_MODIFIERS_H__
