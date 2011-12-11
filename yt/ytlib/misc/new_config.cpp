#include "stdafx.h"
#include "new_config.h"

#include "../ytree/ypath_detail.h"

namespace NYT {

using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

void TConfigBase::Load(NYTree::INode* node, const NYTree::TYPath& path)
{
    YASSERT(node != NULL);
    NYTree::IMapNode::TPtr mapNode;
    try {
        mapNode = node->AsMap();
    } catch(...) {
        ythrow yexception()
            << Sprintf("Configuration must be loaded from a map node (Path: %s)\n%s",
                ~path,
                ~CurrentExceptionMessage());
    }
    FOREACH (auto pair, Parameters) {
        auto name = pair.First();
        auto childPath = CombineYPaths(path, name);
        auto child = mapNode->FindChild(name); // can be NULL
        pair.Second()->Load(~child, childPath);
    }
}

void TConfigBase::Validate(const NYTree::TYPath& path) const
{
    FOREACH (auto pair, Parameters) {
        pair.Second()->Validate(path + "/" + pair.First());
    }
}

void TConfigBase::SetDefaults(const Stroka& path)
{
    DoSetDefaults(true, path);
}

void TConfigBase::DoSetDefaults(bool skipRequiredParameters, const NYTree::TYPath& path)
{
    FOREACH (auto pair, Parameters) {
        auto name = pair.First();
        auto childPath = CombineYPaths(path, name);
        pair.Second()->SetDefaults(skipRequiredParameters, childPath);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
