#include <userver/server/http/http_serialized_headers.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

HttpSerializedHeaders::HttpSerializedHeaders(std::string data)
    : optional_data_{std::move(data)}, data_view_{optional_data_} {}

HttpSerializedHeaders::HttpSerializedHeaders(std::string_view data)
    : data_view_{data} {}

std::string_view HttpSerializedHeaders::GetData() const {
  return data_view_;
}

}

USERVER_NAMESPACE_END
