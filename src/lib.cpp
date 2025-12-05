// =============================================================================
// app_lib - Biblioteca com código testável separado do main()
// =============================================================================
// Este arquivo existe para que a biblioteca tenha pelo menos um translation
// unit. Headers em include/ e src/app/ são incluídos pelos consumidores da lib.
//
// Adicione implementações de funções/classes aqui conforme o projeto cresce.
// =============================================================================

// Placeholder para manter a lib compilável mesmo sendo header-only por
// enquanto.
namespace app::detail {
// Força a criação de um símbolo para evitar warnings de "empty translation
// unit"
[[maybe_unused]] static const int lib_version = 1;
} // namespace app::detail
