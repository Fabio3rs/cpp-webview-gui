// =============================================================================
// Entry Point - Ponto de entrada com parsing de argumentos CLI
// =============================================================================

#include "app/application.h"
#include "app/cli_options.h"
#include "app/config.h"
#include <iostream>

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE /*hInst*/, HINSTANCE /*hPrevInst*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // No Windows GUI, não temos argc/argv facilmente
    // Usa valores padrão
    app::Application application;
    if (!application.initialize()) {
        return 1;
    }
    return application.run();
}
#else
int main(int argc, char *argv[]) {
    auto parser = app::create_parser();
    auto result = parser.parse(argc, argv);

    switch (result.status) {
    case cli::ParseStatus::ShowHelp:
        std::cout << parser.generate_help(argv[0]);
        return 0;

    case cli::ParseStatus::ShowHelpVerbose:
        std::cout << parser.generate_help_verbose(argv[0]);
        return 0;

    case cli::ParseStatus::ShowVersion:
        std::cout << app::config::WINDOW_TITLE << " v" << app::config::VERSION
                  << "\n";
        return 0;

    case cli::ParseStatus::ShowCompletion:
        // Completion já foi tratada internamente, apenas sai
        return 0;

    case cli::ParseStatus::Error:
        std::cerr << "Erro: " << result.error_message << "\n";
        std::cerr << "Use --help para ver as opções disponíveis.\n";
        return 1;

    case cli::ParseStatus::Ok:
        break;
    }

    app::Application application(*result.config);

    if (!application.initialize()) {
        return 1;
    }

    return application.run();
}
#endif
