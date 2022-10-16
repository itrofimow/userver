#pragma once

#include <string>
#include <string_view>

USERVER_NAMESPACE_BEGIN

namespace server::http {

class HttpSerializedHeaders final {
 public:
  explicit HttpSerializedHeaders(std::string data);
  explicit HttpSerializedHeaders(std::string_view data);

  std::string_view GetData() const;
 private:
  std::string optional_data_;
  std::string_view data_view_;
};

}

USERVER_NAMESPACE_END
