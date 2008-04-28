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

#ifndef SECURITY_STREAMHTMLPARSER_JSPARSER_H
#define SECURITY_STREAMHTMLPARSER_JSPARSER_H

#include "config.h"
#include "statemachine.h"

#ifdef __cplusplus
namespace HTMLPARSER_NAMESPACE {
#endif /* __cplusplus */

enum js_state_external_enum {
    JSPARSER_STATE_TEXT,
    JSPARSER_STATE_Q,
    JSPARSER_STATE_DQ,
    JSPARSER_STATE_COMMENT,
};

typedef struct jsparser_ctx_s {
  statemachine_ctx *statemachine;
  statemachine_definition *statemachine_def;
} jsparser_ctx;

void jsparser_reset(jsparser_ctx *ctx);
jsparser_ctx *jsparser_new(void);
int jsparser_state(jsparser_ctx *ctx);
int jsparser_parse(jsparser_ctx *ctx, const char *str, int size);

void jsparser_delete(jsparser_ctx *ctx);

#define jsparser_parse_chr(a,b) jsparser_parse(a, &(b), 1);
#ifdef __cplusplus
#define jsparser_parse_str(a,b) jsparser_parse(a, b, \
                                               static_cast<int>(strlen(b)));
#else
#define jsparser_parse_str(a,b) jsparser_parse(a, b, (int)strlen(b));
#endif

#ifdef __cplusplus
}  /* namespace HTMLPARSER_NAMESPACE */
#endif /* __cplusplus */

#endif /* SECURITY_STREAMHTMLPARSER_JSPARSER_H */
