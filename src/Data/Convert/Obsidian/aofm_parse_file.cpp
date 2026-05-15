#include <peglib.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "convert.hpp"
#include "file.hpp"
#include "new_data.hpp"
#include "tm_configure.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "aofm_telemetry.hpp"

#include "aofm_utils.hpp"
#include "aofm_ast_helpers.hpp"
#include "aofm_metadata.hpp"
#include "aofm_tree_utils.hpp"
#include "aofm_math.hpp"
#include "aofm_callouts.hpp"
#include "aofm_inline.hpp"
#include "aofm_blocks.hpp"

#include <chrono>

extern const char* aofm_grammar;

double time_track = 0.0;
double time_simplify = 0.0;
double time_normalize = 0.0;
double time_latex_mark = 0.0;
double time_doc_to_tree = 0.0;
double time_group_markers = 0.0;
double time_other = 0.0;

double time_parse_latex_doc = 0.0;
double time_latex_to_tree = 0.0;
double aofm_math_time = 0.0;
int aofm_math_count = 0;

double time_l2t_kill_space = 0.0;
double time_l2t_parsed_latex = 0.0;
double time_l2t_finalize_doc = 0.0;
double time_l2t_handle_matches = 0.0;
double time_l2t_upgrade_tex = 0.0;
double time_l2t_finalize_misc = 0.0;
double time_l2t_drd_correct = 0.0;
double time_l2t_style_check = 0.0;
double time_l2t_simplify_correct = 0.0;
double time_l2t_latex_correct = 0.0;
double time_l2t_guess_missing = 0.0;
double time_l2t_post_metadata = 0.0;

int count_l2t_is_document = 0;
int count_l2t_total = 0;

using namespace aofm;

namespace aofm {
  std::string aofm_content;
}

std::shared_ptr<peg::Ast>
aofm_parse_file(const std::string& file_path) {
    peg::parser parser(aofm_grammar);
    if (!parser) {
        report_aofm_error("failed to initialize A-OFM grammar");
        return nullptr;
    }

    parser.enable_ast();
    parser.set_logger([file_path](size_t line, size_t col, const std::string& msg,
                                  const std::string& rule) {
        report_aofm_parse_error(file_path, line, col, msg, rule);
    });

    std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        report_aofm_error("could not open file: " + file_path);
        return nullptr;
    }

    std::string raw((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());
    
    aofm_content = normalize_markdown_lines(raw);
    aofm_content = normalize_transclusion_lines(aofm_content);
    aofm_content = extract_aofm_document_metadata(aofm_content);
    aofm_content = preprocess_isolated_callout_proofs(aofm_content);
    aofm_content = sanitize_markdown_blocks(aofm_content);

    if (!aofm_content.empty() && aofm_content.back() != '\n') {
        aofm_content += '\n';
    }

    std::shared_ptr<peg::Ast> ast;
    if (parser.parse(aofm_content, ast, file_path.c_str())) {
        return ast;
    }

    report_aofm_error("parsing failed for file: " + file_path);
    return nullptr;
}

tree
aofm_ast_to_texmacs_document(const AstPtr& ast) {
    tree body = convert_block(ast);
    body = sanitize_proof_trees(body);
    if (!is_document(body)) body = document(body);

    new_data data;
    if (!aofm_metadata.created_time.empty()) {
        data->init("global-created-time") = tree(tm_string(aofm_metadata.created_time));
    }
    if (!aofm_metadata.modified_time.empty()) {
        data->init("global-modified-time") = tree(tm_string(aofm_metadata.modified_time));
    }
    if (!aofm_metadata.content_hash.empty()) {
        data->init("global-content-hash") = tree(tm_string(aofm_metadata.content_hash));
    }
    return attach_data(body, data, true);
}

std::string
aofm_output_path_for(const std::string& file_path) {
    size_t dot = file_path.find_last_of('.');
    size_t slash = file_path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return file_path + ".ath";
    }
    return file_path.substr(0, dot) + ".ath";
}

bool
aofm_convert_file(const std::string& file_path,
                  const std::string& output_path) {
    aofm_math_time = 0.0;
    aofm_math_count = 0;

    tree doc;
    if (!aofm_convert_tree(tm_string(file_path), doc, true)) return false;

    string serialized = tree_to_texmacs(doc);

    if (save_string(url_system(std_to_tm_string(output_path)), serialized)) {
        report_aofm_error("failed to save output to: " + output_path);
        return false;
    }

    if (aofm_math_count > 0) {
        std::cout << "AOFM] Math processing: " << aofm_math_time << "s (" << aofm_math_count << " formulas, avg " << (aofm_math_time / aofm_math_count) << "s)" << std::endl;
    }

    return true;
}

bool
aofm_convert_tree(string file_path, tree& document, bool materialize_anchor_literals) {
    auto ast = aofm_parse_file(as_charp(file_path));
    if (!ast) return false;

    document = aofm_ast_to_texmacs_document(ast);
    if (materialize_anchor_literals) {
        document = materialize_aofm_anchor_literals(document);
    }
    return true;
}

void
aofm_debug_dump(const std::string& file_path) {
    time_parse_latex_doc = 0.0;
    time_latex_to_tree = 0.0;
    aofm_math_time = 0.0;
    aofm_math_count = 0;

    tree doc;
    if (!aofm_convert_tree(tm_string(file_path), doc, true)) {
        report_aofm_error("conversion failed for file: " + file_path);
        return;
    }

    string serialized = tree_to_texmacs(doc);
    std::string output_path = aofm_output_path_for(file_path);

    std::cout << "--- ATH DUMP BEGIN ---" << std::endl;
    std::cout << as_charp(serialized) << std::endl;
    std::cout << "--- ATH DUMP END ---" << std::endl;
    std::cout << "Saved to: " << output_path << std::endl;
}
