## Process this file with autoconf to produce configure.
## In general, the safest way to proceed is to run the following:
##    % aclocal -I . -I `pwd`/../autoconf && autoheader && autoconf && automake

# make sure we're interpreted by some minimal autoconf
AC_PREREQ(2.57)

AC_INIT(ctemplate, 0.4, opensource@google.com)
# The argument here is just something that should be in the current directory
# (for sanity checking)
AC_CONFIG_SRCDIR(README)
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(src/config.h)

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX

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
AC_CHECK_TYPES([u_int16_t])    # defined in most posix systems
AC_CHECK_TYPES([__uint16])     # defined in some windows systems (vc7)
AC_HEADER_DIRENT               # for template_unittest.cc, template_regtest.cc

# A lot of the code in this directory depends on pthreads
ACX_PTHREAD

# We'd like to use read/write locks in several places in the code.
# See if our pthreads support extends to that.  Note: for linux, it
# does as long as you define _XOPEN_SOURCE appropriately.
AC_RWLOCK

# Find out what namespace 'normal' STL code lives in, and also what namespace
# the user wants our classes to be defined in
AC_CXX_STL_NAMESPACE
AC_DEFINE_GOOGLE_NAMESPACE(google)

AC_CXX_MAKE_HASH_MAP_H(src/google/ctemplate/hash_map.h)
AC_CXX_MAKE_HASH_SET_H(src/google/ctemplate/hash_set.h)

## Check out ../autoconf/ for other macros you can call to do useful stuff

AC_SUBST(ac_google_namespace)
AC_SUBST(ac_google_start_namespace)
AC_SUBST(ac_google_end_namespace)
AC_SUBST(ac_cv_cxx_hash_namespace)
if test "$ac_cv___attribute__" == "yes"; then
   AC_SUBST(ac_google_attribute, 1)
else
   AC_SUBST(ac_google_attribute, 0)
fi

# Write generated configuration file, and also .h files
AC_CONFIG_FILES([Makefile \
                 src/base/arena.h src/google/template_enums.h \
		 src/google/template.h src/google/template_from_string.h \
		 src/google/template_dictionary.h \
		 src/google/template_namelist.h])
AC_OUTPUT
