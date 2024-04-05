from . import default_config
from .helpers import canonize_uuid

from yt.wrapper.common import MB, GB
from yt.common import update, update_inplace

from yt.yson import to_yson_type

try:
    from yt.packages.six.moves import xrange
except ImportError:
    from six.moves import xrange

import random
import os
from copy import deepcopy


DEFAULT_TRANSACTION_PING_PERIOD = 500


def _get_timestamp_provider_addresses(yt_config,
                                      master_connection_configs,
                                      clock_connection_config,
                                      timestamp_provider_addresses):
    if yt_config.timestamp_provider_count > 0 and timestamp_provider_addresses is not None:
        return timestamp_provider_addresses
    elif yt_config.clock_count > 0:
        return clock_connection_config["addresses"]
    else:
        return master_connection_configs[master_connection_configs["primary_cell_tag"]]["addresses"]


def _get_timestamp_provider_peer_configs(yt_config,
                                         master_connection_configs,
                                         clock_connection_config,
                                         timestamp_provider_addresses):
    addresses = _get_timestamp_provider_addresses(yt_config, master_connection_configs, clock_connection_config, timestamp_provider_addresses)
    return [{"address" : address} for address in addresses]


def build_configs(yt_config, ports_generator, dirs, logs_dir, binary_to_version):
    discovery_configs = _build_discovery_server_configs(
        yt_config,
        ports_generator,
        logs_dir)

    clock_configs, clock_connection_config = _build_clock_configs(
        yt_config,
        dirs["clock"],
        dirs["clock_tmpfs"],
        ports_generator,
        logs_dir)

    # Note that queue agent config depends on master rpc ports and master config depends on queue agent rpc ports.
    # That's why we prepare queue agent rpc ports separately before both configs.
    queue_agent_rpc_ports = _allocate_queue_agent_rpc_ports(yt_config, ports_generator)
    # Cypress proxies are involved in a similar cycle.
    cypress_proxy_rpc_ports = _allocate_cypress_proxy_rpc_ports(yt_config, ports_generator)

    master_configs, master_connection_configs = _build_master_configs(
        yt_config,
        dirs["master"],
        dirs["master_tmpfs"],
        clock_connection_config,
        discovery_configs,
        queue_agent_rpc_ports,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir)

    timestamp_provider_configs, timestamp_provider_addresses = _build_timestamp_provider_configs(
        yt_config,
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        ports_generator,
        logs_dir)

    master_cache_configs, master_cache_addresses = _build_master_cache_configs(
        yt_config,
        master_connection_configs,
        clock_connection_config,
        discovery_configs,
        timestamp_provider_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir)

    cell_balancer_configs, cell_balancer_addresses = _build_cell_balancer_configs(
        yt_config,
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
    )

    node_configs, node_addresses = _build_node_configs(
        dirs["node"],
        dirs["node_tmpfs"],
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
        yt_config)

    chaos_node_configs = _build_chaos_node_configs(
        dirs["chaos_node"],
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
        yt_config)

    queue_agent_configs = _build_queue_agent_configs(
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        queue_agent_rpc_ports,
        ports_generator,
        logs_dir,
        yt_config)

    scheduler_configs = _build_scheduler_configs(
        dirs["scheduler"],
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
        yt_config)

    controller_agent_configs = _build_controller_agent_configs(
        dirs["controller_agent"],
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
        yt_config)

    http_proxy_configs = _build_http_proxy_config(
        dirs["http_proxy"],
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
        yt_config=yt_config,
        version=binary_to_version["ytserver-http-proxy"])

    http_proxy_url = None
    if yt_config.http_proxy_count > 0:
        http_proxy_url = "{0}:{1}".format(yt_config.fqdn, http_proxy_configs[0]["port"])

    rpc_proxy_configs = _build_rpc_proxy_configs(
        logs_dir,
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        yt_config)

    rpc_client_config = None
    rpc_proxy_addresses = None
    if yt_config.rpc_proxy_count > 0:
        rpc_proxy_addresses = [
            "{0}:{1}".format(yt_config.fqdn, rpc_proxy_config["rpc_port"])
            for rpc_proxy_config in rpc_proxy_configs
        ]
        rpc_client_config = {
            "connection_type": "rpc",
            "addresses": rpc_proxy_addresses
        }

    driver_configs = _build_native_driver_configs(
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        yt_config=yt_config)

    rpc_driver_config = _build_rpc_driver_config(rpc_proxy_addresses, http_proxy_url)

    tablet_balancer_configs, tablet_balancer_addresses = _build_tablet_balancer_configs(
        yt_config,
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
    )

    cypress_proxy_configs = _build_cypress_proxy_configs(
        yt_config,
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
    )

    replicated_table_tracker_configs = _build_replicated_table_tracker_configs(
        yt_config,
        deepcopy(master_connection_configs),
        deepcopy(clock_connection_config),
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports,
        ports_generator,
        logs_dir,
    )

    cluster_configuration = {
        "master": master_configs,
        "clock": clock_configs,
        "discovery": discovery_configs,
        "queue_agent": queue_agent_configs,
        "timestamp_provider": timestamp_provider_configs,
        "cell_balancer": cell_balancer_configs,
        "driver": driver_configs,
        "rpc_driver": rpc_driver_config,
        "scheduler": scheduler_configs,
        "controller_agent": controller_agent_configs,
        "node": node_configs,
        "chaos_node": chaos_node_configs,
        "master_cache": master_cache_configs,
        "http_proxy": http_proxy_configs,
        "rpc_proxy": rpc_proxy_configs,
        "rpc_client": rpc_client_config,
        "tablet_balancer": tablet_balancer_configs,
        "cypress_proxy": cypress_proxy_configs,
        "replicated_table_tracker": replicated_table_tracker_configs,
        "cluster_connection": _build_cluster_connection_config(
            yt_config,
            master_connection_configs,
            clock_connection_config,
            discovery_configs,
            timestamp_provider_addresses,
            master_cache_addresses,
            cypress_proxy_rpc_ports,
            queue_agent_rpc_ports),
    }

    return cluster_configuration


def _build_master_configs(yt_config,
                          master_dirs,
                          master_tmpfs_dirs,
                          clock_connection_config,
                          discovery_configs,
                          queue_agent_rpc_ports,
                          cypress_proxy_rpc_ports,
                          ports_generator,
                          logs_dir):
    ports = []

    cell_tags = [str(yt_config.primary_cell_tag + index)
                 for index in xrange(yt_config.secondary_cell_count + 1)]
    random_part = random.randint(0, 2 ** 32 - 1)
    cell_ids = [canonize_uuid("%x-ffffffff-%x0259-ffffffff" % (random_part, int(tag)))
                for tag in cell_tags]

    nonvoting_master_count = yt_config.nonvoting_master_count

    connection_configs = {}
    for cell_index in xrange(yt_config.secondary_cell_count + 1):
        cell_ports = []
        cell_addresses = []
        peer_configs = []

        for i in xrange(yt_config.master_count):
            rpc_port, monitoring_port = next(ports_generator), next(ports_generator)
            address = "{0}:{1}".format(yt_config.fqdn, rpc_port)
            peer_config = {}
            peer_config["address"] = address
            if i >= yt_config.master_count - nonvoting_master_count:
                peer_config["voting"] = False
            peer_configs.append(peer_config)
            cell_addresses.append(address)
            cell_ports.append((rpc_port, monitoring_port))

        ports.append(cell_ports)

        connection_config = {
            "peers": peer_configs,
            # COMPAT(aleksandra-zh)
            "addresses": cell_addresses,
            "cell_id": cell_ids[cell_index]
        }
        connection_configs[cell_tags[cell_index]] = connection_config

    connection_configs["primary_cell_tag"] = cell_tags[0]
    connection_configs["secondary_cell_tags"] = cell_tags[1:]

    cluster_connection_config = \
        _build_cluster_connection_config(
            yt_config,
            connection_configs,
            clock_connection_config,
            discovery_configs,
            timestamp_provider_addresses=[],
            master_cache_addresses=[],
            cypress_proxy_rpc_ports=cypress_proxy_rpc_ports,
            queue_agent_rpc_ports=queue_agent_rpc_ports)

    configs = {}
    for cell_index in xrange(yt_config.secondary_cell_count + 1):
        cell_configs = []

        for master_index in xrange(yt_config.master_count):
            config = default_config.get_master_config()

            init_singletons(config, yt_config, master_index)

            init_jaeger_collector(config, "master", {
                "cell_role": "primary" if cell_index == 0 else "secondary",
                "master_index": str(master_index),
            })

            set_at(config, "hydra_manager", _get_hydra_manager_config(), merge=True)

            config["rpc_port"], config["monitoring_port"] = ports[cell_index][master_index]

            config["primary_master"] = connection_configs[cell_tags[0]]
            config["secondary_masters"] = [connection_configs[tag]
                                           for tag in connection_configs["secondary_cell_tags"]]

            config["enable_timestamp_manager"] = (yt_config.clock_count == 0)

            if yt_config.discovery_server_count > 0:
                discovery_server_config = {}
                discovery_server_config["addresses"] = discovery_configs[0]["discovery_server"]["server_addresses"]
                config["discovery_server"] = discovery_server_config

            # COMPAT(aleksandra-zh)
            set_at(config, "timestamp_provider/addresses",
                   _get_timestamp_provider_addresses(yt_config, connection_configs, clock_connection_config, None))
            set_at(config, "timestamp_provider/peers",
                   _get_timestamp_provider_peer_configs(yt_config, connection_configs, clock_connection_config, None))

            set_at(config, "snapshots/path",
                   os.path.join(master_dirs[cell_index][master_index], "snapshots"))

            if master_tmpfs_dirs is None:
                set_at(config, "changelogs/path",
                       os.path.join(master_dirs[cell_index][master_index], "changelogs"))
            else:
                set_at(config, "changelogs/path",
                       os.path.join(master_tmpfs_dirs[cell_index][master_index], "changelogs"))

            config["logging"] = _init_logging(logs_dir,
                                              "master-{0}-{1}".format(cell_index, master_index),
                                              yt_config,
                                              log_errors_to_stderr=True,
                                              has_structured_logs=True)

            config["cluster_connection"] = cluster_connection_config

            cell_configs.append(config)

        configs[cell_tags[cell_index]] = cell_configs

    configs["primary_cell_tag"] = cell_tags[0]
    configs["secondary_cell_tags"] = cell_tags[1:]

    return configs, connection_configs


def _allocate_queue_agent_rpc_ports(yt_config, ports_generator):
    rpc_ports = []

    for i in xrange(yt_config.queue_agent_count):
        rpc_port = next(ports_generator)
        rpc_ports.append(rpc_port)

    return rpc_ports


def _build_clock_configs(yt_config, clock_dirs, clock_tmpfs_dirs, ports_generator, logs_dir):
    cell_tag = 1000
    random_part = random.randint(0, 2 ** 32 - 1)
    cell_id = canonize_uuid("%x-ffffffff-%x0259-ffffffff" % (random_part, int(cell_tag)))

    ports = []
    cell_addresses = []
    peer_configs = []

    for i in xrange(yt_config.clock_count):
        rpc_port, monitoring_port = next(ports_generator), next(ports_generator)
        address = to_yson_type("{0}:{1}".format(yt_config.fqdn, rpc_port))
        cell_addresses.append(address)
        peer_config = {}
        peer_config["address"] = address
        peer_configs.append(peer_config)
        ports.append((rpc_port, monitoring_port))

    connection_config = {
        # COMPAT(aleksandra-zh)
        "addresses": cell_addresses,
        "peers": peer_configs,
        "cell_id": cell_id
    }

    configs = {}
    instance_configs = []

    for clock_index in xrange(yt_config.clock_count):
        config = default_config.get_clock_config()

        init_singletons(config, yt_config, clock_index)

        init_jaeger_collector(config, "clock", {
            "clock_index": str(clock_index)
        })

        set_at(config, "hydra_manager", _get_hydra_manager_config(), merge=True)

        config["rpc_port"], config["monitoring_port"] = ports[clock_index]

        config["clock_cell"] = connection_config

        # COMPAT(aleksandra-zh)
        set_at(config, "timestamp_provider/addresses", connection_config["addresses"])
        set_at(config, "snapshots/path",
               os.path.join(clock_dirs[clock_index], "snapshots"))

        if clock_tmpfs_dirs is None:
            set_at(config, "changelogs/path",
                   os.path.join(clock_dirs[clock_index], "changelogs"))
        else:
            set_at(config, "changelogs/path",
                   os.path.join(clock_tmpfs_dirs[clock_index], "changelogs"))

        config["logging"] = _init_logging(logs_dir,
                                          "clock-{0}".format(clock_index),
                                          yt_config,
                                          log_errors_to_stderr=True)

        instance_configs.append(config)

    configs[cell_tag] = instance_configs
    configs["cell_tag"] = cell_tag

    return configs, connection_config


def _build_discovery_server_configs(yt_config, ports_generator, logs_dir):
    server_addresses = []
    ports = []

    for i in xrange(yt_config.discovery_server_count):
        rpc_port, monitoring_port = next(ports_generator), next(ports_generator)
        address = to_yson_type("{0}:{1}".format(yt_config.fqdn, rpc_port))
        server_addresses.append(address)
        ports.append((rpc_port, monitoring_port))

    configs = []
    for i in xrange(yt_config.discovery_server_count):
        discovery_server_config = {}
        discovery_server_config["server_addresses"] = server_addresses

        config = {}
        config["discovery_server"] = discovery_server_config
        config["logging"] = _init_logging(logs_dir,
                                          "discovery-" + str(i),
                                          yt_config,
                                          log_errors_to_stderr=True)

        config["rpc_port"], config["monitoring_port"] = ports[i]
        configs.append(config)

    return configs


def _build_queue_agent_configs(master_connection_configs,
                               clock_connection_config,
                               discovery_configs,
                               timestamp_provider_addresses,
                               master_cache_addresses,
                               cypress_proxy_rpc_ports,
                               rpc_ports,
                               ports_generator,
                               logs_dir,
                               yt_config):
    configs = []
    for i in xrange(yt_config.queue_agent_count):
        config = default_config.get_queue_agent_config()

        init_singletons(config, yt_config, i)

        init_jaeger_collector(config, "queue_agent", {
            "queue_agent_index": str(i)
        })

        config["logging"] = _init_logging(logs_dir,
                                          "queue-agent-" + str(i),
                                          yt_config)
        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports)

        config["rpc_port"] = rpc_ports[i]
        config["monitoring_port"] = next(ports_generator)

        set_at(config, "queue_agent/stage", "production")

        configs.append(config)

    return configs


def _build_timestamp_provider_configs(yt_config,
                                      master_connection_configs,
                                      clock_connection_config,
                                      ports_generator,
                                      logs_dir):
    configs = []
    addresses = []

    for index in xrange(yt_config.timestamp_provider_count):
        config = default_config.get_timestamp_provider_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "timestamp_provider", {
            "timestamp_provider_index": str(index)
        })

        # COMPAT(aleksandra-zh)
        set_at(config, "timestamp_provider/addresses",
               _get_timestamp_provider_addresses(yt_config, master_connection_configs, clock_connection_config, None))
        set_at(config, "timestamp_provider/peers",
               _get_timestamp_provider_peer_configs(yt_config, master_connection_configs, clock_connection_config, None))

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "timestamp-provider-" + str(index),
                                          yt_config,
                                          log_errors_to_stderr=True)

        configs.append(config)
        addresses.append("{}:{}".format(yt_config.fqdn, config["rpc_port"]))

    return configs, addresses


def _build_cell_balancer_configs(yt_config,
                                 master_connection_configs,
                                 clock_connection_config,
                                 discovery_configs,
                                 timestamp_provider_addresses,
                                 master_cache_addresses,
                                 cypress_proxy_rpc_ports,
                                 ports_generator,
                                 logs_dir):
    configs = []
    addresses = []

    for index in xrange(yt_config.cell_balancer_count):
        config = default_config.get_cell_balancer_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "cell_balancer", {
            "cell_balancer_index": str(index)
        })

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports,
                config_template=config["cluster_connection"])

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "cell-balancer-" + str(index),
                                          yt_config,
                                          log_errors_to_stderr=True)

        configs.append(config)
        addresses.append("{}:{}".format(yt_config.fqdn, config["rpc_port"]))

    return configs, addresses


def _build_master_cache_configs(yt_config,
                                master_connection_configs,
                                clock_connection_config,
                                discovery_configs,
                                timestamp_provider_addresses,
                                cypress_proxy_rpc_ports,
                                ports_generator,
                                logs_dir):
    configs = []
    addresses = []

    for index in xrange(yt_config.master_cache_count):
        config = default_config.get_master_cache_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "master_cache", {"master_cache_index": str(index)})

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                [],  # master cache addresses
                cypress_proxy_rpc_ports,
                config_template=config["cluster_connection"])

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "master-cache-" + str(index),
                                          yt_config,
                                          has_structured_logs=True)

        configs.append(config)
        addresses.append("{}:{}".format(yt_config.fqdn, config["rpc_port"]))

    return configs, addresses


def _build_scheduler_configs(scheduler_dirs,
                             master_connection_configs,
                             clock_connection_config,
                             discovery_configs,
                             timestamp_provider_addresses,
                             master_cache_addresses,
                             cypress_proxy_rpc_ports,
                             ports_generator,
                             logs_dir,
                             yt_config):
    configs = []

    for index in xrange(yt_config.scheduler_count):
        config = default_config.get_scheduler_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "scheduler", {"scheduler_index": str(index)})

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports,
                config_template=config["cluster_connection"])

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "scheduler-" + str(index),
                                          yt_config,
                                          has_structured_logs=True)

        configs.append(config)

    return configs


def _build_controller_agent_configs(controller_agent_dirs,
                                    master_connection_configs,
                                    clock_connection_config,
                                    discovery_configs,
                                    timestamp_provider_addresses,
                                    master_cache_addresses,
                                    cypress_proxy_rpc_ports,
                                    ports_generator,
                                    logs_dir,
                                    yt_config):
    configs = []

    for index in xrange(yt_config.controller_agent_count):
        config = default_config.get_controller_agent_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "controller_agent", {"controller_agent_index": str(index)})

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports,
                config_template=config["cluster_connection"])

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "controller-agent-" + str(index),
                                          yt_config,
                                          has_structured_logs=True)

        configs.append(config)

    return configs


def _build_node_configs(node_dirs,
                        node_tmpfs_dirs,
                        master_connection_configs,
                        clock_connection_config,
                        discovery_configs,
                        timestamp_provider_addresses,
                        master_cache_addresses,
                        cypress_proxy_rpc_ports,
                        ports_generator,
                        logs_dir,
                        yt_config):
    configs = []
    addresses = []

    for index in xrange(yt_config.node_count):
        config = default_config.get_node_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "node", {"node_index": str(index)})

        config["addresses"] = [
            ("interconnect", yt_config.fqdn),
            ("default", yt_config.fqdn)
        ]

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["skynet_http_port"] = next(ports_generator)

        addresses.append("{0}:{1}".format(yt_config.fqdn, config["rpc_port"]))

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports,
                config_template=config["cluster_connection"])

        cache_location_config = {
            "quota": 256 * MB,
            "io_config": {
                "enable_sync": False,
            },
        }

        if node_tmpfs_dirs is not None and yt_config.allow_chunk_storage_in_tmpfs:
            cache_location_config["path"] = os.path.join(node_tmpfs_dirs[index], "chunk_cache")
        else:
            cache_location_config["path"] = os.path.join(node_dirs[index], "chunk_cache")

        if os.path.exists("{}/disabled".format(cache_location_config["path"])):
            os.remove("{}/disabled".format(cache_location_config["path"]))

        set_at(config, "data_node/cache_locations", [cache_location_config])

        if yt_config.jobs_environment_type is not None:
            set_at(
                config,
                "exec_node/slot_manager/job_environment",
                _get_node_job_environment_config(yt_config, index, logs_dir)
            )

        if yt_config.use_slot_user_id:
            start_uid = 10000 + config["rpc_port"]
            set_at(config, "exec_node/slot_manager/job_environment/start_uid", start_uid)
        else:
            set_at(config, "exec_node/do_not_set_user_id", True)

        set_at(config, "exec_node/slot_manager/locations", [
            {"path": os.path.join(node_dirs[index], "slots"), "disk_usage_watermark": 0}
        ])
        set_at(config, "exec_node/root_fs_binds", [
            {"external_path": node_dirs[index], "internal_path": node_dirs[index]}
        ])

        changelog_config = {
            "preallocate_size": 2 ** 20,
        }

        store_location_configs = []

        for location_index in range(yt_config.store_location_count):
            store_location_config = {
                "low_watermark": 0,
                "high_watermark": 0,
                "disable_writes_watermark": 0,
                "io_config": {
                    "enable_sync": False,
                },
                "use_direct_io_for_reads" : yt_config.node_use_direct_io_for_reads,
                "multiplexed_changelog": changelog_config,
                "high_latency_split_changelog": changelog_config,
                "low_latency_split_changelog": changelog_config,
            }

            if yt_config.node_io_engine_type:
                store_location_config["io_engine_type"] = yt_config.node_io_engine_type

            if yt_config.node_chunk_store_quota is not None:
                store_location_config["quota"] = yt_config.node_chunk_store_quota

            if node_tmpfs_dirs is not None and yt_config.allow_chunk_storage_in_tmpfs:
                store_location_config["path"] = os.path.join(node_tmpfs_dirs[index], "chunk_store/{0}".format(location_index))
            else:
                store_location_config["path"] = os.path.join(node_dirs[index], "chunk_store/{0}".format(location_index))

            if os.path.exists("{}/disabled".format(store_location_config["path"])):
                os.remove("{}/disabled".format(store_location_config["path"]))

            store_location_configs.append(store_location_config)

        set_at(config, "data_node/store_locations", store_location_configs)
        set_at(config, "data_node/use_disable_send_blocks", True)

        layer_location_config = {
            "low_watermark": 1,
            "location_is_absolute": False,
        }

        if node_tmpfs_dirs is not None and yt_config.allow_chunk_storage_in_tmpfs:
            layer_location_config["path"] = os.path.join(node_tmpfs_dirs[index], "layers")
        else:
            layer_location_config["path"] = os.path.join(node_dirs[index], "layers")

        if os.path.exists("{}/disabled".format(layer_location_config["path"])):
            os.remove("{}/disabled".format(layer_location_config["path"]))

        set_at(config, "data_node/volume_manager/layer_locations", [layer_location_config])

        config["logging"] = _init_logging(logs_dir, "node-{0}".format(index), yt_config, has_structured_logs=True)

        log_name = "job_proxy-{0}-slot-%slot_index%".format(index)
        set_at(
            config,
            "exec_node/job_proxy_logging",
            _init_logging(logs_dir, log_name, yt_config)
        )
        set_at(
            config,
            "exec_node/job_proxy_stderr_path",
            os.path.join(logs_dir, "job_proxy-{0}-stderr-slot-%slot_index%".format(index)),
        )
        set_at(
            config,
            "exec_node/executor_stderr_path",
            os.path.join(logs_dir, "ytserver_exec-{0}-stderr-slot-%slot_index%".format(index))
        )

        set_at(config, "tablet_node/hydra_manager", _get_hydra_manager_config(), merge=True)
        set_at(config, "tablet_node/hydra_manager/restart_backoff_time", 100)
        set_at(config, "exec_node/job_controller/resource_limits", yt_config.jobs_resource_limits, merge=True)
        set_at(config, "resource_limits", _get_node_resource_limits_config(yt_config), merge=True)

        configs.append(config)

    if hasattr(ports_generator, "local_port_range"):
        USER_PORT_START = ports_generator.local_port_range[1]
        USER_PORT_END = min(USER_PORT_START + 10000, 2 ** 16)
        assert USER_PORT_START < USER_PORT_END
    else:
        # Legacy constants.
        USER_PORT_START = 20000
        USER_PORT_END = 30000

    node_count = len(configs)
    for index, config in enumerate(configs):
        port_start = USER_PORT_START + (index * (USER_PORT_END - USER_PORT_START)) // node_count
        port_end = USER_PORT_START + ((index + 1) * (USER_PORT_END - USER_PORT_START)) // node_count

        if yt_config.node_port_set_size is None:
            set_at(config, "exec_node/job_controller/start_port", port_start)
            set_at(config, "exec_node/job_controller/port_count", port_end - port_start)
        else:
            ports = [next(ports_generator) for _ in xrange(yt_config.node_port_set_size)]
            set_at(config, "exec_node/job_controller/port_set", ports)

    return configs, addresses


def _build_chaos_node_configs(chaos_node_dirs,
                              master_connection_configs,
                              clock_connection_config,
                              discovery_configs,
                              timestamp_provider_addresses,
                              master_cache_addresses,
                              cypress_proxy_rpc_ports,
                              ports_generator,
                              logs_dir,
                              yt_config):
    configs = []

    for index in xrange(yt_config.chaos_node_count):
        config = default_config.get_chaos_node_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "chaos_node", {"chaos_node_index": str(index)})

        config["addresses"] = [
            ("interconnect", yt_config.fqdn),
            ("default", yt_config.fqdn)
        ]
        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["skynet_http_port"] = next(ports_generator)

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports,
                config_template=config["cluster_connection"])

        cache_location_config = {
            "quota": 0,
            "io_config": {
                "enable_sync": False,
            },
            "path": os.path.join(chaos_node_dirs[index], "chunk_cache"),
        }
        set_at(config, "data_node/cache_locations", [cache_location_config])

        set_at(config, "cellar_node/cellar_manager/cellars/chaos/occupant/hydra_manager", _get_hydra_manager_config(), merge=True)
        set_at(config, "cellar_node/cellar_manager/cellars/chaos/occupant/hydra_manager/restart_backoff_time", 100)
        set_at(config, "cellar_node/cellar_manager/cellars/chaos/occupant/response_keeper", _get_response_keeper_config())

        config["logging"] = _init_logging(logs_dir, "chaos-node-{0}".format(index), yt_config)
        configs.append(config)

    return configs


def _build_http_proxy_config(proxy_dir,
                             master_connection_configs,
                             clock_connection_config,
                             discovery_configs,
                             timestamp_provider_addresses,
                             master_cache_addresses,
                             cypress_proxy_rpc_ports,
                             ports_generator,
                             logs_dir,
                             yt_config,
                             version):
    driver_config = default_config.get_driver_config()

    cluster_connection = _build_cluster_connection_config(
        yt_config,
        master_connection_configs,
        clock_connection_config,
        discovery_configs,
        timestamp_provider_addresses,
        master_cache_addresses,
        cypress_proxy_rpc_ports)

    # COMPAT(max42)
    # (22, 4) would suffice in the condition if only REX tests did not use yt binaries from package.
    # Therefore, this compat may be safely removed even when 23.1 branch is released.
    # By that moment package in Arcadia is, hopefully, fresh enough, and compat-tests in trunk
    # are done against 23.1.
    if version.abi <= (23, 1):
        update_inplace(driver_config, cluster_connection)

    proxy_configs = []

    for index in xrange(yt_config.http_proxy_count):
        config = default_config.get_proxy_config()
        config["port"] = \
            yt_config.http_proxy_ports[index] if yt_config.http_proxy_ports else next(ports_generator)
        if yt_config.enable_tvm_only_proxies:
            config["tvm_only_http_server"] = {"port": next(ports_generator)}
        config["monitoring_port"] = next(ports_generator)
        config["rpc_port"] = next(ports_generator)

        fqdn = "{0}:{1}".format(yt_config.fqdn, config["port"])
        set_at(config, "coordinator/public_fqdn", fqdn)
        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "http_proxy", {"http_proxy_index": str(index)})

        config["logging"] = _init_logging(logs_dir, "http-proxy-{}".format(index), yt_config,
                                          has_structured_logs=True)

        config["driver"] = deepcopy(driver_config)
        config["cluster_connection"] = deepcopy(cluster_connection)

        config["zookeeper_proxy"] = {
            "server": {
                "port": next(ports_generator),
            },
        }

        if yt_config.https_cert is not None:
            set_at(config, "https_server", {
                "port": yt_config.https_proxy_ports[index] if yt_config.https_proxy_ports else next(ports_generator),
                "credentials": {
                    "cert_chain": {
                        "file_name": os.path.join(proxy_dir[index], 'https.crt'),
                    },
                    "private_key": {
                        "file_name": os.path.join(proxy_dir[index], 'https.key'),
                    },
                },
            })

        proxy_configs.append(config)

    return proxy_configs


def _build_native_driver_configs(master_connection_configs,
                                 clock_connection_config,
                                 discovery_configs,
                                 timestamp_provider_addresses,
                                 master_cache_addresses,
                                 cypress_proxy_rpc_ports,
                                 yt_config):
    secondary_cell_tags = master_connection_configs["secondary_cell_tags"]
    primary_cell_tag = master_connection_configs["primary_cell_tag"]

    configs = {}
    for cell_index in xrange(yt_config.secondary_cell_count + 1):
        config = default_config.get_driver_config()

        if cell_index == 0:
            tag = primary_cell_tag
            update_inplace(config, _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports))
        else:
            tag = secondary_cell_tags[cell_index - 1]
            cell_connection_config = {
                "primary_master": master_connection_configs[secondary_cell_tags[cell_index - 1]],
                "master_cell_directory_synchronizer": {
                    "sync_period": None
                },
                "timestamp_provider": {
                    # COMPAT(aleksandra-zh)
                    "addresses": _get_timestamp_provider_addresses(
                        yt_config,
                        master_connection_configs,
                        clock_connection_config,
                        timestamp_provider_addresses),
                    "peers": _get_timestamp_provider_peer_configs(
                        yt_config,
                        master_connection_configs,
                        clock_connection_config,
                        timestamp_provider_addresses),
                },
                "transaction_manager": {
                    "default_ping_period": DEFAULT_TRANSACTION_PING_PERIOD
                }
            }

            if yt_config.mock_tvm_id is not None:
                cell_connection_config["tvm_id"] = yt_config.mock_tvm_id

            discovery_server_addresses = master_connection_configs[primary_cell_tag]["addresses"]
            if yt_config.discovery_server_count > 0:
                discovery_server_addresses = discovery_configs[0]["discovery_server"]["server_addresses"]

            discovery_connection_config = {}
            discovery_connection_config["addresses"] = discovery_server_addresses
            cell_connection_config["discovery_connection"] = discovery_connection_config

            update_inplace(cell_connection_config["primary_master"], _get_retrying_channel_config())
            update_inplace(cell_connection_config["primary_master"], _get_rpc_config())

            update_inplace(config, cell_connection_config)

        if yt_config.mock_tvm_id is not None:
            config["tvm_service"] = {
                "enable_mock": True,
                "client_self_id": yt_config.mock_tvm_id,
                "client_enable_service_ticket_fetching": True,
                "client_enable_service_ticket_checking": True,
                "client_dst_map": {
                    "self": yt_config.mock_tvm_id,
                },
                "client_self_secret": "TestSecret-" + str(yt_config.mock_tvm_id),
            }

        configs[tag] = config

    if yt_config.clock_count > 0:
        config = deepcopy(configs[primary_cell_tag])
        update_inplace(config["timestamp_provider"], clock_connection_config)
        configs[primary_cell_tag] = config

    return configs


def _build_rpc_driver_config(rpc_proxy_addresses, http_proxy_url):
    config = default_config.get_driver_config()

    config["connection_type"] = "rpc"

    config["dynamic_channel_pool"] = {
        "soft_backoff_time": 100,
        "hard_backoff_time": 100
    }

    if http_proxy_url is not None:
        config["cluster_url"] = http_proxy_url
    else:
        config["proxy_addresses"] = rpc_proxy_addresses

    return config


def _build_rpc_proxy_configs(logs_dir,
                             master_connection_configs,
                             clock_connection_config,
                             discovery_configs,
                             timestamp_provider_addresses,
                             master_cache_addresses,
                             cypress_proxy_rpc_ports,
                             ports_generator,
                             yt_config):
    configs = []

    for rpc_proxy_index in xrange(yt_config.rpc_proxy_count):
        grpc_server_config = {
            "addresses": [
                {
                    "address": "{}:{}".format(yt_config.fqdn, next(ports_generator))
                }
            ]
        }

        config = {
            "cluster_connection": master_connection_configs,
            "discovery_service": {
                "liveness_update_period": 500,
                "proxy_update_period": 500
            },
            "grpc_server": grpc_server_config,
            "monitoring_port": next(ports_generator),
            "yt_alloc_dump_period": 15000,
            "ref_counted_tracker_dump_period": 15000,
            "enable_authentication": False,
            "api_service": {
                "security_manager": {
                    "user_cache": {
                        "expire_after_successful_update_time": 0,
                        "refresh_time": 0,
                        "expire_after_failed_update_time": 0,
                        "expire_after_access_time": 0,
                    }
                }
            },
            "dynamic_config_manager": {
                "update_period": 100,
            },
            "rpc_server": {
                "tracing_mode": "force",
            }
        }
        init_singletons(config, yt_config, rpc_proxy_index)

        init_jaeger_collector(config, "rpc_proxy", {"rpc_proxy_index": str(rpc_proxy_index)})

        config["cluster_connection"] = _build_cluster_connection_config(
            yt_config,
            master_connection_configs,
            clock_connection_config,
            discovery_configs,
            timestamp_provider_addresses,
            master_cache_addresses,
            cypress_proxy_rpc_ports)
        config["logging"] = _init_logging(logs_dir, "rpc-proxy-{}".format(rpc_proxy_index), yt_config)

        config["rpc_port"] = \
            yt_config.rpc_proxy_ports[rpc_proxy_index] if yt_config.rpc_proxy_ports else next(ports_generator)
        if yt_config.enable_tvm_only_proxies:
            config["tvm_only_rpc_port"] = next(ports_generator)

        configs.append(config)

    return configs


def _build_cluster_connection_config(yt_config,
                                     master_connection_configs,
                                     clock_connection_config,
                                     discovery_configs,
                                     timestamp_provider_addresses,
                                     master_cache_addresses,
                                     cypress_proxy_rpc_ports,
                                     queue_agent_rpc_ports=None,
                                     config_template=None):
    queue_agent_rpc_ports = queue_agent_rpc_ports or []
    primary_cell_tag = master_connection_configs["primary_cell_tag"]
    secondary_cell_tags = master_connection_configs["secondary_cell_tags"]

    cluster_connection = {
        "cluster_name": yt_config.cluster_name,
        "cell_directory": _get_balancing_channel_config(),
        "primary_master": master_connection_configs[primary_cell_tag],
        "transaction_manager": {
            "default_ping_period": DEFAULT_TRANSACTION_PING_PERIOD,
        },
        "timestamp_provider": {
            # COMPAT(aleksandra-zh)
            "addresses": _get_timestamp_provider_addresses(yt_config, master_connection_configs,
                                                           clock_connection_config, timestamp_provider_addresses),
            "peers": _get_timestamp_provider_peer_configs(yt_config, master_connection_configs,
                                                          clock_connection_config, timestamp_provider_addresses),
            "update_period": 500,
            "soft_backoff_time": 100,
            "hard_backoff_time": 100,
        },
        "cell_directory_synchronizer": {
            "sync_period": 500,
            "sync_period_splay": 100,
        },
        "chaos_cell_directory_synchronizer": {
            "sync_period": 500,
            "sync_period_splay": 100,
            "sync_all_chaos_cells": True,
        },
        "cluster_directory_synchronizer": {
            "sync_period": 500,
            "expire_after_successful_update_time": 500,
            "expire_after_failed_update_time": 500,
        },
        "table_mount_cache": {
            "expire_after_successful_update_time": 0,
            "expire_after_failed_update_time": 0,
            "expire_after_access_time": 0,
            "refresh_time": 0,
        },
        "sync_replica_cache": {
            "expire_after_successful_update_time": 0,
            "expire_after_failed_update_time": 0,
            "expire_after_access_time": 0,
            "refresh_time": 0,
        },
        "queue_agent": {
            "stages": {
                "production": {"addresses": ["{}:{}".format(yt_config.fqdn, port) for port in queue_agent_rpc_ports]},
            },
            "queue_consumer_registration_manager": {
                "bypass_caching": True,
                "cache_refresh_period": 3000,
                "configuration_refresh_period": 500,
                "resolve_symlinks": True,
                "resolve_replicas": True,
            },
        },
        "permission_cache": {
            "expire_after_successful_update_time": 0,
            "expire_after_failed_update_time": 0,
            "expire_after_access_time": 0,
            "refresh_time": 0,
        },
        "master_cell_directory_synchronizer": {
            "sync_period": 500,
            "expire_after_successful_update_time": 500,
            "expire_after_failed_update_time": 500,
        },
        "job_node_descriptor_cache": {
            "expire_after_successful_update_time": 0,
            "expire_after_failed_update_time": 0,
            "expire_after_access_time": 0,
            "refresh_time": 0,
        },
        "scheduler": {
            "retry_backoff_time": 100,
            "use_scheduler_job_prober_service": False,
        },
        "node_directory_synchronizer": {
            "sync_period": 500,
            "expire_after_successful_update_time": 500,
            "expire_after_failed_update_time": 500,
        },
        "upload_transaction_timeout": 5000,
        # TODO(gritukan): Turn on after 22.2 compat tests will be removed.
        "use_followers_for_write_targets_allocation": False,
    }

    if len(cypress_proxy_rpc_ports) > 0:
        cypress_proxy_addresses = ["{}:{}".format(yt_config.fqdn, rpc_port) for rpc_port in cypress_proxy_rpc_ports]
        cluster_connection["cypress_proxy"] = {}
        cluster_connection["cypress_proxy"]["addresses"] = cypress_proxy_addresses

    if yt_config.mock_tvm_id is not None:
        cluster_connection["tvm_id"] = yt_config.mock_tvm_id

    update_inplace(cluster_connection["primary_master"], _get_retrying_channel_config())
    update_inplace(cluster_connection["primary_master"], _get_balancing_channel_config())
    update_inplace(cluster_connection["primary_master"], _get_rpc_config())

    cluster_connection["secondary_masters"] = []
    for tag in secondary_cell_tags:
        config = master_connection_configs[tag]
        update_inplace(config, _get_retrying_channel_config())
        update_inplace(config, _get_balancing_channel_config())
        update_inplace(config, _get_rpc_config())
        cluster_connection["secondary_masters"].append(config)

    if config_template is not None:
        cluster_connection = update_inplace(config_template, cluster_connection)

    discovery_server_addresses = cluster_connection["primary_master"]["addresses"]
    if yt_config.discovery_server_count > 0:
        discovery_server_addresses = discovery_configs[0]["discovery_server"]["server_addresses"]

    discovery_connection_config = {}
    discovery_connection_config["addresses"] = discovery_server_addresses
    cluster_connection["discovery_connection"] = discovery_connection_config

    if yt_config.enable_master_cache:
        cluster_connection["master_cache"] = {
            "enable_master_cache_discovery": len(master_cache_addresses) == 0,
            "master_cache_discovery_period": 100,
            "soft_backoff_time": 100,
            "hard_backoff_time": 100,
            "rpc_timeout": 25000,
            # Decrease the number of retry attempts to overcome the following issue:
            # when a master cache node becomes banned it takes this node a significant amount of time
            # to re-register again (once it becomes unbanned) since the node must wait
            # for metadata synchronizer iterations to fail before falling back to
            # direct master communication.
            "retry_attempts": 3,
            "addresses": master_cache_addresses,
            "cell_id": master_connection_configs[primary_cell_tag]["cell_id"],
        }

    if yt_config.clock_count > 0:
        cluster_connection["clock_servers"] = clock_connection_config

    if yt_config.chaos_node_count > 0:
        cluster_connection["replication_card_cache"] = {
            "expire_after_successful_update_time": 100,
            "expire_after_failed_update_time": 100,
            "expire_after_access_time": 100,
            "refresh_time": 50,
            "soft_backoff_time": 100,
            "hard_backoff_time": 100,
            "addresses": master_cache_addresses,
        }
        cluster_connection["chaos_cell_channel"] = {
            "retry_backoff_time": 500,
            "retry_attempts": 10,
            "rpc_acknowledge_timeout": 100,
        }
        cluster_connection["replication_card_residency_cache"] = {
            "expire_after_successful_update_time": 100,
            "expire_after_failed_update_time": 100,
            "expire_after_access_time": 100,
            "refresh_time": 50,
        }

    if not yt_config.enable_permission_cache:
        cluster_connection["permission_cache"] = {
            "expire_after_successful_update_time": 0,
            "expire_after_failed_update_time": 0,
            "expire_after_access_time": 0,
            "refresh_time": 0
        }

    if yt_config.ca_cert is not None:
        set_at(cluster_connection, "bus_client", {
            "ca": {
                "file_name": yt_config.ca_cert,
            },
            "encryption_mode": "required",
            "verification_mode": "full",
            "peer_alternative_host_name": yt_config.cluster_name,
        })

    if yt_config.delta_global_cluster_connection_config:
        cluster_connection = update(cluster_connection, yt_config.delta_global_cluster_connection_config)

    return cluster_connection


def _build_tablet_balancer_configs(yt_config,
                                   master_connection_configs,
                                   clock_connection_config,
                                   discovery_configs,
                                   timestamp_provider_addresses,
                                   master_cache_addresses,
                                   cypress_proxy_rpc_ports,
                                   ports_generator,
                                   logs_dir):
    configs = []
    addresses = []

    for index in xrange(yt_config.tablet_balancer_count):
        config = default_config.get_tablet_balancer_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "tablet_balancer", {"tablet_balancer_index": str(index)})

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports)

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "tablet-balancer-" + str(index),
                                          yt_config,
                                          has_structured_logs=True)

        configs.append(config)
        addresses.append("{}:{}".format(yt_config.fqdn, config["rpc_port"]))

    return configs, addresses


def _build_replicated_table_tracker_configs(yt_config,
                                            master_connection_configs,
                                            clock_connection_config,
                                            discovery_configs,
                                            timestamp_provider_addresses,
                                            master_cache_addresses,
                                            cypress_proxy_rpc_ports,
                                            ports_generator,
                                            logs_dir):
    configs = []

    for index in xrange(yt_config.replicated_table_tracker_count):
        config = default_config.get_replicated_table_tracker_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "replicated_table_tracker", {"replicated_table_tracker_index": str(index)})

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports)

        config["rpc_port"] = next(ports_generator)
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "replicated-table-tracker-" + str(index),
                                          yt_config,
                                          has_structured_logs=True)

        configs.append(config)

    return configs


def _allocate_cypress_proxy_rpc_ports(yt_config, ports_generator):
    rpc_ports = []

    for i in xrange(yt_config.cypress_proxy_count):
        rpc_port = next(ports_generator)
        rpc_ports.append(rpc_port)

    return rpc_ports


def _build_cypress_proxy_configs(yt_config,
                                 master_connection_configs,
                                 clock_connection_config,
                                 discovery_configs,
                                 timestamp_provider_addresses,
                                 master_cache_addresses,
                                 cypress_proxy_rpc_ports,
                                 ports_generator,
                                 logs_dir):
    configs = []

    for index in xrange(yt_config.cypress_proxy_count):
        config = default_config.get_cypress_proxy_config()

        init_singletons(config, yt_config, index)

        init_jaeger_collector(config, "cypress_proxy", {"cypress_proxy_index": str(index)})

        config["cluster_connection"] = \
            _build_cluster_connection_config(
                yt_config,
                master_connection_configs,
                clock_connection_config,
                discovery_configs,
                timestamp_provider_addresses,
                master_cache_addresses,
                cypress_proxy_rpc_ports=[])

        config["rpc_port"] = cypress_proxy_rpc_ports[index]
        config["monitoring_port"] = next(ports_generator)
        config["logging"] = _init_logging(logs_dir,
                                          "cypress-proxy-" + str(index),
                                          yt_config,
                                          has_structured_logs=True)

        configs.append(config)

    return configs


def _init_logging(path, name, yt_config, log_errors_to_stderr=False, has_structured_logs=False):
    return init_logging(
        path,
        name,
        enable_debug_logging=yt_config.enable_debug_logging,
        enable_log_compression=yt_config.enable_log_compression,
        log_compression_method=yt_config.log_compression_method,
        enable_structured_logging=yt_config.enable_structured_logging and has_structured_logs,
        log_errors_to_stderr=log_errors_to_stderr)


def init_logging(path, name,
                 enable_debug_logging=False,
                 enable_log_compression=False,
                 log_compression_method="gzip",
                 enable_structured_logging=False,
                 log_errors_to_stderr=False,
                 abort_on_alert=None,
                 compression_thread_count=None):
    if enable_log_compression and log_compression_method == "zstd":
        suffix = ".zst"
        compression_options = {
            "enable_compression": True,
            "compression_method": "zstd",
            "compression_level": 1,
        }
    elif enable_log_compression:
        suffix = ".gz"
        compression_options = {
            "enable_compression": True,
        }
    else:
        suffix = ""
        compression_options = {}

    if abort_on_alert is None:
        abort_on_alert = True
    if compression_thread_count is None:
        compression_thread_count = 4

    config = {
        "abort_on_alert": abort_on_alert,
        "compression_thread_count": compression_thread_count,
        "rules": [
            {"min_level": "info", "writers": ["info"]},
        ],
        "writers": {
            "info": {
                "type": "file",
                "file_name": "{path}/{name}.log".format(path=path, name=name) + suffix,
            }
        }
    }
    config["writers"]["info"].update(compression_options)

    if log_errors_to_stderr:
        config["rules"].append(
            {"min_level": "error", "writers": ["stderr"]}
        )
        config["writers"]["stderr"] = {
            "type": "stderr",
        }

    if enable_debug_logging:
        config["rules"].append({
            "min_level": "debug",
            "exclude_categories": ["Bus", "Concurrency"],
            "writers": ["debug"],
        })
        config["writers"]["debug"] = {
            "type": "file",
            "file_name": "{path}/{name}.debug.log".format(path=path, name=name) + suffix,
        }
        config["writers"]["debug"].update(compression_options)

    if "YT_ENABLE_TRACE_LOGGING" in os.environ:
        config["rules"].append({
            "min_level": "trace",
            "exclude_categories": ["Bus"],
            "writers": ["trace"],
        })
        config["writers"]["trace"] = {
            "type": "file",
            "file_name": "{path}/{name}.trace.log".format(path=path, name=name) + suffix,
        }
        config["writers"]["trace"].update(compression_options)

    if enable_structured_logging:
        config["rules"].append({
            "min_level": "debug",
            "writers": ["json"],
            "message_format": "structured",
        })
        config["writers"]["json"] = {
            "type": "file",
            "file_name": "{path}/{name}.json.log".format(path=path, name=name),
            "accepted_message_format": "structured",
        }

    return config


def set_at(config, path, value, merge=False):
    """Sets value in config by path creating intermediate dict nodes."""
    parts = path.split("/")
    for index, part in enumerate(parts):
        if index != len(parts) - 1:
            config = config.setdefault(part, {})
        else:
            if merge:
                config[part] = update(config.get(part, {}), value)
            else:
                config[part] = value


def get_at(config, path, default_value=None):
    for part in path.split("/"):
        if not isinstance(config, dict):
            raise ValueError("Path should not contain non-dict intermediate values")
        if part not in config:
            return default_value
        config = config[part]
    return config


def init_singletons(config, yt_config, index):
    set_at(config, "stockpile", {
        "thread_count": 0,
    })
    set_at(config, "yp_service_discovery", {
        "enable": False,
    })
    set_at(config, "rpc_dispatcher", {
        "compression_pool_size": 1,
        "heavy_pool_size": 1,
        "alert_on_missing_request_info": True,
    })
    set_at(config, "chunk_client_dispatcher", {
        "chunk_reader_pool_size": 1,
    })
    set_at(config, "tcp_dispatcher", {
        "thread_pool_size": 2
    })
    set_at(config, "address_resolver/localhost_fqdn", yt_config.fqdn)
    set_at(config, "solomon_exporter/grid_step", 1000)
    set_at(config, "cypress_annotations/yt_env_index", index)
    set_at(config, "enable_ref_counted_tracker_profiling", yt_config.enable_resource_tracking)
    set_at(config, "enable_resource_tracker", yt_config.enable_resource_tracking)
    if yt_config.mock_tvm_id is not None:
        set_at(config, "native_authentication_manager", {
            "tvm_service": {
                "enable_mock": True,
                "client_self_id": yt_config.mock_tvm_id,
                "client_enable_service_ticket_fetching": True,
                "client_enable_service_ticket_checking": True,
                "client_self_secret": "TestSecret-" + str(yt_config.mock_tvm_id),
            },
            "enable_validation": True,
        })
    if yt_config.rpc_cert is not None:
        set_at(config, "bus_server", {
            "encryption_mode": "optional",
            "cert_chain": {
                "file_name": yt_config.rpc_cert,
            },
            "private_key": {
                "file_name": yt_config.rpc_cert_key,
            },
        })


def init_jaeger_collector(config, name, process_tags):
    if "JAEGER_COLLECTOR" in os.environ:
        set_at(config, "jaeger", {
            "service_name": name,
            "flush_period": 100,
            "collector_channel_config": {"address": os.environ["JAEGER_COLLECTOR"]},
            "enable_pid_tag": True,
            "process_tags": process_tags,
        })

        if name == "node":
            set_at(config, "exec_node/job_proxy_jaeger", {
                "service_name": "job_proxy",
                "flush_period": 100,
                "collector_channel_config": {"address": os.environ["JAEGER_COLLECTOR"]},
                "enable_pid_tag": True,
                "process_tags": process_tags,
            })


def _get_hydra_manager_config():
    return {
        "leader_lease_check_period": 100,
        "leader_lease_timeout": 20000,
        "disable_leader_lease_grace_delay": True,
        "invariants_check_probability": 0.005,
        "response_keeper": _get_response_keeper_config()
    }


def _get_response_keeper_config():
    return {
        "enable_warmup": False,
        "expiration_time": 25000,
        "warmup_time": 30000,
    }


def _get_balancing_channel_config():
    return {
        "soft_backoff_time": 100,
        "hard_backoff_time": 100,
        "enable_peer_polling": True,
        "peer_polling_period": 500,
        "peer_polling_period_splay": 100,
        "peer_polling_request_timeout": 100,
        "rediscover_period": 5000,
        "rediscover_splay": 500,
    }


def _get_retrying_channel_config():
    return {
        "retry_backoff_time": 100,
        "retry_attempts": 100
    }


def _get_rpc_config():
    return {
        "rpc_timeout": 25000
    }


def _get_node_resource_limits_config(yt_config):
    FOOTPRINT_MEMORY = 1 * GB
    CHUNK_META_CACHE_MEMORY = 1 * GB
    BLOB_SESSIONS_MEMORY = 2 * GB

    memory = 0
    memory += yt_config.jobs_resource_limits.get("memory", 0)
    if yt_config.node_memory_limit_addition is not None:
        memory += yt_config.node_memory_limit_addition

    memory += FOOTPRINT_MEMORY
    memory += CHUNK_META_CACHE_MEMORY
    memory += BLOB_SESSIONS_MEMORY

    return {"memory": memory}


def _get_node_job_environment_config(yt_config, index, logs_dir):
    return {
        "cri": {
            "type": "cri",
            "cri_executor": {
                "runtime_endpoint": yt_config.cri_endpoint,
                "image_endpoint": yt_config.cri_endpoint,
                "base_cgroup": "yt.slice/{}-node-{}.slice".format(yt_config.cluster_name, index),
                "namespace": "yt--{}-node-{}".format(yt_config.cluster_name, index),
                "verbose_logging": True,
            },
            "job_proxy_image": yt_config.default_docker_image,
            "use_job_proxy_from_image": False,
            "job_proxy_bind_mounts": [
                {
                    "internal_path":  logs_dir,
                    "external_path": logs_dir,
                    "read_only": False,
                },
            ],
        },
        "porto": {
            "type": "porto",
            "use_short_container_names": True,
        },
        "simple": {
            "type": "simple",
        },
    }[yt_config.jobs_environment_type]
