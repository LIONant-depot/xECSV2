#ifndef XECS_PREDEFINITIONS_H
#define XECS_PREDEFINITIONS_H
#pragma once

namespace xecs::component
{
    union entity;
}

namespace xecs::serializer
{
    struct stream;
}

namespace xecs::game_mgr
{
    struct instance;
}

namespace xecs::system
{
    struct instance;
    struct mgr;

    namespace details
    {
        template< typename T_USER_SYSTEM >
        requires(std::derived_from< T_USER_SYSTEM, xecs::system::instance >)
            struct compleated;
    }
}

namespace xecs::archetype
{
    struct instance;
    struct mgr;
    using guid = xresource::guid<struct archetype_tag>;
}

namespace xecs::pool
{
    struct instance;
    struct family;
    struct index
    {
        int     m_Value;
        friend auto operator <=> (const index&, const index&) = default;
    };
}

namespace xecs::query
{
    template< typename T_FUNCTION, auto T_MODE_V >
    struct iterator;
}

#endif
