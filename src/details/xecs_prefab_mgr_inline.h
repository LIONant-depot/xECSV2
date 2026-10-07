#include <filesystem>
#include <format>
#include <atomic>
#include <unordered_set>

// xecs_prefab_recipe_inline.h (included after this file): a nested instance's recipe on what CreatePrefabInstance made of it.
namespace xecs::prefab::recipe
{
    inline void ApplyNestedRecipeLive( xecs::game_mgr::instance&, xecs::component::entity, const xecs::editor::prefab_instance& ) noexcept;
    inline void ConvertNestedRecipe  ( xecs::game_mgr::instance&, xecs::component::entity ) noexcept;
}

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
                       || xecs::component::type::IsComponentType<xecs::component::children>(pInfo)
                       || xecs::component::type::IsComponentType<xecs::component::parent>(pInfo))
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
        // A prefab of the scene format spawns from its baked plan (Spawn: batched, built, nested prefabs flattened). The rest - a prefab made in code
        // (CreatePrefab<T...>), a variant being made, the root left out, components added or removed - is the clone below.
        if constexpr( std::is_same_v<T_ADD_TUPLE, std::tuple<>> && std::is_same_v<T_SUB_TUPLE, std::tuple<>> )
        {
            if( false == bRemoveRoot && false == isVariant && m_PrefabGroups.contains(PrefabGuid.m_Instance.m_Value) )
                return false == Spawn( PrefabGuid, Count, std::forward<T_CALLBACK&&>(Callback) ).empty();
        }
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
            if( xecs::component::type::IsComponentType<xecs::component::entity>(pInfo) )      continue;
            if( xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(pInfo) ) continue;
            if( xecs::component::type::IsComponentType<xecs::component::parent>(pInfo) )       continue;
            if( xecs::component::type::IsComponentType<xecs::component::children>(pInfo) )    continue;   // a nested-instance member never has its own - see CloneEntityIntoPrefabGroup's opaque-leaf exclusion
            if( xecs::component::type::IsComponentType<xecs::prefab::root>(pInfo) )           continue;   // Entity's own group-root identity, if any - never carried by a PLACED instance
            if( InnerDetails.m_pPool->findIndexComponentFromInfo(*pInfo) >= 0 )          continue;   // the inner prefab already defines this - not an "extra"
            ExtraInfos.push_back(pInfo);
        }
        {
            // Tags have no pool slot, so "does the inner prefab define it" is an archetype-bits check.
            std::vector<const xecs::component::type::info*> TagInfos;
            xecs::persist::details::AppendPersistentTagInfos(*EDetails.m_pPool->m_pArchetype, TagInfos);
            for( auto pInfo : TagInfos )
                if( false == InnerDetails.m_pPool->m_pArchetype->getComponentBits().getBit(pInfo->m_BitID) )
                    ExtraInfos.push_back(pInfo);
        }

        // The other half of the same "place != load" gap: PI.m_ComponentDiffs' m_bAdded=false entries
        // record a component the inner prefab DEFINES that Entity deliberately REMOVED (the same
        // add/remove tracking DetectAndUnionPrefabInstance already excludes on the LOAD path) - without
        // checking this here too, placing this nested instance would silently resurrect a component
        // the user explicitly removed, every time.
        bool bHasRemovals = false;
        for( auto& Diff : PI.m_ComponentDiffs )
            if( Diff.m_bAdded == false && Diff.m_Member.empty() ) { bHasRemovals = true; break; }

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
                if( Diff.m_bAdded == false && Diff.m_Member.empty() )
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
        // The nested recipe on what was made: its members' component diffs, its overrides (addressed by member ids), its removals.
        xecs::prefab::recipe::ApplyNestedRecipeLive( m_GameMgr, NestedRoot, PI );
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
        // helper for the full rationale). It holds a scene (prefabs_plan.md, phase 1): Descriptor.txt,
        // entity_db/ and ComponentDeps.txt, as a Scene's folder does. A prefab is always loaded/saved
        // wholesale (unlike Scene, which needs incremental per-entity IO for potentially huge entity
        // counts). Before phase 1 every member lived in ONE file, Entity.txt (EnsureLoadedOldFormat).
        //-----------------------------------------------------------------------------------------
        inline std::wstring PrefabFolder( mgr& Mgr, guid PrefabGuid ) noexcept
        {
            const auto Value = PrefabGuid.m_Instance.m_Value;
            const auto Byte0 = std::format( L"{:02X}", static_cast<std::uint8_t>( Value       & 0xFF) );
            const auto Byte1 = std::format( L"{:02X}", static_cast<std::uint8_t>((Value >> 8) & 0xFF) );
            return Mgr.m_ProjectPath + L"/Descriptors/Prefab/" + Byte0 + L"/" + Byte1 + L"/" + std::format(L"{:X}", Value) + L".desc";
        }

        //-----------------------------------------------------------------------------------------
        // The Game a prefab's folder says it plays with (empty: none, or the folder has no scene-format descriptor yet).
        //-----------------------------------------------------------------------------------------
        inline xecs::level::game_ref ReadDescriptorGame( const std::wstring& Folder ) noexcept
        {
            const auto      Path = Folder + L"/Descriptor.txt";
            std::error_code Ec;
            if( false == std::filesystem::exists( std::filesystem::path(Path), Ec ) ) return {};
            descriptor                   Descriptor;
            xproperty::settings::context Context;
            if( Descriptor.Serialize( true, Path, Context ) ) return {};
            return Descriptor.m_Game;
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
        // The OLD format's member reader (one record of Entity.txt, see EnsureLoadedOldFormat): the
        // read-side counterpart of the SaveGroupMember that wrote it - mirrors xecs::scene::mgr::LoadEntity's own shape
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
            std::uint32_t FileId32   = 0;                       // the old format is from before the ids were widened: 32 bits
            int           nComponents = 0;
            if( auto Err = TextFile.Record( "EntityInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("LocalId",     FileId32))
                    ||(Error = TextFile.Field("nComponents", nComponents));
                }
            ); Err )
            {
                std::printf("[Prefab::LoadGroupMember] failed reading EntityInfo (%s)\n", std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }

            const local_id FileId = FileId32;

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
                    std::printf("[Prefab::LoadGroupMember] FileId=%llX : failed reading ComponentTypes (%s)\n", (unsigned long long)FileId, std::string(Err.getMessage()).c_str());
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
                std::printf("[Prefab::LoadGroupMember] FileId=%llX : DetectAndUnionPrefabInstance failed (%s)\n", (unsigned long long)FileId, std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
            const bool bIsPrefabInstance = PrefabRootEntity.isValid();

            ArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::prefab::tag> );
            if( bIsRoot ) ArchetypeInfos.push_back( &xecs::component::type::info_v<xecs::prefab::root> );

            auto& Archetype = GameMgr.getOrCreateArchetype( { ArchetypeInfos.data(), ArchetypeInfos.size() } );

            // Same staging as Scene::LoadEntity: fill everything first, create once in the final family.
            xecs::persist::details::staged_components Staged( ArchetypeInfos );

            if( bIsPrefabInstance )
                Staged.CopyFrom( GameMgr, PrefabRootEntity, { &xecs::component::type::info_v<xecs::component::children> } );     // the prefab's children are its own: an instance's are its members (or its file's, before recipes)

            for( auto pInfo : Infos )
            {
                if( xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(pInfo) )
                {
                    *Staged.find<xecs::editor::prefab_instance>() = std::move(TempPI);
                    continue;
                }

                // TAG: already in the archetype via ArchetypeInfos; SaveGroupMember writes no data block for it.
                if( pInfo->m_TypeID == xecs::component::type::id::TAG ) continue;

                if( auto Err = xecs::persist::details::SerializeOneComponent(TextFile, true, *pInfo, Staged.find(*pInfo)); Err )
                {
                    std::printf("[Prefab::LoadGroupMember] FileId=%llX : component '%s' failed to read (%s)\n", (unsigned long long)FileId, pInfo->m_pName, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
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
                Staged.find<xecs::prefab::root>()->m_Guid = PrefabGuid;

            const auto NewEntity = Staged.Create(Archetype);

            Group.m_LocalToRuntime[FileId]            = NewEntity;
            Group.m_RuntimeToLocal[NewEntity.m_Value]  = FileId;

            return {};
        }
    }

    namespace details
    {
        //-----------------------------------------------------------------------------------------
        // A prefab that did not load leaves nothing behind: its members (created so far) are deleted and
        // its bookkeeping dropped, so the next EnsureLoaded starts clean instead of finding half a group.
        //-----------------------------------------------------------------------------------------
        inline void ForgetGroup( mgr& Mgr, guid PrefabGuid ) noexcept
        {
            auto It = Mgr.m_PrefabGroups.find(PrefabGuid.m_Instance.m_Value);
            if( It == Mgr.m_PrefabGroups.end() ) return;
            for( auto& [Id, Member] : It->second.m_LocalToRuntime )
            {
                auto E = Member;
                if( Mgr.m_GameMgr.m_ComponentMgr.isEntityValid(E) ) Mgr.m_GameMgr.DeleteEntity(E);
            }
            Mgr.m_PrefabGroups.erase(It);
        }

        //-----------------------------------------------------------------------------------------
        // The old format (before prefabs_plan.md phase 1): every member in one Entity.txt, after a
        // PrefabGroupInfo record. Kept for one release (decision D4): a prefab read this way is converted
        // in memory, and its next Save writes the scene format (UpgradeProject saves them all at once).
        //-----------------------------------------------------------------------------------------
        inline xerr EnsureLoadedOldFormat( mgr& Mgr, guid PrefabGuid, const std::wstring& Folder ) noexcept
        {
            xecs::serializer::stream TextFile;
            if( auto Err = TextFile.Open( true, Folder + L"/Entity.txt", xtextfile::file_type::TEXT, xtextfile::flags{} ); Err )
            {
                std::printf("[Prefab::EnsureLoaded] Guid=%llX : failed to open Entity.txt (%s)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }

            int           nMembers      = 0;
            std::uint32_t RootLocalId32 = 0;                    // the old format is from before the ids were widened: 32 bits
            if( auto Err = TextFile.Record( "PrefabGroupInfo", [&]( xerr& Error ) noexcept
                {
                      (Error = TextFile.Field("nMembers",    nMembers))
                    ||(Error = TextFile.Field("RootLocalId", RootLocalId32));
                }
            ); Err )
            {
                std::printf("[Prefab::EnsureLoaded] Guid=%llX : failed to read PrefabGroupInfo (%s) - this prefab asset is almost certainly in the OLD single-entity on-disk format from before the multi-entity rework, and needs to be recreated (drag the entity onto the asset browser again to make a fresh one)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
            const local_id RootLocalId = RootLocalId32;
            std::printf("[Prefab::EnsureLoaded] Guid=%llX : old format (Entity.txt), nMembers=%d RootLocalId=%llX\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), nMembers, static_cast<unsigned long long>(RootLocalId));
            std::fflush(stdout);

            auto& Group = Mgr.m_PrefabGroups[PrefabGuid.m_Instance.m_Value];

            for( int i = 0; i < nMembers; ++i )
            {
                if( auto Err = LoadGroupMember( Mgr.m_GameMgr, TextFile, Group, RootLocalId, PrefabGuid ); Err )
                {
                    std::printf("[Prefab::EnsureLoaded] Guid=%llX : member %d failed (%s)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), i, std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    ForgetGroup( Mgr, PrefabGuid );
                    return Err;
                }
            }

            auto RootIt = Group.m_LocalToRuntime.find(RootLocalId);
            if( RootIt == Group.m_LocalToRuntime.end() )
            {
                ForgetGroup( Mgr, PrefabGuid );
                return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab group file's RootLocalId does not match any loaded member">();
            }
            const auto Root = RootIt->second;

            // Load everything first, remap references once every member exists.
            for( auto& Pair : Group.m_LocalToRuntime )
            {
                xecs::persist::details::RemapLoadedEntityReferences( Mgr.m_GameMgr, Pair.second, [&]( std::int64_t Encoded ) noexcept -> xecs::component::entity
                {
                    if( Encoded == 0 ) return xecs::component::entity{};
                    // A prefab group has no equivalent of Scene's external-ref table - every reference
                    // must target another member of the same group.
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

            // Only after every member is loaded AND remapped: a nested-instance member's override with a
            // non-empty m_MemberPath needs a REAL children.m_List to walk (calling this any earlier treated
            // raw, un-remapped reference data as a live entity handle and crashed hard). A nested recipe
            // written before phase 3 has its paths turned into member ids first.
            for( auto& Pair : Group.m_LocalToRuntime )
            {
                auto& Details = Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
                if( Details.m_pPool && Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0 )
                {
                    xecs::prefab::recipe::ConvertNestedRecipe( Mgr.m_GameMgr, Pair.second );
                    xecs::persist::details::ApplyPrefabInstancePropertyOverrides( Mgr.m_GameMgr, Pair.second );
                }
            }

            Mgr.m_PrefabList.insert({ PrefabGuid.m_Instance.m_Value, Root });
            return {};
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    xecs::component::entity mgr::CloneSubtreeIntoPrefab
    ( xecs::component::entity                                               Source
    , group_bookkeeping&                                                    Group
    , bool                                                                  bIsRoot
    , const std::unordered_map<std::uint64_t, xecs::component::entity>*     pKnown
    , std::unordered_map<std::uint64_t, xecs::component::entity>*           pOutClones
    , std::vector<outside_reference>*                                       pOutside
    ) noexcept
    {
        // The subtree is cloned, then each clone references the clones of what its source referenced - a raw copy kept the handles of the
        // sources, which Save then found outside the prefab (an assert in Debug, a null in Release: prefabs_plan.md, phase 0, finding 2) - and
        // each clone gets the name its source has in its scene (the prefab keeps its entities' names).
        std::unordered_map<std::uint64_t, xecs::component::entity> Clones;
        const auto NewEntity = CloneEntityIntoPrefabGroup( Source, Group, bIsRoot, &Clones );

        for( auto& [SourceValue, Clone] : Clones )
        {
            // What leaves the group: listed (with its property path, when it has one) before it becomes null.
            if( pOutside )
            {
                auto& D = m_GameMgr.m_ComponentMgr.getEntityDetails(Clone);
                for( auto pInfo : D.m_pPool->m_pArchetype->getDataComponentInfos() )
                {
                    if( pInfo->m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES || pInfo->m_pPropertyTable == nullptr ) continue;
                    if( xecs::component::type::IsComponentType<xecs::component::parent>(pInfo) || xecs::component::type::IsComponentType<xecs::component::children>(pInfo) ) continue;
                    auto* pData = xecs::persist::details::ResolveLiveComponentPointer( m_GameMgr, Clone, *pInfo );
                    if( pData == nullptr ) continue;
                    xproperty::settings::context Context{};
                    xproperty::sprop::collector( pData, *pInfo->m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members&, bool, const void* ) noexcept
                    {
                        if( V.getTypeGuid() != xproperty::settings::var_type<xecs::component::entity>::guid_v ) return;
                        const auto R = V.get<xecs::component::entity>();
                        if( false == R.isValid() || Clones.contains(R.m_Value) || Group.m_RuntimeToLocal.contains(R.m_Value) || (pKnown && pKnown->contains(R.m_Value)) ) return;
                        if( false == m_GameMgr.m_ComponentMgr.isEntityValid(R) ) return;
                        pOutside->push_back({ .m_Member = Group.m_RuntimeToLocal.at(Clone.m_Value), .m_Component = pInfo->m_Guid.m_Value, .m_Path = pName, .m_Target = R });
                    });
                }
            }

            xecs::persist::details::RemapEntityReferences( m_GameMgr, Clone, [&]( xecs::component::entity R ) noexcept
            {
                return xecs::persist::details::KeepReferenceInsidePrefab( m_GameMgr, Group, &Clones, R, pKnown );
            });

            for( auto& pScene : m_GameMgr.m_SceneMgr.m_SceneInstances )
            {
                auto It = pScene->m_RuntimeToLocal.find(SourceValue);
                if( It == pScene->m_RuntimeToLocal.end() ) continue;
                if( auto Name = pScene->m_EntityNames.find(It->second); Name != pScene->m_EntityNames.end() )
                    Group.m_EntityNames[ Group.m_RuntimeToLocal.at(Clone.m_Value) ] = Name->second;
                break;
            }
        }
        if( pOutClones ) *pOutClones = std::move(Clones);
        return NewEntity;
    }

    //--------------------------------------------------------------------------------------------------------------

    xecs::component::entity mgr::CloneEntityIntoPrefabGroup( xecs::component::entity Source, group_bookkeeping& Group, bool bIsRoot, std::unordered_map<std::uint64_t, xecs::component::entity>* pClones ) noexcept
    {
        // The whole clone (the top call): see CloneSubtreeIntoPrefab.
        if( pClones == nullptr )
            return CloneSubtreeIntoPrefab( Source, Group, bIsRoot, nullptr, nullptr, nullptr );

        auto& SourceDetails   = m_GameMgr.m_ComponentMgr.getEntityDetails(Source);
        auto& SourceArchetype = *SourceDetails.m_pPool->m_pArchetype;
        auto  DataSpan        = SourceArchetype.getDataComponentInfos();
        auto  ShareSpan       = SourceArchetype.getShareComponentInfos();

        // New archetype = Source's current data + share components (excluding "entity", added
        // automatically by CreateEntity below) + prefab::tag (every member) + prefab::root (root
        // only) - the exact same bit setup CreatePrefab<T...>() forces onto every compile-time-
        // authored prefab. SHARE must be in the archetype bits or Make Prefab drops body/shape/etc.
        std::vector<const xecs::component::type::info*> Infos;
        Infos.reserve(DataSpan.size() + ShareSpan.size() + 3);
        Infos.push_back( &xecs::component::type::info_v<xecs::component::entity> );
        Infos.push_back( &xecs::component::type::info_v<xecs::prefab::tag> );
        if( bIsRoot ) Infos.push_back( &xecs::component::type::info_v<xecs::prefab::root> );
        for( auto pInfo : DataSpan )
        {
            if( xecs::component::type::IsComponentType<xecs::component::entity>(pInfo) ) continue;
            // A prefab root never carries its own parent link - it's the top of ITS OWN hierarchy,
            // regardless of whether the live entity being converted happened to have one before
            // conversion (E29's "Make Prefab" UI is responsible for reparenting the live scene entity
            // BEFORE calling this - this exclusion only concerns the CLONE).
            if( bIsRoot && xecs::component::type::IsComponentType<xecs::component::parent>(pInfo) ) continue;
            Infos.push_back(pInfo);
        }
        for( auto pInfo : ShareSpan )
            Infos.push_back(pInfo);
        xecs::persist::details::AppendPersistentTagInfos(SourceArchetype, Infos);   // no data to copy - archetype bits only

        auto& NewArchetype = m_GameMgr.getOrCreateArchetype( { Infos.data(), Infos.size() } );

        // Created once, directly with Source's live DATA + SHARE values. children is structural
        // (rebuilt below); parent is copied (what the child follows of its parent, m_Follow, is the
        // author's - skipping it reset it to the default) and its link is set to the new parent below.
        // prefab::root is new here - Source never has it, so it stays default.
        // editor::prefab_instance, if present, copies plain like any other DATA component.
        xecs::persist::details::staged_components Staged( Infos );
        Staged.CopyFrom( m_GameMgr, Source, { &xecs::component::type::info_v<xecs::component::children> } );
        if( auto* pParent = Staged.find<xecs::component::parent>() ) pParent->m_Value = {};      // the new parent is set by the caller
        auto NewEntity = Staged.Create(NewArchetype);

        // Mint and register BEFORE recursing into children below - a later reference-remap pass or
        // diagnostic walk touching the group mid-clone needs this entity's id to already exist.
        const auto Id = details::NextFreeLocalId(Group);
        Group.m_LocalToRuntime[Id]                = NewEntity;
        Group.m_RuntimeToLocal[NewEntity.m_Value] = Id;
        (*pClones)[Source.m_Value]                = NewEntity;

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
                auto NewChild = CloneEntityIntoPrefabGroup( Child, Group, /*bIsRoot=*/false, pClones );
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

    guid mgr::CreatePrefabFromEntity( xecs::component::entity Source, guid PrefabGuid, std::vector<outside_reference>* pOutside, std::unordered_map<std::uint64_t, local_id>* pMemberIds ) noexcept
    {
        InvalidateBaked();
        auto& Group = m_PrefabGroups[PrefabGuid.m_Instance.m_Value];
        std::unordered_map<std::uint64_t, xecs::component::entity> Clones;
        auto  Root  = CloneSubtreeIntoPrefab( Source, Group, /*bIsRoot=*/true, nullptr, &Clones, pOutside );
        if( pMemberIds ) for( auto& [SourceValue, Clone] : Clones ) (*pMemberIds)[SourceValue] = Group.m_RuntimeToLocal.at(Clone.m_Value);

        auto& RootDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Root);
        RootDetails.m_pPool->getComponent<xecs::prefab::root>(RootDetails.m_PoolIndex).m_Guid = PrefabGuid;

        m_PrefabList.insert({ PrefabGuid.m_Instance.m_Value, Root });
        return PrefabGuid;
    }

    //--------------------------------------------------------------------------------------------------------------

    xerr mgr::Save( guid PrefabGuid ) noexcept
    {
        // Another editor holds the prefab as its document: it is the one writer. The saved state goes to a new folder and the editor is handed it.
        if( m_pRedirect && m_pRedirect->m_pTakes && m_pRedirect->m_pDeliver && m_pRedirect->m_pTakes( m_pRedirect->m_pUser, PrefabGuid ) )
        {
            static std::atomic<std::uint32_t> s_Count{ 0 };
            std::error_code Ec;
            const auto Folder = ( std::filesystem::temp_directory_path(Ec) / std::format( L"xlion_prefab_{:X}_{}_{}", PrefabGuid.m_Instance.m_Value, reinterpret_cast<std::uintptr_t>(this), ++s_Count ) ).wstring();
            std::filesystem::remove_all( std::filesystem::path(Folder), Ec );
            std::filesystem::create_directories( std::filesystem::path(Folder), Ec );
            if( auto Err = SaveTo( PrefabGuid, Folder ); Err ) return Err;
            if( false == m_pRedirect->m_pDeliver( m_pRedirect->m_pUser, PrefabGuid, Folder ) )
            {
                std::filesystem::remove_all( std::filesystem::path(Folder), Ec );
                return xerr::create<xecs::game_mgr::state::FAILURE, "prefab::mgr::Save: the editor that holds the prefab did not take the saved state and it could not be written to the prefab's folder either">();
            }
            return {};
        }
        return SaveTo( PrefabGuid, details::PrefabFolder( *this, PrefabGuid ) );
    }

    //--------------------------------------------------------------------------------------------------------------

    xerr mgr::SaveTo( guid PrefabGuid, const std::wstring& Folder ) noexcept
    {
        InvalidateBaked();      // what is saved is what changed (Apply, MakePrefab, the Undo of an Apply): the next spawn bakes again
        auto It = m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if( It == m_PrefabList.end() )
            return xerr::create<xecs::game_mgr::state::FAILURE, "prefab::mgr::Save: prefab is not resident - call EnsureLoaded/CreatePrefabFromEntity first">();

        auto  RootEntity = It->second;
        auto& Group      = m_PrefabGroups[PrefabGuid.m_Instance.m_Value];

        std::vector<xecs::component::entity> Members;
        details::CollectGroupMembers( m_GameMgr, RootEntity, Members );
        details::ReconcileGroupLocalIds( Group, Members );

        // The rules of a prefab, checked before anything is written: one root, every member descends from
        // it (Members is the root's subtree, by construction), and what is written references only its own
        // members. A reference to an entity that no longer exists (a member Apply Overrides removed) is a
        // null one now; one to a live entity outside the prefab refuses the save (the paths that build a
        // template keep their references inside: KeepReferenceInsidePrefab).
        for( auto Member : Members )
        {
            bool bOutside = false;
            xecs::persist::details::RemapWrittenReferences( m_GameMgr, Member, [&]( xecs::component::entity R ) noexcept -> xecs::component::entity
            {
                if( false == R.isValid() || Group.m_RuntimeToLocal.contains(R.m_Value) ) return R;
                if( false == m_GameMgr.m_ComponentMgr.isEntityValid(R) ) return {};
                bOutside = true;
                return R;
            });
            if( bOutside )
            {
                std::printf("[Prefab::Save] Guid=%llX : member %s references an entity outside the prefab - nothing was written\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), xecs::scene::FormatPermanentId(Group.m_RuntimeToLocal.at(Member.m_Value)).c_str());
                std::fflush(stdout);
                return xerr::create<xecs::game_mgr::state::FAILURE, "prefab::mgr::Save: a member references an entity outside the prefab (nothing was written)">();
            }
        }

        // Every member in the scene's entity format, references encoded as the member ids.
        auto GroupResolve = [&]( xecs::component::entity Target, std::int64_t& OutEncoded ) noexcept -> bool
        {
            if( auto It2 = Group.m_RuntimeToLocal.find(Target.m_Value); It2 != Group.m_RuntimeToLocal.end() )
            {
                OutEncoded = static_cast<std::int64_t>(It2->second);
                return true;
            }
            return false;
        };
        for( auto& Member : Members )
        {
            const auto Id = Group.m_RuntimeToLocal.at(Member.m_Value);
            if( auto Err = xecs::scene::details::WriteEntityFile( m_GameMgr, xecs::scene::details::EntityFileInFolder(Folder, Id), Id, Member, GroupResolve ); Err )
                return Err;
        }

        // The descriptor is written after the members it names, to a temp file and renamed over the real one
        // (as the scene's): it is what says the folder holds the scene format.
        descriptor Descriptor;
        Descriptor.m_Game = details::ReadDescriptorGame( details::PrefabFolder(*this, PrefabGuid) );      // the Game the prefab plays with is the editor's, not the template's: every save keeps it
        Descriptor.m_Root = Group.m_RuntimeToLocal.at(RootEntity.m_Value);
        for( auto& [Id, Member] : Group.m_LocalToRuntime ) Descriptor.m_ActiveEntities.push_back(Id);
        std::sort( Descriptor.m_ActiveEntities.begin(), Descriptor.m_ActiveEntities.end() );
        for( auto& [Id, Name] : Group.m_EntityNames )
            if( Group.m_LocalToRuntime.contains(Id) ) Descriptor.m_EntityNames.push_back({ .m_Id = Id, .m_Name = Name });
        std::sort( Descriptor.m_EntityNames.begin(), Descriptor.m_EntityNames.end(), []( auto& A, auto& B ) noexcept { return A.m_Id < B.m_Id; } );
        {
            const auto RealPath = Folder + L"/Descriptor.txt";
            const auto TempPath = RealPath + L".tmp";
            xproperty::settings::context Context;
            if( auto Err = Descriptor.Serialize( false, TempPath, Context ); Err )
                return Err;
            std::error_code RenameEc;
            std::filesystem::rename( TempPath, RealPath, RenameEc );
            if( RenameEc )
                return xerr::create<xecs::game_mgr::state::FAILURE, "prefab::mgr::Save: wrote the temp descriptor but the atomic rename over the real one failed">();
        }

        // Best effort, as the scene's: a failure degrades a later compatibility check, it never fails the save.
        if( auto Err = xecs::scene::details::WriteComponentDependencies( Folder + L"/ComponentDeps.txt"
                     , xecs::scene::details::CollectComponentDependencies( m_GameMgr, Group.m_LocalToRuntime, m_GameMgr.m_SceneMgr.m_pModuleOfComponent, m_GameMgr.m_SceneMgr.m_pModuleOfComponentUser ) ); Err )
        {
            std::printf("[Prefab::Save] ComponentDeps.txt FAILED: %s\n", std::string(Err.getMessage()).c_str());
            std::fflush(stdout);
        }

        // What is not the prefab any more: the old one-file format, and the files of members that are gone.
        std::error_code Ec;
        std::filesystem::remove( std::filesystem::path(Folder + L"/Entity.txt"), Ec );
        for( auto Entry = std::filesystem::recursive_directory_iterator( std::filesystem::path(Folder + L"/entity_db"), std::filesystem::directory_options::skip_permission_denied, Ec )
           ; !Ec && Entry != std::filesystem::recursive_directory_iterator(); Entry.increment(Ec) )
        {
            if( false == Entry->is_regular_file(Ec) || Entry->path().extension() != L".entity" ) continue;
            const auto Id = static_cast<local_id>( std::wcstoull( Entry->path().stem().c_str(), nullptr, 16 ) );
            if( false == Group.m_LocalToRuntime.contains(Id) )
            {
                std::error_code RemoveEc;
                std::filesystem::remove( Entry->path(), RemoveEc );
            }
        }

        return {};
    }

    //--------------------------------------------------------------------------------------------------------------

    void mgr::DropTemplate( guid PrefabGuid ) noexcept
    {
        InvalidateBaked();
        if( m_PrefabList.contains(PrefabGuid.m_Instance.m_Value) )
        {
            details::ForgetGroup( *this, PrefabGuid );
            m_PrefabList.erase(PrefabGuid.m_Instance.m_Value);
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    xerr mgr::EnsureLoaded( guid PrefabGuid ) noexcept
    {
        if( m_PrefabList.find(PrefabGuid.m_Instance.m_Value) != m_PrefabList.end() )
            return {};

        const auto Folder = details::PrefabFolder(*this, PrefabGuid);

        // The descriptor says which format the folder holds: a Root is the scene format.
        descriptor Descriptor;
        {
            const auto      Path = Folder + L"/Descriptor.txt";
            std::error_code Ec;
            xproperty::settings::context Context;
            if( std::filesystem::exists( std::filesystem::path(Path), Ec ) && Descriptor.Serialize( true, Path, Context ) )
                Descriptor.m_Root = invalid_local_id_v;
        }
        if( Descriptor.m_Root == invalid_local_id_v )
            return details::EnsureLoadedOldFormat( *this, PrefabGuid, Folder );

        // 1) Read every member into staging - none is created unless all of them read (a prefab loads whole
        //    or not at all). Every member gets prefab::tag (a template: no system sees it, no builder runs on
        //    it - see game_mgr::getBuildPlan), the root prefab::root.
        const xecs::component::type::info* const Extra[] = { &xecs::component::type::info_v<xecs::prefab::tag>, &xecs::component::type::info_v<xecs::prefab::root> };
        std::vector<xecs::scene::details::staged_entity> StagedMembers;
        StagedMembers.reserve( Descriptor.m_ActiveEntities.size() );
        bool bHasRoot = false;
        for( auto Id : Descriptor.m_ActiveEntities )
        {
            const bool bRoot  = Id == Descriptor.m_Root;
            auto&      Staged = StagedMembers.emplace_back();
            if( auto Err = xecs::scene::details::ReadEntityFile( m_GameMgr, xecs::scene::details::EntityFileInFolder(Folder, Id), Staged, std::span<const xecs::component::type::info* const>( Extra, bRoot ? 2u : 1u ) ); Err )
            {
                std::printf("[Prefab::EnsureLoaded] Guid=%llX : member %s failed (%s)\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), xecs::scene::FormatPermanentId(Id).c_str(), std::string(Err.getMessage()).c_str());
                std::fflush(stdout);
                return Err;
            }
            if( Staged.m_Id != Id )
                return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab member file says another id than the one it is filed under">();
            if( bRoot )
            {
                Staged.m_pStaged->find<xecs::prefab::root>()->m_Guid = PrefabGuid;
                bHasRoot = true;
            }
        }
        if( false == bHasRoot )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab descriptor's Root is not one of its ActiveEntities">();

        // 2) Create them, and give them their names.
        auto& Group = m_PrefabGroups[PrefabGuid.m_Instance.m_Value];
        Group = {};
        for( auto& Staged : StagedMembers )
            xecs::scene::details::CreateStagedEntity( m_GameMgr, Group, Staged );
        for( auto& N : Descriptor.m_EntityNames ) Group.m_EntityNames[N.m_Id] = std::move(N.m_Name);

        // 3) References, once every member exists: a prefab references only its own members.
        for( auto& Pair : Group.m_LocalToRuntime )
        {
            xecs::persist::details::RemapLoadedEntityReferences( m_GameMgr, Pair.second, [&]( std::int64_t Encoded ) noexcept -> xecs::component::entity
            {
                if( Encoded == 0 ) return xecs::component::entity{};
                auto It = Encoded > 0 ? Group.m_LocalToRuntime.find( static_cast<local_id>(Encoded) ) : Group.m_LocalToRuntime.end();
                if( It != Group.m_LocalToRuntime.end() ) return It->second;
                std::printf("[Prefab::EnsureLoaded] Guid=%llX : WARNING encoded reference %lld is not a member of the prefab - encoding as null\n", static_cast<unsigned long long>(PrefabGuid.m_Instance.m_Value), static_cast<long long>(Encoded));
                std::fflush(stdout);
                return xecs::component::entity{};
            });
        }

        // 4) Only after every member is loaded AND remapped: a nested-instance member's override with a
        //    non-empty m_MemberPath needs a REAL children.m_List to walk (see Scene::EnsureLoaded's own
        //    matching pass - calling this any earlier treated raw, un-remapped reference data as a live
        //    entity handle and crashed hard, no assert, just a silent exit).
        for( auto& Pair : Group.m_LocalToRuntime )
        {
            auto& Details = m_GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
            if( Details.m_pPool && Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0 )
            {
                xecs::prefab::recipe::ConvertNestedRecipe( m_GameMgr, Pair.second );      // a nested recipe written before phase 3: its paths become member ids
                xecs::persist::details::ApplyPrefabInstancePropertyOverrides( m_GameMgr, Pair.second );
            }
        }

        m_PrefabList.insert({ PrefabGuid.m_Instance.m_Value, Group.m_LocalToRuntime.at(Descriptor.m_Root) });
        return {};
    }
}

//------------------------------------------------------------------------------------------------------------------
// A prefab opened in a Prefab Editor (documentation/Editors/prefabs_plan.md, phase 5). The editor makes a scene of the prefab's guid with
// instance::m_bPrefabDocument set, and loads it like any scene (the scene manager reads the prefab's folder: a prefab is stored as a scene). The
// entities are ordinary live ones - no prefab::tag, systems see them, the editor's commands edit them - and a nested instance is a recipe like in
// a level. What is different is what the two functions below say: the descriptor is a prefab's, and a save is a prefab's save.
//------------------------------------------------------------------------------------------------------------------
namespace xecs::prefab::document
{
    namespace docdetails
    {
        inline bool HasParent( xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity ) noexcept
        {
            auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            if( Details.m_pPool == nullptr ) return false;
            if( Details.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<xecs::component::parent> ) < 0 ) return false;
            return Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value.isValid();
        }
    }

    xerr LoadDescriptor( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene, std::vector<xecs::scene::permanent_id>& OutActiveEntities ) noexcept
    {
        Scene.m_ParentScenes.clear();
        Scene.m_ExternalRefTable.clear();
        Scene.m_Folders.clear();
        Scene.m_EntityNames.clear();
        OutActiveEntities.clear();

        const auto      Path = xecs::scene::details::DescriptorPath( Mgr, Scene.m_Guid );
        std::error_code Ec;
        if( false == std::filesystem::exists( std::filesystem::path(Path), Ec ) )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab document: the prefab has no Descriptor.txt">();

        descriptor                   Descriptor;
        xproperty::settings::context Context;
        if( auto Err = Descriptor.Serialize( true, Path, Context ); Err )
            return Err;
        if( Descriptor.m_Root == xecs::scene::invalid_permanent_id_v )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab document: the prefab is stored in the old format (one Entity.txt) - UpgradeProject converts it, then it opens">();

        for( auto& N : Descriptor.m_EntityNames ) Scene.m_EntityNames[N.m_Id] = std::move(N.m_Name);
        OutActiveEntities = std::move(Descriptor.m_ActiveEntities);
        return {};
    }

    xerr Save( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene ) noexcept
    {
        auto&                      GameMgr = Mgr.m_GameMgr;
        const xecs::prefab::guid   Guid{ .m_Instance = Scene.m_Guid.m_Instance, .m_Type = xecs::prefab::type_guid_v };

        // The rules of a prefab, checked before anything is written: one root (the one entity that has no parent: everything else descends from it), and what is written
        // references only the prefab's own entities (a context scene's entity, say, is not part of it). A member of a nested instance is not written: its instance is.
        std::vector<xecs::scene::permanent_id> Written, Roots;
        for( auto& [Id, Entity] : Scene.m_LocalToRuntime )
        {
            if( Scene.m_InstanceMembers.contains(Id) ) continue;
            Written.push_back(Id);
            if( false == docdetails::HasParent( GameMgr, Entity ) ) Roots.push_back(Id);
        }
        std::sort( Written.begin(), Written.end() );
        if( Roots.empty() )
        {
            std::printf("[Prefab::document::Save] Guid=%llX : the prefab has no entity without a parent - nothing was written\n", static_cast<unsigned long long>(Guid.m_Instance.m_Value));
            std::fflush(stdout);
            return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab document: a prefab needs exactly one root (an entity with no parent) and it has none (nothing was written)">();
        }
        if( Roots.size() > 1 )
        {
            std::printf("[Prefab::document::Save] Guid=%llX : the prefab has %zu entities without a parent - nothing was written\n", static_cast<unsigned long long>(Guid.m_Instance.m_Value), Roots.size());
            std::fflush(stdout);
            return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab document: a prefab has exactly one root (an entity with no parent) and it has more (nothing was written): put the others under it">();
        }
        for( auto Id : Written )
        {
            bool bOutside = false;
            xecs::persist::details::RemapWrittenReferences( GameMgr, Scene.m_LocalToRuntime.at(Id), [&]( xecs::component::entity R ) noexcept -> xecs::component::entity
            {
                if( false == R.isValid() || Scene.m_RuntimeToLocal.contains(R.m_Value) ) return R;
                if( false == GameMgr.m_ComponentMgr.isEntityValid(R) ) return {};
                bOutside = true;
                return R;
            });
            if( bOutside )
            {
                std::printf("[Prefab::document::Save] Guid=%llX : entity %s references an entity outside the prefab - nothing was written\n", static_cast<unsigned long long>(Guid.m_Instance.m_Value), xecs::scene::FormatPermanentId(Id).c_str());
                std::fflush(stdout);
                return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab document: an entity references an entity outside the prefab (nothing was written)">();
            }
        }

        // The entities (the scene's entity files, in the prefab's folder), then the descriptor that names them, then the manifest.
        const auto      Folder = xecs::scene::details::SceneFolder( Mgr, Scene.m_Guid );
        std::error_code Ec;
        std::filesystem::create_directories( std::filesystem::path(Folder + L"/entity_db"), Ec );
        for( auto Id : Written )
            if( auto Err = Mgr.SaveEntity( Scene.m_Guid, Id, Scene.m_LocalToRuntime.at(Id) ); Err )
                return Err;

        descriptor Descriptor;
        Descriptor.m_Game   = xecs::prefab::details::ReadDescriptorGame( xecs::scene::details::PrefabFolderOf( Mgr.m_ProjectPath, Guid.m_Instance.m_Value ) );      // the Game is the prefab's, not the document's: every save keeps it
        Descriptor.m_Root   = Roots[0];
        Descriptor.m_ActiveEntities = Written;
        for( auto Id : Scene.m_UnloadedEntities )          // could not be loaded (a module that is not in the Game): still the prefab's, as a scene's
            if( false == Scene.m_LocalToRuntime.contains(Id) ) Descriptor.m_ActiveEntities.push_back(Id);
        std::sort( Descriptor.m_ActiveEntities.begin(), Descriptor.m_ActiveEntities.end() );
        for( auto& [Id, Name] : Scene.m_EntityNames )      // a member of a nested instance has its prefab's name: kept only when renamed
        {
            if( false == Scene.m_LocalToRuntime.contains(Id) ) continue;
            if( auto M = Scene.m_InstanceMembers.find(Id); M != Scene.m_InstanceMembers.end() && M->second.m_Name == Name ) continue;
            Descriptor.m_EntityNames.push_back({ .m_Id = Id, .m_Name = Name });
        }
        std::sort( Descriptor.m_EntityNames.begin(), Descriptor.m_EntityNames.end(), []( auto& A, auto& B ) noexcept { return A.m_Id < B.m_Id; } );
        {
            const auto RealPath = Folder + L"/Descriptor.txt";
            const auto TempPath = RealPath + L".tmp";
            xproperty::settings::context Context;
            if( auto Err = Descriptor.Serialize( false, TempPath, Context ); Err )
                return Err;
            std::filesystem::rename( TempPath, RealPath, Ec );
            if( Ec )
                return xerr::create<xecs::game_mgr::state::FAILURE, "Prefab document: wrote the temp descriptor but the atomic rename over the real one failed">();
        }
        if( auto Err = xecs::scene::details::WriteComponentDependencies( Folder + L"/ComponentDeps.txt"
                     , xecs::scene::details::CollectComponentDependencies( GameMgr, Scene.m_LocalToRuntime, Mgr.m_pModuleOfComponent, Mgr.m_pModuleOfComponentUser ) ); Err )
        {
            std::printf("[Prefab::document::Save] ComponentDeps.txt FAILED: %s\n", std::string(Err.getMessage()).c_str());
            std::fflush(stdout);
        }

        // What is not the prefab any more: the files of the entities that left it (and the old one-file format).
        std::filesystem::remove( std::filesystem::path(Folder + L"/Entity.txt"), Ec );
        std::unordered_set<xecs::scene::permanent_id> Active( Descriptor.m_ActiveEntities.begin(), Descriptor.m_ActiveEntities.end() );
        for( auto Entry = std::filesystem::recursive_directory_iterator( std::filesystem::path(Folder + L"/entity_db"), std::filesystem::directory_options::skip_permission_denied, Ec )
           ; !Ec && Entry != std::filesystem::recursive_directory_iterator(); Entry.increment(Ec) )
        {
            if( false == Entry->is_regular_file(Ec) || Entry->path().extension() != L".entity" ) continue;
            const auto Id = static_cast<xecs::scene::permanent_id>( std::wcstoull( Entry->path().stem().c_str(), nullptr, 16 ) );
            if( false == Active.contains(Id) )
            {
                std::error_code RemoveEc;
                std::filesystem::remove( Entry->path(), RemoveEc );
            }
        }

        Scene.m_PendingChanges.clear();

        // Saved into its own folder (not a snapshot): the template this world may hold of the prefab, and the plans baked from it, are what the file said before.
        if( Scene.m_FolderOverride.empty() )
            GameMgr.m_PrefabMgr.DropTemplate( Guid );
        return {};
    }
}
