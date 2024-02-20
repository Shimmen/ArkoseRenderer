#include "CommandLine.h"

#include "core/Assert.h"

CommandLine* CommandLine::s_commandLine { nullptr };

CommandLine::CommandLine(std::vector<std::string>&& arguments)
    : m_arguments(std::move(arguments))
{
}

bool CommandLine::initialize(int argc, char** argv)
{
    std::vector<std::string> arguments;
    for (int idx = 1; idx < argc; ++idx) {
        arguments.emplace_back(argv[idx]);
    }

    s_commandLine = new CommandLine(std::move(arguments));

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
