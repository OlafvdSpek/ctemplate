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
// Author: Jad Boutros
// Heavily inspired from make_tpl_varnames_h.cc
//
// A utility for evaluating the changes to template modifiers
// when you switch from Standard mode to Auto Escape mode.
//
// How it works:
//   . We initialize the same template file twice, once in standard mode
//     and once in Auto Escape mode with the given TemplateContext.
//
//   . We invoke DumpToString on both and compare the modifiers for each
//     variable. When the variable had an in-template modifier *and* the
//     auto-escape mode determined a different one, we print a line
//     with the variable name, the initial modifier and the auto-escape one.
//     You can use --brief to get only one line per file (for >0 diffs).
//     NOTE: If your template file has only variables without any modifiers,
//     (say from the old Google Templates) we don't print anything.
//
//   . We accept some command-line flags, in particular --template_context
//     to set a TemplateContext other than TC_HTML (default),
//     --template_dir to set a template root directory other than cwd
//     and --dump_templates to print the full DumpToString under the
//     auto-escape mode.
//
// Usage Example:
// > $ cd ~/src/google3
// > $ bin/template/diff_tpl_auto_escape TC_HTML some_template_file.tpl
//
// TODO(jad): This script is currently broken. Fix me.

// Note, we keep the google-infrastructure use to a minimum here,
// in order to ease porting to opensource.
//
// Exit code is the number of templates we were unable to parse.

#include "config.h"
// This is for windows.  Even though we #include config.h, just like
// the files used to compile the dll, we are actually a *client* of
// the dll, so we don't get to decl anything.
#undef CTEMPLATE_DLL_DECL

#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdarg.h>
#ifndef _WIN32
#include <getopt.h>
#endif
#include <string.h>
#include <string>
#include <google/template.h>
#include <google/template_pathops.h>

using std::string;
using std::vector;
using GOOGLE_NAMESPACE::Template;
using GOOGLE_NAMESPACE::TemplateContext;
using GOOGLE_NAMESPACE::TC_HTML;
using GOOGLE_NAMESPACE::TC_JS;
using GOOGLE_NAMESPACE::STRIP_WHITESPACE;
namespace ctemplate = GOOGLE_NAMESPACE::ctemplate;   // an interior namespace

enum {LOG_VERBOSE, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL};

// A variable name and optional modifiers.
// For example: in {{NAME:j:x-bla}}
// variable_name is "NAME" and modifiers is "j:x-bla".
struct VariableAndMod {
  VariableAndMod(string name, string mods)
      : variable_name(name), modifiers(mods) { }
  string variable_name;
  string modifiers;
};
typedef vector<VariableAndMod> VariableAndMods;

static string FLAG_template_dir(ctemplate::kCWD);   // "./"
static bool FLAG_dump_templates = false;           // cmd-line arg -d
static string FLAG_template_context("TC_HTML");    // cmd-line arg -c
static bool FLAG_brief = false;                    // cmd-line arg -q
static bool FLAG_verbose = false;                  // cmd-line arg -v

static void LogPrintf(int severity, const char* pat, ...) {
  if (severity == LOG_VERBOSE && !FLAG_verbose)
    return;
  if (severity == LOG_FATAL)
    fprintf(stderr, "FATAL ERROR: ");
  if (severity == LOG_VERBOSE)
    fprintf(stdout, "[VERBOSE] ");
  va_list ap;
  va_start(ap, pat);
  vfprintf(severity == LOG_INFO || severity == LOG_VERBOSE ? stdout: stderr,
           pat, ap);
  va_end(ap);
  if (severity == LOG_FATAL)
    exit(1);
}

// prints to outfile -- usually stdout or stderr -- and then exits
static int Usage(const char* argv0, FILE* outfile) {
  fprintf(outfile, "USAGE: %s [-t<dir>] [-d] [-b] "
          "[-c<context>] <template_filename> ...\n", argv0);
  fprintf(outfile,
          "       -t --template_dir=<dir>  Root directory of templates\n"
          "       -c --context<context>    TemplateContext (default TC_HTML)\n"
          "       -d --dump_templates      Cause templates dump contents\n"
          "       -q --brief               Output only if file has diffs\n"
          "       -h --help                This help\n"
          "       -V --version             Version information\n");
  fprintf(outfile, "\n"
          "This program reports changes to modifiers in your templates\n"
          "that would occur if you switch to the Auto Escape mode.\n"
          "By default it only reports differences in modifiers for variables\n"
          "that have existing modifiers in your templates. To see all the\n"
          "modifiers the Auto Escape mode would set use -d to dump templates.\n"
          "Use -b for terse output when you just want to know if the template\n"
          "file has any diffs."
          "The context is a string (e.g. TC_HTML) and should match what\n"
          "you would provde in Template::GetTemplateWithAutoEscaping.\n");

  exit(0);
}

static int Version(FILE* outfile) {
  fprintf(outfile,
          "diff_tpl_auto_escape (part of google-template 0.9x)\n"
          "\n"
          "Copyright 2008 Google Inc.\n"
          "\n"
          "This is BSD licensed software; see the source for copying conditions\n"
          "and license information.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A\n"
          "PARTICULAR PURPOSE.\n"
          );
  exit(0);
}

// Populates the vector of VariableAndMods from the DumpToString
// representation of the template file.
//
// Each VariableAndMod represents a variable node found in the template
// along with the optional modifiers attached to it (or empty string).
// The parsing is very simple. It looks for lines of the form:
//    "Variable Node: <VAR_NAME>[:<VAR_MODS>]\n"
// as outputted by DumpToString() and extracts from each such line the
// variable name and modifiers when present.
// Because DumpToString also outputs text nodes, it is possible
// to trip this function. Probably ok since this is just a helper tool.
bool LoadVariables(const char* filename, TemplateContext context,
                   bool auto_escape, VariableAndMods& vars_and_mods) {
  const string kVariablePreambleText = "Variable Node: ";
  Template *tpl;
  if (auto_escape) {
    // Setting Strip to the most common value for web-apps.
    // In theory it should not matter, in practice it could particularly
    // with subtle HTML parser cases.
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);
  } else {
    tpl = Template::GetTemplate(filename, STRIP_WHITESPACE);
  }
  if (tpl == NULL) {
    LogPrintf(LOG_ERROR, "Could not load file: %s\n", filename);
    return false;
  }

  string output;
  tpl->DumpToString(filename, &output);
  if (FLAG_dump_templates && auto_escape)
    fprintf(stdout, "%s\n", output.c_str());

  string::size_type index = 0;
  string::size_type delim, end;
  // TODO(jad): Switch to using regular expressions.
  while((index = output.find(kVariablePreambleText, index)) != string::npos) {
    index += kVariablePreambleText.length();
    end = output.find('\n', index);
    if (end == string::npos) {
      // Should never happen but no need to LOG_FATAL.
      LogPrintf(LOG_ERROR, "%s: Did not find terminating newline...\n",
                filename);
      end = output.length();
    }
    string name_and_mods = output.substr(index, end - index);
    delim = name_and_mods.find(":");
    if (delim == string::npos)          // no modifiers.
      delim = name_and_mods.length();
    VariableAndMod var_mod(name_and_mods.substr(0, delim),
                           name_and_mods.substr(delim));
    vars_and_mods.push_back(var_mod);
  }
  return true;
}

// Main function to analyze diffs in a given template filename.
// Given it loads the same file twice, once in standard mode
// and once in auto-escape mode, there is always the improbable
// possibility of the file changing in the mean time. For such
// unexpected conditions, it will fatally terminate the program.
bool DiffTemplate(const char* filename, TemplateContext context) {
  vector<VariableAndMod> vars_and_mods, vars_and_mods_ae;

  if (!LoadVariables(filename, context, false, vars_and_mods) ||
      !LoadVariables(filename, context, true, vars_and_mods_ae))
    return false;

  // Should not happen unless the template file was being edited
  // while this utility is running or other unexpected cases.
  if (vars_and_mods.size() != vars_and_mods_ae.size())
    LogPrintf(LOG_FATAL, "%s: Mismatch in length: %d vs. %d\n",
              filename, vars_and_mods.size(), vars_and_mods_ae.size());

  int mismatch_count = 0;      // How many differences there were.
  int no_modifiers_count = 0;  // How many variables without modifiers.
  VariableAndMods::const_iterator iter, iter_ae;
  for (iter = vars_and_mods.begin(), iter_ae = vars_and_mods_ae.begin();
       iter != vars_and_mods.end() && iter_ae != vars_and_mods_ae.end();
       ++iter, ++iter_ae) {
    // Should not happen unless the template file was being edited
    // while this utility is running or other unexpected cases.
    if (iter->variable_name != iter_ae->variable_name)
      LogPrintf(LOG_FATAL, "%s: Variable name mismatch: %s vs. %s\n",
                filename, iter->variable_name.c_str(),
                iter_ae->variable_name.c_str());
    if (iter->modifiers == "") {
      no_modifiers_count++;
    } else {
      if (iter->modifiers != iter_ae->modifiers) {
        mismatch_count++;
        if (!FLAG_brief)
          LogPrintf(LOG_INFO, "%s: Difference for variable %s -- %s vs. %s\n",
                    filename, iter->variable_name.c_str(),
                    iter->modifiers.c_str(), iter_ae->modifiers.c_str());
      }
    }
  }

  LogPrintf(LOG_VERBOSE, "%s: Variables Found: Total=%d; Diffs=%d; NoMods=%d\n",
            filename, vars_and_mods.size(), mismatch_count, no_modifiers_count);

  if (FLAG_brief && mismatch_count > 0)
    LogPrintf(LOG_INFO, "%s: Detected %d differences.\n",
            filename, mismatch_count);

  return true;
}

int main(int argc, char **argv) {

#if defined(_WIN32)
  // TODO(csilvers): implement something reasonable for windows
# define GETOPT(argc, argv)  -1
  int optind = 1;    // first non-opt argument
  const char* optarg = "";   // not used
#elif defined(HAVE_GETOPT_LONG)
  static struct option longopts[] = {
    {"help", 1, NULL, 'h'},
    {"version", 1, NULL, 'V'},
    {"template_dir", 1, NULL, 't'},
    {"context", 1, NULL, 'c'},
    {"dump_templates", 0, NULL, 'd'},
    {"brief", 0, NULL, 'q'},
    {"verbose", 0, NULL, 'v'},
    {0, 0, 0, 0}
  };
  int option_index;
# define GETOPT(argc, argv)  getopt_long(argc, argv, "t:c:dqhvV", \
                                         longopts, &option_index)
#else
# define GETOPT(argc, argv)  getopt(argc, argv, "t:c:dqhvV")
#endif

  int r = 0;
  while (r != -1) {   // getopt()/getopt_long() return -1 upon no-more-input
    r = GETOPT(argc, argv);
    switch (r) {
      case 't': FLAG_template_dir.assign(optarg); break;
      case 'c': FLAG_template_context.assign(optarg); break;
      case 'd': FLAG_dump_templates = true; break;
      case 'q': FLAG_brief = true; break;
      case 'v': FLAG_verbose = true; break;
      case 'V': Version(stdout); break;
      case -1: break;   // means 'no more input'
      default: Usage(argv[0], stderr);
    }
  }

  if (optind >= argc) {
    LogPrintf(LOG_FATAL,
              "Must specify at least one template file on the command line.\n");
  }

  Template::SetTemplateRootDirectory(FLAG_template_dir);

  // Arbitrary initial value to shush compiler warning, otherwise unused.
  TemplateContext context = TC_HTML;
  if (strcmp(FLAG_template_context.c_str(), "TC_HTML") == 0) {
    context = TC_HTML;
  } else if (strcmp(FLAG_template_context.c_str(), "TC_JS") == 0) {
    context = TC_JS;
  } else {
    LogPrintf(LOG_FATAL, "Context: %s. Must be TC_HTML or TC_JS\n",
              FLAG_template_context.c_str());
  }

  int num_errors = 0;
  for (int i = optind; i < argc; ++i) {
    LogPrintf(LOG_VERBOSE, "------ Checking %s ------\n", argv[i]);
    if (!DiffTemplate(argv[i], context))
      num_errors++;
  }
  return num_errors > 127 ? 127 : num_errors;
}
