
###############################################################################
# MODULE     : main ATHENA make file
# COPYRIGHT  : (C) 1999-2008  Joris van der Hoeven

###############################################################################
# This software falls under the GNU general public license version 3 or later.
# It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
# in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
###############################################################################

prefix = /usr/local
exec_prefix = ${prefix}
includedir = ${prefix}/include
libdir = ${exec_prefix}/lib64
bindir = ${exec_prefix}/bin
datarootdir = ${prefix}/share
datadir = ${datarootdir}
mandir = ${datarootdir}/man
tmdir = ATHENA
tmbin = ${exec_prefix}/libexec/ATHENA
tmdata = ${datarootdir}/ATHENA
verinfo = ${tmdir}/verinfo.txt
tm_devel_version = 2.1.4
tm_devel = ATHENA-2.1.4
tm_devel_release = ATHENA-2.1.4-1
tm_debian_name_devel = athena_2.1.4
tm_stable = ATHENA-2.1.4
tm_stable_release = ATHENA-2.1.4-1
tm_underscore_stable = ATHENA_2.1.4
tm_suffix = x86_64-pc-linux-gnu
so = so
TMREPO = 

#new path and pkg location if tmsdk is used



DESTDIR ?= 

MKDIR = mkdir -p
RM = rm -f
CP = rsync -a --exclude='.*'
MV = mv -f
LN = ln -f
CHMOD = chmod -f
GZIP = gzip -f
STRIP = strip
TOUCH = touch
SIGN = misc/admin/sign_update

.NOTPARALLEL:

###############################################################################
# Main makes
###############################################################################

TEXMACS: EMPTY_DIRS 
	if test "$(ATHENA_DEPS)" != "no"; then \
		cd src; $(MAKE) -f makefile deps; \
	fi
	cd src; $(MAKE) -r -f makefile
	$(MAKE) -f Makefile PLUGINS
	$(MAKE) -f Makefile EX_PLUGINS
	$(CP) misc/scripts/fig2ps $(tmdir)/bin
	$(CP) misc/scripts/athena $(tmdir)/bin
	$(CP) misc/scripts/tm_gs $(tmdir)/bin
	$(CHMOD) 755 $(tmdir)/bin/*
	$(CHMOD) 755 plugins/*/bin/*
	$(RM) -r $(tmdir)/plugins
	$(CP) plugins $(tmdir)/
	@echo ----------------------------------------------------
	@echo dynamic ATHENA has been successfully compiled

STATIC_TEXMACS: EMPTY_DIRS 
	if test "$(ATHENA_DEPS)" != "no"; then \
		cd src; $(MAKE) -f makefile deps; \
	fi
	cd src; $(MAKE) -r -f makefile link=static CFLAGS=-D__TMSTATIC__
	$(MAKE) -f Makefile PLUGINS
	$(MAKE) -f Makefile EX_PLUGINS
	$(CP) misc/scripts/fig2ps $(tmdir)/bin
	$(CP) misc/scripts/athena $(tmdir)/bin
	$(CP) misc/scripts/tm_gs $(tmdir)/bin
	$(CHMOD) 755 $(tmdir)/bin/*
	$(CHMOD) 755 plugins/*/bin/*
	$(RM) -r $(tmdir)/plugins
	$(CP) plugins $(tmdir)/plugins
	@echo ----------------------------------------------------
	@echo static ATHENA has been successfully compiled

: tm-guile168
	cd  && $(MAKE) -j1 && $(MAKE) -j1 install
	
DEPS: EMPTY_DIRS
	cd src; $(MAKE) -f makefile deps

EMPTY_DIRS:
	$(MKDIR) src/Deps
	$(MKDIR) src/Objects
	$(MKDIR) ATHENA/bin
	$(MKDIR) ATHENA/lib

GLUE:
	cd src; $(MAKE) -f makefile GLUE

.PHONY: TEXMACS STATIC_TEXMACS DEPS GLUE EXPERIMENTAL

deps: DEPS
clean: CLEAN
distclean: DISTCLEAN

.PHONY: deps install uninstall clean distclean

###############################################################################
# Plugins
###############################################################################

PLUGINS_ALL := $(wildcard plugins/*)
PLUGINS_MAKEFILE := $(wildcard plugins/*/Makefile)
PLUGINS_COMPILE := $(patsubst %Makefile,%COMPILE,$(PLUGINS_MAKEFILE))
PLUGINS_CLEAN := $(patsubst %Makefile,%CLEAN,$(PLUGINS_MAKEFILE))

plugins/%/COMPILE:
	$(MKDIR) plugins/$*/bin
	cd plugins/$*; $(MAKE) -i -f Makefile CC="clang" CXX="clang++"

plugins/%/CLEAN:
	cd plugins/$*; $(MAKE) -i -f Makefile clean

PLUGINS: $(PLUGINS_COMPILE)

CLEAN_PLUGINS: $(PLUGINS_CLEAN)

.PHONY: PLUGINS CLEAN_PLUGINS

EX_PLUGINS_PRG := $(wildcard ATHENA/examples/plugins/*/progs)
EX_PLUGINS_BIN := $(patsubst %/progs,%/bin,$(EX_PLUGINS_PRG))

ATHENA/examples/plugins/%/bin:
	$(MKDIR) ATHENA/examples/plugins/$*/bin

EX_PLUGINS: $(EX_PLUGINS_BIN)
	$(MKDIR) ATHENA/examples/plugins/dynlink/lib

.PHONY: EX_PLUGINS

###############################################################################
# Installing and removing ATHENA (for system administrators)
###############################################################################

INSTALL_EXECUTABLES:
	$(MKDIR) $(DESTDIR)
	$(MKDIR) $(DESTDIR)/bin
	$(MKDIR) $(DESTDIR)/lib
	$(CP) $(tmdir)/bin/athena.bin $(DESTDIR)/bin
	$(CP) $(tmdir)/bin/tm_gs $(DESTDIR)/bin
	$(CP) $(tmdir)/plugins/*/bin/* $(DESTDIR)/bin
	$(CP) $(tmdir)/plugins/*/lib/*.$(so) \
	$(DESTDIR)/lib 2>/dev/null || :
	$(CP) $(tmdir)/lib/*.$(so) $(DESTDIR)/lib 2>/dev/null || :
	$(CHMOD) 755 $(DESTDIR)/bin/*
	$(CHMOD) 755 $(DESTDIR)/lib/*.$(so) 2>/dev/null || :
	$(RM) $(DESTDIR)/lib/*.a
	@echo installed ATHENA executables in $(DESTDIR)

INSTALL_DATA:
	$(MKDIR) $(DESTDIR)
	$(CP) $(tmdir)/LICENSE $(DESTDIR)
	$(CP) $(tmdir)/doc $(DESTDIR)
	$(CP) $(tmdir)/examples $(DESTDIR)
	$(CP) $(tmdir)/fonts $(DESTDIR)
	$(CP) $(tmdir)/langs $(DESTDIR)
	$(CP) $(tmdir)/misc $(DESTDIR)
	$(CP) $(tmdir)/packages $(DESTDIR)
	$(CP) $(tmdir)/progs $(DESTDIR)
	$(CP) $(tmdir)/styles $(DESTDIR)
	$(CP) $(tmdir)/texts $(DESTDIR)
	$(CHMOD) -R go=rX $(DESTDIR)
	@echo installed ATHENA data in $(DESTDIR)

INSTALL_PLUGINS:
	$(CP) plugins $(DESTDIR)
	$(RM) $(DESTDIR)/plugins/*/Makefile
	$(RM) -r $(DESTDIR)/plugins/*/src
	$(RM) -r $(DESTDIR)/plugins/*/bin
	$(RM) -r $(DESTDIR)/plugins/*/lib
	@echo installed ATHENA plugins data in $(DESTDIR)/plugins

INSTALL_ICONS:
	$(MKDIR) $(DESTDIR)/pixmaps
	$(CP) $(tmdir)/misc/pixmaps/ATHENA.xpm $(DESTDIR)/pixmaps
	packages/linux/icons.sh install $(tmdir)/misc/images athena text-x-athena
	packages/linux/mime.sh install $(tmdir)/misc/mime
	@echo installed ATHENA icons in $(DESTDIR)/pixmaps

INSTALL_STARTUP:
	$(MKDIR) $(DESTDIR)
	$(CHMOD) 755 $(tmdir)/bin/*
	$(CP) $(tmdir)/bin/fig2ps $(DESTDIR)
	$(CP) $(tmdir)/bin/athena $(DESTDIR)
	@echo installed ATHENA startup scripts in $(DESTDIR)

INSTALL_INCLUDE:
	$(MKDIR) $(DESTDIR)
	$(CP) $(tmdir)/include/ATHENA.h $(DESTDIR)
	$(CHMOD) go=rX $(DESTDIR)/ATHENA.h
	@echo installed ATHENA include files in $(DESTDIR)

INSTALL_MANUALS:
	$(MKDIR) $(DESTDIR)
	$(MKDIR) $(DESTDIR)/man1
	$(CP) misc/man/fig2ps.1 $(DESTDIR)/man1
	$(CP) misc/man/athena.1 $(DESTDIR)/man1
	$(GZIP) $(DESTDIR)/man1/fig2ps.1
	$(GZIP) $(DESTDIR)/man1/athena.1
	$(CHMOD) go=rX $(DESTDIR)/man1/fig2ps.1.gz
	$(CHMOD) go=rX $(DESTDIR)/man1/athena.1.gz
	@echo installed ATHENA manual pages in $(DESTDIR)

INSTALL:
	$(MAKE) INSTALL_EXECUTABLES DESTDIR=$(DESTDIR)$(tmbin)
	$(MAKE) INSTALL_DATA DESTDIR=$(DESTDIR)$(tmdata)
	$(MAKE) INSTALL_PLUGINS DESTDIR=$(DESTDIR)$(tmdata)
	$(MAKE) INSTALL_ICONS DESTDIR=$(DESTDIR)$(datadir)
	$(MAKE) INSTALL_STARTUP DESTDIR=$(DESTDIR)$(bindir)
	$(MAKE) INSTALL_INCLUDE DESTDIR=$(DESTDIR)$(includedir)
	$(MAKE) INSTALL_MANUALS DESTDIR=$(DESTDIR)$(mandir)
	@echo ----------------------------------------------------
	@echo ATHENA has been successfully installed

install: INSTALL

UNINSTALL:
	$(RM) -r $(tmbin)
	@echo removed ATHENA executables from $(tmbin)
	$(RM) -r $(tmdata)
	@echo removed ATHENA data from $(tmdata)
	$(RM) $(datadir)/pixmaps/ATHENA.xpm
	packages/linux/mime.sh uninstall $(tmdir)/misc/mime
	packages/linux/icons.sh uninstall $(tmdir)/misc/images athena text-x-athena
	$(RM) $(includedir)/ATHENA.h
	@echo removed ATHENA include files from $(includedir)
	$(RM) $(bindir)/fig2ps
	$(RM) $(bindir)/athena
	@echo removed ATHENA startup scripts from $(bindir)
	$(RM) $(mandir)/man1/fig2ps.1.gz
	$(RM) $(mandir)/man1/athena.1.gz
	@echo removed ATHENA manual pages from $(mandir)
	@echo ----------------------------------------------------
	@echo ATHENA has been successfully removed

unistall: UNINSTALL

.PHONY: INSTALL UNINSTALL

###############################################################################
# Subtargets for the production of packages
###############################################################################

BUILD_DIR = ../distr/build/$(tm_devel)
BUILD_TGZ = ../distr/build/$(tm_devel).tar.gz

COPY_SOURCES:
	$(MKDIR) ../distr
	$(RM) -r ../distr/build
	$(MKDIR) ../distr/build
	$(MKDIR) $(BUILD_DIR)
	$(CP) * $(BUILD_DIR)/.
	cd $(BUILD_DIR); $(MAKE) ACCESS_FLAGS
	cd $(BUILD_DIR); $(MAKE) DISTR_CLEAN

COPY_SOURCES_TGZ: COPY_SOURCES COPY_GUILE
	cd ../distr/build; tar -czf $(tm_devel).tar.gz $(tm_devel) 

COPY_GUILE:
	GUILE_LOAD_PATH=`find /usr/share/guile/1.8 -type d | grep ice-9`; \
	export GUILE_LOAD_PATH; \
	for I in $$GUILE_LOAD_PATH ; \
	do $(CP) $$I $(BUILD_DIR)/ATHENA/progs/ ; done
	$(CHMOD) -R go=rX $(BUILD_DIR)/ATHENA/progs/ice-9

###############################################################################
# Make a source package
###############################################################################

SRC_PACKAGE_DIR = ../distr/source

SRC_PACKAGE: COPY_SOURCES
	$(MV) $(BUILD_DIR) ../distr/build/$(tm_devel)-src
	cd ../distr/build; tar -czf $(tm_devel)-src.tar.gz $(tm_devel)-src
	$(MKDIR) $(SRC_PACKAGE_DIR)
	$(MV) ../distr/build/$(tm_devel)-src.tar.gz $(SRC_PACKAGE_DIR)/.
	$(RM) -r ../distr/build

.PHONY: SRC_PACKAGE

###############################################################################
# Make generic static binary packages
###############################################################################

PACKAGE: GENERIC_PACKAGE

BUNDLE: 

.PHONY: PACKAGE BUNDLE

GENERIC_PACKAGE_DIR = ../distr/generic
STATIC_QT = $(tm_devel)-$(tm_suffix)
STATIC_X11 = $(tm_devel)-x11-$(tm_suffix)

GENERIC_PACKAGE: TEXMACS STRIP
	$(MKDIR) $(GENERIC_PACKAGE_DIR)
	-find /usr/share/guile/1.8 -type d -name ice-9 -exec $(CP) {} $(tmdir)/progs \; && \
		 $(CP) $(tmdir) $(STATIC_QT) && \
		 tar --exclude .svn --mode go=rX -czhf $(GENERIC_PACKAGE_DIR)/$(STATIC_QT)-B.tar.gz $(STATIC_QT)
	$(RM) -r $(tmdir)/progs/ice-9 $(STATIC_QT)

GENERIC_X11_PACKAGE: COPY_SOURCES COPY_GUILE
	cd $(BUILD_DIR); ./configure --disable-qt --disable-pdf-renderer
	cd $(BUILD_DIR); $(MAKE) STATIC_TEXMACS
	cd $(BUILD_DIR); $(MAKE) ACCESS_FLAGS
	$(MV) $(BUILD_DIR)/ATHENA $(BUILD_DIR)/$(STATIC_X11)
	cd $(BUILD_DIR); tar -czf $(STATIC_X11).tar.gz $(STATIC_X11)
	$(MKDIR) $(GENERIC_PACKAGE_DIR)
	$(MV) $(BUILD_DIR)/$(STATIC_X11).tar.gz $(GENERIC_PACKAGE_DIR)
	$(RM) -r ../distr/build

.PHONY: GENERIC_PACKAGE GENERIC_X11_PACKAGE

###############################################################################
# Make a CentOs package
###############################################################################

CENTOS_PACKAGE_SRC = packages/centos
CENTOS_PACKAGE_DIR = ../distr/centos

CENTOS_PACKAGE: COPY_SOURCES_TGZ
	mkdir -p $(HOME)/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	if test ! -f ~/.rpmmacros; then \
		echo '%_topdir %(echo $$HOME)/rpmbuild' > ~/.rpmmacros; \
	fi
	$(CP) $(CENTOS_PACKAGE_SRC)/ATHENA.spec $(HOME)/rpmbuild/SPECS/.
	$(CP) $(BUILD_TGZ) $(HOME)/rpmbuild/SOURCES/.
	cd $(HOME); rpmbuild -ba rpmbuild/SPECS/ATHENA.spec
	$(MV) $(HOME)/rpmbuild/RPMS/*/ATHENA* $(CENTOS_PACKAGE_DIR)
	$(MV) $(HOME)/rpmbuild/SRPMS/ATHENA* $(CENTOS_PACKAGE_DIR)
	$(RM) -r ../distr/build

.PHONY: CENTOS_PACKAGE

###############################################################################
# Make a Debian package
###############################################################################

DEBIAN_PACKAGE_SRC = packages/debian
DEBIAN_PACKAGE_DIR = ../distr/debian

DEBIAN_PACKAGE: COPY_SOURCES_TGZ
	$(CP) $(BUILD_TGZ) ../distr/build/$(tm_debian_name_devel).orig.tar.gz
	$(MKDIR) $(BUILD_DIR)/debian ;
	$(CP) $(DEBIAN_PACKAGE_SRC)/* $(BUILD_DIR)/debian
	cd $(BUILD_DIR) && dh_shlibdeps;debuild -us -uc
	cd $(CURDIR)
	$(MKDIR) $(DEBIAN_PACKAGE_DIR)
	$(MV) ../distr/build/*.deb $(DEBIAN_PACKAGE_DIR)
	$(RM) -fr ../distr/build

.PHONY: DEBIAN_PACKAGE

###############################################################################
# Make a Fedora package
###############################################################################

FEDORA_PACKAGE_SRC = packages/fedora
FEDORA_PACKAGE_DIR = ../distr/fedora

FEDORA_PACKAGE: COPY_SOURCES_TGZ
	rpmdev-setuptree;
	$(CP) $(FEDORA_PACKAGE_SRC)/ATHENA.spec $(HOME)/rpmbuild/SPECS/.
	$(CP) $(BUILD_TGZ) $(HOME)/rpmbuild/SOURCES/.
	cd $(HOME); rpmbuild -ba rpmbuild/SPECS/ATHENA.spec
	$(MV) $(HOME)/rpmbuild/RPMS/*/ATHENA* $(FEDORA_PACKAGE_DIR)
	$(MV) $(HOME)/rpmbuild/SRPMS/ATHENA* $(FEDORA_PACKAGE_DIR)
	$(RM) -r ../distr/build

.PHONY: FEDORA_PACKAGE

###############################################################################
# Make a Mandriva package
###############################################################################

MANDRIVA_PACKAGE_SRC = packages/mandriva
MANDRIVA_PACKAGE_DIR = ../distr/mandriva

MANDRIVA_PACKAGE: COPY_SOURCES_TGZ
	mkdir -p $(HOME)/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	if test ! -f ~/.rpmmacros; then \
		echo '%_topdir %(echo $$HOME)/rpmbuild' > ~/.rpmmacros; \
	fi
	$(CP) $(MANDRIVA_PACKAGE_SRC)/ATHENA.spec $(HOME)/rpmbuild/SPECS/.
	$(CP) $(BUILD_TGZ) $(HOME)/rpmbuild/SOURCES/.
	cd $(HOME); rpmbuild -ba rpmbuild/SPECS/ATHENA.spec
	$(MV) $(HOME)/rpmbuild/RPMS/*/ATHENA* $(MANDRIVA_PACKAGE_DIR)
	$(MV) $(HOME)/rpmbuild/SRPMS/ATHENA* $(MANDRIVA_PACKAGE_DIR)
	$(RM) -r ../distr/build

.PHONY: MANDRIVA_PACKAGE

###############################################################################
# Make a RedHat package
###############################################################################

REDHAT_PACKAGE_SRC = packages/redhat
REDHAT_PACKAGE_DIR = ../distr/redhat

REDHAT_PACKAGE: COPY_SOURCES_TGZ
	mkdir -p $(HOME)/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	if test ! -f ~/.rpmmacros; then \
		echo '%_topdir %(echo $$HOME)/rpmbuild' > ~/.rpmmacros; \
	fi
	$(CP) $(REDHAT_PACKAGE_SRC)/ATHENA.spec $(HOME)/rpmbuild/SPECS/.
	$(CP) $(BUILD_TGZ) $(HOME)/rpmbuild/SOURCES/.
	cd $(HOME); rpmbuild -ba rpmbuild/SPECS/ATHENA.spec
	$(MKDIR) $(REDHAT_PACKAGE_DIR)
	$(MV) $(HOME)/rpmbuild/RPMS/*/ATHENA* $(REDHAT_PACKAGE_DIR)
	$(MV) $(HOME)/rpmbuild/SRPMS/ATHENA* $(REDHAT_PACKAGE_DIR)
	$(RM) -r ../distr/build

.PHONY: REDHAT_PACKAGE

###############################################################################
# Make a Ubuntu package
###############################################################################

UBUNTU_PACKAGE_SRC = packages/debian
UBUNTU_PACKAGE_DIR = ../distr/ubuntu

UBUNTU_PACKAGE: COPY_SOURCES_TGZ
	$(CP) $(BUILD_TGZ) ../distr/build/$(tm_debian_name_devel).orig.tar.gz
	$(MKDIR) $(BUILD_DIR)/debian ;
	$(CP) $(UBUNTU_PACKAGE_SRC)/* $(BUILD_DIR)/debian
	cd $(BUILD_DIR); debuild -us -uc
	$(CP) ../distr/build/*.deb $(UBUNTU_PACKAGE_DIR)
	$(RM) -r ../distr/build

.PHONY: UBUNTU_PACKAGE

###############################################################################
# Make Mac OS X bundles and diskimages
###############################################################################

QT_FRAMEWORKS_PATH = /usr/lib64
QT_PLUGINS_PATH = /usr/lib64/qt5/plugins
QT_PLUGINS_LIST = imageformats,platforms,iconengines
SPARKLE_FRAMEWORK_PATH = @SPARKLE_FRAMEWORK_PATH@

MACOS_PACKAGE_SRC = packages/macos
MACOS_PACKAGE_DIR = ../distr/macos

MACOS_PACKAGE_APP := $$(pwd)/../distr/ATHENA.app
MACOS_PACKAGE_DMG = ../distr/$(tm_devel).dmg
MACOS_PACKAGE_ZIP = ../distr/$(tm_devel).zip

MACOS_PACKAGE_CONTENTS = $(MACOS_PACKAGE_APP)/Contents
MACOS_PACKAGE_RESOURCES = $(MACOS_PACKAGE_CONTENTS)/Resources
MACOS_PACKAGE_TEXMACS = $(MACOS_PACKAGE_RESOURCES)/share/ATHENA

MACOS_BUNDLE: TEXMACS
	$(MKDIR) ../distr
	$(RM) -r $(MACOS_PACKAGE_APP)
	$(MKDIR) $(MACOS_PACKAGE_APP) $(MACOS_PACKAGE_CONTENTS) $(MACOS_PACKAGE_RESOURCES)
	$(MKDIR) $(MACOS_PACKAGE_CONTENTS)/Frameworks $(MACOS_PACKAGE_CONTENTS)/Plugins \
		$(MACOS_PACKAGE_CONTENTS)/MacOS
	$(MKDIR) $(MACOS_PACKAGE_RESOURCES)/bin $(MACOS_PACKAGE_RESOURCES)/lib \
		$(MACOS_PACKAGE_RESOURCES)/share
	$(CP) $(MACOS_PACKAGE_SRC)/Info.plist $(MACOS_PACKAGE_CONTENTS)
	$(CP) $(MACOS_PACKAGE_SRC)/PkgInfo $(MACOS_PACKAGE_CONTENTS)
	$(CP) ATHENA/bin/athena.bin $(MACOS_PACKAGE_CONTENTS)/MacOS/ATHENA
		
	$(CP) $(MACOS_PACKAGE_SRC)/Assets.car $(MACOS_PACKAGE_RESOURCES)

	$(CP) $(MACOS_PACKAGE_SRC)/ATHENA-document.icns \
				$(MACOS_PACKAGE_RESOURCES)
	$(CP) src/Plugins/Cocoa/English.lproj $(MACOS_PACKAGE_RESOURCES)
	$(CP) misc/admin/athena_updates_dsa_pub.pem $(MACOS_PACKAGE_RESOURCES)
	$(CP) ATHENA $(MACOS_PACKAGE_RESOURCES)/share
	$(RM) $(MACOS_PACKAGE_TEXMACS)/bin/athena.bin
	if test -n "";then  $(CP)  $(MACOS_PACKAGE_TEXMACS)/bin/;fi
	find /usr/share/guile/1.8 -type d -name ice-9 -exec $(CP) {} $(MACOS_PACKAGE_TEXMACS)/progs/ \;
	$(CHMOD) -R go=rX $(MACOS_PACKAGE_TEXMACS)/progs/ice-9
	find -d $(MACOS_PACKAGE_TEXMACS) -name .svn -exec rm -rf {} \;

	@echo Deploying libraries into application bundle.
	QT_PLUGINS_PATH="$(QT_PLUGINS_PATH)" QT_PLUGINS_LIST="$(QT_PLUGINS_LIST)" \
	QT_FRAMEWORKS_PATH="$(QT_FRAMEWORKS_PATH)" \
		$(MACOS_PACKAGE_SRC)/bundle-libs.sh $(MACOS_PACKAGE_CONTENTS)/MacOS/ATHENA
	@echo ----------------------------------------------------
	@echo ATHENA has been bundled in $(MACOS_PACKAGE_APP)
	@if test -n "";then \
		codesign --timestamp --deep -s  $(MACOS_PACKAGE_APP) &&\
		echo "Bundle successfully signed" || echo "Signing failed"; fi


MACOS_PACKAGE: MACOS_BUNDLE
	$(RM) $(MACOS_PACKAGE_DMG)
	hdiutil create -srcfolder $(MACOS_PACKAGE_APP) $(MACOS_PACKAGE_DMG)
	$(MKDIR) $(MACOS_PACKAGE_DIR)
	$(MV) $(MACOS_PACKAGE_DMG) $(MACOS_PACKAGE_DIR)
	$(RM) -r $(MACOS_PACKAGE_APP)

MACOS_RELEASE: MACOS_BUNDLE
	$(RM) $(MACOS_PACKAGE_ZIP)
	(cd ../distr; zip -r9Tq $(tm_devel).zip $MACOS_PACKAGE_APP)
	$(SIGN) $(MACOS_PACKAGE_ZIP) Unversioned directory $(tm_devel_version)

.PHONY: MACOS_BUNDLE MACOS_PACKAGE MACOS_RELEASE

###############################################################################
# Make a Windows installer
###############################################################################

WINSPARKLE_DLL = 
WINSPARKLE_PATH = 

WINDOWS_PACKAGE_SRC = packages/windows
WINDOWS_PACKAGE_DIR = ../distr/windows

WINDOWS_BUILD_DIR = ../distr/ATHENA-Windows
WINDOWS_BUILD_BIN_DIR = $(WINDOWS_BUILD_DIR)/bin
UN_QT_P = $(shell cygpath -u $(QT_PLUGINS_PATH))

WINDOWS_PDB: TEXMACS
	@if [ -n "" ]; then "" "$(WINDOWS_BUILD_BIN_DIR)/athena.exe"; fi

WINDOWS_BUNDLE: TEXMACS WINDOWS_PDB
	$(MKDIR) ../distr
	$(MKDIR) $(WINDOWS_BUILD_DIR)
	$(RM) -r $(WINDOWS_BUILD_BIN_DIR)/{athena,athena.bin,athena.exe}
	$(CP) -u ATHENA/* $(WINDOWS_BUILD_DIR)/.
	$(RM) $(WINDOWS_BUILD_BIN_DIR)/athena
	$(MV) $(WINDOWS_BUILD_BIN_DIR)/athena.bin $(WINDOWS_BUILD_BIN_DIR)/athena.exe
	if test -n ""; then  $(WINDOWS_BUILD_BIN_DIR)/athena.exe; fi
	if test "" != @'XTRA_CMD'@; then $(CP) -u "/"* $(WINDOWS_BUILD_BIN_DIR)/; fi
	if [ -d "$(TMREPO)" ]; then \
		$(CP) -r $(TMREPO)/lib/aspell-* $(WINDOWS_BUILD_DIR); \
	fi
	if test -x ""; then $(CP)  $(WINDOWS_BUILD_BIN_DIR)/;fi
	for f in $(WINDOWS_BUILD_BIN_DIR)/*.exe;do $(WINDOWS_PACKAGE_SRC)/copydll.sh $$f;done
	$(CP) misc/admin/athena_updates_dsa_pub.pem $(WINDOWS_BUILD_DIR)/bin
	find /usr/share/guile/1.8 -type d -name ice-9 -exec $(CP) {} $(WINDOWS_BUILD_DIR)/progs/ \;
	if test -d "$(QT_PLUGINS_PATH)";then $(CP) -u $$(cygpath -u $(QT_PLUGINS_PATH)/*) $(WINDOWS_BUILD_BIN_DIR);fi

WINDOWS_PACKAGE: WINDOWS_BUNDLE
	$(MKDIR) $(WINDOWS_PACKAGE_DIR)
	iscc $(WINDOWS_PACKAGE_SRC)/ATHENA.iss
	if test -n ""; then  $(WINDOWS_PACKAGE_DIR)/*-installer.exe; fi

WINDOWS_APPX_CNRS: WINDOWS_BUNDLE
	@if [ -z "" ]; then \
		echo "MAKEAPPX not found, skipping Windows AppX CNRS package creation."; \
	else { \
		$(MKDIR) $(WINDOWS_BUILD_DIR); \
		$(RM) $(WINDOWS_BUILD_DIR)/AppxManifest.xml; \
		$(RM) $(WINDOWS_BUILD_DIR)/ATHENA.msix; \
		$(CP) packages/msix/AppxManifest.xml $(WINDOWS_BUILD_DIR)/AppxManifest.xml; \
		cd $(WINDOWS_BUILD_DIR); powershell.exe -Command "& '' pack /d . /p ../windows/ATHENA.msix"; \
	}; fi

WINDOWS_APPX_STORE:
	@if [ -z "" ]; then \
		echo "MAKEAPPX not found, skipping Windows AppX Store package creation."; \
	else { \
		$(MKDIR) $(WINDOWS_BUILD_DIR); \
		$(RM) $(WINDOWS_BUILD_DIR)/AppxManifest.xml; \
		$(RM) $(WINDOWS_BUILD_DIR)/ATHENAStore.msix; \
		$(CP) packages/msix/AppxManifestStore.xml $(WINDOWS_BUILD_DIR)/AppxManifest.xml; \
		cd $(WINDOWS_BUILD_DIR); \
		powershell.exe -Command "& '' createconfig /cf priconfig.xml /dq en-US"; \
		powershell.exe -Command "& '' new /pr . /cf priconfig.xml"; \
		powershell.exe -Command "& '' pack /d . /p ../windows/ATHENAStore.msix"; \
	}; fi

WINDOWS_APPX: WINDOWS_BUNDLE WINDOWS_PACKAGE WINDOWS_APPX_CNRS WINDOWS_APPX_STORE
	echo Windows AppX packages have been created in $(WINDOWS_BUILD_DIR)

.PHONY: WINDOWS_BUNDLE WINDOWS_PACKAGE

###############################################################################
# Make an Android package
###############################################################################

ANDROID_PACKAGE_SRC = packages/android
ANDROID_PACKAGE_DIR = ../distr/android

ANDROID_BUILD_DIR = ../distr/ATHENA-Android

ANDROID_LIBTEXMACS:  EMPTY_DIRS
	if test "$(ATHENA_DEPS)" != "no"; then \
		cd src; $(MAKE) -f makefile deps; \
	fi
	cd src; $(MAKE) -r -f makefile link=static Objects/libathena.a
	$(CP) plugins $(tmdir)
	$(RM) $(tmdir)/plugins/*/Makefile
	$(RM) -r $(tmdir)/plugins/*/src
	$(RM) -r $(tmdir)/plugins/*/bin
	$(RM) -r $(tmdir)/plugins/*/lib
	@echo ----------------------------------------------------------
	@echo ATHENA library has been successfully compiled for Android

ANDROID_BUNDLE: ANDROID_LIBTEXMACS
	$(MKDIR) ../distr
	$(RM) -r $(ANDROID_BUILD_DIR)/sources
	$(MKDIR) $(ANDROID_BUILD_DIR)/sources
	$(RM) -r $(ANDROID_BUILD_DIR)/build
	$(MKDIR) $(ANDROID_BUILD_DIR)/build
	$(CP) -r packages/android/launcher/* $(ANDROID_BUILD_DIR)/sources/
	$(CP) $(ANDROID_PACKAGE_SRC)/res/* $(ANDROID_BUILD_DIR)/sources/android/res/
	$(CP) $(ANDROID_PACKAGE_SRC)/AndroidManifest.xml $(ANDROID_BUILD_DIR)/sources/android/AndroidManifest.xml
	$(ANDROID_PACKAGE_SRC)/collect_assets.sh ATHENA $(ANDROID_BUILD_DIR)/sources/android/assets/raw
	$(MKDIR) $(ANDROID_BUILD_DIR)/build/libs/${ANDROID_ABI}

ANDROID_AAB: ANDROID_BUNDLE
	$(CP) src/Objects/libathena.a $(ANDROID_BUILD_DIR)/sources
	cd $(ANDROID_BUILD_DIR)/build; cmake -DANDROID_NDK:PATH= \
										 -DANDROID_SDK_ROOT:PATH= \
										 -DANDROID_STL:STRING=c++_shared \
										 -DCMAKE_GENERATOR:STRING=Ninja \
										 -DANDROID_ABI:STRING= \
										 -DQT_HOST_PATH:PATH= \
										 -DCMAKE_BUILD_TYPE:STRING=Release \
										 -DANDROID_PLATFORM:STRING=android-35 \
										 -DQT_USE_TARGET_ANDROID_BUILD_DIR:BOOL=ON \
										 -DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL:BOOL=ON \
										 -DCMAKE_TOOLCHAIN_FILE:FILEPATH=/build/cmake/android.toolchain.cmake \
										 -DANDROID_USE_LEGACY_TOOLCHAIN_FILE:BOOL=OFF \
										 -DANDROID_TARGET_SDK_VERSION=35 \
										 -DCMAKE_FIND_ROOT_PATH=/usr/bin \
										 -DCMAKE_PREFIX_PATH=/usr/bin \
										 -DATHENA_LIBS:STRING="-lz -lpng -Wl,--as-needed -rdynamic -L/usr/lib64 -L/usr/lib /usr/lib64/libQt5PrintSupport.so /usr/lib64/libQt5Svg.so /usr/lib64/libQt5Widgets.so /usr/lib64/libQt5Gui.so /usr/lib64/libQt5Network.so /usr/lib64/libQt5Core.so -lGL -lpthread -lfreetype -lguile -lgmp -lcrypt -lm -lltdl" \
										 -DQT_DIR=/usr/bin/lib/cmake/Qt6 ../sources \
										 -DQt6Core_DIR=/usr/bin/lib/cmake/Qt6Core \
										 -DQt6CoreTools_DIR=/qtbase/lib/cmake/Qt6CoreTools \
										 -DQt6Gui_DIR=/usr/bin/lib/cmake/Qt6Gui \
										 -DQt6GuiTools_DIR=/qtbase/lib/cmake/Qt6GuiTools \
										 -DQt6Widgets_DIR=/usr/bin/lib/cmake/Qt6Widgets \
										 -DQt6WidgetsTools_DIR=/qtbase/lib/cmake/Qt6WidgetsTools \
										 -DQt6Svg_DIR=/usr/bin/lib/cmake/Qt6Svg \
										 -DQt6PrintSupport_DIR=/usr/bin/lib/cmake/Qt6PrintSupport \
										 -DQt6Concurrent_DIR=/usr/bin/lib/cmake/Qt6Concurrent \
										 -DQt6Network_DIR=/usr/bin/lib/cmake/Qt6Network
	cd $(ANDROID_BUILD_DIR)/build; cmake --build . --config Release --target aab

ANDROID_DEV_APK: ANDROID_BUNDLE
	$(CP) src/Objects/libathena.a $(ANDROID_BUILD_DIR)/sources
	cd $(ANDROID_BUILD_DIR)/build; cmake -DANDROID_NDK:PATH= \
										 -DANDROID_SDK_ROOT:PATH= \
										 -DANDROID_STL:STRING=c++_shared \
										 -DCMAKE_GENERATOR:STRING=Ninja \
										 -DANDROID_ABI:STRING= \
										 -DQT_HOST_PATH:PATH= \
										 -DCMAKE_BUILD_TYPE:STRING=Debug \
										 -DANDROID_PLATFORM:STRING=android-35 \
										 -DQT_USE_TARGET_ANDROID_BUILD_DIR:BOOL=ON \
										 -DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL:BOOL=ON \
										 -DCMAKE_TOOLCHAIN_FILE:FILEPATH=/build/cmake/android.toolchain.cmake \
										 -DANDROID_USE_LEGACY_TOOLCHAIN_FILE:BOOL=OFF \
										 -DANDROID_TARGET_SDK_VERSION=35 \
										 -DCMAKE_FIND_ROOT_PATH=/usr/bin \
										 -DCMAKE_PREFIX_PATH=/usr/bin \
										 -DATHENA_LIBS:STRING="-lz -lpng -Wl,--as-needed -rdynamic -L/usr/lib64 -L/usr/lib /usr/lib64/libQt5PrintSupport.so /usr/lib64/libQt5Svg.so /usr/lib64/libQt5Widgets.so /usr/lib64/libQt5Gui.so /usr/lib64/libQt5Network.so /usr/lib64/libQt5Core.so -lGL -lpthread -lfreetype -lguile -lgmp -lcrypt -lm -lltdl" \
										 -DQT_DIR=/usr/bin/lib/cmake/Qt6 ../sources \
										 -DQt6Core_DIR=/usr/bin/lib/cmake/Qt6Core \
										 -DQt6CoreTools_DIR=/qtbase/lib/cmake/Qt6CoreTools \
										 -DQt6Gui_DIR=/usr/bin/lib/cmake/Qt6Gui \
										 -DQt6GuiTools_DIR=/qtbase/lib/cmake/Qt6GuiTools \
										 -DQt6Widgets_DIR=/usr/bin/lib/cmake/Qt6Widgets \
										 -DQt6WidgetsTools_DIR=/qtbase/lib/cmake/Qt6WidgetsTools \
										 -DQt6Svg_DIR=/usr/bin/lib/cmake/Qt6Svg \
										 -DQt6PrintSupport_DIR=/usr/bin/lib/cmake/Qt6PrintSupport \
										 -DQt6Concurrent_DIR=/usr/bin/lib/cmake/Qt6Concurrent \
										 -DQt6Network_DIR=/usr/bin/lib/cmake/Qt6Network
	cd $(ANDROID_BUILD_DIR)/build; cmake --build . --config Debug --target apk

ANDROID_ALL: ANDROID_DEV_APK ANDROID_AAB
	echo "Android APK and AAB have been built successfully"

.PHONY: ANDROID_BUNDLE

###############################################################################
# Make an AppImage
###############################################################################

APPIMAGE: TEXMACS
	# If TMREPO is empty, throw an error
	if [ -z "$(TMREPO)" ]; then \
		echo "TMREPO is empty. AppImage can only be built with ATHENA builder."; \
		echo "See https://www.athena.org/tmweb/download/sources.en.html"; \
		exit 1; \
	fi

	# Create the AppDir folder structure
	$(MKDIR) ../distr
	$(MKDIR) ../distr/ATHENA.AppDir
	$(RM) -r ../distr/ATHENA.AppDir/*
	$(MKDIR) ../distr/ATHENA.AppDir/usr
	$(MKDIR) ../distr/ATHENA.AppDir/usr/bin
	$(MKDIR) ../distr/ATHENA.AppDir/usr/lib
	$(MKDIR) ../distr/ATHENA.AppDir/usr/share
	$(MKDIR) ../distr/ATHENA.AppDir/usr/plugins
	$(MKDIR) ../distr/ATHENA.AppDir/usr/share/metainfo	
	$(MKDIR) ../distr/ATHENA.AppDir/usr/share/applications

	
	# Install ATHENA inside the AppDir
	$(MAKE) INSTALL_EXECUTABLES DESTDIR=../distr/ATHENA.AppDir/usr
	$(MAKE) INSTALL_DATA DESTDIR=../distr/ATHENA.AppDir/usr/share/ATHENA
	$(MAKE) INSTALL_PLUGINS DESTDIR=../distr/ATHENA.AppDir/usr/share/ATHENA
	$(MAKE) INSTALL_ICONS DESTDIR=../distr/ATHENA.AppDir/usr/share/ATHENA/misc
	$(MAKE) INSTALL_STARTUP DESTDIR=../distr/ATHENA.AppDir/usr/bin
	$(MAKE) INSTALL_INCLUDE DESTDIR=../distr/ATHENA.AppDir/usr/include
	
	# Copy ice-9 files inside the ATHENA progs folder
	$(CP) -r `find /usr/share/guile/1.8 -type d | grep ice-9` ../distr/ATHENA.AppDir/usr/share/ATHENA/progs
	
	# Copy the AppImage specific files
	$(CP) packages/appimage/org.athena.athena.metainfo.xml ../distr/ATHENA.AppDir/usr/share/metainfo/org.athena.athena.metainfo.xml
	$(CP) packages/appimage/org.athena.athena.desktop ../distr/ATHENA.AppDir/usr/share/applications/org.athena.athena.desktop
	$(CP) packages/appimage/org.athena.athena.desktop ../distr/ATHENA.AppDir/org.athena.athena.desktop	
	$(CP) ../distr/ATHENA.AppDir/usr/share/ATHENA/misc/images/athena.svg ../distr/ATHENA.AppDir/athena.svg
	$(CP) ../distr/ATHENA.AppDir/usr/share/ATHENA/misc/images/athena.svg ../distr/ATHENA.AppDir/.DirIcon

	# Remove the bash executable of ATHENA
	$(RM) ../distr/ATHENA.AppDir/usr/bin/athena

	# Move the athena binary to AppRun
	# $(MV) ../distr/ATHENA.AppDir/usr/bin/athena.bin ../distr/ATHENA.AppDir/AppRun
	$(CP) packages/appimage/AppRun ../distr/ATHENA.AppDir/AppRun
	chmod +x ../distr/ATHENA.AppDir/AppRun

	# Use patchelf to fix the rpath
	patchelf --force-rpath --set-rpath '$$ORIGIN:$$ORIGIN/../lib:$$ORIGIN/usr/lib:../lib:usr/lib' ../distr/ATHENA.AppDir/usr/bin/athena.bin

	# Install dependencies from the ATHENA builder
	$(CP) -Lr $(TMREPO)/lib/*.so* ../distr/ATHENA.AppDir/usr/lib
	$(CP) -Lr $(TMREPO)/plugins ../distr/ATHENA.AppDir/usr
	$(CP) -Lr $(TMREPO)/translations ../distr/ATHENA.AppDir/usr
	$(CP) -Lr $(TMREPO)/usr/optional ../distr/ATHENA.AppDir/usr

	# Pack the AppImage (offline if APPIMAGE_RUNTIME is provided)
	@if [ -n "$$APPIMAGE_RUNTIME" ]; then \
		echo "Using custom AppImage runtime: $$APPIMAGE_RUNTIME"; \
		appimagetool --runtime-file "$$APPIMAGE_RUNTIME" ../distr/ATHENA.AppDir ../distr/ATHENA.AppImage; \
	else \
		appimagetool ../distr/ATHENA.AppDir ../distr/ATHENA.AppImage; \
	fi



###############################################################################
# Cleaning and backups
###############################################################################

RDISTR:
	-$(RM) $(tmdir)/TEX_PATHS
	find . -name '*~' -o -name core -delete
	-$(RM) $(tmdir)/lib/*.a
	-$(RM) $(tmdir)/fonts/error/* 2>/dev/null
	-$(RM) -r autom*.cache

DISTR: RDISTR
	cd src; $(MAKE) -f makefile DISTR

RCLEAN: RDISTR
	$(RM) $(tmdir)/examples/plugins/bin/* 2>/dev/null || :
	$(RM) -r $(tmdir)/plugins
	$(RM) $(tmdir)/lib/* 2>/dev/null || :
	$(RM) $(tmdir)/bin/* 2>/dev/null || :
	$(RM) -r $(tmdir)/misc/images/.xvpics
	$(RM) -r $(tmdir)/progs/ice-9
	$(RM) -r X11

SCLEAN:
	cd src; $(MAKE) -f makefile CLEAN

CLEAN: SCLEAN RCLEAN CLEAN_PLUGINS

DISTCLEAN: CLEAN 
	$(RM) src/Objects/* 2>/dev/null || :
	$(RM) misc/doxygen/Doxyfile
	$(RM) misc/man/athena.1
	$(RM) misc/scripts/fig2ps
	$(RM) misc/scripts/athena
	$(RM) src/System/config.h
	$(RM) src/System/tm_configure.hpp
	$(RM) src/makefile
	$(RM) ATHENA/examples/plugins/dynlink/Makefile
	$(RM) config.cache config.log config.status
	$(RM) Makefile  ${verinfo} misc/admin/appcast.xml
	$(RM) -r $(WINDOWS_BUILD_DIR)

SVN_CLEAN:
	$(RM) -r .svn
	$(RM) -r */.svn
	$(RM) -r */*/.svn
	$(RM) -r */*/*/.svn
	$(RM) -r */*/*/*/.svn
	$(RM) -r */*/*/*/*/.svn
	$(RM) -r */*/*/*/*/*/.svn
	$(RM) -r */*/*/*/*/*/*/.svn 
	$(RM) -r */*/*/*/*/*/*/*/.svn

DISTR_CLEAN: DISTCLEAN SVN_CLEAN

.PHONY: CLEAN_DOC RDISTR DISTR RCLEAN SCLEAN DISTCLEAN SVN_CLEAN DISTR_CLEAN

CLNGUILE:
	-cd  && $(MAKE) uninstall
	-cd  && $(MAKE) distclean
	-cd  && rm -rf build

###############################################################################
# Miscellaneous targets
###############################################################################

TOUCH:
	$(TOUCH) */*.make
	$(TOUCH) */*/*.hpp
	$(TOUCH) */*/*/*.hpp
	$(TOUCH) */*.cpp
	$(TOUCH) */*/*.cpp
	$(TOUCH) */*/*/*.cpp
	$(TOUCH) */*/*/*/*.cpp

STRIP:
	$(STRIP) $(tmdir)/bin/athena.bin
	$(STRIP) $(tmdir)/lib/*.$(so) 2>/dev/null || >/dev/null
	$(STRIP) $(tmdir)/plugins/*/bin/* 2>/dev/null || >/dev/null

ACCESS_FLAGS:
	$(MKDIR) -p $(tmdir)/bin
	$(MKDIR) -p $(tmdir)/lib
	$(CHMOD) -R go+rX *
	$(CHMOD) -R go+x $(tmdir)/bin
	$(CHMOD) -R go+x $(tmdir)/lib

VERINFO:
	@{ date; uname -srvn ; printf "\nRevision  $SVNREV@  "; \
	egrep '\$$.+configure' config.log; \
	gcc --version; \
	svn -v st | misc/admin/verinfo.sh; \
	printf "\n**** DIFF ****\n\n"; \
	svn diff;  }> ${verinfo}

.PHONY: TOUCH STRIP ACCESS_FLAGS
