# Generated by devtools/yamaker (pypi).

PY2_LIBRARY()

VERSION(2.0)

LICENSE(MIT)

PEERDIR(
    contrib/deprecated/python/backports.functools-lru-cache
    contrib/python/more-itertools
)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    jaraco/functools.py
)

RESOURCE_FILES(
    PREFIX contrib/python/jaraco.functools/py2/
    .dist-info/METADATA
    .dist-info/top_level.txt
)

END()