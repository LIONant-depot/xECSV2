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
        // EncodeRef/DecodeRef, ResolveReferenceForSave, RemapLoadedEntityReferences, and
        // SerializeOneComponent moved to xecs::persist::details (xecs_reference_remap_inline.h) -
        // they were already container-agnostic, just physically homed here; xecs::prefab's own
        // multi-entity group Save/Load now needs the exact same logic. Scene's own resolver for
        // ResolveReferenceForSave (same-scene -> positive local permanent_id; a declared parent's
        // entity -> interned/reused negative external-table key; anything else rejected, per the
        // spec that a strong cross-level reference may only target the same scene or a declared
        // parent) is now a local lambda at each SaveEntity call site instead of a free function.
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

            // Detects whether this is a prefab instance (Infos contains EditorPrafabInstance), reads
            // it standalone into TempPI if so, and unions the prefab root's own components into
            // ArchetypeInfos (minus anything ComponentDiffs marks removed) - see
            // xecs::persist::details::DetectAndUnionPrefabInstance's own comment. PrefabRootEntity
            // stays invalid when this isn't a prefab instance at all.
            std::vector<const xecs::component::type::info*> ArchetypeInfos;
            xecs::editor::prefab_instance                    TempPI;
            xecs::component::entity                          PrefabRootEntity{};
            if( auto Err = xecs::persist::details::DetectAndUnionPrefabInstance( Mgr.m_GameMgr, TextFile, Infos, ArchetypeInfos, TempPI, PrefabRootEntity ); Err )
            {
                std::printf("[Scene::LoadEntity] Id=%u : DetectAndUnionPrefabInstance failed (%s)\n", FileId, std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
            const bool bIsPrefabInstance = PrefabRootEntity.isValid();

            auto& Archetype = Mgr.m_GameMgr.getOrCreateArchetype( { ArchetypeInfos.data(), ArchetypeInfos.size() } );

            std::vector<std::byte*> MoveData( ArchetypeInfos.size(), nullptr );
            auto NewEntity = Archetype.CreateEntity( { ArchetypeInfos.data(), ArchetypeInfos.size() }, { MoveData.data(), MoveData.size() } );

            auto& EDetails = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            auto& Pool      = *EDetails.m_pPool;

            if( bIsPrefabInstance )
                xecs::persist::details::CopyPrefabInstanceDefaults( Mgr.m_GameMgr, PrefabRootEntity, NewEntity, ArchetypeInfos );

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

                if( auto Err = xecs::persist::details::SerializeOneComponent(TextFile, true, *pInfo, pData); Err )
                {
                    std::printf("[Scene::LoadEntity] Id=%u : component '%s' failed to read (%s)\n", FileId, pInfo->m_pName, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    auto E = NewEntity;
                    Mgr.m_GameMgr.DeleteEntity(E);
                    return Err;
                }
            }

            // ApplyPrefabInstancePropertyOverrides is NOT called here anymore - see EnsureLoaded's
            // own third pass, after the whole scene's reference remap. An override whose
            // m_MemberPath is non-empty needs to walk THIS entity's own children.m_List
            // (ResolveMemberPath) to find its real target - but right here, immediately after this
            // one entity's own per-component read loop, children.m_List (like every other reference-
            // bearing field - see children::ReportReferences) still holds RAW, un-remapped encoded
            // values, not real xecs::component::entity handles - those only become valid once
            // RemapLoadedEntityReferences has run for every entity in the scene, which happens in a
            // separate pass AFTER this whole per-entity loop finishes. Calling it this early treated
            // encoded garbage as a live entity handle and crashed hard (no assert, just a silent
            // exit) - direct user report, confirmed by tracing exactly how far the load log got
            // before the process died.

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

        // Sorted by (tree depth, then name) - flat/"greedy" by depth, NOT nested/pre-order - for the
        // same "stable, diffable file" reason m_ActiveEntities is sorted below, with one more
        // git-merge-specific property: re-parenting a folder to a DIFFERENT parent at the SAME depth
        // only changes that one row's own Parent field, not its position in the file - a nested/
        // pre-order sort would instead shift the folder's (and its whole subtree's) position too,
        // turning a one-line semantic change into a large diff. Direct user request.
        {
            std::unordered_map<xecs::scene::folder_id, int> Depths;
            for( auto& F : Descriptor.m_Folders )
            {
                auto Id = F.m_Id;
                int  Depth = 0;
                for(;;)
                {
                    auto It = std::find_if(Descriptor.m_Folders.begin(), Descriptor.m_Folders.end(), [&](auto& F2) noexcept { return F2.m_Id == Id; });
                    if( It == Descriptor.m_Folders.end() || It->m_Parent == xecs::scene::invalid_folder_id_v ) break;
                    Id = It->m_Parent;
                    ++Depth;
                }
                Depths[F.m_Id] = Depth;
            }
            std::sort( Descriptor.m_Folders.begin(), Descriptor.m_Folders.end(), [&](auto& A, auto& B) noexcept
            {
                const auto DepthA = Depths[A.m_Id];
                const auto DepthB = Depths[B.m_Id];
                if( DepthA != DepthB ) return DepthA < DepthB;
                return A.m_Name < B.m_Name;
            });
        }

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

            std::printf( "[SaveScene] Id=%u pending: New=%d Dirty=%d Deleted=%d\n", Id, Change.m_New, Change.m_Dirty, Change.m_Deleted );
            std::fflush(stdout);

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
        auto& Scene = FindOrCreate(SceneGuid);

        auto& EDetails  = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& Archetype = *EDetails.m_pPool->m_pArchetype;
        auto  DataSpan  = Archetype.getDataComponentInfos();

        std::printf("[SaveEntity] Id=%u Entity.m_Value=%llu DataSpan (%zu):", Id, (unsigned long long)Entity.m_Value, DataSpan.size());
        for( auto pInfo : DataSpan ) std::printf(" %s", pInfo->m_pName);
        std::printf("\n"); std::fflush(stdout);

        // Computes PrefabOwnedGuids (what's 100% reconstructable from the prefab, so shouldn't get
        // its own data section here) and Infos (what actually needs writing) - see
        // xecs::persist::details::ComputePrefabInstanceSaveOverlay's own comment.
        std::vector<const xecs::component::type::info*> Infos;
        std::vector<std::uint64_t>                       PrefabOwnedGuids;
        xecs::persist::details::ComputePrefabInstanceSaveOverlay( m_GameMgr, Entity, DataSpan, Infos, PrefabOwnedGuids );

        std::printf("[SaveEntity] Id=%u Infos to write (%zu):", Id, Infos.size());
        for( auto pInfo : Infos ) std::printf(" %s", pInfo->m_pName);
        std::printf("\n"); std::fflush(stdout);

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

        // Same-scene, positive local-id resolver, plus a declared parent's entity as an interned
        // negative external-table key - the container-specific half of ResolveReferenceForSave, see
        // its own comment for why this stays a local lambda per-container instead of a free function.
        auto SceneResolve = [&]( xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept -> bool
        {
            if( auto It = Scene.m_RuntimeToLocal.find(Target.m_Value); It != Scene.m_RuntimeToLocal.end() )
            {
                OutEncoded = static_cast<std::int64_t>(It->second);
                return true;
            }

            for( auto& ParentGuid : Scene.m_ParentScenes )
            {
                auto* pParent = Find(ParentGuid);
                if( pParent == nullptr ) continue;

                auto It2 = pParent->m_RuntimeToLocal.find(Target.m_Value);
                if( It2 == pParent->m_RuntimeToLocal.end() ) continue;

                const external_entity_address Addr{ ParentGuid, It2->second };
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
        };

        for( auto pInfo : Infos )
        {
            const auto iType = EDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
            assert(iType >= 0);
            auto pLive = &EDetails.m_pPool->m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

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

            if( pInfo->m_ReferenceMode != xecs::component::type::reference_mode::NO_REFERENCES )
            {
                if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::BY_FUNCTION )
                {
                    std::vector<xecs::component::entity*> References;
                    pInfo->m_pReportReferencesFn( References, Scratch.data() );
                    for( auto pRef : References )
                    {
                        std::int64_t Encoded = 0;
                        if( false == xecs::persist::details::ResolveReferenceForSave(*pRef, Encoded, SceneResolve) )
                        {
                            // Debug: loud - this is an authoring-time mistake worth catching. Release:
                            // fail gracefully rather than crash a shipped game over a dangling ref.
                            xassert(false);
                            std::printf("[SaveEntity] WARNING: reference target not resolvable (not same-scene or a declared parent) - encoding as null\n");
                            std::fflush(stdout);
                            Encoded = 0;
                        }
                        *pRef = xecs::persist::details::EncodeRef(Encoded);
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
                            if( false == xecs::persist::details::ResolveReferenceForSave(Data.get<xecs::component::entity>(), Encoded, SceneResolve) )
                            {
                                xassert(false);
                                std::printf("[SaveEntity] WARNING: reference target not resolvable (not same-scene or a declared parent) - encoding as null\n");
                                std::fflush(stdout);
                                Encoded = 0;
                            }
                            Data.get<xecs::component::entity>() = xecs::persist::details::EncodeRef(Encoded);
                            xproperty::sprop::setProperty( SetError, Scratch.data(), *pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ pPropertyName, Data }, Context );
                        }
                    });
                }
            }

            if( pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance> )
            {
                auto& PI_Scratch = *reinterpret_cast<xecs::editor::prefab_instance*>(Scratch.data());
                xecs::persist::details::RefreshPrefabInstanceOverlayRecord( m_GameMgr, Entity, DataSpan, PrefabOwnedGuids, PI_Scratch );
            }

            const auto Error = xecs::persist::details::SerializeOneComponent(TextFile, false, *pInfo, Scratch.data());

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
                xecs::persist::details::RemapLoadedEntityReferences( Mgr.m_GameMgr, Pair.second, [&]( std::int64_t Encoded ) noexcept -> xecs::component::entity
                {
                    if( Encoded == 0 )   return xecs::component::entity{};

                    // .at() would throw std::out_of_range (an uncaught exception -> hard crash, no
                    // diagnostic) if the encoded target's id isn't actually resident - a real
                    // possibility any time some OTHER entity failed to load (a corrupted/truncated
                    // file, or - concretely, what actually surfaced this - a prefab instance whose
                    // source prefab is in an old/incompatible on-disk format) and got skipped by
                    // EnsureLoaded's own per-entity error tolerance (see its own comment: "a single
                    // corrupted entity file must not take the whole scene down"). A dangling reference
                    // into a hole like that should degrade to null, not bring down the entire load.
                    if( Encoded > 0 )
                    {
                        auto It = Scene.m_LocalToRuntime.find( static_cast<permanent_id>(Encoded) );
                        if( It == Scene.m_LocalToRuntime.end() )
                        {
                            std::printf("[Scene::EnsureLoaded] WARNING: a loaded entity references permanent_id=%lld, which failed to load or doesn't exist - encoding as null\n", static_cast<long long>(Encoded));
                            std::fflush(stdout);
                            return xecs::component::entity{};
                        }
                        return It->second;
                    }

                    const auto ExtIndex = static_cast<std::size_t>(-Encoded - 1);
                    if( ExtIndex >= Scene.m_ExternalToRuntime.size() )
                    {
                        std::printf("[Scene::EnsureLoaded] WARNING: a loaded entity references external-ref index=%zu, out of range (table size=%zu) - encoding as null\n", ExtIndex, Scene.m_ExternalToRuntime.size());
                        std::fflush(stdout);
                        return xecs::component::entity{};
                    }
                    return Scene.m_ExternalToRuntime[ExtIndex];
                });
            }

            // Third pass, after every entity is loaded AND every reference (parent/children included)
            // has been remapped to a real, live entity handle: NOW it's safe to apply each prefab
            // instance's own recorded property overrides, since an override with a non-empty
            // m_MemberPath needs to walk a REAL children.m_List to find its target - see the removed
            // call's own comment in LoadEntity above for why doing this any earlier crashed.
            for( auto& Pair : Scene.m_LocalToRuntime )
            {
                auto& Details = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
                if( Details.m_pPool && Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0 )
                    xecs::persist::details::ApplyPrefabInstancePropertyOverrides( Mgr.m_GameMgr, Pair.second );
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
