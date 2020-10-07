#pragma once

#include <optional>

#include <components/component_context.hpp>
#include <engine/mutex.hpp>
#include <engine/task/task_processor.hpp>
#include <server/handlers/handler_base.hpp>
#include <server/http/request_handler_base.hpp>
#include <server/request/request_base.hpp>
#include <utils/token_bucket.hpp>

#include "handler_info_index.hpp"

namespace server::http {

class HttpRequestHandler final : public RequestHandlerBase {
 public:
  HttpRequestHandler(
      const components::ComponentContext& component_context,
      const std::optional<std::string>& logger_access_component,
      const std::optional<std::string>& logger_access_tskv_component,
      bool is_monitor);

  using NewRequestHook =
      std::function<void(std::shared_ptr<request::RequestBase>)>;
  void SetNewRequestHook(NewRequestHook hook);

  engine::TaskWithResult<void> StartRequestTask(
      std::shared_ptr<request::RequestBase> request) const override;

  void DisableAddHandler();
  void AddHandler(const handlers::HttpHandlerBase& handler,
                  engine::TaskProcessor& task_processor);
  const HandlerInfoIndex& GetHandlerInfoIndex() const override;

  const logging::LoggerPtr& LoggerAccess() const noexcept override {
    return logger_access_;
  }
  const logging::LoggerPtr& LoggerAccessTskv() const noexcept override {
    return logger_access_tskv_;
  }

  void SetRpsRatelimit(std::optional<size_t> rps);

 private:
  logging::LoggerPtr logger_access_;
  logging::LoggerPtr logger_access_tskv_;

  // handler_infos_mutex_ is used for pushing handlers into handler_info_index_
  // before server start. After start handler_info_index_ is read only and
  // synchronization is not needed.
  engine::Mutex handler_infos_mutex_;
  HandlerInfoIndex handler_info_index_;

  std::atomic<bool> add_handler_disabled_;
  bool is_monitor_;
  NewRequestHook new_request_hook_;
  mutable utils::TokenBucket rate_limit_;
};

}  // namespace server::http
