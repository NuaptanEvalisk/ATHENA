/*
 * Sorter for reading namespaces.
 *
 * Template:
 *   %w %N %s
 *
 * Captures:
 *   a[0], b[0]: course or reading group word
 *   a[1], b[1]: positive integer reading number
 *   a[2], b[2]: reading title
 *
 * Files for different group words are equal under this sorter. Within the same
 * group, readings are ordered by their numeric %N field. The title is ignored.
 */

int
athena_ns_compare (int n, const AthenaNsField* a, const AthenaNsField* b) {
  if (n < 3) return 0;

  if (athena_ns_strcmp (a[0].text, b[0].text) != 0)
    return 0;

  return athena_ns_cmp_int (a[1].integer, b[1].integer);
}
