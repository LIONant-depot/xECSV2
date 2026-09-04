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
}
XPROPERTY_REG(xecs::component::parent)
XPROPERTY_REG(xecs::component::children)

#endif
