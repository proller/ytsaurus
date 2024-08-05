RECURSE(
    bin
)

RECURSE_FOR_TESTS(
    unittests
)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/yt/ya_cpp.make.inc)

PEERDIR(
    yt/yt/library/query/engine
    yt/chyt/client
    yt/chyt/client/protos
    yt/chyt/server/protos
    yt/yt/server/lib
    yt/yt/server/lib/chunk_pools
    yt/yt/ytlib
    yt/yt/library/clickhouse_discovery
    yt/yt/library/clickhouse_functions
    contrib/clickhouse/src
    contrib/clickhouse/base/base
    contrib/deprecated/clickhouse_data_streams_compat
)

ADDINCL(
    contrib/clickhouse/src
    contrib/clickhouse/base
    contrib/libs/double-conversion
    contrib/libs/poco/Foundation/include
    contrib/libs/poco/Net/include
    contrib/libs/poco/Util/include
    contrib/libs/poco/XML/include
    contrib/libs/re2

    contrib/libs/rapidjson/include
)

SRCS(
    block_input_stream.cpp
    block_output_stream.cpp
    bootstrap.cpp
    ch_to_yt_converter.cpp
    clickhouse_config.cpp
    clickhouse_invoker.cpp
    clickhouse_server.cpp
    clickhouse_service.cpp
    clickhouse_singletons.cpp
    cluster_nodes.cpp
    columnar_conversion.cpp
    computed_columns.cpp
    config_repository.cpp
    config.cpp
    conversion.cpp
    data_type_boolean.cpp
    dictionary_source.cpp
    format.cpp
    function_helpers.cpp
    GLOBAL functions_version.cpp
    granule_min_max_filter.cpp
    health_checker.cpp
    helpers.cpp
    host.cpp
    http_handler.cpp
    index.cpp
    invoker_liveness_checker.cpp
    job_size_constraints.cpp
    launcher_compatibility.cpp
    logger.cpp
    logging_transform.cpp
    memory_watchdog.cpp
    object_lock.cpp
    poco_config.cpp
    prewhere_block_input_stream.cpp
    private.cpp
    query_analyzer.cpp
    query_context.cpp
    query_registry.cpp
    query_service.cpp
    revision_tracker.cpp
    schema_inference.cpp
    secondary_query_header.cpp
    stack_size_checker.cpp
    std_helpers.cpp
    storage_base.cpp
    storage_distributor.cpp
    storage_subquery.cpp
    storage_system_clique.cpp
    storage_system_log_table_exporter.cpp
    storages_yt_nodes.cpp
    subquery_spec.cpp
    subquery.cpp
    table_function_yt_list_log_tables.cpp
    table_function_yt_node_attributes.cpp
    table_function_yt_secondary_query.cpp
    table_function_yt_tables.cpp
    table_functions_concat.cpp
    table_functions_list_dir.cpp
    table_functions.cpp
    table_traverser.cpp
    table.cpp
    tcp_handler.cpp
    user_defined_sql_objects_storage.cpp
    version.cpp
    virtual_column.cpp
    yt_to_ch_converter.cpp
    yt_database.cpp
)

END()
