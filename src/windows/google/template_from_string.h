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
// Author:   Frank H. Jernigan
//
// The TemplateFromString class implements a template that is instantiated
// from a string rather than from a file. Like its parent, Template, instances
// are retrieved via its GetTemplate static method, rather than new'ing
// them directly. This GetTemplate has a different parameter list, taking
// both a logical name (template_name) and the text of the template itself
// as a string (template_text). The logical name is used to cache the
// parse template structure. Therefore, it is important to always pass any
// given name with the same template text string, or at least realize that
// the template text string will be ignored on second and subsequent calls
// given the same name.

// Unlike the file-based Template class, there is no way to "Reload" a
// template once it has been retrieved (nor should there be any need
// to do so, as there is no file that might have changed after the
// initial "loading"). Therefore, the methods Reload, ReloadIfChanged,
// and the parent's GetTemplate have all been made private in this
// derived class and given definitions which will produce fatal errors
// with a message to the developer that these are invalid calls.  If
// you call ReloadAllIfChanged() and it affects a string template, it
// will mark the template to be reloaded (ReloadAllIfChanged() always
// does this; it never actually does any reloading itself), but that
// mark will be subsequently ignored.

#ifndef TEMPLATE_FROM_STRING_H
#define TEMPLATE_FROM_STRING_H

#include <string>
#include <google/template.h>

// NOTE: if you are statically linking the template library into your binary
// (rather than using the template .dll), set '/D CTEMPLATE_DLL_DECL='
// as a compiler flag in your project file to turn off the dllimports.
#ifndef CTEMPLATE_DLL_DECL
# define CTEMPLATE_DLL_DECL  __declspec(dllimport)
#endif

namespace google {

class CTEMPLATE_DLL_DECL TemplateFromString : public Template {

 public:
  /* GetTemplate
     Parameters:
       (NOTE: The parameter list is not the same as Template::GetTemplate.)
       cache_key - the cache string used for the template text.
       template_text - the text of the template containing the template
                       code with markers, the very same language that
                       would be stored in a file for the parent class
       strip - same as the parent class
     Description:
       Attempts to find an instance of the class with the given cache_key
       stored in the cache. If it finds one, it returns it, ignoring the
       template_text passed to the method.
       If it does not find one, it creates a new instance of the class,
       stores it in the cache under the template_name, and returns it.
       Note: since cache lookup is by key, you can't have two instances
       with the same key but different text, and expect it to work.
       However, if cache_key is the empty string, we ignore the cache,
       and always create a new instance of the class (without storing it
       in the cache).  In this case *only*, you're responsible for
       deleting the returned TemplateFromString object when done with it.
  */
  static TemplateFromString *GetTemplate(const std::string& cache_key,
                                         const std::string& template_text,
                                         Strip strip);

 private:
  /* This templates constuctor is private just like the parent's is.
     New ones are acquired through TemplateFromString::GetTemplate */
  TemplateFromString(const std::string& cache_key,
                     const std::string& template_text,
                     Strip strip);

  /* The remaining methods, which are valid on the parent, cannot be
     called on this kind of template instance */

  virtual Template *GetTemplate(const std::string& filename, Strip strip);

  virtual void Reload();
  virtual bool ReloadIfChanged();
};

}

#endif //  TEMPLATE_FROM_STRING_H
