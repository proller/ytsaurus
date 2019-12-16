from yt_env_setup import YTEnvSetup, unix_only, wait
from yt_commands import *

from yt.wrapper import JsonFormat

from copy import deepcopy
from random import shuffle

import pytest
import time

##################################################################

class TestRpcProxy(YTEnvSetup):
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True

    @authors("kiselyovp")
    def test_non_sticky_transactions_dont_stick(self):
        tx = start_transaction(timeout=1000)
        wait(lambda: not exists("//sys/transactions/" + tx))

    @authors("prime")
    def test_dynamic_config(self):
        proxy_name = ls("//sys/rpc_proxies")[0]

        set("//sys/rpc_proxies/@config", {"tracing": {"user_sample_rate": {"prime": 1.0}}})

        def config_updated():
            config = get("//sys/rpc_proxies/" + proxy_name + "/orchid/coordinator/dynamic_config")
            return "prime" in config["tracing"]["user_sample_rate"]

        wait(config_updated)

##################################################################

class TestRpcProxyBase(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 5
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True
    ENABLE_HTTP_PROXY = True

    _schema_dicts = [{"name": "index", "type": "int64"},
                     {"name": "str", "type": "string"}]
    _schema = make_schema(_schema_dicts, strict=True)

    _schema_dicts_sorted = [{"name": "index", "type": "int64", "sort_order": "ascending"},
                            {"name": "str", "type": "string"}]
    _schema_sorted = make_schema(_schema_dicts_sorted, strict=True, unique_keys=True)
    _sample_index = 241
    _sample_text = "sample text"
    _sample_line = {"index": _sample_index, "str": _sample_text}

    def _create_simple_table(self, path, data = [], dynamic=True, sorted=True, **kwargs):
        schema = self._schema_sorted if sorted else self._schema
        create("table", path,
               attributes={
                   "dynamic": dynamic,
                   "schema": schema},
               **kwargs)

        if dynamic:
            sync_create_cells(1)
        if not data:
            return
        if dynamic:
            sync_mount_table(path)
            insert_rows(path, data)
            sync_unmount_table(path)
        else:
            insert_rows(path, data)

    def _start_simple_operation(self, cmd, **kwargs):
        self._create_simple_table("//tmp/t_in",
                                  data=[self._sample_line],
                                  sorted=True,
                                  dynamic=True)
        self._create_simple_table("//tmp/t_out", dynamic=False, sorted=True)

        return map(in_="//tmp/t_in",
                   out="//tmp/t_out",
                   track=False,
                   mapper_command=cmd,
                   **kwargs)


    def _start_simple_operation_on_fs(self, event_name="barrier", **kwargs):
        return self._start_simple_operation(events_on_fs().wait_event_cmd(event_name), **kwargs)

    def _start_simple_operation_with_breakpoint(self, cmd_with_breakpoint = "BREAKPOINT", **kwargs):
        return self._start_simple_operation(events_on_fs().with_breakpoint(cmd_with_breakpoint), **kwargs)

    def _prepare_output_table(self):
        alter_table("//tmp/t_out", dynamic=True, schema=self._schema_sorted)
        sync_mount_table("//tmp/t_out")

    def _remove_simple_tables(self):
        remove("//tmp/t_in", recursive=True)
        remove("//tmp/t_out", recursive=True)

##################################################################

class TestOperationsRpcProxy(TestRpcProxyBase):
    @authors("kiselyovp")
    def test_map_reduce_simple(self):
        self._create_simple_table("//tmp/t_in",
                                  data=[self._sample_line])

        self._create_simple_table("//tmp/t_out", dynamic=False)
        map_reduce(in_="//tmp/t_in",
                   out="//tmp/t_out",
                   sort_by="index",
                   reducer_command="cat")

        self._prepare_output_table()

        assert len(select_rows("* from [//tmp/t_out]")) == 1
        assert len(lookup_rows("//tmp/t_out", [{"index": self._sample_index - 2}])) == 0
        assert len(lookup_rows("//tmp/t_out", [{"index": self._sample_index}])) == 1

    @authors("kiselyovp")
    def test_sort(self):
        size = 10 ** 3
        original_table = [{"index": num, "str": "number " + str(num)} for num in range(size)]
        new_table = deepcopy(original_table)
        shuffle(new_table)

        self._create_simple_table("//tmp/t_in1",
                                  data=new_table[:size / 2],
                                  sorted=False)
        self._create_simple_table("//tmp/t_in2",
                                  data=new_table[size / 2:],
                                  sorted=False)
        self._create_simple_table("//tmp/t_out",
                                  dynamic=False,
                                  sorted=True)
        sort(in_=["//tmp/t_in1", "//tmp/t_in2"],
             out="//tmp/t_out",
             sort_by="index")

        self._prepare_output_table()

        assert select_rows("* from [//tmp/t_out] LIMIT " + str(2 * size)) == original_table

    @authors("kiselyovp")
    def test_abort_operation(self):
        op = self._start_simple_operation_with_breakpoint()
        wait(lambda: op.get_state() == "running")

        op.abort()

        wait(lambda: op.get_state() == "aborted")

    @authors("kiselyovp")
    def test_complete_operation(self):
        op = self._start_simple_operation_with_breakpoint()
        wait(lambda: op.get_state() == "running")

        op.complete()

        op.track()
        assert op.get_state() == "completed"

    def _check_get_operation(self, op):
        def filter_attrs(attrs):
            PROPER_ATTRS = [
                "id",
                "authenticated_user",
                "brief_progress",
                "brief_spec",
                "runtime_parameters",
                "finish_time",
                "type",
                # COMPAT(levysotsky): Old name for "type"
                "operation_type",
                "result",
                "start_time",
                "state",
                "suspended",
                "spec",
                "unrecognized_spec",
                "full_spec",
                "slot_index_per_pool_tree",
            ]
            return {key: attrs[key] for key in PROPER_ATTRS if key in attrs}

        res_get_operation = get_operation(op.id, include_scheduler=True)
        res_cypress = _get_operation_from_cypress(op.id)

        assert filter_attrs(res_get_operation) == filter_attrs(res_cypress)

    @authors("kiselyovp")
    def test_suspend_resume_operation(self):
        op = self._start_simple_operation_with_breakpoint()
        wait(lambda: op.get_state() == "running")

        op.suspend(abort_running_jobs=True)
        wait(lambda: get(op.get_path() + "/@suspended"))
        assert op.get_state() == "running"
        events_on_fs().release_breakpoint()
        time.sleep(2)
        assert get(op.get_path() + "/@suspended")
        assert op.get_state() == "running"

        op.resume()
        op.track()
        assert op.get_state() == "completed"

    @authors("kiselyovp")
    def test_update_op_params_check_perms(self):
        op = self._start_simple_operation_with_breakpoint()
        wait(lambda: op.get_state() == "running")

        create_user("u")

        update_op_parameters(op.id, parameters={"acl": [make_ace("allow", "u", ["read", "manage"])]})
        # No exception.
        op.complete(authenticated_user="u")

        events_on_fs().release_breakpoint()
        op.track()

##################################################################

class TestDumpJobContextRpcProxy(TestRpcProxyBase):
    REQUIRE_YTSERVER_ROOT_PRIVILEGES = True

    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "statistics_reporter": {
                "enabled": True,
                "reporting_period": 10,
                "min_repeat_delay": 10,
                "max_repeat_delay": 10,
            }
        }
    }

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "enable_job_reporter": True,
            "enable_job_spec_reporter": True,
            "enable_job_stderr_reporter": True,
        }
    }

    @authors("kiselyovp")
    def test_dump_job_context(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", {"foo": "bar"})

        op = map(
            track=False,
            label="dump_job_context",
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                }
            })

        jobs = wait_breakpoint()
        # Wait till job starts reading input
        wait(lambda: get(op.get_path() + "/controller_orchid/running_jobs/" + jobs[0] + "/progress") >= 0.5)

        dump_job_context(jobs[0], "//tmp/input_context")

        release_breakpoint()
        op.track()

        context = read_file("//tmp/input_context")
        assert get("//tmp/input_context/@description/type") == "input_context"
        assert JsonFormat().loads_row(context)["foo"] == "bar"

##################################################################

class TestPessimisticQuotaCheckRpcProxy(TestRpcProxyBase):
    NUM_MASTERS = 1
    NUM_NODES = 16
    NUM_SCHEDULERS = 0

    REPLICATOR_REACTION_TIME = 3.5

    def _replicator_sleep(self):
        time.sleep(self.REPLICATOR_REACTION_TIME)

    def _set_account_chunk_count_limit(self, account, value):
        set("//sys/accounts/{0}/@resource_limits/chunk_count".format(account), value)

    def _set_account_tablet_count_limit(self, account, value):
        set("//sys/accounts/{0}/@resource_limits/tablet_count".format(account), value)

    def _is_account_disk_space_limit_violated(self, account):
        return get("//sys/accounts/{0}/@violated_resource_limits/disk_space".format(account))

    def _is_account_chunk_count_limit_violated(self, account):
        return get("//sys/accounts/{0}/@violated_resource_limits/chunk_count".format(account))

    @authors("kiselyovp")
    def test_chunk_count_limits(self):
        create_account("max")
        self._set_account_tablet_count_limit("max", 100500)

        self._create_simple_table("//tmp/t", [self._sample_line])
        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "max")

        self._set_account_chunk_count_limit("max", 0)
        self._replicator_sleep()
        assert not self._is_account_chunk_count_limit_violated("max")

        with pytest.raises(YtError): copy("//tmp/t", "//tmp/a/t")
        assert not exists("//tmp/a/t")
        copy("//tmp/t", "//tmp/a/t", pessimistic_quota_check=False)
        assert exists("//tmp/a/t")

    @authors("kiselyovp")
    def test_disk_space_limits(self):
        create_account("max")
        self._set_account_tablet_count_limit("max", 100500)

        self._create_simple_table("//tmp/t", [self._sample_line])
        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "max")

        set_account_disk_space_limit("max", 0)
        self._replicator_sleep()
        assert not self._is_account_disk_space_limit_violated("max")

        with pytest.raises(YtError): copy("//tmp/t", "//tmp/a/t")
        assert not exists("//tmp/a/t")
        copy("//tmp/t", "//tmp/a/t", pessimistic_quota_check=False)
        assert exists("//tmp/a/t")

##################################################################

class TestPessimisticQuotaCheckMulticellRpcProxy(TestPessimisticQuotaCheckRpcProxy):
    NUM_SECONDARY_MASTER_CELLS = 2
    NUM_SCHEDULERS = 1

##################################################################

class TestModifyRowsRpcProxy(TestRpcProxyBase):
    BATCH_CAPACITY = 10
    DELTA_DRIVER_CONFIG = {"modify_rows_batch_capacity": BATCH_CAPACITY}

    def _test_modify_rows_batching(self, request_count, key_count, tx_type="tablet"):
        self._create_simple_table("//tmp/table")
        sync_mount_table("//tmp/table")

        tx = start_transaction(type=tx_type, sticky=True)

        for i in range(request_count):
            insert_rows(
                "//tmp/table",
                [{"index": i % key_count, "str": str(i / key_count)}],
                tx=tx)

        commit_transaction(tx)

        expected_result = [
            {"index": i,
            "str": str((request_count - i - 1) // key_count)}\
                for i in range(key_count)]
        assert select_rows("* from [//tmp/table]") == expected_result

        sync_unmount_table("//tmp/table")
        remove("//tmp/table")

    @authors("kiselyovp")
    def test_modify_rows_batching(self):
        self._test_modify_rows_batching(60, 7, "tablet")
        self._test_modify_rows_batching(65, 7, "master")

