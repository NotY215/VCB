#pragma once
#include "vcb/Ir.hpp"
#include <stdexcept>
#include <string>

namespace vcb {

    class ParseError : public std::runtime_error {
    public:
        int line = 0;
        ParseError(std::string msg, int l)
            : std::runtime_error(std::move(msg)), line(l) {}
    };

    Module parseFile(const std::string& path);
    Module parseText(const std::string& text, const std::string& origin = "<inline>");

} // namespace vcb