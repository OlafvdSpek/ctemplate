%define	ver	%VERSION
%define	RELEASE	1
%define rel     %{?CUSTOM_RELEASE} %{!?CUSTOM_RELEASE:%RELEASE}
%define	prefix	/usr

Name: %NAME
Summary: Simple but powerful template language for C++
Version: %ver
Release: %rel
Group: Development/Libraries
URL: http://goog-ctemplate.sourceforge.net
License: BSD
Vendor: Google
Packager: Google Inc. <opensource@google.com>
Source: http://goog-ctemplate.sourceforge.net/%{NAME}-%{PACKAGE_VERSION}.tar.gz
Distribution: Redhat 7 and above.
Buildroot: %{_tmppath}/%{name}-root
Prefix: %prefix

%description
The %name package contains a library implementing a simple but
powerful template language for C++.  It emphasizes separating logic
from presentation: it is impossible to embed application logic in this
template language.  This limits the power of the template language
without limiting the power of the template *system*.  Indeed, Google's
"main" web search uses this system exclusively for formatting output.

%package devel
Summary: Simple but powerful template language for C++
Group: Development/Libraries

%description devel
The %name-devel package contains static and debug libraries and header
files for developing applications that use the %name package.

%changelog
    * Mon Mar 13 2006 <opensource@google.com>
    - First draft

%prep
%setup

%build
./configure
make prefix=%prefix

%install
rm -rf $RPM_BUILD_ROOT
make prefix=$RPM_BUILD_ROOT%{prefix} install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)

%doc AUTHORS COPYING ChangeLog INSTALL NEWS README doc/designstyle.css doc/index.html doc/howto.html doc/tips.html doc/example.html doc/xss_resources.html contrib/README.contrib contrib/highlighting.vim contrib/tpl-mode.el

%{prefix}/lib/libctemplate.so.0
%{prefix}/lib/libctemplate.so.0.0.0
%{prefix}/lib/libctemplate_nothreads.so.0
%{prefix}/lib/libctemplate_nothreads.so.0.0.0

%files devel
%defattr(-,root,root)

%{prefix}/include/google
%{prefix}/lib/libctemplate.a
%{prefix}/lib/libctemplate.la
%{prefix}/lib/libctemplate.so
%{prefix}/lib/libctemplate_nothreads.a
%{prefix}/lib/libctemplate_nothreads.la
%{prefix}/lib/libctemplate_nothreads.so
%{prefix}/bin/make_tpl_varnames_h
%{prefix}/bin/template-converter
