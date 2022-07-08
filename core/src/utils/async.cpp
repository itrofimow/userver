#include <userver/utils/async.hpp>

#include <engine/task/task_context.hpp>
#include <tracing/span_impl.hpp>
#include <userver/engine/task/inherited_variable.hpp>
#include <userver/tracing/span.hpp>

USERVER_NAMESPACE_BEGIN

namespace utils::impl {

struct SpanWrapCall::Impl {
  explicit Impl(std::string&& name);

  tracing::Span::Impl span_impl_;
  tracing::Span span_;
  engine::impl::task_local::Storage storage_;
};

SpanWrapCall::Impl::Impl(std::string&& name)
    : span_impl_(std::move(name)), span_(span_impl_) {
  if (engine::current_task::GetCurrentTaskContextUnchecked()) {
    storage_.InheritFrom(engine::impl::task_local::GetCurrentStorage());
  }
}

SpanWrapCall::SpanWrapCall(std::string&& name) : pimpl_(std::move(name)) {}

void SpanWrapCall::DoBeforeInvoke() {
  engine::impl::task_local::GetCurrentStorage().InitializeFrom(
      std::move(pimpl_->storage_));
  pimpl_->span_.AttachToCoroStack();
}

SpanWrapCall::~SpanWrapCall() = default;

}  // namespace utils::impl

USERVER_NAMESPACE_END
