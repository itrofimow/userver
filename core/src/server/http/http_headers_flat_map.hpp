#pragma once

#include <array>
#include <string_view>

USERVER_NAMESPACE_BEGIN

namespace server::http {

class HttpHeadersFlatMap final {
 public:
  std::string_view Find(std::string_view key) const;

  void Erase(std::string_view key);

  bool CanAdd(std::string_view key, std::string_view value) const;
  bool Add(std::string_view key, std::string_view value);
  bool AddOrUpdate(std::string_view key, std::string_view value);

  bool Contains(std::string_view key) const;
  void Clear();

  std::size_t Size() const;

  auto begin() { return headers_.begin(); }
  auto begin() const { return headers_.cbegin(); }

  auto end() {
    return headers_count_ == kMaxHeaders ? headers_.end() : headers_.begin() + headers_count_;
  }
  auto end() const {
    return headers_count_ == kMaxHeaders ? headers_.cend() : headers_.cbegin() + headers_count_;
  }
  auto cend() const {
    return headers_.cend();
  }

  std::string_view GetPlainData() const;

  struct StringView final {
    char* data_{nullptr};
    std::size_t size_{0};

    bool operator ==(std::string_view rhs) const;
  };
 private:
  void WriteToBuffer(std::string_view data);
  void EraseAtIndex(size_t index);

  static constexpr std::size_t kMaxHeaders = 32;
  static constexpr std::size_t kMaxDataLength = 1024;

  std::array<std::pair<StringView, std::string_view>, kMaxHeaders> headers_{};
  char data_[kMaxDataLength]{};

  std::size_t headers_count_{0};
  std::size_t data_length_{0};
};

}

USERVER_NAMESPACE_END
