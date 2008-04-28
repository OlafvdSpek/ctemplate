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
#endif /* __cplusplus */

/* Generated state machine definition. */
#include "jsparser_fsm.cc"

#define state_external(x) jsparser_states_external[x]

/* Initializes a new jsparser instance.
 *
 * Returns a pointer to the new instance or NULL if the initialization
 * fails.
 *
 * Initialization failure is fatal, and if this function fails it may not
 * deallocate all previsouly allocated memory.
 */

jsparser_ctx *jsparser_new()
{
    statemachine_ctx *sm;
    statemachine_definition *def;
    jsparser_ctx *js;

    def = statemachine_definition_new(JSPARSER_NUM_STATES);
    if (def == NULL)
      return NULL;

    sm = statemachine_new(def);
    if (sm == NULL)
      return NULL;

    js = CAST(jsparser_ctx *, calloc(1, sizeof(jsparser_ctx)));
    if (js == NULL)
      return NULL;

    js->statemachine = sm;
    js->statemachine_def = def;
    sm->user = js;

    statemachine_definition_populate(def, jsparser_state_transitions);

    return js;
}

void jsparser_reset(jsparser_ctx *ctx)
{
  assert(ctx != NULL);
  ctx->statemachine->current_state = 0;
}


int jsparser_state(jsparser_ctx *ctx)
{
  return state_external(ctx->statemachine->current_state);
}

int jsparser_parse(jsparser_ctx *ctx, const char *str, int size)
{
    int internal_state;
    internal_state = statemachine_parse(ctx->statemachine, str, size);
    return state_external(internal_state);
}

void jsparser_delete(jsparser_ctx *ctx)
{
    assert(ctx != NULL);
    statemachine_delete(ctx->statemachine);
    statemachine_definition_delete(ctx->statemachine_def);
    free(ctx);
}

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif /* __cplusplus */
