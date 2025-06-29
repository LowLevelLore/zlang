#include "all.hpp"

using namespace zust;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        CommandLine::printUsage(argv[0]);
        return 0;
    }

    CommandLine cli(argc, argv);

    if (cli.hasError()) {
        return 1;
    }

    if (cli.showHelp()) {
        CommandLine::printUsage(argv[0]);
        return 0;
    }

    if (cli.showFormats()) {
        CommandLine::printFormats();
        return 0;
    }

    const std::string inputFile = cli.getInputFile();

    assert(inputFile.ends_with(".zz"));

    if (inputFile.empty()) {
        logError(zust::Error(zust::ErrorType::Generic, "No input files."));
        CommandLine::printUsage(argv[0]);
        return 1;
    }

    std::optional<std::string> source = zust::File::readAllText(inputFile);
    if (!source) {
        logError(zust::Error(zust::ErrorType::Generic,
                              "Failed to read from " + inputFile));
        return 1;
    }

    // Parse source
    Lexer lexer(source.value());
    Parser parser(lexer);
    logMessage("Parsing");
    std::unique_ptr<ASTNode> program = parser.parse();

    if (!parser.isCorrect()) {
        return 1;
    }

    if (!program.get()) {
        zust::logError(Error(ErrorType::Generic, "Parsing Failed"));
        return 1;
    }
    if (cli.printAST()) {
        program.get()->print(std::cout);
    }

    logMessage("TypeChecking");

    // Type checking
    TypeChecker typeChecker;
    typeChecker.check(program);

    logMessage("Code Genning");

    if (!typeChecker.shouldCodegen())
        return 1;

    try {
        program->scope->lookupFunction("main");
    } catch (...) {
        logError(Error(ErrorType::Generic, "Main Function does'nt exist in program scope (GLOBALLY)"));
        exit(1);
    }
    std::ostream *outstream = &std::cout;
    std::ofstream ofs;

    // Only open the file if requested:
    if (!cli.getOutputFile().empty()) {
        ofs.open(cli.getOutputFile());
        if (!ofs) {
            std::cerr << "Error: cannot open output file: "
                      << cli.getOutputFile() << "\n";
            std::exit(1);
        }
        outstream = &ofs;  // now point at the file
    }

    std::unique_ptr<zust::CodeGen> cg =
        CodeGen::create(TargetTriple::X86_64_LINUX, *outstream);

    switch (cli.getFormat()) {
    case CodegenOutputFormat::Default:
#ifdef _WIN64
        cg = CodeGen::create(TargetTriple::X86_64_WINDOWS, *outstream);
#endif
#ifdef __linux__
        cg = CodeGen::create(TargetTriple::X86_64_LINUX, *outstream);
#endif
        break;

    case CodegenOutputFormat::X86_64_MSWIN: {
        cg = CodeGen::create(TargetTriple::X86_64_WINDOWS, *outstream);
        break;
    }

    case CodegenOutputFormat::X86_64_LINUX: {
        cg = CodeGen::create(TargetTriple::X86_64_LINUX, *outstream);
        break;
    }

    case CodegenOutputFormat::LLVM_IR: {
        cg = CodeGen::create(TargetTriple::LLVM_IR, *outstream);
        break;
    }

    default:
        std::cerr << "This should not happen, ACP Pradhyumn...\n";
        exit(1);
    }
    try {
        cg->generate(std::move(program));
    } catch (std::exception const &exc) {
        std::cerr << "ERROR: " << exc.what() << "\n";
    }

    return 0;
}
