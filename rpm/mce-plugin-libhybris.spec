# Option for building with hybris support
%bcond_with hybris

Name:       mce-plugin-libhybris
Summary:    Libhybris plugin for Mode Control Entity
Version:    1.15.1
Release:    1
License:    LGPLv2
URL:        https://github.com/mer-hybris/mce-plugin-libhybris
Source0:    %{name}-%{version}.tar.bz2

Requires:         mce >= 1.12.10
Requires:         systemd
Requires(pre):    systemd
Requires(post):   systemd
Requires(preun):  systemd
Requires(postun): systemd

BuildRequires:  pkgconfig(glib-2.0) >= 2.18.0
%if %{with hybris}
BuildRequires:  pkgconfig(libhardware)
BuildRequires:  pkgconfig(android-headers)
%endif
BuildRequires:  pkgconfig(systemd)

%description
This package contains a mce plugin that allows mce to use Android
libhardware via libhybris to control for example display brightness
and enable/disable input from proximity and light sensors.

%prep
%autosetup -n %{name}-%{version}

%build
%make_build %{?with_hybris:ENABLE_HYBRIS_SUPPORT=1}

%install
make install DESTDIR=%{buildroot} _LIBDIR=%{_libdir}

%pre
if [ "$1" = "2" ]; then
  # upgrade
  systemctl stop mce.service || :
fi

%post
# upgrade or install
systemctl restart mce.service || :

%preun
if [ "$1" = "0" ]; then
  # uninstall
  systemctl stop mce.service || :
fi

%postun
if [ "$1" = "0" ]; then
  # uninstall
  systemctl start mce.service || :
fi

%files
%defattr(-,root,root,-)
%license COPYING
%{_libdir}/mce/modules/hybris.so
