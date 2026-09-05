#include <filesystem>
#include <format>
#include <algorithm>
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
        // On-disk paths - the real GUID-sharded Descriptors/Scene/<b0>/<b1>/<guid>.desc/ convention
        // every other resource type in this engine uses (byte0/byte1 = the low two bytes of the
        // instance guid, folder name = the plain hex instance guid value, matching
        // library_mgr::NewAsset's own path formula exactly, so a Scene created through the normal
        // asset-browser "New" flow and one loaded here resolve to the same folder). entity_db uses
        // the same 2-level byte-sharded convention, keyed off the permanent_id instead of a guid.
        //-----------------------------------------------------------------------------------------
        inline std::wstring FormatHexGuid( std::uint64_t V ) noexcept { return std::format( L"{:X}",   V ); }
        inline std::wstring FormatHex32  ( std::uint32_t V ) noexcept { return std::format( L"{:08X}", V ); }
        inline std::wstring FormatHex8   ( std::uint8_t  V ) noexcept { return std::format( L"{:02X}", V ); }

        inline std::wstring SceneFolder( mgr& Mgr, guid SceneGuid ) noexcept
        {
            const auto Value = SceneGuid.m_Instance.m_Value;
            const auto Byte0 = FormatHex8( static_cast<std::uint8_t>( Value       & 0xFF) );
            const auto Byte1 = FormatHex8( static_cast<std::uint8_t>((Value >> 8) & 0xFF) );
            return Mgr.m_ProjectPath + L"/Descriptors/Scene/" + Byte0 + L"/" + Byte1 + L"/" + FormatHexGuid(Value) + L".desc";
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
        // Scan this scene's entity_db for every "<permanent_id-hex>.entity" file on disk. NOT used by
        // the normal load path any more (EnsureLoaded reads descriptor::m_ActiveEntities instead) -
        // relying on a directory scan meant a deleted entity's file, once left on disk, was
        // rediscovered and silently resurrected on every subsequent load, since nothing recorded that
        // it was no longer valid. Also NOT used by the save-time delete sync (SaveSceneDescriptor uses
        // the explicitly-tracked instance::m_PendingEntityChanges instead - a directory walk on every
        // Save does not scale with entity count). Kept as a repair/recovery tool only (e.g. "list
        // every entity file that exists but isn't in m_ActiveEntities" for a future orphan-sweep/GC
        // pass, to catch drift from a crash before a save, manual file surgery, etc).
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
                    if( E.m_ParentScene == Addr.m_ParentScene && E.m_ParentEntity == Addr.m_ParentEntity )
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
        // Loads the real, reflected Scene descriptor (dependency edges + interned external-ref
        // table) via the same descriptor::base::Serialize every other resource type uses, then
        // copies its fields into the runtime instance. If the file doesn't exist yet (a brand new
        // Scene resource, created via the asset browser but never saved), that's not an error - it
        // just means an empty descriptor (no parents, no entities saved yet).
        //-----------------------------------------------------------------------------------------
        // OutActiveEntities receives the descriptor's authoritative id list - EnsureLoaded loads
        // exactly these entities, not whatever happens to be sitting in entity_db (see
        // DiscoverEntityIds's own comment for why a directory scan is no longer the primary path).
        inline xerr LoadSceneDescriptor( mgr& Mgr, instance& Scene, std::vector<permanent_id>& OutActiveEntities ) noexcept
        {
            const auto Path = DescriptorPath(Mgr, Scene.m_Guid);
            std::error_code Ec;
            if( false == std::filesystem::exists(Path, Ec) || Ec )
            {
                Scene.m_ParentScenes.clear();
                Scene.m_ExternalRefTable.clear();
                Scene.m_Folders.clear();
                OutActiveEntities.clear();
                return {};
            }

            descriptor                   Descriptor;
            xproperty::settings::context Context;
            if( auto Err = Descriptor.Serialize( true, Path, Context ); Err )
                return Err;

            Scene.m_ParentScenes     = std::move(Descriptor.m_ParentScenes);
            Scene.m_ExternalRefTable = std::move(Descriptor.m_ExternalRefTable);
            Scene.m_Folders          = std::move(Descriptor.m_Folders);
            OutActiveEntities        = std::move(Descriptor.m_ActiveEntities);
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

            // A prefab instance's ComponentTypes list always contains EditorPrafabInstance, PLUS any
            // component added only to this instance (the prefab doesn't define it, so it has no
            // default to fall back on and must be fully serialized like an ordinary component) - see
            // SaveEntity's own comment. Every component the prefab DOES define is never listed here,
            // overridden or not. That means which prefab this entity instances - needed to build its
            // full archetype, before CreateEntity below can even run - can be read directly out of
            // EditorPrafabInstance's own "Prefab" field, into a standalone, ordinarily-stack-
            // constructed temporary (no pool/archetype needed yet), rather than duplicating that guid
            // into its own separate record. TempPI is later moved into the real entity once it exists,
            // instead of re-reading the same data a second time.
            const bool bIsPrefabInstance = std::find(Infos.begin(), Infos.end(), &xecs::component::type::info_v<xecs::editor::prefab_instance>) != Infos.end();

            xecs::editor::prefab_instance TempPI;
            xecs::component::entity       PrefabRootEntity{};

            if( bIsPrefabInstance )
            {
                if( auto Err = SerializeOneComponent(TextFile, true, xecs::component::type::info_v<xecs::editor::prefab_instance>, reinterpret_cast<std::byte*>(&TempPI)); Err )
                    return Err;
            }

            std::vector<const xecs::component::type::info*> ArchetypeInfos;
            ArchetypeInfos.reserve(Infos.size() + 4);
            ArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::component::entity> );
            for( auto pInfo : Infos ) ArchetypeInfos.push_back(pInfo);

            if( bIsPrefabInstance )
            {
                const xecs::prefab::guid PrefabGuid = TempPI.m_PrefabInstance;
                if( auto Err = Mgr.m_GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err )
                    return Err;

                auto RootIt = Mgr.m_GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
                if( RootIt == Mgr.m_GameMgr.m_PrefabMgr.m_PrefabList.end() )
                    return xerr::create<xecs::game_mgr::state::FAILURE, "Scene entity's source Prefab failed to resolve after EnsureLoaded">();
                PrefabRootEntity = RootIt->second;

                auto& RootDetails   = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(PrefabRootEntity);
                auto& RootArchetype = *RootDetails.m_pPool->m_pArchetype;
                for( auto pRootInfo : RootArchetype.getDataComponentInfos() )
                {
                    if( pRootInfo == &xecs::component::type::info_v<xecs::component::entity> ) continue;
                    if( std::find(ArchetypeInfos.begin(), ArchetypeInfos.end(), pRootInfo) != ArchetypeInfos.end() ) continue;

                    // A component this instance deliberately REMOVED (a ComponentDiffs entry with
                    // m_bAdded false) must NOT be re-added here just because the prefab still defines
                    // it - that would silently undo the removal on every reload.
                    bool bRemoved = false;
                    for( auto& Diff : TempPI.m_ComponentDiffs )
                        if( Diff.m_bAdded == false && Diff.m_ComponentTypeGuid == pRootInfo->m_Guid.m_Value ) { bRemoved = true; break; }
                    if( bRemoved ) continue;

                    ArchetypeInfos.push_back(pRootInfo);
                }
            }

            auto& Archetype = Mgr.m_GameMgr.getOrCreateArchetype( { ArchetypeInfos.data(), ArchetypeInfos.size() } );

            std::vector<std::byte*> MoveData( ArchetypeInfos.size(), nullptr );
            auto NewEntity = Archetype.CreateEntity( { ArchetypeInfos.data(), ArchetypeInfos.size() }, { MoveData.data(), MoveData.size() } );

            auto& EDetails = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            auto& Pool      = *EDetails.m_pPool;

            // Every component the prefab itself owns starts out as a copy of the prefab's CURRENT
            // value - always, even for a component that's ALSO about to be read from the file below.
            // That's what makes a partial per-component read (SerializeOneComponent's isReading path
            // only ever sets whatever properties the file's own row count lists, never touching
            // anything else - see SerializePartialComponent's own comment) come out correct: a
            // property this entity didn't override needs to already hold the prefab's value before
            // the file read runs, since the file may not mention that property at all. Previously this
            // skipped any component ALSO present in Infos, on the assumption the file always held a
            // complete copy of it - true before per-property (only per-component) overrides existed,
            // no longer true now that a component can appear in Infos with only SOME of its properties
            // actually written.
            if( bIsPrefabInstance )
            {
                auto& RootDetails = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(PrefabRootEntity);
                for( auto pInfo : ArchetypeInfos )
                {
                    if( pInfo == &xecs::component::type::info_v<xecs::component::entity> ) continue;

                    const auto iSrcType = RootDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
                    const auto iDstType = Pool.findIndexComponentFromInfo(*pInfo);
                    if( iSrcType < 0 || iDstType < 0 ) continue;

                    auto pSrc = &RootDetails.m_pPool->m_pComponent[iSrcType][ RootDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
                    auto pDst = &Pool.m_pComponent[iDstType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
                    if( pInfo->m_pCopyFn ) pInfo->m_pCopyFn(pDst, pSrc);
                    else                   std::memcpy(pDst, pSrc, pInfo->m_Size);
                }
            }

            for( auto pInfo : Infos )
            {
                if( pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance> )
                {
                    // TempPI already holds this component's fully-parsed data (read once, above,
                    // before the archetype even existed, and always first - SaveEntity guarantees it)
                    // - move it into the real pool slot instead of reading the same block a second
                    // time (there's nothing left to read for it anyway; the stream has already moved
                    // past it).
                    Pool.getComponent<xecs::editor::prefab_instance>(EDetails.m_PoolIndex) = std::move(TempPI);
                    continue;
                }

                // Anything else reaching here is either a plain entity's ordinary component, or a
                // prefab instance's own "added only to this instance" component (no prefab default
                // exists for it, so it's fully serialized exactly like an ordinary one) - same read
                // either way.
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

            // Apply each override's own PropertyValueAsString on top of the prefab defaults just
            // copied above - this IS the actual override value now (SaveEntity no longer writes the
            // owning component's own data for an OVERRIDES-type entry at all, see its own comment),
            // so without this step every overridden property would silently read back as the prefab's
            // plain default. StringToAny needs the property's current type GUID, which this reads via
            // a live collector pass on the (already-defaulted) owner rather than storing it - the
            // owner's reflected type for a given path cannot itself have changed between save and
            // load within one run of the program.
            if( bIsPrefabInstance )
            {
                const auto iPIType = Pool.findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> );
                if( iPIType >= 0 )
                {
                    auto& PI = *reinterpret_cast<xecs::editor::prefab_instance*>( &Pool.m_pComponent[iPIType][ EDetails.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size ] );
                    for( auto& CompOverride : PI.m_lComponents )
                    {
                        auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{CompOverride.m_ComponentTypeGuid} );
                        if( pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr ) continue;

                        const auto iOwnerType = Pool.findIndexComponentFromInfo(*pOwnerInfo);
                        if( iOwnerType < 0 ) continue;
                        auto* pOwnerData = &Pool.m_pComponent[iOwnerType][ EDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size ];

                        for( auto& PropOverride : CompOverride.m_PropertyOverrides )
                        {
                            xproperty::settings::context Context{};
                            std::uint32_t                 TypeGuid = 0;
                            xproperty::sprop::collector( pOwnerData, *pOwnerInfo->m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Value, const xproperty::type::members&, bool, const void* ) noexcept
                            {
                                if( PropOverride.m_PropertyName == pPropertyName && Value.m_pType )
                                    TypeGuid = Value.m_pType->m_GUID;
                            });
                            if( TypeGuid == 0 ) continue;

                            // std::string's own buffer, not a manually null-terminated copy: some
                            // StringToAny cases read String.data() as a null-terminated C-string
                            // (stol/atoi/...), but the std::string case uses String.size() directly -
                            // std::string::data() is guaranteed null-terminated since C++11 while
                            // .size() still excludes that terminator, satisfying both without an
                            // appended '\0' silently becoming part of the parsed string's OWN content.
                            xproperty::any ParsedValue;
                            std::string    ValueBuffer = PropOverride.m_PropertyValueAsString;
                            if( xproperty::settings::StringToAny(ParsedValue, TypeGuid, std::span<char>(ValueBuffer.data(), ValueBuffer.size())) == false ) continue;

                            std::string SetError;
                            xproperty::sprop::setProperty( SetError, pOwnerData, *pOwnerInfo->m_pPropertyTable, xproperty::sprop::container::prop{ PropOverride.m_PropertyName, ParsedValue }, Context );
                        }
                    }
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
            if( Up->m_Guid == SceneGuid )
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

    void mgr::MarkEntityNew( guid SceneGuid, permanent_id Id ) noexcept
    {
        FindOrCreate(SceneGuid).m_PendingChanges[Id].m_New += 1;
    }

    void mgr::MarkEntityDirty( guid SceneGuid, permanent_id Id ) noexcept
    {
        FindOrCreate(SceneGuid).m_PendingChanges[Id].m_Dirty += 1;
    }

    void mgr::MarkEntityDeleted( guid SceneGuid, permanent_id Id ) noexcept
    {
        FindOrCreate(SceneGuid).m_PendingChanges[Id].m_Deleted += 1;
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::SaveSceneDescriptor( guid SceneGuid ) noexcept
    {
        auto* pScene = Find(SceneGuid);
        if( pScene == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "SaveSceneDescriptor: scene is not registered - call FindOrCreate first">();

        std::error_code Ec;
        std::filesystem::create_directories( std::filesystem::path(details::EntityDbFolder(*this, SceneGuid)), Ec );

        descriptor Descriptor;
        Descriptor.m_ParentScenes     = pScene->m_ParentScenes;
        Descriptor.m_ExternalRefTable = pScene->m_ExternalRefTable;
        Descriptor.m_Folders          = pScene->m_Folders;

        // The authoritative "these ids are still valid" list the next load will read (see
        // descriptor::m_ActiveEntities's own comment) - exactly the currently-live entities, sorted
        // for a stable, diffable file rather than unordered_map's arbitrary iteration order.
        Descriptor.m_ActiveEntities.reserve(pScene->m_LocalToRuntime.size());
        for( auto& Pair : pScene->m_LocalToRuntime )
            Descriptor.m_ActiveEntities.push_back(Pair.first);
        std::sort(Descriptor.m_ActiveEntities.begin(), Descriptor.m_ActiveEntities.end());

        xproperty::settings::context Context;
        return Descriptor.Serialize( false, details::DescriptorPath(*this, SceneGuid), Context );
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::SaveScene( guid SceneGuid ) noexcept
    {
        auto* pScene = Find(SceneGuid);
        if( pScene == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "SaveScene: scene is not registered - call FindOrCreate first">();

        // Reconcile disk with the live scene HERE, at save time - not the moment an entity is created/
        // edited/deleted in the editor. On-disk state only ever changes as this one deliberate "sync
        // up with current state" step, never as a side effect of editing. Resolved from
        // m_PendingChanges (explicitly maintained by MarkEntityNew/MarkEntityDirty/MarkEntityDeleted)
        // rather than a directory scan (doesn't scale with entity count) or rewriting every live
        // entity (wastes IO proportional to the whole scene instead of to what actually changed).
        for( auto& [Id, Change] : pScene->m_PendingChanges )
        {
            const bool bNew     = Change.m_New     > 0;
            const bool bDirty   = Change.m_Dirty   > 0;
            const bool bDeleted = Change.m_Deleted > 0;

            if( bDeleted )
            {
                if( bNew ) continue; // created and deleted before ever being saved - never touched disk, nothing to do

                std::error_code DelEc;
                const auto      Path = std::filesystem::path( details::EntityPath(*this, SceneGuid, Id) );
                std::filesystem::remove( Path, DelEc );
                std::printf( "[SaveScene] removed deleted entity file Id=%u (%s)\n", Id, DelEc ? DelEc.message().c_str() : "ok" );
                std::fflush(stdout);
                continue;
            }

            if( !bNew && !bDirty ) continue; // net-zero counters (e.g. undo cancelled everything) - nothing pending after all

            auto It = pScene->m_LocalToRuntime.find(Id);
            if( It == pScene->m_LocalToRuntime.end() ) continue; // defensive - shouldn't happen without a matching Deleted mark

            // New: file must NOT already exist - NextFreeEntityId's own collision retry already makes
            // a real collision extremely unlikely, so finding one here means something is actually
            // wrong, not a live expectation. Dirty: file SHOULD already exist - its absence means the
            // entity's creation was never marked (a MarkEntityNew call got missed), not that anything
            // about the edit itself is wrong. Neither is fatal - just logged, so a real bug doesn't
            // get silently masked.
            std::error_code ExistsEc;
            const bool      bFileExists = std::filesystem::exists( std::filesystem::path( details::EntityPath(*this, SceneGuid, Id) ), ExistsEc );
            if( bNew && bFileExists )
            {
                std::printf( "[SaveScene] WARNING: Id=%u marked NEW but its file already exists on disk (id collision?)\n", Id );
                std::fflush(stdout);
            }
            else if( !bNew && bDirty && !bFileExists )
            {
                std::printf( "[SaveScene] WARNING: Id=%u marked DIRTY but no file exists on disk yet (missed MarkEntityNew?)\n", Id );
                std::fflush(stdout);
            }

            if( auto Err = SaveEntity(SceneGuid, Id, It->second); Err ) return Err;
        }
        pScene->m_PendingChanges.clear();

        // Writes the descriptor, including a freshly-recomputed m_ActiveEntities - see its own comment.
        return SaveSceneDescriptor(SceneGuid);
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::SaveEntity( guid SceneGuid, permanent_id Id, xecs::component::entity Entity ) noexcept
    {
        std::printf("[SaveEntity] Id=%u Entity=0x%llx : begin\n", Id, static_cast<unsigned long long>(Entity.m_Value));
        std::fflush(stdout);

        auto& Scene = FindOrCreate(SceneGuid);

        auto& EDetails  = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& Archetype = *EDetails.m_pPool->m_pArchetype;
        auto  DataSpan  = Archetype.getDataComponentInfos();

        std::printf("[SaveEntity] Id=%u : archetype has %zu data components:", Id, DataSpan.size());
        for( auto pInfo : DataSpan ) std::printf(" %s", pInfo->m_pName);
        std::printf("\n");
        std::fflush(stdout);

        // A prefab instance's live component data always holds the current value (default-from-
        // prefab, or overridden - see xecs_editor.h's own design comment), but only the OVERRIDDEN
        // subset is this entity's own data: a component with zero recorded overrides is 100%
        // resolvable from the prefab, so writing it here would just be a stale duplicate that can
        // silently drift from the prefab (the real source of truth) the moment the prefab changes.
        // LoadEntity's counterpart re-derives every component skipped here from the prefab's own
        // current value at load time.
        const xecs::editor::prefab_instance* pPI = nullptr;
        // A GameMgr that never registered xecs::editor::prefab_instance (any consumer that doesn't
        // use E29's prefab feature - e.g. smoke_test_scene.cpp) leaves its m_BitID at
        // invalid_bit_id_v; calling getBit() with that raw value trips bits::getBit's own range
        // assert, so the registration check must come first.
        if( xecs::component::type::info_v<xecs::editor::prefab_instance>.m_BitID != xecs::component::type::info::invalid_bit_id_v
         && Archetype.getComponentBits().getBit(xecs::component::type::info_v<xecs::editor::prefab_instance>.m_BitID) )
        {
            const auto iType = EDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>);
            assert(iType >= 0);
            pPI = reinterpret_cast<const xecs::editor::prefab_instance*>(&EDetails.m_pPool->m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size ]);
            std::printf("[SaveEntity] Id=%u : is a prefab instance, m_lComponents.size()=%zu\n", Id, pPI->m_lComponents.size());
            std::fflush(stdout);
        }

        // The prefab's OWN component set (guids only) - what tells us, for a prefab instance, which
        // of its CURRENT components are "inherited" (the prefab defines them too - fully
        // reconstructable from prefab defaults + overrides, never needs its own data section) versus
        // "added only here" (the prefab has no such component at all, so there's no default to derive
        // from - needs a real, full write, same as any ordinary component). Computed fresh every
        // save, not trusted from some incrementally-maintained bookkeeping list, for the same reason
        // PropertyValueAsString is refreshed fresh below rather than trusted from OnPropertyChanged's
        // cache: comparing live data against the prefab's live data can never drift out of sync with
        // reality the way hand-maintained state can.
        std::vector<std::uint64_t> PrefabOwnedGuids;
        if( pPI != nullptr )
        {
            if( auto Err = m_GameMgr.m_PrefabMgr.EnsureLoaded(pPI->m_PrefabInstance); Err )
                std::printf("[SaveEntity] Id=%u : WARNING failed to load source prefab to diff components (%s)\n", Id, std::string(Err.getMessage()).c_str());
            else if( auto RootIt = m_GameMgr.m_PrefabMgr.m_PrefabList.find(pPI->m_PrefabInstance.m_Instance.m_Value); RootIt != m_GameMgr.m_PrefabMgr.m_PrefabList.end() )
            {
                auto& RootDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(RootIt->second);
                for( auto pRootInfo : RootDetails.m_pPool->m_pArchetype->getDataComponentInfos() )
                {
                    // The root entity carries xecs::prefab::root (its own bookkeeping, marking IT as
                    // a prefab's root definition) in addition to the components it's actually sharing
                    // with instances - instances never have this component themselves, so counting it
                    // as "prefab-owned" would make every instance falsely diff it as "removed".
                    if( pRootInfo == &xecs::component::type::info_v<xecs::prefab::root> ) continue;
                    PrefabOwnedGuids.push_back(pRootInfo->m_Guid.m_Value);
                }
            }
        }

        std::vector<const xecs::component::type::info*> Infos;
        Infos.reserve(DataSpan.size());
        for( auto pInfo : DataSpan )
        {
            if( pInfo == &xecs::component::type::info_v<xecs::component::entity> ) continue;

            // A component the prefab ALSO defines is fully reconstructable from the prefab's own
            // defaults plus EditorPrafabInstance's own PropertyName/PropertyValue override pairs (see
            // the refresh right before writing its data below), whether or not any of its properties
            // are actually overridden - so it never needs its own data section written here, that
            // would just be the same values a second time. A component the prefab does NOT define
            // (added only to this instance) has no such fallback and must be written in full, exactly
            // like a component on a plain, non-prefab entity.
            if( pPI != nullptr && pInfo != &xecs::component::type::info_v<xecs::editor::prefab_instance>
             && std::find(PrefabOwnedGuids.begin(), PrefabOwnedGuids.end(), pInfo->m_Guid.m_Value) != PrefabOwnedGuids.end() )
                continue;

            Infos.push_back(pInfo);
        }

        // EditorPrafabInstance must be first, if present - LoadEntity reads it via a standalone parse
        // before the entity's archetype/pool memory even exists (it needs the prefab guid inside to
        // build that archetype in the first place - see LoadEntity's own comment), so its xProperties
        // block must be the first one in the file regardless of where DataSpan's own iteration order
        // happened to put it (only relevant now that an "added" component can also appear in Infos).
        if( auto It = std::find(Infos.begin(), Infos.end(), &xecs::component::type::info_v<xecs::editor::prefab_instance>); It != Infos.end() && It != Infos.begin() )
            std::iter_swap(Infos.begin(), It);

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

        // No separate "which prefab" record: for a prefab instance, Infos is always exactly
        // {EditorPrafabInstance}, and that component's own "Prefab" field already carries this same
        // guid - LoadEntity reads it directly out of that block instead (into a standalone temporary,
        // before the archetype exists), so writing it twice here would just be the same value again.

        std::printf("[SaveEntity] Id=%u : writing %zu component(s):", Id, Infos.size());
        for( auto pInfo : Infos ) std::printf(" %s", pInfo->m_pName);
        std::printf("\n");
        std::fflush(stdout);

        for( auto pInfo : Infos )
        {
            std::printf("[SaveEntity] Id=%u : component '%s' - resolving pool index\n", Id, pInfo->m_pName);
            std::fflush(stdout);

            const auto iType = EDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
            assert(iType >= 0);
            auto pLive = &EDetails.m_pPool->m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

            std::printf("[SaveEntity] Id=%u : component '%s' - copying to scratch (size=%u, hasCopyFn=%d)\n", Id, pInfo->m_pName, pInfo->m_Size, pInfo->m_pCopyFn != nullptr);
            std::fflush(stdout);

            // std::vector<std::byte>(Size) only zero-fills raw bytes - it never runs T's real
            // constructor. For a purely-POD component (m_pCopyFn == nullptr, plain memcpy below) that's
            // fine. But m_pCopyFn's body is `*reinterpret_cast<T*>(p1) = *reinterpret_cast<const
            // T*>(p2)` - a real operator= call - and for any T containing a std::vector (like
            // xecs::editor::prefab_instance's m_lComponents), assigning INTO memory that was never
            // actually constructed is undefined behavior: MSVC's debug STL vector keeps a per-container
            // "proxy" object that only a genuine constructor sets up, and zeroed-but-never-constructed
            // memory doesn't have one - manifesting as an "invalidated vector iterator" assert the
            // moment something walks the resulting (miscopied) vector, exactly the crash this was
            // chasing. Constructing Scratch first - the same thing pool::instance::Append() already
            // does for every real pool slot - makes m_pCopyFn's operator= call operate on a genuinely
            // live object, matching what every other copy-into-a-pool-slot call site in this codebase
            // already assumes.
            std::vector<std::byte> Scratch( pInfo->m_Size );
            if( pInfo->m_pConstructFn ) pInfo->m_pConstructFn( Scratch.data() );
            if( pInfo->m_pCopyFn ) pInfo->m_pCopyFn( Scratch.data(), pLive );
            else                   std::memcpy( Scratch.data(), pLive, pInfo->m_Size );

            std::printf("[SaveEntity] Id=%u : component '%s' - copied, checking references\n", Id, pInfo->m_pName);
            std::fflush(stdout);

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

            // Refresh prefab_instance's own bookkeeping from the LIVE archetype/component data right
            // before writing it below - it must be the save's own, always-current source of truth,
            // not whatever incrementally-maintained state a live editing session happened to leave
            // behind (an override entry loaded from an older save and never re-touched since would
            // otherwise round-trip with a stale or empty value; a component removed mid-session would
            // otherwise leave a dangling override entry for something that no longer exists at all).
            if( pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance> )
            {
                auto& PI_Scratch = *reinterpret_cast<xecs::editor::prefab_instance*>(Scratch.data());

                // ComponentDiffs: added = on this entity but not in the prefab's own set; removed =
                // in the prefab's own set but not on this entity. Recomputed from scratch every save.
                PI_Scratch.m_ComponentDiffs.clear();
                for( auto pInfo2 : DataSpan )
                {
                    if( pInfo2 == &xecs::component::type::info_v<xecs::component::entity> ) continue;
                    if( pInfo2 == &xecs::component::type::info_v<xecs::editor::prefab_instance> ) continue;
                    if( std::find(PrefabOwnedGuids.begin(), PrefabOwnedGuids.end(), pInfo2->m_Guid.m_Value) == PrefabOwnedGuids.end() )
                        PI_Scratch.m_ComponentDiffs.push_back(xecs::editor::prefab_component_diff{ .m_ComponentTypeGuid = pInfo2->m_Guid.m_Value, .m_bAdded = true });
                }
                for( auto PrefabGuidValue : PrefabOwnedGuids )
                {
                    bool bStillPresent = false;
                    for( auto pInfo2 : DataSpan )
                        if( pInfo2->m_Guid.m_Value == PrefabGuidValue ) { bStillPresent = true; break; }
                    if( bStillPresent == false )
                        PI_Scratch.m_ComponentDiffs.push_back(xecs::editor::prefab_component_diff{ .m_ComponentTypeGuid = PrefabGuidValue, .m_bAdded = false });
                }

                // A property-level override only makes sense for a component the prefab actually
                // defines (that's the whole point - tracking which of the prefab's own properties
                // this instance changed); prune any entry for a component that's since been removed
                // entirely, or that was never prefab-defined in the first place.
                std::erase_if( PI_Scratch.m_lComponents, [&]( auto& C ) noexcept
                {
                    return std::find(PrefabOwnedGuids.begin(), PrefabOwnedGuids.end(), C.m_ComponentTypeGuid) == PrefabOwnedGuids.end();
                });

                for( auto& CompOverride : PI_Scratch.m_lComponents )
                {
                    auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{CompOverride.m_ComponentTypeGuid} );
                    if( pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr ) continue;

                    const auto iOwnerType = EDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
                    if( iOwnerType < 0 ) continue;
                    auto* pOwnerLive = &EDetails.m_pPool->m_pComponent[iOwnerType][ EDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size ];

                    for( auto& PropOverride : CompOverride.m_PropertyOverrides )
                    {
                        xproperty::settings::context ValueContext{};
                        xproperty::sprop::collector( pOwnerLive, *pOwnerInfo->m_pPropertyTable, ValueContext, [&]( const char* pPropertyName, xproperty::any&& Value, const xproperty::type::members&, bool, const void* ) noexcept
                        {
                            if( PropOverride.m_PropertyName != pPropertyName ) return;
                            std::array<char, 256> ValueBuffer{};
                            const auto             ValueLen = xproperty::settings::AnyToString(ValueBuffer, Value);
                            PropOverride.m_PropertyValueAsString.assign(ValueBuffer.data(), ValueLen > 0 ? static_cast<std::size_t>(ValueLen) : 0);
                        });
                    }
                }
            }

            std::printf("[SaveEntity] Id=%u : component '%s' - calling SerializeOneComponent\n", Id, pInfo->m_pName);
            std::fflush(stdout);

            const auto Error = details::SerializeOneComponent(TextFile, false, *pInfo, Scratch.data());

            std::printf("[SaveEntity] Id=%u : component '%s' - SerializeOneComponent returned, destructing scratch\n", Id, pInfo->m_pName);
            std::fflush(stdout);

            if( pInfo->m_pDestructFn ) pInfo->m_pDestructFn( Scratch.data() );

            std::printf("[SaveEntity] Id=%u : component '%s' - done\n", Id, pInfo->m_pName);
            std::fflush(stdout);

            if( Error ) return Error;
        }

        Scene.m_LocalToRuntime[Id]            = Entity;
        Scene.m_RuntimeToLocal[Entity.m_Value] = Id;

        std::printf("[SaveEntity] Id=%u : end\n", Id);
        std::fflush(stdout);

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

            std::vector<permanent_id> ActiveEntities;
            if( auto Err = LoadSceneDescriptor(Mgr, Scene, ActiveEntities); Err )
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
                        if( AlreadyLoaded == ParentGuid ) break;
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

            for( auto Id : ActiveEntities )
            {
                if( auto Err = LoadEntity(Mgr, Scene, Id); Err )
                {
                    // A single corrupted/unreadable entity file (e.g. left truncated by a crash
                    // mid-save - Save is not yet crash-atomic) must not take the WHOLE scene down
                    // with it: skip it and keep going, rather than failing every other entity too and
                    // leaving the scene permanently unopenable until someone manually edits the
                    // descriptor. Anything genuinely referencing this id (Parent/Children, an
                    // external-ref) will simply fail its own reference-remap below with a clear
                    // error, rather than silently loading a wrong entity.
                    std::printf("[Scene::EnsureLoaded] WARNING: entity Id=%u failed to load (%s) - skipping it, scene will load without it\n", Id, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
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
