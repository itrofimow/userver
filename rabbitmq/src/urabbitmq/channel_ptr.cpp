#include "channel_ptr.hpp"

#include <urabbitmq/impl/amqp_channel.hpp>

#include <urabbitmq/channel_pool.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

ChannelPtr::ChannelPtr(std::shared_ptr<ChannelPool>&& pool,
                       impl::IAmqpChannel* channel)
    : pool_{std::move(pool)}, channel_{channel} {}

ChannelPtr::~ChannelPtr() { Release(); }

ChannelPtr::ChannelPtr(ChannelPtr&& other) noexcept {
  Release();
  pool_ = std::move(other.pool_);
  channel_ = std::move(other.channel_);
  should_return_to_pool_ = other.should_return_to_pool_;
}

impl::IAmqpChannel* ChannelPtr::Get() const {
  if (!pool_->IsWriteable()) {
    throw std::runtime_error{"Chill with your writes"};
  }
  return channel_.get();
}

void ChannelPtr::Adopt() {
  pool_->NotifyChannelAdopted();
  should_return_to_pool_ = false;
}

void ChannelPtr::Release() noexcept {
  if (!channel_ || !should_return_to_pool_) return;

  pool_->Release(std::move(channel_));
}

}  // namespace urabbitmq

USERVER_NAMESPACE_END
