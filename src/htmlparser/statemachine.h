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

#ifndef SECURITY_STREAMHTMLPARSER_STATEMACHINE_H
#define SECURITY_STREAMHTMLPARSER_STATEMACHINE_H

#include "config.h"

#ifdef __cplusplus
namespace HTMLPARSER_NAMESPACE {
#endif

/* TODO(falmeida): I'm not sure about these limits, but since right now we only
 * have 24 states it should be fine */

enum {
    STATEMACHINE_ERROR = 127
};

#define STATEMACHINE_RECORD_BUFFER_SIZE 256

struct statetable_transitions_s {
  const char *condition;
  int source;
  int destination;
};

struct statemachine_ctx_s;

typedef void(*state_event_function)(struct statemachine_ctx_s *, int, char,
                                    int);

typedef struct statemachine_definition_s {
    int num_states;
    int **transition_table;
    state_event_function *in_state_events;
    state_event_function *enter_state_events;
    state_event_function *exit_state_events;
} statemachine_definition;

typedef struct statemachine_ctx_s {
    int current_state;
    int next_state;
    statemachine_definition *definition;
    char current_char;
    char record_buffer[STATEMACHINE_RECORD_BUFFER_SIZE];
    size_t record_pos;

    /* True if we are recording the stream to record_buffer. */
    int recording;

    /* Storage space for the layer above. */
    void *user;
} statemachine_ctx;

void statemachine_definition_populate(statemachine_definition *def,
                                     const struct statetable_transitions_s *tr);

void statemachine_in_state(statemachine_definition *def, int st,
                           state_event_function func);
void statemachine_enter_state(statemachine_definition *def, int st,
                                     state_event_function func);
void statemachine_exit_state(statemachine_definition *def, int st,
                                    state_event_function func);

statemachine_definition *statemachine_definition_new(int states);
void statemachine_definition_delete(statemachine_definition *def);

int statemachine_get_state(statemachine_ctx *ctx);
void statemachine_set_state(statemachine_ctx *ctx, int state);

void statemachine_start_record(statemachine_ctx *ctx);
const char *statemachine_stop_record(statemachine_ctx *ctx);
const char *statemachine_record_buffer(statemachine_ctx *ctx);

/* Returns the the number of characters currently stored in the record buffer.
 */
static inline size_t statemachine_record_length(statemachine_ctx *ctx) {
  return ctx->record_pos + 1;
}

statemachine_ctx *statemachine_new(statemachine_definition *def);
int statemachine_parse(statemachine_ctx *ctx, const char *str, int size);

void statemachine_delete(statemachine_ctx *ctx);

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif

#endif /* SECURITY_STREAMHTMLPARSER_STATEMACHINE_H */
