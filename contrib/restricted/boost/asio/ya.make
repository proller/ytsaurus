# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(
    BSL-1.0 AND
    Public-Domain
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.69.0)

ORIGINAL_SOURCE(https://github.com/boostorg/asio/archive/boost-1.69.0.tar.gz)

PEERDIR(
    contrib/libs/openssl
    contrib/restricted/boost/array
    contrib/restricted/boost/assert
    contrib/restricted/boost/bind
    contrib/restricted/boost/chrono
    contrib/restricted/boost/config
    contrib/restricted/boost/core
    contrib/restricted/boost/coroutine
    contrib/restricted/boost/date_time
    contrib/restricted/boost/function
    contrib/restricted/boost/regex
    contrib/restricted/boost/smart_ptr
    contrib/restricted/boost/system
    contrib/restricted/boost/throw_exception
    contrib/restricted/boost/type_traits
    contrib/restricted/boost/utility
)

ADDINCL(
    GLOBAL contrib/restricted/boost/asio/include
)

END()
