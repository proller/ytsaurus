import six
import sys
import hashlib
import base64


class Result(object):
    pass


def lazy(func):
    result = Result()

    def wrapper(*args, **kwargs):
        try:
            return result._result
        except AttributeError:
            result._result = func(*args, **kwargs)

        return result._result

    return wrapper


def pathid(path):
    return six.ensure_str(base64.b32encode(hashlib.md5(six.ensure_binary(path)).digest()).lower().strip(b'='))


def listid(items):
    return pathid(str(sorted(items)))


def sort_uniq(items):
    return sorted(set(items))


def stripext(fname):
    return fname[: fname.rfind('.')]


def tobuilddir(fname):
    if not fname:
        return '$B'
    if fname.startswith('$S'):
        return fname.replace('$S', '$B', 1)
    else:
        return fname


def before(s, ss):
    p = s.find(ss)

    if p == -1:
        return s

    return s[:p]


def sort_by_keywords(keywords, args):
    flat = []
    res = {}

    cur_key = None
    limit = -1
    for arg in args:
        if arg in keywords:
            limit = keywords[arg]
            if limit == 0:
                res[arg] = True
                cur_key = None
                limit = -1
            else:
                cur_key = arg
            continue
        if limit == 0:
            cur_key = None
            limit = -1
        if cur_key:
            if cur_key in res:
                res[cur_key].append(arg)
            else:
                res[cur_key] = [arg]
            limit -= 1
        else:
            flat.append(arg)
    return (flat, res)


def get_norm_unit_path(unit, extra=None):
    path = strip_roots(unit.path())
    if extra:
        return '{}/{}'.format(path, extra)
    return path


def resolve_common_const(path):
    if path.startswith('${ARCADIA_ROOT}'):
        return path.replace('${ARCADIA_ROOT}', '$S', 1)
    if path.startswith('${ARCADIA_BUILD_ROOT}'):
        return path.replace('${ARCADIA_BUILD_ROOT}', '$B', 1)
    return path


def resolve_to_abs_path(path, source_root, build_root):
    if path.startswith('$S') and source_root is not None:
        return path.replace('$S', source_root, 1)
    if path.startswith('$B') and build_root is not None:
        return path.replace('$B', build_root, 1)
    return path


def resolve_to_ymake_path(path):
    return resolve_to_abs_path(path, '${ARCADIA_ROOT}', '${ARCADIA_BUILD_ROOT}')


def get(fun, num):
    return fun()[num][0]


def make_tuples(arg_list):
    def tpl():
        for x in arg_list:
            yield (x, [])

    return list(tpl())


def resolve_includes(unit, src, paths):
    return unit.resolve_include([src] + paths) if paths else []


def rootrel_arc_src(src, unit):
    if src.startswith('${ARCADIA_ROOT}/'):
        return src[16:]

    if src.startswith('${ARCADIA_BUILD_ROOT}/'):
        return src[22:]

    elif src.startswith('${CURDIR}/'):
        return unit.path()[3:] + '/' + src[10:]

    else:
        resolved = unit.resolve_arc_path(src)

        if resolved.startswith('$S/'):
            return resolved[3:]

        return src  # leave as is


def skip_build_root(x):
    if x.startswith('${ARCADIA_BUILD_ROOT}'):
        return x[len('${ARCADIA_BUILD_ROOT}') :].lstrip('/')

    return x


def get_interpreter_path():
    interpreter_path = [sys.executable]
    if 'ymake' in interpreter_path[0]:
        interpreter_path.append('--python')
    return interpreter_path


def filter_out_by_keyword(test_data, keyword):
    def _iterate():
        i = 0
        while i < len(test_data):
            if test_data[i] == keyword:
                i += 2
            else:
                yield test_data[i]
                i += 1

    return list(_iterate())


def strip_roots(path):
    for prefix in ["$B/", "$S/"]:
        if path.startswith(prefix):
            return path[len(prefix) :]
    return path


def to_yesno(x):
    return "yes" if x else "no"


def get_no_lint_value(unit):
    import ymake

    supported_no_lint_values = ('none', 'none_internal', 'ktlint')
    no_lint_value = unit.get('_NO_LINT_VALUE')
    if no_lint_value and no_lint_value not in supported_no_lint_values:
        ymake.report_configure_error('Unsupported value for NO_LINT macro: {}'.format(no_lint_value))
    return no_lint_value
