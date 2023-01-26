#pragma once

/// @file userver/utils/trivial_map.hpp
/// @brief Bidirectional map|sets over string literals or other trivial types.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <userver/utils/assert.hpp>

USERVER_NAMESPACE_BEGIN

namespace utils {

namespace impl {

constexpr bool HasUppercaseAscii(std::string_view value) noexcept {
  for (auto c : value) {
    if ('A' <= c && c <= 'Z') return true;
  }

  return false;
}

constexpr bool ICaseEqualLowercase(std::string_view lowercase,
                                   std::string_view y) noexcept {
  const auto size = lowercase.size();
  UASSERT(size == y.size());
  constexpr char kLowerToUpperMask = static_cast<char>(~unsigned{32});
  for (std::size_t i = 0; i < size; ++i) {
    const auto lowercase_c = lowercase[i];
    UASSERT(!('A' <= lowercase_c && lowercase_c <= 'Z'));
    if (lowercase_c != y[i]) {
      if (!('a' <= lowercase_c && lowercase_c <= 'z') ||
          (lowercase_c & kLowerToUpperMask) != y[i]) {
        return false;
      }
    }
  }

  return true;
}

template <typename First, typename Second>
class SwitchByFirst final {
 public:
  constexpr explicit SwitchByFirst(First search) noexcept : search_(search) {}

  constexpr SwitchByFirst& Case(First first, Second second) noexcept {
    if (!result_ && search_ == first) {
      result_.emplace(second);
    }
    return *this;
  }

  [[nodiscard]] constexpr std::optional<Second> Extract() noexcept {
    return result_;
  }

 private:
  const First search_;
  std::optional<Second> result_{};
};

template <typename First>
class SwitchByFirst<First, void> final {
 public:
  constexpr explicit SwitchByFirst(First search) noexcept : search_(search) {}

  constexpr SwitchByFirst& Case(First first) noexcept {
    if (!found_ && search_ == first) {
      found_ = true;
    }
    return *this;
  }

  [[nodiscard]] constexpr bool Extract() noexcept { return found_; }

 private:
  const First search_;
  bool found_{false};
};

template <typename Second>
class SwitchByFirstICase final {
 public:
  constexpr explicit SwitchByFirstICase(std::string_view search) noexcept
      : search_(search) {}

  constexpr SwitchByFirstICase& Case(std::string_view first,
                                     Second second) noexcept {
    UASSERT_MSG(!impl::HasUppercaseAscii(first),
                fmt::format("String literal '{}' in utils::Switch*::Case() "
                            "should be in lower case",
                            first));
    if (!result_ && search_.size() == first.size() &&
        impl::ICaseEqualLowercase(first, search_)) {
      result_.emplace(second);
    }
    return *this;
  }

  [[nodiscard]] constexpr std::optional<Second> Extract() noexcept {
    return result_;
  }

 private:
  const std::string_view search_;
  std::optional<Second> result_{};
};

template <>
class SwitchByFirstICase<void> final {
 public:
  constexpr explicit SwitchByFirstICase(std::string_view search) noexcept
      : search_(search) {}

  constexpr SwitchByFirstICase& Case(std::string_view first) noexcept {
    UASSERT_MSG(!impl::HasUppercaseAscii(first),
                fmt::format("String literal '{}' in utils::Switch*::Case() "
                            "should be in lower case",
                            first));
    if (!found_ && search_.size() == first.size() &&
        impl::ICaseEqualLowercase(first, search_)) {
      found_ = true;
    }
    return *this;
  }

  [[nodiscard]] constexpr bool Extract() const noexcept { return found_; }

 private:
  const std::string_view search_;
  bool found_{false};
};

template <typename First, typename Second>
class SwitchBySecond final {
 public:
  constexpr explicit SwitchBySecond(Second search) noexcept : search_(search) {}

  constexpr SwitchBySecond& Case(First first, Second second) noexcept {
    if (!result_ && search_ == second) {
      result_.emplace(first);
    }
    return *this;
  }

  [[nodiscard]] constexpr std::optional<First> Extract() noexcept {
    return result_;
  }

 private:
  const Second search_;
  std::optional<First> result_{};
};

template <typename First, typename Second>
class SwitchTypesDetected final {
 public:
  using first_type = First;
  using second_type = Second;

  constexpr SwitchTypesDetected& Case(First, Second) noexcept { return *this; }
};

template <typename First>
class SwitchTypesDetected<First, void> final {
 public:
  using first_type = First;
  using second_type = void;

  constexpr SwitchTypesDetected& Case(First) noexcept { return *this; }
};

class SwitchTypesDetector final {
 public:
  constexpr SwitchTypesDetector& operator()() noexcept { return *this; }

  template <typename First, typename Second>
  constexpr auto Case(First, Second) noexcept {
    using first_type =
        std::conditional_t<std::is_convertible_v<First, std::string_view>,
                           std::string_view, First>;
    using second_type =
        std::conditional_t<std::is_convertible_v<Second, std::string_view>,
                           std::string_view, Second>;
    return SwitchTypesDetected<first_type, second_type>{};
  }

  template <typename First>
  constexpr auto Case(First) noexcept {
    using first_type =
        std::conditional_t<std::is_convertible_v<First, std::string_view>,
                           std::string_view, First>;
    return SwitchTypesDetected<first_type, void>{};
  }
};

class CaseCounter final {
 public:
  template <typename First, typename Second>
  constexpr CaseCounter& Case(First, Second) noexcept {
    ++count_;
    return *this;
  }

  template <typename First>
  constexpr CaseCounter& Case(First) noexcept {
    ++count_;
    return *this;
  }

  [[nodiscard]] constexpr std::size_t Extract() const noexcept {
    return count_;
  }

 private:
  std::size_t count_{0};
};

class CaseDescriber final {
 public:
  template <typename First, typename Second>
  CaseDescriber& Case(First first, Second second) noexcept {
    if (!description_.empty()) {
      description_ += ", ";
    }

    description_ += fmt::format("('{}', '{}')", first, second);

    return *this;
  }

  [[nodiscard]] std::string Extract() && noexcept {
    return std::move(description_);
  }

 private:
  std::string description_{};
};

class CaseFirstDescriber final {
 public:
  template <typename First>
  CaseFirstDescriber& Case(First first) noexcept {
    if (!description_.empty()) {
      description_ += ", ";
    }

    description_ += fmt::format("'{}'", first);

    return *this;
  }

  template <typename First, typename Second>
  CaseFirstDescriber& Case(First first, Second) noexcept {
    return Case(first);
  }

  [[nodiscard]] std::string Extract() && noexcept {
    return std::move(description_);
  }

 private:
  std::string description_{};
};

}  // namespace impl

/// @ingroup userver_containers
///
/// @brief Bidirectional unordered map for trivial types, including string
/// literals; could be efficiently used as a unordered non-bidirectional map.
///
/// @snippet shared/src/utils/trivial_map_test.cpp  sample string bimap
///
/// utils::TrivialBiMap and utils::TrivialSet are known to outperform
/// std::unordered_map if:
/// * there's 32 or less elements in map/set
/// * or keys are string literals and all of them differ in length.
///
/// Implementation of string search is \b very efficient due to
/// modern compilers optimize it to a switch by input string
/// length and an integral comparison (rather than a std::memcmp call). In other
/// words, it usually takes O(1) to find the match in the map.
///
/// The same story with integral or enum mappings - compiler optimizes them
/// into a switch and it usually takes O(1) to find the match.
///
/// @snippet shared/src/utils/trivial_map_test.cpp  sample bidir bimap
///
/// For a single value Case statements see @ref utils::TrivialSet.
template <typename BuilderFunc>
class TrivialBiMap final {
  using TypesPair =
      std::invoke_result_t<const BuilderFunc&, impl::SwitchTypesDetector>;

 public:
  using First = typename TypesPair::first_type;
  using Second = typename TypesPair::second_type;

  constexpr TrivialBiMap(BuilderFunc&& func) noexcept : func_(std::move(func)) {
    static_assert(std::is_empty_v<BuilderFunc>,
                  "Mapping function should not capture variables");
    static_assert(std::is_trivially_copyable_v<First>,
                  "First type in Case must be trivially copyable");
    static_assert(!std::is_void_v<Second>,
                  "If second type in Case is missing, use "
                  "utils::TrivialSet instead of utils::TrivialBiMap");
    static_assert(std::is_trivially_copyable_v<Second>,
                  "Second type in Case must be trivially copyable");
  }

  constexpr std::optional<Second> TryFindByFirst(First value) const noexcept {
    return func_(
               [value]() { return impl::SwitchByFirst<First, Second>{value}; })
        .Extract();
  }

  constexpr std::optional<First> TryFindBySecond(Second value) const noexcept {
    return func_(
               [value]() { return impl::SwitchBySecond<First, Second>{value}; })
        .Extract();
  }

  template <class T>
  constexpr auto TryFind(T value) const noexcept
      -> std::conditional_t<std::is_convertible_v<T, First>,
                            std::optional<Second>, std::optional<First>> {
    static_assert(
        !std::is_convertible_v<T, First> || !std::is_convertible_v<T, Second>,
        "Ambiguous conversion, use TryFindByFirst/TryFindBySecond instead");

    if constexpr (std::is_convertible_v<T, First>) {
      return TryFindByFirst(value);
    } else {
      return TryFindBySecond(value);
    }
  }

  /// @brief Case insensitive search for value.
  ///
  /// For efficiency reasons, first parameter in Case() should be lower case
  /// string literal.
  constexpr std::conditional_t<std::is_convertible_v<std::string_view, First>,
                               std::optional<Second>, std::optional<First>>
  TryFindICase(std::string_view value) const noexcept {
    static_assert(!std::is_convertible_v<std::string_view, First> ||
                      !std::is_convertible_v<std::string_view, Second>,
                  "Ambiguous conversion. TODO: implement "
                  "TryFindByFirstICase/TryFindBySecondICase");

    return func_([value]() { return impl::SwitchByFirstICase<Second>{value}; })
        .Extract();
  }

  constexpr std::size_t size() const noexcept {
    return func_([]() { return impl::CaseCounter{}; }).Extract();
  }

  /// Returns a string of comma separated quoted values of Case parameters.
  ///
  /// \b Example: "('a', '1'), ('b', '2'), ('c', '3')"
  ///
  /// Parameters of Case should be formattable.
  std::string Describe() const {
    return func_([]() { return impl::CaseDescriber{}; }).Extract();
  }

  /// Returns a string of comma separated quoted values of first Case
  /// parameters.
  ///
  /// \b Example: "'a', 'b', 'c'"
  ///
  /// First parameters of Case should be formattable.
  std::string DescribeFirst() const {
    return func_([]() { return impl::CaseFirstDescriber{}; }).Extract();
  }

 private:
  const BuilderFunc func_;
};

/// @ingroup userver_containers
///
/// @brief Unordered set for trivial types, including string literals.
///
/// For a two-value Case statements or efficiency notes
/// see @ref utils::TrivialBimap.
template <typename BuilderFunc>
class TrivialSet final {
  using TypesPair =
      std::invoke_result_t<const BuilderFunc&, impl::SwitchTypesDetector>;

 public:
  using First = typename TypesPair::first_type;
  using Second = typename TypesPair::second_type;

  constexpr TrivialSet(BuilderFunc&& func) noexcept : func_(std::move(func)) {
    static_assert(std::is_empty_v<BuilderFunc>,
                  "Mapping function should not capture variables");
    static_assert(std::is_trivially_copyable_v<First>,
                  "First type in Case must be trivially copyable");
    static_assert(std::is_void_v<Second>,
                  "Second type in Case should be skipped in utils::TrivialSet");
  }

  constexpr bool Contains(First value) const noexcept {
    return func_(
               [value]() { return impl::SwitchByFirst<First, Second>{value}; })
        .Extract();
  }

  constexpr bool ContainsICase(std::string_view value) const noexcept {
    static_assert(std::is_convertible_v<First, std::string_view>,
                  "ContainsICase works only with std::string_view");

    return func_([value]() { return impl::SwitchByFirstICase<void>{value}; })
        .Extract();
  }

  constexpr std::size_t size() const noexcept {
    return func_([]() { return impl::CaseCounter{}; }).Extract();
  }

  /// Returns a string of comma separated quoted values of Case parameters.
  ///
  /// \b Example: "'a', 'b', 'c'"
  ///
  /// Parameters of Case should be formattable.
  std::string Describe() const {
    return func_([]() { return impl::CaseFirstDescriber{}; }).Extract();
  }

 private:
  const BuilderFunc func_;
};

}  // namespace utils

USERVER_NAMESPACE_END
