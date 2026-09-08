## Process this file with automake to produce Makefile.in.
##
##  	Copyright (C) 1998, 1999, 2000, 2001, 2002, 2006, 2007,
##        2008, 2009, 2010, 2011, 2012, 2013,
##        2014, 2015, 2016, 2022, 2023 Free Software Foundation, Inc.
##
##   This file is part of GUILE.
##
##   GUILE is free software; you can redistribute it and/or modify it
##   under the terms of the GNU Lesser General Public License as
##   published by the Free Software Foundation; either version 3, or
##   (at your option) any later version.
##
##   GUILE is distributed in the hope that it will be useful, but
##   WITHOUT ANY WARRANTY; without even the implied warranty of
##   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##   GNU Lesser General Public License for more details.
##
##   You should have received a copy of the GNU Lesser General Public
##   License along with GUILE; see the file COPYING.LESSER.  If not,
##   write to the Free Software Foundation, Inc., 51 Franklin Street,
##   Fifth Floor, Boston, MA 02110-1301 USA

# ATHENA builds an embedded Scheme runtime and its batch compiler.
AUTOMAKE_OPTIONS = 1.12 foreign
SUBDIRS = lib meta libguile module stage0 stage1 stage2 test-suite
DIST_SUBDIRS = $(SUBDIRS) prebuilt

libguileincludedir = $(pkgincludedir)/$(GUILE_EFFECTIVE_VERSION)
libguileinclude_HEADERS = libguile.h
noinst_HEADERS = lib/glthread/lock.h

# Gnulib's lock header needs this before lib/ is compiled.
BUILT_SOURCES = libguile/scmconfig.h
libguile/scmconfig.h:
	$(MAKE) -C libguile scmconfig.h

EXTRA_DIST = LICENSE ATHENA-UPSTREAM.md GUILE-VERSION \
  am/guilec m4/gnulib-cache.m4 .version \
  gnulib-local/lib/localcharset.h.diff \
  gnulib-local/lib/localcharset.c.diff \
  gnulib-local/m4/clock_time.m4.diff \
  gnulib-local/build-aux/git-version-gen.diff gdbinit

ACLOCAL_AMFLAGS = -I m4
DISTCLEANFILES = check-guile.log
DISTCHECK_CONFIGURE_FLAGS = --enable-error-on-warning --enable-mini-gmp
CONFIG_STATUS_DEPENDENCIES = GUILE-VERSION

clean-local:
	rm -rf cache/

BUILT_SOURCES += $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version
