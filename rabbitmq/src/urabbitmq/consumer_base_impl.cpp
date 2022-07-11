#include "consumer_base_impl.hpp"

#include <string>

#include <fmt/format.h>

#include <userver/concurrent/background_task_storage.hpp>
#include <userver/engine/task/task.hpp>
#include <userver/logging/log.hpp>
#include <userver/tracing/span.hpp>
#include <userver/utils/async.hpp>

#include <urabbitmq/impl/amqp_channel.hpp>
#include <urabbitmq/impl/deferred_wrapper.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

ConsumerBaseImpl::ConsumerBaseImpl(ChannelPtr&& channel,
                                   const ConsumerSettings& settings)
    : dispatcher_{engine::current_task::GetTaskProcessor()},
      queue_name_{settings.queue.GetUnderlying()},
      channel_ptr_{std::move(channel)},
      channel_{dynamic_cast<impl::AmqpChannel*>(channel_ptr_.Get())} {
  if (!channel_) {
    throw std::runtime_error{
        "Shouldn't happen, consumer shouldn't be created on a reliable "
        "channel"};
  }
  // We take ownership of the channel, because if it remains pooled
  // things get messy with lifetimes and callbacks
  channel_ptr_.Adopt();

  auto deferred = impl::DeferredWrapper::Create();

  const auto prefetch_count = settings.prefetch_count;
  channel_->GetEvThread().RunInEvLoopSync([this, deferred, prefetch_count] {
    deferred->Wrap(channel_->channel_->setQos(prefetch_count));
  });

  deferred->Wait();
}

ConsumerBaseImpl::~ConsumerBaseImpl() { Stop(); }

void ConsumerBaseImpl::Start(DispatchCallback cb) {
  if (started_) {
    throw std::logic_error{"Consumer is already started."};
  }
  if (stopped_) {
    throw std::logic_error{"Consumer has been explicitly stopped."};
  }
  dispatch_callback_ = std::move(cb);

  channel_->GetEvThread().RunInEvLoopSync([this] {
    channel_->channel_->onError([this](const char*) { broken_ = true; });
    channel_->channel_->consume(queue_name_)
        .onSuccess([this](const std::string& consumer_tag) {
          consumer_tag_.emplace(consumer_tag);
        })
        .onMessage([this](const AMQP::Message& message,
                                          uint64_t delivery_tag, bool) {
          // We received a message but won't ack it, so it will be requeued
          // at some point
          if (!stopped_in_ev_) {
            OnMessage(message, delivery_tag);
          }
        });
  });
  started_ = true;
}

void ConsumerBaseImpl::Stop() {
  if (!started_ || stopped_) return;

  // First mark the channel as stopped and try to cancel the consumer
  channel_->GetEvThread().RunInEvLoopSync([this] {
    // This ensures we stop dispatching tasks even if we can't cancel
    stopped_in_ev_ = true;
    if (consumer_tag_.has_value()) {
      channel_->channel_->cancel(*consumer_tag_);
    }
  });
  // Cancel all the active dispatched tasks
  bts_->CancelAndWait();

  // Destroy the channel: at this point all the remaining tasks are stopped,
  // consumer is either stopped or in unknown state - that could happen if we
  // didn't receive onSuccess callback yet.
  // I'm not sure whether consumer.onMessage could fire in channel destructor,
  // so we guard against that via `stopped_in_ev`
  auto channel_thread = channel_->GetEvThread();
  {
    [[maybe_unused]] ChannelPtr tmp{std::move(channel_ptr_)};
  }
  // channel destruction could potentially set this, and since
  // it's not synchronized we destroy it in ev
  channel_thread.RunInEvLoopSync([this] {
    if (consumer_tag_.has_value()) {
      consumer_tag_.reset();
    }
  });

  stopped_ = true;
}

bool ConsumerBaseImpl::IsBroken() const { return broken_; }

void ConsumerBaseImpl::OnMessage(const AMQP::Message& message,
                                 uint64_t delivery_tag) {
  UASSERT(channel_->GetEvThread().IsInEvThread());

  std::string span_name{fmt::format("consume_{}", queue_name_)};
  std::string message_data{message.body(), message.bodySize()};

  bts_->Detach(engine::AsyncNoSpan(
      dispatcher_, [this, message = std::move(message_data),
                    span_name = std::move(span_name), delivery_tag]() mutable {
        tracing::Span span{std::move(span_name)};

        bool success = false;
        try {
          dispatch_callback_(std::move(message));
          success = true;
        } catch (const std::exception& ex) {
          LOG_ERROR() << "Failed to process the consumed message, " << ex.what()
                      << "; would requeue";
        }

        if (success)
          channel_->Ack(delivery_tag);
        else
          channel_->Reject(delivery_tag, true);
      }));
}

}  // namespace urabbitmq

USERVER_NAMESPACE_END