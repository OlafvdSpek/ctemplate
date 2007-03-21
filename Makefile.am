## Process this file with automake to produce Makefile.in

# Make sure that when we re-make ./configure, we get the macros we need
ACLOCAL_AMFLAGS = -I `pwd`/../autoconf

# This is so we can #include <google/foo>
AM_CPPFLAGS = -I$(top_srcdir)/src

googleincludedir = $(includedir)/google
## The .h files you want to install (that is, .h files that people
## who install this package can include in their own applications.)
## We have to include both the .h and .h.in forms.  The latter we
## put in noinst_HEADERS.
googleinclude_HEADERS = \
	src/google/template.h \
	src/google/template_enums.h \
	src/google/template_dictionary.h \
	src/google/template_namelist.h \
	src/google/template_from_string.h \
	src/google/ctemplate/hash_map.h
	src/google/ctemplate/hash_set.h
noinst_HEADERS = \
	src/base/arena.h.in \
	src/google/template.h.in \
	src/google/template_enums.h.in \
	src/google/template_dictionary.h.in \
	src/google/template_namelist.h.in \
	src/google/template_from_string.h.in

ctemplateincludedir = $(googleincludedir)/ctemplate
# The 'private' header files go into /usr/local/include/google/ctemplate.
ctemplateinclude_HEADERS = \
	src/google/ctemplate/hash_map.h \
	src/google/ctemplate/hash_set.h


docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
## This is for HTML and other documentation you want to install.
## Add your documentation files (in doc/) in addition to these
## top-level boilerplate files.  Also add a TODO file if you have one.
dist_doc_DATA = AUTHORS COPYING ChangeLog INSTALL NEWS README \
	doc/designstyle.css doc/index.html \
	doc/howto.html doc/tips.html doc/example.html

## The libraries (.so's) you want to install
lib_LTLIBRARIES =
## The binaries you want to install
bin_PROGRAMS =
bin_SCRIPTS =

## unittests you want to run when people type 'make check'.
## TESTS is for binary unittests, check_SCRIPTS for script-based unittests.
## TESTS_ENVIRONMENT sets environment variables for when you run unittest,
## but it only seems to take effect for *binary* unittests (argh!)
TESTS =
check_SCRIPTS = make_tpl_varnames_h_unittest_sh
TESTS_ENVIRONMENT = TEMPLATE_ROOTDIR=$(top_srcdir)
# Every time you add a unittest to check_SCRIPTS, add it here too
noinst_SCRIPTS = src/tests/make_tpl_varnames_h_unittest.sh


## vvvv RULES TO MAKE THE LIBRARIES, BINARIES, AND UNITTESTS

lib_LTLIBRARIES += libctemplate.la
libctemplate_la_SOURCES = $(googleinclude_HEADERS) src/config.h \
	src/base/arena.h src/base/arena.cc \
	src/template.cc src/template_dictionary.cc src/template_namelist.cc \
	src/template_from_string.cc
libctemplate_la_CXXFLAGS = $(PTHREAD_CFLAGS) -DNDEBUG
libctemplate_la_LDFLAGS = $(PTHREAD_CFLAGS)
libctemplate_la_LIBADD = $(PTHREAD_LIBS)

# automake will make different .o files for this library, which is good,
# because we use an extra compiler flag.
lib_LTLIBRARIES += libctemplate_nothreads.la
libctemplate_nothreads_la_SOURCES = $(libctemplate_la_SOURCES)
libctemplate_nothreads_la_CXXFLAGS = -DNDEBUG -DNO_THREADS

# Helper apps
bin_PROGRAMS += make_tpl_varnames_h
make_tpl_varnames_h_SOURCES = $(googleinclude_HEADERS) src/config.h \
	src/make_tpl_varnames_h.cc
make_tpl_varnames_h_LDADD = libctemplate_nothreads.la

bin_SCRIPTS += src/template-converter

# For each of the tests, we test with and without threads

TESTS += template_dictionary_unittest template_dictionary_nothreads_unittest
template_dictionary_unittest_SOURCES = src/tests/template_dictionary_unittest.cc
template_dictionary_unittest_CXXFLAGS = $(PTHREAD_CFLAGS)
template_dictionary_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_dictionary_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_dictionary_nothreads_unittest_SOURCES = $(template_dictionary_unittest_SOURCES)
template_dictionary_nothreads_unittest_CXXFLAGS = -DNO_THREADS
template_dictionary_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_setglobals_unittest template_setglobals_nothreads_unittest
template_setglobals_unittest_SOURCES = src/tests/template_setglobals_unittest.cc
template_setglobals_unittest_CXXFLAGS = $(PTHREAD_CFLAGS)
template_setglobals_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_setglobals_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_setglobals_nothreads_unittest_SOURCES = $(template_setglobals_unittest_SOURCES)
template_setglobals_nothreads_unittest_CXXFLAGS = -DNO_THREADS
template_setglobals_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_from_string_unittest template_from_string_nothreads_unittest
template_from_string_unittest_SOURCES = src/tests/template_from_string_unittest.cc
template_from_string_unittest_CXXFLAGS = $(PTHREAD_CFLAGS)
template_from_string_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_from_string_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_from_string_nothreads_unittest_SOURCES = $(template_from_string_unittest_SOURCES)
template_from_string_nothreads_unittest_CXXFLAGS = -DNO_THREADS
template_from_string_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_unittest template_nothreads_unittest
template_unittest_SOURCES = src/tests/template_unittest.cc
template_unittest_CXXFLAGS = $(PTHREAD_CFLAGS)
template_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_nothreads_unittest_SOURCES = src/tests/template_unittest.cc
template_nothreads_unittest_CXXFLAGS = -DNO_THREADS
template_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_regtest template_nothreads_regtest
template_regtest_SOURCES = src/tests/template_regtest.cc
template_regtest_CXXFLAGS = $(PTHREAD_CFLAGS)
template_regtest_LDFLAGS = $(PTHREAD_CFLAGS)
template_regtest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_nothreads_regtest_SOURCES = $(template_regtest_SOURCES)
template_nothreads_regtest_CXXFLAGS = -DNO_THREADS
template_nothreads_regtest_LDADD = libctemplate_nothreads.la

make_tpl_varnames_h_unittest_sh: $(top_srcdir)/src/tests/make_tpl_varnames_h_unittest.sh \
                                 make_tpl_varnames_h
	$< $(top_builddir)/make_tpl_varnames_h /tmp/$@_dir

## ^^^^ END OF RULES TO MAKE THE LIBRARIES, BINARIES, AND UNITTESTS


## This should always include $(TESTS), but may also include other
## binaries that you compile but don't want automatically installed.
noinst_PROGRAMS = $(TESTS)

rpm: dist-gzip packages/rpm.sh packages/rpm/rpm.spec
	@cd packages && ./rpm.sh ${PACKAGE} ${VERSION}

deb: dist-gzip packages/deb.sh packages/deb/*
	@cd packages && ./deb.sh ${PACKAGE} ${VERSION}

## If you're using libtool, add 'libtool' here.  Also add this rule:
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
EXTRA_DIST = packages/rpm.sh packages/rpm/rpm.spec packages/deb.sh packages/deb \
	$(SCRIPTS) libtool

## If you create hash_map.h, hash_set.h, and/or hash_fun.h via the
## ACC_CXX_MAKE_*_H configure.ac macros, add those files here.
DISTCLEANFILES = src/google/ctemplate/hash_map.h src/google/ctemplate/hash_set.h
