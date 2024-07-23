from yt_env_setup import (
    YTEnvSetup,
    Restarter,
    CONTROLLER_AGENTS_SERVICE,
)

from yt_commands import (
    authors,
    copy,
    create,
    create_user,
    get,
    join_reduce,
    raises_yt_error,
    read_table,
    release_breakpoint,
    wait_breakpoint,
    with_breakpoint,
    write_table,
    sorted_dicts,
    get_driver,
    write_file,
    map,
    map_reduce,
    merge,
    reduce,
    sort,
)

from yt_helpers import skip_if_no_descending
import yt.yson as yson

from textwrap import dedent
import pytest
from random import Random


##################################################################


class TestSchedulerRemoteOperationCommandsBase(YTEnvSetup):
    NUM_TEST_PARTITIONS = 5

    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    NUM_REMOTE_CLUSTERS = 1

    NUM_MASTERS_REMOTE_0 = 1
    NUM_SCHEDULERS_REMOTE_0 = 0

    REMOTE_CLUSTER_NAME = "remote_0"

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "snapshot_period": 500,
            "remote_copy_operation_options": {
                "spec_template": {
                    "use_remote_master_caches": True,
                },
            },
            "disallow_remote_operations": {
                "allowed_users": ["root"],
                "allowed_clusters": ["remote_0"],
            }
        },
    }

    @classmethod
    def setup_class(cls):
        super(TestSchedulerRemoteOperationCommandsBase, cls).setup_class()
        cls.remote_driver = get_driver(cluster=cls.REMOTE_CLUSTER_NAME)

    def to_remote_path(self, path):
        return f"<cluster={self.REMOTE_CLUSTER_NAME}>{path}"


##################################################################


class TestSchedulerRemoteOperationCommands(TestSchedulerRemoteOperationCommandsBase):
    @authors("coteeq")
    def test_map_empty_table(self):
        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t2")

        map(
            in_=self.to_remote_path("//tmp/t1"),
            out="//tmp/t2",
            command="cat",
        )

        assert read_table("//tmp/t2") == []
        assert not get("//tmp/t2/@sorted")

    @authors("coteeq")
    def test_map_only_remote_table(self):
        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t2")

        data = [{"a": 1}, {"a": 2}]
        write_table("<append=%true>//tmp/t1", data, driver=self.remote_driver)

        map(
            in_=self.to_remote_path("//tmp/t1"),
            out="//tmp/t2",
            command="cat",
        )

        assert sorted_dicts(read_table("//tmp/t2")) == sorted_dicts(data)
        assert not get("//tmp/t2/@sorted")

    @authors("coteeq")
    def test_map_remote_and_local_table(self):
        n_chunks = 2
        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t1")
        create("table", "//tmp/t_out")
        data1 = [{"a": 1}, {"a": 2}]
        data2 = [{"a": 10}, {"a": 20}]
        for _ in range(n_chunks):
            write_table("<append=%true>//tmp/t1", data1, driver=self.remote_driver)
            write_table("<append=%true>//tmp/t1", data2)

        map(
            in_=[
                self.to_remote_path("//tmp/t1"),
                "//tmp/t1",
            ],
            out="//tmp/t_out",
            command="cat",
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json",
                    "enable_input_table_index": False,
                },
            },
        )

        assert sorted_dicts(read_table("//tmp/t_out")) == sorted_dicts((data1 + data2) * n_chunks)
        assert not get("//tmp/t_out/@sorted")

    def _upload_mapper_and_reducer(self):
        mapper = dedent(
            """
            import json
            import sys
            rows = []
            for row in sys.stdin:
                rows.append(json.loads(row))
                if rows[-1]['a'] is None:
                    print(row, file=sys.stderr)
                    raise RuntimeError()
            for row in rows:
                print(json.dumps({'a': row['a'] * 10}))
            """
        )

        reducer = dedent(
            """
            import json
            import sys
            rows = []
            for row in sys.stdin:
                rows.append(json.loads(row))
                if rows[-1]['a'] is None:
                    print(row, file=sys.stderr)
                    raise RuntimeError()
            if len(rows) == 0:
                raise RuntimeError()
            print(json.dumps({'a': sum(row['a'] for row in rows)}))
            """
        )

        create("file", "//tmp/mapper.py")
        create("file", "//tmp/reducer.py")
        write_file("//tmp/mapper.py", mapper.encode("ascii"))
        write_file("//tmp/reducer.py", reducer.encode("ascii"))

    @authors("coteeq")
    def test_map_remote_and_local_with_mapper(self):
        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        data1 = [{"a": 1}, {"a": 2}]
        data2 = [{"a": 10}, {"a": 20}]
        write_table("//tmp/t1", data1, driver=self.remote_driver)
        write_table("//tmp/t1", data2)

        self._upload_mapper_and_reducer()

        map(
            in_=[
                self.to_remote_path("//tmp/t1"),
                "//tmp/t1",
            ],
            out="//tmp/t2",
            mapper_file=["//tmp/mapper.py"],
            mapper_command="python3 mapper.py",
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json",
                    "enable_input_table_index": False,
                },
            },
        )

        expected = [{"a": row["a"] * 10} for row in data1 + data2]
        assert sorted_dicts(read_table("//tmp/t2")) == sorted_dicts(expected)
        assert not get("//tmp/t2/@sorted")

    @authors("coteeq")
    def test_map_reduce_small_table(self):
        driver = self.remote_driver
        create("table", "//tmp/t1", driver=driver)
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"a": 1}, {"a": 2}], driver=driver)

        self._upload_mapper_and_reducer()

        map_reduce(
            in_=self.to_remote_path("//tmp/t1"),
            out="//tmp/t2",
            mapper_file=["//tmp/mapper.py"],
            reducer_file=["//tmp/reducer.py"],
            mapper_command="python3 mapper.py",
            reducer_command="python3 reducer.py",
            reduce_by=["a"],
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json",
                },
                "reducer": {
                    "input_format": "json",
                    "output_format": "json",
                },
            }
        )

        assert read_table("//tmp/t2") == [{"a": 30}]

    @authors("coteeq")
    @pytest.mark.parametrize("sort_order", ["ascending", "descending"])
    def test_reduce_cat(self, sort_order):
        if sort_order == "descending":
            skip_if_no_descending(self.Env)

        create("table", "//tmp/in1")
        rows = [
            {"key": 0, "value": 1},
            {"key": 2, "value": 2},
            {"key": 4, "value": 3},
            {"key": 7, "value": 4},
        ]
        if sort_order == "descending":
            rows = rows[::-1]
        write_table(
            "//tmp/in1",
            rows,
            sorted_by=[{"name": "key", "sort_order": sort_order}],
        )

        create("table", "//tmp/in2", driver=self.remote_driver)
        rows = [
            {"key": -1, "value": 5},
            {"key": 1, "value": 6},
            {"key": 3, "value": 7},
            {"key": 5, "value": 8},
        ]
        if sort_order == "descending":
            rows = rows[::-1]
        write_table(
            "//tmp/in2",
            rows,
            sorted_by=[{"name": "key", "sort_order": sort_order}],
            driver=self.remote_driver,
        )

        create("table", "//tmp/out")

        reduce(
            in_=["//tmp/in1", self.to_remote_path("//tmp/in2")],
            out="<sorted_by=[{{name=key;sort_order={}}}]>//tmp/out".format(sort_order),
            reduce_by=[{"name": "key", "sort_order": sort_order}],
            command="cat",
            spec={"reducer": {"format": "dsv"}},
        )

        expected = [
            {"key": "-1", "value": "5"},
            {"key": "0", "value": "1"},
            {"key": "1", "value": "6"},
            {"key": "2", "value": "2"},
            {"key": "3", "value": "7"},
            {"key": "4", "value": "3"},
            {"key": "5", "value": "8"},
            {"key": "7", "value": "4"},
        ]
        if sort_order == "descending":
            expected = expected[::-1]
        assert read_table("//tmp/out") == expected
        assert get("//tmp/out/@sorted")

    @authors("coteeq")
    @pytest.mark.timeout(30)
    def test_revive_map(self):
        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"a": 1}, {"a": 2}], driver=self.remote_driver)
        write_table("<append=%true>//tmp/t1", [{"a": 3}, {"a": 4}], driver=self.remote_driver)

        self._upload_mapper_and_reducer()

        op = map(
            in_=self.to_remote_path("//tmp/t1"),
            out="//tmp/t2",
            mapper_file=["//tmp/mapper.py"],
            mapper_command=with_breakpoint("BREAKPOINT; python3 mapper.py"),
            spec={
                "mapper": {"input_format": "json", "output_format": "json"},
            },
            track=False,
        )

        wait_breakpoint()

        with Restarter(self.Env, [CONTROLLER_AGENTS_SERVICE]):
            pass

        release_breakpoint()

        op.track()

        assert sorted_dicts(read_table("//tmp/t2")) == [{"a": 10}, {"a": 20}, {"a": 30}, {"a": 40}]
        assert not get("//tmp/t2/@sorted")

    @authors("coteeq")
    def test_table_reuses_chunk(self):
        create("table", "//tmp/t1", driver=self.remote_driver)
        write_table("//tmp/t1", [{"a": 1}, {"a": 2}], driver=self.remote_driver)
        copy("//tmp/t1", "//tmp/t2", driver=self.remote_driver)
        assert get("//tmp/t1/@chunk_ids", driver=self.remote_driver) == get("//tmp/t2/@chunk_ids", driver=self.remote_driver)

        create("table", "//tmp/out")

        self._upload_mapper_and_reducer()

        map(
            in_=[self.to_remote_path("//tmp/t1"), self.to_remote_path("//tmp/t2")],
            out="//tmp/out",
            mapper_file=["//tmp/mapper.py"],
            mapper_command="python3 mapper.py",
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json",
                    "enable_input_table_index": False,
                },
            },
        )

        assert sorted_dicts(read_table("//tmp/out")) == sorted_dicts([{"a": 10}, {"a": 20}] * 2)

    @authors("coteeq")
    def test_sort(self):
        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t2")

        data = list(range(10))
        Random(123).shuffle(data)
        remote_data = [{"value" : val} for val in data[:5]]
        local_data = [{"value" : val} for val in data[5:]]

        write_table("//tmp/t1", remote_data, driver=self.remote_driver)
        write_table("//tmp/t2", local_data)

        create("table", "//tmp/out")

        sort(
            in_=[
                self.to_remote_path("//tmp/t1"),
                "//tmp/t2",
            ],
            out="//tmp/out",
            sort_by=["value"],
        )

        assert read_table("//tmp/out") == [{"value": val} for val in list(range(10))]

    @authors("coteeq")
    def test_tricky_reduce(self):
        sorted_by = [
            {"name": "key", "sort_order": "ascending"},
            {"name": "value1", "sort_order": "ascending"},
        ]

        remote_data = [
            [
                {"key": 1, "value1":  1, "value2": 2},
                {"key": 2, "value1": 10, "value2": 2},
            ],
            [
                {"key": 3, "value1":  1, "value2": 2},
                {"key": 4, "value1": 10, "value2": 2},
            ],
            [
                {"key": 5, "value1":  1, "value2": 2},
            ],
        ]
        local_data = [
            [
                {"key": 1, "value1": 10, "value2": 20},
                {"key": 2, "value1":  1, "value2": 20},
            ],
            [
                {"key": 3, "value1": 10, "value2": 20},
                {"key": 4, "value1":  1, "value2": 20},
            ],
            [
                {"key": 5, "value1": 10, "value2": 20},
            ],
        ]

        create("table", "//tmp/t1", driver=self.remote_driver)
        create("table", "//tmp/t2")

        for datum in remote_data:
            write_table("<append=%true>//tmp/t1", datum, sorted_by=sorted_by, driver=self.remote_driver)
        for datum in local_data:
            write_table("<append=%true>//tmp/t2", datum, sorted_by=sorted_by)

        reducer = dedent(
            """
            import sys
            from sys import stdin
            from json import loads, dumps

            firsts = dict()
            for line in stdin:
                row = loads(line)
                if "$attributes" in line:
                    continue
                if row["key"] not in firsts:
                    firsts[row["key"]] = row["value2"]

            for key, first in firsts.items():
                print(dumps({"key": key, "first": first}))
            """
        )

        create("file", "//tmp/reducer.py")
        write_file("//tmp/reducer.py", reducer.encode("ascii"))

        create("table", "//tmp/out")

        reduce(
            in_=[
                self.to_remote_path("//tmp/t1"),
                "//tmp/t2",
            ],
            reducer_file=["//tmp/reducer.py"],
            reducer_command="python3 reducer.py",
            out="""<sorted_by=[{name=key; sort_order=ascending}]>//tmp/out""",
            reduce_by=["key"],
            sort_by=["key", "value1"],
            spec={
                "reducer": {"input_format": "json", "output_format": "json"},
                "data_size_per_job": 1,
            },
        )

        assert read_table("//tmp/out") == [
            {"key": 1, "first": 2},
            {"key": 2, "first": 20},
            {"key": 3, "first": 2},
            {"key": 4, "first": 20},
            {"key": 5, "first": 2},
        ]

    @authors("coteeq")
    @pytest.mark.parametrize("mode", ["unordered", "ordered", "sorted"])
    def test_merge_does_not_teleport(self, mode):
        create("table", "//tmp/t_in", driver=self.remote_driver)
        create("table", "//tmp/t_out")

        sorted_by = "<sorted_by=[{name=key; sort_order=ascending}]>"

        data = [{"key": i, "value": i + 100} for i in range(0, 10)]

        write_table(sorted_by + "//tmp/t_in", data, driver=self.remote_driver)

        merge_by = {"merge_by": ["key"]} if mode == "sorted" else {}
        merge(
            in_=[
                self.to_remote_path("//tmp/t_in"),
            ],
            out="//tmp/t_out",
            mode=mode,
            **merge_by,
        )

        if mode == "sorted":
            assert get("//tmp/t_out/@sorted")

        assert read_table("//tmp/t_out") == data

    @authors("coteeq")
    def test_merge_interleave_rows(self):
        create("table", "//tmp/t_in", driver=self.remote_driver)
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")

        sorted_by = "<sorted_by=[{name=key; sort_order=ascending}]>"

        data1 = [{"key": i, "value": i + 100} for i in range(0, 10, 2)]
        data2 = [{"key": i, "value": i + 100} for i in range(1, 10, 2)]

        write_table(sorted_by + "//tmp/t_in", data1, driver=self.remote_driver)
        write_table(sorted_by + "//tmp/t_in", data2)

        merge(
            in_=[
                self.to_remote_path("//tmp/t_in"),
                "//tmp/t_in"
            ],
            out="//tmp/t_out",
            mode="sorted",
            merge_by=["key"],
        )

        expected = data1 + data2
        expected.sort(key=lambda row: row["key"])

        assert get("//tmp/t_out/@sorted")
        assert read_table("//tmp/t_out") == expected

    @authors("coteeq")
    @pytest.mark.parametrize("remote_primary", [False, True])
    @pytest.mark.parametrize("remote_foreign", [False, True])
    def test_join_reduce(self, remote_primary, remote_foreign):
        def _get_driver(remote):
            if remote:
                return self.remote_driver
            else:
                return get_driver()

        create("table", "//tmp/primary", driver=_get_driver(remote_primary))
        create("table", "//tmp/foreign", driver=_get_driver(remote_foreign))
        create("table", "//tmp/out")

        primary_data = [
            {"key": "1_primary"},
            {"key": "2_primary_foreign"},
        ]

        foreign_data = [
            {"key": "2_primary_foreign"},
            {"key": "3_foreign"},
        ]

        sorted_by = "<sorted_by=[{name=key; sort_order=ascending}]>"
        sorted_by_and_append = "<sorted_by=[{name=key; sort_order=ascending}]; append=%true>"

        for row in primary_data:
            write_table(sorted_by_and_append + "//tmp/primary", [row], driver=_get_driver(remote_primary))
        for row in foreign_data:
            write_table(sorted_by_and_append + "//tmp/foreign", [row], driver=_get_driver(remote_foreign))

        primary_path = "//tmp/primary"
        foreign_path = "<foreign=%true>//tmp/foreign"
        if remote_primary:
            primary_path = self.to_remote_path(primary_path)
        if remote_foreign:
            foreign_path = f"<foreign=%true;cluster=\"{self.REMOTE_CLUSTER_NAME}\">//tmp/foreign"

        join_reduce(
            in_=[
                primary_path,
                foreign_path,
            ],
            out=sorted_by + "//tmp/out",
            reduce_by=["key"],
            join_by=["key"],
            command="cat",
            spec={
                "reducer": {"format": yson.loads(b"<line_prefix=tskv; enable_table_index=true>dsv")},
                "data_size_per_job": 1,
                "enable_key_guarantee": True,
            }
        )

        expected = [
            {"key": "1_primary", "@table_index": "0"},
            {"key": "2_primary_foreign", "@table_index": "0"},
            {"key": "2_primary_foreign", "@table_index": "1"},
        ]
        assert read_table("//tmp/out") == expected

    @authors("coteeq")
    def test_disallow(self):
        create_user("user-not-allowed")
        with raises_yt_error("not allowed to start operations"):
            map(
                in_=self.to_remote_path("//tmp/t"),
                out_="//tmp/out",
                authenticated_user="user-not-allowed",
                command="cat"
            )

        with raises_yt_error("not allowed to be an input remote cluster"):
            map(
                # NB: Cluster 'not-allowed' does not need to exist
                in_="""<cluster="not-allowed">//tmp/t""",
                out_="//tmp/out",
                command="cat"
            )
