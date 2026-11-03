#include "mfdp/errors.h"

namespace mfdp {

const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::Conflict: return "Conflict";
    case ErrorCode::Io: return "Io";
    case ErrorCode::UnsupportedLanguage: return "UnsupportedLanguage";
    case ErrorCode::ParseFailed: return "ParseFailed";
    case ErrorCode::AdapterUnavailable: return "AdapterUnavailable";
    case ErrorCode::AdapterFailed: return "AdapterFailed";
    case ErrorCode::CorruptArtifact: return "CorruptArtifact";
    case ErrorCode::TruncatedArtifact: return "TruncatedArtifact";
    case ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
    case ErrorCode::TooLarge: return "TooLarge";
    case ErrorCode::OutputExists: return "OutputExists";
    case ErrorCode::StaleSource: return "StaleSource";
    case ErrorCode::NoMatch: return "NoMatch";
  }
  return "Unknown";
}

Error::Error(ErrorCode code, const std::string& message, const std::string& file, uint32_t line,
             uint32_t column)
    : std::runtime_error(message), code_(code), file_(file), line_(line), column_(column) {}

std::string Error::describe() const {
  std::string out = std::string("[") + errorCodeName(code_) + "] ";
  if (!file_.empty()) {
    out += file_;
    if (line_ > 0) {
      out += ":" + std::to_string(line_);
      if (column_ > 0) out += ":" + std::to_string(column_);
    }
    out += ": ";
  }
  out += what();
  return out;
}

}
