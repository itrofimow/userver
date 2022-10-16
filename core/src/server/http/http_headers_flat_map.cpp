#include <server/http/http_headers_flat_map.hpp>

#include <cstring>

#include <userver/utils/assert.hpp>
#include <userver/utils/str_icase.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

namespace {

constexpr std::string_view kCrlf = "\r\n";
constexpr std::string_view kKeyValueHeaderSeparator = ": ";
constexpr char kPlaceholder = ' ';

std::size_t CalcSizeNeeded(std::string_view key, std::string_view value) {
  return key.size() + kKeyValueHeaderSeparator.size() + value.size() + kCrlf.size();
}

}

bool HttpHeadersFlatMap::StringView::operator==(std::string_view rhs) const {
  return utils::StrIcaseEqual{}(std::string_view{data_, size_}, rhs);
}

std::string_view HttpHeadersFlatMap::Find(std::string_view key) const {
  if (key.empty()) return {};

  for (std::size_t i = 0; i < headers_count_; ++i) {
    if (headers_[i].first == key) {
      return headers_[i].second;
    }
  }

  return {};
}

void HttpHeadersFlatMap::Erase(std::string_view key) {
  if (key.empty()) return;

  for (std::size_t i = 0; i < headers_count_; ++i) {
    if (headers_[i].first == key) {
      EraseAtIndex(i);
      return;
    }
  }
}

bool HttpHeadersFlatMap::CanAdd(std::string_view key, std::string_view value) const {
  return headers_count_ < kMaxHeaders &&
    data_length_ + CalcSizeNeeded(key, value) <= kMaxDataLength;
}

bool HttpHeadersFlatMap::Add(std::string_view key, std::string_view value) {
  UINVARIANT(!key.empty() && !value.empty(), "oops");
  if (!CanAdd(key, value)) return false;

  headers_[headers_count_].first = StringView{&data_[data_length_], key.size()};
  headers_[headers_count_].second = {
      &data_[data_length_ + key.size() + kKeyValueHeaderSeparator.size()],
      value.size()
  };
  ++headers_count_;

  WriteToBuffer(key);
  WriteToBuffer(kKeyValueHeaderSeparator);
  WriteToBuffer(value);
  WriteToBuffer(kCrlf);

  return true;
}

bool HttpHeadersFlatMap::AddOrUpdate(std::string_view key, std::string_view value) {
  for (size_t i = 0; i < headers_count_; ++i) {
    if (headers_[i].first == key) {
      if (headers_[i].second.size() >= value.size()) {
        char* header_value_start = headers_[i].first.data_ + headers_[i].first.size_ + kKeyValueHeaderSeparator.size();
        const auto len_diff = headers_[i].second.size() - value.size();

        std::memcpy(header_value_start, value.data(), value.size());
        header_value_start += value.size();

        std::memcpy(header_value_start, kCrlf.data(), kCrlf.size());
        header_value_start += kCrlf.size();

        if (len_diff != 0) {
          std::memset(header_value_start, kPlaceholder, len_diff);
        }
        return true;
      } else {
        EraseAtIndex(i);
        return Add(key, value);
      }
    }
  }

  return Add(key, value);
}

bool HttpHeadersFlatMap::Contains(std::string_view key) const {
  if (key.empty()) return false;

  for (size_t i = 0; i < headers_count_; ++i) {
    if (headers_[i].first == key) {
      return true;
    }
  }

  return false;
}

void HttpHeadersFlatMap::Clear() {
  headers_count_ = 0;
  data_length_ = 0;
}

std::size_t HttpHeadersFlatMap::Size() const {
  return headers_count_;
}

std::string_view HttpHeadersFlatMap::GetPlainData() const {
  return {data_, data_length_};
}

void HttpHeadersFlatMap::WriteToBuffer(std::string_view data) {
  std::memcpy(&data_[data_length_], data.data(), data.size());
  data_length_ += data.size();
}

void HttpHeadersFlatMap::EraseAtIndex(size_t index) {
  char* header_pair_begin = headers_[index].first.data_;
  const std::size_t header_pair_size = headers_[index].first.size_ + kKeyValueHeaderSeparator.size() +
                                       headers_[index].second.size() + kCrlf.size();

  std::memset(header_pair_begin, kPlaceholder, header_pair_size - kCrlf.size());

  headers_[index].first = {};
}

}

USERVER_NAMESPACE_END
