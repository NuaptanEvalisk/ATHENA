/*
 * Sorter for namespace "Lec".
 *
 * Template:
 *   %w Lecture Notes %R
 *
 * Captures:
 *   a[0], b[0]: course word
 *   a[1], b[1]: lecture number as a Roman numeral
 *
 * Per "Namespaces in ATHENA.md": lecture notes for different course words are
 * equal under this namespace sorter; lecture notes for the same course word are
 * ordered by their Roman lecture number.
 */

int
athena_ns_compare (int n, const AthenaNsField* a, const AthenaNsField* b) {
  if (n < 2) return 0;

  if (athena_ns_strcmp (a[0].text, b[0].text) != 0)
    return 0;

  return athena_ns_cmp_roman (a[1].roman, b[1].roman);
}
