// =============================================================================
// Entry Point - Ponto de entrada com parsing de argumentos CLI
// =============================================================================

#include "app/application.h"
#include "app/cli_options.h"
#include "app/config.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <shellapi.h>
#endif

namespace {
int run_app(int argc, char *argv[]) {
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

#ifdef APP_DEV_MODE
    if (result.config->prod_mode) {
        std::cerr << "Este build não contém a UI embutida; configure CMake com "
                     "-DDEV_MODE=OFF para usar --prod.\n";
        return 1;
    }
#endif

    app::Application application(*result.config);

    if (!application.initialize()) {
        return 1;
    }

    return application.run();
}
} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR *wide_args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide_args)
        return 1;
    const std::unique_ptr<void, decltype(&LocalFree)> wide_args_owner(
        wide_args, &LocalFree);
    std::vector<std::string> strings;
    strings.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide_args[index], -1,
                                             nullptr, 0, nullptr, nullptr);
        if (size <= 0) {
            return 1;
        }
        std::string arg(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, wide_args[index], -1, arg.data(),
                                size, nullptr, nullptr) != size) {
            return 1;
        }
        arg.pop_back();
        strings.push_back(std::move(arg));
    }
    std::vector<char *> argv;
    argv.reserve(strings.size());
    for (auto &arg : strings)
        argv.push_back(arg.data());
    return run_app(argc, argv.data());
}
#else
int main(int argc, char *argv[]) { return run_app(argc, argv); }
#endif
