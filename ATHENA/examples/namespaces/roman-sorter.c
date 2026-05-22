/*
 * Sorter for namespaces whose template captures one Roman numeral field.
 *
 * Example template:
 *   %R
 *
 * Captures:
 *   a[0], b[0]: Roman numeral
 */

int
athena_ns_compare (int n, const AthenaNsField* a, const AthenaNsField* b) {
  if (n < 1) return 0;
  return athena_ns_cmp_roman (a[0].roman, b[0].roman);
}
