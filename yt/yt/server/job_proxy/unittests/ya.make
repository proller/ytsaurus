GTEST(unittester-job-proxy)

INCLUDE(${ARCADIA_ROOT}/yt/ya_cpp.make.inc)

ALLOCATOR(TCMALLOC)

SRCS(
    asan_warning_filter_ut.cpp
    stderr_writer_ut.cpp
)

INCLUDE(${ARCADIA_ROOT}/yt/opensource.inc)

PEERDIR(
    yt/yt/server/job_proxy
)

SIZE(MEDIUM)

END()
