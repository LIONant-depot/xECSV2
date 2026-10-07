#include <filesystem>
#include <format>
#include <algorithm>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>

// xproperty::sprop::serializer::Stream (the xtextfile-backed whole-object property serializer) is
// the same modern replacement SerializeGameState uses for BY_PROPERTIES components - see the note
// at the top of xecs_game_mgr.cpp. Not pulled in transitively by xecs.h, so it needs its own include
// here too, matching that file's exact relative path (this file lives in the same details/ folder).
#include "../../../xproperty/source/sprop/property_sprop_xtextfile_serializer.h"

// A scene that is the document of a Prefab Editor (instance::m_bPrefabDocument) reads and writes its descriptor, and is saved, as a prefab: xecs_prefab_mgr_inline.h, included after this file.
namespace xecs::prefab::document
{
    inline xerr LoadDescriptor( xecs::scene::mgr&, xecs::scene::instance&, std::vector<xecs::scene::permanent_id>& ) noexcept;
    inline xerr Save          ( xecs::scene::mgr&, xecs::scene::instance& ) noexcept;
}

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
        inline std::wstring FormatHex8   ( std::uint8_t  V ) noexcept { return std::format( L"{:02X}", V ); }

        // ProjectPath-only overloads (below) are what a background thread must use - see
        // DiscoverEntityIds' own comment for why capturing mgr& itself across a detached thread
        // boundary is unsafe (Stop/hot-reload can destroy the whole GameMgr while a scan is still
        // in flight). The mgr&-taking overloads stay as thin, zero-cost wrappers so every existing
        // synchronous caller (DescriptorPath, EntityPath, SaveSceneDescriptor's create_directories
        // call) needs no changes at all.
        inline std::wstring SceneFolder( std::wstring_view ProjectPath, guid SceneGuid ) noexcept
        {
            const auto Value = SceneGuid.m_Instance.m_Value;
            const auto Byte0 = FormatHex8( static_cast<std::uint8_t>( Value       & 0xFF) );
            const auto Byte1 = FormatHex8( static_cast<std::uint8_t>((Value >> 8) & 0xFF) );
            return std::wstring(ProjectPath) + L"/Descriptors/Scene/" + Byte0 + L"/" + Byte1 + L"/" + FormatHexGuid(Value) + L".desc";
        }

        // The folder of a prefab (Descriptors/Prefab/<b0>/<b1>/<guid>.desc): a prefab is stored as a scene (prefabs_plan.md, phase 1), so a prefab document is read and written
        // like a scene, from here. The same path formula xecs::prefab::details::PrefabFolder has.
        inline std::wstring PrefabFolderOf( std::wstring_view ProjectPath, std::uint64_t Value ) noexcept
        {
            return std::wstring(ProjectPath) + L"/Descriptors/Prefab/" + FormatHex8( static_cast<std::uint8_t>( Value & 0xFF) ) + L"/" + FormatHex8( static_cast<std::uint8_t>((Value >> 8) & 0xFF) ) + L"/" + FormatHexGuid(Value) + L".desc";
        }

        // Where the scene's files are: its own folder, the folder of the prefab it is the document of, or the folder it was told to use for now (m_FolderOverride).
        inline std::wstring SceneFolder( mgr& Mgr, guid SceneGuid ) noexcept
        {
            if( auto* pScene = Mgr.Find(SceneGuid); pScene && pScene->m_bPrefabDocument )
                return pScene->m_FolderOverride.empty() ? PrefabFolderOf( Mgr.m_ProjectPath, SceneGuid.m_Instance.m_Value ) : pScene->m_FolderOverride;
            return SceneFolder(std::wstring_view(Mgr.m_ProjectPath), SceneGuid);
        }

        inline std::wstring DescriptorPath( mgr& Mgr, guid SceneGuid ) noexcept
        {
            return SceneFolder(Mgr, SceneGuid) + L"/Descriptor.txt";
        }

        // Component-type dependency manifest - "which component types does this scene's own entity
        // data actually use" - a separate, small file (not a field on the descriptor above), matching
        // the existing dependencies.txt convention this project's asset pipeline already uses: cheap
        // to bulk-scan project-wide without opening any entity data (the descriptor itself doesn't
        // carry component-type info at all - only each individual .entity file's own "ComponentTypes"
        // record does, see SaveEntity/LoadEntity), and independently regenerable without touching the
        // descriptor's own timestamp. See SaveSceneComponentDependencies's own comment for what writes
        // it and why.
        inline std::wstring ComponentDepsPath( std::wstring_view ProjectPath, guid SceneGuid ) noexcept
        {
            return SceneFolder(ProjectPath, SceneGuid) + L"/ComponentDeps.txt";
        }
        inline std::wstring ComponentDepsPath( mgr& Mgr, guid SceneGuid ) noexcept { return SceneFolder(Mgr, SceneGuid) + L"/ComponentDeps.txt"; }

        inline std::wstring EntityDbFolder( std::wstring_view ProjectPath, guid SceneGuid ) noexcept
        {
            return SceneFolder(ProjectPath, SceneGuid) + L"/entity_db";
        }
        inline std::wstring EntityDbFolder( mgr& Mgr, guid SceneGuid ) noexcept { return SceneFolder(Mgr, SceneGuid) + L"/entity_db"; }

        // One entity's file in the scene stored in ResourceFolder - a Scene's .desc folder, or a Prefab's (a prefab is stored as a scene).
        inline std::wstring EntityFileInFolder( std::wstring_view ResourceFolder, permanent_id Id ) noexcept
        {
            const auto Byte0 = FormatHex8( static_cast<std::uint8_t>( Id         & 0xFF) );
            const auto Byte1 = FormatHex8( static_cast<std::uint8_t>((Id >> 8)   & 0xFF) );
            return std::wstring(ResourceFolder) + L"/entity_db/" + Byte0 + L"/" + Byte1 + L"/" + FormatPermanentIdW(Id) + L".entity";
        }

        inline std::wstring EntityPath( mgr& Mgr, guid SceneGuid, permanent_id Id ) noexcept
        {
            return EntityFileInFolder( SceneFolder(Mgr, SceneGuid), Id );
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
        //
        // Also the detector behind E29's own Idle Work sanity scan (see kit/E29_IdleWork.h in the
        // xGPU example, and [[e29_idle_work_system]] memory) - an O(n) set comparison against
        // descriptor::m_ActiveEntities/the scene's own live membership, split into two severities -
        // file exists but GUID isn't listed (orphaned, e.g. an interrupted save - see SaveScene's own
        // comment for exactly how that can happen - or manual file surgery; low severity, warn only)
        // vs. GUID listed but file missing (dangling reference - an existence claim with no data
        // behind it; still just a warning, never a hard block). Runs on a detached background thread,
        // only once the editor has been genuinely idle for a while - never inline on the Load/Play/
        // Stop path (a recursive_directory_iterator walk of the whole entity_db folder is real,
        // scales-with-entity-count I/O - a scene can be on the order of 1,000,000 entities). Takes
        // ProjectPath directly rather than mgr& for the same reason - a detached thread must never
        // hold a reference into a GameMgr that a concurrent Stop/hot-reload could destroy out from
        // under it; a plain wstring copy has no such lifetime hazard.
        //
        // pCancelRequested (optional): checked once per directory entry, so the walk can bail out
        // within roughly one file's worth of I/O latency of being asked to - direct user request: "when
        // the user starts using the editor, the idle work should stop ASAP," not just "won't start a
        // NEW scan." A cancelled walk returns whatever it collected so far; the caller (E29_IdleWork.h)
        // treats a cancelled result as untrustworthy for the DANGLING-reference check specifically (an
        // incomplete on-disk listing would make plenty of real, present files look falsely "missing"),
        // and simply tries again next idle period rather than acting on partial data.
        //-----------------------------------------------------------------------------------------
        inline std::vector<permanent_id> DiscoverEntityIds( std::wstring_view ProjectPath, guid SceneGuid, const std::atomic<bool>* pCancelRequested = nullptr ) noexcept
        {
            std::vector<permanent_id> Ids;
            std::error_code           Ec;

            auto Root = std::filesystem::path( EntityDbFolder(ProjectPath, SceneGuid) );
            if( false == std::filesystem::exists(Root, Ec) || Ec )
            {
                // the scene of a prefab opened in a Prefab Editor: its entity files are the prefab's
                Root = std::filesystem::path( PrefabFolderOf(ProjectPath, SceneGuid.m_Instance.m_Value) + L"/entity_db" );
                if( false == std::filesystem::exists(Root, Ec) || Ec ) return Ids;
            }

            for( auto& Entry : std::filesystem::recursive_directory_iterator(Root, std::filesystem::directory_options::skip_permission_denied, Ec) )
            {
                if( pCancelRequested && pCancelRequested->load(std::memory_order_relaxed) ) break;
                if( Ec ) break;
                if( false == Entry.is_regular_file() ) continue;
                if( Entry.path().extension() != L".entity" ) continue;

                try
                {
                    const auto Value = std::stoull( Entry.path().stem().wstring(), nullptr, 16 );
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
            Scene.m_InstanceMembers.clear();
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
            if( Scene.m_bPrefabDocument ) return xecs::prefab::document::LoadDescriptor( Mgr, Scene, OutActiveEntities );

            const auto Path = DescriptorPath(Mgr, Scene.m_Guid);
            std::error_code Ec;
            if( false == std::filesystem::exists(Path, Ec) || Ec )
            {
                Scene.m_ParentScenes.clear();
                Scene.m_ExternalRefTable.clear();
                Scene.m_Folders.clear();
                Scene.m_EntityNames.clear();
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
            Scene.m_EntityNames.clear();
            for( auto& N : Descriptor.m_EntityNames ) Scene.m_EntityNames[N.m_Id] = std::move(N.m_Name);
            OutActiveEntities        = std::move(Descriptor.m_ActiveEntities);
            return {};
        }

        //-----------------------------------------------------------------------------------------
        // One entity read from its file into staging, not yet created (see staged_components). A
        // scene is loaded as: read every entity -> apply prefab overrides in staging -> create
        // (which runs the builder systems, so they see the overridden values) -> remap references.
        struct staged_entity
        {
            permanent_id                                               m_Id     = invalid_permanent_id_v;
            const xecs::game_mgr::instance::build_plan*                m_pPlan  = nullptr;
            std::unique_ptr<xecs::persist::details::staged_components> m_pStaged;
        };

        inline bool ResolveReferenceInScene( mgr& Mgr, instance& Scene, xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept;  // below
    }
}

// A prefab instance is a recipe (xecs_prefab_recipe_inline.h, included after this file): what the scene's load and save ask of it.
namespace xecs::prefab::recipe
{
    inline void StageSceneInstances( xecs::game_mgr::instance&, std::vector<xecs::scene::details::staged_entity>&, std::unordered_map<xecs::scene::permanent_id, std::size_t>&, std::vector<std::pair<xecs::scene::permanent_id, xecs::scene::instance_member>>& ) noexcept;
    inline void RegisterMembers( xecs::scene::instance&, std::vector<std::pair<xecs::scene::permanent_id, xecs::scene::instance_member>>& ) noexcept;
    inline void LinkChildren( xecs::game_mgr::instance&, xecs::scene::instance& ) noexcept;
    inline int  ConvertOldInstances( xecs::scene::mgr&, xecs::scene::instance& ) noexcept;
    template< typename T_ENCODE >
    inline void RefreshRecipe( xecs::game_mgr::instance&, xecs::scene::instance&, xecs::scene::permanent_id, T_ENCODE&& ) noexcept;
}

namespace xecs::scene
{
    namespace details
    {

        // One entity file read into staging. ExtraInfos join the components the file names: a prefab's
        // members get prefab::tag and its root prefab::root, which are never written (see mgr::Save in
        // xecs_prefab_mgr_inline.h - a prefab is stored as a scene).
        inline xerr ReadEntityFile( xecs::game_mgr::instance& GameMgr, const std::wstring& Path, staged_entity& Out, std::span<const xecs::component::type::info* const> ExtraInfos = {} ) noexcept
        {
            xecs::serializer::stream TextFile;
            if( auto Err = TextFile.Open( true, Path, xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
                return Err;

            std::uint32_t FileId32   = 0;
            int           nComponents = 0;

            if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("PermanentId",  FileId32))
                    ||(Error = TextFile.Field("nComponents",  nComponents));
                }
            ); Err ) return Err;

            // An id that does not fit in 32 bits has the record EntityId64 right after EntityInfo (see WriteEntityFile)
            permanent_id FileId = FileId32;
            if( TextFile.getRecordName() == "EntityId64" )
            {
                std::uint64_t Id64 = 0;
                if( auto Err = TextFile.Record( "EntityId64", [&]( xerr& Error ) noexcept { Error = TextFile.Field("Id", Id64); } ); Err ) return Err;
                FileId = Id64;
            }

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
            if( auto Err = xecs::persist::details::DetectAndUnionPrefabInstance( GameMgr, TextFile, Infos, ArchetypeInfos, TempPI, PrefabRootEntity ); Err )
            {
                std::printf("[Scene::LoadEntity] Id=%llX : DetectAndUnionPrefabInstance failed (%s)\n", (unsigned long long)FileId, std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
            const bool bIsPrefabInstance = PrefabRootEntity.isValid();
            for( auto pInfo : ExtraInfos )
                if( std::find(ArchetypeInfos.begin(), ArchetypeInfos.end(), pInfo) == ArchetypeInfos.end() ) ArchetypeInfos.push_back(pInfo);   // a duplicate would corrupt the pool

            // Created later, once, already in its final share family - and, when builders are on,
            // already without its builder components (doc/xecs_builder_components.md).
            Out.m_Id      = FileId;
            Out.m_pPlan   = &GameMgr.getBuildPlan( { ArchetypeInfos.data(), ArchetypeInfos.size() } );
            Out.m_pStaged = std::make_unique<xecs::persist::details::staged_components>( ArchetypeInfos );
            auto& Staged  = *Out.m_pStaged;

            // A prefab instance starts from the prefab's CURRENT values; the file only holds the
            // components added on this instance (overrides are applied afterwards, in staging).
            if( bIsPrefabInstance )
                Staged.CopyFrom( GameMgr, PrefabRootEntity, { &xecs::component::type::info_v<xecs::component::children> } );     // the prefab's children are its own: an instance's are its members (or its file's, before recipes)

            for( auto pInfo : Infos )
            {
                // TempPI was already parsed (it's always first in the file) - the stream is past it.
                if( xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(pInfo) )
                {
                    *Staged.find<xecs::editor::prefab_instance>() = std::move(TempPI);
                    continue;
                }

                // TAG: already in the archetype via ArchetypeInfos; SaveEntity writes no data block for it.
                if( pInfo->m_TypeID == xecs::component::type::id::TAG ) continue;

                if( auto Err = xecs::persist::details::SerializeOneComponent(TextFile, true, *pInfo, Staged.find(*pInfo)); Err )
                {
                    std::printf("[Scene::LoadEntity] Id=%llX : component '%s' failed to read (%s)\n", (unsigned long long)FileId, pInfo->m_pName, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    return Err;
                }
            }

            return {};
        }

        inline xerr ReadEntity( mgr& Mgr, instance& Scene, permanent_id Id, staged_entity& Out ) noexcept
        {
            return ReadEntityFile( Mgr.m_GameMgr, EntityPath(Mgr, Scene.m_Guid, Id), Out );
        }

        //-----------------------------------------------------------------------------------------
        // Scene is whatever keeps the ids: a scene of the scene manager, or a prefab template (the prefab manager's).
        inline void CreateStagedEntity( xecs::game_mgr::instance& GameMgr, instance& Scene, staged_entity& Staged ) noexcept
        {
            const auto NewEntity = Staged.m_pStaged->Create( GameMgr, *Staged.m_pPlan );
            Staged.m_pStaged.reset();

            Scene.m_LocalToRuntime[Staged.m_Id]       = NewEntity;
            Scene.m_RuntimeToLocal[NewEntity.m_Value] = Staged.m_Id;
        }

        //-----------------------------------------------------------------------------------------
        // Single-entity read + create (e.g. restoring one deleted entity). Its prefab overrides are
        // the caller's job, on the live entity. A whole scene goes through EnsureLoaded instead.
        inline xerr LoadEntity( mgr& Mgr, instance& Scene, permanent_id Id ) noexcept
        {
            staged_entity Staged;
            if( auto Err = ReadEntity( Mgr, Scene, Id, Staged ); Err ) return Err;
            CreateStagedEntity( Mgr.m_GameMgr, Scene, Staged );
            return {};
        }

        //-----------------------------------------------------------------------------------------
        // Writes one live entity to Path in the scene's entity format (the read side is ReadEntityFile).
        // Resolve(Target, OutEncoded) says how a reference is written - the container-specific half of
        // ResolveReferenceForSave: a Scene's same-scene/declared-parent lookup (mgr::SaveEntity), or a
        // prefab's own members only (prefab::mgr::Save). xecs::prefab::root is never written: the folder
        // the file is in already says which prefab it is, and the descriptor which member is the root.
        //-----------------------------------------------------------------------------------------
        template< typename T_RESOLVE >   // bool(xecs::component::entity Target, std::int64_t& OutEncoded) noexcept
        inline xerr WriteEntityFile( xecs::game_mgr::instance& GameMgr, const std::wstring& Path, permanent_id Id, xecs::component::entity Entity, T_RESOLVE&& Resolve ) noexcept
        {
            auto& EDetails  = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            auto& Archetype = *EDetails.m_pPool->m_pArchetype;
            auto  DataSpan  = Archetype.getDataComponentInfos();
            auto  ShareSpan = Archetype.getShareComponentInfos();
            std::vector<const xecs::component::type::info*> AllComponentSpan;
            AllComponentSpan.reserve(DataSpan.size() + ShareSpan.size());
            for( auto p : DataSpan  ) AllComponentSpan.push_back(p);
            for( auto p : ShareSpan ) AllComponentSpan.push_back(p);
            xecs::persist::details::AppendPersistentTagInfos(Archetype, AllComponentSpan);

            std::printf("[SaveEntity] Id=%llX Entity.m_Value=%llu DataSpan (%zu) ShareSpan (%zu):", (unsigned long long)Id, (unsigned long long)Entity.m_Value, DataSpan.size(), ShareSpan.size());
            for( auto pInfo : DataSpan  ) std::printf(" %s", pInfo->m_pName);
            for( auto pInfo : ShareSpan ) std::printf(" %s", pInfo->m_pName);
            std::printf("\n"); std::fflush(stdout);

            // Computes PrefabOwnedGuids (what's 100% reconstructable from the prefab, so shouldn't get
            // its own data section here) and Infos (what actually needs writing) - see
            // xecs::persist::details::ComputePrefabInstanceSaveOverlay's own comment.
            std::vector<const xecs::component::type::info*> Infos;
            std::vector<std::uint64_t>                       PrefabOwnedGuids;
            xecs::persist::details::ComputePrefabInstanceSaveOverlay( GameMgr, Entity, DataSpan, Infos, PrefabOwnedGuids );
            std::erase_if( Infos, xecs::component::type::IsComponentType<xecs::prefab::root> );

            // A recipe does not hold its root's children: its members are spawned from the prefab, and an entity of the scene under it says so in its own parent.
            if( auto* pPI = static_cast<const xecs::editor::prefab_instance*>( static_cast<const void*>( xecs::persist::details::ResolveLiveComponentPointer(GameMgr, Entity, xecs::component::type::info_v<xecs::editor::prefab_instance>) ) );
                pPI && pPI->m_Format >= xecs::editor::prefab_instance::recipe_format_v )
                std::erase_if( Infos, xecs::component::type::IsComponentType<xecs::component::children> );

            std::printf("[SaveEntity] Id=%llX Infos to write (%zu):", (unsigned long long)Id, Infos.size());
            for( auto pInfo : Infos ) std::printf(" %s", pInfo->m_pName);
            std::printf("\n"); std::fflush(stdout);

            std::error_code Ec;
            std::filesystem::create_directories( std::filesystem::path(Path).parent_path(), Ec );

            // The resource pipeline's watcher reads a file of the project the moment it changes: a save of many files in a row can find one of them
            // open for a moment ("Permission denied: for writing", seen converting the Soccer scene). Tried again for up to half a second.
            xecs::serializer::stream TextFile;
            for( int Attempt = 0; ; ++Attempt )
            {
                auto Err = TextFile.Open( false, Path, xtextfile::file_type::TEXT, xtextfile::flags{ .m_isWriteFloats = true } );
                if( !Err ) break;
                if( Attempt == 20 ) return Err;
                std::this_thread::sleep_for( std::chrono::milliseconds(25) );
            }

            // The ids are 64 bits, the file's PermanentId column is 32: an id that fits (every id minted today) is written there whole, so the file
            // is what it always was; one that does not has the record EntityId64 right after EntityInfo with all of it (PermanentId then holds the low half).
            std::uint32_t WriteId     = static_cast<std::uint32_t>(Id);
            int           nComponents = static_cast<int>(Infos.size());

            if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("PermanentId", WriteId))
                    ||(Error = TextFile.Field("nComponents", nComponents));
                }
            ); Err ) return Err;

            if( Id > 0xFFFFFFFFull )
            {
                std::uint64_t Id64 = Id;
                if( auto Err = TextFile.Record( "EntityId64", [&]( xerr& Error ) noexcept { Error = TextFile.Field("Id", Id64); } ); Err ) return Err;
            }

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

            for( auto pInfo : Infos )
            {
                // TAG: listed in ComponentTypes above, but has no data to write.
                if( pInfo->m_TypeID == xecs::component::type::id::TAG ) continue;

                // DATA: entity pool. SHARE: share-entity via family (never in the entity DATA pool).
                auto* pLive = xecs::persist::details::ResolveLiveComponentPointer( GameMgr, Entity, *pInfo );
                assert(pLive != nullptr);
                if( pLive == nullptr ) return xerr::create<xecs::game_mgr::state::FAILURE, "SaveEntity could not resolve a component instance">();

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
                            if( false == xecs::persist::details::ResolveReferenceForSave(*pRef, Encoded, Resolve) )
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
                                if( false == xecs::persist::details::ResolveReferenceForSave(Data.get<xecs::component::entity>(), Encoded, Resolve) )
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

                if( xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(pInfo) )
                {
                    auto& PI_Scratch = *reinterpret_cast<xecs::editor::prefab_instance*>(Scratch.data());
                    xecs::persist::details::RefreshPrefabInstanceOverlayRecord( GameMgr, Entity, AllComponentSpan, PrefabOwnedGuids, PI_Scratch, [&]( xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept
                    {
                        return xecs::persist::details::ResolveReferenceForSave( Target, OutEncoded, Resolve );
                    });
                }

                const auto Error = xecs::persist::details::SerializeOneComponent(TextFile, false, *pInfo, Scratch.data());

                if( pInfo->m_pDestructFn ) pInfo->m_pDestructFn( Scratch.data() );

                if( Error ) return Error;
            }

            return {};
        }

        //-----------------------------------------------------------------------------------------
        // The component types the entities of LocalToRuntime use (guid, name, module), sorted by guid -
        // what ComponentDeps.txt holds, for a Scene or a prefab template. Each DISTINCT archetype among
        // them is walked once rather than every entity (entities are already grouped by archetype for
        // storage - reusing that grouping is both cheaper and naturally dedup'd).
        //-----------------------------------------------------------------------------------------
        inline std::vector<component_dependency> CollectComponentDependencies
        ( xecs::game_mgr::instance&                                                 GameMgr
        , const std::unordered_map<permanent_id, xecs::component::entity>&          LocalToRuntime
        , std::uint64_t                                                           (*pModuleOfComponent)( void* pUser, xecs::component::type::guid ) noexcept
        , void*                                                                     pModuleOfComponentUser
        ) noexcept
        {
            std::vector<component_dependency> Result;

            std::vector<const xecs::archetype::instance*>   SeenArchetypes;
            std::vector<const xecs::component::type::info*> UsedInfos;
            for( auto& Pair : LocalToRuntime )
            {
                auto& EDetails = GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
                if( EDetails.m_pPool == nullptr || EDetails.m_pPool->m_pArchetype == nullptr ) continue;

                auto* pArchetype = EDetails.m_pPool->m_pArchetype;
                if( std::find(SeenArchetypes.begin(), SeenArchetypes.end(), pArchetype) != SeenArchetypes.end() ) continue;
                SeenArchetypes.push_back(pArchetype);

                pArchetype->getComponentBits().Foreach( [&]( int, const xecs::component::type::info& Info ) noexcept
                {
                    if( std::find(UsedInfos.begin(), UsedInfos.end(), &Info) == UsedInfos.end() )
                        UsedInfos.push_back(&Info);
                });
            }

            // Sorted by guid - stable, diffable file, same reasoning m_ActiveEntities/m_Folders already
            // use in SaveSceneDescriptor.
            std::sort( UsedInfos.begin(), UsedInfos.end(), []( auto* A, auto* B ) noexcept { return A->m_Guid.m_Value < B->m_Guid.m_Value; } );

            Result.reserve(UsedInfos.size());
            for( auto* pInfo : UsedInfos )
                Result.push_back( { pInfo->m_Guid, pInfo->m_pName, pModuleOfComponent ? pModuleOfComponent(pModuleOfComponentUser, pInfo->m_Guid) : unknown_module_v } );
            return Result;
        }

        //-----------------------------------------------------------------------------------------
        // Writes a ComponentDeps.txt (see ComponentDepsPath's own comment) - to a temp file, then an
        // atomic rename over the real one.
        //-----------------------------------------------------------------------------------------
        inline xerr WriteComponentDependencies( const std::wstring& RealPath, const std::vector<component_dependency>& Deps ) noexcept
        {
            const auto TempPath = RealPath + L".tmp";

            // Scoped so TextFile's own destructor (which actually closes/flushes the underlying file
            // handle - there's no separate explicit Close()) runs BEFORE the rename below, not after -
            // std::filesystem::rename onto/over a file this process still has open fails with "being used
            // by another process" otherwise (confirmed live). SaveSceneDescriptor's own equivalent temp+
            // rename step doesn't hit this because its Descriptor.Serialize(...) call opens/writes/closes
            // its own xtextfile::stream entirely internally, returning only after that stream is already
            // gone.
            {
                xecs::serializer::stream TextFile;
                if( auto Err = TextFile.Open( false, TempPath, xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
                    return Err;

                if( auto Err = TextFile.Record( "ComponentDeps"
                ,   [&]( std::size_t& C, xerr& ) noexcept { C = Deps.size(); }
                ,   [&]( std::size_t i, xerr& Error ) noexcept
                    {
                        std::uint64_t V      = Deps[i].m_Guid.m_Value;
                        std::string   Name   = Deps[i].m_Name;
                        std::uint64_t Module = Deps[i].m_Module;
                          (Error = TextFile.Field("Guid",   V))
                        ||(Error = TextFile.Field("Name",   Name))
                        ||(Error = TextFile.Field("Module", Module));
                    }
                ); Err )
                    return Err;
            }

            std::error_code RenameEc;
            std::filesystem::rename( TempPath, RealPath, RenameEc );
            if( RenameEc )
                return xerr::create<xecs::game_mgr::state::FAILURE, "WriteComponentDependencies: wrote the temp file but the atomic rename over the real one failed">();

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

        // Only names of entities that still exist, sorted by id - a stable, diffable file. A member of a prefab instance has its prefab's name: the
        // scene keeps it only when it was renamed in the scene.
        for( auto& [Id, Name] : pScene->m_EntityNames )
        {
            if( false == pScene->m_LocalToRuntime.contains(Id) ) continue;
            if( auto M = pScene->m_InstanceMembers.find(Id); M != pScene->m_InstanceMembers.end() && M->second.m_Name == Name ) continue;
            Descriptor.m_EntityNames.push_back({ .m_Id = Id, .m_Name = Name });
        }
        std::sort( Descriptor.m_EntityNames.begin(), Descriptor.m_EntityNames.end(), [](auto& A, auto& B) noexcept { return A.m_Id < B.m_Id; } );

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
        Descriptor.m_ActiveEntities.reserve(pScene->m_LocalToRuntime.size() + pScene->m_UnloadedEntities.size());
        for( auto& Pair : pScene->m_LocalToRuntime )
            if( false == pScene->m_InstanceMembers.contains(Pair.first) )          // a member of a prefab instance is spawned by its instance: no file
                Descriptor.m_ActiveEntities.push_back(Pair.first);
        // The entities that could not be loaded are still the scene's: forgetting them here would unlink their files for good (the next load only reads what this list names).
        for( auto Id : pScene->m_UnloadedEntities )
            if( pScene->m_LocalToRuntime.find(Id) == pScene->m_LocalToRuntime.end() )
                Descriptor.m_ActiveEntities.push_back(Id);
        std::sort(Descriptor.m_ActiveEntities.begin(), Descriptor.m_ActiveEntities.end());

        // Written to a temp file + atomic rename over the real path, closing one of the two
        // deliberately-deferred hardening gaps documented on SaveScene's own comment below (direct
        // user request 2026-09-07, implemented 2026-09-12): a crash mid-write of the descriptor
        // itself could previously leave it truncated/corrupt, or - worse - leave it claiming a GUID is
        // still active with no entity file behind it if the crash landed between an entity delete and
        // this rewrite. std::filesystem::rename is atomic on the same volume (this file's project
        // path always is), so the real Descriptor.txt is NEVER observed mid-write: either the OLD
        // complete descriptor is still there, or the NEW complete one is - never a partial one. This
        // does NOT make the whole multi-file SaveScene transactional (individual entity writes still
        // aren't atomic with each other or with this) - it closes only "the descriptor itself is never
        // caught half-written," matching option (b) from SaveScene's own comment.
        const auto RealPath = details::DescriptorPath(*this, SceneGuid);
        const auto TempPath = RealPath + L".tmp";

        xproperty::settings::context Context;
        if( auto Err = Descriptor.Serialize( false, TempPath, Context ); Err )
            return Err;

        std::error_code RenameEc;
        std::filesystem::rename( TempPath, RealPath, RenameEc );
        if( RenameEc )
            return xerr::create<xecs::game_mgr::state::FAILURE, "SaveSceneDescriptor: wrote the temp descriptor but the atomic rename over the real one failed">();

        return {};
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::SaveScene( guid SceneGuid ) noexcept
    {
        auto* pScene = Find(SceneGuid);
        if( pScene == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "SaveScene: scene is not registered - call FindOrCreate first">();

        // A prefab document is written whole, as a prefab (its rules checked first): there is no pending-change bookkeeping to resolve.
        if( pScene->m_bPrefabDocument ) return xecs::prefab::document::Save( *this, *pScene );

        // Reconcile disk with the live scene HERE, at save time - not the moment an entity is created/
        // edited/deleted in the editor. On-disk state only ever changes as this one deliberate "sync
        // up with current state" step, never as a side effect of editing. Resolved from
        // m_PendingChanges (explicitly maintained by MarkEntityNew/MarkEntityDirty/MarkEntityDeleted)
        // rather than a directory scan (doesn't scale with entity count) or rewriting every live
        // entity (wastes IO proportional to the whole scene instead of to what actually changed).
        //
        // A member of a prefab instance has no file: a change to it is a change to its instance's recipe, so its instance is written instead.
        std::vector<permanent_id> Recipes;
        for( auto& [Id, Change] : pScene->m_PendingChanges )
        {
            auto M = pScene->m_InstanceMembers.find(Id);
            if( M == pScene->m_InstanceMembers.end() ) continue;
            if( pScene->m_LocalToRuntime.contains(M->second.m_Root) && std::find(Recipes.begin(), Recipes.end(), M->second.m_Root) == Recipes.end() )
                Recipes.push_back(M->second.m_Root);
        }
        for( auto Root : Recipes )
        {
            auto& C = pScene->m_PendingChanges[Root];
            if( C.m_New <= 0 && C.m_Dirty <= 0 && C.m_Deleted <= 0 ) C.m_Dirty = 1;
        }

        for( auto& [Id, Change] : pScene->m_PendingChanges )
        {
            if( pScene->m_InstanceMembers.contains(Id) ) continue;

            const bool bNew     = Change.m_New     > 0;
            const bool bDirty   = Change.m_Dirty   > 0;
            const bool bDeleted = Change.m_Deleted > 0;

            std::printf( "[SaveScene] Id=%llX pending: New=%d Dirty=%d Deleted=%d\n", (unsigned long long)Id, Change.m_New, Change.m_Dirty, Change.m_Deleted );
            std::fflush(stdout);

            if( bDeleted )
            {
                if( bNew ) continue; // created and deleted before ever being saved - never touched disk, nothing to do

                std::error_code DelEc;
                const auto      Path = std::filesystem::path( details::EntityPath(*this, SceneGuid, Id) );
                std::filesystem::remove( Path, DelEc );
                std::printf( "[SaveScene] removed deleted entity file Id=%llX (%s)\n", (unsigned long long)Id, DelEc ? DelEc.message().c_str() : "ok" );
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
                std::printf( "[SaveScene] WARNING: Id=%llX marked NEW but its file already exists on disk (id collision?)\n", (unsigned long long)Id );
                std::fflush(stdout);
            }
            else if( !bNew && bDirty && !bFileExists )
            {
                std::printf( "[SaveScene] WARNING: Id=%llX marked DIRTY but no file exists on disk yet (missed MarkEntityNew?)\n", (unsigned long long)Id );
                std::fflush(stdout);
            }

            if( auto Err = SaveEntity(SceneGuid, Id, It->second); Err ) return Err;
        }
        pScene->m_PendingChanges.clear();

        // This save is still NOT atomic as a whole - the loop above deletes/writes each entity's own
        // file one at a time, and only AFTER it fully completes does the line below rewrite the
        // descriptor (the thing that records which GUIDs are actually active). A crash (power loss,
        // force-kill) between an entity's file being deleted and this descriptor rewrite would still
        // leave the OLD descriptor listing that GUID as active with no file behind it - individual
        // entity writes still aren't transactional with each other or with the descriptor rewrite.
        //
        // What IS now closed (2026-09-12, direct user follow-up on the two gaps documented here since
        // 2026-09-07): SaveSceneDescriptor itself writes to a temp file + atomic rename, so the
        // descriptor file specifically is never caught half-written by a crash mid-write of THAT one
        // file - and EnsureLoaded now cross-checks DiscoverEntityIds against m_ActiveEntities on every
        // load (see its own comment), catching exactly the dangling-reference case above after the
        // fact with a clear, distinct warning rather than the previous generic
        // "entity failed to load" message. Together these two are the (a)/(b) pair this comment used
        // to describe as future work - both landed, neither makes the WHOLE multi-file save
        // transactional, which remains a real, understood, and currently accepted gap.
        //
        // Writes the descriptor, including a freshly-recomputed m_ActiveEntities - see its own comment.
        if( auto Err = SaveSceneDescriptor(SceneGuid); Err ) return Err;

        // Regenerated unconditionally on every Save, same as the descriptor above - see its own
        // comment for why this is a separate file rather than a descriptor field. Best-effort: a
        // failure here degrades a future compatibility check to "nothing to check against," it never
        // blocks the save the user actually asked for.
        if( auto Err = SaveSceneComponentDependencies(SceneGuid); Err )
        {
            std::printf("[SaveScene] SaveSceneComponentDependencies FAILED: %s\n", std::string(Err.getMessage()).c_str()); std::fflush(stdout);
        }

        return {};
    }

    //-----------------------------------------------------------------------------------------------
    // Writes ComponentDepsPath(SceneGuid) - the union of every component-type {guid, name} used by
    // ANY of this scene's own live entities (via Scene.m_LocalToRuntime), deduplicated by walking each
    // DISTINCT archetype among them exactly once rather than every entity individually (entities in
    // this project are already grouped by archetype for storage - reusing that grouping is both
    // cheaper and naturally dedup'd). This is the "does the current registry cover what this scene
    // needs" data source for the reload/open/module-removal compatibility checks - see the E29-side
    // consumer for the full design ("component-registry compatibility" plan).
    //-----------------------------------------------------------------------------------------------
    inline
    std::vector<component_dependency> mgr::CollectSceneComponentDependencies( guid SceneGuid ) const noexcept
    {
        auto* pScene = const_cast<mgr*>(this)->Find(SceneGuid);
        if( pScene == nullptr ) return {};
        return details::CollectComponentDependencies( m_GameMgr, pScene->m_LocalToRuntime, m_pModuleOfComponent, m_pModuleOfComponentUser );
    }

    inline
    xerr mgr::SaveSceneComponentDependencies( guid SceneGuid ) noexcept
    {
        if( Find(SceneGuid) == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "SaveSceneComponentDependencies: scene is not registered - call FindOrCreate first">();

        auto Deps = CollectSceneComponentDependencies(SceneGuid);

        // The components that the entities which could not be loaded need are not in any live archetype: keep what the file said about the ones the registry does not know now (a game module that
        // is not loaded), so that the file still says what the scene needs.
        if( !Find(SceneGuid)->m_UnloadedEntities.empty() )
        {
            for( auto& Old : LoadSceneComponentDependencies( std::wstring_view(m_ProjectPath), SceneGuid ) )
            {
                const bool bLive = std::any_of( Deps.begin(), Deps.end(), [&]( const component_dependency& D ) noexcept { return D.m_Guid.m_Value == Old.m_Guid.m_Value; } );
                if( !bLive && m_GameMgr.m_ComponentMgr.findComponentTypeInfo(Old.m_Guid) == nullptr ) Deps.push_back(std::move(Old));
            }
            std::sort( Deps.begin(), Deps.end(), []( const component_dependency& A, const component_dependency& B ) noexcept { return A.m_Guid.m_Value < B.m_Guid.m_Value; } );
        }

        return details::WriteComponentDependencies( details::ComponentDepsPath(*this, SceneGuid), Deps );
    }

    //-----------------------------------------------------------------------------------------------
    // Standalone read - no entity/archetype construction, no game_mgr instance needed - just the
    // {guid, name} pairs SaveSceneComponentDependencies last wrote. Returns empty (not an error) if
    // the file doesn't exist yet (a scene saved before this feature existed, or one that's never been
    // saved at all) - matches this project's established "best-effort, never block on an admittedly-
    // incomplete reference" posture for every other optional metadata file.
    //-----------------------------------------------------------------------------------------------
    inline
    std::vector<component_dependency> LoadSceneComponentDependencies( std::wstring_view ProjectPath, guid SceneGuid ) noexcept
    {
        std::vector<component_dependency> Result;

        auto Path = details::ComponentDepsPath(ProjectPath, SceneGuid);
        std::error_code ExistsEc;
        if( !std::filesystem::exists( std::filesystem::path(Path), ExistsEc ) )
        {
            // A prefab opened in a Prefab Editor is a scene of its guid: its manifest is the prefab's (a prefab is stored as a scene).
            Path = details::PrefabFolderOf( ProjectPath, SceneGuid.m_Instance.m_Value ) + L"/ComponentDeps.txt";
            if( !std::filesystem::exists( std::filesystem::path(Path), ExistsEc ) )
                return Result;
        }

        xecs::serializer::stream TextFile;
        if( auto Err = TextFile.Open( true, Path, xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
            return Result;

        auto ReadErr = TextFile.Record( "ComponentDeps"
        ,   [&]( std::size_t& C, xerr& ) noexcept { Result.reserve(C); }
        ,   [&]( std::size_t /*i*/, xerr& Error ) noexcept
            {
                component_dependency Entry;
                std::uint64_t        V = 0;
                  (Error = TextFile.Field("Guid", V))
                ||(Error = TextFile.Field("Name", Entry.m_Name));
                Entry.m_Guid = xecs::component::type::guid{V};

                // A file written before modules were tracked has no Module column: that is not an error, the component's module is just unknown.
                // Read apart from Error so that the missing column does not end the record.
                std::uint64_t Module = unknown_module_v;
                if( auto ModuleErr = TextFile.Field("Module", Module); !ModuleErr ) Entry.m_Module = Module;
                Result.push_back(std::move(Entry));
            }
        );
        (void)ReadErr; // best-effort - a partially-read/corrupt manifest just yields whatever was parsed so far

        return Result;
    }

    //-----------------------------------------------------------------------------------------------

    namespace details
    {
        // How a scene writes a reference: an entity of the same scene is its permanent id (positive); one of a declared parent scene is an interned
        // negative key of the external table (the live entity it names joins m_ExternalToRuntime too, so a reference written now resolves before the
        // next load); anything else cannot be written (false). The container-specific half of ResolveReferenceForSave.
        inline bool ResolveReferenceInScene( mgr& Mgr, instance& Scene, xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept
        {
            if( auto It = Scene.m_RuntimeToLocal.find(Target.m_Value); It != Scene.m_RuntimeToLocal.end() )
            {
                OutEncoded = static_cast<std::int64_t>(It->second);
                return true;
            }

            for( auto& ParentGuid : Scene.m_ParentScenes )
            {
                auto* pParent = Mgr.Find(ParentGuid);
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
                if( Scene.m_ExternalToRuntime.size() < Scene.m_ExternalRefTable.size() ) Scene.m_ExternalToRuntime.resize( Scene.m_ExternalRefTable.size() );
                Scene.m_ExternalToRuntime[ Scene.m_ExternalRefTable.size() - 1 ] = Target;
                OutEncoded = -(static_cast<std::int64_t>(Scene.m_ExternalRefTable.size()));
                return true;
            }

            return false;
        }
    }

    xerr mgr::SaveEntity( guid SceneGuid, permanent_id Id, xecs::component::entity Entity ) noexcept
    {
        auto& Scene = FindOrCreate(SceneGuid);

        // The ids are 63 bits: the top bit of an encoded reference says it is an external one (see max_permanent_id_v). The commands refuse such an
        // id; the engine itself only ever mints or derives smaller ones.
        assert( Id <= max_permanent_id_v );

        auto SceneResolve = [&]( xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept -> bool
        {
            return details::ResolveReferenceInScene( *this, Scene, Target, OutEncoded );
        };

        // A prefab instance of this scene is written as its recipe, refreshed from what its members are now (they have no file of their own).
        xecs::prefab::recipe::RefreshRecipe( m_GameMgr, Scene, Id, SceneResolve );

        if( auto Err = details::WriteEntityFile( m_GameMgr, details::EntityPath(*this, SceneGuid, Id), Id, Entity, SceneResolve ); Err )
            return Err;

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

            // The orphan/dangling consistency check that USED to run inline here (even as a detached
            // background thread) was moved out entirely (2026-09-12, direct user follow-up: "it should
            // go into a Sanity/Background process step... when the editor becomes idle") - a
            // recursive_directory_iterator walk of the whole entity_db folder is real, scales-with-
            // entity-count I/O (a scene can be on the order of 1,000,000 entities), and there's no
            // reason to pay that cost on EVERY load/Play/Stop cycle when the check is purely diagnostic
            // and nothing about it is time-sensitive. See E29_IdleWork.h (kit/) for where it actually
            // lives now - triggered only once the editor (both the user AND any AI/CLI driver) has been
            // genuinely idle for a while, using the exact same DiscoverEntityIds/SceneFolder/
            // EntityDbFolder ProjectPath-only overloads this file still exposes for it.

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
            Scene.m_UnloadedEntities.clear();

            // 1) Read every entity into staging.
            std::vector<staged_entity>                     StagedEntities;
            std::unordered_map<permanent_id, std::size_t>  StagedIndex;
            StagedEntities.reserve( ActiveEntities.size() );
            for( auto Id : ActiveEntities )
            {
                staged_entity Staged;
                if( auto Err = ReadEntity(Mgr, Scene, Id, Staged); Err )
                {
                    // A single corrupted/unreadable entity file (e.g. left truncated by a crash
                    // mid-save - Save is not yet crash-atomic) must not take the WHOLE scene down
                    // with it: skip it and keep going, rather than failing every other entity too and
                    // leaving the scene permanently unopenable until someone manually edits the
                    // descriptor. Anything genuinely referencing this id (Parent/Children, an
                    // external-ref) will simply fail its own reference-remap below with a clear
                    // error, rather than silently loading a wrong entity.
                    std::printf("[Scene::EnsureLoaded] WARNING: entity Id=%llX failed to load (%s) - skipping it, scene will load without it\n", (unsigned long long)Id, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    Scene.m_UnloadedEntities.push_back(Id);                     // not in the world, but still the scene's: see instance::m_UnloadedEntities
                    continue;
                }
                StagedIndex[Staged.m_Id] = StagedEntities.size();
                StagedEntities.push_back( std::move(Staged) );
            }

            // 2) The members of the scene's prefab instances (recipes: prefabs_plan.md phase 3), staged from their prefabs with their derived
            //    ids, and every recipe's overrides applied in staging - so builder systems (run at creation) see the overridden values.
            std::vector<std::pair<permanent_id, instance_member>> Members;
            xecs::prefab::recipe::StageSceneInstances( Mgr.m_GameMgr, StagedEntities, StagedIndex, Members );

            //    An instance saved before recipes has its members in files of the scene and child-index paths: its overrides walk the staged
            //    children lists (encoded permanent ids); it is converted once live (step 5).
            for( auto& Staged : StagedEntities )
            {
                xecs::persist::details::ApplyPrefabInstancePropertyOverrides( *Staged.m_pStaged, [&]( std::int64_t Encoded ) noexcept -> xecs::persist::details::staged_components*
                {
                    if( Encoded <= 0 ) return nullptr;
                    auto It = StagedIndex.find( static_cast<permanent_id>(Encoded) );
                    return It == StagedIndex.end() ? nullptr : StagedEntities[It->second].m_pStaged.get();
                });
            }

            // 3) Create them - each placed once, in its final archetype, builder systems run.
            for( auto& Staged : StagedEntities )
                CreateStagedEntity( Mgr.m_GameMgr, Scene, Staged );
            xecs::prefab::recipe::RegisterMembers( Scene, Members );

            // Soft-fail dangling external refs the same way the local-ref remap just below does
            // (encode null + WARNING, keep loading). A hard Failure here used to make the WHOLE
            // Level unopenable whenever one parent permanent_id had been deleted/skipped (e.g. after
            // Make Prefab deleted a referenced entity and the scene was saved) - same class of
            // "one hole must not take the whole scene down" that EnsureLoaded's own per-entity
            // skip and RemapLoadedEntityReferences' local .find()-or-null already established.
            Scene.m_ExternalToRuntime.resize( Scene.m_ExternalRefTable.size() );
            for( std::size_t i = 0; i < Scene.m_ExternalRefTable.size(); ++i )
            {
                auto& Addr    = Scene.m_ExternalRefTable[i];
                auto* pParent = Mgr.Find(Addr.m_ParentScene);
                if( pParent == nullptr )
                {
                    std::printf("[Scene::EnsureLoaded] WARNING: external-ref[%zu] points at parent scene 0x%llX which failed to load / is not resident - encoding as null\n"
                        , i, (unsigned long long)Addr.m_ParentScene.m_Instance.m_Value);
                    std::fflush(stdout);
                    Scene.m_ExternalToRuntime[i] = xecs::component::entity{};
                    continue;
                }

                auto It = pParent->m_LocalToRuntime.find(Addr.m_ParentEntity);
                if( It == pParent->m_LocalToRuntime.end() )
                {
                    std::printf("[Scene::EnsureLoaded] WARNING: external-ref[%zu] points at permanent_id=%llX in parent scene 0x%llX, which failed to load or doesn't exist - encoding as null\n"
                        , i, (unsigned long long)Addr.m_ParentEntity, (unsigned long long)Addr.m_ParentScene.m_Instance.m_Value);
                    std::fflush(stdout);
                    Scene.m_ExternalToRuntime[i] = xecs::component::entity{};
                    continue;
                }

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

            // 4) The scene's entities under an instance root or a member (their children lists are not stored) join those lists.
            xecs::prefab::recipe::LinkChildren( Mgr.m_GameMgr, Scene );

            // 5) The instances saved before recipes become recipes (written at the next save).
            xecs::prefab::recipe::ConvertOldInstances( Mgr, Scene );

            // Append() bumps m_Size; Size()/Search read m_CurrentCount until flush. Flush before
            // marking Active so the first Play tick (and editor Search) already sees live entities.
            Mgr.m_GameMgr.m_ArchetypeMgr.UpdateStructuralChanges();
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
