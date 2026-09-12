// Container-agnostic persistence helpers shared by xecs::scene (xecs_scene_inline.h) and
// xecs::prefab (xecs_prefab_mgr_inline.h) - both a Scene and a multi-entity Prefab group need the
// exact same "encode/decode an entity reference for disk", "walk a loaded entity's components and
// fix up its reference fields", and "this entity is itself an instance of a prefab, union/overlay
// its data accordingly" logic; only the id scheme differs (Scene's permanent_id vs. a prefab
// group's own local_id), so every function below is parameterized on a caller-supplied resolver
// instead of hardcoding xecs::scene::instance. Physically lived inside xecs_scene_inline.h until
// this Milestone-3 pass, moved here once a second, genuinely different consumer needed the same
// logic (xecs_prefab_mgr_inline.h already reached into xecs::scene::details::SerializeOneComponent
// cross-namespace before this move, which is what made the extraction obviously overdue).
namespace xecs::persist::details
{
    //-----------------------------------------------------------------------------------------
    // Reference encoding: a serialized entity-reference field is one signed 64bit integer stored
    // in the field's xecs::component::entity.m_Value (bit-preserving round trip - both are 64bit,
    // and int64/uint64 conversion is two's-complement-defined in C++20). 0 = null, >0 = local id
    // (Scene's permanent_id, or a prefab group's own local_id), <0 = -(index into the owning
    // container's own external/global reference table) - 1 (Scene only - a prefab group has no
    // such table, see ResolveReferenceForSave's own comment).
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
    // Save-side: resolves a live runtime entity into its on-disk encoded form via a caller-supplied
    // resolver (Scene's own same-scene/parent-scene-intern lookup, or a prefab group's own
    // same-group-only lookup) - this function itself only owns the universal null-guard, the
    // container-specific lookup logic lives entirely in Resolve's closure.
    //-----------------------------------------------------------------------------------------
    template< typename T_RESOLVE >   // bool(xecs::component::entity Target, std::int64_t& OutEncoded) noexcept
    inline bool ResolveReferenceForSave( xecs::component::entity Target, std::int64_t& OutEncoded, T_RESOLVE&& Resolve ) noexcept
    {
        if( false == Target.isValid() ) { OutEncoded = 0; return true; }
        return Resolve( Target, OutEncoded );
    }

    //-----------------------------------------------------------------------------------------
    // Walks one newly-created (already component-populated) entity's data components, in the same
    // DataSpan/getComponentInSequenceByInfo pattern CreatePrefabInstance's own in-memory reference
    // remap pass uses (details/xecs_prefab_mgr_inline.h - a separate, raw-entity-handle remap, not
    // this disk-encoding one), and replaces every encoded reference field's raw int64 with the
    // resolved live runtime entity - or a proper null (xecs::component::entity{}, NOT a bit-pattern
    // 0) for an encoded 0.
    //-----------------------------------------------------------------------------------------
    template< typename T_RESOLVE >   // xecs::component::entity(std::int64_t Encoded) noexcept
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
             || xecs::component::type::IsComponentType<xecs::component::entity>(pInfo) )
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

        return xerr::create<xecs::game_mgr::state::FAILURE, "Entity component has neither a serializer nor a property table">();
    }

    //-----------------------------------------------------------------------------------------
    // LOAD, step 1 of 3 - called right after an entity file's own ComponentTypes list (Infos) has
    // been read, BEFORE the archetype/entity even exists: if Infos names xecs::editor::prefab_instance,
    // reads it standalone (its "which prefab" guid is needed to union the archetype in the first
    // place), EnsureLoads that prefab, and unions the prefab root's own components into
    // OutArchetypeInfos - excluding anything a ComponentDiffs entry marks as deliberately removed on
    // this instance (re-adding it just because the prefab still defines it would silently undo the
    // removal on every reload). OutArchetypeInfos always ends up {entity}+Infos[+unioned root
    // components]; OutPrefabRootEntity stays invalid (default-constructed) when this entity isn't a
    // prefab instance at all - callers use that validity as the "was this a prefab instance" signal
    // for steps 2/3 below, rather than a separate bool.
    //-----------------------------------------------------------------------------------------
    inline xerr DetectAndUnionPrefabInstance
    ( xecs::game_mgr::instance& GameMgr
    , xecs::serializer::stream& TextFile
    , std::span<const xecs::component::type::info* const> Infos
    , std::vector<const xecs::component::type::info*>& OutArchetypeInfos
    , xecs::editor::prefab_instance& OutTempPI
    , xecs::component::entity& OutPrefabRootEntity
    ) noexcept
    {
        OutArchetypeInfos.clear();
        OutArchetypeInfos.reserve(Infos.size() + 4);
        OutArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::component::entity> );
        for( auto pInfo : Infos ) OutArchetypeInfos.push_back(pInfo);

        const bool bIsPrefabInstance = std::find_if(Infos.begin(), Infos.end(), xecs::component::type::IsComponentType<xecs::editor::prefab_instance>) != Infos.end();
        if( false == bIsPrefabInstance ) return {};

        if( auto Err = SerializeOneComponent(TextFile, true, xecs::component::type::info_v<xecs::editor::prefab_instance>, reinterpret_cast<std::byte*>(&OutTempPI)); Err )
            return Err;

        const xecs::prefab::guid PrefabGuid = OutTempPI.m_PrefabInstance;
        if( auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err )
            return Err;

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if( RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end() )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Entity's source Prefab failed to resolve after EnsureLoaded">();
        OutPrefabRootEntity = RootIt->second;

        auto& RootDetails   = GameMgr.m_ComponentMgr.getEntityDetails(OutPrefabRootEntity);
        auto& RootArchetype = *RootDetails.m_pPool->m_pArchetype;
        for( auto pRootInfo : RootArchetype.getDataComponentInfos() )
        {
            if( xecs::component::type::IsComponentType<xecs::component::entity>(pRootInfo) ) continue;

            // xecs::prefab::root is the INNER prefab's OWN bookkeeping (its guid, its variant-parent
            // guid) - never something a caller unions in as a baseline default. Every prior use of this
            // union (Scene's LoadEntity, a plain instance) never collides with this since a Scene entity
            // never separately re-adds prefab::root - but a GROUP's own root member that is ALSO a
            // nested instance of a different prefab does: LoadGroupMember's own `if(bIsRoot)` branch adds
            // prefab::root itself, based on the OUTER group's structure, not inherited from the inner
            // prefab's. Without this exclusion, both additions land in ArchetypeInfos - a duplicate
            // component in the archetype's info list that corrupts the resulting pool's per-component
            // storage indexing (same class of bug this session's own [[xecs_pool_reallocation_hazard]]
            // catalog exists for, just from a duplicate entry rather than a stale reference).
            if( xecs::component::type::IsComponentType<xecs::prefab::root>(pRootInfo) ) continue;

            // xecs::component::children is STRUCTURAL, PER-INSTANCE data (this entity's OWN actually-
            // registered children), never a plain value reconstructable from the referenced prefab's
            // own definition - unioning it in here would populate this entity's Children with the
            // PREFAB ASSET's own internal entity handles (meaningless outside m_PrefabGroups'
            // bookkeeping - not registered in this Scene's m_LocalToRuntime at all), which then fail to
            // resolve at render time and show as an expandable-but-empty row. Matches the SAME exclusion
            // ComputePrefabInstanceSaveOverlay's own save-side union now applies, for the identical
            // reason - see [[xecs_multientity_prefab_architecture]]'s own note on this bug.
            if( xecs::component::type::IsComponentType<xecs::component::children>(pRootInfo) ) continue;

            if( std::find(OutArchetypeInfos.begin(), OutArchetypeInfos.end(), pRootInfo) != OutArchetypeInfos.end() ) continue;

            bool bRemoved = false;
            for( auto& Diff : OutTempPI.m_ComponentDiffs )
                if( Diff.m_bAdded == false && Diff.m_ComponentTypeGuid == pRootInfo->m_Guid.m_Value ) { bRemoved = true; break; }
            if( bRemoved ) continue;

            OutArchetypeInfos.push_back(pRootInfo);
        }
        return {};
    }

    //-----------------------------------------------------------------------------------------
    // LOAD, step 2 of 3 - called right after NewEntity's archetype/pool memory exists, BEFORE any
    // per-component file read: every component the prefab itself owns starts out as a copy of the
    // prefab's CURRENT value, always - even one about to be read from the file too (a per-property
    // override's file record never re-states a whole component's data, only the overridden
    // properties, so whatever this entity did NOT override needs to already hold the prefab's value
    // before the file read runs).
    //-----------------------------------------------------------------------------------------
    inline void CopyPrefabInstanceDefaults
    ( xecs::game_mgr::instance& GameMgr
    , xecs::component::entity PrefabRootEntity
    , xecs::component::entity NewEntity
    , std::span<const xecs::component::type::info* const> ArchetypeInfos
    ) noexcept
    {
        auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(PrefabRootEntity);
        auto& NewDetails  = GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
        auto& Pool        = *NewDetails.m_pPool;

        for( auto pInfo : ArchetypeInfos )
        {
            if( xecs::component::type::IsComponentType<xecs::component::entity>(pInfo) ) continue;

            const auto iSrcType = RootDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
            const auto iDstType = Pool.findIndexComponentFromInfo(*pInfo);
            if( iSrcType < 0 || iDstType < 0 ) continue;

            auto pSrc = &RootDetails.m_pPool->m_pComponent[iSrcType][ RootDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
            auto pDst = &Pool.m_pComponent[iDstType][ NewDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
            if( pInfo->m_pCopyFn ) pInfo->m_pCopyFn(pDst, pSrc);
            else                   std::memcpy(pDst, pSrc, pInfo->m_Size);
        }
    }

    //-----------------------------------------------------------------------------------------
    // Walks Root -> children.m_List[Path[0]] -> children.m_List[Path[1]] -> ... and returns the
    // entity reached, or an invalid entity if Path is empty (Root itself), any index is out of
    // range, or an intermediate entity has no children component at all. Used to resolve a
    // prefab_component_override's m_MemberPath back to a live entity - the SAME path, walked from
    // either the placed instance's own root or the source prefab's own root, reaches the
    // corresponding member on each side (see m_MemberPath's own comment for why no separate id is
    // needed).
    //-----------------------------------------------------------------------------------------
    inline xecs::component::entity ResolveMemberPath
    ( xecs::game_mgr::instance& GameMgr
    , xecs::component::entity Root
    , std::span<const std::uint32_t> Path
    ) noexcept
    {
        auto Cur = Root;
        for( auto Index : Path )
        {
            auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Cur);
            if( Details.m_pPool == nullptr ) return {};
            const auto iChildrenType = Details.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::component::children> );
            if( iChildrenType < 0 ) return {};
            auto& List = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
            if( Index >= List.size() ) return {};
            Cur = List[Index];
        }
        return Cur;
    }

    //-----------------------------------------------------------------------------------------
    // Destroys Entity and its child subtree without scene permanent-id bookkeeping - used when
    // applying hierarchy diffs during nested prefab place (entities may not be scene-registered yet).
    //-----------------------------------------------------------------------------------------
    inline void DeleteEntitySubtreeUnregistered( xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity ) noexcept
    {
        if( !Entity.isValid() ) return;
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if( Details.m_pPool == nullptr ) return;

        if( Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::parent>) >= 0 )
        {
            const auto ParentEntity = Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value;
            if( ParentEntity.isValid() )
            {
                auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(ParentEntity);
                if( PDetails.m_pPool && PDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::children>) >= 0 )
                {
                    auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                    std::erase_if(List, [&](auto& E) noexcept { return E.m_Value == Entity.m_Value; });
                }
            }
        }

        if( Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::children>) >= 0 )
        {
            auto ChildEntities = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
            for( auto Child : ChildEntities )
                DeleteEntitySubtreeUnregistered(GameMgr, Child);
        }

        auto E = Entity;
        GameMgr.DeleteEntity(E);
    }

    //-----------------------------------------------------------------------------------------
    // Honor PI.m_HierarchyDiffs removals on a live instance root (nested place / re-instantiate).
    // Deepest paths first so sibling indices stay stable while deleting.
    //-----------------------------------------------------------------------------------------
    inline void ApplyRemovedHierarchyDiffs( xecs::game_mgr::instance& GameMgr, xecs::component::entity InstanceRoot, const xecs::editor::prefab_instance& PI ) noexcept
    {
        std::vector<std::vector<std::uint32_t>> Removed;
        for( auto& D : PI.m_HierarchyDiffs )
            if( !D.m_bAdded && !D.m_MemberPath.empty() )
                Removed.push_back(D.m_MemberPath);

        std::sort(Removed.begin(), Removed.end(), [](const auto& A, const auto& B) noexcept
        {
            if( A.size() != B.size() ) return A.size() > B.size();
            return std::lexicographical_compare(A.rbegin(), A.rend(), B.rbegin(), B.rend());
        });

        for( auto& Path : Removed )
        {
            const auto Target = ResolveMemberPath(GameMgr, InstanceRoot, Path);
            if( Target.isValid() )
                DeleteEntitySubtreeUnregistered(GameMgr, Target);
        }
    }

    //-----------------------------------------------------------------------------------------
    // LOAD, step 3 of 3 - called after the per-component file read loop has finished (which must
    // itself have already moved the parsed prefab_instance component into the entity's own pool
    // slot - this function reads it back out from there, not from a temporary, since the read loop
    // is the caller's own container-specific code). Applies each override's PropertyValueAsString
    // on top of the prefab defaults CopyPrefabInstanceDefaults already wrote - this IS the actual
    // override value now (the save side never writes the owning component's own data for an
    // overridden property at all), so without this step every overridden property would silently
    // read back as the prefab's plain default. An override whose m_MemberPath is non-empty targets
    // a non-root member of THIS group (a plain child, or the root of a nested instance) rather than
    // Entity's own data - resolved via ResolveMemberPath before locating pOwnerData.
    //-----------------------------------------------------------------------------------------
    inline void ApplyPrefabInstancePropertyOverrides( xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity ) noexcept
    {
        auto& EDetails = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& Pool     = *EDetails.m_pPool;

        const auto iPIType = Pool.findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> );
        if( iPIType < 0 ) return;

        auto& PI = *reinterpret_cast<xecs::editor::prefab_instance*>( &Pool.m_pComponent[iPIType][ EDetails.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size ] );
        for( auto& CompOverride : PI.m_lComponents )
        {
            auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{CompOverride.m_ComponentTypeGuid} );
            if( pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr ) continue;

            const auto TargetEntity = CompOverride.m_MemberPath.empty() ? Entity : ResolveMemberPath(GameMgr, Entity, CompOverride.m_MemberPath);
            if( TargetEntity.isValid() == false ) continue;
            auto& TDetails = GameMgr.m_ComponentMgr.getEntityDetails(TargetEntity);
            if( TDetails.m_pPool == nullptr ) continue;

            const auto iOwnerType = TDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
            if( iOwnerType < 0 ) continue;
            auto* pOwnerData = &TDetails.m_pPool->m_pComponent[iOwnerType][ TDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size ];

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

                // std::string's own buffer, not a manually null-terminated copy: some StringToAny
                // cases read String.data() as a null-terminated C-string (stol/atoi/...), but the
                // std::string case uses String.size() directly - std::string::data() is guaranteed
                // null-terminated since C++11 while .size() still excludes that terminator,
                // satisfying both without an appended '\0' silently becoming part of the parsed
                // string's OWN content.
                xproperty::any ParsedValue;
                std::string    ValueBuffer = PropOverride.m_PropertyValueAsString;
                if( xproperty::settings::StringToAny(ParsedValue, TypeGuid, std::span<char>(ValueBuffer.data(), ValueBuffer.size())) == false ) continue;

                std::string SetError;
                xproperty::sprop::setProperty( SetError, pOwnerData, *pOwnerInfo->m_pPropertyTable, xproperty::sprop::container::prop{ PropOverride.m_PropertyName, ParsedValue }, Context );
            }
        }
    }

    //-----------------------------------------------------------------------------------------
    // Unity's "Apply to Prefab" - the reverse direction of ApplyPrefabInstancePropertyOverrides
    // above: propagates every override this ONE placed instance currently has recorded up into the
    // source prefab's own live data (and re-saves the prefab asset), then clears this instance's own
    // override bookkeeping (m_lComponents) - once applied, the instance's current value IS the new
    // prefab default, so it's no longer "different from the prefab" by definition, exactly matching
    // Unity clearing the bold/overridden indicator the moment Apply runs. PIRootEntity is whichever
    // entity actually carries editor::prefab_instance (a group's outer root, or a nested instance's
    // own root) - every override this ONE prefab_instance component tracks, across however many
    // different group members m_MemberPath addresses, gets applied and cleared together as one unit
    // (there is no per-property "Apply" in this pass, only "Apply everything on this instance" -
    // Unity itself defaults to the same granularity via its top-level "Apply All" action; a future
    // pass could add per-property Apply if ever needed, mirroring OnOverrideReset's own granularity,
    // but that needs a NEW hook in the shared xproperty inspector library itself since it only
    // exposes m_OnOverrideCheck/m_OnOverrideReset today - out of scope here to avoid touching a
    // library other editors, e.g. E20/E21's own material/mesh-instance override UI, also depend on).
    //
    // Does NOT touch any OTHER already-placed instance of the same prefab that might be resident in
    // an open scene right now - their own live data keeps whatever value they already have (matches
    // Unity: Apply changes the prefab ASSET's default going forward, it doesn't retroactively touch
    // sibling instances' own already-diverged values, only what they'd fall back to on a fresh
    // instantiate/revert).
    //-----------------------------------------------------------------------------------------
    inline xerr ApplyInstanceOverridesToPrefab( xecs::game_mgr::instance& GameMgr, xecs::component::entity PIRootEntity ) noexcept
    {
        auto& PIDetails = GameMgr.m_ComponentMgr.getEntityDetails(PIRootEntity);
        if( PIDetails.m_pPool == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyInstanceOverridesToPrefab: entity has no pool">();

        const auto iPIType = PIDetails.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> );
        if( iPIType < 0 )
            return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyInstanceOverridesToPrefab: entity is not a prefab instance">();

        auto& PI = *reinterpret_cast<xecs::editor::prefab_instance*>( &PIDetails.m_pPool->m_pComponent[iPIType][ PIDetails.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size ] );

        if( auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PI.m_PrefabInstance); Err )
            return Err;

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PI.m_PrefabInstance.m_Instance.m_Value);
        if( RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end() )
            return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyInstanceOverridesToPrefab: source prefab root not resolved">();

        for( auto& CompOverride : PI.m_lComponents )
        {
            auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{CompOverride.m_ComponentTypeGuid} );
            if( pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr ) continue;

            const auto InstanceEntity = CompOverride.m_MemberPath.empty() ? PIRootEntity : ResolveMemberPath(GameMgr, PIRootEntity, CompOverride.m_MemberPath);
            const auto PrefabEntity   = ResolveMemberPath(GameMgr, RootIt->second, CompOverride.m_MemberPath);
            if( InstanceEntity.isValid() == false || PrefabEntity.isValid() == false ) continue;

            auto& IDetails = GameMgr.m_ComponentMgr.getEntityDetails(InstanceEntity);
            auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(PrefabEntity);
            if( IDetails.m_pPool == nullptr || PDetails.m_pPool == nullptr ) continue;

            const auto iInstType = IDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
            const auto iPrefType = PDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
            if( iInstType < 0 || iPrefType < 0 ) continue;

            auto* pInstData = &IDetails.m_pPool->m_pComponent[iInstType][ IDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size ];
            auto* pPrefData = &PDetails.m_pPool->m_pComponent[iPrefType][ PDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size ];

            for( auto& PropOverride : CompOverride.m_PropertyOverrides )
            {
                xproperty::settings::context Context{};
                xproperty::any               CurrentValue;
                bool                          bFound = false;
                xproperty::sprop::collector( pInstData, *pOwnerInfo->m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Value, const xproperty::type::members&, bool, const void* ) noexcept
                {
                    if( PropOverride.m_PropertyName == pPropertyName ) { CurrentValue = std::move(Value); bFound = true; }
                });
                if( bFound == false ) continue;

                std::string SetError;
                xproperty::sprop::setProperty( SetError, pPrefData, *pOwnerInfo->m_pPropertyTable, xproperty::sprop::container::prop{ PropOverride.m_PropertyName, CurrentValue }, Context );
            }
        }

        // Hierarchy diffs: push structural instance changes into the Prefab ASSET (Unity Apply).
        // 1) Resolve Added sources on the live INSTANCE (paths still valid there).
        // 2) Apply removals on the PREFAB root (still has those members).
        // 3) Shift each Added path for those removals, clone into the prefab under the parent path.
        {
            struct add_job
            {
                xecs::component::entity    m_Source{};
                std::vector<std::uint32_t> m_MemberPath;
            };
            std::vector<add_job> Adds;
            for (auto& D : PI.m_HierarchyDiffs)
            {
                if (!D.m_bAdded || D.m_MemberPath.empty()) continue;
                const auto Src = ResolveMemberPath(GameMgr, PIRootEntity, D.m_MemberPath);
                if (!Src.isValid()) continue;
                Adds.push_back(add_job{ Src, D.m_MemberPath });
            }

            ApplyRemovedHierarchyDiffs(GameMgr, RootIt->second, PI);

            auto& Group = GameMgr.m_PrefabMgr.m_PrefabGroups[PI.m_PrefabInstance.m_Instance.m_Value];
            for (auto& J : Adds)
            {
                auto Path = J.m_MemberPath;
                bool bDrop = false;
                for (auto& D : PI.m_HierarchyDiffs)
                {
                    if (D.m_bAdded || D.m_MemberPath.empty()) continue;
                    const auto& RemovedPath = D.m_MemberPath;
                    if (Path.size() >= RemovedPath.size()
                     && std::equal(RemovedPath.begin(), RemovedPath.end(), Path.begin()))
                    { bDrop = true; break; }
                    const auto PrefixLen = RemovedPath.size() - 1;
                    const auto DeletedIndex = RemovedPath.back();
                    if (Path.size() <= PrefixLen) continue;
                    if (!std::equal(RemovedPath.begin(), RemovedPath.begin() + static_cast<std::ptrdiff_t>(PrefixLen), Path.begin())) continue;
                    if (Path[PrefixLen] > DeletedIndex) --Path[PrefixLen];
                }
                if (bDrop || Path.empty()) continue;

                const auto InsertIndex = Path.back();
                std::vector<std::uint32_t> ParentPath(Path.begin(), Path.end() - 1);
                const auto PrefabParent = ParentPath.empty()
                    ? RootIt->second
                    : ResolveMemberPath(GameMgr, RootIt->second, ParentPath);
                if (!PrefabParent.isValid()) continue;

                auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(PrefabParent);
                if (PDetails.m_pPool == nullptr) continue;
                if (PDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::children>) < 0)
                    continue;

                auto NewChild = GameMgr.m_PrefabMgr.CloneEntityIntoPrefabGroup(J.m_Source, Group, /*bIsRoot=*/false);
                if (!NewChild.isValid()) continue;

                auto& ChildList = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                if (InsertIndex <= ChildList.size())
                    ChildList.insert(ChildList.begin() + static_cast<std::ptrdiff_t>(InsertIndex), NewChild);
                else
                    ChildList.push_back(NewChild);

                auto& CDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewChild);
                if (CDetails.m_pPool && CDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::parent>) >= 0)
                    CDetails.m_pPool->getComponent<xecs::component::parent>(CDetails.m_PoolIndex).m_Value = PrefabParent;
            }
        }

        // The instance no longer differs from the prefab by definition - clear the bookkeeping
        // BEFORE saving the prefab (Save reads the prefab's own entities, not PI.m_lComponents, so
        // ordering here only matters for when the in-memory override-indicator UI updates, not for
        // save correctness).
        PI.m_lComponents.clear();
        PI.m_HierarchyDiffs.clear();

        return GameMgr.m_PrefabMgr.Save(PI.m_PrefabInstance);
    }

    //-----------------------------------------------------------------------------------------
    // SAVE, step 1 of 2 - computes PrefabOwnedGuids (the prefab's own component-guid list, minus
    // xecs::prefab::root's own bookkeeping - instances never carry that component themselves, so
    // counting it as "prefab-owned" would make every instance falsely diff it as "removed") and
    // OutInfosToWrite (DataSpan minus anything PrefabOwnedGuids says is 100% reconstructable from
    // the prefab, since writing that here would just be a stale duplicate that can silently drift
    // from the prefab - the real source of truth - the moment the prefab changes; LoadEntity/
    // LoadGroupMember's own CopyPrefabInstanceDefaults re-derives every skipped component from the
    // prefab's own current value at load time). editor::prefab_instance itself, if present, is
    // always kept and moved first (LoadEntity/LoadGroupMember read it via a standalone parse before
    // the entity's archetype/pool memory even exists, so its block must be first in the file
    // regardless of DataSpan's own iteration order). Returns whether Entity is a prefab instance at
    // all - if false, OutInfosToWrite/OutPrefabOwnedGuids are irrelevant to the prefab-overlay
    // concern (the caller still writes OutInfosToWrite as an ordinary entity would).
    //-----------------------------------------------------------------------------------------
    inline bool ComputePrefabInstanceSaveOverlay
    ( xecs::game_mgr::instance& GameMgr
    , xecs::component::entity Entity
    , std::span<const xecs::component::type::info* const> DataSpan
    , std::vector<const xecs::component::type::info*>& OutInfosToWrite
    , std::vector<std::uint64_t>& OutPrefabOwnedGuids
    ) noexcept
    {
        auto& EDetails  = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& Archetype = *EDetails.m_pPool->m_pArchetype;

        // A GameMgr that never registered xecs::editor::prefab_instance (any consumer that doesn't
        // use the prefab-override feature at all, e.g. smoke_test_scene.cpp) has no such component in
        // any pool at all, so findIndexComponentFromInfo naturally returns -1 for it - no separate
        // registration check needed. findIndexComponentFromInfo, not getComponentBits().getBit() - see
        // [[xecs_getbit_vs_findindexcomponentfrominfo]] (a runtime-assigned component bit checked that
        // way can read as absent/invalid even when the component is genuinely present).
        const xecs::editor::prefab_instance* pPI = nullptr;
        if( const auto iType = EDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>); iType >= 0 )
        {
            pPI = reinterpret_cast<const xecs::editor::prefab_instance*>(&EDetails.m_pPool->m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size ]);
        }

        if( pPI != nullptr )
        {
            if( auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(pPI->m_PrefabInstance); Err )
            {
                std::printf("[PrefabInstanceOverlay] WARNING: failed to load source prefab to diff components (%s)\n", std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
            }
            else if( auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(pPI->m_PrefabInstance.m_Instance.m_Value); RootIt != GameMgr.m_PrefabMgr.m_PrefabList.end() )
            {
                auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(RootIt->second);
                for( auto pRootInfo : RootDetails.m_pPool->m_pArchetype->getDataComponentInfos() )
                {
                    if( xecs::component::type::IsComponentType<xecs::prefab::root>(pRootInfo) ) continue;

                    // xecs::component::children is STRUCTURAL, PER-INSTANCE data - never something
                    // reconstructable from the prefab's own static definition the way an ordinary
                    // property is. Treating it as "prefab owned" (skip writing, reconstruct from the
                    // prefab at load time via CopyPrefabInstanceDefaults) silently replaced THIS
                    // instance's own actually-registered child entities with the PREFAB ASSET's own
                    // internal entity handles on every save/reload - a real data-loss bug (an
                    // instantiated multi-entity prefab's children vanished from the Level tree after a
                    // save+reload, since those copied handles were never scene-registered at all -
                    // direct user report).
                    if( xecs::component::type::IsComponentType<xecs::component::children>(pRootInfo) ) continue;

                    OutPrefabOwnedGuids.push_back(pRootInfo->m_Guid.m_Value);
                }
            }
        }

        OutInfosToWrite.reserve(DataSpan.size());
        for( auto pInfo : DataSpan )
        {
            if( xecs::component::type::IsComponentType<xecs::component::entity>(pInfo) ) continue;

            if( pPI != nullptr && !xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(pInfo)
             && std::find(OutPrefabOwnedGuids.begin(), OutPrefabOwnedGuids.end(), pInfo->m_Guid.m_Value) != OutPrefabOwnedGuids.end() )
                continue;

            OutInfosToWrite.push_back(pInfo);
        }

        if( auto It = std::find_if(OutInfosToWrite.begin(), OutInfosToWrite.end(), xecs::component::type::IsComponentType<xecs::editor::prefab_instance>); It != OutInfosToWrite.end() && It != OutInfosToWrite.begin() )
            std::iter_swap(OutInfosToWrite.begin(), It);

        return pPI != nullptr;
    }

    //-----------------------------------------------------------------------------------------
    // SAVE, step 2 of 2 - called from inside the caller's per-component write loop, only when about
    // to write editor::prefab_instance's own scratch copy: recomputes ComponentDiffs from scratch
    // (added = on this entity but not in the prefab's own set; removed = in the prefab's own set but
    // not on this entity), prunes any property-override entry for a component that's no longer
    // prefab-owned, and refreshes each remaining override's PropertyValueAsString from the entity's
    // OWN live data (not the scratch copy - the live pool is what the ECS actually simulates with).
    // Recomputed fresh every save rather than trusted from incrementally-maintained state, so it can
    // never drift out of sync with reality the way hand-maintained bookkeeping can.
    //-----------------------------------------------------------------------------------------
    inline void RefreshPrefabInstanceOverlayRecord
    ( xecs::game_mgr::instance& GameMgr
    , xecs::component::entity Entity
    , std::span<const xecs::component::type::info* const> DataSpan
    , const std::vector<std::uint64_t>& PrefabOwnedGuids
    , xecs::editor::prefab_instance& InOutScratchPI
    ) noexcept
    {
        InOutScratchPI.m_ComponentDiffs.clear();
        for( auto pInfo2 : DataSpan )
        {
            if( xecs::component::type::IsComponentType<xecs::component::entity>(pInfo2) ) continue;
            if( xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(pInfo2) ) continue;
            if( std::find(PrefabOwnedGuids.begin(), PrefabOwnedGuids.end(), pInfo2->m_Guid.m_Value) == PrefabOwnedGuids.end() )
                InOutScratchPI.m_ComponentDiffs.push_back(xecs::editor::prefab_component_diff{ .m_ComponentTypeGuid = pInfo2->m_Guid.m_Value, .m_bAdded = true });
        }
        for( auto PrefabGuidValue : PrefabOwnedGuids )
        {
            bool bStillPresent = false;
            for( auto pInfo2 : DataSpan )
                if( pInfo2->m_Guid.m_Value == PrefabGuidValue ) { bStillPresent = true; break; }
            if( bStillPresent == false )
                InOutScratchPI.m_ComponentDiffs.push_back(xecs::editor::prefab_component_diff{ .m_ComponentTypeGuid = PrefabGuidValue, .m_bAdded = false });
        }

        std::erase_if( InOutScratchPI.m_lComponents, [&]( auto& C ) noexcept
        {
            return std::find(PrefabOwnedGuids.begin(), PrefabOwnedGuids.end(), C.m_ComponentTypeGuid) == PrefabOwnedGuids.end();
        });

        for( auto& CompOverride : InOutScratchPI.m_lComponents )
        {
            auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{CompOverride.m_ComponentTypeGuid} );
            if( pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr ) continue;

            // CompOverride.m_MemberPath addresses a DIFFERENT entity than Entity (the PI-carrying
            // one) whenever this override belongs to a non-root group member - reading Entity's own
            // live data unconditionally here (as this used to) refreshes the override string from
            // the WRONG entity's current value (e.g. the root's own Name instead of the actual
            // overridden member's), silently corrupting the on-disk override the next time this
            // entity is saved: this string, not the live component itself, is what a prefab-owned
            // property's file record actually persists (SerializeOneComponent skips the owning
            // component's own data for anything prefab-owned) - a second AI review caught this
            // ("child picks up the root's value" after save+reload), confirmed via direct code read.
            const auto TargetEntity = CompOverride.m_MemberPath.empty() ? Entity : ResolveMemberPath(GameMgr, Entity, CompOverride.m_MemberPath);
            if( TargetEntity.isValid() == false ) continue;
            auto& TDetails = GameMgr.m_ComponentMgr.getEntityDetails(TargetEntity);
            if( TDetails.m_pPool == nullptr ) continue;

            const auto iOwnerType = TDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
            if( iOwnerType < 0 ) continue;
            auto* pOwnerLive = &TDetails.m_pPool->m_pComponent[iOwnerType][ TDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size ];

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
}
