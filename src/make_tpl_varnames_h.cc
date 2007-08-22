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
// A utility for checking syntax and generating headers to
// use with Google Templates.
//
// For example:
//
// > ./make_tpl_varnames_h some_template_file.tpl
//
// This creates the header file some_template_file.tpl.varnames.h
// in ./. If there are any syntax errors they are
// reported to stderr  (in which case, no header file is created)
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
#ifndef WIN32
#include <getopt.h>
#endif
#include <errno.h>
#include <string.h>
#include <string>
#include <google/template_pathops.h>
#include <google/template.h>

using std::string;
using GOOGLE_NAMESPACE::Template;
namespace ctemplate = GOOGLE_NAMESPACE::ctemplate;   // an interior namespace

enum {LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL};

static void LogPrintf(int severity, int should_log_info, const char* pat, ...) {
  if (severity == LOG_INFO && !should_log_info)
    return;
  if (severity == LOG_FATAL)
    fprintf(stderr, "FATAL ERROR: ");
  va_list ap;
  va_start(ap, pat);
  vfprintf(stderr, pat, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  if (severity == LOG_FATAL)
    exit(1);
}

// prints to outfile -- usually stdout or stderr -- and then exits
static int Usage(const char* argv0, FILE* outfile) {
  fprintf(outfile, "USAGE: %s [-t<dir>] [-h<dir>] [-s<suffix>] [-f<filename>]"
          " [-n] [-d] [-q] <template_filename> ...\n", argv0);
  fprintf(outfile,
          "       -t<dir> --template_dir=<dir>  Root directory of templates\n"
          "       -o<dir> --header_dir=<dir>    Where to place output files\n"
          "       -s<suffix> --outputfile_suffix=<suffix>\n"
          "                                     outname = inname + suffix\n"
          "       -f<filename> --outputfile=<filename>\n"
          "                                     outname = filename (Only allowed\n"
          "                                     when processing a single input file)\n"
          "       -n --noheader                 Just check syntax, no output\n"
          "       -d --dump_templates           Cause templates dump contents\n"
          "       -q --nolog_info               Only log on error\n"
          "          --v=-1                     Obsolete, confusing synonym for -q\n"
          "       -h --help                     This help\n"
          "       -V --version                  Version information\n");
  fprintf(outfile, "\n"
          "This program checks the syntax of one or more google templates.\n"
          "By default (without -n) it also emits a header file to an output\n"
          "directory that defines all valid template keys.  This can be used\n"
          "in programs to minimze the probability of typos in template code.\n");
  exit(0);
}

static int Version(FILE* outfile) {
  fprintf(outfile,
          "make_tpl_varnames_h (part of google-template 0.1)\n"
          "\n"
          "Copyright 1998-2005 Google Inc.\n"
          "\n"
          "This is BSD licensed software; see the source for copying conditions\n"
          "and license information.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A\n"
          "PARTICULAR PURPOSE.\n"
          );
  exit(0);
}

int main(int argc, char **argv) {
  string FLAG_template_dir(ctemplate::kCWD);   // "./"
  string FLAG_header_dir(ctemplate::kCWD);
  string FLAG_outputfile_suffix(".varnames.h");
  string FLAG_outputfile("");
  bool FLAG_header = true;
  bool FLAG_dump_templates = false;
  bool FLAG_log_info = true;

#if defined(WIN32)
  // TODO(csilvers): implement something reasonable for windows
# define GETOPT(argc, argv)  -1
  int optind = 1;    // first non-opt argument
  const char* optarg = "";   // not used
#elif defined(HAVE_GETOPT_LONG)
  static struct option longopts[] = {
    {"help", 1, NULL, 'h'},
    {"version", 1, NULL, 'V'},
    {"template_dir", 1, NULL, 't'},
    {"header_dir", 1, NULL, 'o'},
    {"outputfile_suffix", 1, NULL, 's'},
    {"outputfile", 1, NULL, 'f'},
    {"noheader", 0, NULL, 'n'},
    {"dump_templates", 0, NULL, 'd'},
    {"nolog_info", 0, NULL, 'q'},
    {"v", 1, NULL, 'q'},
    {0, 0, 0, 0}
  };
  int option_index;
# define GETOPT(argc, argv)  getopt_long(argc, argv, "t:o:s:f:ndqhV", \
                                         longopts, &option_index)
#else
# define GETOPT(argc, argv)  getopt(argc, argv, "t:o:s:f:ndqhV")
#endif

  int r = 0;
  while (r != -1) {   // getopt()/getopt_long() return -1 upon no-more-input
    r = GETOPT(argc, argv);
    switch (r) {
      case 't': FLAG_template_dir.assign(optarg); break;
      case 'o': FLAG_header_dir.assign(optarg); break;
      case 's': FLAG_outputfile_suffix.assign(optarg); break;
      case 'f': FLAG_outputfile.assign(optarg); break;
      case 'n': FLAG_header = false; break;
      case 'd': FLAG_dump_templates = true; break;
      case 'q': FLAG_log_info = false; break;
      case 'V': Version(stdout); break;
      case -1: break;   // means 'no more input'
      default: Usage(argv[0], stderr);
    }
  }

  if (optind >= argc) {
    LogPrintf(LOG_FATAL, FLAG_log_info,
              "Must specify at least one template file on the command line.");
  }

  // If --outputfile option is being used then --outputfile_suffix as well as
  // --header_dir will be ignored. If there are multiple input files then an
  // error needs to be reported.
  if (!FLAG_outputfile.empty() && optind + 1 > argc) {
    LogPrintf(LOG_FATAL, FLAG_log_info,
              "Only one template file allowed when specifying an explicit "
              "output filename.");
  }

  Template::SetTemplateRootDirectory(FLAG_template_dir);

  int num_errors = 0;
  for (int i = optind; i < argc; ++i) {
    LogPrintf(LOG_INFO, FLAG_log_info, "\n------ Checking %s ------", argv[i]);

    // The last two arguments in the following call do not matter
    // since they control how the template gets expanded and we never
    // expand the template after loading it here
    Template * tpl = Template::GetTemplate(argv[i], GOOGLE_NAMESPACE::DO_NOT_STRIP);

    // The call to GetTemplate (above) loads the template from disk
    // and attempts to parse it. If it cannot find the file or if it
    // detects any template syntax errors, the parsing routines
    // report the error and GetTemplate returns NULL. Syntax errors
    // include such things as mismatched double-curly-bracket pairs,
    // e.g. '{{VAR}', Invalid characters in template variables or
    // section names, e.g.  '{{BAD_VAR?}}' [the question mark is
    // illegal], improperly nested section/end section markers,
    // e.g. a section close marker with no section start marker or a
    // section start of a different name.
    // If that happens, since the parsing errors have already been reported
    // we just continue on to the next one.
    if (!tpl) {
      LogPrintf(LOG_ERROR, FLAG_log_info, "Could not load file: %s", argv[i]);
      num_errors++;
      continue;
    } else {
      LogPrintf(LOG_INFO, FLAG_log_info, "No syntax errors detected in %s", argv[i]);
      if (FLAG_dump_templates)
        tpl->Dump(tpl->template_file());
    }

    // The rest of the loop creates the header file
    if (!FLAG_header)
      continue;            // They don't want header files

    string contents(string("//\n") +
                    "// This header file auto-generated for the template\n" +
                    "//    " + tpl->template_file() + "\n" +
                    "// by " + argv[0] + "\n" +
                    "// DO NOT MODIFY THIS FILE DIRECTLY\n" +
                    "//\n");
    // Now append the header-entry info to the intro above
    tpl->WriteHeaderEntries(&contents);

    // If there is no explicit output filename set, figure out the
    // filename by removing any path before the template_file filename
    string header_file;
    if (!FLAG_outputfile.empty()) {
      header_file = FLAG_outputfile;
    } else {
      string basename = ctemplate::Basename(argv[i]);
      header_file = ctemplate::PathJoin(FLAG_header_dir,
                                        basename + FLAG_outputfile_suffix);
    }

    // Write the results to disk.
    FILE* header = fopen(header_file.c_str(), "wb");
    if (!header) {
      LogPrintf(LOG_ERROR, FLAG_log_info, "Can't open %s", header_file.c_str());
      num_errors++;
      continue;
    } else {
      LogPrintf(LOG_INFO, FLAG_log_info, "Creating %s", header_file.c_str());
    }

    if (fwrite(contents.c_str(), 1, contents.length(), header)
        != contents.length()) {
      LogPrintf(LOG_ERROR, FLAG_log_info, "Can't write %s: %s",
                header_file.c_str(), strerror(errno));
      num_errors++;
    }
    fclose(header);
  }
  // Cap at 127 to avoid causing problems with return code
  return num_errors > 127 ? 127 : num_errors;
}
