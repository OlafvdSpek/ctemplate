## Process this file with automake to produce Makefile.in

# Make sure that when we re-make ./configure, we get the macros we need
ACLOCAL_AMFLAGS = -I m4

# This is so we can #include <google/foo>
AM_CPPFLAGS = -I$(top_srcdir)/src

# These are good warnings to turn on by default
if GCC
AM_CXXFLAGS = -Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare
endif

googleincludedir = $(includedir)/google
## The .h files you want to install (that is, .h files that people
## who install this package can include in their own applications.)
## We have to include both the .h and .h.in forms.  The latter we
## put in noinst_HEADERS.
nodist_googleinclude_HEADERS = \
	src/google/template.h \
	src/google/template_enums.h \
	src/google/template_pathops.h \
	src/google/template_modifiers.h \
	src/google/template_dictionary.h \
	src/google/template_emitter.h \
	src/google/template_namelist.h \
	src/google/template_from_string.h
noinst_HEADERS = \
	src/google/template.h.in \
	src/google/template_enums.h.in \
	src/google/template_pathops.h.in \
	src/google/template_modifiers.h.in \
	src/google/template_dictionary.h.in \
	src/google/template_emitter.h.in \
	src/google/template_namelist.h.in \
	src/google/template_from_string.h.in

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
## This is for HTML and other documentation you want to install.
## Add your documentation files (in doc/) in addition to these
## top-level boilerplate files.  Also add a TODO file if you have one.
dist_doc_DATA = AUTHORS COPYING ChangeLog INSTALL NEWS README README.windows \
	doc/designstyle.css doc/index.html \
	doc/howto.html doc/tips.html doc/example.html \
	doc/xss_resources.html

## The libraries (.so's) you want to install
lib_LTLIBRARIES =
## Libraries that we want to use only internally (for our unittest binaries)
noinst_LTLIBRARIES =
## The binaries you want to install
bin_PROGRAMS =
bin_SCRIPTS =
## The location of the windows project file for each binary we make
WINDOWS_PROJECTS = google-ctemplate.sln


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

# These are the symbols (classes, mostly) we want to export from our library.
# Note this regexp applies to the *mangled* name, which is why we have to
# be careful where we want to assert [^A-Za-z].  (Particularly bad on Darwin.)
# template_modifiers and BaseArena and UnsafeArena perhaps shouldn't be
# exported, but they're needed -- at least -- for some unittests.
CTEMPLATE_SYMBOLS = '(ctemplate|template_modifiers|BaseArena|UnsafeArena|[^A-Za-z]Template[^A-Za-z]|TemplateDictionary|TemplateNamelist|TemplateFromString|TemplateString|TemplateState|[^A-Za-z]Strip[^A-Za-z])'

lib_LTLIBRARIES += libctemplate.la
WINDOWS_PROJECTS += vsprojects/libctemplate/libctemplate.vcproj
libctemplate_la_SOURCES = $(googleinclude_HEADERS) \
	src/base/arena.h src/base/arena.cc src/base/mutex.h \
	src/template.cc src/template_dictionary.cc src/template_modifiers.cc \
	src/template_namelist.cc src/template_pathops.cc \
	src/template_from_string.cc
libctemplate_la_CXXFLAGS = $(PTHREAD_CFLAGS) -DNDEBUG $(AM_CXXFLAGS)
libctemplate_la_LDFLAGS = $(PTHREAD_CFLAGS) -export-symbols-regex $(CTEMPLATE_SYMBOLS)
libctemplate_la_LIBADD = $(PTHREAD_LIBS)

# automake will make different .o files for this library, which is good,
# because we use an extra compiler flag.
lib_LTLIBRARIES += libctemplate_nothreads.la
libctemplate_nothreads_la_SOURCES = $(libctemplate_la_SOURCES)
libctemplate_nothreads_la_CXXFLAGS = -DNDEBUG -DNO_THREADS $(AM_CXXFLAGS)
libctemplate_nothreads_la_LDFLAGS = -export-symbols-regex $(CTEMPLATE_SYMBOLS)

# We could also make a library that has the TemplateDictionaryPeer
# class.  This class is useful for testing (it provides introspection
# on the TemplateDictionary hierarchy that's easier to use than the
# default debugging method, TemplateDictionary::Dump()) but by the
# same token also violates abstraction in ways that might be fragile.
# We don't expose the library for now, until the demonstrated need
# outweighs the costs.  If you'd like to use this library, please send
# mail to google-ctemplate@googlegroups.com!
##lib_LTLIBRARIES += libctemplate_testing.la
noinst_LTLIBRARIES += libctemplate_testing.la
libctemplate_testing_la_SOURCES = $(googleinclude_HEADERS) \
                                  src/tests/template_test_util.h \
                                  src/tests/template_test_util.cc
libctemplate_testing_la_CXXFLAGS = -DNDEBUG $(AM_CXXFLAGS)
CTEMPLATE_TESTING_SYMBOLS = 'TemporaryRegisterTemplate|TemplateDictionaryPeer'
libctemplate_testing_la_LDFLAGS = -export-symbols-regex $(CTEMPLATE_TESTING_SYMBOLS)
# This library depends on libctemplate, but it can use either libctemplate
# or libctemplate_nothreads.  Rather than have two versions of this
# library, I just leave out the deps entirely: you must be sure to specify
# one of the two whenever you link this library into your code.
#libctemplate_testing_la_LIBADD = libctemplate.la # or libctemplate_nothreads.la


# Helper apps
bin_PROGRAMS += make_tpl_varnames_h
WINDOWS_PROJECTS += vsprojects/make_tpl_varname_h/make_tpl_varname_h.vcproj
make_tpl_varnames_h_SOURCES = $(googleinclude_HEADERS) \
	src/make_tpl_varnames_h.cc
make_tpl_varnames_h_LDADD = libctemplate_nothreads.la

bin_SCRIPTS += src/template-converter

# For each of the tests, we test with and without threads

TESTS += template_dictionary_unittest template_dictionary_nothreads_unittest
WINDOWS_PROJECTS += vsprojects/template_dictionary_unittest/template_dictionary_unittest.vcproj
template_dictionary_unittest_SOURCES = src/config_for_unittests.h \
                                       src/base/arena.h \
                                       src/tests/template_dictionary_unittest.cc
template_dictionary_unittest_CXXFLAGS = $(PTHREAD_CFLAGS) $(AM_CXXFLAGS)
template_dictionary_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_dictionary_unittest_LDADD = libctemplate.la libctemplate_testing.la \
                                     $(PTHREAD_LIBS)
template_dictionary_nothreads_unittest_SOURCES = $(template_dictionary_unittest_SOURCES)
template_dictionary_nothreads_unittest_CXXFLAGS = -DNO_THREADS $(AM_CXXFLAGS)
template_dictionary_nothreads_unittest_LDADD = libctemplate_nothreads.la \
                                               libctemplate_testing.la

TESTS += template_modifiers_unittest template_modifiers_nothreads_unittest
WINDOWS_PROJECTS += vsprojects/template_modifiers_unittest/template_modifiers_unittest.vcproj
template_modifiers_unittest_SOURCES = src/config_for_unittests.h \
                                      src/tests/template_modifiers_unittest.cc
template_modifiers_unittest_CXXFLAGS = $(PTHREAD_CFLAGS) $(AM_CXXFLAGS)
template_modifiers_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_modifiers_unittest_LDADD = libctemplate.la libctemplate_testing.la \
                                    $(PTHREAD_LIBS)
template_modifiers_nothreads_unittest_SOURCES = $(template_modifiers_unittest_SOURCES)
template_modifiers_nothreads_unittest_CXXFLAGS = -DNO_THREADS $(AM_CXXFLAGS)
template_modifiers_nothreads_unittest_LDADD = libctemplate_nothreads.la \
                                              libctemplate_testing.la

TESTS += template_setglobals_unittest template_setglobals_nothreads_unittest
WINDOWS_PROJECTS += vsprojects/template_setglobals_unittest/template_setglobals_unittest.vcproj
template_setglobals_unittest_SOURCES = src/config_for_unittests.h \
                                       src/tests/template_setglobals_unittest.cc
template_setglobals_unittest_CXXFLAGS = $(PTHREAD_CFLAGS) $(AM_CXXFLAGS)
template_setglobals_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_setglobals_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_setglobals_nothreads_unittest_SOURCES = $(template_setglobals_unittest_SOURCES)
template_setglobals_nothreads_unittest_CXXFLAGS = -DNO_THREADS $(AM_CXXFLAGS)
template_setglobals_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_from_string_unittest template_from_string_nothreads_unittest
WINDOWS_PROJECTS += vsprojects/template_from_string_unittest/template_from_string_unittest.vcproj
template_from_string_unittest_SOURCES = src/config_for_unittests.h \
                                        src/tests/template_from_string_unittest.cc
template_from_string_unittest_CXXFLAGS = $(PTHREAD_CFLAGS) $(AM_CXXFLAGS)
template_from_string_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_from_string_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_from_string_nothreads_unittest_SOURCES = $(template_from_string_unittest_SOURCES)
template_from_string_nothreads_unittest_CXXFLAGS = -DNO_THREADS $(AM_CXXFLAGS)
template_from_string_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_unittest template_nothreads_unittest
WINDOWS_PROJECTS += vsprojects/template_unittest/template_unittest.vcproj
WINDOWS_PROJECTS += vsprojects/template_unittest_static/template_unittest_static.vcproj
template_unittest_SOURCES = src/config_for_unittests.h \
                            src/tests/template_unittest.cc
template_unittest_CXXFLAGS = $(PTHREAD_CFLAGS) $(AM_CXXFLAGS)
template_unittest_LDFLAGS = $(PTHREAD_CFLAGS)
template_unittest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_nothreads_unittest_SOURCES = src/tests/template_unittest.cc
template_nothreads_unittest_CXXFLAGS = -DNO_THREADS $(AM_CXXFLAGS)
template_nothreads_unittest_LDADD = libctemplate_nothreads.la

TESTS += template_regtest template_nothreads_regtest
WINDOWS_PROJECTS += vsprojects/template_regtest/template_regtest.vcproj
template_regtest_SOURCES = src/tests/template_regtest.cc
template_regtest_CXXFLAGS = $(PTHREAD_CFLAGS) $(AM_CXXFLAGS)
template_regtest_LDFLAGS = $(PTHREAD_CFLAGS)
template_regtest_LDADD = libctemplate.la $(PTHREAD_LIBS)
template_nothreads_regtest_SOURCES = $(template_regtest_SOURCES)
template_nothreads_regtest_CXXFLAGS = -DNO_THREADS $(AM_CXXFLAGS)
template_nothreads_regtest_LDADD = libctemplate_nothreads.la

make_tpl_varnames_h_unittest_sh: $(top_srcdir)/src/tests/make_tpl_varnames_h_unittest.sh \
                                 make_tpl_varnames_h
	$(top_srcdir)/src/tests/make_tpl_varnames_h_unittest.sh \
	   $(top_builddir)/make_tpl_varnames_h /tmp/$@_dir

## ^^^^ END OF RULES TO MAKE THE LIBRARIES, BINARIES, AND UNITTESTS


## This should always include $(TESTS), but may also include other
## binaries that you compile but don't want automatically installed.
noinst_PROGRAMS = $(TESTS)

rpm: dist-gzip packages/rpm.sh packages/rpm/rpm.spec
	@cd packages && ./rpm.sh ${PACKAGE} ${VERSION}

deb: dist-gzip packages/deb.sh packages/deb/*
	@cd packages && ./deb.sh ${PACKAGE} ${VERSION}

# Windows wants write permission to .vcproj files and maybe even sln files.
dist-hook:
	test -e "$(distdir)/vsprojects" \
	   && chmod -R u+w $(distdir)/*.sln $(distdir)/vsprojects/

## If you're using libtool, add 'libtool' here.  Also add this rule:
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
EXTRA_DIST = packages/rpm.sh packages/rpm/rpm.spec packages/deb.sh packages/deb \
	$(SCRIPTS) libtool \
	src/windows $(WINDOWS_PROJECTS) src/solaris/libstdc++.la contrib
