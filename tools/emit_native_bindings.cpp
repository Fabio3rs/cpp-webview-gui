#include "app/application.h"
#include "app/bindings_meta.h"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Uso: emit_native_bindings <out.d.ts> <out.json>\n";
        return 1;
    }

    const std::string dts_path = argv[1];
    const std::string json_path = argv[2];

    app::Application application;
    if (!application.initialize()) {
        std::cerr << "Falha ao inicializar app para captura de bindings.\n";
        return 1;
    }

    std::ofstream dts_file(dts_path);
    std::ofstream json_file(json_path);
    if (!dts_file || !json_file) {
        std::cerr << "Falha ao abrir arquivos de saída.\n";
        return 1;
    }

    app::bindings::meta::dump_typescript_and_index(dts_file, json_file);
    std::cout << "Gerado: " << dts_path << " and " << json_path << std::endl;
    return 0;
}
