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

/* TODO(falmeida): Breaks on NULL characters in the stream. fix.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "statemachine.h"
#include "htmlparser.h"
#include "jsparser.h"

/* So we can support both C and C++ compilers, we use the CAST() macro instead
 * of using C style casts or static_cast<>() directly.
 */
#ifdef __cplusplus
  #define CAST(type, expression) (static_cast<type>(expression))
#else
  #define CAST(type, expression) ((type)(expression))
#endif

#ifdef __cplusplus
namespace HTMLPARSER_NAMESPACE {
#endif

/* Generated state machine definition. */
#include "htmlparser_fsm.cc"

#define is_js_attribute(attr) ((attr)[0] == 'o' && (attr)[1] == 'n')
#define is_style_attribute(attr) (strcmp((attr), "style") == 0)

/* html entity filter */
static struct entityfilter_table_s {
    const char *entity;
    const char *value;
} entityfilter_table[] = {
    { "lt",     "<" },
    { "gt",     ">" },
    { "quot",   "\"" },
    { "amp",    "&" },
    { "apos",   "\'" },
    { NULL,     NULL }
};

/* Utility functions */

/* Converts the internal state into the external superstate.
 */
static int state_external(int st) {
    if (st == STATEMACHINE_ERROR)
      return HTMLPARSER_STATE_ERROR;
    else
      return htmlparser_states_external[st];
}

/* Returns true if the character is considered an html whitespace character.
 *
 * From: http://www.w3.org/TR/html401/struct/text.html#h-9.1
 */
static inline int html_isspace(char chr)
{
  if (chr == ' ' || chr == '\t' || chr == '\n' || chr == '\r') {
    return 1;
  } else {
    return 0;
  }
}

/* Returns true if the attribute is expected to contain a url
 * This list was taken from: http://www.w3.org/TR/html4/index/attributes.html
 */
static int is_uri_attribute(char *attr)
{
  if (attr == NULL)
    return 0;

  switch (attr[0]) {
    case 'a':
      if (strcmp(attr, "action") == 0)
        return 1;
      /* TODO(falmeida): This is a uri list. Should we treat it diferently? */
      if (strcmp(attr, "archive") == 0)  /* This is a uri list */
        return 1;
      break;

    case 'b':
      if (strcmp(attr, "background") == 0)
        return 1;
      break;

    case 'c':
      if (strcmp(attr, "cite") == 0)
        return 1;
      if (strcmp(attr, "classid") == 0)
        return 1;
      if (strcmp(attr, "codebase") == 0)
        return 1;
      break;

    case 'd':
      if (strcmp(attr, "data") == 0)
        return 1;
      if (strcmp(attr, "dynsrc") == 0) /* from msdn */
        return 1;
      break;

    case 'h':
      if (strcmp(attr, "href") == 0)
        return 1;
      break;

    case 'l':
      if (strcmp(attr, "longdesc") == 0)
        return 1;
      break;

    case 's':
      if (strcmp(attr, "src") == 0)
        return 1;
      break;

    case 'u':
      if (strcmp(attr, "usemap") == 0)
        return 1;
      break;
  }

  return 0;

}

/* Convert a string to lower case characters inplace.
 */
static void tolower_str(char *s)
{
    while (*s != '\0') {
      *s = CAST(char, tolower(CAST(unsigned char,*s)));
      s++;
    }
}

/* Resets the entityfilter to it's initial state so it can be reused.
 */
void entityfilter_reset(entityfilter_ctx *ctx)
{
    ctx->buffer[0] = 0;
    ctx->buffer_pos = 0;
    ctx->in_entity = 0;
}

/* Initializes a new entity filter object.
 */
entityfilter_ctx *entityfilter_new()
{
    entityfilter_ctx *ctx;
    ctx = CAST(entityfilter_ctx *,
               malloc(sizeof(entityfilter_ctx)));

    if (ctx == NULL)
      return NULL;
    ctx->buffer[0] = 0;
    ctx->buffer_pos = 0;
    ctx->in_entity = 0;

    return ctx;
}

/* Deallocates an entity filter object.
 */
void entityfilter_delete(entityfilter_ctx *ctx)
{
    free(ctx);
}

/* Converts a string containing an hexadecimal number to a string containing
 * one character with the corresponding ascii value.
 *
 * The provided output char array must be at least 2 chars long.
 */
static const char *parse_hex(const char *s, char *output)
{
    int n;
    n = strtol(s, NULL, 16);
    output[0] = n;
    output[1] = 0;
    /* TODO(falmeida): Make this function return void */
    return output;
}

/* Converts a string containing a decimal number to a string containing one
 * character with the corresponding ascii value.
 *
 * The provided output char array must be at least 2 chars long.
 */
static const char *parse_dec(const char *s, char *output)
{
    int n;
    n = strtol(s, NULL, 10);
    output[0] = n;
    output[1] = 0;
    return output;
}

/* Converts a string with an html entity to it's encoded form, which is written
 * to the output string
 */
static const char *entity_convert(const char *s, char *output)
{
  /* TODO(falmeida): Handle wide char encodings */
    struct entityfilter_table_s *t = entityfilter_table;

    if (s[0] == '#') {
      if (s[1] == 'x') { /* hex */
          return parse_hex(s + 2, output);
      } else { /* decimal */
          return parse_dec(s + 1, output);
      }
    }

    while (t->entity != NULL) {
        if (strcasecmp(t->entity, s) == 0)
            return t->value;
        t++;
    }

    snprintf(output, HTMLPARSER_MAX_ENTITY_SIZE, "&%s;", s);
    output[HTMLPARSER_MAX_ENTITY_SIZE - 1] = '\0';

    return output;
}


/* Processes a character from the input stream and decodes any html entities
 * in the processed input stream.
 *
 * Returns a reference to a string that points to an internal buffer. This
 * buffer will be changed after every call to entityfilter_process(). As
 * such this string should be duplicated before subsequent calls to
 * entityfilter_process().
 */
const char *entityfilter_process(entityfilter_ctx *ctx, char c)
{
    if (ctx->in_entity) {
        if (c == ';' || html_isspace(c)) {
            ctx->in_entity = 0;
            ctx->buffer[ctx->buffer_pos] = 0;
            ctx->buffer_pos = 0;
            return entity_convert(ctx->buffer, ctx->output);
        } else {
            ctx->buffer[ctx->buffer_pos++] = c;
            if (ctx->buffer_pos >= HTMLPARSER_MAX_ENTITY_SIZE - 1) {
                /* No more buffer to use, finalize and return */
                ctx->buffer[ctx->buffer_pos] = '\0';
                ctx->in_entity=0;
                ctx->buffer_pos = 0;
                strncpy(ctx->output, ctx->buffer, HTMLPARSER_MAX_ENTITY_SIZE);
                ctx->output[HTMLPARSER_MAX_ENTITY_SIZE - 1] = '\0';
                return ctx->output;
            }
        }
    } else {
        if (c == '&') {
            ctx->in_entity = 1;
            ctx->buffer_pos = 0;
        } else {
            ctx->output[0] = c;
            ctx->output[1] = 0;
            return ctx->output;
        }
    }
    return "";
}

/* Called when the parser enters a new tag. Starts recording it's name into
 * html->tag.
 */
static void enter_tag_name(statemachine_ctx *ctx, int start, char chr, int end)
{
    htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
    assert(html != NULL);

    statemachine_start_record(ctx, html->tag, HTMLPARSER_MAX_STRING - 1);
}

/* Called when the parser exits the tag name in order to finalize the recording.
 *
 * It converts the tag name to lowercase, and if the tag was closed, just
 * clears html->tag.
 */
static void exit_tag_name(statemachine_ctx *ctx, int start, char chr, int end)
{
    htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
    assert(html != NULL);

    statemachine_stop_record(ctx);

    tolower_str(html->tag);

    if (html->tag[0] == '/')
      html->tag[0] = 0;
}

/* Called when the parser enters a new tag. Starts recording it's name into
 * html->attr
 */
static void enter_attr(statemachine_ctx *ctx, int start, char chr, int end)
{
    htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
    assert(html != NULL);

    statemachine_start_record(ctx, html->attr, HTMLPARSER_MAX_STRING - 1);
}

/* Called when the parser exits the tag name in order to finalize the recording.
 *
 * It converts the tag name to lowercase.
 */
static void exit_attr(statemachine_ctx *ctx, int start, char chr, int end)
{
    htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
    assert(html != NULL);

    statemachine_stop_record(ctx);
    tolower_str(html->attr);
}

/* Called when we enter an attribute value.
 *
 * Keeps track of a position index inside the value and initializes the
 * javascript state machine for attributes that accept javascript.
 */
static void enter_value(statemachine_ctx *ctx, int start, char chr, int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  html->value_index = 0;

  if (is_js_attribute(html->attr)) {
    entityfilter_reset(html->entityfilter);
    jsparser_reset(html->jsparser);
    html->in_js = 1;
  } else {
    html->in_js = 0;
  }
}

/* Called when we enter the contents of an attribute value.
 *
 * Initializes the recording of the contents of the value.
 */
static void enter_value_content(statemachine_ctx *ctx, int start, char chr,
                                int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  statemachine_start_record(ctx, html->value, HTMLPARSER_MAX_STRING - 1);
}

/* Called when we exit the contents of an attribute value.
 *
 * Finalizes the recording of the contents of the value.
 */
static void exit_value_content(statemachine_ctx *ctx, int start, char chr,
                                int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  statemachine_stop_record(ctx);
  html->in_js = 0;
}

/* Called for every character inside an attribute value.
 *
 * Used to process javascript and keep track of the position index inside the
 * attribute value.
 */
static void in_state_value(statemachine_ctx *ctx, int start, char chr, int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  html->value_index++;

  if (html->in_js == 1) {
    const char *output;
    output = entityfilter_process(html->entityfilter, chr);
    jsparser_parse_str(html->jsparser, output);
  }
}

/* Called everytime the parser leaves a tag definition.
 *
 * When we encounter a script tag, we initialize the js parser and switch the
 * state to cdata. We also switch to the cdata state when we encounter any
 * other CDATA/RCDATA tag (style, title or textarea) except that we do not
 * initialize the js parser.
 *
 * To simplify the code, we treat RCDATA and CDATA sections the same since the
 * differences between them don't affect the context we are in.
 */
static void enter_text(statemachine_ctx *ctx, int start, char chr, int end)
{
    htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
    assert(html != NULL);

    if (strcmp(html->tag, "script") == 0) {
      ctx->next_state = HTMLPARSER_STATE_INT_CDATA_TEXT;
      jsparser_reset(html->jsparser);
      html->in_js = 1;
    } else if (strcmp(html->tag, "style") == 0 ||
               strcmp(html->tag, "title") == 0 ||
               strcmp(html->tag, "textarea") == 0) {
      ctx->next_state = HTMLPARSER_STATE_INT_CDATA_TEXT;
      html->in_js = 0;
    }
}

/* Called inside cdata blocks in order to parse the javascript.
 *
 * Calls the javascript parser if currently in a script tag.
 */
static void in_state_cdata(statemachine_ctx *ctx, int start, char chr, int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  if (html->in_js)
    jsparser_parse_chr(html->jsparser, chr);
}

/* Called if we encounter a '<' character in a cdata section.
 *
 * When encountering a '<' character inside cdata, we need to find the closing
 * tag name in order to know if the tag is going to be closed or not, so we
 * start recording the name of what could be the closing tag.
 */
static void enter_state_cdata_may_close(statemachine_ctx *ctx, int start,
                                        char chr, int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  statemachine_start_record(ctx, html->cdata_close_tag,
                            HTMLPARSER_MAX_STRING - 1);
}

/* Called when we finish reading what could be a closing cdata tag.
 *
 * Checks if the closing tag name matches the current entity, and if so closes
 * the element.
 */
static void exit_state_cdata_may_close(statemachine_ctx *ctx, int start,
                                       char chr, int end)
{
  htmlparser_ctx *html = CAST(htmlparser_ctx *, ctx->user);
  assert(html != NULL);

  statemachine_stop_record(ctx);
  assert(html->cdata_close_tag[0] == '/');

  if (strcasecmp(&html->cdata_close_tag[1], html->tag) == 0 &&
      (chr == '>' || html_isspace(chr))) { /* Make sure we have a delimiter */
    html->tag[0] = '\0';  /* Empty tag mimicking exit_tag_name(). */
    html->in_js = 0;  /* In case this was a script tag. */
  } else {
    /* Does not close the CDATA section. Go back to CDATA. */
    ctx->next_state = HTMLPARSER_STATE_INT_CDATA_TEXT;
  }
}

/* htmlparser_reset_mode(htmlparser_ctx *ctx, int mode)
 *
 * Resets the parser to it's initial state and changes the parser mode.
 * All internal context like tag name, attribute name or the state of the
 * statemachine are reset to it's original values as if the object was just
 * created.
 *
 * Available modes:
 *  HTMLPARSER_MODE_HTML - Parses html text
 *  HTMLPARSER_MODE_JS - Parses javascript files
 *
 */
void htmlparser_reset_mode(htmlparser_ctx *ctx, int mode)
{
  assert(ctx != NULL);
  ctx->statemachine->current_state = 0;
  ctx->in_js = 0;
  ctx->tag[0] = '\0';
  ctx->attr[0] = '\0';
  ctx->value[0] = '\0';
  ctx->cdata_close_tag[0] = '\0';

  jsparser_reset(ctx->jsparser);

  switch (mode) {
    case HTMLPARSER_MODE_HTML:
      ctx->statemachine->current_state = HTMLPARSER_STATE_INT_TEXT;
      break;
    case HTMLPARSER_MODE_JS:
      ctx->statemachine->current_state = HTMLPARSER_STATE_INT_JS_FILE;
      ctx->in_js = 1;
      break;
    default:
      assert("Invalid mode in htmlparser_reset_mode()." == NULL);
  }
}

/* Resets the parser to it's initial state and to the default mode, which
 * is MODE_HTML.
 *
 * All internal context like tag name, attribute name or the state of the
 * statemachine are reset to it's original values as if the object was just
 * created.
 */
void htmlparser_reset(htmlparser_ctx *ctx)
{
    assert(ctx != NULL);
    htmlparser_reset_mode(ctx, HTMLPARSER_MODE_HTML);
}

/* Initializes a new htmlparser instance.
 *
 * Returns a pointer to the new instance or NULL if the initialization fails.
 * Initialization failure is fatal, and if this function fails it may not
 * deallocate all previsouly allocated memory.
 */
htmlparser_ctx *htmlparser_new()
{
    statemachine_ctx *sm;
    statemachine_definition *def;
    htmlparser_ctx *html;

    def = statemachine_definition_new(HTMLPARSER_NUM_STATES);
    if (def == NULL)
      return NULL;

    sm = statemachine_new(def);
    html = CAST(htmlparser_ctx *, calloc(1, sizeof(htmlparser_ctx)));
    if (html == NULL)
      return NULL;

    html->statemachine = sm;
    html->statemachine_def = def;
    html->jsparser = jsparser_new();
    if (html->jsparser == NULL)
      return NULL;

    html->entityfilter = entityfilter_new();
    if (html->entityfilter == NULL)
      return NULL;

    sm->user = html;

    htmlparser_reset(html);

    statemachine_definition_populate(def, htmlparser_state_transitions);

    statemachine_enter_state(def, HTMLPARSER_STATE_INT_TAG_NAME,
                             enter_tag_name);
    statemachine_exit_state(def, HTMLPARSER_STATE_INT_TAG_NAME, exit_tag_name);

    statemachine_enter_state(def, HTMLPARSER_STATE_INT_ATTR, enter_attr);
    statemachine_exit_state(def, HTMLPARSER_STATE_INT_ATTR, exit_attr);

    statemachine_enter_state(def, HTMLPARSER_STATE_INT_TEXT, enter_text);

/* CDATA states. We must list all cdata and javascript states here. */
/* TODO(falmeida): Declare this list in htmlparser_fsm.config so it doensn't go
 * out of sync. */
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_TEXT, in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_COMMENT_START,
                          in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_COMMENT_START_DASH,
                          in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_COMMENT_BODY,
                          in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_COMMENT_DASH,
                          in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_COMMENT_DASH_DASH,
                          in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_LT, in_state_cdata);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_CDATA_MAY_CLOSE,
                          in_state_cdata);

/* For simplification, we treat the javascript mode as if it were cdata. */
    statemachine_in_state(def, HTMLPARSER_STATE_INT_JS_FILE, in_state_cdata);

    statemachine_enter_state(def, HTMLPARSER_STATE_INT_CDATA_MAY_CLOSE,
                             enter_state_cdata_may_close);
    statemachine_exit_state(def, HTMLPARSER_STATE_INT_CDATA_MAY_CLOSE,
                            exit_state_cdata_may_close);
/* value states */
    statemachine_enter_state(def, HTMLPARSER_STATE_INT_VALUE, enter_value);

/* Called when we enter the content of the value */
    statemachine_enter_state(def, HTMLPARSER_STATE_INT_VALUE_TEXT,
                             enter_value_content);
    statemachine_enter_state(def, HTMLPARSER_STATE_INT_VALUE_Q,
                             enter_value_content);
    statemachine_enter_state(def, HTMLPARSER_STATE_INT_VALUE_DQ,
                             enter_value_content);

/* Called when we exit the content of the value */
    statemachine_exit_state(def, HTMLPARSER_STATE_INT_VALUE_TEXT,
                            exit_value_content);
    statemachine_exit_state(def, HTMLPARSER_STATE_INT_VALUE_Q,
                            exit_value_content);
    statemachine_exit_state(def, HTMLPARSER_STATE_INT_VALUE_DQ,
                            exit_value_content);

    statemachine_in_state(def, HTMLPARSER_STATE_INT_VALUE_TEXT, in_state_value);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_VALUE_Q, in_state_value);
    statemachine_in_state(def, HTMLPARSER_STATE_INT_VALUE_DQ, in_state_value);

    return html;
}

/* Receives an htmlparser context and Returns the current html state.
 */
int htmlparser_state(htmlparser_ctx *ctx)
{
  return state_external(ctx->statemachine->current_state);
}

/* Parses the input html stream and returns the finishing state.
 *
 * Returns HTMLPARSER_ERROR if unable to parse the input. If htmlparser_parse()
 * is called after an error situation was encountered the behaviour is
 * unspecified. At this point, htmlparser_reset() or htmlparser_reset_mode()
 * can be called to reset the state.
 */
int htmlparser_parse(htmlparser_ctx *ctx, const char *str, int size)
{
    int internal_state;
    internal_state = statemachine_parse(ctx->statemachine, str, size);
    return state_external(internal_state);
}


/* Returns true if the parser is inside an attribute value and the value is
 * surrounded by single or double quotes. */
int htmlparser_is_attr_quoted(htmlparser_ctx *ctx) {
  int st = statemachine_get_state(ctx->statemachine);
  if (st == HTMLPARSER_STATE_INT_VALUE_Q_START ||
      st == HTMLPARSER_STATE_INT_VALUE_Q ||
      st == HTMLPARSER_STATE_INT_VALUE_DQ_START ||
      st == HTMLPARSER_STATE_INT_VALUE_DQ)
      return 1;
  else
      return 0;
}

/* Returns true if the parser is currently in javascript. This can be a
 * an attribute that takes javascript, a javascript block or the parser
 * can just be in MODE_JS. */
int htmlparser_in_js(htmlparser_ctx *ctx) {
  int st = statemachine_get_state(ctx->statemachine);

/* CDATA states plus JS_FILE. We must list all cdata and javascript states
 * here. */
/* TODO(falmeida): Declare this list in htmlparser_fsm.config so it doesn't go
 * out of sync. */
  if (ctx->in_js &&
      (st == HTMLPARSER_STATE_INT_CDATA_TEXT ||
       st == HTMLPARSER_STATE_INT_CDATA_COMMENT_START ||
       st == HTMLPARSER_STATE_INT_CDATA_COMMENT_START_DASH ||
       st == HTMLPARSER_STATE_INT_CDATA_COMMENT_BODY ||
       st == HTMLPARSER_STATE_INT_CDATA_COMMENT_DASH ||
       st == HTMLPARSER_STATE_INT_CDATA_COMMENT_DASH_DASH ||
       st == HTMLPARSER_STATE_INT_CDATA_LT ||
       st == HTMLPARSER_STATE_INT_CDATA_MAY_CLOSE ||
       st == HTMLPARSER_STATE_INT_JS_FILE))
    return 1;

  if (state_external(st) == HTMLPARSER_STATE_VALUE && ctx->in_js)
      return 1;
  else
      return 0;
}

/* Returns the current tag or NULL if not available.
 *
 * There is no stack implemented because we currently don't have a need for
 * it, which means tag names are tracked only one level deep.
 *
 * This is better understood by looking at the following example:
 *
 * <b [tag=b]>
 *   [tag=b]
 *   <i>
 *    [tag=i]
 *   </i>
 *  [tag=NULL]
 * </b>
 *
 * The tag is correctly filled inside the tag itself and before any new inner
 * tag is closed, at which point the tag will be null.
 *
 * For our current purposes this is not a problem, but we may implement a tag
 * tracking stack in the future for completeness.
 *
 */
const char *htmlparser_tag(htmlparser_ctx *ctx)
{
  if (ctx->tag[0] != '\0')
    return ctx->tag;
  else
    return NULL;
}

/* Returns true if inside an attribute or a value */
int htmlparser_in_attr(htmlparser_ctx *ctx)
{
    int ext_state = state_external(statemachine_get_state(ctx->statemachine));
    return ext_state == HTMLPARSER_STATE_ATTR ||
           ext_state == HTMLPARSER_STATE_VALUE;
}

/* Returns the current attribute name if inside an attribute name or an
 * attribute value. Returns NULL otherwise */
const char *htmlparser_attr(htmlparser_ctx *ctx)
{
  if (htmlparser_in_attr(ctx))
    return ctx->attr;
  else
    return NULL;
}

/* Returns the contents of the current attribute value.
 *
 * Returns NULL if not inside an attribute value.
 */
const char *htmlparser_value(htmlparser_ctx *ctx)
{
  int ext_state = state_external(statemachine_get_state(ctx->statemachine));
  if (ext_state == HTMLPARSER_STATE_VALUE) {
    return ctx->value;
  } else {
    return NULL;
  }
}


/* Returns the current state of the javascript state machine
 *
 * Currently only present for testing purposes.
 */
int htmlparser_js_state(htmlparser_ctx *ctx)
{
   return jsparser_state(ctx->jsparser);
}

/* True is currently inside a javascript string literal
 */
int htmlparser_is_js_quoted(htmlparser_ctx *ctx)
{
    if (htmlparser_in_js(ctx)) {
      int st = jsparser_state(ctx->jsparser);
      if (st == JSPARSER_STATE_Q ||
          st == JSPARSER_STATE_DQ)
        return 1;
    }
    return 0;
}

/* True if currently inside an attribute value
 */
int htmlparser_in_value(htmlparser_ctx *ctx)
{
    int ext_state = state_external(statemachine_get_state(ctx->statemachine));
    return ext_state == HTMLPARSER_STATE_VALUE;
}

/* Returns the position inside the current attribute value
 */
int htmlparser_value_index(htmlparser_ctx *ctx)
{
    if (htmlparser_in_value(ctx))
        return ctx->value_index;

    return -1;
}

/* Returns the current attribute type.
 *
 * The attribute type can be one of:
 *   HTMLPARSER_ATTR_NONE - not inside an attribute.
 *   HTMLPARSER_ATTR_REGULAR - Inside a normal attribute.
 *   HTMLPARSER_ATTR_URI - Inside an attribute that accepts a uri.
 *   HTMLPARSER_ATTR_JS - Inside a javascript attribute.
 *   HTMLPARSER_ATTR_STYLE - Inside a css style attribute.
 */
int htmlparser_attr_type(htmlparser_ctx *ctx)
{
    if (!htmlparser_in_attr(ctx))
        return HTMLPARSER_ATTR_NONE;

    if (is_js_attribute(ctx->attr))
        return HTMLPARSER_ATTR_JS;

    if (is_uri_attribute(ctx->attr))
        return HTMLPARSER_ATTR_URI;

    if (is_style_attribute(ctx->attr))
        return HTMLPARSER_ATTR_STYLE;

    return HTMLPARSER_ATTR_REGULAR;
}

/* Invoked by the caller when text is expanded by the caller.
 *
 * Should be invoked when a template directive that expands to content is
 * executed but we don't provide this content to the parser itself. This
 * changes the current state by following the default rule, ensuring we
 * stay in sync with the template. Ideally the caller would provide an
 * assertion that ensures the state change change executes the right state
 * transition.
 *
 * Returns 1 if template directives are accepted for this state and 0 if they
 * are not, which should result in an error condition.
 *
 * Right now the only case being handled are unquoted attribute values and it
 * always returns 1. In the future we can handle more cases and restrict
 * the states were we allow template directives by returning 0 for those.
 */
int htmlparser_insert_text(htmlparser_ctx *ctx)
{
  /* TODO(falmeida): Generalize and use a table for these values. */

  if (statemachine_get_state(ctx->statemachine) == HTMLPARSER_STATE_INT_VALUE) {
    statemachine_set_state(ctx->statemachine, HTMLPARSER_STATE_INT_VALUE_TEXT);
  }
  return 1;
}

/* Deallocates an htmlparser context object.
 */
void htmlparser_delete(htmlparser_ctx *ctx)
{
    assert(ctx != NULL);
    statemachine_definition_delete(ctx->statemachine_def);
    statemachine_delete(ctx->statemachine);
    jsparser_delete(ctx->jsparser);
    entityfilter_delete(ctx->entityfilter);
    free(ctx);
}

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif
