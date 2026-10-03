-- Direct-comparator example. Prefer key(fields) when a lexicographic key exists;
-- compare(a,b) is validated against the current member set before publication.
return {
    version = 1,
    compare = function(a, b)
        return athena.byte_compare(a[1].text, b[1].text)
    end,
}
