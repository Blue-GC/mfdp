#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mfdp {

enum class ErrorCode {
  InvalidArgument,
  Conflict,
  Io,
  UnsupportedLanguage,
  ParseFailed,
  AdapterUnavailable,
  AdapterFailed,
  CorruptArtifact,
  TruncatedArtifact,
  UnsupportedVersion,
  TooLarge,
  OutputExists,
  StaleSource,
  NoMatch
};

const char* errorCodeName(ErrorCode code);

class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, const std::string& message, const std::string& file = std::string(),
        uint32_t line = 0, uint32_t column = 0);

  ErrorCode code() const noexcept { return code_; }
  const std::string& file() const noexcept { return file_; }
  uint32_t line() const noexcept { return line_; }
  uint32_t column() const noexcept { return column_; }
  std::string describe() const;

 private:
  ErrorCode code_;
  std::string file_;
  uint32_t line_;
  uint32_t column_;
};

}
