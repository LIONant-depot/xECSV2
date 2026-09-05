#ifndef XECS_COMPONENT_OTHERS_H
#define XECS_COMPONENT_OTHERS_H
#pragma once

namespace xecs::component
{
    //
    // Reference Count Data Component
    //
    struct ref_count
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName       = "ReferenceCount"
        };

        inline xerr Serialize( xecs::serializer::stream&, bool ) noexcept;

        int m_Value{ 1 };
    };

    //
    // Exclusive tag that tells Archetypes to treat share components as data components
    //
    struct share_as_data_exclusive_tag
    {
        constexpr static auto typedef_v = xecs::component::type::exclusive_tag
        {
            .m_pName = "ExclusiveShareAsData"
        };
    };

    //
    // Query Share Filter component
    //
    struct share_filter
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName            = "ShareFilter"
        ,   .m_SerializeMode    = xecs::component::type::serialize_mode::DONT_SERIALIZE
        };

        struct entry
        {
            xecs::archetype::instance*          m_pArchetype;
            std::vector<xecs::pool::family*>    m_lFamilies;
        };

        std::vector<entry>  m_lEntries;
    };

    //
    // Parent component for hierarchical entities, used only for entities that have parents (not root entities).
    //
    struct parent
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName                = "Parent"
        };

        inline xerr Serialize( xecs::serializer::stream&, bool ) noexcept;
        inline void       ReportReferences(std::vector<xecs::component::entity*>& ) noexcept;

        xecs::component::entity m_Value;

        XPROPERTY_DEF
        ( "Parent", parent
        , obj_member<"Parent", &parent::m_Value>
        )
    };

    //
    // Parent component for hierarchical entities, this components is used by entities that can have children.
    //
    struct children
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName                = "Children"
        };

        inline static xerr FullSerialize( xecs::serializer::stream&, bool, children*, int& ) noexcept;
        inline void              ReportReferences(std::vector<xecs::component::entity*>& ) noexcept;

        std::vector<xecs::component::entity> m_List;

        XPROPERTY_DEF
        ( "Children", children
        , obj_member<"Children", &children::m_List>
        )
    };

    //
    // General-purpose "points at another entity" component - unlike parent/children (which encode a
    // specific hierarchy relationship and use ReportReferences/BY_FUNCTION), this is a plain reflected
    // field with no hand-written Serialize/ReportReferences at all: m_ReferenceMode stays AUTO, which
    // self-detects BY_PROPERTIES since m_Target is xproperty-reflected (see
    // xecs_entity_xproperty_bridge.h's var_type<entity> specialization, and
    // xecs_component_type_inline.h's references_mode_v/ScopeHasEntityReferenceProperty) - so save/load
    // reference remapping (xecs_scene_inline.h's ResolveReferenceForSave/RemapLoadedEntityReferences)
    // already works for this with zero new code there, same-scene or a declared parent-scene target
    // alike. The user-facing point of this component: it's the first (and, for now, only) way to
    // create a genuine cross-scene entity reference in the editor, which is what actually exercises a
    // scene's "Dependencies" (m_ParentScenes/m_ExternalRefTable) - without some component like this,
    // that machinery is fully built but never actually driven by anything a user can create.
    struct entity_reference
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName                = "EntityReference"
        };

        xecs::component::entity m_Target;

        XPROPERTY_DEF
        ( "EntityReference", entity_reference
        , obj_member<"Target", &entity_reference::m_Target>
        )
    };
}
XPROPERTY_REG(xecs::component::parent)
XPROPERTY_REG(xecs::component::children)
XPROPERTY_REG(xecs::component::entity_reference)

#endif
