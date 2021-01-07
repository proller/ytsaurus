from base import ClickHouseTestBase, Clique, QueryFailedError

from yt_commands import (set, create, write_table, read_table, authors, raises_yt_error, remove, merge)

import yt.yson as yson


class BaseTestYsonFunctions(ClickHouseTestBase):
    ENABLE_COMPOSITE_CONVERSION = False

    def setup(self):
        self._setup()

        create(
            "table",
            "//tmp/t",
            attributes={
                "schema": [
                    {"name": "i", "type": "int64"},
                    {"name": "v", "type": "any"},
                    {"name": "key", "type": "string"},
                    {"name": "fmt", "type": "string"},
                ]
            },
        )
        write_table(
            "//tmp/t",
            [
                {
                    "i": 0,
                    "v": {
                        "i64": -42,
                        "ui64": yson.YsonUint64(23),
                        "bool": True,
                        "dbl": 3.14,
                        "str": "xyz",
                        "subnode": {
                            "i64": 123,
                        },
                        "arr_i64": [-1, 0, 1],
                        "arr_ui64": [1, 1, 2, 3, 5],
                        "arr_dbl": [-1.1, 2.71],
                        "arr_bool": [False, True, False],
                    },
                    "key": "/arr_i64/0",
                },
                {
                    "i": 1,
                    "v": {
                        "i64": "xyz",  # Wrong type.
                    },
                    "key": "/i64",
                    "fmt": "text",
                },
                {
                    "i": 2,
                    "v": {
                        "i64": yson.YsonUint64(2 ** 63 + 42),  # Out of range for getting value as i64.
                    },
                    "fmt": "text",
                },
                {
                    "i": 3,
                    "v": {},  # Key i64 is missing.
                    "fmt": "pretty",
                },
                {
                    "i": 4,
                    "v": {
                        "i64": 57,
                    },
                    "key": None,
                    "fmt": "pretty",
                },
                {
                    "i": 5,
                    "v": None,
                    "key": "/unknown",
                },
            ],
        )
        set(
            "//sys/clickhouse/config/yt/settings/composite/enable_conversion",
            self.ENABLE_COMPOSITE_CONVERSION,
            recursive=True,
        )

    def teardown(self):
        remove("//sys/clickhouse/config/yt/settings/composite/enable_conversion", force=True)

    @authors("max42")
    def test_read_int64_strict(self):
        with Clique(1) as clique:
            for i in xrange(4):
                query = "select YPathInt64Strict(v, '/i64') from \"//tmp/t\" where i = {}".format(i)
                if i != 0:
                    with raises_yt_error(QueryFailedError):
                        clique.make_query(query)
                else:
                    result = clique.make_query(query)
                    assert result[0].popitem()[1] == -42

    @authors("max42")
    def test_read_uint64_strict(self):
        with Clique(1) as clique:
            result = clique.make_query("select YPathUInt64Strict(v, '/i64') from \"//tmp/t\" where i = 4")
            assert result[0].popitem()[1] == 57

    @authors("max42")
    def test_read_from_subnode(self):
        with Clique(1) as clique:
            result = clique.make_query("select YPathUInt64Strict(v, '/subnode/i64') from \"//tmp/t\" where i = 0")
            assert result[0].popitem()[1] == 123

    @authors("max42", "dakovalkov")
    def test_read_int64_non_strict(self):
        with Clique(1) as clique:
            query = "select YPathInt64(v, '/i64') from \"//tmp/t\""
            result = clique.make_query(query)
            for i, item in enumerate(result):
                if i == 0:
                    assert item.popitem()[1] == -42
                elif i == 4:
                    assert item.popitem()[1] == 57
                elif i == 5:
                    assert item.popitem()[1] is None
                else:
                    assert item.popitem()[1] == 0

    @authors("max42")
    def test_read_all_types_strict(self):
        query = (
            "select YPathInt64Strict(v, '/i64') as i64, YPathUInt64Strict(v, '/ui64') as ui64, "
            "YPathDoubleStrict(v, '/dbl') as dbl, YPathBooleanStrict(v, '/bool') as bool, "
            "YPathStringStrict(v, '/str') as str, YPathArrayInt64Strict(v, '/arr_i64') as arr_i64, "
            "YPathArrayUInt64Strict(v, '/arr_ui64') as arr_ui64, YPathArrayDoubleStrict(v, '/arr_dbl') as arr_dbl, "
            "YPathArrayBooleanStrict(v, '/arr_bool') as arr_bool from \"//tmp/t\" where i = 0"
        )
        with Clique(1) as clique:
            result = clique.make_query(query)
        assert result == [
            {
                "i64": -42,
                "ui64": 23,
                "bool": True,
                "dbl": 3.14,
                "str": "xyz",
                "arr_i64": [-1, 0, 1],
                "arr_ui64": [1, 1, 2, 3, 5],
                "arr_dbl": [-1.1, 2.71],
                "arr_bool": [False, True, False],
            }
        ]

    @authors("max42")
    def test_read_all_types_non_strict(self):
        query = (
            "select YPathInt64(v, '/i64') as i64, YPathUInt64(v, '/ui64') as ui64, "
            "YPathDouble(v, '/dbl') as dbl, YPathBoolean(v, '/bool') as bool, "
            "YPathString(v, '/str') as str, YPathArrayInt64(v, '/arr_i64') as arr_i64, "
            "YPathArrayUInt64(v, '/arr_ui64') as arr_ui64, YPathArrayDouble(v, '/arr_dbl') as arr_dbl, "
            "YPathArrayBoolean(v, '/arr_bool') as arr_bool from \"//tmp/t\" where i = 3"
        )
        with Clique(1) as clique:
            result = clique.make_query(query)
        assert result == [
            {
                "i64": 0,
                "ui64": 0,
                "bool": False,
                "dbl": 0.0,
                "str": "",
                "arr_i64": [],
                "arr_ui64": [],
                "arr_dbl": [],
                "arr_bool": [],
            }
        ]

    @authors("max42")
    def test_const_args(self):
        with Clique(1) as clique:
            result = clique.make_query("select YPathString('{a=[1;2;{b=xyz}]}', '/a/2/b') as str")
        assert result == [{"str": "xyz"}]

    @authors("max42", "dakovalkov")
    def test_nulls(self):
        with Clique(1) as clique:
            result = clique.make_query(
                "select YPathString(NULL, NULL) as a, YPathString(NULL, '/x') as b, " "YPathString('{a=1}', NULL) as c"
            )
            assert result == [{"a": None, "b": None, "c": None}]

            result = clique.make_query('select YPathInt64(v, key) from "//tmp/t"')
            for i, item in enumerate(result):
                if i == 0:
                    assert item.popitem()[1] == -1
                elif i == 1:
                    assert item.popitem()[1] == 0
                else:
                    assert item.popitem()[1] is None

    # CHYT-157.
    @authors("max42")
    def test_int64_as_any(self):
        create("table", "//tmp/s1", attributes={"schema": [{"name": "a", "type": "int64"}]})
        create("table", "//tmp/s2", attributes={"schema": [{"name": "a", "type": "any"}]})
        lst = [{"a": -(2 ** 63)}, {"a": -42}, {"a": 123456789123456789}, {"a": 2 ** 63 - 1}]
        write_table("//tmp/s1", lst)
        merge(in_="//tmp/s1", out="//tmp/s2")

        with Clique(1) as clique:
            result = clique.make_query("select YPathInt64(a, '') as i from \"//tmp/s2\" order by i")
            assert result == [{"i": row["a"]} for row in lst]

    @authors("dakovalkov")
    def test_raw_yson_as_any(self):
        object = {"a": [1, 2, {"b": "xxx"}]}
        create("table", "//tmp/s1", attributes={"schema": [{"name": "a", "type": "any"}]})
        write_table("//tmp/s1", {"a": object})

        with Clique(1) as clique:
            result = clique.make_query("select YPathRaw(a, '') as i from \"//tmp/s1\"")
            assert result == [{"i": yson.dumps(object, "binary")}]
            result = clique.make_query("select YPathRawStrict(a, '/a') as i from \"//tmp/s1\"")
            assert result == [{"i": yson.dumps(object["a"], "binary")}]
            result = clique.make_query("select YPathRaw(a, '', 'text') as i from \"//tmp/s1\"")
            assert result == [{"i": yson.dumps(object, "text")}]
            result = clique.make_query("select YPathRaw(a, '/b') as i from \"//tmp/s1\"")
            assert result == [{"i": None}]
            with raises_yt_error(QueryFailedError):
                clique.make_query("select YPathRawStrict(a, '/b') as i from \"//tmp/s1\"")

    @authors("dakovalkov")
    def test_ypath_extract(self):
        object = {"a": [[1, 2, 3], [4, 5], [6, 7, 8, 9]]}
        create("table", "//tmp/s1", attributes={"schema": [{"name": "a", "type": "any"}]})
        write_table("//tmp/s1", {"a": object})

        with Clique(1) as clique:
            result = clique.make_query("select YPathExtract(a, '/a/1/1', 'UInt64') as i from \"//tmp/s1\"")
            assert result == [{"i": object["a"][1][1]}]
            result = clique.make_query("select YPathExtract(a, '/a/2', 'Array(UInt64)') as i from \"//tmp/s1\"")
            assert result == [{"i": object["a"][2]}]
            result = clique.make_query("select YPathExtract(a, '/a', 'Array(Array(UInt64))') as i from \"//tmp/s1\"")
            assert result == [{"i": object["a"]}]

    @authors("max42")
    def test_rich_types_v3_are_strings(self):
        create(
            "table",
            "//tmp/t2",
            attributes={"schema": [{"name": "a", "type_v3": {"type_name": "list", "item": "int64"}}]},
        )
        lst = [42, 23]
        write_table("//tmp/t2", [{"a": lst}])

        with Clique(1) as clique:
            result = clique.make_query("describe `//tmp/t2`")
            assert len(result) == 1
            assert result[0]["type"] == "Array(Int64)"

            assert clique.make_query("select a from `//tmp/t2`")[0] == {"a": [42, 23]}

    # CHYT-370.
    @authors("max42")
    def test_const_arguments(self):
        with Clique(1) as clique:
            assert clique.make_query("select YPathRaw('[foo; bar]', '', 'text') as a")[0] == {"a": '["foo";"bar";]'}
            with raises_yt_error(QueryFailedError):
                clique.make_query("select YPathRaw('[invalid_yson', '', 'text') as a")
                clique.make_query("select YPathRawStrict('[invalid_yson', '', 'text') as a")

    @authors("max42")
    def test_different_format_per_row(self):
        with Clique(1) as clique:
            assert clique.make_query("select YPathRaw(v, '', fmt) as a from `//tmp/t[#1:#5]`") == [
                {"a": yson.dumps(row["v"], row["fmt"])} for row in read_table("//tmp/t[#1:#5]")
            ]


class TestYsonFunctionsCompositeConversionEnabled(BaseTestYsonFunctions):
    ENABLE_COMPOSITE_CONVERSION = True


class TestYsonFunctionsCompositeConversionDisabled(BaseTestYsonFunctions):
    ENABLE_COMPOSITE_CONVERSION = False
