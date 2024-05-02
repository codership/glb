Name:           glbd
Version:        1.0.1
Release:        1%{?dist}
Summary:        glbd and libglb: TCP proxy daemon and load balancing library in one bottle

License:        GPLv2
URL:            https://github.com/codership/glb            
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  autoconf, automake, libtool
BuildRequires:  gcc, make

%description
glbd is a simple TCP connection balancer made with scalability and performance in mind. 
It was inspired by pen, but unlike pen its functionality is limited only to balancing generic TCP connections.

%prep
%setup -q


%build
autoreconf -i -f

%configure
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%make_install

%{__install} -d %{buildroot}/%{_sysconfdir}/sysconfig
%{__install} -d %{buildroot}/%{_initddir}
%{__install} -d %{buildroot}/%{_docdir}/%{name}-%{version}/examples

%{__install} -m 644 %{_builddir}/%{name}-%{version}/files/glbd.cfg  %{buildroot}/%{_sysconfdir}/sysconfig/glbd
%{__install} -m 755 %{_builddir}/%{name}-%{version}/files/glbd.sh   %{buildroot}/%{_initddir}/glbd
%{__install} -m 644 %{_builddir}/%{name}-%{version}/files/curl.sh   %{buildroot}/%{_docdir}/%{name}-%{version}/examples
%{__install} -m 644 %{_builddir}/%{name}-%{version}/files/mysql.sh  %{buildroot}/%{_docdir}/%{name}-%{version}/examples

%files
%doc README COPYING AUTHORS BUGS ChangeLog
%doc %{_docdir}/%{name}-%{version}/examples/curl.sh
%doc %{_docdir}/%{name}-%{version}/examples/mysql.sh
%{_sbindir}/glbd
%{_libdir}/libglb.*
%config(noreplace) %{_sysconfdir}/sysconfig/glbd
%attr(0755,root,root) %{_initddir}/glbd

%post
%if 0%{?rhel}
chkconfig --add %{name}
%endif

%preun
%if 0%{?rhel}
if [ "$1" = "0" ]; then
  chkconfig --del %{name}
fi
%endif

%changelog
* Wed Oct 20 2021 Alexey Bychko <alexey.bychko@galeracluster.com> - 1.0.1-1
- Initial RPM release

* Tue Sep 26 2021 ALexey Yurchenko <alexey..yurchenko@galeracluster.com> - 1.0.1-1
- Remove gcc-c++ dependecy
