#pragma once

#include <ark/copying.h>
#include <vector>
#include <string>

class CommandLine final {
public:
    static bool initialize(int argc, char** argv);
    static void shutdown();

    ARK_NON_COPYABLE(CommandLine)

    static bool hasArgument(char const* argument);

private:
    explicit CommandLine(std::vector<std::string>&&);
    static CommandLine* s_commandLine;

    std::vector<std::string> m_arguments;
};