#!/bin/sh

# Copyright (c) 2008, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# ---
# Author: Jad Boutros
# Heavily inspired from make_tpl_varnames_h_unittest.sh

die() {
    echo "Test failed: $@" 1>&2
    exit 1
}

# Optional first argument is where the executable lives
DIFFTPL=${1-"$TEST_SRCDIR/google3/template/diff_tpl_auto_escape"}

# Optional second argument is tmpdir to use
TMPDIR=${2-"$TEST_TMPDIR/difftpl"}

rm -rf $TMPDIR
mkdir $TMPDIR || die "$LINENO: Can't make $TMPDIR"

# Let's make some templates
# ok1 and ok2 have no diffs, ok3 has one diff, ok4 has 2 diffs.
# bad1 would fail parsing and hence cause an error.
# ok_js is for a template with TC_JS TemplateContext, exercises
# quoted and non-quoted literal javascript substitutions.
echo 'Hello {{USER}}. Have a good one! -- {{OWNER}}.' > $TMPDIR/ok1.tpl
echo 'Hello {{USER:h}}. Have a good one! -- {{OWNER:p}}.' > $TMPDIR/ok2.tpl
echo 'Hello {{USER:j}}. Have a good one! -- {{OWNER:h}}.' > $TMPDIR/ok3.tpl
echo 'Hello {{USER:j}}. Have a good one! -- {{OWNER:c}}.' > $TMPDIR/ok4.tpl
echo "var a = '{{VALUE:j}}'; var b = {{VALUE:J=number}};" > $TMPDIR/ok_js.tpl
echo '<a href={{QC' > $TMPDIR/bad1.tpl

# First, test commandline flags
$DIFFTPL >/dev/null 2>&1 \
   && die "$LINENO: $DIFFTPL with no args didn't give an error"
$DIFFTPL -c TC_HTML >/dev/null 2>&1 \
   && die "$LINENO: $DIFFTPL with no template didn't give an error"
$DIFFTPL -h >/dev/null 2>&1 \
   || die "$LINENO: $DIFFTPL -h failed"
$DIFFTPL --help >/dev/null 2>&1 \
   || die "$LINENO: $DIFFTPL --help failed"

# Some weird (broken) shells leave the ending EOF in the here-document,
# hence the grep.
# No output expected besides verbose info.
expected_ok1_verbose=`cat <<EOF | grep -v '^EOF$'
[VERBOSE] ------ Checking $TMPDIR/ok1.tpl ------
[VERBOSE] $TMPDIR/ok1.tpl: Variables Found: Total=2; Diffs=0; NoMods=2
EOF`

# No output expected besides verbose info.
expected_ok2_verbose=`cat <<EOF | grep -v '^EOF$'
[VERBOSE] ------ Checking $TMPDIR/ok2.tpl ------
[VERBOSE] $TMPDIR/ok2.tpl: Variables Found: Total=2; Diffs=0; NoMods=0
EOF`

# One difference.  We also capture the LOG(WARNING) output
expected_ok3=`cat <<EOF |  grep -v '^EOF$'
WARNING: Token: USER has missing in-template modifiers. You gave :j and we computed :h. We changed to :j:h
$TMPDIR/ok3.tpl: Difference for variable USER -- :j vs. :j:h
EOF`

# Two differences (with associated LOG(WARNING) output).
expected_ok4=`cat <<EOF |  grep -v '^EOF$'
WARNING: Token: USER has missing in-template modifiers. You gave :j and we computed :h. We changed to :j:h
WARNING: Token: OWNER has missing in-template modifiers. You gave :c and we computed :h. We changed to :c:h
$TMPDIR/ok4.tpl: Difference for variable USER -- :j vs. :j:h
$TMPDIR/ok4.tpl: Difference for variable OWNER -- :c vs. :c:h
EOF`

# Two differences but --brief output.
expected_ok4_brief=`cat <<EOF |  grep -v '^EOF$'
WARNING: Token: USER has missing in-template modifiers. You gave :j and we computed :h. We changed to :j:h
WARNING: Token: OWNER has missing in-template modifiers. You gave :c and we computed :h. We changed to :c:h
$TMPDIR/ok4.tpl: Detected 2 differences.
EOF`

# No differences, TemplateContext set to TC_JS, verbose mode.
expected_ok_js_verbose=`cat <<EOF | grep -v '^EOF$'
[VERBOSE] ------ Checking $TMPDIR/ok_js.tpl ------
[VERBOSE] $TMPDIR/ok_js.tpl: Variables Found: Total=2; Diffs=0; NoMods=0
EOF`

# syntax-check these templates
echo "TMPDIR is: $TMPDIR"
$DIFFTPL $TMPDIR/ok1.tpl $TMPDIR/ok2.tpl $TMPDIR/ok3.tpl >/dev/null 2>&1 \
   || die "$LINENO: $DIFFTPL gave error parsing good templates"
$DIFFTPL $TMPDIR/ok1.tpl $TMPDIR/ok2.tpl $TMPDIR/bad1.tpl >/dev/null 2>&1 \
   && die "$LINENO: $DIFFTPL gave no error parsing bad template"
$DIFFTPL $TMPDIR/ok1.tpl $TMPDIR/ok2.tpl $TMPDIR/ok100.tpl >/dev/null 2>&1 \
   && die "$LINENO: $DIFFTPL gave no error parsing non-existent template"

# Now try the same thing, but use template-root so we don't need absdirs
$DIFFTPL --template_root=$TMPDIR ok1.tpl ok2.tpl >/dev/null 2>&1 \
   || die "$LINENO: $DIFFTPL gave error parsing good templates"

# Parse the templates.  Bad one in the middle should be ignored.
$DIFFTPL -c TC_HTML $TMPDIR/ok1.tpl $TMPDIR/bad1.tpl $TMPDIR/ok3.tpl >/dev/null 2>&1
[ $? = 1 ] || die "$LINENO: $DIFFTPL gave wrong error-code parsing 1 bad template: $?"

# If you use relative filenames, must first fix expected outputs.
out=`$DIFFTPL -v $TMPDIR/ok1.tpl 2>&1`
[ "$out" != "$expected_ok1_verbose" ] &&\
  die "$LINENO: $DIFFTPL: bad output for ok1: $out\n"

out=`$DIFFTPL -v $TMPDIR/ok2.tpl 2>&1`
[ "$out" != "$expected_ok2_verbose" ] &&\
  die "$LINENO: $DIFFTPL: bad output for ok2: $out\n"

out=`$DIFFTPL $TMPDIR/ok3.tpl 2>&1`
[ "$out" != "$expected_ok3" ] &&\
  die "$LINENO: $DIFFTPL: bad output for ok3: $out\n"

out=`$DIFFTPL $TMPDIR/ok4.tpl 2>&1`
[ "$out" != "$expected_ok4" ] &&\
  die "$LINENO: $DIFFTPL: bad output for ok4: $out\n"

out=`$DIFFTPL -q $TMPDIR/ok4.tpl 2>&1`
[ "$out" != "$expected_ok4_brief" ] &&\
  die "$LINENO: $DIFFTPL: bad output for ok4 [--brief]: $out\n"

out=`$DIFFTPL -v -c TC_JS $TMPDIR/ok_js.tpl 2>&1`
[ "$out" != "$expected_ok_js_verbose" ] &&\
  die "$LINENO: $DIFFTPL: bad output for ok_js [TC_JS]: $out\n"

echo "PASSED"
