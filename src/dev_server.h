#pragma once
// =============================================================================
// Dev Server Manager - Gerencia o Vite dev server para hot reload
// =============================================================================

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dev {

// =============================================================================
// Tipos e estruturas
// =============================================================================

enum class ServerState { Stopped, Starting, Running, Failed };

struct ServerConfig {
    std::string dev_url = "http://127.0.0.1:5173";
    std::string host = "127.0.0.1";
    int port = 5173;
    std::string command = "npm run dev";
    std::string working_dir = ""; // diretório do UI
    std::chrono::seconds timeout{30};
};

struct ServerProcess {
    ServerState state = ServerState::Stopped;
#ifdef _WIN32
    HANDLE process_handle = nullptr;
    DWORD process_id = 0;
#else
    pid_t pid = -1;
#endif
    bool owned = false; // true se nós iniciamos o processo
};

// =============================================================================
// Health check - verifica se o servidor está respondendo
// =============================================================================

inline bool is_server_responding(const std::string &host, int port) {
#ifdef _WIN32
    // Windows: usa WinHTTP
    HINTERNET session =
        WinHttpOpen(L"DevServer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!session)
        return false;

    std::wstring whost(host.begin(), host.end());
    HINTERNET connect = WinHttpConnect(session, whost.c_str(),
                                       static_cast<INTERNET_PORT>(port), 0);

    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request =
        WinHttpOpenRequest(connect, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    bool success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                   WinHttpReceiveResponse(request, nullptr);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    return success;
#else
    // POSIX: socket simples
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return false;

    // Timeout de conexão
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sockfd);
        return false;
    }

    bool connected =
        (connect(sockfd, reinterpret_cast<struct sockaddr *>(&serv_addr),
                 sizeof(serv_addr)) == 0);

    if (connected) {
        // Envia request HTTP básico
        const char *request =
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        send(sockfd, request, strlen(request), 0);

        // Lê resposta
        char buffer[256];
        ssize_t bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            // Verifica se é HTTP response válido
            connected = (strstr(buffer, "HTTP/") != nullptr);
        } else {
            connected = false;
        }
    }

    close(sockfd);
    return connected;
#endif
}

// =============================================================================
// Spawn do processo
// =============================================================================

inline bool spawn_server(const ServerConfig &cfg, ServerProcess &proc) {
    std::cout << "[DEV] Iniciando Vite dev server..." << std::endl;
    std::cout << "[DEV] Comando: " << cfg.command << std::endl;
    std::cout << "[DEV] Diretório: " << cfg.working_dir << std::endl;

#ifdef _WIN32
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    // Monta comando com cd + comando
    std::string full_cmd =
        "cmd /c cd /d \"" + cfg.working_dir + "\" && " + cfg.command;

    if (!CreateProcessA(nullptr, const_cast<char *>(full_cmd.c_str()), nullptr,
                        nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        std::cerr << "[DEV] Erro ao iniciar processo: " << GetLastError()
                  << std::endl;
        proc.state = ServerState::Failed;
        return false;
    }

    proc.process_handle = pi.hProcess;
    proc.process_id = pi.dwProcessId;
    CloseHandle(pi.hThread);
#else
    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "[DEV] Erro no fork: " << strerror(errno) << std::endl;
        proc.state = ServerState::Failed;
        return false;
    }

    if (pid == 0) {
        // Processo filho
        if (!cfg.working_dir.empty()) {
            if (chdir(cfg.working_dir.c_str()) != 0) {
                std::cerr << "[DEV] Erro ao mudar diretório: "
                          << strerror(errno) << std::endl;
                _exit(1);
            }
        }

        // Cria novo grupo de processo
        setsid();

        // Executa o comando via shell
        execl("/bin/sh", "sh", "-c", cfg.command.c_str(), nullptr);

        // Se chegou aqui, exec falhou
        std::cerr << "[DEV] Erro no exec: " << strerror(errno) << std::endl;
        _exit(1);
    }

    proc.pid = pid;
#endif

    proc.state = ServerState::Starting;
    proc.owned = true;
    return true;
}

// =============================================================================
// Encerrar processo
// =============================================================================

inline void stop_server(ServerProcess &proc) {
    if (!proc.owned) {
        std::cout << "[DEV] Servidor externo, não será encerrado." << std::endl;
        return;
    }

#ifdef _WIN32
    if (proc.process_handle) {
        std::cout << "[DEV] Encerrando Vite dev server (PID: "
                  << proc.process_id << ")..." << std::endl;

        // Tenta terminar graciosamente primeiro
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, proc.process_id);

        // Espera um pouco
        if (WaitForSingleObject(proc.process_handle, 3000) == WAIT_TIMEOUT) {
            // Força encerramento
            TerminateProcess(proc.process_handle, 0);
        }

        CloseHandle(proc.process_handle);
        proc.process_handle = nullptr;
        proc.process_id = 0;
    }
#else
    if (proc.pid > 0) {
        std::cout << "[DEV] Encerrando Vite dev server (PID: " << proc.pid
                  << ")..." << std::endl;

        // Envia SIGTERM para o grupo de processos
        kill(-proc.pid, SIGTERM);

        // Espera um pouco
        int status;
        int waited = 0;
        while (waitpid(proc.pid, &status, WNOHANG) == 0 && waited < 30) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waited++;
        }

        // Se ainda estiver rodando, força
        if (waited >= 30) {
            kill(-proc.pid, SIGKILL);
            waitpid(proc.pid, &status, 0);
        }

        proc.pid = -1;
    }
#endif

    proc.state = ServerState::Stopped;
    proc.owned = false;
    std::cout << "[DEV] Vite dev server encerrado." << std::endl;
}

// =============================================================================
// Garantir que o servidor está rodando
// =============================================================================

inline bool ensure_server_running(const ServerConfig &cfg,
                                  ServerProcess &proc) {
    // 1. Já tem algo respondendo?
    if (is_server_responding(cfg.host, cfg.port)) {
        std::cout << "[DEV] Servidor já está rodando em " << cfg.dev_url
                  << std::endl;
        proc.state = ServerState::Running;
        proc.owned = false; // Não fomos nós que iniciamos
        return true;
    }

    // 2. Se não tem processo ainda, spawn
    if (proc.state == ServerState::Stopped ||
        proc.state == ServerState::Failed) {
        if (!spawn_server(cfg, proc)) {
            return false;
        }
    }

    // 3. Esperar ficar de pé
    std::cout << "[DEV] Aguardando servidor ficar disponível..." << std::endl;
    auto start = std::chrono::steady_clock::now();
    int dots = 0;

    while (!is_server_responding(cfg.host, cfg.port)) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > cfg.timeout) {
            std::cerr << "\n[DEV] Timeout: servidor não respondeu em "
                      << cfg.timeout.count() << " segundos." << std::endl;
            proc.state = ServerState::Failed;
            stop_server(proc);
            return false;
        }

        // Feedback visual
        if (++dots % 4 == 0) {
            std::cout << "." << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::cout << "\n[DEV] Servidor disponível em " << cfg.dev_url << std::endl;
    proc.state = ServerState::Running;
    return true;
}

// =============================================================================
// Helper: detecta se estamos em modo dev
// =============================================================================

inline bool is_dev_mode() {
    // Primeiro, checa a env var (permite override em runtime)
    const char *env = std::getenv("APP_DEV");
    if (env) {
        std::string_view val(env);
        return val == "1" || val == "true" || val == "yes";
    }

    // Se não houver env var, usa a definição de compilação
#ifdef APP_DEV_MODE
    return true; // Compilado com -DDEV_MODE=ON
#else
    return false; // Compilado em modo produção
#endif
}

// =============================================================================
// Helper: obtém o diretório fonte do projeto
// =============================================================================

inline std::string get_source_dir() {
#ifdef APP_SOURCE_DIR
    return APP_SOURCE_DIR;
#else
    return "."; // fallback para diretório atual
#endif
}

// =============================================================================
// Helper: cria config padrão com paths corretos
// =============================================================================

inline ServerConfig get_default_config() {
    ServerConfig cfg;
    cfg.dev_url = "http://127.0.0.1:5173";
    cfg.host = "127.0.0.1";
    cfg.port = 5173;
    cfg.command = "npm run dev";
    cfg.working_dir = get_source_dir() + "/ui";
    cfg.timeout = std::chrono::seconds{30};
    return cfg;
}

} // namespace dev
