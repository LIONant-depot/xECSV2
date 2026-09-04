#ifndef XECS_ENTITY_XPROPERTY_BRIDGE_H
#define XECS_ENTITY_XPROPERTY_BRIDGE_H
#pragma once

// xecs::component::entity is a plain union (index + generation + zombie flag packed into one
// uint64_t), so it can't use the inheritance-based "give_properties" wrapper idiom that
// xresource_xproperty_bridge.h uses for xresource::guid<T> (you can't derive from a union).
// Instead it gets a direct var_type<> specialization, the same way xresource::full_guid itself
// is registered in my_properties.h. This lives in xECS (not in the shared my_properties.h) since
// xECS is the consumer of xproperty here, not the other way around - my_properties.h can't see
// xecs::component::entity without a circular include.
namespace xproperty::settings
{
    template<>
    struct var_type<xecs::component::entity> : var_defaults<"entity", xecs::component::entity>
    {
        constexpr static inline char ctype_v                = 'e';
        constexpr static inline auto serialization_type_v   = "G"; // wire layout: one uint64 field (matches old xCore's "entity","G" user-type tag)

        template< typename T, auto CRC_V >
        constexpr static auto XCoreTextFile( T& FileStream, ::xproperty::any& Any )
        {
            if ( Any.m_pType == nullptr || Any.m_pType->m_GUID != guid_v ) Any.Reset<xecs::component::entity>();
            auto E   = Any.get<xecs::component::entity>();
            auto Err = FileStream.Field( CRC_V, "Value:?", E.m_Value );
            if ( !Err && FileStream.isReading() ) Any.get<atomic_type>() = E;
            return Err;
        }
    };
}

// xECS's own atomic-type list for xproperty::sprop::serializer::Stream<>: the project-wide
// xproperty::settings::atomic_types_tuple plus xecs::component::entity, which can't live in the
// shared list for the same circular-include reason noted above.
namespace xecs::component
{
    using xproperty_atomic_types_tuple = xecs::types::tuple_cat_t< xproperty::settings::atomic_types_tuple, std::tuple<xecs::component::entity> >;
}

#endif
