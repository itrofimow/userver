#pragma once

#include <cmath>

#include <utils/assert.hpp>

namespace formats::common {

/// Common restrictions to floating type values serialization
template <typename ExceptionType, typename Float>
Float ValidateFloat(Float value) {
  if (std::isnan(value)) {
    UASSERT_MSG(false, "Floating point nan value serialization is forbidden");
    throw ExceptionType("Floating point nan value serialization is forbidden");
  }
  if (std::isinf(value)) {
    UASSERT_MSG(false, "Floating point inf value serialization is forbidden");
    throw ExceptionType("Floating point inf value serialization is forbidden");
  }
  return value;
}

}  // namespace formats::common
