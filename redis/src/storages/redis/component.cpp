#include <userver/storages/redis/component.hpp>

#include <stdexcept>
#include <vector>

#include <storages/redis/impl/keyshard_impl.hpp>
#include <storages/redis/impl/subscribe_sentinel.hpp>
#include <userver/components/component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/redis/impl/sentinel.hpp>
#include <userver/storages/redis/impl/thread_pools.hpp>
#include <userver/storages/redis/reply.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/exceptions.hpp>
#include <userver/storages/secdist/secdist.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/statistics/aggregated_values.hpp>
#include <userver/utils/statistics/metadata.hpp>
#include <userver/utils/statistics/percentile_format_json.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <userver/storages/redis/client.hpp>
#include <userver/storages/redis/redis_config.hpp>
#include <userver/storages/redis/subscribe_client.hpp>

#include "client_impl.hpp"
#include "redis_secdist.hpp"
#include "subscribe_client_impl.hpp"
#include "userver/storages/redis/impl/base.hpp"

USERVER_NAMESPACE_BEGIN

namespace {

const auto kStatisticsName = "redis";
const auto kSubscribeStatisticsName = "redis-pubsub";

formats::json::ValueBuilder InstanceStatisticsToJson(
    const redis::InstanceStatistics& stats,
    const redis::MetricsSettings& metrics_settings, bool real_instance) {
  formats::json::ValueBuilder result(formats::json::Type::kObject);
  formats::json::ValueBuilder errors(formats::json::Type::kObject);
  formats::json::ValueBuilder states(formats::json::Type::kObject);

  if (metrics_settings.request_sizes_enabled) {
    result["request_sizes"]["1min"] =
        utils::statistics::PercentileToJson(stats.request_size_percentile);
    utils::statistics::SolomonSkip(result["request_sizes"]["1min"]);
  }
  if (metrics_settings.reply_sizes_enabled) {
    result["reply_sizes"]["1min"] =
        utils::statistics::PercentileToJson(stats.reply_size_percentile);
    utils::statistics::SolomonSkip(result["reply_sizes"]["1min"]);
  }
  if (metrics_settings.timings_enabled) {
    result["timings"]["1min"] =
        utils::statistics::PercentileToJson(stats.timings_percentile);
    utils::statistics::SolomonSkip(result["timings"]["1min"]);
  }
  if (metrics_settings.command_timings_enabled &&
      !stats.command_timings_percentile.empty()) {
    auto timings = result["command_timings"];
    utils::statistics::SolomonChildrenAreLabelValues(timings, "redis_command");
    for (const auto& [command, percentile] : stats.command_timings_percentile) {
      timings[command] = utils::statistics::PercentileToJson(percentile);
    }
  }

  result["reconnects"] = stats.reconnects;

  for (size_t i = 0; i <= redis::REDIS_ERR_MAX; ++i)
    errors[storages::redis::Reply::StatusToString(i)] = stats.error_count[i];
  utils::statistics::SolomonChildrenAreLabelValues(errors, "redis_error");
  result["errors"] = errors;

  if (real_instance) {
    result["last_ping_ms"] = stats.last_ping_ms;
    result["is_syncing"] = static_cast<int>(stats.is_syncing);
    result["offset_from_master"] = stats.offset_from_master;

    for (size_t i = 0;
         i <= static_cast<int>(redis::Redis::State::kDisconnectError); ++i) {
      auto state = static_cast<redis::Redis::State>(i);
      states[redis::Redis::StateToString(state)] = stats.state == state ? 1 : 0;
    }
    utils::statistics::SolomonChildrenAreLabelValues(states,
                                                     "redis_instance_state");
    result["state"] = states;

    long long session_time_ms =
        stats.state == redis::Redis::State::kConnected
            ? (redis::MillisecondsSinceEpoch() - stats.session_start_time)
                  .count()
            : 0;
    result["session-time-ms"] = session_time_ms;
  }

  return result;
}

formats::json::ValueBuilder ShardStatisticsToJson(
    const redis::ShardStatistics& shard_stats,
    const redis::MetricsSettings& metrics_settings) {
  formats::json::ValueBuilder result(formats::json::Type::kObject);
  formats::json::ValueBuilder insts(formats::json::Type::kObject);
  for (const auto& it2 : shard_stats.instances) {
    const auto& inst_name = it2.first;
    const auto& inst_stats = it2.second;
    insts[inst_name] =
        InstanceStatisticsToJson(inst_stats, metrics_settings, true);
  }
  utils::statistics::SolomonChildrenAreLabelValues(insts, "redis_instance");
  utils::statistics::SolomonSkip(insts);
  result["instances"] = insts;
  result["instances_count"] = shard_stats.instances.size();

  result["shard-total"] = InstanceStatisticsToJson(
      shard_stats.GetShardTotalStatistics(), metrics_settings, false);
  utils::statistics::SolomonSkip(result["shard-total"]);

  result["is_ready"] = shard_stats.is_ready ? 1 : 0;

  long long not_ready =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - shard_stats.last_ready_time)
          .count();
  result["not_ready_ms"] = shard_stats.is_ready ? 0 : not_ready;
  return result;
}

formats::json::ValueBuilder RedisStatisticsToJson(
    const std::shared_ptr<redis::Sentinel>& redis,
    const redis::MetricsSettings& metrics_settings) {
  formats::json::ValueBuilder result(formats::json::Type::kObject);
  formats::json::ValueBuilder masters(formats::json::Type::kObject);
  formats::json::ValueBuilder slaves(formats::json::Type::kObject);
  auto stats = redis->GetStatistics();

  for (const auto& it : stats.masters) {
    const auto& shard_name = it.first;
    const auto& shard_stats = it.second;
    masters[shard_name] = ShardStatisticsToJson(shard_stats, metrics_settings);
  }
  utils::statistics::SolomonChildrenAreLabelValues(masters, "redis_shard");
  utils::statistics::SolomonLabelValue(masters, "redis_instance_type");
  result["masters"] = masters;
  for (const auto& it : stats.slaves) {
    const auto& shard_name = it.first;
    const auto& shard_stats = it.second;
    slaves[shard_name] = ShardStatisticsToJson(shard_stats, metrics_settings);
  }
  utils::statistics::SolomonChildrenAreLabelValues(slaves, "redis_shard");
  utils::statistics::SolomonLabelValue(slaves, "redis_instance_type");
  result["slaves"] = slaves;
  result["sentinels"] = ShardStatisticsToJson(stats.sentinel, metrics_settings);
  utils::statistics::SolomonLabelValue(result["sentinels"],
                                       "redis_instance_type");

  result["shard-group-total"] = InstanceStatisticsToJson(
      stats.GetShardGroupTotalStatistics(), metrics_settings, false);
  utils::statistics::SolomonSkip(result["shard-group-total"]);

  result["errors"] = formats::json::Type::kObject;
  result["errors"]["redis_not_ready"] = stats.internal.redis_not_ready.load();
  utils::statistics::SolomonChildrenAreLabelValues(result["errors"],
                                                   "redis_error");
  return result;
}

formats::json::ValueBuilder PubsubChannelStatisticsToJson(
    const redis::PubsubChannelStatistics& stats, bool extra) {
  formats::json::ValueBuilder json(formats::json::Type::kObject);
  json["messages"]["count"] = stats.messages_count;
  json["messages"]["alien-count"] = stats.messages_alien_count;
  json["messages"]["size"] = stats.messages_size;

  if (extra) {
    auto diff = std::chrono::steady_clock::now() - stats.subscription_timestamp;
    json["subscribed-ms"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();

    auto inst_name = stats.server_id.GetDescription();
    if (inst_name.empty()) inst_name = "unknown";
    auto insts = json["instances"];
    insts[inst_name] = 1;
    utils::statistics::SolomonChildrenAreLabelValues(insts, "redis_instance");
    utils::statistics::SolomonSkip(insts);
  }
  return json;
}

formats::json::ValueBuilder PubsubShardStatisticsToJson(
    const redis::PubsubShardStatistics& stats, bool extra) {
  formats::json::ValueBuilder json(formats::json::Type::kObject);
  for (const auto& [ch_name, ch_stats] : stats.by_channel) {
    json[ch_name] = PubsubChannelStatisticsToJson(ch_stats, extra);
  }
  utils::statistics::SolomonChildrenAreLabelValues(json,
                                                   "redis_pubsub_channel");
  return json;
}

formats::json::ValueBuilder RedisSubscribeStatisticsToJson(
    const redis::SubscribeSentinel& redis) {
  auto stats = redis.GetSubscriberStatistics();
  formats::json::ValueBuilder result(formats::json::Type::kObject);

  formats::json::ValueBuilder by_shard(formats::json::Type::kObject);
  for (auto& it : stats.by_shard) {
    by_shard[it.first] = PubsubShardStatisticsToJson(it.second, true);
  }
  utils::statistics::SolomonChildrenAreLabelValues(by_shard, "redis_shard");
  utils::statistics::SolomonSkip(by_shard);
  result["by-shard"] = std::move(by_shard);

  auto total_stats = stats.SumByShards();
  result["shard-group-total"] = PubsubShardStatisticsToJson(total_stats, false);
  utils::statistics::SolomonSkip(result["shard-group-total"]);

  return result;
}

template <typename RedisGroup>
USERVER_NAMESPACE::secdist::RedisSettings GetSecdistSettings(
    components::Secdist& secdist_component, const RedisGroup& redis_group) {
  try {
    return secdist_component.Get()
        .Get<storages::secdist::RedisMapSettings>()
        .GetSettings(redis_group.config_name);
  } catch (const storages::secdist::SecdistError& ex) {
    LOG_ERROR() << "Failed to load redis config (db=" << redis_group.db
                << " config_name=" << redis_group.config_name << "): " << ex;
    throw;
  }
}

}  // namespace

namespace components {

struct RedisGroup {
  std::string db;
  std::string config_name;
  std::string sharding_strategy;
  bool allow_reads_from_master{false};
};

RedisGroup Parse(const yaml_config::YamlConfig& value,
                 formats::parse::To<RedisGroup>) {
  RedisGroup config;
  config.db = value["db"].As<std::string>();
  config.config_name = value["config_name"].As<std::string>();
  config.sharding_strategy = value["sharding_strategy"].As<std::string>("");
  config.allow_reads_from_master =
      value["allow_reads_from_master"].As<bool>(false);
  return config;
}

struct SubscribeRedisGroup {
  std::string db;
  std::string config_name;
  std::string sharding_strategy;
};

SubscribeRedisGroup Parse(const yaml_config::YamlConfig& value,
                          formats::parse::To<SubscribeRedisGroup>) {
  SubscribeRedisGroup config;
  config.db = value["db"].As<std::string>();
  config.config_name = value["config_name"].As<std::string>();
  config.sharding_strategy = value["sharding_strategy"].As<std::string>("");
  return config;
}

struct RedisPools {
  int sentinel_thread_pool_size;
  int redis_thread_pool_size;
};

RedisPools Parse(const yaml_config::YamlConfig& value,
                 formats::parse::To<RedisPools>) {
  RedisPools pools{};
  pools.sentinel_thread_pool_size =
      value["sentinel_thread_pool_size"].As<int>();
  pools.redis_thread_pool_size = value["redis_thread_pool_size"].As<int>();
  return pools;
}

Redis::Redis(const ComponentConfig& config,
             const ComponentContext& component_context)
    : LoggableComponentBase(config, component_context),
      config_(component_context.FindComponent<DynamicConfig>().GetSource()) {
  const auto& testsuite_redis_control =
      component_context.FindComponent<components::TestsuiteSupport>()
          .GetRedisControl();
  Connect(config, component_context, testsuite_redis_control);

  config_subscription_ =
      config_.UpdateAndListen(this, "redis", &Redis::OnConfigUpdate);

  auto& statistics_storage =
      component_context.FindComponent<components::StatisticsStorage>()
          .GetStorage();

  statistics_holder_ = statistics_storage.RegisterExtender(
      kStatisticsName,
      [this](const utils::statistics::StatisticsRequest& request) {
        return ExtendStatisticsRedis(request);
      });

  subscribe_statistics_holder_ = statistics_storage.RegisterExtender(
      kSubscribeStatisticsName,
      [this](const utils::statistics::StatisticsRequest& request) {
        return ExtendStatisticsRedisPubsub(request);
      });
}

std::shared_ptr<storages::redis::Client> Redis::GetClient(
    const std::string& name,
    USERVER_NAMESPACE::redis::RedisWaitConnected wait_connected) const {
  auto it = clients_.find(name);
  if (it == clients_.end())
    throw std::runtime_error(name + " redis client not found");
  it->second->WaitConnectedOnce(wait_connected);
  return it->second;
}

std::shared_ptr<redis::Sentinel> Redis::Client(const std::string& name) const {
  auto it = sentinels_.find(name);
  if (it == sentinels_.end())
    throw std::runtime_error(name + " redis client not found");
  return it->second;
}

std::shared_ptr<storages::redis::SubscribeClient> Redis::GetSubscribeClient(
    const std::string& name,
    USERVER_NAMESPACE::redis::RedisWaitConnected wait_connected) const {
  auto it = subscribe_clients_.find(name);
  if (it == subscribe_clients_.end())
    throw std::runtime_error(name + " redis subscribe-client not found");
  it->second->WaitConnectedOnce(wait_connected);
  return std::static_pointer_cast<storages::redis::SubscribeClient>(it->second);
}

void Redis::Connect(const ComponentConfig& config,
                    const ComponentContext& component_context,
                    const testsuite::RedisControl& testsuite_redis_control) {
  auto& secdist_component = component_context.FindComponent<Secdist>();

  const auto redis_pools = config["thread_pools"].As<RedisPools>();

  thread_pools_ = std::make_shared<redis::ThreadPools>(
      redis_pools.sentinel_thread_pool_size,
      redis_pools.redis_thread_pool_size);

  const auto redis_groups = config["groups"].As<std::vector<RedisGroup>>();
  for (const RedisGroup& redis_group : redis_groups) {
    auto settings = GetSecdistSettings(secdist_component, redis_group);

    auto command_control = redis::kDefaultCommandControl;
    command_control.allow_reads_from_master =
        redis_group.allow_reads_from_master;

    auto sentinel = redis::Sentinel::CreateSentinel(
        thread_pools_, settings, redis_group.config_name, redis_group.db,
        redis::KeyShardFactory{redis_group.sharding_strategy}, command_control,
        testsuite_redis_control);
    if (sentinel) {
      sentinels_.emplace(redis_group.db, sentinel);
      const auto& client =
          std::make_shared<storages::redis::ClientImpl>(sentinel);
      clients_.emplace(redis_group.db, client);
    } else {
      LOG_WARNING() << "skip redis client for " << redis_group.db;
    }
  }

  auto cfg = config_.GetSnapshot();
  const auto& redis_config = cfg.Get<storages::redis::Config>();
  for (auto& sentinel_it : sentinels_) {
    sentinel_it.second->WaitConnectedOnce(redis_config.redis_wait_connected);
  }

  auto subscribe_redis_groups =
      config["subscribe_groups"].As<std::vector<SubscribeRedisGroup>>();

  for (const auto& redis_group : subscribe_redis_groups) {
    auto settings = GetSecdistSettings(secdist_component, redis_group);

    bool is_cluster_mode = USERVER_NAMESPACE::redis::IsClusterStrategy(
        redis_group.sharding_strategy);

    auto sentinel = redis::SubscribeSentinel::Create(
        thread_pools_, settings, redis_group.config_name, redis_group.db,
        is_cluster_mode, testsuite_redis_control);
    if (sentinel)
      subscribe_clients_.emplace(
          redis_group.db,
          std::make_shared<storages::redis::SubscribeClientImpl>(
              std::move(sentinel)));
    else
      LOG_WARNING() << "skip subscribe-redis client for " << redis_group.db;
  }

  auto redis_wait_connected_subscribe = redis_config.redis_wait_connected.Get();
  if (redis_wait_connected_subscribe.mode !=
      USERVER_NAMESPACE::redis::WaitConnectedMode::kNoWait)
    redis_wait_connected_subscribe.mode =
        USERVER_NAMESPACE::redis::WaitConnectedMode::kMasterOrSlave;
  for (auto& subscribe_client_it : subscribe_clients_) {
    subscribe_client_it.second->WaitConnectedOnce(
        redis_wait_connected_subscribe);
  }
}

Redis::~Redis() {
  try {
    statistics_holder_.Unregister();
    subscribe_statistics_holder_.Unregister();
    config_subscription_.Unsubscribe();
  } catch (std::exception const& e) {
    LOG_ERROR() << "exception while destroying Redis component: " << e;
  } catch (...) {
    LOG_ERROR() << "non-standard exception while destroying Redis component";
  }
}

formats::json::Value Redis::ExtendStatisticsRedis(
    const utils::statistics::StatisticsRequest& /*request*/) {
  formats::json::ValueBuilder json(formats::json::Type::kObject);
  auto settings = metrics_settings_.Read();
  for (const auto& client : sentinels_) {
    const auto& name = client.first;
    const auto& redis = client.second;
    json[name] = RedisStatisticsToJson(redis, *settings);
  }
  utils::statistics::SolomonChildrenAreLabelValues(json, "redis_database");
  return json.ExtractValue();
}

formats::json::Value Redis::ExtendStatisticsRedisPubsub(
    const utils::statistics::StatisticsRequest& /*request*/) {
  formats::json::ValueBuilder subscribe_json(formats::json::Type::kObject);
  for (const auto& client : subscribe_clients_) {
    const auto& name = client.first;
    const auto& redis = client.second->GetNative();
    subscribe_json[name] = RedisSubscribeStatisticsToJson(redis);
  }
  utils::statistics::SolomonChildrenAreLabelValues(subscribe_json,
                                                   "redis_database");
  return subscribe_json.ExtractValue();
}

void Redis::OnConfigUpdate(const dynamic_config::Snapshot& cfg) {
  LOG_INFO() << "update default command control";
  const auto& redis_config = cfg.Get<storages::redis::Config>();

  auto cc = std::make_shared<redis::CommandControl>(
      redis_config.default_command_control);
  for (auto& it : sentinels_) {
    const auto& name = it.first;
    auto& client = it.second;
    client->SetConfigDefaultCommandControl(cc);
    client->SetCommandsBufferingSettings(
        redis_config.commands_buffering_settings);
    client->SetReplicationMonitoringSettings(
        redis_config.replication_monitoring_settings.GetOptional(name).value_or(
            redis::ReplicationMonitoringSettings{}));
  }

  auto subscriber_cc = std::make_shared<redis::CommandControl>(
      redis_config.subscriber_default_command_control);
  std::chrono::seconds subscriptions_rebalance_min_interval{
      redis_config.subscriptions_rebalance_min_interval_seconds.Get()};
  for (auto& it : subscribe_clients_) {
    auto& subscribe_client = it.second->GetNative();
    subscribe_client.SetConfigDefaultCommandControl(subscriber_cc);
    subscribe_client.SetRebalanceMinInterval(
        subscriptions_rebalance_min_interval);
  }

  auto metrics_settings = metrics_settings_.Read();
  if (*metrics_settings != redis_config.metrics_settings) {
    metrics_settings_.Assign(redis_config.metrics_settings);
  }
}

yaml_config::Schema Redis::GetStaticConfigSchema() {
  return yaml_config::MergeSchemas<LoggableComponentBase>(R"(
type: object
description: Redis client component
additionalProperties: false
properties:
    thread_pools:
        type: object
        description: thread pools options
        additionalProperties: false
        properties:
            redis_thread_pool_size:
                type: integer
                description: thread count to serve Redis requests
            sentinel_thread_pool_size:
                type: integer
                description: thread count to serve sentinel requests
    groups:
        type: array
        description: array of redis clusters to work with excluding subscribers
        items:
            type: object
            description: redis cluster to work with excluding subscribers
            additionalProperties: false
            properties:
                config_name:
                    type: string
                    description: key name in secdist with options for this cluster
                db:
                    type: string
                    description: name to refer to the cluster in components::Redis::GetClient()
                sharding_strategy:
                    type: string
                    description: one of RedisCluster, KeyShardCrc32, KeyShardTaximeterCrc32 or KeyShardGpsStorageDriver
                    defaultDescription: "KeyShardTaximeterCrc32"
                    enum:
                      - RedisCluster
                      - KeyShardCrc32
                      - KeyShardTaximeterCrc32
                      - KeyShardGpsStorageDriver
                allow_reads_from_master:
                    type: boolean
                    description: allows read requests from master instance
                    defaultDescription: false
    subscribe_groups:
        type: array
        description: array of redis clusters to work with in subscribe mode
        items:
            type: object
            description: redis cluster to work with in subscribe mode
            additionalProperties: false
            properties:
                config_name:
                    type: string
                    description: key name in secdist with options for this cluster
                db:
                    type: string
                    description: name to refer to the cluster in components::Redis::GetSubscribeClient()
                sharding_strategy:
                    type: string
                    description: either RedisCluster or KeyShardTaximeterCrc32
                    defaultDescription: "KeyShardTaximeterCrc32"
                    enum:
                      - RedisCluster
                      - KeyShardTaximeterCrc32
)");
}

}  // namespace components

USERVER_NAMESPACE_END
