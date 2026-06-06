/*
 * Sorter for assignment namespaces.
 *
 * Template:
 *   %w Assignments %s %R
 *
 * Captures:
 *   a[0], b[0]: course word
 *   a[1], b[1]: assignment label
 *   a[2], b[2]: assignment number as a Roman numeral
 *
 * As with lec-sorter.c, files for different course words are equal under this
 * sorter. Within the same course, assignments are ordered by label and then by
 * Roman number.
 */

int
athena_ns_compare (int n, const AthenaNsField* a, const AthenaNsField* b) {
  if (n < 3) return 0;

  if (athena_ns_strcmp (a[0].text, b[0].text) != 0)
    return 0;

  int label_cmp= athena_ns_strcmp (a[1].text, b[1].text);
  if (label_cmp != 0) return label_cmp;

  return athena_ns_cmp_roman (a[2].roman, b[2].roman);
}
