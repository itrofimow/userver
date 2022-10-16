#pragma once

#include <string_view>

#include <userver/server/http/http_serialized_headers.hpp>
#include <userver/utils/fast_pimpl.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

class HttpHeadersMap final {
 public:
  HttpHeadersMap();
  ~HttpHeadersMap();

  std::string_view Find(std::string_view key) const;

  void Erase(std::string_view key);

  void Add(std::string_view key, std::string_view value);
  void AddOrUpdate(std::string_view key, std::string_view value);

  bool Contains(std::string_view key) const;

  void Clear();

  std::size_t Size() const;

  HttpSerializedHeaders GetSerializedHeaders() const;
 private:
  class Impl;
  utils::FastPimpl<Impl, 2136, 8> impl_;
};

}

USERVER_NAMESPACE_END
