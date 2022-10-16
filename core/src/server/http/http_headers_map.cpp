#include <userver/server/http/http_headers_map.hpp>

#include <unordered_map>

#include <userver/utils/assert.hpp>
#include <userver/utils/str_icase.hpp>
#include <server/http/http_headers_flat_map.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

namespace {

// TODO : unify
constexpr std::string_view kCrlf = "\r\n";
constexpr std::string_view kKeyValueHeaderSeparator = ": ";

}

class HttpHeadersMap::Impl final {
 public:
  std::string_view Find(std::string_view key) const {
    if (is_flat_) {
      return flat_map_.Find(key);
    } else {
      const auto it = map_.find(std::string{key});
      if (it != map_.end()) return it->second;
      return {};
    }
  }

  void Erase(std::string_view key) {
    if (is_flat_) {
      flat_map_.Erase(key);
    } else {
      map_.erase(std::string{key});
    }
  }

  void Add(std::string_view key, std::string_view value) {
    if (is_flat_) {
      if (!flat_map_.Add(key, value)) {
        SwitchToMap();
        Add(key, value);
      }
    } else {
      map_[std::string{key}] = value;
    }
  }

  void AddOrUpdate(std::string_view key, std::string_view value) {
    if (is_flat_) {
      if (!flat_map_.AddOrUpdate(key, value)) {
        SwitchToMap();
        AddOrUpdate(key, value);
      }
    } else {
      std::string key_str{key};
      const auto header_it = map_.find(key_str);
      if (header_it == map_.end()) {
        map_.emplace(std::move(key_str), value);
      } else {
        header_it->second = value;
      }
    }
  }

  bool Contains(std::string_view key) const {
    if (is_flat_) {
      return flat_map_.Contains(key);
    } else {
      return map_.find(std::string{key}) != map_.end();
    }
  }

  void Clear() {
    is_flat_ ? flat_map_.Clear() : map_.clear();
  }

  HttpSerializedHeaders GetSerializedHeaders() const {
    if (is_flat_) {
      return HttpSerializedHeaders{flat_map_.GetPlainData()};
    } else {
      return HttpSerializedHeaders{SerializeMapHeaders()};
    }
  }

  std::size_t Size() const {
    return is_flat_ ? flat_map_.Size() : map_.size();
  }

 private:
  std::string SerializeMapHeaders() const {
    UASSERT(!is_flat_);

    std::string result;
    // TODO : better reserve
    result.reserve(1024);

    for (const auto& [key, value] : map_) {
      result.append(key);
      result.append(kKeyValueHeaderSeparator);
      result.append(value);
      result.append(kCrlf);
    }

    return result;
  }

  void SwitchToMap() {
    UASSERT(is_flat_);
    is_flat_ = false;

    map_.reserve(flat_map_.Size());
    for (const auto& [key, value] : flat_map_) {
      map_.emplace(std::piecewise_construct,
                   std::tie(key.data_, key.size_),
                   std::forward_as_tuple(value));
    }
  }

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
  return impl_->Size();
}

HttpSerializedHeaders HttpHeadersMap::GetSerializedHeaders() const {
  return impl_->GetSerializedHeaders();
}

}

USERVER_NAMESPACE_END
