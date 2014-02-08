import yt.yson as yson
import simplejson as json

"""This module provides default ytserver configs"""

def get_master_config():
    return yson.loads(
"""
{
    masters = {
        addresses = [ ];
    };

    timestamp_provider = {
        addresses = [ ];
    };

    changelogs = {
        path = "";
    };
    
    snapshots = {
        path = "";
    };

    hydra = {
        follower_tracker = {
            ping_interval = 3000;
        };
        leader_committer = {
            changelog_rotation_period = 1000000;
            max_batch_delay = 0;
            rpc_timeout = 10000;
        };
    };

    transaction_manager = {
        default_transaction_timeout = 300000;
    };

    chunk_manager = {
        chunk_refresh_delay = 700;
        chunk_refresh_period = 10;
        chunk_properties_update_period = 10;
    };

    cypress_manager = {
        statistics_flush_period = 10;
    };

    security_manager = {
        statistics_flush_period = 10;
        request_rate_smoothing_period = 60000;
    };

    node_tracker = {
        online_node_timeout = 1000;
    };

    object_manager = {
        gc_sweep_period = 10;
    };

    hive_manager = {
        ping_period = 1000;
        rpc_timeout = 100;
    };

    logging = {
        rules = [
            {
                min_level = Info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = Debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
            {
                min_level = Error;
                writers = [ stderr ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = Stderr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = File;
                file_name = "master-0.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = raw;
                file_name = "master-0.debug.log";
            };
        }
  };
}
""")

def get_scheduler_config():
    return yson.loads(
"""
{
    masters = {
        addresses = [ ];
    };

    timestamp_provider = {
        addresses = [ ];
    };

    scheduler = {
        strategy = fair_share;
        max_failed_job_count = 10;
        snapshot_period = 100000000;
        connect_grace_delay = 0;
        environment = {
             PYTHONUSERBASE = "/tmp"
        };
    };

    logging = {
        rules = [
            {
                min_level = Info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = Debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
            {
                min_level = Error;
                writers = [ stderr ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = Stderr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = File;
                file_name = "scheduler-0.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = raw;
                file_name = "scheduler-0.debug.log";
            };
        }
    }
}
""")


def get_driver_config():
    return yson.loads(
"""
{
    masters = {
        addresses = [ ];
        rpc_timeout = 30000;
    };

    timestamp_provider = {
        addresses = [ ];
    };

    logging = {
        rules = [
            {
                min_level = Info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = Debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
            {
                min_level = Error;
                writers = [ stderr ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = Stderr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = File;
                file_name = "ytdriver.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = Raw;
                file_name = "ytdriver.debug.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
        };
    };
    "format_defaults" = {
        "structured" = <
            "format" = "text"
        > "yson";
        "tabular" = <
            "format" = "text"
        > "yson"
    };
    "operation_wait_timeout" = 3000;
    "transaction_manager" = {
        "ping_period" = 5000
    }
}
""")

def get_node_config():
    return yson.loads(
"""
{
    data_node = {
        cache_location = {
            path = "";
        };
        store_locations = [];
        incremental_heartbeat_period = 100;
    };

    exec_agent = {
        scheduler_connector = {
            heartbeat_period = 100;
        };

        environment_manager = {
            environments = {
                default = {
                    type = unsafe;
                };
            };
        };

        job_controller = {
            resource_limits = {
                memory = 8000000000;
                slots = 1;
            };
        };

        slot_manager = {
            path = "";
        };

        job_proxy_logging = {
            rules = [
                {
                    min_level = Info;
                    writers = [ file ];
                    categories  = [ "*" ];
                };
                {
                    min_level = Debug;
                    writers = [ raw ];
                    categories  = [ "*" ];
                };
                {
                    min_level = Error;
                    writers = [ stderr ];
                    categories  = [ "*" ];
                };
            ];
            writers = {
                stderr = {
                    type = Stderr;
                    pattern = "$(datetime) $(level) $(category) $(message)";
                };
                file = {
                    type = File;
                    file_name = "job_proxy-0.log";
                    pattern = "$(datetime) $(level) $(category) $(message)";
                };
                raw = {
                    type = Raw;
                    file_name = "job_proxy-0.debug.log";
                };
            }
        };
    };

    tablet_node = {
        changelogs = {
            path = "";
        };
        snapshots = {
            temp_path = "";
        };
    };

    query_agent = {
    };

    masters = {
        addresses = [];
        rpc_timeout = 5000
    };

    timestamp_provider = {
        addresses = [ ];
    };

    logging = {
        rules = [
            {
                min_level = info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = stderr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = file;
                file_name = "node-0.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = raw;
                file_name = "node-0.debug.log";
            };
        }
    };
}
""")

def get_proxy_config():
    return json.loads(
"""
{
    "port" : -1,
    "log_port" : -1,
    "address" : "localhost",
    "number_of_workers" : 1,
    "memory_limit" : 33554432,
    "thread_limit" : 64,
    "spare_threads" : 4,

    "neighbours" : [  ],

    "logging" : {
        "filename" : "/dev/null"
    },

    "authentication" : {
        "enable" : false
    },

    "coordination" : {
        "enable" : false
    },

    "proxy" : {
        "logging" : {
            "rules" : [ ],
            "writers" : { }
        },
        "driver" : { }
    }
}
""")
