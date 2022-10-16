#pragma once

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

  std::string_view GetPlainData() const;
 private:
  void WriteToBuffer(std::string_view data);
  void EraseAtIndex(size_t index);

  struct StringView final {
    char* data_{nullptr};
    std::size_t size_{0};

    bool operator ==(std::string_view rhs) const;
  };

  static constexpr std::size_t kMaxHeaders = 20;
  static constexpr std::size_t kMaxDataLength = 1024;

  std::pair<StringView, std::string_view> headers_[kMaxHeaders]{};
  char data_[kMaxDataLength]{};

  std::size_t headers_count_{0};
  std::size_t data_length_{0};
};

}

USERVER_NAMESPACE_END
