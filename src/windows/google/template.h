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

#ifndef _TEMPLATE_H
#define _TEMPLATE_H

#include <time.h>             // for time_t
#include <string>
#include <google/template_enums.h>

// We include this just so folks don't have to include both template.h
// and template_dictionary.h, or template_namelist.h etc, to use the
// template system; we don't actually use anything in these files ourselves.
#if 1
#include <google/template_dictionary.h>
#include <google/template_namelist.h>
#else
namespace google { class TemplateDictionary; }
#endif

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google {

// TemplateState of a template is:
// - TS_EMPTY before parsing is complete,
// - TS_ERROR if a syntax error was found during parsing, and
// - TS_READY if parsing has completed successfully
// - TS_SHOULD_RELOAD if marked to reload next time it is called for
// (TS_UNUSED is not used)
enum TemplateState { TS_UNUSED, TS_EMPTY, TS_ERROR, TS_READY, TS_SHOULD_RELOAD };


// Template
//   The object which reads and parses the template file and then is used to
//   expand the parsed structure to a string.
class CTEMPLATE_DLL_DECL Template {
 public:
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

  // Template destructor
  virtual ~Template();

  // SetTemplateRootDirectory
  //   Sets the root directory for all templates used by the program.
  //   After calling this method, the filename passed to GetTemplate
  //   may be a relative pathname (no leading '/'), in which case
  //   this root-directory is prepended to the filename.
  static bool SetTemplateRootDirectory(const std::string& directory);

  // ReloadAllIfChanged
  //   Marks each template object in the cache to check to see if
  //   its template file has changed the next time it is invoked
  //   via GetTemplate. If it finds the file has changed, it
  //   then reloads and parses the file before being returned by
  //   GetTemplate.
  static void ReloadAllIfChanged();

  // ClearCache
  //   Deletes all the template objects in the cache. This should only
  //   be done once, just before exiting the program and after all
  //   template expansions are completed. (If you want to refresh the
  //   cache, the correct method to use is ReloadAllIfChanged, not
  //   this one.) Note: this method is not necessary unless you are
  //   testing for memory leaks. Calling this before exiting the
  //   program will prevent unnecessary reporting in that case.
  static void ClearCache();

  // Expand
  //   Expands the template into a string using the values
  //   in the supplied dictionary.  Returns true iff all the template
  //   files load and parse correctly.
  bool Expand(std::string *output_buffer,
              const TemplateDictionary *dictionary) const;

  // Dump
  //   Dumps the parsed structure to stdout for debugging assistance
  void Dump(const char *filename) const;

  // state
  //   Retrieves the state of the template (see TemplateState above)
  TemplateState state() const;

  // template_file
  //   Returns the name of the template file used to build this template
  const char *template_file() const;

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

  // template_root_directory
  //   Returns the stored template root directory name
  static std::string template_root_directory();

  // WriteHeaderEntries
  void WriteHeaderEntries(std::string *outstring) const;

 protected:
  friend class SectionTemplateNode;  // for access to set_state(), ParseState
  friend class TemplateTemplateNode; // for recursive call to Expand()

  // AssureGlobalsInitialized
  //   Initializes the global (static) variables the first time it is
  //   called. After that it simply checks one of the
  //   initialized pointers and finds it has already been called.
  static void AssureGlobalsInitialized();

  // Template constructor
  //   Reads the template file and parses it into a parse tree of TemplateNodes
  //   by calling the method ReloadIfChanged
  //   The top node is a section node with the arbitrary name "__MAIN__"
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
  Template(const std::string& filename, Strip strip);

  // BuildTree
  //   Parses the contents of the file (retrieved via ReloadIfChanged)
  //   and stores the resulting parse structure in tree_.  Returns true
  //   iff the tree-builder encountered no errors.
  bool BuildTree(const char *input_buffer, const char* input_buffer_end);

  // Internal version of Expand, used for recursive calls.
  // force_annotate_dict is a dictionary that can be used to force
  // annotations: even if dictionary->ShouldAnnotateOutput() is false,
  // if force_annotate_dict->ShouldAnnotateOutput() is true, we annotate.
  bool Expand(class ExpandEmitter *expand_emitter,
              const TemplateDictionary *dictionary,
              const TemplateDictionary *force_annotate_dict) const;

  // Internal version of ReloadIfChanged, used when the function already
  // has a write-lock on mutex_.
  bool ReloadIfChangedLocked();

  // set_state
  //   Sets the state of the template.  Used during BuildTree().
  void set_state(TemplateState new_state);

  // InsertLine and InsertFile
  //   Writes each line to buffer, and returns number of bytes written.
  //   A line is a string of characters that ends with a \n (or \0).
  //   (We need to be line-based because Strip works line-by-line).
  //   buffer must be big enough to hold the output.  It's guaranteed
  //   that the output size is no bigger than the input size.
  //   Used by ReloadIfChanged()
  int InsertLine(const char *line, int len, char* buffer);
  int InsertFile(const char *file, size_t len, char* buffer);


  // The file we read the template from
  std::string filename_;    // can't be const because of TemplateFromString :-(
  time_t filename_mtime_;   // lastmod time for filename last time we loaded it

  // What to do with whitespace at template-expand time
  const Strip strip_;

  // Keeps track of where we are in reloading, or if there was an error loading
  TemplateState state_;

  // The current template-contents, as read from the file
  const char* template_text_;
  int template_text_len_;

  // The current parsed template structure.  Has pointers into template_text_.
  class SectionTemplateNode *tree_;       // defined in template.cc

  // The current parsing state.  Used in BuildTree() and subroutines
  struct ParseState {
    const char* bufstart;
    const char* bufend;
    enum { PS_UNUSED, GETTING_TEXT, GETTING_NAME } phase;
    ParseState() : bufstart(NULL), bufend(NULL), phase(PS_UNUSED) {}
  };
  ParseState parse_state_;

  // The mutex object used during ReloadIfChanged to prevent the same
  // object from reloading the template file in parallel by different
  // threads.
  mutable class Mutex* mutex_;

  // The root directory for all templates. Defaults to "./" until
  // SetTemplateRootDirectory changes it
  static std::string *template_root_directory_;

 private:
  // Can't invoke copy constructor or assignment operator
  Template(const Template&);
  void operator=(const Template &);
};

}

#endif // _TEMPLATE_H
