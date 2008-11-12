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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "statemachine.h"

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

#define MAX_CHAR_8BIT 256

/* Makes all outgoing transitions from state source to point dest.
 */
static void statetable_fill(int **st, int source, int dest)
{
    int c;

    assert(st != NULL);
    for (c = 0; c < MAX_CHAR_8BIT; c++)
        st[source][CAST(unsigned char, c)] = dest;
}

/* Creates a transition from state source to state dest for input chr.
 */
static void statetable_set(int **st, int source, char chr, int dest)
{
    assert(st != NULL);
    st[source][CAST(unsigned char, chr)] = dest;
}

/* Creates a transition from state source to state dest for the range of
 * characters between start_chr and end_chr.
 */
static void statetable_set_range(int **st, int source, char start_chr,
                                 char end_chr, int dest)
{
    int c;

    assert(st != NULL);
    for (c = start_chr; c <= end_chr; c++)
        statetable_set(st, source, c, dest);
}

/* Creates a transition between state source and dest for an input element
 * that matches the bracket expression expr.
 *
 * The bracket expression is similar to grep(1) or regular expression bracket
 * expressions but it does not support the negation (^) modifier or named
 * character classes like [:alpha:] or [:alnum:].
 */
static void statetable_set_expression(int **st, int source, const char *expr,
                                      int dest)
{
    const char *next;

    assert(st != NULL);
    while (*expr != '\0') {
        next = expr + 1;
        if (*next == '-') {
            next++;
            if (*next != '\0') {
                statetable_set_range(st, source, *expr, *next, dest);
                expr = next;
            } else {
                statetable_set(st, source, '-', dest);
                statetable_set(st, source, *expr, dest);
                return;
            }
        } else {
            statetable_set(st, source, *expr, dest);
        }
        expr++;
    }
}

/* Receives a rule list specified by struct statetable_transitions_s and
 * populates the state table.
 *
 * Example:
 *  struct state_transitions_s transitions[] {
 *      "[:default:]", STATE_I_COMMENT_OPEN, STATE_I_COMMENT,
 *      ">",           STATE_I_COMMENT_OPEN, STATE_I_TEXT,
 *      "-",           STATE_I_COMMENT_OPEN, STATE_I_COMMENT_IN,
 *      NULL, NULL, NULL
 *  };
 *
 * The rules are evaluated in reverse order. :default: is a special expression
 * that matches any input character and if used must be the first rule for a
 * specific state.
 *
 * Also receives an optional argument state_names pointing to a list of strings
 * containing human readable state names. These strings are used when reporting
 * error messages.
 */
void statemachine_definition_populate(statemachine_definition *def,
                                      const struct statetable_transitions_s *tr,
                                      const char **state_names)
{
  assert(def != NULL);
  assert(tr != NULL);

  while (tr->condition != 0) {
    if (strcmp(tr->condition, "[:default:]") == 0)
      statetable_fill(def->transition_table, tr->source, tr->destination);
    else
      statetable_set_expression(def->transition_table, tr->source,
                                tr->condition, tr->destination);
    tr++;
  }

  def->state_names = state_names;
}

/* Initializes a new statetable with a predefined limit of states.
 * Returns the state table object or NULL if the initialization failed, in
 * which case there is no guarantees that this statetable was fully deallocated
 * from memory. */
static int **statetable_new(int states)
{
    int i;
    int c;
    /* TODO(falmeida): Just receive statemachine_definition directly */
    int **state_table;
    state_table = CAST(int **, malloc(states * sizeof(int **)));
    if (!state_table)
      return NULL;

    for (i = 0; i < states; i++) {
        state_table[i] = CAST(int *, malloc(MAX_CHAR_8BIT * sizeof(int)));
        if (!state_table[i])
          return NULL;

        for (c = 0; c < MAX_CHAR_8BIT; c++)
            state_table[i][c] = STATEMACHINE_ERROR;
    }
    return state_table;
}

/* Called to deallocate the state table array.
 */
static void statetable_delete(int **state_table, int states)
{
    int i;

    assert(state_table != NULL);
    for (i = 0; i < states; i++)
        free(state_table[i]);

    free(state_table);
}

/* Add's the callback for the event in_state that is called when the
 * statemachine is in state st.
 *
 * This event is called everytime the the statemachine is in the specified
 * state forevery character in the input stream even if the state remains
 * the same.
 *
 * This is event is the last event to be called and is fired after both events
 * exit_state and enter_state.
 */
void statemachine_in_state(statemachine_definition *def, int st,
                           state_event_function func)
{
    assert(def != NULL);
    assert(st < def->num_states);
    def->in_state_events[st] = func;
}

/* Add's the callback for the event enter_state that is called when the
 * statemachine enters state st.
 *
 * This event is fired after the event exit_state but before the event
 * in_state.
 */
void statemachine_enter_state(statemachine_definition *def, int st,
                              state_event_function func)
{
    assert(def != NULL);
    assert(st < def->num_states);
    def->enter_state_events[st] = func;
}

/* Add's the callback for the event exit_state that is called when the
 * statemachine exits from state st.
 *
 * This is the first event to be called and is fired before both the events
 * enter_state and in_state.
 */
void statemachine_exit_state(statemachine_definition *def, int st,
                             state_event_function func)
{
    assert(def != NULL);
    assert(st < def->num_states);
    def->exit_state_events[st] = func;
}

/* Initializes a new statemachine definition with a defined number of states.
 *
 * Returns NULL if initialization fails.
 *
 * Initialization failure is fatal, and if this function fails it may not
 * deallocate all previsouly allocated memory.
 */
statemachine_definition *statemachine_definition_new(int states)
{
    statemachine_definition *def;
    def = CAST(statemachine_definition *,
               malloc(sizeof(statemachine_definition)));
    if (def == NULL)
      return NULL;

    def->transition_table = statetable_new(states);
    if (def->transition_table == NULL)
      return NULL;


    def->in_state_events = CAST(state_event_function *,
                                calloc(states, sizeof(state_event_function)));
    if (def->in_state_events == NULL)
      return NULL;

    def->enter_state_events =CAST(state_event_function *,
                                   calloc(states,
                                          sizeof(state_event_function)));
    if (def->enter_state_events == NULL)
      return NULL;

    def->exit_state_events = CAST(state_event_function *,
                                  calloc(states, sizeof(state_event_function)));
    if (def->exit_state_events == NULL)
      return NULL;

    def->num_states = states;
    def->state_names = NULL;
    return def;
}

/* Deallocates a statemachine definition object
 */
void statemachine_definition_delete(statemachine_definition *def)
{
    assert(def != NULL);
    statetable_delete(def->transition_table, def->num_states);
    free(def->in_state_events);
    free(def->enter_state_events);
    free(def->exit_state_events);
    free(def);
}

/* Returns the current state.
 */
int statemachine_get_state(statemachine_ctx *ctx) {
  return ctx->current_state;
}

/* Sets the current state.
 *
 * It calls the exit event for the old state and the enter event for the state
 * we intend to move into.
 *
 * Since this state change was not initiated by a character in the input stream
 * we pass a null char to the event functions.
 */
void statemachine_set_state(statemachine_ctx *ctx, int state)
{

  statemachine_definition *def;

  assert(ctx != NULL);
  assert(ctx->definition != NULL);

  def = ctx->definition;

  assert(state < def->num_states);

  ctx->next_state = state;

  if (ctx->current_state != ctx->next_state) {
    if (def->exit_state_events[ctx->current_state])
       def->exit_state_events[ctx->current_state](ctx,
                                                 ctx->current_state,
                                                 '\0',
                                                 ctx->next_state);

    if (def->enter_state_events[ctx->next_state])
       def->enter_state_events[ctx->next_state](ctx,
                                               ctx->current_state,
                                               '\0',
                                               ctx->next_state);
  }

  ctx->current_state = state;
}

/* Reset the statemachine.
 *
 * The state is set to the initialization values. This includes setting the
 * state to the default state (0), stopping recording and setting the line
 * number to 1.
 */
void statemachine_reset(statemachine_ctx *ctx)
{
  ctx->current_state = 0;
  ctx->next_state = 0;
  ctx->record_buffer[0] = '\0';
  ctx->record_pos = 0;
  ctx->recording = 0;
  ctx->lineno = 1;
}

/* Initializes a new statemachine. Receives a statemachine definition object
 * that should have been initialized with statemachine_definition_new() and a
 * user reference to be used by the caller.
 *
 * The user reference is used by the caller to store any instance specific data
 * the caller may need and is typically used to propagate context information
 * to the event callbacks. The user pointer can just be set to NULL if the
 * caller doesn't need it.
 *
 * Returns NULL if initialization fails.
 *
 * Initialization failure is fatal, and if this function fails it may not
 * deallocate all previously allocated memory.
 */
statemachine_ctx *statemachine_new(statemachine_definition *def,
                                   void *user)
{
    statemachine_ctx *ctx;
    assert(def != NULL);
    ctx = CAST(statemachine_ctx *, malloc(sizeof(statemachine_ctx)));
    if (ctx == NULL)
      return NULL;

    statemachine_reset(ctx);

    ctx->definition = def;
    ctx->user = user;

    return ctx;
}

/* Returns a pointer to a context which is a duplicate of the statemachine src.
 * The statemachine definition and the user pointer have to be provided since
 * these references are not owned by the statemachine itself, but this will be
 * shallow copies as they point to data structures we do not own.
 */
statemachine_ctx *statemachine_duplicate(statemachine_ctx *src,
                                         statemachine_definition *def,
                                         void *user)
{
    statemachine_ctx *dst;
    assert(src != NULL);
    dst = statemachine_new(def, user);
    if (dst == NULL)
      return NULL;

    statemachine_copy(dst, src, def, user);

    return dst;
}

/* Copies the context of the statemachine pointed to by src to the statemachine
 * provided by dst.
 * The statemachine definition and the user pointer have to be provided since
 * these references are not owned by the statemachine itself.
 */
void statemachine_copy(statemachine_ctx *dst,
                       statemachine_ctx *src,
                       statemachine_definition *def,
                       void *user)
{
    memcpy(dst, src, sizeof(statemachine_ctx));
    dst->definition = def;
    dst->user = user;
}

/* Deallocates a statemachine object
 */
void statemachine_delete(statemachine_ctx *ctx)
{
    assert(ctx != NULL);
    free(ctx);
}

/* Starts recording the current input stream into an internal buffer.
 * The current input character is included in the recording.
 */
void statemachine_start_record(statemachine_ctx *ctx)
{
    assert(ctx != NULL);
    ctx->record_buffer[0] = '\0';
    ctx->record_pos = 0;
    ctx->recording = 1;
}

/* Stops recording the current input stream.
 * The last input character is not included in the recording.
 * This function returns a pointer to the recorded string buffer.
 */
const char *statemachine_stop_record(statemachine_ctx *ctx)
{
    assert(ctx != NULL);
    assert(ctx->recording);
    ctx->record_buffer[ctx->record_pos] = '\0';
    ctx->recording = 0;
    return ctx->record_buffer;
}

 /* Returns a pointer to the record string buffer.
 */
const char *statemachine_record_buffer(statemachine_ctx *ctx)
{
    return ctx->record_buffer;
}

void statemachine_encode_char(char schr, char *output, size_t len)
{
  unsigned char chr = schr;
  if (chr == '\'') {
    strncpy(output, "\\'", len);
  } else if (chr == '\\') {
    strncpy(output, "\\\\", len);

  /* Like isprint() but not dependent on locale. */
  } else if (chr >= 32 && chr <= 126) {
    snprintf(output, len, "%c", chr);
  } else if (chr == '\n') {
    strncpy(output, "\\n", len);
  } else if (chr == '\r') {
    strncpy(output, "\\r", len);
  } else if (chr == '\t') {
    strncpy(output, "\\t", len);
  } else {
    snprintf(output, len, "\\x%.2x", chr);
  }

  output[len - 1] = '\0';
}

/* Sets the error message in case of a transition error.
 *
 * Called from statemachine_parse to set the error message in case of a
 * transition error.
 */
static void statemachine_set_transition_error_message(statemachine_ctx *ctx)
{
  char encoded_char[10];
  statemachine_encode_char(ctx->current_char, encoded_char,
                           sizeof(encoded_char));

  if (ctx->definition->state_names) {
    snprintf(ctx->error_msg, STATEMACHINE_MAX_STR_ERROR,
             "Unexpected character '%s' in state '%s'",
             encoded_char,
             ctx->definition->state_names[ctx->current_state]);
  } else {
    snprintf(ctx->error_msg, STATEMACHINE_MAX_STR_ERROR,
             "Unexpected character '%s'", encoded_char);
  }

}

/* Parses the input html stream and returns the finishing state.
 *
 * Returns STATEMACHINE_ERROR if unable to parse the input. If
 * statemachine_parse() is called after an error situation was encountered
 * the behaviour is unspecified.
 */
/* TODO(falmeida): change int size to size_t size */
int statemachine_parse(statemachine_ctx *ctx, const char *str, int size)
{
    int i;
    int **state_table = ctx->definition->transition_table;
    statemachine_definition *def;

    assert(ctx !=NULL &&
           ctx->definition != NULL &&
           ctx->definition->transition_table != NULL);

    if (size < 0) {
        snprintf(ctx->error_msg, STATEMACHINE_MAX_STR_ERROR, "%s",
                 "Negative size in statemachine_parse().");
        return STATEMACHINE_ERROR;
    }

    def = ctx->definition;

    for (i = 0; i < size; i++) {
        ctx->current_char = *str;
        ctx->next_state =
            state_table[ctx->current_state][CAST(unsigned char, *str)];
        if (ctx->next_state == STATEMACHINE_ERROR) {
            statemachine_set_transition_error_message(ctx);
            return STATEMACHINE_ERROR;
        }

        if (ctx->current_state != ctx->next_state) {
            if (def->exit_state_events[ctx->current_state])
                def->exit_state_events[ctx->current_state](ctx,
                                                           ctx->current_state,
                                                           *str,
                                                           ctx->next_state);
        }
        if (ctx->current_state != ctx->next_state) {
            if (def->enter_state_events[ctx->next_state])
                def->enter_state_events[ctx->next_state](ctx,
                                                         ctx->current_state,
                                                         *str,
                                                         ctx->next_state);
        }

        if (def->in_state_events[ctx->next_state])
            def->in_state_events[ctx->next_state](ctx,
                                                  ctx->current_state,
                                                  *str,
                                                  ctx->next_state);

        /* We need two bytes left so we can NULL terminate the string. */
        if (ctx->recording &&
            STATEMACHINE_RECORD_BUFFER_SIZE - 1 > ctx->record_pos) {
            ctx->record_buffer[ctx->record_pos++] = *str;
            ctx->record_buffer[ctx->record_pos] = '\0';
        }

/* TODO(falmeida): Should clarify the contract here, since an event can change
 * ctx->next_state and we need this functionality */

        ctx->current_state = ctx->next_state;

        if (*str == '\n')
          ctx->lineno++;
        str++;
    }

    return ctx->current_state;
}

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif