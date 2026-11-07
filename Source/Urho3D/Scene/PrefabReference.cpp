//
// Copyright (c) 2022-2022 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include <Urho3D/Precompiled.h>

#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/ResourceEvents.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/PrefabReader.h>
#include <Urho3D/Scene/PrefabReference.h>
#include <Urho3D/Scene/PrefabWriter.h>

namespace Urho3D
{

PrefabReference::PrefabReference(Context* context)
    : BaseClassName(context)
    , prefabRef_(PrefabResource::GetTypeStatic())
{
}

PrefabReference::~PrefabReference()
{
    RemoveInstance();
}

void PrefabReference::RegisterObject(Context* context)
{
    context->AddFactoryReflection<PrefabReference>(Category_Scene);

    URHO3D_ACTION_STATIC_LABEL("Inline", InlineConservative, "Convert prefab reference to nodes and components");
    URHO3D_ACTION_STATIC_LABEL("Inline+", InlineAggressive, "Same as Inline. Also converts all temporary objects to persistent");
    URHO3D_ACTION_STATIC_LABEL("Commit", CommitChanges, "Commit changes in this instance to the prefab resource");

    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Prefab", GetPrefabAttr, SetPrefabAttr, ResourceRef, ResourceRef(PrefabResource::GetTypeStatic()), AM_DEFAULT)
        .SetScopeHint(AttributeScopeHint::Node);
    URHO3D_ATTRIBUTE_EX("Path", ea::string, path_, MarkPrefabDirty, EMPTY_STRING, AM_DEFAULT)
        .SetScopeHint(AttributeScopeHint::Node);
}

void PrefabReference::ApplyAttributes()
{
    if (prefabDirty_)
    {
        prefabDirty_ = false;
        CreateInstance(true);
    }
}

const NodePrefab& PrefabReference::GetNodePrefab() const
{
    if (node_ && prefab_)
        return prefab_->GetNodePrefabSlice(path_);
    return NodePrefab::Empty;
}

bool PrefabReference::IsInstanceMatching(const Node* node, const NodePrefab& nodePrefab, bool temporaryOnly) const
{
    if (!AreComponentsMatching(node, nodePrefab.GetComponents(), temporaryOnly))
        return false;

    if (!AreChildrenMatching(node, nodePrefab.GetChildren(), temporaryOnly))
        return false;

    return true;
}

bool PrefabReference::AreComponentsMatching(
    const Node* node, const ea::vector<SerializablePrefab>& componentPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (const Component* component : node->GetComponents())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= componentPrefabs.size())
            return true;

        if (temporaryOnly && !component->IsTemporary())
            continue;

        if (component->GetType() != componentPrefabs[index].GetTypeNameHash())
            return false;

        ++index;
    }

    return index >= componentPrefabs.size();
}

bool PrefabReference::AreChildrenMatching(const Node* node, const ea::vector<NodePrefab>& childPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (const Node* child : node->GetChildren())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= childPrefabs.size())
            return true;

        if (temporaryOnly && !child->IsTemporary())
            continue;

        if (!IsInstanceMatching(child, childPrefabs[index], false /* ignore temporary flag */))
            return false;

        ++index;
    }

    return index >= childPrefabs.size();
}

void PrefabReference::ExportInstance(Node* node, const NodePrefab& nodePrefab, bool temporaryOnly) const
{
    ExportComponents(node, nodePrefab.GetComponents(), temporaryOnly);
    ExportChildren(node, nodePrefab.GetChildren(), temporaryOnly);
}

void PrefabReference::ExportComponents(
    Node* node, const ea::vector<SerializablePrefab>& componentPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (Component* component : node->GetComponents())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= componentPrefabs.size())
            return;

        if (temporaryOnly && !component->IsTemporary())
            continue;

        componentPrefabs[index].Export(component, PrefabLoadFlag::KeepTemporaryState);

        ++index;
    }
}

void PrefabReference::ExportChildren(Node* node, const ea::vector<NodePrefab>& childPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (Node* child : node->GetChildren())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= childPrefabs.size())
            return;

        if (temporaryOnly && !child->IsTemporary())
            continue;

        ExportInstance(child, childPrefabs[index], false /* ignore temporary flag */);

        ++index;
    }
}

bool PrefabReference::TryCreateInplace()
{
    const NodePrefab& nodePrefab = GetNodePrefab();

    if (nodePrefab.IsEmpty())
        return false;

    if (!IsInstanceMatching(node_, nodePrefab, true /* temporary only */))
        return false;

    ExportInstance(node_, nodePrefab, true /* temporary only */);
    return true;
}

void PrefabReference::RemoveTemporaryComponents(Node* node) const
{
    const auto& components = node->GetComponents();
    const int numComponents = static_cast<int>(node->GetNumComponents());

    for (int i = numComponents - 1; i >= 0; --i)
    {
        Component* component = components[i];
        if (component->IsTemporary())
        {
            if (component != this)
                node->RemoveComponent(component);
            else
            {
                URHO3D_LOGWARNING("PrefabReference component should not be temporary");
                component->SetTemporary(false);
            }
        }
    }
}

void PrefabReference::RemoveTemporaryChildren(Node* node) const
{
    const auto& children = node->GetChildren();
    const int numChildren = static_cast<int>(node->GetNumChildren());

    for (int i = numChildren - 1; i >= 0; --i)
    {
        Node* child = children[i];
        if (child->IsTemporary())
            node->RemoveChild(child);
    }
}

void PrefabReference::RemoveInstance()
{
    if (Node* instanceNode = instanceNode_)
    {
        RemoveTemporaryComponents(instanceNode);
        RemoveTemporaryChildren(instanceNode);
    }

    instanceNode_ = nullptr;
}

void PrefabReference::InstantiatePrefab(const NodePrefab& nodePrefab, PrefabInstanceFlags instanceFlags)
{
    const auto flags = PrefabLoadFlag::KeepExistingComponents | PrefabLoadFlag::KeepExistingChildren
        | PrefabLoadFlag::LoadAsTemporary | PrefabLoadFlag::IgnoreRootAttributes;
    PrefabReaderFromMemory reader{nodePrefab};
    node_->Load(reader, flags);

    if (instanceFlags != PrefabInstanceFlag::None)
    {
        static const ea::unordered_map<ea::string, PrefabInstanceFlag> attributeToFlag = {
            {"Scale", PrefabInstanceFlag::UpdateScale},
            {"Position", PrefabInstanceFlag::UpdatePosition},
            {"Rotation", PrefabInstanceFlag::UpdateRotation},
            {"Tags", PrefabInstanceFlag::UpdateTags},
            {"Name", PrefabInstanceFlag::UpdateName},
            {"Variables", PrefabInstanceFlag::UpdateVariables},
        };

        for (const AttributePrefab& attribute : nodePrefab.GetNode().GetAttributes())
        {
            const auto iter = attributeToFlag.find(attribute.GetName());
            if (iter == attributeToFlag.end())
                continue;

            const auto& [name, flag] = *iter;
            if (instanceFlags.Test(flag))
                node_->SetAttribute(name, attribute.GetValue());
        }
    }
}

void PrefabReference::CreateInstance(bool tryInplace, PrefabInstanceFlags instanceFlags)
{
    // Remove existing instance if moved to another node
    if (instanceNode_ && instanceNode_ != node_)
        RemoveInstance();

    // Cannot spawn instance without node
    if (!node_)
        return;

    const NodePrefab& nodePrefab = GetNodePrefab();
    instanceNode_ = node_;
    numInstanceComponents_ = nodePrefab.GetComponents().size();
    numInstanceChildren_ = nodePrefab.GetChildren().size();

    // Try to create inplace first
    if (tryInplace && TryCreateInplace())
        return;

    RemoveTemporaryComponents(node_);
    RemoveTemporaryChildren(node_);
    InstantiatePrefab(nodePrefab, instanceFlags);
}

void PrefabReference::SetPrefab(PrefabResource* prefab, ea::string_view path, bool createInstance, PrefabInstanceFlags instanceFlags)
{
    if (prefab == prefab_ && path == path_)
    {
        return;
    }

    if (prefab_)
    {
        UnsubscribeFromEvent(prefab_, E_RELOADFINISHED);
    }

    prefab_ = prefab;
    path_ = path;

    if (prefab_)
    {
        SubscribeToEvent(prefab_, E_RELOADFINISHED, [this] { CreateInstance(); });
        prefabRef_ = GetResourceRef(prefab_, PrefabResource::GetTypeStatic());
    }
    else
    {
        prefabRef_ = ResourceRef(PrefabResource::GetTypeStatic());
    }

    if (createInstance)
        CreateInstance(false, instanceFlags);
}

void PrefabReference::SetPrefabAttr(const ResourceRef& prefab)
{
    if (prefab.name_.empty())
    {
        SetPrefab(nullptr, path_, false);
    }
    else
    {
        auto cache = context_->GetSubsystem<ResourceCache>();
        SetPrefab(cache->GetResource<PrefabResource>(prefab.name_), path_, false);
    }

    prefabRef_ = prefab;
    MarkPrefabDirty();
}

void PrefabReference::SetPath(ea::string_view path)
{
    if (path_ != path)
        SetPrefab(prefab_, path, true);
}

void PrefabReference::Inline(PrefabInlineFlags flags)
{
    if (!node_)
        return;

    // Hold self until the end of the function
    Node* node = node_;
    instanceNode_ = nullptr;

    SharedPtr<PrefabReference> self{this};
    Remove();

    // Some temporary components and children are spawned by the prefab itself.
    // Other temporary components and children may be spawned by the components in prefab.
    if (flags.Test(PrefabInlineFlag::KeepOtherTemporary))
    {
        unsigned temporaryComponentIndex = 0;
        for (const auto& component : node->GetComponents())
        {
            // Break when first N temporary components are converted
            if (temporaryComponentIndex >= numInstanceComponents_)
                break;

            // Skip persistent components
            if (!component->IsTemporary())
                continue;

            component->SetTemporary(false);
            ++temporaryComponentIndex;
        }

        unsigned temporaryChildIndex = 0;
        for (const auto& child : node->GetChildren())
        {
            // Break when first N temporary children are converted
            if (temporaryChildIndex >= numInstanceChildren_)
                break;

            // Skip persistent children
            if (!child->IsTemporary())
                continue;

            child->SetTemporary(false);
            ++temporaryChildIndex;
        }
    }
    else
    {
        for (const auto& component : node->GetComponents())
            component->SetTemporary(false);
        for (const auto& child : node->GetChildren())
            child->SetTemporary(false);
    }
}

/// Remove materialized content that belongs to nested prefab instances.
/// A node with a PrefabReference component is a stub whose subtree is owned by the other prefab
/// file. Saving that subtree with SaveTemporary would flatten the nested prefab into this one.
/// For such stubs only the PrefabReference itself is kept: user-added components and children
/// are overrides that must be committed to the nested prefab through its own Commit button.
/// Must be called while NodePrefab entries are still aligned with the in-memory tree (i.e.
/// before any pruning of the root node).
void PruneNestedInstances(Node* node, NodePrefab& nodePrefab)
{
    const bool isNested = node->GetComponent<PrefabReference>() != nullptr;
    if (isNested)
    {
        // Keep only the PrefabReference itself: everything else belongs to the nested prefab.
        auto& prefabComponents = nodePrefab.GetMutableComponents();
        ea::erase_if(prefabComponents, [](const SerializablePrefab& component) //
            { return component.GetTypeNameHash() != PrefabReference::GetTypeStatic(); });
        nodePrefab.GetMutableChildren().clear();
        return;
    }

    // Recurse while the entries are still aligned with the in-memory tree.
    const auto& children = node->GetChildren();
    auto& prefabChildren = nodePrefab.GetMutableChildren();
    for (unsigned i = 0; i < prefabChildren.size(); ++i)
        PruneNestedInstances(children[i], prefabChildren[i]);
}

bool HasUncommittedNestedOverrides(Node* node)
{
    for (Node* child : node->GetChildren())
    {
        if (child->GetComponent<PrefabReference>() != nullptr)
        {
            // Nested stub: any persistent component other than PrefabReference or any persistent
            // child is an uncommitted override of the nested prefab.
            for (Component* component : child->GetComponents())
            {
                if (!component->IsTemporary() && component->GetType() != PrefabReference::GetTypeStatic())
                    return true;
            }
            for (Node* grandChild : child->GetChildren())
            {
                if (!grandChild->IsTemporary())
                    return true;
            }
        }
        if (HasUncommittedNestedOverrides(child))
            return true;
    }
    return false;
}

void GeneratePrefabMaterialized(Node* node, NodePrefab& prefab)
{
    const PrefabSaveFlags flags =
        PrefabSaveFlag::EnumsAsStrings | PrefabSaveFlag::Prefab | PrefabSaveFlag::SaveTemporary;
    PrefabWriterToMemory writer{prefab, flags};
    node->Save(writer);

    // Keep nested prefab instances as references instead of flattening them into the file.
    const auto& children = node->GetChildren();
    auto& prefabChildren = prefab.GetMutableChildren();
    for (unsigned i = 0; i < prefabChildren.size(); ++i)
        PruneNestedInstances(children[i], prefabChildren[i]);
}


void PrefabReference::CommitChanges()
{
    if (!path_.empty())
    {
        URHO3D_LOGERROR("Cannot commit changes to a prefab slice");
        return;
    }

    if (!node_ || !prefab_)
        return;

    const NodePrefab& originalNodePrefab = GetNodePrefab();

    // Reject the commit if nested instances have uncommitted overrides: clearing the instance
    // below would silently destroy them. They must be committed to the nested prefab first.
    if (HasUncommittedNestedOverrides(node_))
    {
        URHO3D_LOGERROR("Prefab: nested instances have uncommitted overrides. Commit the innermost prefab first.");
        return;
    }

    NodePrefab newNodePrefab;
    GeneratePrefabMaterialized(node_, newNodePrefab);

    // Don't change Node attributes, they are considered external
    newNodePrefab.GetMutableNode().GetMutableAttributes() = originalNodePrefab.GetNode().GetAttributes();

    // Drop the scene-side PrefabReference from the root: it belongs to the scene holding this
    // instance, not to the prefab (it would create a self-reference). Everything else added by
    // the user (persistent components and children) is an override and must be committed.
    auto& rootComponents = newNodePrefab.GetMutableComponents();
    ea::erase_if(rootComponents, [](const SerializablePrefab& component) //
        { return component.GetTypeNameHash() == PrefabReference::GetTypeStatic(); });

    // Skip the write entirely if nothing has changed. IDs are normalized on copies first, because
    // in-memory IDs differ from the IDs stored in the file.
    NodePrefab normalizedOriginal = originalNodePrefab;
    NodePrefab normalizedNew = newNodePrefab;
    normalizedOriginal.NormalizeIds(context_);
    normalizedNew.NormalizeIds(context_);
    if (normalizedOriginal == normalizedNew)
    {
        URHO3D_LOGINFO("Prefab: nothing to commit, no changes detected");
        return;
    }

    // Create and save resource
    auto newResource = MakeShared<PrefabResource>(context_);
    newResource->GetMutableScenePrefab() = prefab_->GetScenePrefab();
    newResource->GetMutableNodePrefab() = newNodePrefab;
    newResource->NormalizeIds();
    newResource->SaveFile(prefab_->GetAbsoluteFileName());

    // Remove all instantiated content and overrides, keeping only the stub itself (the node and
    // this PrefabReference component). The resource reload below rebuilds the instance from the
    // just-committed file, resolving all overrides back into the prefab and clearing the diff.
    const ea::vector<SharedPtr<Component>> components = node_->GetComponents();
    for (const SharedPtr<Component>& component : components)
    {
        if (component != this)
            node_->RemoveComponent(component);
    }
    node_->RemoveAllChildren();
    instanceNode_ = nullptr;

    // Reload the resource so that this and all other instances are rebuilt from the committed file.
    auto* cache = GetSubsystem<ResourceCache>();
    if (!cache || !cache->ReloadResource(prefab_))
        CreateInstance();
}

void PrefabReference::OnSetEnabled()
{
    if (!IsEnabledEffective())
        RemoveInstance();
    else if (instanceNode_ != node_)
        CreateInstance(true);
}

}
