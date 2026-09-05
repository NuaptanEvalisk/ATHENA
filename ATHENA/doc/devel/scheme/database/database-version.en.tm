<TeXmacs|1.99.2>

<style|tmdoc>

<\body>
  <tmdoc-title|Version management>

  Sometimes, a new or better version for a database entry becomes available.
  This happens for instance, when the value of some field needs to be
  corrected, or when a paper in a bibliographic database gets published. In
  <scm|db-version.scm>, we introduce a few additional attributes and routines
  for version management of database entries.

  ATHENA resolves import conflicts between entries with the same name in the
  selected database, without contributor identities. Explicit version history,
  manual-edit precedence and timestamps determine whether an incoming entry
  replaces an existing one. The <scm|newer> attribute records the identifiers
  of superseded entries.

  <paragraph|Special attributes>

  <\description>
    <item*|<scm|modus>>Specifies the way the entry was contributed
    (<scm|manual> or <scm|imported>).

    <item*|<scm|origin>>A source file in the case when the entry was
    imported.

    <item*|<scm|newer>>Specifies a list of older versions of a given entry.
  </description>

  <paragraph|Useful routines>

  <\explain>
    <scm|(db-update-entry id l)><explain-synopsis|update an entry>
  <|explain>
    Create a new version of the entry with identifier <scm|id> (the new
    version having fields <scm|l>) and return the identifier of the new
    entry.
  </explain>

  <\explain>
    <scm|(db-import-entry id l)><explain-synopsis|import an entry>
  <|explain>
    Given an entry <scm|id> with fields <scm|l>, typically from another
    database, import the entry into our current database. Do the necessary
    whenever newer versions of this entry already exist in the database (in
    which case the import is cancelled), or whenever the entry supersedes
    existing entries.
  </explain>

  <tmdoc-copyright|2015|Joris van der Hoeven>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>
