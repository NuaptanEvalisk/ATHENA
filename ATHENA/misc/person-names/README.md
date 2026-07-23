# Bundled Person Names

`wikidata-person-names.tsv` is a generated offline snapshot of complete English
person labels and `P734` family names from Wikidata. It is used only as a
recognition vocabulary: ATHENA stores semantic person information in document
trees as `<person|...>`. Family names use stricter recognition rules than full
labels. Wikidata structured data is available under CC0.

The snapshot can be reproduced with
`tools/person-names/update_wikidata_person_names.py`. Existing semantic person
tags in a document or vault augment this vocabulary, allowing users to tag
people outside the bundled scholarly set without editing this generated file.
