#pragma once

#include "core/Types.h"
#include <ark/copying.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class CommandLine final {
public:
    static bool initialize(int argc, char** argv);
    static void shutdown();

    ARK_NON_COPYABLE(CommandLine)

    static bool hasArgument(char const* argument);

    static bool hasNamedArgument(char const* argument);
    static std::string_view namedArgumentValue(char const* argument);

    template<typename T>
    static std::optional<T> namedArgumentValue(char const* argument);

private:
    explicit CommandLine(std::vector<std::string>&&, std::unordered_map<std::string, std::string>&&);
    static CommandLine* s_commandLine;

    std::vector<std::string> m_arguments;
    std::unordered_map<std::string, std::string> m_namedArguments;
};

template<typename T>
std::optional<T> CommandLine::namedArgumentValue(char const* argument)
{
    using namespace std::string_view_literals;

    std::string_view value = namedArgumentValue(argument);
    if (value == ""sv) {
        return {};
    }

    if constexpr (std::is_integral_v<T>) {
        long long parsedValue = std::atoll(value.data());
        return narrow_cast<T>(parsedValue);
    }

    if constexpr (std::is_floating_point_v<T>) {
        double parsedDouble = std::atof(value.data());
        return static_cast<T>(parsedDouble);
    }

    return {};
}
