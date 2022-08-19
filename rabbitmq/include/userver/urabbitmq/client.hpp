#pragma once

/// @file userver/urabbitmq/client.hpp
/// @brief @copybrief urabbitmq::Client

#include <memory>

#include <userver/clients/dns/resolver_fwd.hpp>
#include <userver/formats/json_fwd.hpp>
#include <userver/utils/fast_pimpl.hpp>

#include <userver/urabbitmq/broker_interface.hpp>
#include <userver/urabbitmq/client_settings.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

class AdminChannel;
class Channel;
class ReliableChannel;
class ConsumerBase;
class ClientImpl;

/// @ingroup userver_clients
///
/// @brief Interface for communicating with a RabbitMQ cluster.
///
/// Usually retrieved from components::RabbitMQ component.
class Client : public std::enable_shared_from_this<Client>,
               public IAdminInterface,
               public IChannelInterface,
               public IReliableChannelInterface {
 public:
  /// Client factory function
  /// @param resolver asynchronous DNS resolver
  /// @param settings client settings
  static std::shared_ptr<Client> Create(clients::dns::Resolver& resolver,
                                        const ClientSettings& settings);
  /// Client destructor
  ~Client();

  void DeclareExchange(const Exchange& exchange, Exchange::Type type,
                       utils::Flags<Exchange::Flags> flags,
                       engine::Deadline deadline) override;

  void DeclareExchange(const Exchange& exchange, Exchange::Type type,
                       engine::Deadline deadline) override {
    DeclareExchange(exchange, type, {}, deadline);
  }

  void DeclareExchange(const Exchange& exchange,
                       engine::Deadline deadline) override {
    DeclareExchange(exchange, Exchange::Type::kFanOut, {}, deadline);
  }

  void DeclareQueue(const Queue& queue, utils::Flags<Queue::Flags> flags,
                    engine::Deadline deadline) override;

  void DeclareQueue(const Queue& queue, engine::Deadline deadline) override {
    DeclareQueue(queue, {}, deadline);
  }

  void BindQueue(const Exchange& exchange, const Queue& queue,
                 const std::string& routing_key,
                 engine::Deadline deadline) override;

  void RemoveExchange(const Exchange& exchange,
                      engine::Deadline deadline) override;

  void RemoveQueue(const Queue& queue, engine::Deadline deadline) override;

  /// @brief Get an administrative interface for the broker.
  AdminChannel GetAdminChannel();

  void Publish(const Exchange& exchange, const std::string& routing_key,
               const std::string& message, MessageType type,
               engine::Deadline deadline) override;

  /// @brief Get a publisher interface for the broker.
  Channel GetChannel();

  void PublishReliable(const Exchange& exchange, const std::string& routing_key,
                       const std::string& message, MessageType type,
                       engine::Deadline deadline) override;

  /// @brief Get a reliable publisher interface for the broker
  /// (publisher-confirms)
  ReliableChannel GetReliableChannel();

  /// Get cluster statistics
  formats::json::Value GetStatistics() const;

 protected:
  Client(clients::dns::Resolver& resolver, const ClientSettings& settings);

 private:
  friend class ConsumerBase;
  utils::FastPimpl<ClientImpl, 232, 8> impl_;
};

}  // namespace urabbitmq

USERVER_NAMESPACE_END
