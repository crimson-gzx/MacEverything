#pragma once
#include <string>

namespace me {
/// Case-insensitive lowercasing: ASCII fast-path + CoreFoundation Unicode fallback.
std::string toLower(const std::string& s);
}
