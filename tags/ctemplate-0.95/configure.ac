## Process this file with autoconf to produce configure.
## In general, the safest way to proceed is to run ./autogen.sh

# make sure we're interpreted by some minimal autoconf
AC_PREREQ(2.57)

AC_INIT(ctemplate, 0.95, opensource@google.com)
# The argument here is just something that should be in the current directory
# (for sanity checking)
AC_CONFIG_SRCDIR(README)
AM_INIT_AUTOMAKE([dist-zip])
AM_CONFIG_HEADER(src/config.h)

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AM_CONDITIONAL(GCC, test "$GCC" = yes)   # let the Makefile know if we're gcc

# Uncomment this if you'll be exporting libraries (.so's)
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)

AX_C___ATTRIBUTE__

# Check whether some low-level functions/files are available
AC_HEADER_STDC

# Defines PRIuS
AC_COMPILER_CHARACTERISTICS

# Here are some examples of how to check for the existence of a fn or file
AC_CHECK_FUNCS(getopt_long)
##AC_CHECK_HEADERS(sys/resource.h)

# These are the types I need.  We look for them in either stdint.h,
# sys/types.h, or inttypes.h, all of which are part of the default-includes.
AC_CHECK_TYPES([uint32_t])
AC_CHECK_TYPES([u_int32_t])
AC_CHECK_TYPES([__int32])
AC_CHECK_TYPES([uint64_t])
AC_CHECK_TYPES([u_int64_t])
AC_CHECK_TYPES([__int64])

AC_HEADER_DIRENT               # for template_unittest.cc, template_regtest.cc

# We need to do byte-swapping efficiently to hash efficiently in
# template_string.cc.  Different architectures do this differently.
AC_CHECK_HEADERS(byteswap.h)              # Linux (GNU in general)
AC_CHECK_HEADERS(libkern/OSByteOrder.h)   # OS X
AC_CHECK_HEADERS(sys/byteorder.h)         # Solaris 10
AC_CHECK_HEADERS(endian.h)                # Linux
AC_CHECK_HEADERS(machine/endian.h)        # OS X
AC_CHECK_HEADERS(sys/endian.h)            # FreeBSD
AC_CHECK_HEADERS(sys/isa_defs.h)          # Solaris 10

# A lot of the code in this directory depends on pthreads
ACX_PTHREAD

# We'd like to use read/write locks in several places in the code.
# See if our pthreads support extends to that.  Note: for linux, it
# does as long as you define _XOPEN_SOURCE appropriately.
AC_RWLOCK

# Find out what namespace 'normal' STL code lives in, and also what namespace
# the user wants our classes to be defined in
AC_CXX_STL_NAMESPACE
AC_DEFINE_GOOGLE_NAMESPACE(ctemplate)

# Figures out where hash_map and hash_set live, and what namespace they use
AC_CXX_STL_HASH

# If we found tr1/unordered_map, prefer unordered_map/unordered_set to
# hash_map/hash_set.
if test "$ac_cv_cxx_have_unordered_map" = yes; then
   ac_cv_cxx_hash_map_class="$ac_cv_cxx_hash_namespace::unordered_map"
   ac_cv_cxx_hash_set_class="$ac_cv_cxx_hash_namespace::unordered_set"
else
   ac_cv_cxx_hash_map_class="$ac_cv_cxx_hash_namespace::hash_map"
   ac_cv_cxx_hash_set_class="$ac_cv_cxx_hash_namespace::hash_set"
fi

AC_SUBST(ac_google_namespace)
AC_SUBST(ac_google_start_namespace)
AC_SUBST(ac_google_end_namespace)
AC_SUBST(ac_cv_cxx_hash_map)
AC_SUBST(ac_cv_cxx_hash_set)
AC_SUBST(ac_cv_cxx_hash_map_class)
AC_SUBST(ac_cv_cxx_hash_set_class)
if test "$ac_cv___attribute__" == "yes"; then
   AC_SUBST(ac_google_attribute, 1)
else
   AC_SUBST(ac_google_attribute, 0)
fi
if test "$ac_cv_type_u_int64_t" == "yes"; then
   AC_SUBST(ac_cv_uint64, u_int64_t)
elif test "$ac_cv_type_uint64_t" == "yes"; then
   AC_SUBST(ac_cv_uint64, uint64_t)
elif test "$ac_cv_type___int64" == "yes"; then
   AC_SUBST(ac_cv_uint64, unsigned __int64)
else
   AC_SUBST(ac_cv_uint64, unsigned long long)   # best we can do
fi

# In unix (that is, using this script), this dll-export stuff will always
# be the empty string.  In windows, we'll replace it with windows-specific
# text.
ac_windows_dllexport_defines=
ac_windows_dllexport=
AC_SUBST(ac_windows_dllexport_defines)
AC_SUBST(ac_windows_dllexport)

# This will (should) never change, but we put it here so if we do need
# to change it, to avoid naming conflicts or something, it's easy to
# do in one place.
ac_htmlparser_namespace=google_ctemplate_streamhtmlparser
AC_SUBST(ac_htmlparser_namespace)
AC_DEFINE_UNQUOTED(HTMLPARSER_NAMESPACE, $ac_htmlparser_namespace,
                   [The namespace to put the htmlparser code.])

# Write generated configuration file, and also .h files
AC_CONFIG_FILES([Makefile \
                 src/ctemplate/template_string.h \
                 src/ctemplate/template_enums.h \
                 src/ctemplate/template.h \
                 src/ctemplate/template_modifiers.h \
                 src/ctemplate/template_emitter.h \
                 src/ctemplate/template_annotator.h \
                 src/ctemplate/template_dictionary.h \
                 src/ctemplate/template_pathops.h \
                 src/ctemplate/template_namelist.h \
                 src/ctemplate/per_expand_data.h \
                 src/ctemplate/template_dictionary_interface.h \
		 ])
AC_OUTPUT
