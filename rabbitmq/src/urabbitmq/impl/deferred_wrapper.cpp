#include "deferred_wrapper.hpp"

#include <amqpcpp.h>

#include <urabbitmq/make_shared_enabler.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq::impl {

void DeferredWrapper::Fail(const char* message) {
  if (is_signaled_) return;

  is_signaled_.store(true);
  error_.emplace(message);
  event_.Send();
}

void DeferredWrapper::Ok() {
  is_signaled_.store(true);
  event_.Send();
}

void DeferredWrapper::Wait(engine::Deadline deadline) {
  if (!event_.WaitForEventUntil(deadline)) {
    throw std::runtime_error{"operation timeout"};
  }

  if (error_.has_value()) {
    throw std::runtime_error{*error_};
  }
}

DeferredWrapper::DeferredWrapper() = default;

std::shared_ptr<DeferredWrapper> DeferredWrapper::Create() {
  return std::make_shared<MakeSharedEnabler<DeferredWrapper>>();
}

void DeferredWrapper::Wrap(AMQP::Deferred& deferred) {
  deferred.onSuccess([wrap = shared_from_this()] { wrap->Ok(); })
      .onError([wrap = shared_from_this()](const char* error) {
        wrap->Fail(error);
      });
}

ResponseAwaiter::ResponseAwaiter(engine::SemaphoreLock&& lock)
    : lock_{std::move(lock)}, wrapper_{DeferredWrapper::Create()} {}

ResponseAwaiter::~ResponseAwaiter() = default;

ResponseAwaiter::ResponseAwaiter(ResponseAwaiter&& other) noexcept = default;

void ResponseAwaiter::Wait(engine::Deadline deadline) const {
  GetWrapper()->Wait(deadline);
}

const std::shared_ptr<DeferredWrapper>& ResponseAwaiter::GetWrapper() const {
  return wrapper_;
}

}  // namespace urabbitmq::impl

USERVER_NAMESPACE_END