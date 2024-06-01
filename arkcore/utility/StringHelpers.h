#pragma once

namespace StringHelpers {

    template<typename F>
    void forEachToken(std::string_view text, char delimiter, F&& tokenCallback)
    {
        size_t findOffset = 0;
        size_t tokenIndex = 0;

        size_t characterIndex;
        while ((characterIndex = text.find(delimiter, findOffset)) != std::string::npos) {
            std::string_view token = text.substr(findOffset, characterIndex - findOffset);
            tokenCallback(token, tokenIndex++);
            findOffset = characterIndex + 1;
        }

        std::string_view finalToken = text.substr(findOffset);
        tokenCallback(finalToken, tokenIndex);
    }

}
