
#include "Module.hpp"
#include "PassManager.hpp"
#include "ast.hpp"
#include "cminusf_builder.hpp"
#include "CodeGen.hpp"
#include "PassManager.hpp"
#include "DeadCode.hpp"
#include "Mem2Reg.hpp"
#include "LoopDetection.hpp"
#include "LICM.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using std::string;
using std::operator""s;

struct Config {
    string exe_name; // compiler exe name
    std::filesystem::path input_file;
    std::filesystem::path output_file;

    bool emitast{false};
    bool emitasm{false};
    bool emitllvm{false};
    // optization conifg
    bool mem2reg{false};
    bool licm{false};

    Config(int argc, char **argv) : argc(argc), argv(argv) {
        parse_cmd_line();
        check();
    }

  private:
    int argc{-1};
    char **argv{nullptr};

    void parse_cmd_line();
    void check();
    // print helper infomation and exit
    void print_help() const;
    void print_err(const string &msg) const;
};

int main(int argc, char **argv) {
    Config config(argc, argv);

    auto syntax_tree = parse(config.input_file.c_str());
    auto ast = AST(syntax_tree);

    if (config.emitast) { // if emit ast (lab1), print ast and return
        ASTPrinter printer;
        ast.run_visitor(printer);
    } else {
        std::unique_ptr<Module> m;
        CminusfBuilder builder;
        ast.run_visitor(builder);
        m = builder.getModule();

        PassManager PM(m.get());
        // optimization 
        if(config.mem2reg) {
            PM.add_pass<Mem2Reg>();
            PM.add_pass<DeadCode>();
        }
        if(config.licm) {
            PM.add_pass<LoopInvariantCodeMotion>();
            PM.add_pass<DeadCode>();
        }
        PM.run();

        std::ofstream output_stream(config.output_file);
        if (config.emitllvm) {
            auto abs_path = std::filesystem::canonical(config.input_file);
            output_stream << "; ModuleID = 'cminus'\n";
            output_stream << "source_filename = " << abs_path << "\n\n";
            output_stream << m->print();
        } else if (config.emitasm) {
            CodeGen codegen(m.get());
            codegen.run();
            output_stream << codegen.print();
        }
    }

    return 0;
}

void Config::parse_cmd_line() {
    exe_name = argv[0];
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == "-h"s || argv[i] == "--help"s) {
            print_help();
        } else if (argv[i] == "-o"s) {
            if (output_file.empty() && i + 1 < argc) {
                output_file = argv[i + 1];
                i += 1;
            } else {
                print_err("bad output file");
            }
        } else if (argv[i] == "-emit-ast"s) {
            emitast = true;
        } else if (argv[i] == "-S"s) {
            emitasm = true;
        } else if (argv[i] == "-emit-llvm"s) {
            emitllvm = true;
        } else if (argv[i] == "-mem2reg"s) {
            mem2reg = true;
        } else if (argv[i] == "-licm"s) {
            licm = true;
        }else {
            if (input_file.empty()) {
                input_file = argv[i];
            } else {
                string err =
                    "unrecognized command-line option \'"s + argv[i] + "\'"s;
                print_err(err);
            }
        }
    }
}

void Config::check() {
    if (input_file.empty()) {
        print_err("no input file");
    }
    if (input_file.extension() != ".cminus") {
        print_err("file format not recognized");
    }
    if (emitllvm and emitasm) {
        print_err("emit llvm and emit asm both set");
    }
    if (not emitllvm and not emitasm and not emitast) {
        print_err("not supported: generate executable file directly");
    }
    if (licm and not mem2reg) {
        print_err("licm must be used with mem2reg");
    }
    if (output_file.empty()) {
        output_file = input_file.stem();
        if (emitllvm) {
            output_file.replace_extension(".ll");
        } else if (emitasm) {
            output_file.replace_extension(".s");
        }
    }
}

void Config::print_help() const {
    std::cout << "Usage: " << exe_name
              << " [-h|--help] [-o <target-file>] [-emit-llvm] [-S] [-dump-json]"
                 "[-mem2reg] [-licm]"
                 "<input-file>"
              << std::endl;
    exit(0);
}

void Config::print_err(const string &msg) const {
    std::cout << exe_name << ": " << msg << std::endl;
    exit(-1);
}
