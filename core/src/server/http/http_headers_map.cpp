#include <userver/server/http/http_headers_map.hpp>

#include <unordered_map>

#include <userver/utils/str_icase.hpp>
#include <server/http/http_headers_flat_map.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

class HttpHeadersMap::Impl final {
 public:
  std::string_view Find(std::string_view key) const {
    if (is_flat_) return flat_map_.Find(key);

    return {};
  }

  void Erase(std::string_view key) {
    if (is_flat_) {
      return flat_map_.Erase(key);
    }
  }

  void Add(std::string_view key, std::string_view value) {
    if (is_flat_) {
      if (!flat_map_.Add(key, value)) {
        is_flat_ = false;

        // TODO : fill map_ and Add
      }
    }
  }

  void AddOrUpdate(std::string_view key, std::string_view value) {
    if (is_flat_) {
      if (!flat_map_.AddOrUpdate(key, value)) {
        is_flat_ = false;

        // TODO : fill map_ and AddOrUpdate
      }
    }
  }

  bool Contains(std::string_view key) const {
    if (is_flat_) return flat_map_.Contains(key);

    return false;
  }

  void Clear() {
    if (is_flat_) {
      return flat_map_.Clear();
    }
  }

  HttpSerializedHeaders GetSerializedHeaders() const {
    if (is_flat_) return HttpSerializedHeaders{flat_map_.GetPlainData()};

    // TODO : fix me
    return HttpSerializedHeaders{std::string{}};
  }

  std::size_t Size() const {
    if (is_flat_) return flat_map_.Size();

    // TODO : fix me
    return 0;
  }

 private:
  HttpHeadersFlatMap flat_map_{};
  std::unordered_map<std::string, std::string, utils::StrIcaseHash,
                     utils::StrIcaseEqual> map_{};

  bool is_flat_{true};
};

HttpHeadersMap::HttpHeadersMap() = default;

HttpHeadersMap::~HttpHeadersMap() = default;

std::string_view HttpHeadersMap::Find(std::string_view key) const {
  return impl_->Find(key);
}

void HttpHeadersMap::Erase(std::string_view key) {
  return impl_->Erase(key);
}

void HttpHeadersMap::Add(std::string_view key, std::string_view value) {
  return impl_->Add(key, value);
}

void HttpHeadersMap::AddOrUpdate(std::string_view key, std::string_view value) {
  return impl_->AddOrUpdate(key, value);
}

bool HttpHeadersMap::Contains(std::string_view key) const {
  return impl_->Contains(key);
}

void HttpHeadersMap::Clear() {
  return impl_->Clear();
}

std::size_t HttpHeadersMap::Size() const {

}

HttpSerializedHeaders HttpHeadersMap::GetSerializedHeaders() const {
  return impl_->GetSerializedHeaders();
}

}

USERVER_NAMESPACE_END
