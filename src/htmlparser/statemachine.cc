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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "config.h"
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
 */
void statemachine_definition_populate(statemachine_definition *def,
                                      const struct statetable_transitions_s *tr)
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

/* Initializes a new statemachine. Receives a statemachine definition object
 * that should have been initialized with statemachine_definition_new()
 *
 * Returns NULL if initialization fails.
 *
 * Initialization failure is fatal, and if this function fails it may not
 * deallocate all previsouly allocated memory.
 */
statemachine_ctx *statemachine_new(statemachine_definition *def)
{
    statemachine_ctx *ctx;
    assert(def != NULL);
    ctx = CAST(statemachine_ctx *, malloc(sizeof(statemachine_ctx)));
    if (ctx == NULL)
      return NULL;

    ctx->definition = def;
    ctx->current_state = 0;
    ctx->record_buffer = NULL;
    ctx->record_left = 0;
    return ctx;
}

/* Deallocates a statemachine object
 */
void statemachine_delete(statemachine_ctx *ctx)
{
    assert(ctx != NULL);
    free(ctx);
}

/* Starts recording the current input stream into str. str should have been
 * previously allocated.
 * The current input character is included in the recording.
 */
void statemachine_start_record(statemachine_ctx *ctx, char *str, int len)
{
    assert(ctx != NULL && str != NULL);
    ctx->record_buffer = str;
    ctx->record_buffer[0] = '\0';
    ctx->record_pos = 0;
    ctx->record_left = len;
}

/* statemachine_stop_record(statemachine_ctx *ctx)
 *
 * Stops recording the current input stream.
 * The last input character is not included in the recording.
 */
void statemachine_stop_record(statemachine_ctx *ctx)
{
    assert(ctx != NULL && ctx->record_buffer != NULL);
    ctx->record_buffer[ctx->record_pos] = '\0';
    ctx->record_buffer = NULL;
    ctx->record_left = 0;
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

    assert(ctx !=NULL &&
           ctx->definition != NULL &&
           ctx->definition->transition_table != NULL);

    if (size < 0)
        return STATEMACHINE_ERROR;

    statemachine_definition *def = ctx->definition;

    for (i = 0; i < size; i++) {
        ctx->current_char = *str;
        ctx->next_state =
            state_table[ctx->current_state][CAST(unsigned char, *str)];
        if (ctx->next_state == STATEMACHINE_ERROR) {
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
        if (ctx->record_left >= 2) {
            ctx->record_buffer[ctx->record_pos++] = *str;
            ctx->record_buffer[ctx->record_pos] = '\0';
            ctx->record_left--;
        }

/* TODO(falmeida): Should clarify the contract here, since an event can change
 * ctx->next_state and we need this functionality */

        ctx->current_state = ctx->next_state;
        str++;
    }

    return ctx->current_state;
}

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif
