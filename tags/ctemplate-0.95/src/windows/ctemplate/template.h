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
// This file implements the Template class.  For information about
// how to use this class, and to write the templates it takes as input,
// see doc/howto.html

#ifndef CTEMPLATE_TEMPLATE_H_
#define CTEMPLATE_TEMPLATE_H_

#include <time.h>             // for time_t
#include <string>
#include <ctemplate/template_enums.h>

// We include this just so folks don't have to include both template.h
// and template_dictionary.h, or template_namelist.h etc, to use the
// template system; we don't actually use anything in these files ourselves.
#if 1
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_namelist.h>
#include <ctemplate/per_expand_data.h>
#else
namespace ctemplate {
class TemplateDictionaryInterface;
class PerExpandData;
}
#endif

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google_ctemplate_streamhtmlparser {
class HtmlParser;
}

namespace ctemplate {

// TemplateState of a template is:
// - TS_EMPTY before parsing is complete,
// - TS_ERROR if a syntax error was found during parsing, and
// - TS_READY if parsing has completed successfully
// - TS_SHOULD_RELOAD if marked to reload next time it is called for
// (TS_UNUSED is not used)
enum TemplateState { TS_UNUSED, TS_EMPTY, TS_ERROR, TS_READY, TS_SHOULD_RELOAD };

// To use the auto-escape mode you will need to provide the context (a.k.a type)
// of the template in GetTemplate. We provide:
// - TC_HTML:   The template contains HTML code. Need not be a complete HTML
//              page just content the browser interprets in the context of
//              HTML parsing. This should be the most common context to use.
//              This mode activates our HTML parser.
// - TC_JS:     The template contains raw javascript. If your template
//              starts with <script> tag, it is of type TC_HTML not TC_JS.
//              TC_JS is typically associated with a content-type of
//              text/javascript. This mode activates our HTML parser.
// - TC_CSS:    The template contains CSS (cascaded style-sheet). If your
//              template starts with a <style> tag, it is of type TC_HTML
//              not TC_CSS. A TC_CSS template is typically associated with a
//              text/css content-type header. Currently treated same as
//              TC_HTML but don't rely on that. We may later develop
//              CSS-specific sanitizers and parsers.
// - TC_JSON:   The template contains raw JSON. Applies javascript_escape
//              to variables. Note: javascript_escape is safer than
//              json_escape which we may want to remove.
// - TC_XML:    The template contains raw XML. Applies xml_escape to variables.
//              CAUTION: This mode is not suitable for cases where the
//              application data encapsulated in XML requires special
//              escaping, such as the case of XHTML.
//              TC_XML is typically associated with text/xml content-type.
// - TC_NONE:   Don't use, Internal use only to support passivating the
//              HTML-aware parsing and auto-escaping when template-includes
//              specify modifiers.
// - TC_MANUAL: Equivalent to not specifying auto-escaping at all.
//
// TODO(jad): Move declaration to inside Template class.
enum TemplateContext { TC_UNUSED, TC_HTML, TC_JS, TC_CSS, TC_JSON,
                       TC_XML, TC_NONE, TC_MANUAL };

// Template
//   The object which reads and parses the template file and then is used to
//   expand the parsed structure to a string.
class CTEMPLATE_DLL_DECL Template {
 public:
  // ---- CREATING A TEMPLATE OBJECT -------
  //    GetTemplate
  //    StringToTemplate
  //    StringToTemplateCache

  // GetTemplate (static Template factory method)
  //   Attempts to retrieve the template object from the cache, stored
  //   under its filename. This will fail only the first time the method
  //   is invoked for a given filename.
  //   Any object retrieved from the cache is then checked to see if
  //   its status is marked for "reload if changed." If so, ReloadIfChanged
  //   is called on the retrieved object. Then the retrieved object is
  //   returned.
  //   When it fails to retrieve one from the cache, it creates a new
  //   template object, passing the filename and 'strip' values to the
  //   constructor. (See constructor below for the meaning of the flags.)
  //   If it succeeds in creating an object, including loading and parsing
  //   the associated template file, the object is stored in the cache
  //   and then returned.
  //   If it fails in loading and parsing the template file, either
  //   because the file was not found or it contained syntax errors, then
  //   the newly created object is deleted and the method returns NULL.
  //   (NOTE: This description is much longer and less precise and probably
  //   harder to understand than the method itself. Read the code.)
  //
  //   'Strip' indicates how to handle whitespace when expanding the
  //   template.  DO_NOT_STRIP keeps the template exactly as-is.
  //   STRIP_BLANK_LINES elides all blank lines in the template.
  //   STRIP_WHITESPACE elides all blank lines, and also all whitespace
  //   at either the beginning or end of a line.  See template constructor
  //   for more details.
  static Template *GetTemplate(const std::string& filename, Strip strip);

  // Parses the string as a template file (e.g. "Hello {{WORLD}}"),
  // and returns the generated Template object, or NULL on parse
  // error.  It is the caller's responsibility to free the template
  // when done.  NOTE: since there is no 'key' associated with these
  // templates, you cannot use include them inside another template
  // (via "{{>TEMPLATE_THAT_COMES_FROM_A_STRING}}").  If you need that
  // functionality, use StringToTemplateCache.
  // Also note that Auto-Escape may be enabled by adding the
  // AUTOESCAPE pragma at the top of the string.
  static Template* StringToTemplate(const char* content, size_t content_len,
                                    Strip strip);
  static Template* StringToTemplate(const std::string& content,
                                    Strip strip) {
    return StringToTemplate(content.data(), content.length(), strip);
  }

  // Parses the string as a template file (e.g. "Hello {{WORLD}}"),
  // and inserts it into the template cache, so it can later be
  // retrieved by GetTemplate.  The user specifies a key, which is
  // passed in to GetTemplate.  (Note the user does not specify strip
  // or context; those will be specified later in the GetTemplate
  // call.)  The template system will manage memory for this template.
  // Returns true if the template was successfully parsed and
  // submitted to the template cache, or false otherwise.  In particular,
  // we return false if a string was already cached with the given key.
  // NOTE: to include this template from within another template (via
  // "{{>TEMPLATE_THAT_COMES_FROM_A_STRING}}"), the argument you pass
  // to TemplateDictionary::SetFilename() is the key you used to register
  // the string-template.
  static bool StringToTemplateCache(const std::string& key,
                                    const char* content, size_t content_len);
  static bool StringToTemplateCache(const std::string& key, const std::string& content) {
    return StringToTemplateCache(key, content.data(), content.length());
  }

  // ---- EXPANDING A TEMPLATE -------
  //    Expand
  //    ExpandWithData

  // ExpandWithData
  //   If you want any per-expand data to be used at expand time, call
  //   this routine instead of Expand.  You pass in an extra PerExpandData
  //   structure (see per_expand_data.h) which sets this data:
  //   whether or not you want the template to be annotated, and any
  //   data you want to pass in to template modifers.  If per_expand_data
  //   is NULL, this is exactly the same as Expand().
  //   The first version is the most general, followed by common-case code.
  bool ExpandWithData(ExpandEmitter* output,
                      const TemplateDictionaryInterface *dictionary,
                      PerExpandData* per_expand_data) const;

  bool ExpandWithData(std::string *output_buffer,
                      const TemplateDictionaryInterface *dictionary,
                      PerExpandData* per_expand_data) const {
    if (output_buffer == NULL)  return false;
    StringEmitter e(output_buffer);
    return ExpandWithData(&e, dictionary, per_expand_data);
  }

  // Expand
  //   Expands the template into a string using the values
  //   in the supplied dictionary.  Returns true iff all the template
  //   files load and parse correctly.
  //   The first version is the most general, followed by common-case code.
  bool Expand(ExpandEmitter* output,
              const TemplateDictionaryInterface *dictionary) const {
    return ExpandWithData(output, dictionary, NULL);
  }
  bool Expand(std::string *output_buffer,
              const TemplateDictionaryInterface *dictionary) const {
    return ExpandWithData(output_buffer, dictionary, NULL);
  }


  // ---- OTHER TEMPLATE OPERATIONS -------

  // Template destructor
  virtual ~Template();

  // SetTemplateRootDirectory
  //   Sets the root directory for all templates used by the program.
  //   After calling this method, the filename passed to GetTemplate
  //   may be a relative pathname (no leading '/'), in which case
  //   this root-directory is prepended to the filename.
  static bool SetTemplateRootDirectory(const std::string& directory);

  // ClearCache
  //   Deletes all the template objects in the cache and all raw
  //   contents cached from StringToTemplateCache. This should only
  //   be done once, just before exiting the program and after all
  //   template expansions are completed. (If you want to refresh the
  //   cache, the correct method to use is ReloadAllIfChanged, not
  //   this one.) Note: this method is not necessary unless you are
  //   testing for memory leaks. Calling this before exiting the
  //   program will prevent unnecessary reporting in that case.
  static void ClearCache();

  // RemoveStringFromTemplateCache
  //   The opposite of StringToTemplateCache, this removes the string
  //   with the given key from the cache.  Note it can only be used
  //   on strings added to the cache via StringToTemplateCache; to
  //   remove disk-based entries, you must delete the file on disk
  //   and then call ReloadIfChanged().
  static void RemoveStringFromTemplateCache(const std::string& key);

  // ReloadAllIfChanged
  //   Marks each template object in the cache to check to see if
  //   its template file has changed the next time it is invoked
  //   via GetTemplate. If it finds the file has changed, it
  //   then reloads and parses the file before being returned by
  //   GetTemplate.
  static void ReloadAllIfChanged();

  // ReloadIfChanged
  //   Reloads the file from the filesystem iff its mtime is different
  //   now from what it was last time the file was reloaded.  Note a
  //   file is always "reloaded" the first time this is called.  If
  //   the file is in fact reloaded, then the contents of the file are
  //   parsed into the template node parse tree by calling BuildTree
  //   After this call, the state of the Template will be either
  //   TS_READY or TS_ERROR.  Return value is true iff we reloaded
  //   because the content changed and could be parsed with no errors.
  bool ReloadIfChanged();

  // Dump
  //   Dumps the parsed structure of the template for debugging assistance.
  //   It goes to stdout.
  void Dump(const char *filename) const;

  // DumpToString
  //   Same as Dump above except it dumps to the given string instead.
  //   Format is identical to Dump.
  void DumpToString(const char *filename, std::string *out) const;

  // state
  //   Retrieves the state of the template (see TemplateState above).
  TemplateState state() const;

  // template_file
  //   Returns the name of the template file used to build this template.
  const char *template_file() const;

  // template_root_directory
  //   Returns the stored template root directory name
  static std::string template_root_directory();

  // WriteHeaderEntries (used by make_tpl_varnames_h.cc)
  void WriteHeaderEntries(std::string *outstring) const;

 protected:
  friend class SectionTemplateNode;  // for access to set_state(), ParseState
  friend class TemplateTemplateNode; // for recursive call to Expand()

  // Performs the actual work for both factory methods
  // GetTemplate and GetTemplateWithAutoEscaping.
  // See description of member variable selective_autoescape_ for
  // information on Selective Auto-Escape.
  static Template *GetTemplateCommon(const std::string& filename, Strip strip,
                                     TemplateContext context,
                                     bool selective_autoescape);

  // AssureGlobalsInitialized
  //   Initializes the global (static) variables the first time it is
  //   called. After that it simply checks one of the
  //   initialized pointers and finds it has already been called.
  static void AssureGlobalsInitialized();

  // Template constructor
  //   Reads the template file and parses it into a parse tree of TemplateNodes
  //   by calling the method ReloadIfChanged
  //   The top node is a section node with the arbitrary name "__{{MAIN}}__"
  //   'Strip' indicates how to handle whitespace when expanding the
  //   template.  DO_NOT_STRIP keeps the template exactly as-is.
  //   STRIP_BLANK_LINES elides all blank lines in the template.
  //   STRIP_WHITESPACE elides all blank lines, and also all whitespace
  //   at either the beginning or end of a line.  It also removes
  //   any linefeed (possibly following whitespace) that follows a closing
  //   '}}' of any kind of template marker EXCEPT a template variable.
  //   This means a linefeed may be removed anywhere by simply placing
  //   a comment marker as the last element on the line.
  //   These two options allow the template to include whitespace for
  //   readability without adding to the expanded output.
  //   If context is not TC_MANUAL we enable auto-escaping
  //   for that template and any template it includes.
  Template(const std::string& filename, Strip strip, TemplateContext context,
           bool selective_autoescape);

  // MaybeInitHtmlParser
  //   In TemplateContexts where the HTML parser is needed, we initialize it
  //   in the appropriate mode. Also we do a sanity check (cannot fail) on the
  //   template filename. This function is called at most once for a Template.
  //   In_tag is only meaningful for TC_HTML: It is true for templates that
  //   start inside an HTML tag and hence are expected to contain HTML attribute
  //   name/value pairs only. It is false for standard HTML templates.
  void MaybeInitHtmlParser(bool in_tag);

  // BuildTree
  //   Parses the contents of the file (retrieved via ReloadIfChanged)
  //   and stores the resulting parse structure in tree_.  Returns true
  //   iff the tree-builder encountered no errors.  Note: takes
  //   ownership of input_buffer, and will delete it.  It should have
  //   been created via new[].
  bool BuildTree(const char *input_buffer, const char* input_buffer_end);

  // Internal version of ReloadIfChanged, used when the function already
  // has a write-lock on mutex_.
  bool ReloadIfChangedLocked();

  // set_state
  //   Sets the state of the template.  Used during BuildTree().
  void set_state(TemplateState new_state);

  // StripBuffer
  //   Modifies buffer in-place based on the strip_ mode, to remove
  //   extra whitespace.  May delete[] the input buffer and replace
  //   it with a new buffer.  Used by ReloadIfChanged().
  void StripBuffer(char **buffer, size_t* len);

  // The file we read the template from
  const std::string filename_;
  time_t filename_mtime_;   // lastmod time for filename last time we loaded it

  // What to do with whitespace at template-expand time
  Strip strip_;

  // Keeps track of where we are in reloading, or if there was an error loading
  TemplateState state_;

  // The current template-contents, as read from the file
  const char* template_text_;
  int template_text_len_;

  // The current parsed template structure.  Has pointers into template_text_.
  class SectionTemplateNode *tree_;       // defined in template.cc

  // Template markers have the form {{VARIABLE}}, etc.  These constants
  // define the {{ and }} that delimit template markers.
  struct MarkerDelimiters {
    const char* start_marker;
    size_t start_marker_len;
    const char* end_marker;
    size_t end_marker_len;

    MarkerDelimiters() {
      start_marker = "{{";    // The default start-marker
      start_marker_len = strlen(start_marker);
      end_marker = "}}";
      end_marker_len = strlen(end_marker);
    }
  };

  // The current parsing state.  Used in BuildTree() and subroutines
  struct ParseState {
    const char* bufstart;
    const char* bufend;
    enum { PS_UNUSED, GETTING_TEXT, GETTING_NAME } phase;
    MarkerDelimiters current_delimiters;
    ParseState()
        : bufstart(NULL), bufend(NULL), phase(PS_UNUSED), current_delimiters()
    {}
  };
  ParseState parse_state_;

  // The mutex object used during ReloadIfChanged to prevent the same
  // object from reloading the template file in parallel by different
  // threads.
  mutable class Mutex* mutex_;

  // The root directory for all templates. Defaults to "./" until
  // SetTemplateRootDirectory changes it
  static std::string *template_root_directory_;

  // TC_MANUAL indicates the template is not in auto-escaping mode.
  // other values imply auto-escape mode is enabled.
  TemplateContext initial_context_;
  // Non-null if template was initialized in auto_escape mode.
  google_ctemplate_streamhtmlparser::HtmlParser *htmlparser_;

  // We now support two types of Auto-Escape:
  // 1. Full-on Auto-Escape where a template and all its included templates
  //    get auto-escaped. This is the case when the top-level template is
  //    obtained via GetTemplateWithAutoEscaping, or for string Templates
  //    via StringToTemplate with an auto-escape template context.
  // 2. Selective Auto-Escape where only templates which contain the AUTOESCAPE
  //    pragma {{%AUTOESCAPE ...}} are auto-escaped. The top-level template
  //    must be obtained via GetTemplate, or for string Templates, via
  //    StringToTemplate with TC_MANUAL template context.
  // In the first case, selective_autoescape_ is false, in the second case
  // it is true. Note: It is propagated as-is from parent to included template.
  const bool selective_autoescape_;

 private:
  // These are helper routines to StripFile.  I would make them static
  // inside template.cc, but they use the MarerDelimiters struct.
  static bool ParseDelimiters(const char* text, size_t textlen,
                              MarkerDelimiters* delim);
  static bool IsBlankOrOnlyHasOneRemovableMarker(const char** line, size_t* len,
                                                 const MarkerDelimiters& delim);
  static size_t InsertLine(const char *line, size_t len, Strip strip,
                           const MarkerDelimiters& delim, char* buffer);

  // Can't invoke copy constructor or assignment operator
  Template(const Template&);
  void operator=(const Template &);
};

}

#endif // CTEMPLATE_TEMPLATE_H_
