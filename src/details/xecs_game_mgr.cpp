#include <windows.h>
#include <Lmcons.h>
#include <time.h>
#include <format>

// Modern xproperty object serializer (xproperty::sprop::serializer::Stream) - the drop-in
// replacement for this file's old hand-rolled "enumerate every property and dispatch per-type"
// serialization (property::SerializeEnum + a per-property TextFile.Field switch). Not pulled in
// transitively by xecs.h/xcore.h, so it needs its own include here where it's actually used.
#include "../../../xproperty/source/sprop/property_sprop_xtextfile_serializer.h"

namespace xecs::component
{
    // The one, physical definition of the component-type registry this binary owns (declared in
    // xecs_component_mgr.h as `static type::registry s_Registry;`, deliberately NOT `inline static`
    // - see that declaration's own comment). This file is the right home for it: xecs_game_mgr.cpp
    // is `#include`d directly into xecs.cpp itself and is never compiled as its own standalone
    // translation unit, so this is guaranteed to be the one and only definition per binary - unlike
    // any of the `details/*_inline.h` headers, which get pulled into every separate .cpp that
    // includes xecs.h (xecs.cpp, E29's own .cpp, smoke_test.cpp, ...) and would violate ODR if a
    // plain out-of-line static definition were placed there instead.
    xecs::component::type::registry mgr::s_Registry{};
}

// The one, physical explicit-instantiation DEFINITION for each of xECSV2's own built-in component
// types - see xecs_builtin_instantiations.h's `extern template` declarations (visible to every
// other translation unit) for why. Same "guaranteed compiled exactly once per binary" reasoning as
// mgr::s_Registry just above applies here too.
namespace xecs::component::type::details
{
    template struct XECS_API info_var<xecs::component::entity>;
    template struct XECS_API info_var<xecs::component::parent>;
    template struct XECS_API info_var<xecs::component::children>;
    template struct XECS_API info_var<xecs::component::share_as_data_exclusive_tag>;
    template struct XECS_API info_var<xecs::component::ref_count>;
    template struct XECS_API info_var<xecs::component::share_filter>;
    template struct XECS_API info_var<xecs::component::entity_reference>;
    template struct XECS_API info_var<xecs::prefab::tag>;
    template struct XECS_API info_var<xecs::prefab::root>;
    template struct XECS_API info_var<xecs::editor::prefab_instance>;
}

namespace xecs::game_mgr
{
    //---------------------------------------------------------------------------

    void instance::Run(void) noexcept
    {
        if (m_isRunning == false)
        {
            m_isRunning = true;
            m_ArchetypeMgr.UpdateStructuralChanges();
            // Captures the currently-authored Update-system order/enabled state so any toggle made
            // through the "System Registry" UI WHILE this run is live stays purely transient - Stop()
            // below discards it, reverting to exactly this snapshot.
            m_SystemMgr.SnapshotForPlay();
            m_SystemMgr.m_Events.m_OnGameStart.NotifyAll();
        }

        XCORE_PERF_FRAME_MARK()
        XCORE_PERF_FRAME_MARK_START("ecs::Frame")

        //
        // Run systems
        //
        m_SystemMgr.Run();

        XCORE_PERF_FRAME_MARK_END("ecs::Frame")
    }

    //---------------------------------------------------------------------------

    void instance::Stop(void) noexcept
    {
        if (m_isRunning)
        {
            m_isRunning = false;
            m_SystemMgr.m_Events.m_OnGameEnd.NotifyAll();
            m_SystemMgr.RestoreFromSnapshot();
        }
    }

    //---------------------------------------------------------------------------
    // NOTE(port): the old textfile_property_types_v table (keyed off property::settings::data_variant,
    // an xCore-era Properties-library type list that no longer exists) is gone. It's redundant now
    // anyway: xproperty::sprop::serializer::Stream() registers its own user types automatically from
    // xecs::component::xproperty_atomic_types_tuple the first time it's used on a given stream (see
    // property_sprop_xtextfile_serializer.h - Stream() calls Stream.AddUserTypes(...) itself when
    // getUserTypeCount() == 0), so the manual AddUserTypes(textfile_property_types_v) call this used
    // to require is gone too - see SerializeGameState/SerializeGameStateV2 below.
    //---------------------------------------------------------------------------
    namespace details
    {
        enum serialized_mode : std::int8_t
        { MODE_NONE
        , MODE_SERIALIZER
        , MODE_PROPERTY            
        };

        struct component_serialized_mode
        {
            xecs::component::type::guid         m_Guid;
            int                                 m_Mode;
            const xecs::component::type::info*  m_pInfo;
        };
    }

    xerr instance::SerializeGameState
    ( const char* pFileName
    , bool        isRead
    , bool        isBinary
    ) noexcept
    {
        std::array<xecs::component::entity,             xecs::settings::max_share_components_per_entity_v> ShareEntities{};
        std::array<xecs::component::type::share::key,   xecs::settings::max_share_components_per_entity_v> ShareKeys    {};

        xecs::serializer::stream    TextFile;
        xerr                        Error;

        //
        // Make sure that we are all up to date
        //
        if(isRead == false) m_ArchetypeMgr.UpdateStructuralChanges();

        //
        // Open file for writing
        //
        // TODO(port): xtextfile::stream::Open now takes std::wstring_view - narrow-to-wide widened here (ASCII assumption preserved from pFileName)
        if(Error = TextFile.Open
        ( isRead
        , std::wstring{ pFileName, pFileName + std::strlen(pFileName) }
        , isBinary ? xtextfile::file_type::BINARY : xtextfile::file_type::TEXT
        , xtextfile::flags{ .m_isWriteFloats = true }
        ); Error ) return Error;

        //
        // Serialize some basic info
        //
        int ArchetypeCount = isRead 
        ? 0 
        : [&]
        {
            // We only serialize archetypes that are not prefabs...
            // Note that prefabs-variants are also prefabs but with the extra xecs::prefab::instance component
            int Count = 0;
            for( const auto& E : m_ArchetypeMgr.m_lArchetype )
            {
                if( false == E->getComponentBits().getBit( xecs::component::type::info_v<xecs::prefab::tag>.m_BitID) )
                {
                    Count++;
                }
            }
            return Count;
        }();

        if (Error = TextFile.Record( "GameMgr"
            , [&](xerr& Error) noexcept
            {
                Error = TextFile.Field("nArchetypes", ArchetypeCount);
            }
        )) return Error;

        // If we are writing set the count back to the true count
        // this would allow us to filter later
        if( isRead == false ) ArchetypeCount = static_cast<int>(m_ArchetypeMgr.m_lArchetype.size());


        //
        // Global info
        //
        if (Error = TextFile.Record( "GlobalInfo", [&](xerr& Error) noexcept
        {
            int LastSubrangeRuntime = m_ComponentMgr.m_GlobalEntityInfos.m_LastRuntimeSubrange;
            Error = TextFile.Field("LastSubRange", LastSubrangeRuntime);

            if( isRead )
            {
                if( m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo == nullptr )
                {
                    m_ComponentMgr.m_GlobalEntityInfos.Initialize(LastSubrangeRuntime);
                }

                // TODO: This could be optimize
                while (m_ComponentMgr.m_GlobalEntityInfos.m_LastRuntimeSubrange < LastSubrangeRuntime)
                {
                    m_ComponentMgr.m_GlobalEntityInfos.AppendNewSubrange();
                }
            }

        })) return Error;

        //
        // Write Global Guids
        //
        if( Error = TextFile.Record( "GlobalEntities"
        ,   [&]( std::size_t& C, xerr& ) noexcept
            {
                if( false == isRead )
                {
                    // We will save at least one entry even if the validation is zero
                    // because we don't want to deal with the case where there is block does not save
                    C = 1;

                    //
                    // Determine the max index we want to write
                    //
                    auto Span = std::span{ m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo, xecs::component::ranges::sub_range_entity_count_v * static_cast<std::size_t>(m_ComponentMgr.m_GlobalEntityInfos.m_LastRuntimeSubrange <= 0 ? 0 : 1 + m_ComponentMgr.m_GlobalEntityInfos.m_LastRuntimeSubrange ) };
                    for (auto It = Span.rbegin(); It != Span.rend(); ++It)
                    {
                        auto& E = *It;
                        if (E.m_Validation.m_Value)
                        {
                            C = 1 + static_cast<int>(static_cast<std::size_t>(&E - m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo ));
                            break;
                        }
                    }

                    int b=0;
                }
            }
        ,   [&]( std::size_t i, xerr& Error ) noexcept
            {
                Error = TextFile.Field("Validation", m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo[i].m_Validation.m_Value );
            }
        )) return Error;

        //
        // Serialize all the archetypes
        //
        for( int iArchetype=0; iArchetype < ArchetypeCount; ++iArchetype)
        {
            // When writing we want to filter out any prefab archetype
            if( false == isRead && m_ArchetypeMgr.m_lArchetype[iArchetype]->getComponentBits().getBit(xecs::component::type::info_v<xecs::prefab::tag>.m_BitID))
                continue;

            //
            // Archetype main header
            //
            xecs::component::entity::info_array Infos           {};
            int                                 InfoCount       {};
            int                                 nDataTypes      {};
            int                                 nShareTypes     {};
            int                                 nTagTypes       {};
            int                                 nFamilies       {};
            xecs::archetype::guid               ArchetypeGuid   {};
            std::array<std::int8_t, xecs::settings::max_components_per_entity_v> SerializedModes {};

            if( false == isRead )
            {
                auto&               Archetype   = m_ArchetypeMgr.m_lArchetype[iArchetype];
                xecs::tools::bits   TagBits     = xecs::tools::bits{}.setupAnd(Archetype->m_ComponentBits, xecs::component::mgr::s_Registry.m_TagsBits);

                nDataTypes      = Archetype->m_nDataComponents;
                nShareTypes     = Archetype->m_nShareComponents;
                nTagTypes       = TagBits.CountComponents();
                InfoCount       = Archetype->m_ComponentBits.ToInfoArray(Infos);
                nFamilies       = 0;
                ArchetypeGuid   = Archetype->m_Guid;

                // Save only families which have entites
                for( auto pF = Archetype->getFamilyHead(); pF; pF = pF->m_Next.get() )
                {
                    nFamilies++;
                }

                //
                // Write comments to help the user read the file
                //
                if ((Error = TextFile.WriteComment(" TypeInfo Details: "))) return Error;
                for(int i=0; i< InfoCount; i++ )
                {
                    if( (Error = TextFile.WriteComment( std::format( "   Guid:{:016X}   Type:{}   Name:{}"
                        , Infos[i]->m_Guid.m_Value
                        , Infos[i]->m_TypeID == xecs::component::type::id::DATA
                            ? "Data"
                            : Infos[i]->m_TypeID == xecs::component::type::id::SHARE
                            ? "Share"
                            : "Tag"
                        , Infos[i]->m_pName ) ))) return Error;
                }
            }

            //
            // Save the basic archetype info
            //
            if( Error = TextFile.Record( "Archetype"
                ,   [&]( xerr& Error ) noexcept
                    {
                            (Error = TextFile.Field("Guid",         ArchetypeGuid.m_Value))
                        ||  (Error = TextFile.Field("nFamilies",    nFamilies))
                        ||  (Error = TextFile.Field("nDataTypes",   nDataTypes))
                        ||  (Error = TextFile.Field("nShareTypes",  nShareTypes))
                        ||  (Error = TextFile.Field("nTagTypes",    nTagTypes));
                    }
                )) return Error;

            //
            // Read the archetype types
            //
            if( Error = TextFile.Record( "ArchetypeTypes"
                ,   [&]( std::size_t& C, xerr& ) noexcept
                    {
                        if( isRead ) InfoCount  = static_cast<int>(C);
                        else         C          = InfoCount;
                    }
                ,   [&]( std::size_t i, xerr& Error ) noexcept
                    {
                        xecs::component::type::guid Guid
                            = isRead
                            ? xecs::component::type::guid{}
                            : Infos[i]->m_Guid;

                        if( (Error = TextFile.Field( "TypeGuid", Guid.m_Value )) ) return;

                        if( isRead )
                        {
                            auto pInfo = m_ComponentMgr.findComponentTypeInfo(Guid);
                            if( pInfo == nullptr )
                            {
                                // TODO: Change this to a warning?
                                Error = xerr::create<state::FAILURE, "Serialization Error, Fail to find one of the components types">();
                                return;
                            }

                            // Set the info into the structure this includes tags
                            Infos[i] = pInfo;
                        }

                        // We need to know how this type was serialized when loading. This is only relevant when
                        // we saved with properties and then we add the serializing function, for the loader to do the
                        // right thing it should used the properties loader since this is how it was saved with.
                        if( isRead == false )
                        {
                            if( Infos[i]->m_pSerilizeFn         ) SerializedModes[i] = details::serialized_mode::MODE_SERIALIZER;
                            else if (Infos[i]->m_pPropertyTable ) SerializedModes[i] = details::serialized_mode::MODE_PROPERTY;
                            else                                  SerializedModes[i] = details::serialized_mode::MODE_NONE;
                        }
                        TextFile.Field("SerializationMode", SerializedModes[i]).clear();
                    }
                )) return Error;


            //
            // Get or create the actual archetype
            //
            xecs::archetype::instance* pArchetype
                = isRead
                ? &getOrCreateArchetype({ Infos.data(), static_cast<std::size_t>(InfoCount) })
                : m_ArchetypeMgr.m_lArchetype[iArchetype].get();

            //
            // Serialize all families
            //
            xecs::pool::family* pF = isRead ? nullptr : pArchetype->getFamilyHead();
            for( int iFamily = 0; iFamily != nFamilies; ++iFamily, pF = pF->m_Next.get() )
            {
                int                         nPools      = 0;
                int                         nEntities   = 0;

                //
                // Count how many pools we have
                //
                if( false == isRead )
                {
                    for( auto pP = &pF->m_DefaultPool; pP; pP = pP->m_Next.get() )
                    {
                        nEntities += pP->Size();
                        nPools++;
                    }
                }

                //
                // Deal with the families
                //
                xecs::pool::family::guid    FamilyGuid = isRead ? xecs::pool::family::guid{} : pF->m_Guid;

                if( Error = TextFile.Record( "Family"
                ,   [&]( xerr& Error ) noexcept
                    {
                          (Error = TextFile.Field("Guid",       FamilyGuid.m_Value ))
                        ||(Error = TextFile.Field("nPools",     nPools))
                        ||(Error = TextFile.Field("nEntities",  nEntities));
                    }
                )) return Error;

                if( nShareTypes && (Error = TextFile.Record( "FamilyDetails"
                ,   [&]( std::size_t& C, xerr& ) noexcept
                    {
                        if( false == isRead ) C = pF->m_ShareInfos.size();
                        else                  assert( C == nShareTypes );
                    }
                ,   [&]( std::size_t i, xerr& Error ) noexcept
                    {
                        if( isRead )
                        {
                              (Error = TextFile.Field("Entity",     ShareEntities[i].m_Value ))
                            ||(Error = TextFile.Field("ShareKey",   ShareKeys[i].m_Value));
                        }
                        else
                        {
                              (Error = TextFile.Field("Entity",     pF->m_ShareDetails[i].m_Entity.m_Value ))
                            ||(Error = TextFile.Field("ShareKey",   pF->m_ShareDetails[i].m_Key.m_Value));
                        }
                    }
                ))) return Error;

                // Create a family in case of reading
                if(isRead)
                {
                    // TODO: Make sure the order of families match the saved order in memory
                    //      To do that we need to fix the link list of pending families
                    pF = &pArchetype->CreateNewPoolFamily
                    ( FamilyGuid
                    , std::span{ ShareEntities.data(),  static_cast<std::size_t>(nShareTypes) }
                    , std::span{ ShareKeys.data(),      static_cast<std::size_t>(nShareTypes) }
                    );

                    //
                    // If the Family has shares then we must insert the shares into the hash map
                    //
                    if(nShareTypes)
                    {
                        for (int i = 0; i < nShareTypes; i++)
                        {
                            auto& Details = pF->m_ShareDetails[i];
                            m_ArchetypeMgr.m_ShareComponentEntityMap.emplace( std::pair{ Details.m_Key, Details.m_Entity });
                        }
                    }
                }

                //
                // Serialize Pools
                //
                auto pP = &pF->m_DefaultPool;
                for( int iPool =0; iPool < nPools; ++iPool, pP = pP->m_Next.get() )
                {
                    int     nEntitiesInPool = isRead ? 0 : pP->Size();

                    if( Error = TextFile.Record( "PoolInfo"
                    ,   [&]( xerr& Error ) noexcept
                        {
                            Error = TextFile.Field("nEntities", nEntitiesInPool);
                        }
                    )) return Error;

                    // Do we have any entities that we need to deal with?
                    if( nEntitiesInPool == 0 )
                    {
                        //
                        // If we need to add another pool lets do so
                        //
                        if( isRead )
                        {
                            if ((iPool + 1) != nPools)
                            {
                                pP->m_Next = std::make_unique<pool::instance>();
                                pP->m_Next->Initialize(pF->m_DefaultPool.m_ComponentInfos, *pF);
                            }
                        }

                        // Go next pool
                        continue;
                    }

                    //
                    // Serialize Entities
                    //
                    if( isRead )
                    {
                        pP->Append( nEntitiesInPool );

                        for( auto iType = 0, end = (int)pP->m_ComponentInfos.size(); iType != end; iType++ )
                        {
                            if( SerializedModes[iType] == details::serialized_mode::MODE_SERIALIZER && pP->m_ComponentInfos[iType]->m_pSerilizeFn)
                            {
                                int Count = 0;
                                Error = pP->m_ComponentInfos[iType]->m_pSerilizeFn(TextFile, isRead, pP->m_pComponent[iType], Count);
                                if (Error) return Error;
                                assert(Count == nEntitiesInPool);
                            }
                            else if( SerializedModes[iType] == details::serialized_mode::MODE_PROPERTY && pP->m_ComponentInfos[iType]->m_pPropertyTable )
                            {
                                int             Count    = nEntitiesInPool;
                                std::byte*      pData    = pP->m_pComponent[iType];
                                auto            TypeSize = pP->m_ComponentInfos[iType]->m_Size;
                                auto&           Table    = *pP->m_ComponentInfos[iType]->m_pPropertyTable;
                                xproperty::settings::context Context{};

                                for( int i = 0; i < Count; ++i )
                                {
                                    if( Error = xproperty::sprop::serializer::Stream<xecs::component::xproperty_atomic_types_tuple>( TextFile, pData, Table, Context ); Error )
                                    {
                                        if( Error.getState<xtextfile::state>() == xtextfile::state::UNEXPECTED_RECORD )
                                        {
                                            printf( "Warning: We were expecting a table but we failed to find it");
                                            Error.clear();
                                        }
                                        else
                                        {
                                            return Error;
                                        }
                                    }
                                    pData += TypeSize;
                                } // for
                            }
                            else
                            {
                                if( SerializedModes[iType] != details::serialized_mode::MODE_NONE )
                                {
                                    if( SerializedModes[iType] == details::serialized_mode::MODE_SERIALIZER )
                                    {
                                        return xerr::create<state::FAILURE, "Error: TextFile should have a skip record function, but none the less we failed to load file because we don't have serialized functions any more">();
                                    }
                                    else if( SerializedModes[iType] == details::serialized_mode::MODE_PROPERTY )
                                    {
                                        //TextFile.SkipRecord();
                                        return xerr::create<state::FAILURE, "Error: TextFile should have a skip record function, but none the less we failed to load file because we don't have properties any more">();
                                    }
                                    else
                                    {
                                        assert(false);
                                    }
                                }

                                continue;
                            }
        
                            // Are we dealing with the entities?
                            if( iType == 0 )
                            {
                                //
                                // Hook up with the global entries
                                //
                                for( int i=0; i<nEntitiesInPool; i++ )
                                {
                                    auto& Entity = reinterpret_cast<xecs::component::entity*>(pP->m_pComponent[0])[i];
                                    auto& Global = m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo[Entity.m_GlobalInfoIndex];

                                    assert( Global.m_Validation == Entity.m_Validation );
                                    Global.m_PoolIndex  = xecs::pool::index{i};
                                    Global.m_pPool      = pP;
                                }
                            }
                        }

                        //
                        // Add to the pending list
                        //
                        m_ArchetypeMgr.AddToStructuralPendingList(*pP);

                        //
                        // If we need to add another pool lets do so
                        //
                        if((iPool+1) != nPools)
                        {
                            pP->m_Next = std::make_unique<pool::instance>();
                            pP->m_Next->Initialize(pF->m_DefaultPool.m_ComponentInfos, *pF);
                        }
                    }
                    else
                    {
                        for( auto iType=0, end = (int)pP->m_ComponentInfos.size(); iType != end; iType++ )
                        {
                            auto& PropInfo = *pP->m_ComponentInfos[iType];
                            if( SerializedModes[iType] == details::serialized_mode::MODE_SERIALIZER )
                            {
                                int Count = pP->Size();
                                Error = pP->m_ComponentInfos[iType]->m_pSerilizeFn( TextFile, isRead, pP->m_pComponent[iType], Count );
                                if(Error) return Error;
                            }
                            else if( SerializedModes[iType] == details::serialized_mode::MODE_PROPERTY )
                            {
                                int         Count    = pP->Size();
                                std::byte*  pData    = pP->m_pComponent[iType];
                                auto        TypeSize = PropInfo.m_Size;
                                auto&       Table    = *PropInfo.m_pPropertyTable;
                                xproperty::settings::context Context{};

                                for( int i=0; i<Count; ++i )
                                {
                                    Error = xproperty::sprop::serializer::Stream<xecs::component::xproperty_atomic_types_tuple>( TextFile, pData + TypeSize * i, Table, Context );
                                    if( Error ) return Error;
                                }
                            }
                            else
                            {
                                assert( SerializedModes[iType] == details::serialized_mode::MODE_NONE );
                                continue;
                            }
                        }
                    }
                }
            }
        }

        //
        // Fill unused global entities to the empty list
        //
        if( isRead )
        {
            m_ComponentMgr.m_GlobalEntityInfos.m_EmptyHead = -1;
            auto Span = std::span{ m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo, static_cast<std::size_t>(m_ComponentMgr.m_GlobalEntityInfos.m_LastRuntimeSubrange) };
            for( auto It = Span.rbegin(); It != Span.rend(); ++It )
            {
                auto& E = *It;
                if( E.m_pPool == nullptr )
                {
                    E.m_PoolIndex.m_Value = m_ComponentMgr.m_GlobalEntityInfos.m_EmptyHead;
                    m_ComponentMgr.m_GlobalEntityInfos.m_EmptyHead = static_cast<int>(static_cast<std::size_t>(&E - m_ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo));
                }
            }

            //
            // Lets Update the structural changes
            //
            m_ArchetypeMgr.UpdateStructuralChanges();
        }

        return Error;
    }

} // end of namespace
