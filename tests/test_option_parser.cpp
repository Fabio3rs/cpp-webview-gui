#include "option_parser.hpp"

#include <gtest/gtest.h>

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::literals;

struct TestConfig {
    bool verbose = false;
    std::string option_value;
    std::string input;
    std::string output;
    std::vector<std::string> remaining;
};

inline constexpr std::array<std::string_view, 2> MODE_VALUES = {"fast", "safe"};
inline constexpr std::array<std::string_view, 2> INPUT_VALUES = {"first",
                                                                 "final"};
inline constexpr std::array<std::string_view, 2> OUTPUT_VALUES = {"json",
                                                                  "text"};
inline constexpr std::array<std::string_view, 1> DASH_VALUES = {"-literal"};
inline constexpr std::array<std::string_view, 3> SPACED_VALUES = {
    "two words", "two worlds", "other"};

inline constexpr std::array<cli::OptionSpec<TestConfig>, 3> OPTIONS = {{
    {
        .long_name = "verbose",
        .short_name = 'v',
        .takes_value = false,
        .value_name = "",
        .help = "Enable verbose output",
        .long_help = "Enable detailed diagnostic output.",
        .allowed_values = {},
        .apply = [](TestConfig &config,
                    std::string_view) { config.verbose = true; },
    },
    {
        .long_name = "mode",
        .short_name = 'm',
        .takes_value = true,
        .value_name = "<mode>",
        .help = "Select mode",
        .long_help = "Select the execution mode.",
        .allowed_values = MODE_VALUES,
        .apply = [](TestConfig &config,
                    std::string_view value) { config.option_value = value; },
    },
    {
        .long_name = "value",
        .short_name = 'x',
        .takes_value = true,
        .value_name = "<value>",
        .help = "Set an arbitrary value",
        .long_help = "Set an arbitrary option value.",
        .allowed_values = {},
        .apply = [](TestConfig &config,
                    std::string_view value) { config.option_value = value; },
    },
}};

inline constexpr std::array<cli::PositionalSpec<TestConfig>, 3> POSITIONALS = {{
    {
        .name = "INPUT",
        .help = "Input value",
        .long_help = "The primary input value.",
        .allowed_values = {},
        .apply = [](TestConfig &config,
                    std::string_view value) { config.input = value; },
        .arity = cli::PositionalArity::ExactlyOne,
    },
    {
        .name = "OUTPUT",
        .help = "Optional output value",
        .long_help = "The optional output value.",
        .allowed_values = {},
        .apply = [](TestConfig &config,
                    std::string_view value) { config.output = value; },
        .arity = cli::PositionalArity::ZeroOrOne,
    },
    {
        .name = "ARG",
        .help = "Remaining values",
        .long_help = "Additional values passed to the command.",
        .allowed_values = {},
        .apply =
            [](TestConfig &config, std::string_view value) {
                config.remaining.emplace_back(value);
            },
        .arity = cli::PositionalArity::ZeroOrMore,
    },
}};

inline constexpr std::array<cli::PositionalSpec<TestConfig>, 1>
    REQUIRED_POSITIONAL = {{
        {
            .name = "INPUT",
            .help = "Input value",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &config,
                        std::string_view value) { config.input = value; },
            .arity = cli::PositionalArity::ExactlyOne,
        },
    }};

inline constexpr std::array<cli::PositionalSpec<TestConfig>, 1>
    VARIADIC_POSITIONAL = {{
        {
            .name = "ARG",
            .help = "Argument values",
            .long_help = "",
            .allowed_values = {},
            .apply =
                [](TestConfig &config, std::string_view value) {
                    config.remaining.emplace_back(value);
                },
            .arity = cli::PositionalArity::ZeroOrMore,
        },
    }};

template <std::size_t Size>
cli::ParseResult<TestConfig>
parse(const cli::OptionParser<TestConfig> &parser,
      const std::array<std::string_view, Size> &arguments) {
    return parser.parse(std::span<const std::string_view>{arguments});
}

TEST(OptionParserPositionals, ParsesRequiredOptionalAndVariadicArguments) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, POSITIONALS);
    constexpr std::array arguments = {"app"sv, "source"sv, "destination"sv,
                                      "one"sv, "two"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_EQ(result.config->input, "source");
    EXPECT_EQ(result.config->output, "destination");
    EXPECT_EQ(result.config->remaining,
              (std::vector<std::string>{"one", "two"}));
}

TEST(OptionParserPositionals, ReportsMissingExactlyOneArgument) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, REQUIRED_POSITIONAL);
    constexpr std::array arguments = {"app"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Missing required positional argument 'INPUT'");
    EXPECT_FALSE(result.config.has_value());
}

TEST(OptionParserPositionals, RequiresAtLeastOneValueForOneOrMore) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "FILE",
            .help = "Files",
            .long_help = "",
            .allowed_values = {},
            .apply =
                [](TestConfig &config, std::string_view value) {
                    config.remaining.emplace_back(value);
                },
            .arity = cli::PositionalArity::OneOrMore,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Missing required positional argument 'FILE'");
}

TEST(OptionParserPositionals, ParsesEveryValueForOneOrMore) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "FILE",
            .help = "Files",
            .long_help = "",
            .allowed_values = {},
            .apply =
                [](TestConfig &config, std::string_view value) {
                    config.remaining.emplace_back(value);
                },
            .arity = cli::PositionalArity::OneOrMore,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv, "one"sv, "two"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_EQ(result.config->remaining,
              (std::vector<std::string>{"one", "two"}));
}

TEST(OptionParserPositionals, AllowsOptionsAfterPositionalArguments) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, POSITIONALS);
    constexpr std::array arguments = {"app"sv, "source"sv, "--verbose"sv,
                                      "destination"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_TRUE(result.config->verbose);
    EXPECT_EQ(result.config->input, "source");
    EXPECT_EQ(result.config->output, "destination");
}

TEST(OptionParserPositionals, TreatsEverythingAfterDelimiterAsPositional) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, VARIADIC_POSITIONAL);
    constexpr std::array arguments = {"app"sv, "--"sv, "-v"sv, "--help"sv,
                                      "--"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_FALSE(result.config->verbose);
    EXPECT_EQ(result.config->remaining,
              (std::vector<std::string>{"-v", "--help", "--"}));
}

TEST(OptionParserPositionals, ConsumesDelimiterAsAnOptionValue) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &config,
                        std::string_view value) { config.input = value; },
            .arity = cli::PositionalArity::ZeroOrOne,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv, "--value"sv, "--"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_EQ(result.config->option_value, "--");
    EXPECT_TRUE(result.config->input.empty());
}

TEST(OptionParserPositionals, TreatsSingleDashAsPositional) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, REQUIRED_POSITIONAL);
    constexpr std::array arguments = {"app"sv, "-"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_EQ(result.config->input, "-");
}

TEST(OptionParserPositionals, HelpBeforeDelimiterBypassesRequiredArgument) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, REQUIRED_POSITIONAL);
    constexpr std::array arguments = {"app"sv, "--help"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::ShowHelp);
    EXPECT_FALSE(result.config.has_value());
}

TEST(OptionParserPositionals, ReportsUnexpectedExtraArgument) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, REQUIRED_POSITIONAL);
    constexpr std::array arguments = {"app"sv, "first"sv, "extra"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message, "Unexpected positional argument 'extra'");
}

TEST(OptionParserPositionals, RejectsDisallowedPositionalValue) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = INPUT_VALUES,
            .apply = [](TestConfig &config,
                        std::string_view value) { config.input = value; },
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv, "invalid"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Invalid value 'invalid' for positional argument 'INPUT'. "
              "Allowed: first, final");
}

TEST(OptionParserPositionals, ReportsCallbackException) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = {},
            .apply =
                [](TestConfig &, std::string_view) {
                    throw std::runtime_error("positional callback failed");
                },
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv, "value"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message, "positional callback failed");
}

TEST(OptionParserPositionals, RejectsVariadicArgumentBeforeLastPosition) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "ARGS",
            .help = "Arguments",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::ZeroOrMore,
        },
        cli::PositionalSpec<TestConfig>{
            .name = "OUTPUT",
            .help = "Output",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::ZeroOrOne,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Invalid positional specification 'ARGS': variadic argument "
              "must be last");
}

TEST(OptionParserPositionals, RejectsRequiredArgumentAfterOptionalPosition) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "OUTPUT",
            .help = "Output",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::ZeroOrOne,
        },
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::ExactlyOne,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Invalid positional specification 'INPUT': required argument "
              "cannot follow an optional argument");
}

TEST(OptionParserPositionals, RejectsNullCallback) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = {},
            .apply = nullptr,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Invalid positional specification 'INPUT': callback cannot be "
              "null");
}

TEST(OptionParserPositionals, RejectsEmptyName) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "",
            .help = "Input",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    constexpr std::array arguments = {"app"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Invalid positional specification: name cannot be empty");
}

TEST(OptionParserPositionals, GeneratesConcisePositionalHelp) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, POSITIONALS);

    const std::string help = parser.generate_help("app");

    EXPECT_NE(help.find("Usage: app [OPTIONS] <INPUT> [OUTPUT] [ARG...]"),
              std::string::npos);
    EXPECT_NE(help.find("Positional arguments:"), std::string::npos);
    EXPECT_NE(help.find("<INPUT>"), std::string::npos);
    EXPECT_NE(help.find("Treat following arguments as positional"),
              std::string::npos);
}

TEST(OptionParserPositionals, GeneratesVerbosePositionalHelp) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "Detailed positional documentation.",
            .allowed_values = INPUT_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);

    const std::string help = parser.generate_help_verbose("app");

    EXPECT_NE(help.find("POSITIONAL ARGUMENTS"), std::string::npos);
    EXPECT_NE(help.find("Detailed positional documentation."),
              std::string::npos);
    EXPECT_NE(help.find("Allowed values: first, final"), std::string::npos);
}

TEST(OptionParserPositionals, ShowsOneOrMoreCardinalityInSynopsis) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "FILE",
            .help = "Files",
            .long_help = "",
            .allowed_values = {},
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::OneOrMore,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);

    const std::string help = parser.generate_help("app");

    EXPECT_NE(help.find("Usage: app [OPTIONS] <FILE>..."), std::string::npos);
}

TEST(OptionParserCompletion, SuggestsPositionalValuesAndOptionsAtEmptyWord) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = INPUT_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app ";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    const std::string_view suggestions = output.view();
    EXPECT_NE(suggestions.find("first\n"), std::string::npos);
    EXPECT_NE(suggestions.find("--verbose\n"), std::string::npos);
}

TEST(OptionParserCompletion, FiltersCurrentPositionalValueByPrefix) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = INPUT_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app fi";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    EXPECT_EQ(output.view(), "first\nfinal\n");
}

TEST(OptionParserCompletion, CompletesQuotedPositionalPrefix) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = SPACED_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app \"two w";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    EXPECT_EQ(output.view(), "two words\ntwo worlds\n");
}

TEST(OptionParserCompletion, CompletesEscapedPositionalPrefix) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = SPACED_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app two\\ w";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    EXPECT_EQ(output.view(), "two words\ntwo worlds\n");
}

TEST(OptionParserCompletion, KeepsOneOrMorePositionAfterACompletedValue) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Inputs",
            .long_help = "",
            .allowed_values = INPUT_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::OneOrMore,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app first f";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    EXPECT_EQ(output.view(), "first\nfinal\n");
}

TEST(OptionParserCompletion, SuggestsAllowedOptionValuesBeforePositionals) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, REQUIRED_POSITIONAL);
    std::ostringstream output;
    constexpr std::string_view line = "app --mode ";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    EXPECT_EQ(output.view(), "fast\nsafe\n");
}

TEST(OptionParserCompletion, AdvancesToNextPositionalSpecification) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = INPUT_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
        cli::PositionalSpec<TestConfig>{
            .name = "OUTPUT",
            .help = "Output",
            .long_help = "",
            .allowed_values = OUTPUT_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
            .arity = cli::PositionalArity::ZeroOrOne,
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app first ";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    const std::string_view suggestions = output.view();
    EXPECT_NE(suggestions.find("json\n"), std::string::npos);
    EXPECT_EQ(suggestions.find("first\n"), std::string::npos);
}

TEST(OptionParserCompletion, SuppressesOptionSuggestionsAfterDelimiter) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = DASH_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app -- -";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    const std::string_view suggestions = output.view();
    EXPECT_EQ(suggestions, "-literal\n");
    EXPECT_EQ(suggestions.find("--verbose"), std::string::npos);
}

TEST(OptionParserCompletion, RecognizesQuotedDelimiter) {
    constexpr std::array positionals = {
        cli::PositionalSpec<TestConfig>{
            .name = "INPUT",
            .help = "Input",
            .long_help = "",
            .allowed_values = DASH_VALUES,
            .apply = [](TestConfig &, std::string_view) {},
        },
    };
    const cli::OptionParser<TestConfig> parser(OPTIONS, positionals);
    std::ostringstream output;
    constexpr std::string_view line = "app '--' -";

    cli::CompletionHandler::write_completions(parser, line, line.size(),
                                              output);

    EXPECT_EQ(output.view(), "-literal\n");
}

TEST(OptionParserRegression, ParserWithoutPositionalsKeepsDefaultBehavior) {
    const cli::OptionParser<TestConfig> parser(OPTIONS);
    constexpr std::array arguments = {"app"sv, "-v"sv, "--mode"sv, "safe"sv};

    const auto result = parse(parser, arguments);

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_TRUE(result.config->verbose);
    EXPECT_EQ(result.config->option_value, "safe");
}

TEST(OptionParserRegression, ParserWithoutPositionalsRejectsPlainArgument) {
    const cli::OptionParser<TestConfig> parser(OPTIONS);
    constexpr std::array arguments = {"app"sv, "plain"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message, "Unexpected positional argument 'plain'");
}

TEST(OptionParserRegression,
     ParserWithoutPositionalsRejectsArgumentsAfterDelimiter) {
    const cli::OptionParser<TestConfig> parser(OPTIONS);
    constexpr std::array arguments = {"app"sv, "--"sv, "-literal"sv};

    const auto result = parse(parser, arguments);

    EXPECT_EQ(result.status, cli::ParseStatus::Error);
    EXPECT_EQ(result.error_message,
              "Unexpected positional argument '-literal'");
}

TEST(OptionParserRegression, ArgvOverloadParsesWithoutAnIntermediateViewArray) {
    const cli::OptionParser<TestConfig> parser(OPTIONS, REQUIRED_POSITIONAL);
    std::array first = {'a', 'p', 'p', '\0'};
    std::array second = {'v', 'a', 'l', 'u', 'e', '\0'};
    std::array<char *, 2> arguments = {first.data(), second.data()};

    const auto result =
        parser.parse(static_cast<int>(arguments.size()), arguments.data());

    ASSERT_EQ(result.status, cli::ParseStatus::Ok);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_EQ(result.config->input, "value");
}

} // namespace
