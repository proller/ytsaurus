from .helpers import check_schema_module_available, is_schema_module_available

from ..errors import YtError

from .. import skiff

import copy
import datetime
import types


try:
    import typing
except ImportError:
    import yt.packages.typing as typing


import yt.type_info as ti


try:
    import dataclasses
except ImportError:
    pass


if is_schema_module_available():
    try:
        from yt.packages.typing_extensions import Annotated, Protocol
    except ImportError:
        from typing_extensions import Annotated, Protocol
else:
    Protocol = object


if typing.TYPE_CHECKING:
    import typing
    from typing_extensions import dataclass_transform
else:
    if is_schema_module_available():
        try:
            from yt.packages.typing_extensions import dataclass_transform
        except ImportError:
            from typing_extensions import dataclass_transform
    else:
        def dataclass_transform():
            return lambda x: x


_T = typing.TypeVar('_T')


@dataclass_transform()
def yt_dataclass(cls):
    # type: (typing.Type[_T]) -> typing.Type[_T]
    """ Decorator for classes representing table rows and embedded structures.
    """
    check_schema_module_available()
    dataclass_cls = dataclasses.dataclass(cls)

    class with_slots(dataclass_cls):
        _YT_DATACLASS_MARKER = None
        _FIELDS = dataclasses.fields(dataclass_cls)
        __slots__ = [field.name for field in _FIELDS]

    with_slots.__name__ = cls.__name__
    with_slots.__qualname__ = cls.__qualname__
    with_slots.__module__ = cls.__module__
    return with_slots


def is_yt_dataclass(type_):
    check_schema_module_available()
    return hasattr(type_, "_YT_DATACLASS_MARKER")


class Annotation:
    def __init__(self, ti_type, to_yt_type=None, from_yt_type=None):
        check_schema_module_available()
        self._ti_type = ti_type
        self._to_yt_type = to_yt_type
        self._from_yt_type = from_yt_type

    def __getstate__(self):
        d = copy.deepcopy(self.__dict__)
        d["_ti_type"] = ti.serialize_yson(self._ti_type)
        return d

    def __setstate__(self, d):
        d = copy.deepcopy(d)
        d["_ti_type"] = ti.deserialize_yson(d["_ti_type"])
        self.__dict__ = d

    def __repr__(self):
        return "Annotation({})".format(self._ti_type)


def create_annotated_type(py_type, ti_type, to_yt_type=None, from_yt_type=None):
    """
    Create an alias of a python type `py_type` that will correspond to `ti_type`
    in table schemas.
    """
    check_schema_module_available()
    if not ti.is_valid_type(ti_type):
        raise TypeError("Expected ti_type to be a type_info type")
    if not _is_py_type_compatible_with_ti_type(py_type, ti_type) and (not to_yt_type or not from_yt_type):
        raise YtError('Python type {} is not compatible with type "{}" from annotation'
                      .format(py_type, ti_type))
    return Annotated[py_type, Annotation(ti_type, to_yt_type=to_yt_type, from_yt_type=from_yt_type)]


def _is_py_type_optional(py_type):
    return _is_py_type_optional_old_style(py_type) or _is_py_type_optional_new_style(py_type)


def _is_py_type_optional_old_style(py_type):
    args = getattr(py_type, "__args__", [])
    return getattr(py_type, "__module__", None) == "typing" and \
        getattr(py_type, "__origin__", None) == typing.Union and \
        len(args) == 2 and \
        args[-1] == type(None)  # noqa


def _is_py_type_optional_new_style(py_type):
    if getattr(py_type, "__module__", None) == "types" and getattr(type(py_type), "__name__", None) == "UnionType":
        args = typing.get_args(py_type)
        return len(args) == 2 and types.NoneType in args
    return False


def _get_integer_info():
    if hasattr(_get_integer_info, "_info"):
        return _get_integer_info._info
    check_schema_module_available()
    unsigned = {
        ti.Uint8, ti.Uint16, ti.Uint32, ti.Uint64,
    }
    signed = {
        ti.Int8, ti.Int16, ti.Int32, ti.Int64,
    }
    bit_width = {
        ti.Int8: 8,
        ti.Uint8: 8,
        ti.Int16: 16,
        ti.Uint16: 16,
        ti.Int32: 32,
        ti.Uint32: 32,
        ti.Int64: 64,
        ti.Uint64: 64,
    }
    _get_integer_info._info = {
        "unsigned": unsigned,
        "signed": signed,
        "all": signed | unsigned,
        "bit_width": bit_width,
    }
    return _get_integer_info._info


def _get_time_types():
    if hasattr(_get_time_types, "_info"):
        return _get_time_types._info
    check_schema_module_available()
    _get_time_types._info = {
        ti.Date,
        ti.Datetime,
        ti.Timestamp,
        ti.Interval,
    }
    return _get_time_types._info


def _get_py_time_types():
    if hasattr(_get_py_time_types, "_info"):
        return _get_py_time_types._info
    check_schema_module_available()
    _get_py_time_types._info = {
        datetime.date,
        datetime.datetime,
        datetime.timedelta,
    }
    return _get_py_time_types._info


def _is_py_type_compatible_with_ti_type(py_type, ti_type):
    check_schema_module_available()
    if py_type is int:
        return ti_type in _get_integer_info()["all"] or ti_type in _get_time_types()
    elif py_type is str:
        return ti_type in (ti.Utf8, ti.String)
    elif py_type is bytes:
        return ti_type in (ti.String, ti.Yson, ti.Utf8)
    elif py_type is bool:
        return ti_type == ti.Bool
    elif py_type is float:
        return ti_type in (ti.Float, ti.Double)
    elif py_type is datetime.date:
        return ti_type == ti.Date
    elif py_type is datetime.datetime:
        return ti_type in (ti.Datetime, ti.Timestamp)
    elif py_type is datetime.timedelta:
        return ti_type == ti.Interval
    else:
        assert False, "Unsupported python type {}".format(py_type)


def _check_ti_types_compatible(src_type, dst_type, field_path):
    def raise_incompatible(message=None):
        if message is None:
            message = 'source type "{}" is incompatible with destination type "{}"'
        raise YtError(('Type mismatch for field "{}": ' + message).format(field_path, src_type, dst_type))

    integer_info = _get_integer_info()
    if src_type in integer_info["all"]:
        if dst_type not in integer_info["all"]:
            raise_incompatible()
        src_signed = src_type in integer_info["signed"]
        dst_signed = dst_type in integer_info["signed"]
        if src_signed != dst_signed:
            raise_incompatible('signedness of source type "{}" and destination type "{}" differ')
        if integer_info["bit_width"][src_type] > integer_info["bit_width"][dst_type]:
            raise_incompatible('source type "{}" is larger than destination type "{}"')
        return
    else:
        if src_type != dst_type:
            raise_incompatible()


if Protocol is not object:
    class _YtDataclassProtocol(Protocol):
        _YT_DATACLASS_MARKER = None
else:
    _YtDataclassProtocol = None


YtDataclassType = typing.TypeVar('YtDataclassType', bound=_YtDataclassProtocol)


class OutputRow(typing.Generic[YtDataclassType]):
    """
    Wrapper for job output row.
    """

    __slots__ = ("_row", "_table_index")

    def __init__(self, row, table_index=0):
        self._row = row
        self._table_index = table_index


class RowIteratorProtocol(typing.Iterable[YtDataclassType]):
    def with_context():
        pass


class ContextProtocol(Protocol):
    def get_table_index():
        pass

    def get_row_index():
        pass

    def get_range_index():
        pass


if is_schema_module_available():
    Int8 = create_annotated_type(int, ti.Int8)
    Int16 = create_annotated_type(int, ti.Int16)
    Int32 = create_annotated_type(int, ti.Int32)
    Int64 = create_annotated_type(int, ti.Int64)

    Uint8 = create_annotated_type(int, ti.Uint8)
    Uint16 = create_annotated_type(int, ti.Uint16)
    Uint32 = create_annotated_type(int, ti.Uint32)
    Uint64 = create_annotated_type(int, ti.Uint64)

    Float = create_annotated_type(float, ti.Float)
    Double = create_annotated_type(float, ti.Double)

    Date = create_annotated_type(int, ti.Date)
    Datetime = create_annotated_type(int, ti.Datetime)
    Timestamp = create_annotated_type(int, ti.Timestamp)
    Interval = create_annotated_type(int, ti.Interval)

    YsonBytes = create_annotated_type(bytes, ti.Yson)

    OtherColumns = skiff.SkiffOtherColumns

    class FormattedPyDatetime:
        """
        Generic type for annotating yt_dataclass fields that parses a string column with date and/or time
        in datetime.datetime using specified pattern (generic parameter).

        Example:
        ```
            @yt.yt_dataclass
            class Row:
                date: FormattedPyDatetime["%Y-%m-%d"]

            yt.write_table_structured(table, Row, [Row(date=datetime.datetime(year=2010, month=10, day=29))])
            list(yt.read_table(table))
            > [{'date': '2010-10-29'}]
        ```
        """
        def __class_getitem__(cls, format):
            if type(format) is not str:
                raise TypeError("The datetime format must be a string")

            def to_yt_type(datetime_):
                return datetime_.strftime(format).encode("UTF-8")

            def from_yt_type(byte_string):
                datetime_ = datetime.datetime.strptime(byte_string.decode("UTF-8"), format)
                return datetime_

            return create_annotated_type(datetime.datetime, ti.String, to_yt_type, from_yt_type)

        @classmethod
        def _name(cls):
            return "{module}.{classname}".format(module=cls.__module__, classname=cls.__qualname__)

        def __new__(cls, *args, **kwargs):
            raise TypeError("Type {} cannot be instantiated".format(FormattedPyDatetime._name()))

        def __init_subclass__(cls, *args, **kwargs):
            raise TypeError("{} cannot be subclassed".format(FormattedPyDatetime._name()))
