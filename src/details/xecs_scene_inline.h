#include <filesystem>
#include <format>
#include <cstdio>

// xproperty::sprop::serializer::Stream (the xtextfile-backed whole-object property serializer) is
// the same modern replacement SerializeGameState uses for BY_PROPERTIES components - see the note
// at the top of xecs_game_mgr.cpp. Not pulled in transitively by xecs.h, so it needs its own include
// here too, matching that file's exact relative path (this file lives in the same details/ folder).
#include "../../../xproperty/source/sprop/property_sprop_xtextfile_serializer.h"

namespace xecs::scene
{
    namespace details
    {
        //-----------------------------------------------------------------------------------------
        // Reference encoding (doc's Level-loading spec, adapted onto xecs::component::entity):
        // a serialized entity-reference field is one signed 64bit integer stored in the field's
        // xecs::component::entity.m_Value (bit-preserving round trip - both are 64bit, and
        // int64/uint64 conversion is two's-complement-defined in C++20). 0 = null, >0 = local
        // permanent_id, <0 = -(index into the owning scene's external-ref table) - 1.
        //-----------------------------------------------------------------------------------------
        constexpr xecs::component::entity EncodeRef( std::int64_t V ) noexcept
        {
            xecs::component::entity E;
            E.m_Value = static_cast<std::uint64_t>(V);
            return E;
        }

        constexpr std::int64_t DecodeRef( xecs::component::entity E ) noexcept
        {
            return static_cast<std::int64_t>(E.m_Value);
        }

        //-----------------------------------------------------------------------------------------
        // On-disk paths. Milestone 1 uses a plain folder (Mgr.m_RootPath), not the GUID-sharded
        // Descriptors/Scene/<b0>/<b1>/<guid>.desc/ layout the rest of the resource pipeline uses -
        // that integration is deferred. The entity_db itself DOES use the same 2-level byte-sharded
        // convention as every other resource type, keyed off the permanent_id instead of a guid.
        //-----------------------------------------------------------------------------------------
        inline std::wstring FormatHex64( std::uint64_t V ) noexcept { return std::format( L"{:016X}", V ); }
        inline std::wstring FormatHex32( std::uint32_t V ) noexcept { return std::format( L"{:08X}",  V ); }
        inline std::wstring FormatHex8 ( std::uint8_t  V ) noexcept { return std::format( L"{:02X}",  V ); }

        inline std::wstring SceneFolder( mgr& Mgr, guid SceneGuid ) noexcept
        {
            return Mgr.m_RootPath + L"/" + FormatHex64(SceneGuid.m_Value) + L".scene";
        }

        inline std::wstring DescriptorPath( mgr& Mgr, guid SceneGuid ) noexcept
        {
            return SceneFolder(Mgr, SceneGuid) + L"/Descriptor.txt";
        }

        inline std::wstring EntityDbFolder( mgr& Mgr, guid SceneGuid ) noexcept
        {
            return SceneFolder(Mgr, SceneGuid) + L"/entity_db";
        }

        inline std::wstring EntityPath( mgr& Mgr, guid SceneGuid, permanent_id Id ) noexcept
        {
            const auto Byte0 = FormatHex8( static_cast<std::uint8_t>( Id         & 0xFF) );
            const auto Byte1 = FormatHex8( static_cast<std::uint8_t>((Id >> 8)   & 0xFF) );
            return EntityDbFolder(Mgr, SceneGuid) + L"/" + Byte0 + L"/" + Byte1 + L"/" + FormatHex32(Id) + L".entity";
        }

        //-----------------------------------------------------------------------------------------
        // Scan this scene's entity_db for every "<permanent_id-hex>.entity" file. This (rather than
        // a persisted manifest) is the whole point of giving every entity its own file: entities can
        // be added/removed/merged independently without keeping a shared list in sync.
        //-----------------------------------------------------------------------------------------
        inline std::vector<permanent_id> DiscoverEntityIds( mgr& Mgr, guid SceneGuid ) noexcept
        {
            std::vector<permanent_id> Ids;
            std::error_code           Ec;

            const auto Root = std::filesystem::path( EntityDbFolder(Mgr, SceneGuid) );
            if( false == std::filesystem::exists(Root, Ec) || Ec ) return Ids;

            for( auto& Entry : std::filesystem::recursive_directory_iterator(Root, std::filesystem::directory_options::skip_permission_denied, Ec) )
            {
                if( Ec ) break;
                if( false == Entry.is_regular_file() ) continue;
                if( Entry.path().extension() != L".entity" ) continue;

                try
                {
                    const auto Value = std::stoul( Entry.path().stem().wstring(), nullptr, 16 );
                    Ids.push_back( static_cast<permanent_id>(Value) );
                }
                catch(...) { continue; }
            }

            return Ids;
        }

        //-----------------------------------------------------------------------------------------
        // Resolves a live runtime entity reference (as currently seen from inside `Scene`, which may
        // not be the entity's own scene) into its on-disk encoded form. Same-scene -> positive local
        // permanent_id. A declared parent's entity -> interned/reused negative external-table key.
        // Anything else (child/unrelated/dynamic/invalid target) is rejected - per the spec, a strong
        // cross-level reference may only target the same scene or a declared parent.
        //-----------------------------------------------------------------------------------------
        inline bool ResolveReferenceForSave( mgr& Mgr, instance& Scene, xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept
        {
            if( false == Target.isValid() ) { OutEncoded = 0; return true; }

            if( auto It = Scene.m_RuntimeToLocal.find(Target.m_Value); It != Scene.m_RuntimeToLocal.end() )
            {
                OutEncoded = static_cast<std::int64_t>(It->second);
                return true;
            }

            for( auto& ParentGuid : Scene.m_ParentScenes )
            {
                auto* pParent = Mgr.Find(ParentGuid);
                if( pParent == nullptr ) continue;

                auto It = pParent->m_RuntimeToLocal.find(Target.m_Value);
                if( It == pParent->m_RuntimeToLocal.end() ) continue;

                const external_entity_address Addr{ ParentGuid, It->second };
                for( std::size_t i = 0; i < Scene.m_ExternalRefTable.size(); ++i )
                {
                    auto& E = Scene.m_ExternalRefTable[i];
                    if( E.m_ParentScene.m_Value == Addr.m_ParentScene.m_Value && E.m_ParentEntity == Addr.m_ParentEntity )
                    {
                        OutEncoded = -(static_cast<std::int64_t>(i) + 1);
                        return true;
                    }
                }

                Scene.m_ExternalRefTable.push_back(Addr);
                OutEncoded = -(static_cast<std::int64_t>(Scene.m_ExternalRefTable.size()));
                return true;
            }

            return false;
        }

        //-----------------------------------------------------------------------------------------
        // Walks one newly-created (already component-populated) entity's data components, in the
        // same DataSpan/getComponentInSequenceByInfo pattern CreatePrefabInstance's own reference
        // remap pass already uses (details/xecs_prefab_mgr_inline.h), and replaces every encoded
        // reference field's raw int64 with the resolved live runtime entity - or a proper null
        // (xecs::component::entity{} / invalid_entity_v, NOT a bit-pattern 0) for an encoded 0.
        //-----------------------------------------------------------------------------------------
        template< typename T_RESOLVE >
        inline void RemapLoadedEntityReferences( xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity, T_RESOLVE&& Resolve ) noexcept
        {
            auto& IDetails  = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            auto& Archetype = *IDetails.m_pPool->m_pArchetype;
            auto  DataSpan  = Archetype.getDataComponentInfos();

            std::vector<xecs::component::entity*> References;
            int iSequence = 0;
            for( auto pInfo : DataSpan )
            {
                if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES
                 || pInfo == &xecs::component::type::info_v<xecs::component::entity> )
                    continue;

                auto pData = IDetails.m_pPool->getComponentInSequenceByInfo( *pInfo, IDetails.m_PoolIndex, iSequence );

                if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::BY_FUNCTION )
                {
                    pInfo->m_pReportReferencesFn( References, pData );
                    for( auto pRef : References )
                        *pRef = Resolve( DecodeRef(*pRef) );
                    References.clear();
                }
                else
                {
                    xproperty::settings::context Context{};
                    std::string                  SetError;
                    xproperty::sprop::collector( pData, *pInfo->m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void* ) noexcept
                    {
                        if( Data.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v )
                        {
                            const auto Encoded = DecodeRef( Data.get<xecs::component::entity>() );
                            Data.get<xecs::component::entity>() = Resolve(Encoded);
                            xproperty::sprop::setProperty( SetError, pData, *pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ pPropertyName, Data }, Context );
                        }
                    });
                }
            }
        }

        //-----------------------------------------------------------------------------------------
        // Serializes (read or write) exactly one instance of a data component, dispatching the same
        // way SerializeGameState's live read/write path already does: prefer the hand-written
        // full_serialize_fn, otherwise fall back to whole-object property serialization.
        //-----------------------------------------------------------------------------------------
        inline xerr SerializeOneComponent( xecs::serializer::stream& TextFile, bool isRead, const xecs::component::type::info& Info, std::byte* pData ) noexcept
        {
            if( Info.m_pSerilizeFn )
            {
                int Count = 1;
                return Info.m_pSerilizeFn( TextFile, isRead, pData, Count );
            }

            if( Info.m_pPropertyTable )
            {
                xproperty::settings::context Context{};
                return xproperty::sprop::serializer::Stream<xecs::component::xproperty_atomic_types_tuple>( TextFile, pData, *Info.m_pPropertyTable, Context );
            }

            return xerr::create<xecs::game_mgr::state::FAILURE, "Scene entity component has neither a serializer nor a property table">();
        }

        //-----------------------------------------------------------------------------------------
        inline void UnloadSceneEntities( xecs::game_mgr::instance& GameMgr, instance& Scene ) noexcept
        {
            for( auto& Pair : Scene.m_LocalToRuntime )
            {
                auto E = Pair.second;
                GameMgr.DeleteEntity(E);
            }

            Scene.m_LocalToRuntime.clear();
            Scene.m_RuntimeToLocal.clear();
            Scene.m_ExternalToRuntime.clear();
        }

        //-----------------------------------------------------------------------------------------
        // Unloads a scene (child-first, cascading into parents) iff its residency is currently zero.
        // Only ever touches m_DependentSceneCount, never m_ExplicitRequests - an explicit request is
        // only ever acquired/released by the top-level mgr::RequestLoad/ReleaseLoad entry points, and
        // this helper is what both those entry points and the parent-cascade defer to so a scene that
        // becomes idle purely because a *dependent* went away never has someone else's explicit
        // request silently released out from under them.
        //-----------------------------------------------------------------------------------------
        inline void UnloadIfIdle( mgr& Mgr, instance& Scene ) noexcept
        {
            if( Scene.m_ExplicitRequests > 0 || Scene.m_DependentSceneCount > 0 ) return;
            if( Scene.m_State != state::Active && Scene.m_State != state::Failed ) return;

            UnloadSceneEntities( Mgr.m_GameMgr, Scene );

            for( auto& ParentGuid : Scene.m_ParentScenes )
            {
                if( auto* pParent = Mgr.Find(ParentGuid) )
                {
                    if( pParent->m_DependentSceneCount > 0 ) pParent->m_DependentSceneCount--;
                    UnloadIfIdle( Mgr, *pParent );
                }
            }

            Scene.m_State = state::Unloaded;
        }

        //-----------------------------------------------------------------------------------------
        inline xerr LoadSceneDescriptor( mgr& Mgr, instance& Scene ) noexcept
        {
            xecs::serializer::stream TextFile;
            if( auto Err = TextFile.Open( true, DescriptorPath(Mgr, Scene.m_Guid), xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
                return Err;

            std::uint64_t GuidValue = 0;
            int           nParents  = 0;
            int           nExternal = 0;

            if( auto Err = TextFile.Record( "SceneInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("Guid",         GuidValue))
                    ||(Error = TextFile.Field("nParents",     nParents))
                    ||(Error = TextFile.Field("nExternalRefs", nExternal));
                }
            ); Err ) return Err;

            // A zero-count array record is never actually written to disk (WriteRecord only flushes
            // its "[Name:Count]" header lazily, on the first element - with zero elements nothing gets
            // flushed at all), so a scene with no parents/external refs simply has no such section to
            // read back - skip the Record() call entirely rather than trying to read a section that
            // was never written.
            Scene.m_ParentScenes.assign(nParents, guid{});
            if( nParents > 0 )
            {
                if( auto Err = TextFile.Record( "Parents"
                ,   [&]( std::size_t& C, xerr& ) noexcept { C = static_cast<std::size_t>(nParents); }
                ,   [&]( std::size_t i, xerr& Error ) noexcept
                    {
                        std::uint64_t V = 0;
                        if( (Error = TextFile.Field("SceneGuid", V)) ) return;
                        Scene.m_ParentScenes[i] = guid{V};
                    }
                ); Err ) return Err;
            }

            Scene.m_ExternalRefTable.assign(nExternal, external_entity_address{});
            if( nExternal > 0 )
            {
                if( auto Err = TextFile.Record( "ExternalRefs"
                ,   [&]( std::size_t& C, xerr& ) noexcept { C = static_cast<std::size_t>(nExternal); }
                ,   [&]( std::size_t i, xerr& Error ) noexcept
                    {
                        std::uint64_t ParentGuidValue = 0;
                        permanent_id  ParentEntity    = invalid_permanent_id_v;
                        if( (Error = TextFile.Field("ParentSceneGuid", ParentGuidValue)) ) return;
                        if( (Error = TextFile.Field("ParentEntity",    ParentEntity)) ) return;
                        Scene.m_ExternalRefTable[i] = external_entity_address{ guid{ParentGuidValue}, ParentEntity };
                    }
                ); Err ) return Err;
            }

            return {};
        }

        //-----------------------------------------------------------------------------------------
        inline xerr LoadEntity( mgr& Mgr, instance& Scene, permanent_id Id ) noexcept
        {
            xecs::serializer::stream TextFile;
            if( auto Err = TextFile.Open( true, EntityPath(Mgr, Scene.m_Guid, Id), xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
                return Err;

            permanent_id FileId      = invalid_permanent_id_v;
            int          nComponents = 0;

            if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("PermanentId",  FileId))
                    ||(Error = TextFile.Field("nComponents",  nComponents));
                }
            ); Err ) return Err;

            std::vector<const xecs::component::type::info*> Infos( static_cast<std::size_t>(nComponents), nullptr );
            if( nComponents > 0 )
            {
                if( auto Err = TextFile.Record( "ComponentTypes"
                ,   [&]( std::size_t& C, xerr& ) noexcept { C = static_cast<std::size_t>(nComponents); }
                ,   [&]( std::size_t i, xerr& Error ) noexcept
                    {
                        std::uint64_t GuidValue = 0;
                        if( (Error = TextFile.Field("Guid", GuidValue)) == false )
                            Infos[i] = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{GuidValue} );
                    }
                ); Err ) return Err;
            }

            for( auto pInfo : Infos )
            {
                if( pInfo == nullptr )
                    return xerr::create<xecs::game_mgr::state::FAILURE, "Scene entity file references a component type that is no longer registered">();
            }

            std::vector<const xecs::component::type::info*> ArchetypeInfos;
            ArchetypeInfos.reserve(Infos.size() + 1);
            ArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::component::entity> );
            for( auto pInfo : Infos ) ArchetypeInfos.push_back(pInfo);

            auto& Archetype = Mgr.m_GameMgr.getOrCreateArchetype( { ArchetypeInfos.data(), ArchetypeInfos.size() } );

            std::vector<std::byte*> MoveData( ArchetypeInfos.size(), nullptr );
            auto NewEntity = Archetype.CreateEntity( { ArchetypeInfos.data(), ArchetypeInfos.size() }, { MoveData.data(), MoveData.size() } );

            auto& EDetails = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            auto& Pool      = *EDetails.m_pPool;

            for( auto pInfo : Infos )
            {
                const auto iType = Pool.findIndexComponentFromInfo(*pInfo);
                assert(iType >= 0);
                auto pData = &Pool.m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

                if( auto Err = SerializeOneComponent(TextFile, true, *pInfo, pData); Err )
                {
                    auto E = NewEntity;
                    Mgr.m_GameMgr.DeleteEntity(E);
                    return Err;
                }
            }

            Scene.m_LocalToRuntime[FileId]           = NewEntity;
            Scene.m_RuntimeToLocal[NewEntity.m_Value] = FileId;

            return {};
        }
    }

    //-----------------------------------------------------------------------------------------------

    instance* mgr::Find( guid SceneGuid ) noexcept
    {
        for( auto& Up : m_SceneInstances )
            if( Up->m_Guid.m_Value == SceneGuid.m_Value )
                return Up.get();
        return nullptr;
    }

    //-----------------------------------------------------------------------------------------------

    instance& mgr::FindOrCreate( guid SceneGuid ) noexcept
    {
        if( auto* pScene = Find(SceneGuid) ) return *pScene;

        auto Up        = std::make_unique<instance>();
        Up->m_Guid      = SceneGuid;
        auto& Ref       = *Up;
        m_SceneInstances.push_back( std::move(Up) );
        return Ref;
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::SaveSceneDescriptor( guid SceneGuid ) noexcept
    {
        auto* pScene = Find(SceneGuid);
        if( pScene == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "SaveSceneDescriptor: scene is not registered - call FindOrCreate first">();

        std::error_code Ec;
        std::filesystem::create_directories( std::filesystem::path(details::EntityDbFolder(*this, SceneGuid)), Ec );

        xecs::serializer::stream TextFile;
        if( auto Err = TextFile.Open( false, details::DescriptorPath(*this, SceneGuid), xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
            return Err;

        std::uint64_t GuidValue = pScene->m_Guid.m_Value;
        int           nParents  = static_cast<int>(pScene->m_ParentScenes.size());
        int           nExternal = static_cast<int>(pScene->m_ExternalRefTable.size());

        if( auto Err = TextFile.Record( "SceneInfo", [&]( xerr& Error ) noexcept
            {
                  (Error = TextFile.Field("Guid",          GuidValue))
                ||(Error = TextFile.Field("nParents",      nParents))
                ||(Error = TextFile.Field("nExternalRefs", nExternal));
            }
        ); Err ) return Err;

        // Matching LoadSceneDescriptor: a zero-count array record is never actually flushed to disk
        // by WriteRecord (its header line is only written lazily, alongside the first element), so
        // skip these entirely when empty rather than relying on that being harmless.
        if( false == pScene->m_ParentScenes.empty() )
        {
            if( auto Err = TextFile.Record( "Parents"
            ,   [&]( std::size_t& C, xerr& ) noexcept { C = pScene->m_ParentScenes.size(); }
            ,   [&]( std::size_t i, xerr& Error ) noexcept
                {
                    std::uint64_t V = pScene->m_ParentScenes[i].m_Value;
                    Error = TextFile.Field("SceneGuid", V);
                }
            ); Err ) return Err;
        }

        if( false == pScene->m_ExternalRefTable.empty() )
        {
            if( auto Err = TextFile.Record( "ExternalRefs"
            ,   [&]( std::size_t& C, xerr& ) noexcept { C = pScene->m_ExternalRefTable.size(); }
            ,   [&]( std::size_t i, xerr& Error ) noexcept
                {
                    std::uint64_t ParentGuidValue = pScene->m_ExternalRefTable[i].m_ParentScene.m_Value;
                    permanent_id  ParentEntity    = pScene->m_ExternalRefTable[i].m_ParentEntity;
                      (Error = TextFile.Field("ParentSceneGuid", ParentGuidValue))
                    ||(Error = TextFile.Field("ParentEntity",    ParentEntity));
                }
            ); Err ) return Err;
        }

        return {};
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::SaveEntity( guid SceneGuid, permanent_id Id, xecs::component::entity Entity ) noexcept
    {
        auto& Scene = FindOrCreate(SceneGuid);

        auto& EDetails  = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& Archetype = *EDetails.m_pPool->m_pArchetype;
        auto  DataSpan  = Archetype.getDataComponentInfos();

        std::vector<const xecs::component::type::info*> Infos;
        Infos.reserve(DataSpan.size());
        for( auto pInfo : DataSpan )
            if( pInfo != &xecs::component::type::info_v<xecs::component::entity> )
                Infos.push_back(pInfo);

        const auto Path = details::EntityPath(*this, SceneGuid, Id);
        std::error_code Ec;
        std::filesystem::create_directories( std::filesystem::path(Path).parent_path(), Ec );

        xecs::serializer::stream TextFile;
        if( auto Err = TextFile.Open( false, Path, xtextfile::file_type::TEXT, xtextfile::flags{ .m_isWriteFloats = true } ); Err )
            return Err;

        permanent_id WriteId      = Id;
        int          nComponents = static_cast<int>(Infos.size());

        if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
            {
                  (Error = TextFile.Field("PermanentId", WriteId))
                ||(Error = TextFile.Field("nComponents", nComponents));
            }
        ); Err ) return Err;

        if( false == Infos.empty() )
        {
            if( auto Err = TextFile.Record( "ComponentTypes"
            ,   [&]( std::size_t& C, xerr& ) noexcept { C = Infos.size(); }
            ,   [&]( std::size_t i, xerr& Error ) noexcept
                {
                    std::uint64_t V = Infos[i]->m_Guid.m_Value;
                    Error = TextFile.Field("Guid", V);
                }
            ); Err ) return Err;
        }

        for( auto pInfo : Infos )
        {
            const auto iType = EDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
            assert(iType >= 0);
            auto pLive = &EDetails.m_pPool->m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

            std::vector<std::byte> Scratch( pInfo->m_Size );
            if( pInfo->m_pCopyFn ) pInfo->m_pCopyFn( Scratch.data(), pLive );
            else                   std::memcpy( Scratch.data(), pLive, pInfo->m_Size );

            if( pInfo->m_ReferenceMode != xecs::component::type::reference_mode::NO_REFERENCES )
            {
                if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::BY_FUNCTION )
                {
                    std::vector<xecs::component::entity*> References;
                    pInfo->m_pReportReferencesFn( References, Scratch.data() );
                    for( auto pRef : References )
                    {
                        std::int64_t Encoded = 0;
                        if( false == details::ResolveReferenceForSave(*this, Scene, *pRef, Encoded) ) { assert(false); Encoded = 0; }
                        *pRef = details::EncodeRef(Encoded);
                    }
                }
                else
                {
                    xproperty::settings::context Context{};
                    std::string                  SetError;
                    xproperty::sprop::collector( Scratch.data(), *pInfo->m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void* ) noexcept
                    {
                        if( Data.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v )
                        {
                            std::int64_t Encoded = 0;
                            if( false == details::ResolveReferenceForSave(*this, Scene, Data.get<xecs::component::entity>(), Encoded) ) { assert(false); Encoded = 0; }
                            Data.get<xecs::component::entity>() = details::EncodeRef(Encoded);
                            xproperty::sprop::setProperty( SetError, Scratch.data(), *pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ pPropertyName, Data }, Context );
                        }
                    });
                }
            }

            const auto Error = details::SerializeOneComponent(TextFile, false, *pInfo, Scratch.data());

            if( pInfo->m_pDestructFn ) pInfo->m_pDestructFn( Scratch.data() );

            if( Error ) return Error;
        }

        Scene.m_LocalToRuntime[Id]            = Entity;
        Scene.m_RuntimeToLocal[Entity.m_Value] = Id;

        return {};
    }

    //-----------------------------------------------------------------------------------------------

    namespace details
    {
        //-----------------------------------------------------------------------------------------
        // Does the actual work of getting a scene to Active (loading it, and recursively any
        // not-yet-resident parents, if needed). Deliberately never touches m_ExplicitRequests -
        // that field belongs exclusively to the public mgr::RequestLoad/ReleaseLoad entry points.
        // A parent pulled in here is tracked purely via the child's own m_DependentSceneCount bump,
        // so this same function serves both the public RequestLoad and the recursive parent-loading
        // step without either one mistaking "I was needed as a dependency" for "someone explicitly
        // asked to keep me resident".
        //-----------------------------------------------------------------------------------------
        inline xerr EnsureLoaded( mgr& Mgr, guid SceneGuid ) noexcept
        {
            auto& Scene = Mgr.FindOrCreate(SceneGuid);

            if( Scene.m_State == state::Active ) return {};

            if( Scene.m_State == state::WaitingForParents || Scene.m_State == state::LoadingEntities )
                return xerr::create<xecs::game_mgr::state::FAILURE, "Scene dependency cycle detected while loading">();

            Scene.m_State = state::WaitingForParents;

            if( auto Err = LoadSceneDescriptor(Mgr, Scene); Err )
            {
                Scene.m_State = state::Failed;
                return Err;
            }

            for( auto& ParentGuid : Scene.m_ParentScenes )
            {
                if( auto Err = EnsureLoaded(Mgr, ParentGuid); Err )
                {
                    // Undo dependent bumps we already made on earlier parents in this same loop.
                    for( auto& AlreadyLoaded : Scene.m_ParentScenes )
                    {
                        if( AlreadyLoaded.m_Value == ParentGuid.m_Value ) break;
                        if( auto* pAlready = Mgr.Find(AlreadyLoaded) )
                        {
                            if( pAlready->m_DependentSceneCount > 0 ) pAlready->m_DependentSceneCount--;
                            UnloadIfIdle(Mgr, *pAlready);
                        }
                    }
                    Scene.m_State = state::Failed;
                    return Err;
                }
                Mgr.Find(ParentGuid)->m_DependentSceneCount++;
            }

            Scene.m_State = state::LoadingEntities;

            const auto Ids = DiscoverEntityIds(Mgr, SceneGuid);
            for( auto Id : Ids )
            {
                if( auto Err = LoadEntity(Mgr, Scene, Id); Err )
                {
                    UnloadSceneEntities(Mgr.m_GameMgr, Scene);
                    for( auto& ParentGuid : Scene.m_ParentScenes )
                    {
                        if( auto* pParent = Mgr.Find(ParentGuid) )
                        {
                            if( pParent->m_DependentSceneCount > 0 ) pParent->m_DependentSceneCount--;
                            UnloadIfIdle(Mgr, *pParent);
                        }
                    }
                    Scene.m_State = state::Failed;
                    return Err;
                }
            }

            Scene.m_ExternalToRuntime.resize( Scene.m_ExternalRefTable.size() );
            for( std::size_t i = 0; i < Scene.m_ExternalRefTable.size(); ++i )
            {
                auto& Addr    = Scene.m_ExternalRefTable[i];
                auto* pParent = Mgr.Find(Addr.m_ParentScene);
                if( pParent == nullptr )
                    return xerr::create<xecs::game_mgr::state::FAILURE, "Scene external reference points at a scene that failed to load">();

                auto It = pParent->m_LocalToRuntime.find(Addr.m_ParentEntity);
                if( It == pParent->m_LocalToRuntime.end() )
                    return xerr::create<xecs::game_mgr::state::FAILURE, "Scene external reference points at a permanent_id that does not exist in the parent scene">();

                Scene.m_ExternalToRuntime[i] = It->second;
            }

            for( auto& Pair : Scene.m_LocalToRuntime )
            {
                RemapLoadedEntityReferences( Mgr.m_GameMgr, Pair.second, [&]( std::int64_t Encoded ) noexcept -> xecs::component::entity
                {
                    if( Encoded == 0 )   return xecs::component::entity{};
                    if( Encoded > 0 )    return Scene.m_LocalToRuntime.at( static_cast<permanent_id>(Encoded) );
                    return Scene.m_ExternalToRuntime.at( static_cast<std::size_t>(-Encoded - 1) );
                });
            }

            Scene.m_State = state::Active;
            return {};
        }
    }

    xerr mgr::RequestLoad( guid SceneGuid ) noexcept
    {
        auto& Scene = FindOrCreate(SceneGuid);
        Scene.m_ExplicitRequests++;

        if( auto Err = details::EnsureLoaded(*this, SceneGuid); Err )
        {
            Scene.m_ExplicitRequests--;
            return Err;
        }
        return {};
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::ReleaseLoad( guid SceneGuid ) noexcept
    {
        auto* pScene = Find(SceneGuid);
        if( pScene == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "ReleaseLoad: scene is not registered">();

        if( pScene->m_ExplicitRequests > 0 ) pScene->m_ExplicitRequests--;

        details::UnloadIfIdle( *this, *pScene );
        return {};
    }
}
