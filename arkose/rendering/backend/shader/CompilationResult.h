#pragma once

#include <string>
#include <vector>

template<typename T>
class CompilationResult {
public:

	virtual bool success() const = 0;
    virtual std::string errorMessage() const = 0;
    
    virtual std::vector<std::string> const& includedFiles() const = 0;

    using const_iterator = T const*;

    virtual const_iterator begin() const = 0;
    virtual const_iterator end() const = 0;

};
