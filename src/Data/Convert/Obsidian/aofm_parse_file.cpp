#include <peglib.h>
#include <fstream>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

extern const char* aofm_grammar;

std::shared_ptr<peg::Ast> aofm_parse_file(const std::string& file_path) {
    peg::parser parser(aofm_grammar);
    
    if (!parser) {
        std::cerr << "aofm_parse_file: Failed to initialize parser from aofm_grammar" << std::endl;
        return nullptr;
    }

    parser.enable_ast();

    // Set logger for detailed error reporting
    parser.set_logger([](size_t line, size_t col, const std::string& msg, const std::string& rule) {
        std::cerr << "AOFM Parse Error at " << line << ":" << col 
                  << " (Rule: " << rule << "): " << msg << std::endl;
    });

    std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "aofm_parse_file: Could not open file: " << file_path << std::endl;
        return nullptr;
    }

    // Static to keep memory alive for string_view in AST nodes
    static std::string content;
    content.assign((std::istreambuf_iterator<char>(ifs)),
                   (std::istreambuf_iterator<char>()));
    
    if (!content.empty() && content.back() != '\n') {
        content += '\n';
    }

    std::shared_ptr<peg::Ast> ast;
    if (parser.parse(content, ast, file_path.c_str())) {
        std::cout << "aofm_parse_file: SUCCESS" << std::endl;
        return parser.optimize_ast(ast);
    } else {
        std::cerr << "aofm_parse_file: Parsing failed for file: " << file_path << std::endl;
        return nullptr;
    }
}

void aofm_dump_ast(std::shared_ptr<peg::Ast> ast) {
    if (ast) {
        std::cout << "--- AST DUMP BEGIN ---" << std::endl;
        std::cout << peg::ast_to_s(ast) << std::endl;
        std::cout << "--- AST DUMP END ---" << std::endl;
        std::cout.flush();
    } else {
        std::cout << "aofm_dump_ast: AST is null" << std::endl;
    }
}

void aofm_debug_dump(const std::string& file_path) {
    auto ast = aofm_parse_file(file_path);
    aofm_dump_ast(ast);
}
