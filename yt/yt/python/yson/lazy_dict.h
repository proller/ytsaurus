#pragma once

#include "object_builder.h"

#include <yt/yt/python/common/helpers.h>
#include <yt/yt/python/common/public.h>
#include <yt/yt/python/common/stream.h>

#include <yt/yt/core/yson/lexer_detail.h>

#include <yt/yt/core/ytree/convert.h>

#include <Extensions.hxx> // pycxx
#include <Objects.hxx> // pycxx

#include <Python.h>

namespace NYT::NYTree {

////////////////////////////////////////////////////////////////////////////////

struct TPyObjectHasher
{
    size_t operator()(const Py::Object& object) const;
};

struct TLazyDictValue
{
    TSharedRef Data;
    std::optional<Py::Object> Value;
};

class TLazyDict
{
public:
    typedef THashMap<Py::Object, TLazyDictValue, TPyObjectHasher> THashMapType;

    TLazyDict(bool alwaysCreateAttributes, const std::optional<TString>& encoding);

    PyObject* GetItem(const Py::Object& key);
    void SetItem(const Py::Object& key, const TSharedRef& value);
    void SetItem(const Py::Object& key, const Py::Object& value);
    bool HasItem(const Py::Object& key) const;
    void DeleteItem(const Py::Object& key);
    void Clear();
    size_t Length() const;
    THashMapType* GetUnderlyingHashMap();
    Py::Object GetConsumerParams();

private:
    THashMapType Data_;
    std::unique_ptr<NYTree::TPythonObjectBuilder> Consumer_;
    bool AlwaysCreateAttributes_;
    std::optional<TString> Encoding_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTree
