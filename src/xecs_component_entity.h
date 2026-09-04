#ifndef XECS_COMPONENT_ENTITY_H
#define XECS_COMPONENT_ENTITY_H
#pragma once

namespace xecs::component
{
    union entity final
    {
        constexpr static auto invalid_entity_v  = 0xffffffffffffffffu;
        constexpr static auto typedef_v         = xecs::component::type::data
        {
            .m_pName        = "Entity"
        };

        inline xerr Serialize( xecs::serializer::stream& TextFile, bool ) noexcept;

        // Array of infos for an entity
        using info_array = std::array< const xecs::component::type::info*, xecs::settings::max_components_per_entity_v >;

        // Validation allows us to know if a particular entity Unique ID is still valid
        // Farther more it allow us to know if an entity has been deleted but not removed yet (Zombie)
        union validation final
        {
            std::uint32_t       m_Value;
            struct
            {
                std::uint32_t   m_Generation : 31   // Which generation to avoid collisions
                ,               m_bZombie    : 1;   // If the entity is mark as a zombie
            };

            constexpr bool operator == (const validation& V) const noexcept { return m_Value == V.m_Value; }
        };
        static_assert(sizeof(validation) == sizeof(std::uint32_t));

        // The actual global record of the entity is contain in this structure
        // Which it means that for every entity in the game there is one of these
        // Note that this record gets recycle so knowing of an all entity ID is
        // still valid can be done thought the m_Validation 
        struct global_info final
        {
            global_info(const global_info&) = delete;
            global_info()                   = default;

            xecs::pool::instance*   m_pPool         {};         // 64bits
            xecs::pool::index       m_PoolIndex     {};         // 32bits
            validation              m_Validation    {};         // 32bits
        };
        static_assert(sizeof(global_info) == (8 * 2), "We are assuming here 64 bit pointers.");

        // This is the actual entity component data members. It boils down to an
        // index to the global record as shown above and a validation.
        std::uint64_t       m_Value{ invalid_entity_v };
        struct
        {
            std::uint32_t   m_GlobalInfoIndex;  // Index to the global_info
            validation      m_Validation;       // Determine the state and give an additional unique element
        };

        constexpr bool isZombie         (void)                const noexcept { return m_Validation.m_bZombie; }
        constexpr bool operator ==      (const entity& V)     const noexcept { return m_Value == V.m_Value; }
        constexpr bool isValid          (void)                const noexcept { return m_Value != invalid_entity_v; }
    };
    static_assert(sizeof(entity) == sizeof(std::uint64_t));
}
#endif