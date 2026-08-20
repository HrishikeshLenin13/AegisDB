#pragma once

#include <stdexcept>
#include <string>

namespace aegisdb {

class AegisError : public std::runtime_error {
 public:
  explicit AegisError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace aegisdb
