#include "CommandLine.h"

#include "core/Assert.h"

CommandLine* CommandLine::s_commandLine { nullptr };

CommandLine::CommandLine(std::vector<std::string>&& arguments, std::unordered_map<std::string, std::string>&& namedArguments)
    : m_arguments(std::move(arguments))
    , m_namedArguments(std::move(namedArguments))
{
}

bool CommandLine::initialize(int argc, char** argv)
{
    std::vector<std::string> arguments;
    std::unordered_map<std::string, std::string> namedArguments;

    std::optional<std::string> prevArgument = {};
    for (int idx = 1; idx < argc; ++idx) {
        char const* str = argv[idx];

        bool isArgument = str[0] == '-';

        if (!isArgument && prevArgument.has_value()) {
            namedArguments[*prevArgument] = str;
            prevArgument = {};
        } else if (isArgument) {
            if (prevArgument) {
                arguments.push_back(*prevArgument);
            }
            prevArgument = str;
        }
    }

    if (prevArgument.has_value()) {
        arguments.push_back(*prevArgument);
    }

    s_commandLine = new CommandLine(std::move(arguments), std::move(namedArguments));

    return true;
}

void CommandLine::shutdown()
{
    delete s_commandLine;
    s_commandLine = nullptr;
}

bool CommandLine::hasArgument(char const* argument)
{
    ARKOSE_ASSERT(s_commandLine != nullptr);
    auto const& arguments = s_commandLine->m_arguments;

    for (std::string const& existingArgument : arguments) {
        if (argument == existingArgument) {
            return true;
        }
    }

    return false;
}

bool CommandLine::hasNamedArgument(char const* argument)
{
    ARKOSE_ASSERT(s_commandLine != nullptr);
    auto const& namedArguments = s_commandLine->m_namedArguments;

    auto entry = namedArguments.find(argument);
    return entry != namedArguments.end();
}

std::string_view CommandLine::namedArgumentValue(char const* argument)
{
    ARKOSE_ASSERT(s_commandLine != nullptr);
    auto const& namedArguments = s_commandLine->m_namedArguments;

    auto entry = namedArguments.find(argument);
    if (entry != namedArguments.end()) {
        return entry->second;
    } else {
        using namespace std::string_view_literals;
        return ""sv;
    }
}
