#pragma once
// Pequeno utilitário para monitorar shutdown usando std::jthread

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <thread>

namespace app {

class ShutdownMonitor {
  public:
    // Constrói o monitor: should_shutdown() é consultado para saber se foi
    // solicitado shutdown via sinal; on_shutdown() é chamado quando shutdown
    // efetivamente precisa executar a ação de encerramento (ex: terminar a
    // janela). O jthread é gerenciado internamente e junta automaticamente.
    ShutdownMonitor(std::function<bool()> should_shutdown,
                    std::function<void()> on_shutdown)
        : should_shutdown_(std::move(should_shutdown)),
          on_shutdown_(std::move(on_shutdown)) {
        monitor_thread_ = std::jthread([this](std::stop_token st) {
            try {
                std::unique_lock<std::mutex> lock(shutdown_mutex_);
                auto shutdownCondition = [this, &st]() {
                    return should_shutdown_() || st.stop_requested();
                };
                while (!shutdownCondition()) {
                    // Timeout como fallback caso algo trave
                    shutdown_cv_.wait_for(lock, std::chrono::seconds(5),
                                          shutdownCondition);
                    std::this_thread::yield();
                }

                if (should_shutdown_()) {
                    std::cout << "[APP] Shutdown solicitado, terminando..."
                              << std::endl;
                    on_shutdown_();
                }
                // Se foi stop_requested(), saída normal — nada a fazer
            } catch (const std::exception &e) {
                std::cerr << "[APP] Erro no shutdown monitor: " << e.what()
                          << std::endl;
            }
        });
    }

    // Request stop no jthread e notifica a condvar interna para acordar o loop
    void request_stop() {
        monitor_thread_.request_stop();
        shutdown_cv_.notify_all();
    }

    // Destrutor seguro: garante que o callback de encerramento seja chamado
    // ao menos uma vez se necessário, protegendo com try/catch e evitando
    // chamadas duplicadas usando flag atômica.
    ~ShutdownMonitor() {
        try {
            // Solicita parada e acorda a thread para que ela termine rápido
            monitor_thread_.request_stop();
            shutdown_cv_.notify_all();

            // Ao sair do escopo, o std::jthread fará join automaticamente.

            // Se o shutdown foi solicitado externamente (should_shutdown_),
            // asseguramos chamar on_shutdown_ pelo menos uma vez.
            if (should_shutdown_() && !shutdown_called_.exchange(true)) {
                try {
                    on_shutdown_();
                } catch (const std::exception &e) {
                    std::cerr << "[APP] Erro no on_shutdown do destrutor: "
                              << e.what() << std::endl;
                }
            }
        } catch (const std::exception &e) {
            std::cerr << "[APP] Erro no destrutor do ShutdownMonitor: "
                      << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[APP] Erro desconhecido no destrutor do "
                         "ShutdownMonitor."
                      << std::endl;
        }
    }

  private:
    std::jthread monitor_thread_;
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    std::function<bool()> should_shutdown_;
    std::function<void()> on_shutdown_;
    std::atomic<bool> shutdown_called_{false};
};

} // namespace app
