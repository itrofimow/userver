#pragma once

#include <memory>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

namespace impl {
class IAmqpChannel;
}

class Connection;
class ConsumerBaseImpl;

class ChannelPtr final {
 public:
  ChannelPtr(std::shared_ptr<Connection>&& connection,
             impl::IAmqpChannel* channel);
  ~ChannelPtr();

  ChannelPtr(ChannelPtr&& other) noexcept;

  impl::IAmqpChannel* Get() const;

  impl::IAmqpChannel& operator*() const;
  impl::IAmqpChannel* operator->() const noexcept;

 private:
  friend class ConsumerBaseImpl;
  void Adopt();

  void Release() noexcept;

  std::shared_ptr<Connection> connection_;
  std::unique_ptr<impl::IAmqpChannel> channel_;

  bool should_return_to_pool_{true};
};

}  // namespace urabbitmq

USERVER_NAMESPACE_END