#include "channel_ptr.hpp"

#include <urabbitmq/impl/amqp_channel.hpp>

#include <urabbitmq/connection.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

ChannelPtr::ChannelPtr(std::shared_ptr<Connection>&& connection,
                       impl::IAmqpChannel* channel)
    : connection_{std::move(connection)}, channel_{channel} {}

ChannelPtr::~ChannelPtr() { Release(); }

ChannelPtr::ChannelPtr(ChannelPtr&& other) noexcept {
  Release();
  connection_ = std::move(other.connection_);
  channel_ = std::move(other.channel_);
}

impl::IAmqpChannel* ChannelPtr::Get() const { return channel_.get(); }

impl::IAmqpChannel& ChannelPtr::operator*() const { return *Get(); }

impl::IAmqpChannel* ChannelPtr::operator->() const noexcept { return Get(); }

void ChannelPtr::Release() noexcept {
  if (!channel_) return;

  connection_->Release(channel_.release());
}

}  // namespace urabbitmq

USERVER_NAMESPACE_END
