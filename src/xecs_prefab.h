#ifndef XECS_PREFABS_H
#define XECS_PREFABS_H
#pragma once

namespace xecs::prefab
{
    using                   guid            = xresource::full_guid;

    // Prefab is a full resource, same treatment as Scene/Level: a real minted type guid, used both
    // in the plugin's Plugin.config (Prefab is registered/listed/asset-browsable, with no compiler)
    // and as new prefabs' full_guid.m_Type at creation time (see mgr::CreatePrefabFromEntity).
    inline constexpr auto type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("Prefab"));

    struct tag
    {
        constexpr static auto typedef_v = xecs::component::type::exclusive_tag
        {
            .m_pName = "PrefabTag"
        };
    };

    struct root
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName            = "RootPrefabComponent"
        ,   .m_ReferenceMode    = xecs::component::type::reference_mode::NO_REFERENCES
        };

        inline xerr Serialize(xecs::serializer::stream&, bool) noexcept;

        // A variant is a prefab whose root is an instance of its base prefab (its prefab_instance says which): the root no longer records a
        // parent prefab guid of its own (prefabs_plan.md, phase 3: one mechanism for nesting and variants).
        guid                                                        m_Guid;             // GUID of the prefab
    };

    // A prefab is a scene (documentation/Editors/prefabs_plan.md, 3.1): its members are addressed by the
    // scene's permanent_id, scoped to that one prefab. GUID-like minting (mgr::details::NextFreeLocalId),
    // not sequential - same merge-collision reasoning already applied to E29's own NextFreeEntityId. The
    // "LocalId" values of the old one-file format are kept as the permanent ids.
    using local_id = xecs::scene::permanent_id;
    constexpr local_id invalid_local_id_v = xecs::scene::invalid_permanent_id_v;

    // A resident template's bookkeeping is the scene's own (m_LocalToRuntime/m_RuntimeToLocal, the entity
    // names), held by the prefab manager (mgr::m_PrefabGroups) and never in the scene manager's list: no
    // tree lists a template's members. mgr::m_PrefabList still stores only the root entity.
    using group_bookkeeping = xecs::scene::instance;

    // A reference a member of a group being made into a prefab held to an entity outside the group: the prefab keeps a null one (a prefab
    // references only its own members), and the instance the group becomes keeps it as an override (what Unity does), see mgr::CreatePrefabFromEntity.
    struct outside_reference
    {
        local_id                    m_Member    = invalid_local_id_v;   // the member's id in the prefab
        std::uint64_t               m_Component = 0;                    // the component type guid
        std::string                 m_Path;                             // the property path
        xecs::component::entity     m_Target{};                         // what it referenced
    };

    //-------------------------------------------------------------------------------
/*
    struct instance
    {
        inline static xcore::err FullSerialize(xecs::serializer::stream&, bool, std::byte*, int&) noexcept;

        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName            = "PrafabInstance"
        ,   .m_pFullSerializeFn = FullSerialize
        };

        struct component
        {
            enum class type : std::uint8_t
            { OVERRIDES         // This component was part of the original archetype and I am overriding some properties
            , NEW               // New component so all properties are assumed to be overwritten 
            , REMOVE            // I removed this component from the original archetype
            };

            struct override
            {
                std::string             m_PropertyName;
                xcore::crc<32>          m_PropertyType;
            };

            xecs::component::type::guid m_ComponentTypeGuid;
            std::vector<override>       m_PropertyOverrides;
            type                        m_Type;
        };

        xecs::component::entity         m_PrefabEntity;
        std::vector<component>          m_lComponents;
    };
*/


    //-------------------------------------------------------------------------------

/*
    namespace details
    {
        xcore::err Serializer(xcore::textfile::stream& TextFile, bool isRead, std::byte* pData, int& Count) noexcept
        {
            xcore::err Error;

            //
            // Write a nice comment for the user so he knows what type of component we are writing 
            //
            if (isRead == false)
            {
                Error = TextFile.WriteComment({ instance::typedef_v.m_pName, 1 + std::strlen( instance::typedef_v.m_pName) });
                if (Error) return Error;
            }

            //
            // Write the main record of the component
            //
            int nInfos = 0;
            TextFile.Record
            (   Error
                , "PrefabI"
                , [&](std::size_t& C, xcore::err&) noexcept
                {
                    if (isRead) Count = static_cast<int>(C);
                    else        C     = Count;
                }
                , [&]( std::size_t c, xcore::err& Err ) noexcept
                {
                    auto& I     = *reinterpret_cast<instance*>( pData + sizeof(instance) * c );
                    int   Count = static_cast<int>(I.m_lComponents.size());
                      (Err = TextFile.Field( "Prefab", I.m_PrefabEntity.m_Value ))
                    ||(Err = TextFile.Field( "nInfos", Count ));

                    nInfos += Count;

                    if( false == isRead )
                    {
                        I.m_lComponents.resize(Count);
                    }
                }
            );

            //
            // Write the components
            //
            instance* pI = reinterpret_cast<instance*>(pData);
            std::size_t e = 0;
            int nOverrides = 0;

            if( nInfos && TextFile.Record
            (   Error
                , "PrefabI+"
                , [&](std::size_t& C, xcore::err&) noexcept
                {
                    if (isRead) Count = nInfos;
                    else        C     = Count;
                }
                , [&]( std::size_t c, xcore::err& Err ) noexcept
                {
                    // Advance indices
                    while( e == pI->m_lComponents.size() )
                    {
                        pI++;
                        e = 0;
                    }

                    auto& E = pI->m_lComponents[e++];

                    int Type;
                    int n = 0;

                    if( false == isRead )
                    {
                        switch (E.m_Type)
                        {  
                        case details::component::type::NEW:         Type = 0; break; 
                        case details::component::type::OVERRIDES:   Type = 1; nOverrides += n = static_cast<int>(E.m_PropertyOverrides.size()); break;
                        case details::component::type::REMOVE:      Type = 2; break;
                        }
                    }

                      (Err = TextFile.Field( "TypeGuid", E.m_ComponentTypeGuid.m_Value ))
                    ||(Err = TextFile.Field( "Type", E.m_Type ))
                    ||(Err = TextFile.Field( "Count", n ));

                    nOverrides += Count;

                    if( isRead )
                    {
                        if(n) E.m_PropertyOverrides.resize(n);
                    }
                }
            ) )
            {
                if( Error.getCode().getState<xcore::textfile::error_state>() != xcore::textfile::error_state::UNEXPECTED_RECORD )
                    return Error;
            }

            //
            // Write any component overrides
            //
            std::size_t z = 0;
            pI = reinterpret_cast<instance*>(pData);
            e  = 0;
            std::vector<xcore::property::entry> Properties;

            if( nOverrides && TextFile.Record
            ( Error
                , "Overrides"
                , [&](std::size_t& C, xcore::err&) noexcept
                {
                    if (isRead) Count = nOverrides;
                    else        C     = Count;
                }
                , [&](std::size_t c, xcore::err& Err) noexcept
                {
                    // Advance indices
                    do
                    {
                        while( e == pI->m_lComponents.size() )
                        {
                            pI++;
                            e = 0;
                            z = 0;
                        }
                        
                    } while( z == pI->m_lComponents[e++].m_PropertyOverrides.size() );

                    if( z == 0 )
                    {
                        Properties.clear();

                        if (false == isRead)
                        {
                            xcore::property::SerializeEnum
                            ( *xecs::component::mgr::findComponentTypeInfo(pI->m_lComponents[e-1].m_ComponentTypeGuid)->m_pPropertyTable
                            , nullptr
                            , [&]( std::string_view PropertyName, property::data&& Data, const property::table& Table, std::size_t Index, property::flags::type Flags )
                            {
                                //...
                            });
                        }
                    }

                    auto& E = pI->m_lComponents[e].m_PropertyOverrides[z++];


                }
            ))
            {
                if (Error.getCode().getState<xcore::textfile::error_state>() != xcore::textfile::error_state::UNEXPECTED_RECORD)
                    return Error;
            }

            return Error;
            
        }
    }
    */
}

#endif