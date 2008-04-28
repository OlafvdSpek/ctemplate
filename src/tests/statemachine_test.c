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

#include "config_for_unittests.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "htmlparser/statemachine.h"

/* This makes it easy to test this file with both C and C++ */
#ifdef __cplusplus
using namespace HTMLPARSER_NAMESPACE;
#endif

/* Taken from google templates */

#define ASSERT(cond)  do {                                      \
  if (!(cond)) {                                                \
    printf("%s: %d: ASSERT FAILED: %s\n", __FILE__, __LINE__,   \
           #cond);                                              \
    assert(cond);                                               \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define ASSERT_STREQ(a, b)  do {                                          \
  if (strcmp((a), (b))) {                                                 \
    printf("%s: %d: ASSERT FAILED: '%s' != '%s'\n", __FILE__, __LINE__,   \
           (a), (b));                                                     \
    assert(!strcmp((a), (b)));                                            \
    exit(1);                                                              \
  }                                                                       \
} while (0)

#define ASSERT_STRSTR(text, substr)  do {                       \
  if (!strstr((text), (substr))) {                              \
    printf("%s: %d: ASSERT FAILED: '%s' not in '%s'\n",         \
           __FILE__, __LINE__, (substr), (text));               \
    assert(strstr((text), (substr)));                           \
    exit(1);                                                    \
  }                                                             \
} while (0)


#define NUM_STATES 10
/* This is useful on windows machines */
#undef ERROR

/* To simply the tests */
#define statemachine_parse_str(a,b) statemachine_parse(a, b, (int)strlen(b));

enum state_list {
    A, B, C, D, E, F, ERROR
};

/* Simple state machine test. */
int test_simple()
{
  statemachine_definition *def;
  statemachine_ctx *sm;
  def = statemachine_definition_new(NUM_STATES);
  sm = statemachine_new(def);

  struct statetable_transitions_s transitions[] = {
    { "[:default:]", A, A },
    { "1", A, B },
    { "[:default:]", B, B },
    { "1", B, C },
    { "2", B, A },
    { "[:default:]", C, C },
    { "1", C, D },
    { "2", C, B },
    { "[:default:]", D, D },
    { "2", D, C },
    { NULL, ERROR, ERROR }
  };

  statemachine_definition_populate(def, transitions);
  ASSERT(sm->current_state == A);

  statemachine_parse(sm, "001", 3);
  ASSERT(sm->current_state == B);

  statemachine_parse(sm, "001", 3);
  ASSERT(sm->current_state == C);

  statemachine_parse(sm, "2", 1);
  ASSERT(sm->current_state == B);

  statemachine_parse(sm, "11", 2);
  ASSERT(sm->current_state == D);

  statemachine_delete(sm);
  return 0;

}

/* Tests error handling logic when we try to follow non existent transitions. */
int test_error()
{
  statemachine_definition *def;
  statemachine_ctx *sm;
  int res;
  def = statemachine_definition_new(NUM_STATES);
  sm = statemachine_new(def);

  struct statetable_transitions_s transitions[] = {
    { "[:default:]", A, A },
    { "1", A, B },
    { "1", B, C },
    { "2", B, A },
    { NULL, ERROR, ERROR }
  };

  statemachine_definition_populate(def, transitions);
  ASSERT(sm->current_state == A);

  res = statemachine_parse_str(sm, "001");
  ASSERT(sm->current_state == B);
  ASSERT(sm->current_state == res);

  res = statemachine_parse(sm, "3", 1);
  ASSERT(res == STATEMACHINE_ERROR);


  statemachine_delete(sm);
  return 0;
}

/* Tests htmlparser_start_record() and htmlparser_end_record() logic. */
int test_record()
{
  statemachine_definition *def;
  statemachine_ctx *sm;
  char buffer[100];
  int res;
  def = statemachine_definition_new(NUM_STATES);
  sm = statemachine_new(def);

  struct statetable_transitions_s transitions[] = {
    { "[:default:]", A, A },
    { "1", A, B },
    { "[:default:]", B, B },
    { "2", B, A },
    { NULL, ERROR, ERROR }
  };

  statemachine_definition_populate(def, transitions);
  ASSERT(sm->current_state == A);

  res = statemachine_parse_str(sm, "001");
  ASSERT(sm->current_state == B);
  ASSERT(sm->current_state == res);

  statemachine_start_record(sm, buffer, 20);
  statemachine_parse_str(sm, "121212");
  ASSERT(strcmp(buffer, "121212") == 0);

  statemachine_parse_str(sm, "000");
  statemachine_stop_record(sm);
  ASSERT(strcmp(buffer, "121212000") == 0);

  /* Test for size limit. */
  statemachine_start_record(sm, buffer, 5);
  statemachine_parse_str(sm, "1234567890");
  ASSERT(strcmp(buffer, "1234") == 0);
  statemachine_stop_record(sm);

  /* Test for null termination. */
  statemachine_start_record(sm, buffer, 5);
  statemachine_parse_str(sm, "1");
  ASSERT(strcmp(buffer, "1") == 0);
  statemachine_stop_record(sm);

  statemachine_delete(sm);
  return 0;
}

/* Test with characters outside of the ascii range */
int test_no_ascii()
{
  statemachine_definition *def;
  statemachine_ctx *sm;
  def = statemachine_definition_new(NUM_STATES);
  sm = statemachine_new(def);

  struct statetable_transitions_s transitions[] = {
    { "[:default:]", A, A },
    { "\xf1", A, B },
    { "[:default:]", B, B },
    { "\xf1", B, C },
    { "\xf2", B, A },
    { "[:default:]", C, C },
    { "\xf1", C, D },
    { "\xf2", C, B },
    { "[:default:]", D, D },
    { "\xf2", D, C },
    { NULL, ERROR, ERROR }
  };

  statemachine_definition_populate(def, transitions);
  ASSERT(sm->current_state == A);

  statemachine_parse(sm, "\xf0\xf0\xf1", 3);
  ASSERT(sm->current_state == B);

  statemachine_parse(sm, "\xf0\xf0\xf1", 3);
  ASSERT(sm->current_state == C);

  statemachine_parse(sm, "\xf2", 1);
  ASSERT(sm->current_state == B);

  statemachine_parse(sm, "\xf1\xf1", 2);
  ASSERT(sm->current_state == D);

  statemachine_delete(sm);
  return 0;

}


int main(int argc, char **argv)
{
  test_simple();
  test_error();
  test_record();
  test_no_ascii();
  printf("DONE.\n");
  return 0;
}
