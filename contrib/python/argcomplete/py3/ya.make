# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(3.2.2)

LICENSE(Apache-2.0)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    argcomplete/__init__.py
    argcomplete/_check_console_script.py
    argcomplete/_check_module.py
    argcomplete/completers.py
    argcomplete/exceptions.py
    argcomplete/finders.py
    argcomplete/io.py
    argcomplete/lexers.py
    argcomplete/packages/__init__.py
    argcomplete/packages/_argparse.py
    argcomplete/packages/_shlex.py
    argcomplete/shell_integration.py
)

RESOURCE_FILES(
    PREFIX contrib/python/argcomplete/py3/
    .dist-info/METADATA
    .dist-info/top_level.txt
    argcomplete/bash_completion.d/_python-argcomplete
    argcomplete/py.typed
)

END()
