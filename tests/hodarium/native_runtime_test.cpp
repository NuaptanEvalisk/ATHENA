/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "athena_document_xml.hpp"
#include "interop_document_source.hpp"
#include "node_metadata.hpp"
#include "vars.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}
int reports= 0;
void report (const char* message) {
  check (std::string (message) == "native-runtime-test", "Exception diagnostic changed");
  ++reports;
}
}

int main () {
  try {
    init_standard_tree_labels ();
    std::thread again ([] { init_standard_tree_labels (); });
    again.join ();
    check (as_tree_label ("document") == DOCUMENT, "Document label mismatch");
    check (as_tree_label ("concat") == CONCAT, "Concat label mismatch");
    check (as_tree_label ("named-symbol") == NAMED_SYMBOL, "Named-symbol label mismatch");
    check (as_string (RAW_DATA) == "raw-data", "Raw-data label mismatch");
    check (existing_tree_label ("enunciation"), "Missing extension tag");
    check (existing_tree_label (FONT), "Missing environment variable label");

    tree source (DOCUMENT, compound ("body", tree (CONCAT, "native ", "XML")));
    athena::node::metadata metadata;
    metadata.id= "11111111-1111-4111-8111-111111111111";
    metadata.properties["test:preserved"]= athena::node::property (std::string ("value"));
    athena::node::set (source[0][0], metadata);
    auto encoded= athena::document::write_xml_v2 (source);
    auto decoded= athena::document::read_xml_v2 (encoded);
    check (decoded == source, "XML v2 tree round trip changed content");
    check (athena::node::equal_metadata (decoded[0][0], source[0][0]),
           "XML v2 metadata round trip changed content");
    check (interop_document_source_error (decoded).empty (), "Valid document rejected");
    check (!interop_document_source_error (tree (CONCAT)).empty (), "Invalid root accepted");
    check (!interop_document_source_error (tree (DOCUMENT,
      compound ("body", "a"), compound ("body", "b"))).empty (), "Duplicate body accepted");

    string output;
    tm_ostream stream= string_ostream (output);
    stream << "native " << 42;
    stream.flush ();
    check (output == "native 42", "Native stream formatting changed");
    debug_set (string ("io"), true);
    check (debug_get (string ("io")), "Debug flag was not set");
    int saved= debug_off ();
    check (!debug_get (string ("io")), "Debug flags were not cleared");
    debug_on (saved);
    check (debug_get (string ("io")), "Debug flags were not restored");
    debug_off ();
    set_tm_exception_reporter (report);
    bool caught= false;
    try { tm_throw ("native-runtime-test"); }
    catch (const string& error) { caught= error == "native-runtime-test"; }
    set_tm_exception_reporter (nullptr);
    check (caught && reports == 1, "Exception/reporting contract changed");
    std::cout << "Native runtime, standard labels, and XML v2 verified\n";
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
