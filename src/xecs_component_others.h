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
    // An entity with a parent is a CHILD: its Transform is relative to the parent (an entity without one is a root, and its Transform is the world pose).
    // The component also keeps what the engine derived from that every frame: the child's WORLD pose (position, rotation, scale; see the transform system of the engine). It is
    // never saved, never shown, and always valid (it is the pose of the last time the transform system ran). Anything that needs the world position of an entity that may be a
    // child asks the parent component when there is one, and the Transform when there is not (xlioncore::WorldOf does exactly that).
    //
    // m_Follow says what the child takes from its parent: the three position axes, the rotation and the scale. A channel that does not follow takes the child's own value as a
    // WORLD value (a shadow follows x and z and stays on the ground: it does not follow y). By default everything follows except the scale: in this engine the scale of an entity
    // is also its size, and a name tag over a player must not be squashed by the player.
    //
    struct parent
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName                = "Parent"
        };

        enum follow : std::uint8_t
        { FOLLOW_X         = 1 << 0
        , FOLLOW_Y         = 1 << 1
        , FOLLOW_Z         = 1 << 2
        , FOLLOW_ROTATION  = 1 << 3
        , FOLLOW_SCALE     = 1 << 4
        , FOLLOW_DEFAULT   = FOLLOW_X | FOLLOW_Y | FOLLOW_Z | FOLLOW_ROTATION
        };

        inline xerr Serialize( xecs::serializer::stream&, bool ) noexcept;
        inline void       ReportReferences(std::vector<xecs::component::entity*>& ) noexcept;

        xecs::component::entity m_Value;
        std::uint8_t            m_Follow = FOLLOW_DEFAULT;

        // Derived, not saved: the world pose of this entity as the transform system left it (x y z w for each; the position and the scale use xyz). Plain floats: this
        // library does not know the math of the engine.
        alignas(16) float       m_WorldPosition[4] = { 0, 0, 0, 0 };
        alignas(16) float       m_WorldRotation[4] = { 0, 0, 0, 1 };
        alignas(16) float       m_WorldScale   [4] = { 1, 1, 1, 0 };

        XPROPERTY_DEF
        ( "Parent", parent
        , obj_member<"Parent", &parent::m_Value>
        , obj_member<"FollowX",        +[](parent& O, bool bRead, bool& V){ if (bRead) V = (O.m_Follow & FOLLOW_X) != 0;        else O.m_Follow = static_cast<std::uint8_t>(V ? (O.m_Follow | FOLLOW_X)        : (O.m_Follow & ~FOLLOW_X));        }, member_help<"The child takes the x of its parent's position. Off: its own x is a world x.">>
        , obj_member<"FollowY",        +[](parent& O, bool bRead, bool& V){ if (bRead) V = (O.m_Follow & FOLLOW_Y) != 0;        else O.m_Follow = static_cast<std::uint8_t>(V ? (O.m_Follow | FOLLOW_Y)        : (O.m_Follow & ~FOLLOW_Y));        }, member_help<"The child takes the y of its parent's position. Off: its own y is a world y (a shadow stays on the ground).">>
        , obj_member<"FollowZ",        +[](parent& O, bool bRead, bool& V){ if (bRead) V = (O.m_Follow & FOLLOW_Z) != 0;        else O.m_Follow = static_cast<std::uint8_t>(V ? (O.m_Follow | FOLLOW_Z)        : (O.m_Follow & ~FOLLOW_Z));        }, member_help<"The child takes the z of its parent's position. Off: its own z is a world z.">>
        , obj_member<"FollowRotation", +[](parent& O, bool bRead, bool& V){ if (bRead) V = (O.m_Follow & FOLLOW_ROTATION) != 0; else O.m_Follow = static_cast<std::uint8_t>(V ? (O.m_Follow | FOLLOW_ROTATION) : (O.m_Follow & ~FOLLOW_ROTATION)); }, member_help<"The child turns with its parent, and its offset turns with it. Off: its own rotation is a world rotation.">>
        , obj_member<"FollowScale",    +[](parent& O, bool bRead, bool& V){ if (bRead) V = (O.m_Follow & FOLLOW_SCALE) != 0;    else O.m_Follow = static_cast<std::uint8_t>(V ? (O.m_Follow | FOLLOW_SCALE)    : (O.m_Follow & ~FOLLOW_SCALE));    }, member_help<"The child is scaled by its parent. Off (the default): the scale of the parent is its size, not the child's.">>
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
