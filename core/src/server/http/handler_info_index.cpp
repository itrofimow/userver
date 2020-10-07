#include "handler_info_index.hpp"

#include <array>
#include <stdexcept>

#include <logging/logger.hpp>
#include <utils/assert.hpp>
#include <utils/overloaded.hpp>

#include <server/http/fixed_path_index.hpp>
#include <server/http/handler_methods.hpp>
#include <server/http/wildcard_path_index.hpp>

namespace server::http {

class HandlerInfoIndex::HandlerInfoIndexImpl final {
  using FallbackHandlersStorage =
      std::array<std::optional<HandlerInfo>, handlers::kFallbackHandlerMax + 1>;

 public:
  void AddHandler(const handlers::HttpHandlerBase& handler,
                  engine::TaskProcessor& task_processor);
  MatchRequestResult MatchRequest(HttpMethod method,
                                  const std::string& path) const;

  void SetFallbackHandler(const handlers::HttpHandlerBase& handler,
                          engine::TaskProcessor& task_processor);
  const HandlerInfo* GetFallbackHandler(handlers::FallbackHandler) const;

 private:
  impl::FixedPathIndex fixed_path_index_;
  impl::WildcardPathIndex wildcard_path_index_;
  FallbackHandlersStorage fallback_handlers_{};
};

void HandlerInfoIndex::HandlerInfoIndexImpl::AddHandler(
    const handlers::HttpHandlerBase& handler,
    engine::TaskProcessor& task_processor) {
  const auto& path = std::get<std::string>(handler.GetConfig().path);
  if (!impl::HasWildcardSpecificSymbols(path) &&
      (path.empty() || path.back() != '*')) {
    fixed_path_index_.AddHandler(handler, task_processor);
  } else {
    wildcard_path_index_.AddHandler(handler, task_processor);
  }
}

MatchRequestResult HandlerInfoIndex::HandlerInfoIndexImpl::MatchRequest(
    HttpMethod method, const std::string& path) const {
  MatchRequestResult match_result;
  if (fixed_path_index_.MatchRequest(method, path, match_result))
    return match_result;

  wildcard_path_index_.MatchRequest(method, path, match_result);
  return match_result;
}

void HandlerInfoIndex::HandlerInfoIndexImpl::SetFallbackHandler(
    const handlers::HttpHandlerBase& handler,
    engine::TaskProcessor& task_processor) {
  const auto& fallback =
      std::get<handlers::FallbackHandler>(handler.GetConfig().path);
  const auto index = static_cast<size_t>(fallback);
  UASSERT(index <= handlers::kFallbackHandlerMax);
  if (fallback_handlers_[index])
    throw std::runtime_error(fmt::format(
        "fallback {} handler already registered", ToString(fallback)));
  fallback_handlers_[index].emplace(task_processor, handler);
}

const HandlerInfo* HandlerInfoIndex::HandlerInfoIndexImpl::GetFallbackHandler(
    handlers::FallbackHandler fallback) const {
  const auto index = static_cast<size_t>(fallback);
  if (index > handlers::kFallbackHandlerMax || !fallback_handlers_[index])
    return nullptr;
  return &fallback_handlers_[index].value();
}

HandlerInfoIndex::HandlerInfoIndex()
    : impl_(std::make_unique<HandlerInfoIndexImpl>()) {}

HandlerInfoIndex::~HandlerInfoIndex() = default;

void HandlerInfoIndex::AddHandler(const handlers::HttpHandlerBase& handler,
                                  engine::TaskProcessor& task_processor) {
  std::visit(utils::Overloaded{[&](const std::string&) {
                                 impl_->AddHandler(handler, task_processor);
                               },
                               [&](handlers::FallbackHandler) {
                                 impl_->SetFallbackHandler(handler,
                                                           task_processor);
                               }},
             handler.GetConfig().path);
}

MatchRequestResult HandlerInfoIndex::MatchRequest(
    HttpMethod method, const std::string& path) const {
  return impl_->MatchRequest(method, path);
}

const HandlerInfo* HandlerInfoIndex::GetFallbackHandler(
    handlers::FallbackHandler fallback) const {
  return impl_->GetFallbackHandler(fallback);
}

}  // namespace server::http
