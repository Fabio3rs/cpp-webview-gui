#include "app/app_bindings.h"
#include "app/bindings_meta.h"
#include <exception>
#include <fstream>
#include <iostream>
#include <span>

int main(int argc, char **argv) {
    constexpr int required_arguments = 5;
    if (argc < required_arguments) {
        std::cerr << "Uso: emit_native_bindings <out.d.ts> <out.json> <out.js> "
                     "<ui-root>\n";
        return 1;
    }

    const std::span<char *> arguments(argv, static_cast<std::size_t>(argc));
    try {
        const std::string dts_path = arguments[1];
        const std::string json_path = arguments[2];
        const std::string js_path = arguments[3];

        app::HandlerRegistry handlers;
        app::register_app_bindings(nullptr, nullptr, handlers, nullptr);

        std::ofstream dts_file(dts_path);
        std::ofstream json_file(json_path);
        std::ofstream js_file(js_path);
        if (!dts_file || !json_file || !js_file) {
            std::cerr << "Falha ao abrir arquivos de saída.\n";
            return 1;
        }

        app::bindings::meta::dump_typescript_and_index(dts_file, json_file,
                                                       arguments[4]);
        app::bindings::meta::dump_javascript(js_file);
        std::cout << "Gerado: " << dts_path << ", " << json_path << " and "
                  << js_path << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Falha ao gerar bindings: " << error.what() << '\n';
        return 1;
    }
}
