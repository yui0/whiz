%global build_date %(TZ=Asia/Tokyo date +%Y%m%d)
%define _build_id_links none
%global debug_package %{nil}

# Binary RPM from the source tree:
#   make rpm
# or:
#   rpmbuild -bb --build-in-place --define "_topdir $PWD/rpmbuild" whiz.spec

Name:           whiz
Version:        %{build_date}
Release:        b1
Summary:        Japanese input method for Luna Desktop

License:        MPL-2.0
URL:            https://github.com/Berry-OS/whiz
Group:          System Environment/Libraries

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(gl)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(gtk4)

%description
Whiz is a kana-kanji conversion input method.  This package ships the
Wayland input-method-v2 frontend used by Luna Desktop, GTK 3/4 IM modules
(whiz), plus the runtime model, dictionary, and candidate UI.


%prep
# `make rpm` uses --build-in-place; sources are already in the working tree.


%build
# PREFIX must match %install so WHIZ_MODEL_PATH / WHIZ_DIC_PATH are correct.
%{__make} im-wayland im PREFIX=%{_prefix}


%install
%{__make} install DESTDIR=%{buildroot} PREFIX=%{_prefix}


%post
G3=$(command -v gtk-query-immodules-3.0-64 2>/dev/null || command -v gtk-query-immodules-3.0 2>/dev/null)
G3D=$(pkg-config --variable=libdir gtk+-3.0)/gtk-3.0/$(pkg-config --variable=gtk_binary_version gtk+-3.0)
G4D=$(pkg-config --variable=libdir gtk4)/gtk-4.0/$(pkg-config --variable=gtk_binary_version gtk4)/immodules
if [ -n "$G3" ] && [ -d "$(dirname "$G3D")" ]; then
	$G3 > "$G3D/immodules.cache"
fi
GIO=$(command -v gio-querymodules-64 2>/dev/null || command -v gio-querymodules 2>/dev/null)
if [ -n "$GIO" ] && [ -d "$G4D" ]; then
	$GIO "$G4D"
fi


%files
%{_bindir}/whiz-im-wayland
%{_datadir}/whiz/
%{_libdir}/gtk-3.0/immodules/whiz.so
%{_libdir}/gtk-4.0/*/immodules/libwhiz.so


%changelog
* Tue Aug 18 2026 Yuichiro Nakada <berry@berry-lab.net> - 20260818-b1
- Package Wayland IME, GTK whiz modules, model, dictionary, and UI
- Create for Berry Linux
