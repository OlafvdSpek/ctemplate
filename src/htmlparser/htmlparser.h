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

#ifndef SECURITY_STREAMHTMLPARSER_HTMLPARSER_H
#define SECURITY_STREAMHTMLPARSER_HTMLPARSER_H

#include "config.h"
#include "statemachine.h"
#include "jsparser.h"

// Annoying stuff for windows -- make sure clients (in this case
// unittests) can import the class definitions and variables.
#ifndef CTEMPLATE_DLL_DECL
# ifdef WIN32
#   define CTEMPLATE_DLL_DECL  __declspec(dllimport)
# else
#   define CTEMPLATE_DLL_DECL  /* should be the empty string for non-windows */
# endif
#endif

#ifdef __cplusplus
namespace HTMLPARSER_NAMESPACE {
#endif

/* entity filter */

/* String sizes used in htmlparser and entityfilter structures including the
 * NULL terminator.
 */
#define HTMLPARSER_MAX_STRING STATEMACHINE_RECORD_BUFFER_SIZE
#define HTMLPARSER_MAX_ENTITY_SIZE 10


enum htmlparser_state_external_enum {
    HTMLPARSER_STATE_TEXT,
    HTMLPARSER_STATE_TAG,
    HTMLPARSER_STATE_ATTR,
    HTMLPARSER_STATE_VALUE,
    HTMLPARSER_STATE_COMMENT,
    HTMLPARSER_STATE_JS_FILE,
    HTMLPARSER_STATE_CSS_FILE,
    HTMLPARSER_STATE_ERROR
};

enum htmlparser_mode {
    HTMLPARSER_MODE_HTML,
    HTMLPARSER_MODE_JS
};

enum htmlparser_attr_type {
    HTMLPARSER_ATTR_NONE,
    HTMLPARSER_ATTR_REGULAR,
    HTMLPARSER_ATTR_URI,
    HTMLPARSER_ATTR_JS,
    HTMLPARSER_ATTR_STYLE
};


/* TODO(falmeida): Maybe move some of these declaration to the .c and only keep
 * a forward declaration in here, since these structures are private.
 */
/* entityfilter context structure */
typedef struct entityfilter_ctx_s {

    /* Current position into the buffer. */
    int buffer_pos;

    /* True if currently processing an html entity. */
    int in_entity;

    /* Temporary character buffer that is used while processing html entities.
     */
    char buffer[HTMLPARSER_MAX_ENTITY_SIZE];

    /* String buffer returned to the application after we decoded an html
     * entity.
     */
    char output[HTMLPARSER_MAX_ENTITY_SIZE];
} entityfilter_ctx;

void entityfilter_reset(entityfilter_ctx *ctx);
entityfilter_ctx *entityfilter_new(void);
const char *entityfilter_process(entityfilter_ctx *ctx, char c);


/* html parser */

typedef struct htmlparser_ctx_s {

  /* Holds a reference to the statemachine context. */
  statemachine_ctx *statemachine;

  /* Holds a reference to the statemachine definition in use. Right now this is
   * only used so we can deallocate it at the end. */
  statemachine_definition *statemachine_def;

  /* Holds a reference to the javascript parser. */
  jsparser_ctx *jsparser;

  /* Holds a reference to the entity filter. Used for decoding html entities
   * inside javascript attributes. */
  entityfilter_ctx *entityfilter;

  /* Offset into the current attribute value where 0 is the first character in
   * the value. */
  int value_index;

  /* True if currently processing javascript. */
  int in_js;

  /* Current tag name. */
  char tag[HTMLPARSER_MAX_STRING];

  /* Current attribute name. */
  char attr[HTMLPARSER_MAX_STRING];

  /* Contents of the current value capped to HTMLPARSER_MAX_STRING. */
  char value[HTMLPARSER_MAX_STRING];

} htmlparser_ctx;

CTEMPLATE_DLL_DECL void htmlparser_reset(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL void htmlparser_reset_mode(htmlparser_ctx *ctx, int mode);
CTEMPLATE_DLL_DECL htmlparser_ctx *htmlparser_new(void);
CTEMPLATE_DLL_DECL int htmlparser_state(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL int htmlparser_parse(htmlparser_ctx *ctx, const char *str, int size);

CTEMPLATE_DLL_DECL const char *htmlparser_state_str(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL int htmlparser_is_attr_quoted(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL int htmlparser_in_js(htmlparser_ctx *ctx);

CTEMPLATE_DLL_DECL const char *htmlparser_tag(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL const char *htmlparser_attr(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL const char *htmlparser_value(htmlparser_ctx *ctx);

CTEMPLATE_DLL_DECL int htmlparser_js_state(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL int htmlparser_is_js_quoted(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL int htmlparser_value_index(htmlparser_ctx *ctx);
CTEMPLATE_DLL_DECL int htmlparser_attr_type(htmlparser_ctx *ctx);

CTEMPLATE_DLL_DECL int htmlparser_insert_text(htmlparser_ctx *ctx);

CTEMPLATE_DLL_DECL void htmlparser_delete(htmlparser_ctx *ctx);

#define htmlparser_parse_chr(a,b) htmlparser_parse(a, &(b), 1);
#ifdef __cplusplus
#define htmlparser_parse_str(a,b) htmlparser_parse(a, b, \
                                                   static_cast<int>(strlen(b)));
#else
#define htmlparser_parse_str(a,b) htmlparser_parse(a, b, (int)strlen(b));
#endif

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif

#endif /* SECURITY_STREAMHTMLPARSER_HTMLPARSER_H */
