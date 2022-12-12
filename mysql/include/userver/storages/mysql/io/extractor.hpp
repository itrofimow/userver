#pragma once

#include <vector>

#include <userver/storages/mysql/io/binder.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mysql::io {

class ExtractorBase {
 public:
  virtual void Reserve(std::size_t size) = 0;
  virtual impl::bindings::OutputBindings& BindNextRow() = 0;
  virtual void RollbackLastRow() = 0;
  virtual std::size_t ColumnsCount() const = 0;
};

template <typename T>
class TypedExtractor final : public ExtractorBase {
 public:
  TypedExtractor() : binder_{boost::pfr::tuple_size_v<T>} {}

  void Reserve(std::size_t size) final;

  impl::bindings::OutputBindings& BindNextRow() final;

  void RollbackLastRow() final;

  std::size_t ColumnsCount() const final;

  std::vector<T>&& ExtractData() &&;

 private:
  ResultBinder binder_;
  std::vector<T> data_;
};

template <typename T>
void TypedExtractor<T>::Reserve(std::size_t size) {
  data_.reserve(size);
}

template <typename T>
impl::bindings::OutputBindings& TypedExtractor<T>::BindNextRow() {
  data_.emplace_back();
  return binder_.BindTo(data_.back());
}

template <typename T>
void TypedExtractor<T>::RollbackLastRow() {
  data_.pop_back();
}

template <typename T>
std::size_t TypedExtractor<T>::ColumnsCount() const {
  return boost::pfr::tuple_size_v<T>;
}

template <typename T>
std::vector<T>&& TypedExtractor<T>::ExtractData() && {
  return std::move(data_);
}

}  // namespace storages::mysql::io

USERVER_NAMESPACE_END
