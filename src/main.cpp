#include "Error.hpp"
#define FMT_HEADER_ONLY
#include <argparse/argparse.hpp>
#include <dtslib/filesystem.hpp>
#include <fmt/printf.h>

#include "Assembler.hpp"
#include "Compiler.hpp"
#include "Lexer.hpp"

static bool
  invoke_external_command(const std::string& command, const bool verbose) {
    if (verbose) { fmt::print(stdout, "[INFO] {}\n", command); }

    const auto executable_name = [&]() {
        if (const auto space_index = command.find(' ');
            space_index != std::string::npos) {
            return command.substr(0, space_index);
        }
        return command;
    }();

    // FIXME: There has to be a better way to do this
    FILE* executable_process = popen(command.c_str(), "r");
    if (executable_process == nullptr) {
        fmt::print(
          stderr, fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error: "
        );
        fmt::print(
          stderr, fmt::emphasis::bold, "{} not found\n", executable_name
        );
        return false;
    }

    pclose(executable_process);
    return true;
}

int main(const int argc, const char** argv) {
    argparse::ArgumentParser parser("rack", "0.0.1");
    parser.add_argument("file").help("path to rack file to compile");
    parser.add_argument("-o", "--output").help("compiled binary output path");
    parser.add_argument("-s", "--generate-asm")
      .help("generate assembly intermediate file");
    parser.add_argument("-T", "--lexed-tokens")
      .help("prints lexed tokens to stdout")
      .default_value(false)
      .implicit_value(true)
      .help("prints lexed tokens to stdout");
    parser.add_argument("-V", "--verbose")
      .default_value(false)
      .implicit_value(true)
      .help("print compilation phases, and executed commands to stdout");

    try {
        parser.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        fmt::print(
          stderr, fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error: "
        );
        fmt::print(stderr, fmt::emphasis::bold, "{}\n", err.what());
        fmt::print(stderr, "{}\n", parser.help().str());
    }

    if (parser["--verbose"] == true) {
        fmt::print(stdout, "[INFO] Compiling (Lexing, Generating Assembly)\n");
    }

    const std::shared_ptr<Compiler> compiler =
      Compiler::create(parser.get<std::string>("file"));
    const auto tokens = Lexer::lex(compiler);

    if (!tokens.has_value()) {
        fmt::print(stderr, "[INTERNAL ERROR] lex error: {}\n", tokens.error());
        compiler->print_errors();
        return 1;
    }

    if (parser["--lexed-tokens"] == true) {
        for (const auto& token : tokens.value()) { fmt::println("{}", token); }
    }

    const auto compile_result =
      Assembler_x86_64::compile(compiler, tokens.value());

    if (!compile_result.has_value()) {
        fmt::print(
          stderr, "[INTERNAL ERROR] compile error: {}\n", compile_result.error()
        );
        compiler->print_errors();
        return 1;
    }

    // As a final stage, print compiler errors if present
    if (compiler->has_errors()) {
        compiler->print_errors();
        return 1;
    }

    // Now we can invoke nasm and then link
    const auto file_path =
      std::filesystem::path(parser.get<std::string>("file"));
    const std::string file_path_without_extension =
      file_path.parent_path() / file_path.stem();

    // TODO: Optimize this
    const std::string nasm_command = [&]() {
        if (const auto output_file = parser.present("--output")) {
            const auto output_path =
              std::filesystem::path(parser.get<std::string>("--output"));
            const std::string output_path_without_extension =
              output_path.parent_path() / output_path.stem();
            return fmt::format(
              "nasm -f elf64 {}.asm -o {}.o",
              output_path_without_extension,
              output_path_without_extension
            );
        } else {
            return fmt::format(
              "nasm -f elf64 {}.asm -o {}.o",
              file_path_without_extension,
              file_path_without_extension
            );
        }
    }();

    if (!invoke_external_command(nasm_command, parser.get<bool>("--verbose"))) {
        return 1;
    }

    // Optimize this
    const std::string ld_command = [&]() {
        if (const auto output_file = parser.present("--output")) {
            const auto output_path =
              std::filesystem::path(parser.get<std::string>("--output"));
            const std::string output_path_without_extension =
              output_path.parent_path() / output_path.stem();
            return fmt::format(
              "ld {}.o -o {}",
              output_path_without_extension,
              output_path.string()
            );
        } else {
            return fmt::format(
              "ld {}.o -o {}",
              file_path_without_extension,
              file_path_without_extension
            );
        }
    }();

    if (!invoke_external_command(ld_command, parser.get<bool>("--verbose"))) {
        return 1;
    }

    return 0;
}
