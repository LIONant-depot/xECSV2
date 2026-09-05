#include <filesystem>
#include <format>

namespace xecs::prefab
{
    //--------------------------------------------------------------------------------------------------------------

    xecs::component::entity mgr::CreatePrefabInstance( xecs::component::entity Entity, std::unordered_map< std::uint64_t, xecs::component::entity >& Remap, xecs::component::entity ParentEntity, bool isVariant ) noexcept
    {
        auto& PrefabDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& PrefabArchetype = *PrefabDetails.m_pPool->m_pArchetype;

        // Nested-prefab-instance splice: this group member isn't just "an entity to clone", it's
        // itself an instance of a DIFFERENT prefab (editor::prefab_instance) - route through the full
        // instantiation process for that inner prefab instead of a plain component-copy. Every one of
        // this file's recursion points already funnels through this exact function (the shape is
        // always `E = CreatePrefabInstance(E, Remap, Instance, isVariant)`), so this one early-return
        // gives the whole recursive walk nested-instance-awareness with no other call site changes -
        // this is what makes prefab composition (variants containing instances of other prefabs)
        // recursive. Checked via findIndexComponentFromInfo, not getComponentBits().getBit() - see the
        // note on the templated CreatePrefabInstance overload's own matching check below.
        if( PrefabDetails.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> ) >= 0 )
        {
            return CreateNestedPrefabInstance( Entity, Remap, ParentEntity, isVariant );
        }

        xecs::tools::bits InstanceBits = PrefabArchetype.getComponentBits();

        if( false == isVariant ) InstanceBits.clearBit( xecs::component::type::info_v<xecs::prefab::tag>.m_BitID );
        if( false == ParentEntity.isValid() ) InstanceBits.clearBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID);

        //
        // Get the instance archetype
        //
        auto& InstanceArchetype = m_GameMgr.m_ArchetypeMgr.getOrCreateArchetype(InstanceBits);

        xecs::component::entity PrefabInstance;

        // Recursing into CreatePrefabInstance for each child (below) can create/move entities in
        // OTHER pools - including, in principle, this exact one, if some sibling happens to share
        // Instance's own archetype - which would reallocate the pool storage backing a live
        // `children&` reference captured inside the CreateEntities callback below. Every "has
        // children" branch therefore only SNAPSHOTS Children.m_List inside its own callback (a plain
        // copy, safe to read after the callback returns) and defers the actual recursive remap +
        // write-back to this shared helper, called once the callback (and CreateEntities itself) has
        // fully returned - by which point Instance's own pool slot is safe to re-fetch fresh.
        auto RemapChildrenSafely = [&]( xecs::component::entity Instance, const std::vector<xecs::component::entity>& SnapshotChildren ) noexcept
        {
            std::vector<xecs::component::entity> NewChildren;
            NewChildren.reserve(SnapshotChildren.size());
            for( auto& E : SnapshotChildren )
                NewChildren.push_back( CreatePrefabInstance(E, Remap, Instance, isVariant) );

            auto& FreshDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Instance);
            FreshDetails.m_pPool->getComponent<xecs::component::children>(FreshDetails.m_PoolIndex).m_List = std::move(NewChildren);
        };

        //
        // Create the instance
        //
        if( InstanceArchetype.hasShareComponents() )
        {
            auto& Family = PrefabArchetype.hasShareComponents() ? InstanceArchetype.getOrCreatePoolFamily(*PrefabDetails.m_pPool->m_pMyFamily) : InstanceArchetype.getOrCreatePoolFamily({}, {});

            if( InstanceBits.getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) )
            {
                std::vector<xecs::component::entity> SnapshotChildren;
                if( ParentEntity.isValid() )
                {
                    InstanceArchetype.CreateEntities( Family, 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children, xecs::component::parent& Parent ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        SnapshotChildren = Children.m_List;
                        Parent.m_Value = ParentEntity;
                    });
                }
                else
                {
                    InstanceArchetype.CreateEntities(Family, 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children ) noexcept
                    {
                        Remap.insert( { Entity.m_Value, Instance } );
                        SnapshotChildren = Children.m_List;
                        PrefabInstance = Instance;
                    });
                }
                RemapChildrenSafely(PrefabInstance, SnapshotChildren);
            }
            else
            {
                if (ParentEntity.isValid())
                {
                    if (ParentEntity.isValid())
                    {
                        InstanceArchetype.CreateEntities(Family, 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::parent& Parent) noexcept
                        {
                            PrefabInstance = Instance;
                            Remap.insert( { Entity.m_Value, Instance } );
                            Parent.m_Value = ParentEntity;
                        });
                    }
                    else
                    {
                        InstanceArchetype.CreateEntities(Family, 1, Entity, [&](const xecs::component::entity& Instance) noexcept
                        {
                            PrefabInstance = Instance;
                            Remap.insert( { Entity.m_Value, Instance } );
                        });
                    }
                }
            }
        }
        else
        {
            if( InstanceBits.getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) )
            {
                std::vector<xecs::component::entity> SnapshotChildren;
                if (ParentEntity.isValid())
                {
                    InstanceArchetype.CreateEntities( 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children, xecs::component::parent& Parent ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        SnapshotChildren = Children.m_List;
                        Parent.m_Value = ParentEntity;
                    });
                }
                else
                {
                    InstanceArchetype.CreateEntities( 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        SnapshotChildren = Children.m_List;
                    });
                }
                RemapChildrenSafely(PrefabInstance, SnapshotChildren);
            }
            else
            {
                if (ParentEntity.isValid())
                {
                    InstanceArchetype.CreateEntities( 1, Entity, [&]( const xecs::component::entity& Instance, xecs::component::parent& Parent ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        Parent.m_Value = ParentEntity;
                    });
                }
                else
                {
                    InstanceArchetype.CreateEntities( 1, Entity, [&]( const xecs::component::entity& Instance) noexcept
                    {
                        Remap.insert( { Entity.m_Value, Instance } );
                        PrefabInstance = Instance;
                    });
                }
            }
        }

        return PrefabInstance;
    }

    //--------------------------------------------------------------------------------------------------------------

    template
    < typename T_ADD_TUPLE
    , typename T_SUB_TUPLE
    , typename T_CALLBACK
    > requires
    (    ( std::is_same_v< std::tuple<>, T_ADD_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_ADD_TUPLE> )
      && ( std::is_same_v< std::tuple<>, T_SUB_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_SUB_TUPLE> )
      && xecs::tools::assert_standard_function_v<T_CALLBACK>
      && xecs::tools::assert_function_return_v<T_CALLBACK, void>
    ) __inline
    xecs::component::entity mgr::CreatePrefabInstance( int Count, xecs::component::entity Entity, T_CALLBACK&& Callback, bool bRemoveRoot, bool isVariant ) noexcept
    {
        //
        // Get the right set of bits
        //
        auto& PrefabDetails   = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& PrefabArchetype = *PrefabDetails.m_pPool->m_pArchetype;

        // This prefab's own ROOT is itself an instance of a DIFFERENT prefab (the Unity "Prefab
        // Variant" workflow - a new prefab whose sole content is a single wrapped instance, no extra
        // grouping needed) - splice through CreateNestedPrefabInstance instead of cloning Entity's own
        // stored bytes directly, so the result always re-derives from the inner prefab's CURRENT state
        // (not a frozen byte-copy) and keeps its own nested-instance bookkeeping (guid + overrides)
        // going forward. Gated on isVariant==false: CreatePrefabVariant (isVariant=true) deliberately
        // wants the OTHER, plain-clone behavior below - it's minting a brand-new PREFAB RESOURCE that
        // should carry prefab::root/tag and any nested-instance data forward as-is, the same way
        // CloneEntityIntoPrefabGroup's own authoring-time clone does. bRemoveRoot has no meaning here
        // (a nested-instance root never has children of its own to keep - see
        // CloneEntityIntoPrefabGroup's own documented exclusion).
        if( false == isVariant )
        {
            // Checked via findIndexComponentFromInfo (matches the per-component lookup
            // SaveGroupMember/LoadGroupMember already use for this exact question), not
            // getComponentBits().getBit() - the latter compares a bit index captured from this call
            // site against an archetype built elsewhere, which this codebase's own component-
            // registration pattern (a runtime-assigned id on an otherwise compile-time constexpr
            // object) doesn't reliably support here for reasons not fully isolated this session; see
            // the matching note on AttachPrefabInstanceComponent in E29_LevelScene_Editor.cpp.
            if( PrefabDetails.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> ) >= 0 )
            {
                xecs::component::entity Last{};
                for( int i = 0; i < Count; ++i )
                {
                    std::unordered_map<std::uint64_t, xecs::component::entity> LocalRemap;
                    Last = CreateNestedPrefabInstance( Entity, LocalRemap, xecs::component::entity{}, isVariant );
                    if constexpr( false == std::is_same_v< T_CALLBACK, xecs::tools::empty_lambda > )
                        (void)m_GameMgr.getEntity( Last, std::forward<T_CALLBACK&&>(Callback) );
                }
                return Last;
            }
        }

        xecs::tools::bits InstanceBits = PrefabArchetype.getComponentBits();

        xassert( InstanceBits.getBit(xecs::component::type::info_v<xecs::prefab::tag>.m_BitID) );

        if( false == isVariant ) 
        {
            InstanceBits.clearBit( xecs::component::type::info_v<xecs::prefab::tag>.m_BitID );
            InstanceBits.clearBit( xecs::component::type::info_v<xecs::prefab::root>.m_BitID );
        }
        InstanceBits.AddFromComponents( xecs::types::null_tuple_v<T_ADD_TUPLE> );
        InstanceBits.ClearFromComponents( xecs::types::null_tuple_v<T_SUB_TUPLE> );

        //
        // Are we dealing with a scene prefab or a regular prefab?
        //
        using fn_traits = xecs::function::traits<T_CALLBACK>;
        if( false == InstanceBits.getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) )
        {
            xecs::component::entity InstanceEntity;

            //
            // Get the instance archetype
            //
            auto& InstanceArchetype = m_GameMgr.m_ArchetypeMgr.getOrCreateArchetype(InstanceBits);

            //
            // If we have to deal with share components...
            //
            if( InstanceArchetype.hasShareComponents() )
            {
                auto& Family = &PrefabArchetype == &InstanceArchetype ? *PrefabDetails.m_pPool->m_pMyFamily
                                                                      : PrefabArchetype.hasShareComponents() ? InstanceArchetype.getOrCreatePoolFamily(*PrefabDetails.m_pPool->m_pMyFamily) 
                                                                                                             : InstanceArchetype.getOrCreatePoolFamily({}, {});

                // Can we choose the fast path here?
                //  - We don't have a function or if we have a function but not share components then not changes in share components will happen which means we can speed up things.
                if constexpr( std::is_same_v< T_CALLBACK, xecs::tools::empty_lambda > || false == xecs::tools::function_has_share_component_args_v<T_CALLBACK> )
                {
                    InstanceEntity = InstanceArchetype.CreateEntities(Family, Count, Entity, std::forward<T_CALLBACK&&>(Callback) );
                }
                else
                {
                    static_assert(std::tuple_size_v<xecs::component::type::details::share_only_tuple_t<typename fn_traits::args_tuple>> > 0);

                    // TODO: We could try to see if we can improve the performance of this
                    for( int i=0; i<Count; ++i )
                    {
                        // We will first place the new entity in the default family
                        InstanceArchetype.CreateEntities( Family, 1, Entity, [&]( const xecs::component::entity& Entity ) constexpr noexcept 
                        {
                            InstanceEntity = Entity;
                        });

                        // Now that everything has been copied over including the share components then we are going to let the user move it to the final family
                        (void)m_GameMgr.getEntity( InstanceEntity, std::forward<T_CALLBACK&&>(Callback) );
                    }
                }
            }
            else
            {
                assert( std::tuple_size_v<xecs::component::type::details::share_only_tuple_t<typename fn_traits::args_tuple>> == 0 );
                if constexpr (std::is_same_v< T_CALLBACK, xecs::tools::empty_lambda > || std::tuple_size_v<xecs::component::type::details::share_only_tuple_t<typename fn_traits::args_tuple>> == 0) InstanceEntity = InstanceArchetype.CreateEntities( Count, Entity, std::forward<T_CALLBACK&&>(Callback) );
                else 
                {
                    xassert( false && "You are trying to chage a share component using a function but the entity has not share components" );
                }
            }

            // Entity-Prefab-Instance-Done
            return InstanceEntity;
        }

        //
        // If we are dealing with a Scene-Prefab...
        //
        auto pInstanceArchetype = bRemoveRoot ? nullptr : &m_GameMgr.m_ArchetypeMgr.getOrCreateArchetype(InstanceBits);
        xecs::component::entity EntityInstance;

        std::vector<xecs::component::entity*> References;       // Minimize allocation by factoring it out here
        for( int i=0; i<Count; ++i )
        {
            std::unordered_map< std::uint64_t, xecs::component::entity >    EntityRemap;

            //
            // Create All entities and Remap the parent/children
            //
            if( bRemoveRoot )
            {
                //
                // Create all the children
                //
                (void)m_GameMgr.getEntity( Entity, [&]( xecs::component::children& Children ) constexpr noexcept
                {
                    for( auto& E : Children.m_List )
                    {
                        CreatePrefabInstance( E, EntityRemap, EntityInstance, isVariant );
                    }
                });
            }
            else
            {
                // Same snapshot-then-deferred-remap discipline as the low-level recursive
                // CreatePrefabInstance's own RemapChildrenSafely (see its comment): a `children&`
                // reference captured inside this CreateEntities callback would go stale the moment a
                // recursive clone below lands in this exact pool, so the callback only snapshots
                // Children.m_List; the actual recursion + write-back happens after CreateEntities has
                // fully returned.
                std::vector<xecs::component::entity> SnapshotChildren;
                if ( pInstanceArchetype->hasShareComponents() )
                {
                    auto& Family = PrefabArchetype.hasShareComponents() ? pInstanceArchetype->getOrCreatePoolFamily(*PrefabDetails.m_pPool->m_pMyFamily) : pInstanceArchetype->getOrCreatePoolFamily({}, {});
                    pInstanceArchetype->CreateEntities( Family, 1, Entity, [&]( const xecs::component::entity& EntityI, xecs::component::children& Children ) constexpr noexcept
                    {
                        EntityInstance = EntityI;
                        EntityRemap.insert( {Entity.m_Value, EntityI} );
                        SnapshotChildren = Children.m_List;
                    });
                }
                else
                {
                    pInstanceArchetype->CreateEntities( 1, Entity, [&]( const xecs::component::entity& EntityI, xecs::component::children& Children ) constexpr noexcept
                    {
                        EntityInstance = EntityI;
                        EntityRemap.insert( {Entity.m_Value, EntityI} );
                        SnapshotChildren = Children.m_List;
                    });
                }

                std::vector<xecs::component::entity> NewChildren;
                NewChildren.reserve(SnapshotChildren.size());
                for( auto& E : SnapshotChildren )
                    NewChildren.push_back( CreatePrefabInstance( E, EntityRemap, EntityInstance, isVariant ) );

                auto& FreshDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(EntityInstance);
                FreshDetails.m_pPool->getComponent<xecs::component::children>(FreshDetails.m_PoolIndex).m_List = std::move(NewChildren);
            }

            //
            // Remap references for all new entities
            //
            for( auto& E : EntityRemap )
            {
                auto& IDetails  = m_GameMgr.m_ComponentMgr.getEntityDetails(E.second);
                auto& Archetype = IDetails.m_pPool->m_pArchetype;
                auto  DataSpan  = Archetype->getDataComponentInfos();

                int iSequence = 0;
                for( auto pInfo : DataSpan )
                {
                    // If we don't need to deal with remapping things then lets move on
                    if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES 
                       || pInfo == &xecs::component::type::info_v<xecs::component::children>
                       || pInfo == &xecs::component::type::info_v<xecs::component::parent>)
                        continue;

                    // get the component data
                    auto pData = IDetails.m_pPool->getComponentInSequenceByInfo( *pInfo, IDetails.m_PoolIndex, iSequence );

                    const int local_resever_index_v = 1000000;

                    // Remapping by function?
                    if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::BY_FUNCTION )
                    {
                        pInfo->m_pReportReferencesFn( References, pData );
                        for( auto pRefs : References )
                        {
                            // Make sure that we are not dealing with a global entity... those are safe references.
                            if( pRefs->m_GlobalInfoIndex < local_resever_index_v)
                            {
                                if (auto It = EntityRemap.find(pRefs->m_Value); It == EntityRemap.end())
                                {
                                    // Issue a warning here!!!
                                    xassert(false);
                                }
                                else
                                {
                                    *pRefs = It->second;
                                }
                            }
                        }
                        References.clear();
                    }
                    else
                    {
                        // Remap by property... a bit slow here...
                        // xproperty::sprop::collector/setProperty are the same enumerate/write-back
                        // primitives xproperty::sprop::serializer::Stream itself uses (see
                        // property_sprop_xtextfile_serializer.h) - the modern replacement for the old
                        // property::SerializeEnum/property::set pair. The "is this an entity property"
                        // check compares the property's atomic-type GUID against xecs::component::entity's
                        // registered GUID (xecs_entity_xproperty_bridge.h), since xproperty::any has no
                        // finite data_variant to index into any more - same fix as
                        // xecs_component_type_inline.h's ScopeHasEntityReferenceProperty (used by
                        // references_mode_v's AUTO-detection).
                        xproperty::settings::context Context{};
                        std::string                  SetError;
                        xproperty::sprop::collector( pData, *pInfo->m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void* ) noexcept
                        {
                            // Have we found an entity?
                            if( Data.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v )
                            {
                                auto RefEntity = Data.get<xecs::component::entity>();

                                // Make sure that we are not dealing with a global entity... those are safe references.
                                if( RefEntity.m_GlobalInfoIndex < static_cast<std::size_t>(local_resever_index_v) )
                                {
                                    if( auto It = EntityRemap.find( RefEntity.m_Value ); It == EntityRemap.end() )
                                    {
                                        // Issue a warning here!!!
                                        xassert(false);
                                    }
                                    else
                                    {
                                        // set the new value
                                        Data.get<xecs::component::entity>() = It->second;
                                        xproperty::sprop::setProperty( SetError, pData, *pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ pPropertyName, Data }, Context );
                                    }
                                }
                            }
                        });
                    }
                }

                //
                // Deal with share component change of references...
                // TODO: ...

            }

            //
            // Call the user function for the root node
            //
            if constexpr ( false == std::is_same_v< T_CALLBACK, xecs::tools::empty_lambda > )
            {
                if( bRemoveRoot == false )
                {
                    (void)m_GameMgr.getEntity(EntityInstance, std::forward<T_CALLBACK&&>(Callback) );
                }
                else
                {
                    assert( false && "You are asking to remove the root entity and yet you are asking me to use a call back on it! Make up your mind!" );
                }
            }
        }

        return EntityInstance;
    }

    //--------------------------------------------------------------------------------------------------------------

    template
    < typename T_ADD_TUPLE
    , typename T_SUB_TUPLE
    , typename T_CALLBACK
    > requires
    (    ( std::is_same_v< std::tuple<>, T_ADD_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_ADD_TUPLE> )
      && ( std::is_same_v< std::tuple<>, T_SUB_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_SUB_TUPLE> )
      && xecs::tools::assert_standard_function_v<T_CALLBACK>
      && xecs::tools::assert_function_return_v<T_CALLBACK, void>
    ) __inline
    bool mgr::CreatePrefabInstance( int Count, xecs::prefab::guid PrefabGuid, T_CALLBACK&& Callback, bool bRemoveRoot, bool isVariant) noexcept
    {
        if( auto Tuple = m_PrefabList.find(PrefabGuid.m_Instance.m_Value); Tuple == m_PrefabList.end() ) return false;
        else CreatePrefabInstance( Count, Tuple->second, std::forward<T_CALLBACK&&>(Callback), bRemoveRoot, isVariant);
        return true;
    }

    //--------------------------------------------------------------------------------------------------------------
    // See xecs_prefab_mgr.h's own comment - the splice point CreatePrefabInstance(Entity,Remap,Parent,
    // isVariant)'s new early-return branch routes into when the entity it was about to clone is itself
    // a nested instance of a DIFFERENT prefab.
    //--------------------------------------------------------------------------------------------------------------
    xecs::component::entity mgr::CreateNestedPrefabInstance( xecs::component::entity Entity, std::unordered_map<std::uint64_t, xecs::component::entity>& Remap, xecs::component::entity ParentEntity, bool isVariant ) noexcept
    {
        auto& EDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);

        // Copy, not reference - Entity's own pool slot is unrelated to (and may be migrated/
        // invalidated independently of) what follows. This IS this member's own captured
        // "which prefab / what overrides" record and must be carried forward onto NestedRoot below -
        // discarding it here would silently lose every property override this specific member had on
        // top of the inner prefab, and would leave NestedRoot with no prefab_instance bookkeeping of
        // its own at all (breaking a later re-save of the OUTER group/scene, which needs that bit
        // present to recognize this member as a nested instance rather than a plain baked entity).
        auto PI = EDetails.m_pPool->getComponent<xecs::editor::prefab_instance>(EDetails.m_PoolIndex);

        // EnsureLoaded's existing early-out (already resident in m_PrefabList) makes this a no-op
        // after the first call for a given guid, so a deep chain of nested prefabs instantiated many
        // times over doesn't redundantly re-read from disk.
        if( auto Err = EnsureLoaded(PI.m_PrefabInstance); Err )
        {
            xassert(false);
            return {};
        }

        auto RootIt = m_PrefabList.find(PI.m_PrefabInstance.m_Instance.m_Value);
        if( RootIt == m_PrefabList.end() )
        {
            xassert(false);
            return {};
        }
        auto& InnerRoot    = RootIt->second;
        auto& InnerDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(InnerRoot);

        // Entity's own EXTRA data components - anything captured onto Entity beyond what the inner
        // prefab itself defines. CloneEntityIntoPrefabGroup's own byte-copy-everything convention
        // preserves these onto a nested-instance member at authoring time (e.g. "make a prefab from an
        // instance that already had an extra component added on top"), and LoadGroupMember's own
        // DetectAndUnionPrefabInstance union preserves them across a save/load round trip - without
        // this same union here, PLACING a nested instance would silently drop them every time
        // (place != load).
        std::vector<const xecs::component::type::info*> ExtraInfos;
        for( auto pInfo : EDetails.m_pPool->m_pArchetype->getDataComponentInfos() )
        {
            if( pInfo == &xecs::component::type::info_v<xecs::component::entity> )      continue;
            if( pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance> ) continue;
            if( pInfo == &xecs::component::type::info_v<xecs::component::parent> )       continue;
            if( pInfo == &xecs::component::type::info_v<xecs::component::children> )    continue;   // a nested-instance member never has its own - see CloneEntityIntoPrefabGroup's opaque-leaf exclusion
            if( pInfo == &xecs::component::type::info_v<xecs::prefab::root> )           continue;   // Entity's own group-root identity, if any - never carried by a PLACED instance
            if( InnerDetails.m_pPool->findIndexComponentFromInfo(*pInfo) >= 0 )          continue;   // the inner prefab already defines this - not an "extra"
            ExtraInfos.push_back(pInfo);
        }

        // The other half of the same "place != load" gap: PI.m_ComponentDiffs' m_bAdded=false entries
        // record a component the inner prefab DEFINES that Entity deliberately REMOVED (the same
        // add/remove tracking DetectAndUnionPrefabInstance already excludes on the LOAD path) - without
        // checking this here too, placing this nested instance would silently resurrect a component
        // the user explicitly removed, every time.
        bool bHasRemovals = false;
        for( auto& Diff : PI.m_ComponentDiffs )
            if( Diff.m_bAdded == false ) { bHasRemovals = true; break; }

        xecs::component::entity NestedRoot;
        if( ExtraInfos.empty() && !bHasRemovals )
        {
            // Common case: nothing beyond what the inner prefab defines - T_ADD_TUPLE always adds
            // editor::prefab_instance (so PI can be written into NestedRoot's own pool slot below,
            // regardless of whether the INNER prefab's root happens to carry that component itself)
            // and, only when a real ParentEntity was supplied, xecs::component::parent too - splicing
            // an invalid/zero parent onto every top-level (no-group-context) instantiation would be a
            // change of shape for something that previously never carried a parent component at all.
            // The inner prefab's own root never carries a parent component of its own (a prefab root,
            // by definition, has none - see CreatePrefabFromEntity/CloneEntityIntoPrefabGroup), so this
            // is always a genuinely NEW component for it either way. xecs::function::traits binds
            // callback parameters by type alone (confirmed via xecs_archetype_inline.h's
            // _CreateEntities), so a callback with no xecs::component::entity parameter at all is fine
            // - the created entity comes back via this call's own return value instead.
            NestedRoot = ParentEntity.isValid()
                ? CreatePrefabInstance<std::tuple<xecs::component::parent, xecs::editor::prefab_instance>, std::tuple<>>
                  ( 1, InnerRoot
                  , [&]( xecs::component::parent& Parent, xecs::editor::prefab_instance& NewPI ) noexcept { Parent.m_Value = ParentEntity; NewPI = PI; }
                  , /*bRemoveRoot=*/false   // an inner nested root must survive to be spliced in
                  , isVariant
                  )
                : CreatePrefabInstance<std::tuple<xecs::editor::prefab_instance>, std::tuple<>>
                  ( 1, InnerRoot
                  , [&]( xecs::editor::prefab_instance& NewPI ) noexcept { NewPI = PI; }
                  , /*bRemoveRoot=*/false
                  , isVariant
                  );
        }
        else
        {
            // Has extras and/or removals - a second AI review's own suggestion, and the right fix:
            // "full instantiate, then add/remove on root" instead of hand-rolling a second
            // archetype/entity constructor. The OLD approach here built ArchetypeInfos directly from
            // InnerDetails's own data-component list and called CopyPrefabInstanceDefaults to fill
            // it - which, for a multi-entity INNER prefab, blindly copied xecs::component::children
            // too (nothing in this branch ever excluded it the way DetectAndUnionPrefabInstance/
            // ComputePrefabInstanceSaveOverlay's own union/save-overlay loops now do), raw-copying
            // the PREFAB ASSET's own internal (never scene-registered) child entity handles onto
            // NestedRoot instead of recursing into real, live children - the exact same class of bug
            // already fixed on the save/load path, just via a different, never-audited code path,
            // and specifically hit whenever the inner prefab is itself multi-entity AND the outer
            // capture also has extras/removed-component diffs.
            //
            // Fixed by instantiating through the SAME already-proven common-case call below first
            // (which correctly recurses into real children via CreatePrefabInstance's own
            // RemapChildrenSafely - see this exact function's own children-bit branch), THEN
            // migrating the resulting root with AddOrRemoveComponents to add the extras and strip
            // the removed components - no second, riskier construction path, no children-copy bug
            // possible since children is never touched directly here at all.
            NestedRoot = ParentEntity.isValid()
                ? CreatePrefabInstance<std::tuple<xecs::component::parent, xecs::editor::prefab_instance>, std::tuple<>>
                  ( 1, InnerRoot
                  , [&]( xecs::component::parent& Parent, xecs::editor::prefab_instance& NewPI ) noexcept { Parent.m_Value = ParentEntity; NewPI = PI; }
                  , /*bRemoveRoot=*/false
                  , isVariant
                  )
                : CreatePrefabInstance<std::tuple<xecs::editor::prefab_instance>, std::tuple<>>
                  ( 1, InnerRoot
                  , [&]( xecs::editor::prefab_instance& NewPI ) noexcept { NewPI = PI; }
                  , /*bRemoveRoot=*/false
                  , isVariant
                  );

            std::vector<const xecs::component::type::info*> RemovedInfos;
            for( auto& Diff : PI.m_ComponentDiffs )
                if( Diff.m_bAdded == false )
                    if( auto* pInfo = xecs::component::mgr::findComponentTypeInfo(xecs::component::type::guid{Diff.m_ComponentTypeGuid}) )
                        RemovedInfos.push_back(pInfo);

            if( !ExtraInfos.empty() || !RemovedInfos.empty() )
                NestedRoot = m_GameMgr.AddOrRemoveComponents( NestedRoot, { ExtraInfos.data(), ExtraInfos.size() }, { RemovedInfos.data(), RemovedInfos.size() } );

            // Entity's OWN current values for its extras (the inner prefab has no data to derive
            // these from at all - AddOrRemoveComponents only default-constructed the new slots).
            // EDetails is re-fetched fresh here, NOT reused from this function's own top - the
            // recursive instantiate above can create/move entities in other pools, including this
            // exact one if some sibling happens to share Entity's own archetype (the established
            // pool-reallocation hazard this session has hit repeatedly elsewhere).
            if( !ExtraInfos.empty() )
            {
                auto& FreshEDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
                auto& NewDetails    = m_GameMgr.m_ComponentMgr.getEntityDetails(NestedRoot);
                for( auto pInfo : ExtraInfos )
                {
                    const auto iSrcType = FreshEDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
                    const auto iDstType = NewDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
                    if( iSrcType < 0 || iDstType < 0 ) continue;
                    auto pSrc = &FreshEDetails.m_pPool->m_pComponent[iSrcType][ FreshEDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
                    auto pDst = &NewDetails.m_pPool->m_pComponent[iDstType][ NewDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
                    if( pInfo->m_pCopyFn ) pInfo->m_pCopyFn(pDst, pSrc);
                    else                   std::memcpy(pDst, pSrc, pInfo->m_Size);
                }
            }
        }

        Remap.insert( { Entity.m_Value, NestedRoot } );   // outer siblings referencing Entity still resolve
        xecs::persist::details::ApplyPrefabInstancePropertyOverrides( m_GameMgr, NestedRoot );
        return NestedRoot;
    }

/*
    //-------------------------------------------------------------------------------

    template
    < typename T_FUNCTION = xecs::tools::empty_lambda
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
    ) [[nodiscard]] xecs::component::entity 
    AddOrRemoveComponents
    ( xecs::game_mgr::instance&                            GameMgr
    , xecs::component::entity                              Entity
    , const xecs::tools::bits&                             Add
    , const xecs::tools::bits&                             Sub
    , T_FUNCTION&&                                         Function
    ) noexcept
    {
        auto newEntity = GameMgr.m_ArchetypeMgr( Entity, Add, Sub, std::function<T_FUNCTION>(Function) );
        if( newEntity.isZombie() ) return newEntity;

        GameMgr.getEntity( newEntity, [&]( override_tracker* pTracker ) noexcept
        {
            if( pTracker == nullptr ) return;

            const auto  PrefabBits = GameMgr.getArchetype(pTracker->m_PrefabEntity).getComponentBits();
            const auto  EntityBits = GameMgr.getArchetype(newEntity).getComponentBits();

            // Make sure we mark all the relevant components to add/remove
            pTracker->m_DeletedComponents = xecs::tools::bits{};
            pTracker->m_NewComponents     = xecs::tools::bits{};
            for (int i = 0, end = PrefabBits.m_Bits.size() * 64; i < end; ++i)
            {
                if( EntityBits.getBit(i) )
                {
                    if( PrefabBits.getBit(i) == false ) pTracker->m_NewComponents.setBit(i);
                }
                else
                {
                    if( PrefabBits.getBit(i) ) pTracker->m_DeletedComponents.setBit(i);
                }
            }
        });

    }

    //-------------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
    ) [[nodiscard]] xecs::component::entity 
AddOrRemoveComponents
    ( xecs::game_mgr::instance&                            GameMgr
    , xecs::component::entity                              Entity
    , std::span<const xecs::component::type::info* const>  Add
    , std::span<const xecs::component::type::info* const>  Sub
    , T_FUNCTION&&                                         Function
    ) noexcept
    {
        xecs::tools::bits AddBits;
        xecs::tools::bits SubBits;

        for (auto& e : Add) AddBits.setBit(e->m_BitID);
        for (auto& e : Sub) AddBits.setBit(e->m_BitID);

        return AddOrRemoveComponents
        ( GameMgr
        , Entity
        , AddBits
        , SubBits
        , std::forward<T_FUNCTION&&>(Function)
        );
   }

    //-------------------------------------------------------------------------------

    template
    < typename T_TUPLE_ADD
    , typename T_TUPLE_SUBTRACT = std::tuple<>
    , typename T_FUNCTION = xecs::tools::empty_lambda
    > requires
    (  xecs::function::is_callable_v<T_FUNCTION>
    && xecs::types::is_specialized_v<std::tuple, T_TUPLE_ADD>
    && xecs::types::is_specialized_v<std::tuple, T_TUPLE_SUBTRACT>
    ) [[nodiscard]] xecs::component::entity 
    AddOrRemoveComponents
    ( xecs::game_mgr::instance&     GameMgr
    , xecs::component::entity       Entity
    , T_FUNCTION&&                  Function
    ) noexcept
    {
        xecs::tools::bits AddBits;
        xecs::tools::bits SubBits;

        [&]<typename...T>(std::tuple<T...>*){ AddBits.AddFromComponents<T...>(); }( xecs::types::null_tuple_v<T_TUPLE_ADD> );
        [&]<typename...T>(std::tuple<T...>*){ SubBits.AddFromComponents<T...>(); }( xecs::types::null_tuple_v<T_TUPLE_SUBTRACT> );

        return AddOrRemoveComponents
        ( GameMgr
        , Entity
        , AddBits
        , SubBits
        , Function
        );
    }
    */

    namespace details
    {
        //-----------------------------------------------------------------------------------------
        // On-disk path - the same real GUID-sharded Descriptors/Prefab/<b0>/<b1>/<guid>.desc/
        // convention xecs::scene::mgr/xecs::level::mgr use (see xecs_scene_inline.h's identical
        // helper for the full rationale). A prefab group is always loaded/saved wholesale (unlike
        // Scene, which needs incremental per-entity IO for potentially huge entity counts) - so unlike
        // Scene's sharded entity_db, every member's data lives in ONE file (Entity.txt) directly in
        // this folder, rewritten in full on every Save.
        //-----------------------------------------------------------------------------------------
        inline std::wstring PrefabFolder( mgr& Mgr, guid PrefabGuid ) noexcept
        {
            const auto Value = PrefabGuid.m_Instance.m_Value;
            const auto Byte0 = std::format( L"{:02X}", static_cast<std::uint8_t>( Value       & 0xFF) );
            const auto Byte1 = std::format( L"{:02X}", static_cast<std::uint8_t>((Value >> 8) & 0xFF) );
            return Mgr.m_ProjectPath + L"/Descriptors/Prefab/" + Byte0 + L"/" + Byte1 + L"/" + std::format(L"{:X}", Value) + L".desc";
        }

        //-----------------------------------------------------------------------------------------
        // GUID-like minting (not sequential) for a prefab group member's local_id - same
        // merge-collision reasoning already applied to E29's own NextFreeEntityId (two branches
        // independently adding a child to the same group must not collide). Collision-checked
        // against the group's own current membership before returning.
        //-----------------------------------------------------------------------------------------
        inline local_id NextFreeLocalId( const group_bookkeeping& Group ) noexcept
        {
            for(;;)
            {
                const auto Full      = xresource::guid_generator::Instance64();
                const auto Candidate = static_cast<local_id>( (Full >> 32) ^ (Full & 0xFFFFFFFFull) );
                if( Candidate == invalid_local_id_v ) continue;
                if( Group.m_LocalToRuntime.find(Candidate) == Group.m_LocalToRuntime.end() )
                    return Candidate;
            }
        }

        //-----------------------------------------------------------------------------------------
        // Recursive xecs::component::children walk from Root, collecting every live member of the
        // group (root included) into Out - stops at (but still includes) any member carrying
        // xecs::editor::prefab_instance without descending into ITS children: a nested instance's own
        // internal structure belongs to the inner prefab, not this outer group, so treating it as
        // anything but a leaf here would double-count the inner prefab's own children as if they were
        // plain outer-group members.
        //-----------------------------------------------------------------------------------------
        inline void CollectGroupMembers( xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity, std::vector<xecs::component::entity>& Out ) noexcept
        {
            Out.push_back(Entity);

            auto& Details   = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            auto& Archetype = *Details.m_pPool->m_pArchetype;

            // findIndexComponentFromInfo, not getComponentBits().getBit() - see
            // [[xecs_getbit_vs_findindexcomponentfrominfo]]. This was the 7th occurrence of the same
            // fragile idiom, missed by an earlier "full sweep" that grepped for the exact literal
            // expression instead of every .getBit( call - it went unnoticed here specifically because
            // this call site extracts the bit into a local first (`Bit != invalid && ...getBit(Bit)`),
            // not `...getBit(info_v<T>.m_BitID)` directly, so the earlier grep's literal string never
            // matched it.
            if( Details.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> ) >= 0 )
                return;

            if( Archetype.getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) )
            {
                // Snapshot, not a live reference into this entity's own pool slot - the recursive call
                // below can create/move entities in other pools, and if a sibling happens to land in
                // the SAME pool/archetype as THIS entity, that reallocates the storage backing
                // Children.m_List out from under an iterator/reference held across the call (the exact
                // hazard already fixed in CloneEntityIntoPrefabGroup's own children walk - this is the
                // Save-side equivalent, and the actual cause of two children saving out identical).
                auto ChildList = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
                for( auto& Child : ChildList )
                    CollectGroupMembers(GameMgr, Child, Out);
            }
        }

        //-----------------------------------------------------------------------------------------
        // A member already known to Group (by runtime handle) keeps its existing local_id, stable
        // across re-saves; a genuinely new member (added since the group was last loaded/saved) mints
        // a fresh one; a member no longer reachable is pruned from the bookkeeping maps only - never
        // entity-deleted, matching Scene's "deletion is an explicit action" philosophy.
        //-----------------------------------------------------------------------------------------
        inline void ReconcileGroupLocalIds( group_bookkeeping& Group, const std::vector<xecs::component::entity>& Members ) noexcept
        {
            std::unordered_map<local_id, xecs::component::entity>   NewLocalToRuntime;
            std::unordered_map<std::uint64_t, local_id>              NewRuntimeToLocal;

            for( auto& E : Members )
            {
                local_id Id;
                if( auto It = Group.m_RuntimeToLocal.find(E.m_Value); It != Group.m_RuntimeToLocal.end() )
                {
                    Id = It->second;
                }
                else
                {
                    // Collision-check against both the group's existing ids AND anything already
                    // minted earlier in this same reconciliation pass.
                    do { Id = NextFreeLocalId(Group); } while( NewLocalToRuntime.find(Id) != NewLocalToRuntime.end() );
                }

                NewLocalToRuntime[Id]        = E;
                NewRuntimeToLocal[E.m_Value] = Id;
            }

            // Anything not in Members any more is implicitly pruned here (never entity-deleted -
            // matches Scene's "deletion is an explicit action" philosophy).
            Group.m_LocalToRuntime = std::move(NewLocalToRuntime);
            Group.m_RuntimeToLocal = std::move(NewRuntimeToLocal);
        }

        //-----------------------------------------------------------------------------------------
        // One member's on-disk record - the same EntityInfo/ComponentTypes/SerializeOneComponent
        // shape xecs::scene::mgr::SaveEntity/LoadEntity use, "PermanentId" renamed "LocalId", minus
        // the Scene-only prefab-overlay bookkeeping (a prefab group member CAN itself be a nested
        // prefab instance - xecs::persist::details::ComputePrefabInstanceSaveOverlay/
        // DetectAndUnionPrefabInstance handle that identically either way).
        //-----------------------------------------------------------------------------------------
        inline xerr SaveGroupMember( xecs::game_mgr::instance& GameMgr, xecs::serializer::stream& TextFile, local_id Id, xecs::component::entity Entity, const group_bookkeeping& Group ) noexcept
        {
            auto& EDetails  = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            auto& Archetype = *EDetails.m_pPool->m_pArchetype;
            auto  DataSpan  = Archetype.getDataComponentInfos();

            std::vector<const xecs::component::type::info*> Infos;
            std::vector<std::uint64_t>                       PrefabOwnedGuids;
            xecs::persist::details::ComputePrefabInstanceSaveOverlay( GameMgr, Entity, DataSpan, Infos, PrefabOwnedGuids );

            // xecs::prefab::root never gets its own data section (its m_Guid is redundant with the
            // file's own location, matching the original single-entity design) - only the root member
            // ever carries this component at all, so this is a no-op for every other member.
            std::erase( Infos, &xecs::component::type::info_v<xecs::prefab::root> );

            local_id WriteId      = Id;
            int      nComponents = static_cast<int>(Infos.size());
            if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("LocalId",     WriteId))
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

            // Same-group-only resolver - a prefab has no equivalent of Scene's declared-parent/
            // external-ref-table for anything outside its own membership (see
            // xecs::persist::details::ResolveReferenceForSave's own comment).
            auto GroupResolve = [&]( xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept -> bool
            {
                if( auto It = Group.m_RuntimeToLocal.find(Target.m_Value); It != Group.m_RuntimeToLocal.end() )
                {
                    OutEncoded = static_cast<std::int64_t>(It->second);
                    return true;
                }
                return false;
            };

            for( auto pInfo : Infos )
            {
                std::printf("[Prefab::SaveGroupMember DEBUG] Id=%u writing component '%s' ReferenceMode=%d\n", Id, pInfo->m_pName, (int)pInfo->m_ReferenceMode);
                std::fflush(stdout);
                const auto iType = EDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
                assert(iType >= 0);
                auto pLive = &EDetails.m_pPool->m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

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
                        std::printf("[Prefab::SaveGroupMember DEBUG]  BY_FUNCTION reported %zu reference(s)\n", References.size());
                        std::fflush(stdout);

                        for( auto pRef : References )
                        {
                            std::int64_t Encoded = 0;
                            if( false == xecs::persist::details::ResolveReferenceForSave(*pRef, Encoded, GroupResolve) )
                            {
                                // Debug: loud - referencing something outside this prefab's own group
                                // is an authoring-time mistake. Release: fail gracefully rather than
                                // crash a shipped game over a dangling ref.
                                xassert(false);
                                std::printf("[Prefab::SaveGroupMember] WARNING: reference target is outside this prefab's own group - encoding as null\n");
                                std::fflush(stdout);
                                Encoded = 0;
                            }
                            *pRef = xecs::persist::details::EncodeRef(Encoded);
                        }
                    }
                    else
                    {
                        std::printf("[Prefab::SaveGroupMember DEBUG]  property-based (AUTO) reference scan starting\n");
                        std::fflush(stdout);
                        xproperty::settings::context Context{};
                        std::string                  SetError;
                        xproperty::sprop::collector( Scratch.data(), *pInfo->m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void* ) noexcept
                        {
                            if( Data.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v )
                            {
                                auto RawTarget = Data.get<xecs::component::entity>();
                                std::printf("[Prefab::SaveGroupMember DEBUG]   found entity property '%s' Target=0x%llx valid=%d\n", pPropertyName, (unsigned long long)RawTarget.m_Value, RawTarget.isValid());
                                std::fflush(stdout);
                                std::int64_t Encoded = 0;
                                if( false == xecs::persist::details::ResolveReferenceForSave(RawTarget, Encoded, GroupResolve) )
                                {
                                    xassert(false);
                                    std::printf("[Prefab::SaveGroupMember] WARNING: reference target is outside this prefab's own group - encoding as null\n");
                                    std::fflush(stdout);
                                    Encoded = 0;
                                }
                                Data.get<xecs::component::entity>() = xecs::persist::details::EncodeRef(Encoded);
                                xproperty::sprop::setProperty( SetError, Scratch.data(), *pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ pPropertyName, Data }, Context );
                            }
                        });
                        std::printf("[Prefab::SaveGroupMember DEBUG]  property-based (AUTO) reference scan done\n");
                        std::fflush(stdout);
                    }
                }

                if( pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance> )
                {
                    auto& PI_Scratch = *reinterpret_cast<xecs::editor::prefab_instance*>(Scratch.data());
                    xecs::persist::details::RefreshPrefabInstanceOverlayRecord( GameMgr, Entity, DataSpan, PrefabOwnedGuids, PI_Scratch );
                }

                const auto Error = xecs::persist::details::SerializeOneComponent(TextFile, false, *pInfo, Scratch.data());
                if( pInfo->m_pDestructFn ) pInfo->m_pDestructFn( Scratch.data() );
                if( Error ) return Error;
                std::printf("[Prefab::SaveGroupMember DEBUG]  component '%s' done\n", pInfo->m_pName);
                std::fflush(stdout);
            }

            return {};
        }

        //-----------------------------------------------------------------------------------------
        // Read-side counterpart of SaveGroupMember - mirrors xecs::scene::mgr::LoadEntity's own shape
        // via the same shared xecs::persist::details helpers, minus the Scene-only path/dependency
        // concerns. Every member gets xecs::prefab::tag added back (excluded from what SaveGroupMember
        // writes, same as the original single-entity design - a TAG component carries no data at all,
        // so it was never part of DataSpan/Infos in the first place); the one member whose file LocalId
        // matches RootLocalId additionally gets xecs::prefab::root added back and its m_Guid stamped
        // (root's own identity isn't persisted as component data either - PrefabGuid is already known
        // from the file's own location, writing it a second time would just be the same value again).
        // Registers the newly-created entity into Group's maps under the file's own LocalId before
        // returning (needed so a LATER member's reference-remap pass, or a later member in the same
        // file whose OWN reference points at this one, can already find it).
        //-----------------------------------------------------------------------------------------
        inline xerr LoadGroupMember( xecs::game_mgr::instance& GameMgr, xecs::serializer::stream& TextFile, group_bookkeeping& Group, local_id RootLocalId, guid PrefabGuid ) noexcept
        {
            local_id FileId      = invalid_local_id_v;
            int      nComponents = 0;
            if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("LocalId",     FileId))
                    ||(Error = TextFile.Field("nComponents", nComponents));
                }
            ); Err )
            {
                std::printf("[Prefab::LoadGroupMember] failed reading EntityInfo (%s)\n", std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
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
                ); Err )
                {
                    std::printf("[Prefab::LoadGroupMember] FileId=%u : failed reading ComponentTypes (%s)\n", FileId, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    return Err;
                }
            }

            for( auto pInfo : Infos )
                if( pInfo == nullptr )
                    return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab group member file references a component type that is no longer registered">();

            const bool bIsRoot = (FileId == RootLocalId);

            std::vector<const xecs::component::type::info*> ArchetypeInfos;
            xecs::editor::prefab_instance                    TempPI;
            xecs::component::entity                          PrefabRootEntity{};
            if( auto Err = xecs::persist::details::DetectAndUnionPrefabInstance( GameMgr, TextFile, Infos, ArchetypeInfos, TempPI, PrefabRootEntity ); Err )
            {
                std::printf("[Prefab::LoadGroupMember] FileId=%u : DetectAndUnionPrefabInstance failed (%s)\n", FileId, std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
            const bool bIsPrefabInstance = PrefabRootEntity.isValid();

            ArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::prefab::tag> );
            if( bIsRoot ) ArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::prefab::root> );

            auto& Archetype = GameMgr.getOrCreateArchetype( { ArchetypeInfos.data(), ArchetypeInfos.size() } );

            // Tag components carry no data and are never part of a pool's per-entity DATA storage -
            // CreateEntity's Infos/MoveData span must only list DATA components.
            std::vector<const xecs::component::type::info*> DataInfos;
            DataInfos.reserve(ArchetypeInfos.size());
            for( auto pInfo : ArchetypeInfos )
                if( pInfo->m_TypeID != xecs::component::type::id::TAG )
                    DataInfos.push_back(pInfo);

            std::vector<std::byte*> MoveData( DataInfos.size(), nullptr );
            auto NewEntity = Archetype.CreateEntity( { DataInfos.data(), DataInfos.size() }, { MoveData.data(), MoveData.size() } );

            auto& EDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            auto& Pool     = *EDetails.m_pPool;

            if( bIsPrefabInstance )
                xecs::persist::details::CopyPrefabInstanceDefaults( GameMgr, PrefabRootEntity, NewEntity, ArchetypeInfos );

            for( auto pInfo : Infos )
            {
                if( pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance> )
                {
                    Pool.getComponent<xecs::editor::prefab_instance>(EDetails.m_PoolIndex) = std::move(TempPI);
                    continue;
                }

                const auto iType = Pool.findIndexComponentFromInfo(*pInfo);
                assert(iType >= 0);
                auto pData = &Pool.m_pComponent[iType][ EDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

                if( auto Err = xecs::persist::details::SerializeOneComponent(TextFile, true, *pInfo, pData); Err )
                {
                    std::printf("[Prefab::LoadGroupMember] FileId=%u : component '%s' failed to read (%s)\n", FileId, pInfo->m_pName, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    auto E = NewEntity;
                    GameMgr.DeleteEntity(E);
                    return Err;
                }
            }

            // ApplyPrefabInstancePropertyOverrides is NOT called here anymore - see this prefab's own
            // EnsureLoaded, third pass, after the whole group's reference remap. Same reasoning as
            // Scene::LoadEntity's own matching removal: an override whose m_MemberPath is non-empty
            // needs a REAL children.m_List to walk, which this one member's own children field isn't
            // yet - it still holds raw, un-remapped encoded values until every member in the group has
            // loaded AND the group-wide remap pass has run.

            if( bIsRoot )
                Pool.getComponent<xecs::prefab::root>(EDetails.m_PoolIndex).m_Guid = PrefabGuid;

            Group.m_LocalToRuntime[FileId]            = NewEntity;
            Group.m_RuntimeToLocal[NewEntity.m_Value]  = FileId;

            return {};
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    xecs::component::entity mgr::CloneEntityIntoPrefabGroup( xecs::component::entity Source, group_bookkeeping& Group, bool bIsRoot ) noexcept
    {
        auto& SourceDetails   = m_GameMgr.m_ComponentMgr.getEntityDetails(Source);
        auto& SourceArchetype = *SourceDetails.m_pPool->m_pArchetype;
        auto  DataSpan        = SourceArchetype.getDataComponentInfos();

        // New archetype = Source's current data components (excluding "entity", added automatically
        // by CreateEntity below) + prefab::tag (every member) + prefab::root (root only) - the exact
        // same bit setup CreatePrefab<T...>() forces onto every compile-time-authored prefab.
        std::vector<const xecs::component::type::info*> Infos;
        Infos.reserve(DataSpan.size() + 3);
        Infos.push_back( &xecs::component::type::info_v<xecs::component::entity> );
        Infos.push_back( &xecs::component::type::info_v<xecs::prefab::tag> );
        if( bIsRoot ) Infos.push_back( &xecs::component::type::info_v<xecs::prefab::root> );
        for( auto pInfo : DataSpan )
        {
            if( pInfo == &xecs::component::type::info_v<xecs::component::entity> ) continue;
            // A prefab root never carries its own parent link - it's the top of ITS OWN hierarchy,
            // regardless of whether the live entity being converted happened to have one before
            // conversion (E29's "Make Prefab" UI is responsible for reparenting the live scene entity
            // BEFORE calling this - this exclusion only concerns the CLONE).
            if( bIsRoot && pInfo == &xecs::component::type::info_v<xecs::component::parent> ) continue;
            Infos.push_back(pInfo);
        }

        auto& NewArchetype = m_GameMgr.getOrCreateArchetype( { Infos.data(), Infos.size() } );

        // CreateEntity's Infos/MoveData span only walks a pool's per-entity DATA storage - tag
        // components (xecs::prefab::tag included) carry no data and are never part of a pool's
        // per-component array. They still belong in Infos above (the archetype's bit identity needs
        // them) - just not here.
        std::vector<const xecs::component::type::info*> DataInfos;
        DataInfos.reserve(Infos.size());
        for( auto pInfo : Infos )
            if( pInfo->m_TypeID != xecs::component::type::id::TAG )
                DataInfos.push_back(pInfo);

        std::vector<std::byte*> MoveData( DataInfos.size(), nullptr );
        auto NewEntity = NewArchetype.CreateEntity( { DataInfos.data(), DataInfos.size() }, { MoveData.data(), MoveData.size() } );

        auto& NewDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
        auto& NewPool    = *NewDetails.m_pPool;

        // Mint and register BEFORE recursing into children below - a later reference-remap pass or
        // diagnostic walk touching the group mid-clone needs this entity's id to already exist.
        const auto Id = details::NextFreeLocalId(Group);
        Group.m_LocalToRuntime[Id]                = NewEntity;
        Group.m_RuntimeToLocal[NewEntity.m_Value] = Id;

        // Copy each of SOURCE's own live component values across (iterating DataSpan - Source's own
        // component list - not DataInfos/the NEW archetype's list, which for the root member also
        // includes xecs::prefab::root; Source, a live plain scene entity, never has that component
        // itself, so iterating the new archetype's list here would try to copy a component Source
        // doesn't have at all) - except parent/children (structural, rebuilt below rather than
        // raw-copied, since a raw copy would carry Source's live, meaningless-after-clone entity
        // handles) and entity itself (no data of its own). editor::prefab_instance, if present, copies
        // plain like any other component - a live prefab-instance entity's archetype already holds its
        // full RESOLVED current values, which is exactly what makes the resulting group member a
        // genuine nested-prefab-instance record once persisted, with no special-casing needed here.
        for( auto pInfo : DataSpan )
        {
            if( pInfo == &xecs::component::type::info_v<xecs::component::entity> )   continue;
            if( pInfo == &xecs::component::type::info_v<xecs::component::parent> )   continue;
            if( pInfo == &xecs::component::type::info_v<xecs::component::children> ) continue;

            const auto iSrcType = SourceDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
            const auto iDstType = NewPool.findIndexComponentFromInfo(*pInfo);
            assert(iSrcType >= 0 && iDstType >= 0);

            auto pSrc = &SourceDetails.m_pPool->m_pComponent[iSrcType][ SourceDetails.m_PoolIndex.m_Value * pInfo->m_Size ];
            auto pDst = &NewPool.m_pComponent[iDstType][ NewDetails.m_PoolIndex.m_Value * pInfo->m_Size ];

            if( pInfo->m_pCopyFn ) pInfo->m_pCopyFn(pDst, pSrc);
            else                   std::memcpy(pDst, pSrc, pInfo->m_Size);
        }

        // root.m_Guid is stamped by CreatePrefabFromEntity right after this call returns (the only
        // caller of a bIsRoot=true clone, and already knows PrefabGuid) - avoids threading an extra
        // parameter through every recursive non-root call just for the one entity that needs it.

        // Critical exception: a nested prefab instance is captured as a single opaque member - never
        // recurse into ITS OWN children here, which belong to the inner prefab, not this outer group
        // (same exclusion Save's own CollectGroupMembers walk applies). Checked via
        // findIndexComponentFromInfo, not getComponentBits().getBit() - see
        // [[xecs_getbit_vs_findindexcomponentfrominfo]]: this exact .getBit() idiom was already found
        // unreliable at 5 other call sites this session and missed here - if it misses here too, this
        // branch never fires and the code below wrongly recurses into a multi-entity prefab instance's
        // OWN inner children as if they were plain group members, corrupting the outer group with
        // clones of entities that don't belong to it.
        if( SourceDetails.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::editor::prefab_instance> ) >= 0 )
        {
            return NewEntity;
        }

        if( SourceArchetype.getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) )
        {
            auto& SourceChildren = SourceDetails.m_pPool->getComponent<xecs::component::children>(SourceDetails.m_PoolIndex);

            // Snapshot, not a live reference - the recursive CloneEntityIntoPrefabGroup call below can
            // create entities in OTHER pools (including, in principle, this exact one, if some sibling
            // happens to share NewEntity's own archetype) which may reallocate pool storage; walking a
            // copy of Source's own list (read-only, never mutated) sidesteps that entirely rather than
            // relying on SourceChildren staying valid across the recursion.
            auto SourceChildList = SourceChildren.m_List;

            std::vector<xecs::component::entity> NewChildList;
            NewChildList.reserve(SourceChildList.size());
            for( auto& Child : SourceChildList )
            {
                auto NewChild = CloneEntityIntoPrefabGroup( Child, Group, /*bIsRoot=*/false );
                NewChildList.push_back(NewChild);

                auto& NewChildDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(NewChild);
                if( NewChildDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID) )
                    NewChildDetails.m_pPool->getComponent<xecs::component::parent>(NewChildDetails.m_PoolIndex).m_Value = NewEntity;
            }

            // Re-fetch NewEntity's own details/pool fresh, right before writing NewChildList into it -
            // any of the recursive clones above could have caused THIS pool to reallocate (a sibling
            // landing in the same archetype/pool as NewEntity itself), so NewDetails/NewPool (captured
            // before the recursion started) are not trusted to still be valid here.
            auto& FreshNewDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            FreshNewDetails.m_pPool->getComponent<xecs::component::children>(FreshNewDetails.m_PoolIndex).m_List = std::move(NewChildList);
        }

        return NewEntity;
    }

    //--------------------------------------------------------------------------------------------------------------

    guid mgr::CreatePrefabFromEntity( xecs::component::entity Source, guid PrefabGuid ) noexcept
    {
        auto& Group = m_PrefabGroups[PrefabGuid.m_Instance.m_Value];
        auto  Root  = CloneEntityIntoPrefabGroup( Source, Group, /*bIsRoot=*/true );

        auto& RootDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Root);
        RootDetails.m_pPool->getComponent<xecs::prefab::root>(RootDetails.m_PoolIndex).m_Guid = PrefabGuid;

        m_PrefabList.insert({ PrefabGuid.m_Instance.m_Value, Root });
        return PrefabGuid;
    }

    //--------------------------------------------------------------------------------------------------------------

    xerr mgr::Save( guid PrefabGuid ) noexcept
    {
        auto It = m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if( It == m_PrefabList.end() )
            return xerr::create<xecs::game_mgr::state::FAILURE, "prefab::mgr::Save: prefab is not resident - call EnsureLoaded/CreatePrefabFromEntity first">();

        auto  RootEntity = It->second;
        auto& Group      = m_PrefabGroups[PrefabGuid.m_Instance.m_Value];

        std::vector<xecs::component::entity> Members;
        details::CollectGroupMembers( m_GameMgr, RootEntity, Members );
        details::ReconcileGroupLocalIds( Group, Members );

        const auto Folder = details::PrefabFolder(*this, PrefabGuid);
        std::error_code Ec;
        std::filesystem::create_directories( std::filesystem::path(Folder), Ec );

        // The real, reflected descriptor - a read-only diagnostic listing of the union of every
        // member's component types (not just the root's own).
        descriptor Descriptor;
        for( auto& Member : Members )
        {
            auto& MDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Member);
            for( auto pInfo : MDetails.m_pPool->m_pArchetype->getDataComponentInfos() )
            {
                if( pInfo == &xecs::component::type::info_v<xecs::component::entity> ) continue;
                if( std::find(Descriptor.m_ComponentTypeGuids.begin(), Descriptor.m_ComponentTypeGuids.end(), pInfo->m_Guid.m_Value) == Descriptor.m_ComponentTypeGuids.end() )
                    Descriptor.m_ComponentTypeGuids.push_back(pInfo->m_Guid.m_Value);
            }
        }

        xproperty::settings::context Context;
        if( auto Err = Descriptor.Serialize( false, Folder + L"/Descriptor.txt", Context ); Err )
            return Err;

        // Written wholesale, always - a prefab group is always loaded/saved as one atomic unit (no
        // per-entity incremental IO concern here the way Scene's m_PendingChanges exists for).
        xecs::serializer::stream TextFile;
        if( auto Err = TextFile.Open( false, Folder + L"/Entity.txt", xtextfile::file_type::TEXT, xtextfile::flags{ .m_isWriteFloats = true } ); Err )
            return Err;

        const auto RootLocalId = Group.m_RuntimeToLocal.at(RootEntity.m_Value);
        int        nMembers    = static_cast<int>(Members.size());
        local_id   WriteRootId = RootLocalId;
        if( auto Err = TextFile.Record( "PrefabGroupInfo", [&]( xerr& Error ) noexcept
            {
                  (Error = TextFile.Field("nMembers",    nMembers))
                ||(Error = TextFile.Field("RootLocalId", WriteRootId));
            }
        ); Err ) return Err;

        for( auto& Member : Members )
        {
            const auto Id = Group.m_RuntimeToLocal.at(Member.m_Value);
            auto& MDetails2 = m_GameMgr.m_ComponentMgr.getEntityDetails(Member);
            std::printf("[Prefab::Save DEBUG] member Entity=0x%llx Id=%u DataComponents:", (unsigned long long)Member.m_Value, Id);
            for( auto pInfo : MDetails2.m_pPool->m_pArchetype->getDataComponentInfos() ) std::printf(" %s", pInfo->m_pName);
            std::printf("\n");
            std::fflush(stdout);
            if( auto Err = details::SaveGroupMember( m_GameMgr, TextFile, Id, Member, Group ); Err )
                return Err;
            std::printf("[Prefab::Save DEBUG] member Entity=0x%llx SaveGroupMember returned OK\n", (unsigned long long)Member.m_Value);
            std::fflush(stdout);
        }

        return {};
    }

    //--------------------------------------------------------------------------------------------------------------

    xerr mgr::EnsureLoaded( guid PrefabGuid ) noexcept
    {
        std::printf("[Prefab::EnsureLoaded] Guid=%llX : begin\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value));
        std::fflush(stdout);

        if( m_PrefabList.find(PrefabGuid.m_Instance.m_Value) != m_PrefabList.end() )
        {
            std::printf("[Prefab::EnsureLoaded] Guid=%llX : already resident\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value));
            std::fflush(stdout);
            return {};
        }

        const auto Folder = details::PrefabFolder(*this, PrefabGuid);

        xecs::serializer::stream TextFile;
        if( auto Err = TextFile.Open( true, Folder + L"/Entity.txt", xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
        {
            std::printf("[Prefab::EnsureLoaded] Guid=%llX : failed to open Entity.txt (%s)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), std::string(Err.getMessage()).c_str());
            std::fflush(stdout);
            return Err;
        }

        int      nMembers    = 0;
        local_id RootLocalId = invalid_local_id_v;
        if( auto Err = TextFile.Record( "PrefabGroupInfo", [&]( xerr& Error ) noexcept
            {
                  (Error = TextFile.Field("nMembers",    nMembers))
                ||(Error = TextFile.Field("RootLocalId", RootLocalId));
            }
        ); Err )
        {
            std::printf("[Prefab::EnsureLoaded] Guid=%llX : failed to read PrefabGroupInfo (%s) - this prefab asset is almost certainly in the OLD single-entity on-disk format from before this session's multi-entity rework, and needs to be recreated (drag the entity onto the asset browser again to make a fresh one)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), std::string(Err.getMessage()).c_str());
            std::fflush(stdout);
            return Err;
        }
        std::printf("[Prefab::EnsureLoaded] Guid=%llX : PrefabGroupInfo read OK, nMembers=%d RootLocalId=%u\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), nMembers, RootLocalId);
        std::fflush(stdout);

        auto& Group = m_PrefabGroups[PrefabGuid.m_Instance.m_Value];

        for( int i = 0; i < nMembers; ++i )
        {
            if( auto Err = details::LoadGroupMember( m_GameMgr, TextFile, Group, RootLocalId, PrefabGuid ); Err )
            {
                std::printf("[Prefab::EnsureLoaded] Guid=%llX : member %d failed (%s)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), i, std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
        }
        std::printf("[Prefab::EnsureLoaded] Guid=%llX : all %d member(s) loaded, resolving root\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), nMembers);
        std::fflush(stdout);

        auto RootIt = Group.m_LocalToRuntime.find(RootLocalId);
        if( RootIt == Group.m_LocalToRuntime.end() )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab group file's RootLocalId does not match any loaded member">();

        // Second pass, mirroring Scene's own two-pass EnsureLoaded shape (load everything first,
        // remap references once every member exists).
        std::printf("[Prefab::EnsureLoaded] Guid=%llX : remapping references across %zu member(s)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), Group.m_LocalToRuntime.size());
        std::fflush(stdout);
        for( auto& Pair : Group.m_LocalToRuntime )
        {
            xecs::persist::details::RemapLoadedEntityReferences( m_GameMgr, Pair.second, [&]( std::int64_t Encoded ) noexcept -> xecs::component::entity
            {
                if( Encoded == 0 ) return xecs::component::entity{};
                // A prefab group has no equivalent of Scene's external-ref table - every reference
                // must target another member of the same group (see ResolveReferenceForSave's own
                // save-side comment for why this stays that strict, at least for now).
                if( Encoded <= 0 )
                {
                    std::printf("[Prefab::EnsureLoaded] Guid=%llX : WARNING encoded reference %lld is not a same-group positive local id - encoding as null\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), static_cast<long long>(Encoded));
                    std::fflush(stdout);
                    return xecs::component::entity{};
                }
                auto It = Group.m_LocalToRuntime.find( static_cast<local_id>(Encoded) );
                return It != Group.m_LocalToRuntime.end() ? It->second : xecs::component::entity{};
            });
        }

        // Third pass, after every member is loaded AND remapped: only now is it safe to apply each
        // nested-instance member's own recorded property overrides (a non-empty m_MemberPath needs a
        // REAL children.m_List to walk) - see Scene::EnsureLoaded's own matching third pass for the
        // full reasoning (calling this any earlier treated raw, un-remapped reference data as a live
        // entity handle and crashed hard, no assert, just a silent exit).
        for( auto& Pair : Group.m_LocalToRuntime )
        {
            auto& Details = m_GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
            if( Details.m_pPool && Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0 )
                xecs::persist::details::ApplyPrefabInstancePropertyOverrides( m_GameMgr, Pair.second );
        }

        m_PrefabList.insert({ PrefabGuid.m_Instance.m_Value, RootIt->second });
        std::printf("[Prefab::EnsureLoaded] Guid=%llX : end (success)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value));
        std::fflush(stdout);
        return {};
    }
}