## Process this file with automake to produce Makefile.in.
##
##   Copyright (C) 2009-2022 Free Software Foundation, Inc.
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

include $(top_srcdir)/am/bootstrap.am

moddir = $(pkgdatadir)/$(GUILE_EFFECTIVE_VERSION)
nobase_mod_DATA = $(SOURCES) $(NOCOMP_SOURCES)
EXTRA_DIST = $(SOURCES) $(NOCOMP_SOURCES)
ETAGS_ARGS = $(SOURCES) $(NOCOMP_SOURCES)

# Don't build .go files here.
nobase_noinst_DATA =

## ice-9/test.scm is not currently installed.
EXTRA_DIST +=					\
  ice-9/test.scm				\
  ice-9/compile-psyntax.scm

ETAGS_ARGS +=					\
  ice-9/test.scm				\
  ice-9/compile-psyntax.scm

ice-9/psyntax-pp.scm.gen:
	$(top_builddir_absolute)/meta/guile --no-auto-compile -s $(srcdir)/ice-9/compile-psyntax.scm \
		$(srcdir)/ice-9/psyntax.scm $(srcdir)/ice-9/psyntax-pp.scm
.PHONY: ice-9/psyntax-pp.scm.gen
