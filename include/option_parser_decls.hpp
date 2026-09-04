// option_parser.hpp - Generic command-line option parsing framework
// Provides a declarative, table-driven approach to CLI argument parsing
// with automatic help generation and bash completion support.

#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "expected.hpp"

namespace cli {

/// Status of parsing operation
enum class ParseStatus {
    Ok,              ///< Parsing succeeded
    ShowHelp,        ///< --help was requested
    ShowHelpVerbose, ///< --help-verbose was requested
    ShowVersion,     ///< --version was requested
    ShowCompletion,  ///< Bash completion was requested (COMP_LINE set)
    Error            ///< Parsing failed
};

/// Result of parsing operation
template <typename Config> struct ParseResult {
    std::optional<Config> config;
    ParseStatus status = ParseStatus::Error;
    std::string error_message;
    std::string output; ///< Output to print (help text, completions, etc.)
};

/// Specification for a single command-line option
template <typename Config> struct OptionSpec {
    std::string_view long_name{}; ///< Long option name (without --)
    char short_name = '\0';       ///< Short option character (or '\0')
    bool takes_value = false;     ///< Whether option expects a value
    std::string_view
        value_name{};        ///< Value placeholder for help (e.g., "<file>")
    std::string_view help{}; ///< Short help text for concise mode
    std::string_view long_help{}; ///< Detailed help for verbose mode (optional)

    /// Allowed values (empty span = any value accepted)
    std::span<const std::string_view> allowed_values{};

    /// Function to apply this option to config
    void (*apply)(Config &, std::string_view) = nullptr;

    /// Whether this option is required
    bool required = false;
};

/// Number of values accepted by a positional argument.
enum class PositionalArity {
    ExactlyOne, ///< One value is required
    ZeroOrOne,  ///< The value is optional
    OneOrMore,  ///< At least one value is required; must be the last spec
    ZeroOrMore, ///< Any number of values; must be the last spec
};

/// Specification for one positional argument, in declaration order.
template <typename Config> struct PositionalSpec {
    std::string_view name{};      ///< Name shown in help and error messages
    std::string_view help{};      ///< Short help text for concise mode
    std::string_view long_help{}; ///< Detailed help for verbose mode (optional)

    /// Allowed values (empty span = any value accepted)
    std::span<const std::string_view> allowed_values{};

    /// Function to apply each accepted value to config. The value is
    /// non-owning and must be copied if Config needs to retain it.
    void (*apply)(Config &, std::string_view) = nullptr;

    PositionalArity arity = PositionalArity::ExactlyOne;
};

/// Helper to create std::array from variadic arguments
template <typename... T> constexpr auto make_array(T &&...t) {
    using Common = std::common_type_t<std::remove_cvref_t<T>...>;
    return std::array<Common, sizeof...(T)>{std::forward<T>(t)...};
}

/// Generic option parser
template <typename Config> class OptionParser {
  public:
    /// Construct a parser. Both specification spans must outlive the parser.
    explicit OptionParser(
        std::span<const OptionSpec<Config>> specs,
        std::span<const PositionalSpec<Config>> positional_specs = {})
        : specs_(specs), positional_specs_(positional_specs) {}

    /// Set program description for help text
    OptionParser &with_description(std::string_view desc) {
        description_ = desc;
        return *this;
    }

    /// Set examples section for help text
    OptionParser &with_examples(std::string_view examples) {
        examples_ = examples;
        return *this;
    }

    /// Set database sources documentation
    OptionParser &with_database_sources(std::string_view db_sources) {
        database_sources_ = db_sources;
        return *this;
    }

    /// Parse command-line arguments
    [[nodiscard]] ParseResult<Config> parse(int argc, char **argv) const;

    /// Parse from string_view span (useful for testing)
    [[nodiscard]] ParseResult<Config>
    parse(std::span<const std::string_view> args) const;

    /// Generate concise help text with examples
    [[nodiscard]] std::string
    generate_help(std::string_view program_name) const;

    /// Generate detailed help text (man-page style)
    [[nodiscard]] std::string
    generate_help_verbose(std::string_view program_name) const;

    /// Find option by long name
    [[nodiscard]] const OptionSpec<Config> *
    find_option(std::string_view long_name) const;

    /// Find option by short name
    [[nodiscard]] const OptionSpec<Config> *find_option(char short_name) const;

    /// Find option by token (e.g., "--left" or "-l")
    [[nodiscard]] const OptionSpec<Config> *
    find_option_token(std::string_view token) const;

    /// Get all option specifications (for completion handlers)
    [[nodiscard]] std::span<const OptionSpec<Config>> specs() const noexcept {
        return specs_;
    }

    /// Get positional specifications (for completion handlers)
    [[nodiscard]] std::span<const PositionalSpec<Config>>
    positional_specs() const noexcept {
        return positional_specs_;
    }

  private:
    static constexpr std::size_t HELP_COLUMN_WIDTH = 30U;
    static constexpr std::size_t CONCISE_HELP_CAPACITY = 2048U;
    static constexpr std::size_t VERBOSE_HELP_CAPACITY = 4096U;

    std::span<const OptionSpec<Config>> specs_;
    std::span<const PositionalSpec<Config>> positional_specs_;
    std::string_view description_;
    std::string_view examples_;
    std::string_view database_sources_;

    template <typename ArgumentRange>
    [[nodiscard]] ParseResult<Config>
    parse_impl(const ArgumentRange &args) const;

    template <typename Spec>
    [[nodiscard]] Expected<void, std::string>
    validate_value(const Spec &spec, std::string_view value) const;

    [[nodiscard]] Expected<void, std::string> validate_positional_specs() const;

    void format_option_help(std::string &out,
                            const OptionSpec<Config> &opt) const;
    void format_option_help_verbose(std::string &out,
                                    const OptionSpec<Config> &opt) const;
    void append_positional_usage(std::string &out,
                                 const PositionalSpec<Config> &spec) const;
    static void
    append_allowed_values(std::string &out,
                          std::span<const std::string_view> allowed_values);
    void format_positional_help(std::string &out,
                                const PositionalSpec<Config> &spec) const;
    void
    format_positional_help_verbose(std::string &out,
                                   const PositionalSpec<Config> &spec) const;
};

/// Bash completion handler
class CompletionHandler {
  public:
    /// Handle bash completion request
    /// Returns 0 on success
    template <typename Config>
    static int handle_completion(const OptionParser<Config> &parser);

    /// Write completion candidates for a command line prefix.
    template <typename Config>
    static void write_completions(const OptionParser<Config> &parser,
                                  std::string_view line, std::size_t point,
                                  std::ostream &out);

  private:
    enum class QuoteMode { None, Single, Double };

    struct CompletionWord {
        std::string_view raw{};
        bool complete = false;
        bool needs_decoding = false;
    };

    static bool next_completion_word(std::string_view line, std::size_t &cursor,
                                     CompletionWord &word);
    static std::string_view decode_completion_word(const CompletionWord &word,
                                                   std::string &storage);
    static bool is_double_quote_escape(char character) noexcept;

    /// Suggest option flags
    template <typename Config>
    static void suggest_options(const OptionParser<Config> &parser,
                                std::string_view prefix, std::ostream &out);

    /// Suggest values for an option
    static void suggest_values(std::span<const std::string_view> values,
                               std::string_view prefix, std::ostream &out);
};

} // namespace cli
