// option_parser_impl.hpp - Template implementations for option_parser.hpp

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <iostream>
#include <ranges>

#include "expected.hpp"
#include "option_parser_decls.hpp"

namespace cli {

// ============================================================================
// OptionParser implementation
// ============================================================================

template <typename Config>
ParseResult<Config> OptionParser<Config>::parse(int argc, char **argv) const {
    if (argc < 0 || (argc > 0 && argv == nullptr)) {
        ParseResult<Config> result;
        result.status = ParseStatus::Error;
        result.error_message = "Invalid argument vector";
        return result;
    }

    const auto argument_count = static_cast<std::size_t>(argc);
    return parse_impl(std::span<char *>{argv, argument_count});
}

template <typename Config>
ParseResult<Config>
OptionParser<Config>::parse(std::span<const std::string_view> args) const {
    return parse_impl(args);
}

template <typename Config>
const OptionSpec<Config> *
OptionParser<Config>::find_option(std::string_view long_name) const {
    for (const auto &spec : specs_) {
        if (spec.long_name == long_name) {
            return &spec;
        }
    }
    return nullptr;
}

template <typename Config>
const OptionSpec<Config> *
OptionParser<Config>::find_option(char short_name) const {
    if (short_name == '\0') {
        return nullptr;
    }
    for (const auto &spec : specs_) {
        if (spec.short_name == short_name) {
            return &spec;
        }
    }
    return nullptr;
}

template <typename Config>
const OptionSpec<Config> *
OptionParser<Config>::find_option_token(std::string_view token) const {
    using namespace std::literals;

    if (token.size() >= 3 && token.starts_with("--"sv)) {
        return find_option(token.substr(2));
    }
    if (token.size() == 2 && token[0] == '-') {
        return find_option(token[1]);
    }
    return nullptr;
}

template <typename Config>
template <typename Spec>
Expected<void, std::string>
OptionParser<Config>::validate_value(const Spec &spec,
                                     std::string_view value) const {
    if (spec.allowed_values.empty()) {
        return {}; // Any value is allowed
    }

    bool found =
        std::ranges::any_of(spec.allowed_values,
                            [value](std::string_view v) { return v == value; });

    if (!found) {
        std::string allowed;
        append_allowed_values(allowed, spec.allowed_values);

        if constexpr (std::is_same_v<Spec, OptionSpec<Config>>) {
            return make_unexpected(
                std::format("Invalid value '{}' for '--{}'. Allowed: {}", value,
                            spec.long_name, allowed));
        } else {
            return make_unexpected(std::format(
                "Invalid value '{}' for positional argument '{}'. Allowed: {}",
                value, spec.name, allowed));
        }
    }

    return {};
}

template <typename Config>
void OptionParser<Config>::append_allowed_values(
    std::string &out, std::span<const std::string_view> allowed_values) {
    bool first = true;
    for (auto value : allowed_values) {
        if (!first) {
            out += ", ";
        }
        out += value;
        first = false;
    }
}

template <typename Config>
Expected<void, std::string>
OptionParser<Config>::validate_positional_specs() const {
    bool optional_seen = false;

    for (std::size_t index = 0; index < positional_specs_.size(); ++index) {
        const auto &spec = positional_specs_[index];
        if (spec.name.empty()) {
            return make_unexpected(std::string(
                "Invalid positional specification: name cannot be empty"));
        }
        if (spec.apply == nullptr) {
            return make_unexpected(
                std::format("Invalid positional specification '{}': callback "
                            "cannot be null",
                            spec.name));
        }

        const bool is_variadic = spec.arity == PositionalArity::OneOrMore ||
                                 spec.arity == PositionalArity::ZeroOrMore;
        if (is_variadic && index + 1U != positional_specs_.size()) {
            return make_unexpected(std::format(
                "Invalid positional specification '{}': variadic argument must "
                "be last",
                spec.name));
        }

        const bool is_required = spec.arity == PositionalArity::ExactlyOne ||
                                 spec.arity == PositionalArity::OneOrMore;
        if (is_required && optional_seen) {
            return make_unexpected(std::format(
                "Invalid positional specification '{}': required argument "
                "cannot follow an optional argument",
                spec.name));
        }

        optional_seen = optional_seen || !is_required;
    }

    return {};
}

template <typename Config>
template <typename ArgumentRange>
ParseResult<Config>
OptionParser<Config>::parse_impl(const ArgumentRange &args) const {

    using namespace std::literals;

    ParseResult<Config> result;
    Config config{};

    if (auto validation = validate_positional_specs(); !validation) {
        result.status = ParseStatus::Error;
        result.error_message = validation.error();
        return result;
    }

    // Detecta contexto de bash completion (COMP_LINE está definido)
    if (std::getenv("COMP_LINE") != nullptr) {
        CompletionHandler::handle_completion(*this);
        result.status = ParseStatus::ShowCompletion;
        return result;
    }

    std::size_t positional_index = 0;
    std::size_t variadic_count = 0;
    bool options_enabled = true;

    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view arg = args[i];

        // Handle --help / -h
        if (options_enabled && (arg == "--help"sv || arg == "-h"sv)) {
            result.status = ParseStatus::ShowHelp;
            return result;
        }

        // Handle --help-verbose
        if (options_enabled && arg == "--help-verbose"sv) {
            result.status = ParseStatus::ShowHelpVerbose;
            return result;
        }

        // Handle --version / -V
        if (options_enabled && (arg == "--version"sv || arg == "-V"sv)) {
            result.status = ParseStatus::ShowVersion;
            return result;
        }

        // Handle --
        if (options_enabled && arg == "--"sv) {
            options_enabled = false;
            continue;
        }

        // Handle long options (--option)
        if (options_enabled && arg.starts_with("--"sv)) {
            auto name = arg.substr(2);
            auto *spec = find_option(name);

            if (!spec) {
                result.status = ParseStatus::Error;
                result.error_message =
                    std::format("Unknown option '--{}'", name);
                return result;
            }

            std::string_view value;
            if (spec->takes_value) {
                if (i + 1 >= args.size()) {
                    result.status = ParseStatus::Error;
                    result.error_message =
                        std::format("Option '--{}' requires a value", name);
                    return result;
                }
                value = args[++i];

                if (auto err = validate_value(*spec, value); !err) {
                    result.status = ParseStatus::Error;
                    result.error_message = err.error();
                    return result;
                }
            }

            try {
                spec->apply(config, value);
            } catch (const std::exception &ex) {
                result.status = ParseStatus::Error;
                result.error_message = ex.what();
                return result;
            }
            continue;
        }
        // Handle short options (-o)
        if (options_enabled && arg.starts_with('-') && arg.size() > 1) {
            for (std::size_t pos = 1; pos < arg.size(); ++pos) {
                char short_opt = arg[pos];
                auto *spec = find_option(short_opt);

                if (!spec) {
                    result.status = ParseStatus::Error;
                    result.error_message =
                        std::format("Unknown option '-{}'", short_opt);
                    return result;
                }

                std::string_view value;
                if (spec->takes_value) {
                    // Value can be attached (-ovalue) or separate (-o value)
                    if (pos + 1 < arg.size()) {
                        value = arg.substr(pos + 1);
                        pos = arg.size(); // Consume rest of arg
                    } else {
                        if (i + 1 >= args.size()) {
                            result.status = ParseStatus::Error;
                            result.error_message = std::format(
                                "Option '-{}' requires a value", short_opt);
                            return result;
                        }
                        value = args[++i];
                    }

                    if (auto err = validate_value(*spec, value); !err) {
                        result.status = ParseStatus::Error;
                        result.error_message = err.error();
                        return result;
                    }
                }

                try {
                    spec->apply(config, value);
                } catch (const std::exception &ex) {
                    result.status = ParseStatus::Error;
                    result.error_message = ex.what();
                    return result;
                }
            }
            continue;
        }

        if (positional_index >= positional_specs_.size()) {
            result.status = ParseStatus::Error;
            result.error_message =
                std::format("Unexpected positional argument '{}'", arg);
            return result;
        }

        const auto &spec = positional_specs_[positional_index];
        if (auto validation = validate_value(spec, arg); !validation) {
            result.status = ParseStatus::Error;
            result.error_message = validation.error();
            return result;
        }

        try {
            spec.apply(config, arg);
        } catch (const std::exception &ex) {
            result.status = ParseStatus::Error;
            result.error_message = ex.what();
            return result;
        }

        if (spec.arity == PositionalArity::OneOrMore ||
            spec.arity == PositionalArity::ZeroOrMore) {
            ++variadic_count;
        } else {
            ++positional_index;
        }
    }

    for (std::size_t index = positional_index; index < positional_specs_.size();
         ++index) {
        const auto &spec = positional_specs_[index];
        const bool missing_exactly_one =
            spec.arity == PositionalArity::ExactlyOne;
        const bool missing_one_or_more =
            spec.arity == PositionalArity::OneOrMore && variadic_count == 0;
        if (missing_exactly_one || missing_one_or_more) {
            result.status = ParseStatus::Error;
            result.error_message = std::format(
                "Missing required positional argument '{}'", spec.name);
            return result;
        }
    }

    result.config = std::move(config);
    result.status = ParseStatus::Ok;
    return result;
}

template <typename Config>
std::string
OptionParser<Config>::generate_help(std::string_view program_name) const {
    std::string help;
    help.reserve(CONCISE_HELP_CAPACITY);
    help += std::format("Usage: {} [OPTIONS]", program_name);
    for (const auto &spec : positional_specs_) {
        help += ' ';
        append_positional_usage(help, spec);
    }
    help += "\n\n";

    if (!positional_specs_.empty()) {
        help += "Positional arguments:\n";
        for (const auto &spec : positional_specs_) {
            format_positional_help(help, spec);
        }
        help += '\n';
    }

    help += "Options:\n";

    for (const auto &opt : specs_) {
        format_option_help(help, opt);
    }

    help += "\n  -h, --help              Show this help message\n";
    help += "      --help-verbose      Show detailed help with examples\n";
    if (!positional_specs_.empty()) {
        help += "      --                    Treat following arguments as "
                "positional\n";
    }

    // Add database sources section if provided
    if (!database_sources_.empty()) {
        help += "\nDATABASE SOURCES:\n";
        help += database_sources_;
        if (!database_sources_.ends_with('\n')) {
            help += '\n';
        }
    }

    // Add examples section if provided
    if (!examples_.empty()) {
        help += "\nEXAMPLES:\n";
        help += examples_;
        if (!examples_.ends_with('\n')) {
            help += '\n';
        }
    }

    return help;
}

template <typename Config>
void OptionParser<Config>::append_positional_usage(
    std::string &out, const PositionalSpec<Config> &spec) const {
    switch (spec.arity) {
    case PositionalArity::ExactlyOne:
        out += '<';
        out += spec.name;
        out += '>';
        break;
    case PositionalArity::ZeroOrOne:
        out += '[';
        out += spec.name;
        out += ']';
        break;
    case PositionalArity::OneOrMore:
        out += '<';
        out += spec.name;
        out += ">...";
        break;
    case PositionalArity::ZeroOrMore:
        out += '[';
        out += spec.name;
        out += "...]";
        break;
    }
}

template <typename Config>
void OptionParser<Config>::format_positional_help(
    std::string &out, const PositionalSpec<Config> &spec) const {
    out += "  ";
    append_positional_usage(out, spec);

    const std::size_t line_start = out.rfind('\n') + 1U;
    const std::size_t current_length = out.size() - line_start;
    if (current_length < HELP_COLUMN_WIDTH) {
        out.append(HELP_COLUMN_WIDTH - current_length, ' ');
    } else {
        out += "  ";
    }

    out += spec.help;
    if (!spec.allowed_values.empty()) {
        out += " (";
        append_allowed_values(out, spec.allowed_values);
        out += ')';
    }
    out += '\n';
}

template <typename Config>
void OptionParser<Config>::format_option_help(
    std::string &out, const OptionSpec<Config> &opt) const {
    out += "  ";

    // Short option
    if (opt.short_name != '\0') {
        out += '-';
        out += opt.short_name;
        if (!opt.long_name.empty()) {
            out += ", ";
        }
    } else {
        out += "    ";
    }

    // Long option
    if (!opt.long_name.empty()) {
        out += "--";
        out += opt.long_name;
    }

    // Value placeholder
    if (opt.takes_value && !opt.value_name.empty()) {
        out += ' ';
        out += opt.value_name;
    }

    // Padding to align descriptions (assuming max ~30 chars for option part)
    const std::size_t current_len = out.size() - out.rfind('\n') - 1U;
    if (current_len < HELP_COLUMN_WIDTH) {
        out.append(HELP_COLUMN_WIDTH - current_len, ' ');
    } else {
        out += "  ";
    }

    // Help text
    out += opt.help;

    // Show allowed values
    if (!opt.allowed_values.empty()) {
        out += " (";
        append_allowed_values(out, opt.allowed_values);
        out += ')';
    }

    if (opt.required) {
        out += " [required]";
    }

    out += '\n';
}

template <typename Config>
std::string OptionParser<Config>::generate_help_verbose(
    std::string_view program_name) const {
    std::string help;
    help.reserve(VERBOSE_HELP_CAPACITY);

    // NAME section
    help += "NAME\n";
    help += std::format("    {} - ", program_name);
    if (!description_.empty()) {
        help += description_;
    } else {
        help += "Command-line tool";
    }
    help += "\n\n";

    // SYNOPSIS section
    help += "SYNOPSIS\n";
    help += std::format("    {} [OPTIONS]", program_name);
    for (const auto &spec : positional_specs_) {
        help += ' ';
        append_positional_usage(help, spec);
    }
    help += "\n\n";

    // DESCRIPTION section (if provided)
    if (!description_.empty()) {
        help += "DESCRIPTION\n";
        // Indent description text
        for (char c : description_) {
            if (help.back() == '\n') {
                help += "    ";
            }
            help += c;
        }
        if (!description_.ends_with('\n')) {
            help += '\n';
        }
        help += '\n';
    }

    if (!positional_specs_.empty()) {
        help += "POSITIONAL ARGUMENTS\n";
        for (const auto &spec : positional_specs_) {
            format_positional_help_verbose(help, spec);
        }
    }

    // OPTIONS section (detailed)
    help += "OPTIONS\n";
    for (const auto &opt : specs_) {
        format_option_help_verbose(help, opt);
    }

    // Add help options
    help += "    -h, --help\n";
    help += "        Show concise help message with examples.\n\n";
    help += "    --help-verbose\n";
    help += "        Show this detailed help message (man-page style).\n\n";
    if (!positional_specs_.empty()) {
        help += "    --\n";
        help += "        Treat all following arguments as positional.\n\n";
    }

    // DATABASE SOURCES section
    if (!database_sources_.empty()) {
        help += "DATABASE SOURCES\n";
        // Indent database sources text
        for (char c : database_sources_) {
            if (help.back() == '\n') {
                help += "    ";
            }
            help += c;
        }
        if (!database_sources_.ends_with('\n')) {
            help += '\n';
        }
        help += '\n';
    }

    // EXAMPLES section
    if (!examples_.empty()) {
        help += "EXAMPLES\n";
        // Indent examples text
        for (char c : examples_) {
            if (help.back() == '\n') {
                help += "    ";
            }
            help += c;
        }
        if (!examples_.ends_with('\n')) {
            help += '\n';
        }
    }

    return help;
}

template <typename Config>
void OptionParser<Config>::format_positional_help_verbose(
    std::string &out, const PositionalSpec<Config> &spec) const {
    out += "    ";
    append_positional_usage(out, spec);
    out += '\n';

    const std::string_view description =
        spec.long_help.empty() ? spec.help : spec.long_help;
    out += "        ";
    for (char character : description) {
        out += character;
        if (character == '\n') {
            out += "        ";
        }
    }
    if (!description.ends_with('\n')) {
        out += '\n';
    }

    if (!spec.allowed_values.empty()) {
        out += "        \n";
        out += "        Allowed values: ";
        append_allowed_values(out, spec.allowed_values);
        out += '\n';
    }

    out += '\n';
}

template <typename Config>
void OptionParser<Config>::format_option_help_verbose(
    std::string &out, const OptionSpec<Config> &opt) const {
    // Option signature line
    out += "    ";

    if (opt.short_name != '\0') {
        out += '-';
        out += opt.short_name;
        if (!opt.long_name.empty()) {
            out += ", ";
        }
    }

    if (!opt.long_name.empty()) {
        out += "--";
        out += opt.long_name;
    }

    if (opt.takes_value && !opt.value_name.empty()) {
        out += ' ';
        out += opt.value_name;
    }

    out += '\n';

    // Detailed description (indented)
    std::string_view desc = opt.long_help.empty() ? opt.help : opt.long_help;

    out += "        ";
    for (char c : desc) {
        out += c;
        if (c == '\n') {
            out += "        "; // Indent continuation lines
        }
    }

    if (!desc.ends_with('\n')) {
        out += '\n';
    }

    // Show allowed values on separate line if present
    if (!opt.allowed_values.empty()) {
        out += "        \n";
        out += "        Allowed values: ";
        append_allowed_values(out, opt.allowed_values);
        out += '\n';
    }

    // Show required marker
    if (opt.required) {
        out += "        \n";
        out += "        This option is required.\n";
    }

    out += '\n';
}

// ============================================================================
// CompletionHandler implementation
// ============================================================================

inline bool CompletionHandler::next_completion_word(std::string_view line,
                                                    std::size_t &cursor,
                                                    CompletionWord &word) {
    while (cursor < line.size() &&
           std::isspace(static_cast<unsigned char>(line[cursor])) != 0) {
        ++cursor;
    }
    if (cursor == line.size()) {
        return false;
    }

    const std::size_t start = cursor;
    QuoteMode quote = QuoteMode::None;
    bool escaped = false;
    bool needs_decoding = false;

    while (cursor < line.size()) {
        const char character = line[cursor];
        if (escaped) {
            escaped = false;
            ++cursor;
            continue;
        }

        if (quote == QuoteMode::Single) {
            if (character == '\'') {
                quote = QuoteMode::None;
            }
            ++cursor;
            continue;
        }

        if (quote == QuoteMode::Double) {
            if (character == '"') {
                quote = QuoteMode::None;
            } else if (character == '\\' && cursor + 1U < line.size() &&
                       is_double_quote_escape(line[cursor + 1U])) {
                escaped = true;
            }
            ++cursor;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            word = CompletionWord{
                .raw = line.substr(start, cursor - start),
                .complete = true,
                .needs_decoding = needs_decoding,
            };
            return true;
        }
        if (character == '\'') {
            quote = QuoteMode::Single;
            needs_decoding = true;
        } else if (character == '"') {
            quote = QuoteMode::Double;
            needs_decoding = true;
        } else if (character == '\\') {
            escaped = true;
            needs_decoding = true;
        }
        ++cursor;
    }

    word = CompletionWord{
        .raw = line.substr(start),
        .complete = false,
        .needs_decoding = needs_decoding,
    };
    return true;
}

inline bool CompletionHandler::is_double_quote_escape(char character) noexcept {
    return character == '$' || character == '`' || character == '"' ||
           character == '\\' || character == '\n';
}

inline std::string_view
CompletionHandler::decode_completion_word(const CompletionWord &word,
                                          std::string &storage) {
    if (!word.needs_decoding) {
        return word.raw;
    }

    storage.clear();
    storage.reserve(word.raw.size());
    QuoteMode quote = QuoteMode::None;
    bool escaped = false;

    for (std::size_t index = 0; index < word.raw.size(); ++index) {
        const char character = word.raw[index];
        if (escaped) {
            if (character != '\n') {
                storage += character;
            }
            escaped = false;
            continue;
        }

        if (quote == QuoteMode::Single) {
            if (character == '\'') {
                quote = QuoteMode::None;
            } else {
                storage += character;
            }
            continue;
        }

        if (quote == QuoteMode::Double) {
            if (character == '"') {
                quote = QuoteMode::None;
            } else if (character == '\\' && index + 1U < word.raw.size() &&
                       is_double_quote_escape(word.raw[index + 1U])) {
                escaped = true;
            } else {
                storage += character;
            }
            continue;
        }

        if (character == '\'') {
            quote = QuoteMode::Single;
        } else if (character == '"') {
            quote = QuoteMode::Double;
        } else if (character == '\\') {
            escaped = true;
        } else {
            storage += character;
        }
    }

    return storage;
}

template <typename Config>
void CompletionHandler::suggest_options(const OptionParser<Config> &parser,
                                        std::string_view prefix,
                                        std::ostream &out) {
    for (const auto &spec : parser.specs()) {
        const bool matches_long =
            prefix.empty() || prefix == "-" ||
            (prefix.starts_with("--") &&
             spec.long_name.starts_with(prefix.substr(2)));
        if (!spec.long_name.empty() && matches_long) {
            out << "--" << spec.long_name << '\n';
        }

        const bool matches_short =
            prefix.empty() || prefix == "-" ||
            (prefix.size() == 2U && prefix.front() == '-' &&
             prefix.back() == spec.short_name);
        if (spec.short_name != '\0' && matches_short) {
            out << '-' << spec.short_name << '\n';
        }
    }
}

inline void
CompletionHandler::suggest_values(std::span<const std::string_view> values,
                                  std::string_view prefix, std::ostream &out) {
    for (auto value : values) {
        if (prefix.empty() || value.starts_with(prefix)) {
            out << value << '\n';
        }
    }
}

template <typename Config>
void CompletionHandler::write_completions(const OptionParser<Config> &parser,
                                          std::string_view line,
                                          std::size_t point,
                                          std::ostream &out) {
    point = std::min(point, line.size());
    line = line.substr(0, point);
    if (line.empty()) {
        return;
    }

    bool program_seen = false;
    bool options_enabled = true;
    std::size_t positional_index = 0;
    const OptionSpec<Config> *pending_value = nullptr;
    std::size_t cursor = 0;
    std::string decoded_storage;
    std::string_view current;
    bool current_present = false;

    const auto advance_positional = [&parser, &positional_index]() {
        if (positional_index >= parser.positional_specs().size()) {
            return;
        }
        const auto arity = parser.positional_specs()[positional_index].arity;
        if (arity == PositionalArity::ExactlyOne ||
            arity == PositionalArity::ZeroOrOne) {
            ++positional_index;
        }
    };

    CompletionWord completion_word;
    while (next_completion_word(line, cursor, completion_word)) {
        const std::string_view word =
            decode_completion_word(completion_word, decoded_storage);
        if (!completion_word.complete) {
            current = word;
            current_present = true;
            break;
        }

        if (!program_seen) {
            program_seen = true;
            continue;
        }

        if (pending_value != nullptr) {
            pending_value = nullptr;
            continue;
        }

        if (options_enabled && word == "--") {
            options_enabled = false;
            continue;
        }

        if (options_enabled && word.starts_with("--")) {
            if (const auto *spec = parser.find_option(word.substr(2));
                spec != nullptr && spec->takes_value) {
                pending_value = spec;
            }
            continue;
        }

        if (options_enabled && word.starts_with('-') && word.size() > 1U) {
            for (std::size_t position = 1; position < word.size(); ++position) {
                const auto *spec = parser.find_option(word[position]);
                if (spec == nullptr) {
                    break;
                }
                if (spec->takes_value) {
                    if (position + 1U == word.size()) {
                        pending_value = spec;
                    }
                    break;
                }
            }
            continue;
        }

        advance_positional();
    }

    if (!program_seen) {
        return;
    }

    if (pending_value != nullptr) {
        suggest_values(pending_value->allowed_values, current, out);
        return;
    }

    const PositionalSpec<Config> *positional = nullptr;
    if (positional_index < parser.positional_specs().size()) {
        positional = &parser.positional_specs()[positional_index];
    }

    if (!options_enabled) {
        if (positional != nullptr) {
            suggest_values(positional->allowed_values, current, out);
        }
        return;
    }

    if (current.starts_with('-')) {
        suggest_options(parser, current, out);
        return;
    }

    if (positional != nullptr) {
        suggest_values(positional->allowed_values, current, out);
    }
    if (!current_present) {
        suggest_options(parser, current, out);
    }
}

template <typename Config>
int CompletionHandler::handle_completion(const OptionParser<Config> &parser) {
    const char *line_env = std::getenv("COMP_LINE");
    const char *point_env = std::getenv("COMP_POINT");
    if (line_env == nullptr || point_env == nullptr) {
        return 0;
    }

    const std::string_view line(line_env);
    const std::string_view point_text(point_env);
    std::size_t point = line.size();
    std::size_t requested_point = 0;
    const auto conversion =
        std::from_chars(point_text.data(),
                        point_text.data() + point_text.size(), requested_point);
    if (conversion.ec == std::errc{} &&
        conversion.ptr == point_text.data() + point_text.size()) {
        point = std::min(requested_point, line.size());
    }

    write_completions(parser, line, point, std::cout);
    return 0;
}

} // namespace cli
