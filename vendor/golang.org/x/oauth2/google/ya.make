GO_LIBRARY()

LICENSE(BSD-3-Clause)

SRCS(
    appengine.go
    appengine_gen2_flex.go
    default.go
    doc.go
    error.go
    google.go
    jwt.go
    sdk.go
)

END()

RECURSE(
    downscope
    externalaccount
    internal
)
