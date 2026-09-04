namespace xecs::prefab
{
    //--------------------------------------------------------------------------------------------------------------

    xecs::component::entity mgr::CreatePrefabInstance( xecs::component::entity Entity, std::unordered_map< std::uint64_t, xecs::component::entity >& Remap, xecs::component::entity ParentEntity, bool isVariant ) noexcept
    {
        auto& PrefabDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& PrefabArchetype = *PrefabDetails.m_pPool->m_pArchetype;

        xecs::tools::bits InstanceBits = PrefabArchetype.getComponentBits();

        if( false == isVariant ) InstanceBits.clearBit( xecs::component::type::info_v<xecs::prefab::tag>.m_BitID );
        if( false == ParentEntity.isValid() ) InstanceBits.clearBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID);

        //
        // Get the instance archetype
        //
        auto& InstanceArchetype = m_GameMgr.m_ArchetypeMgr.getOrCreateArchetype(InstanceBits);

        xecs::component::entity PrefabInstance;

        //
        // Create the instance
        //
        if( InstanceArchetype.hasShareComponents() )
        {
            auto& Family = PrefabArchetype.hasShareComponents() ? InstanceArchetype.getOrCreatePoolFamily(*PrefabDetails.m_pPool->m_pMyFamily) : InstanceArchetype.getOrCreatePoolFamily({}, {});

            if( InstanceBits.getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) )
            {
                if( ParentEntity.isValid() )
                {
                    InstanceArchetype.CreateEntities( Family, 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children, xecs::component::parent& Parent ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        for( auto& E : Children.m_List ) E = CreatePrefabInstance( E, Remap, Instance, isVariant);
                        Parent.m_Value = ParentEntity;
                    });
                }
                else
                {
                    InstanceArchetype.CreateEntities(Family, 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children ) noexcept
                    {
                        Remap.insert( { Entity.m_Value, Instance } );
                        for( auto& E : Children.m_List ) E = CreatePrefabInstance( E, Remap, Instance, isVariant);
                        PrefabInstance = Instance;
                    });
                }
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
                if (ParentEntity.isValid())
                {
                    InstanceArchetype.CreateEntities( 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children, xecs::component::parent& Parent ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        for( auto& E : Children.m_List ) E = CreatePrefabInstance( E, Remap, Instance, isVariant);
                        Parent.m_Value = ParentEntity;
                    });
                }
                else
                {
                    InstanceArchetype.CreateEntities( 1, Entity, [&](const xecs::component::entity& Instance, xecs::component::children& Children ) noexcept
                    {
                        PrefabInstance = Instance;
                        Remap.insert( { Entity.m_Value, Instance } );
                        for( auto& E : Children.m_List ) E = CreatePrefabInstance( E, Remap, Instance, isVariant);
                    });
                }
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
                if constexpr (std::is_same_v< T_CALLBACK, xecs::tools::empty_lambda > || std::tuple_size_v<xecs::component::type::details::share_only_tuple_t<typename fn_traits::args_tuple>> == 0) InstanceArchetype.CreateEntities( Count, Entity, std::forward<T_CALLBACK&&>(Callback) );
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
                if ( pInstanceArchetype->hasShareComponents() )
                {
                    auto& Family = PrefabArchetype.hasShareComponents() ? pInstanceArchetype->getOrCreatePoolFamily(*PrefabDetails.m_pPool->m_pMyFamily) : pInstanceArchetype->getOrCreatePoolFamily({}, {});
                    pInstanceArchetype->CreateEntities( Family, 1, Entity, [&]( const xecs::component::entity& EntityI, xecs::component::children& Children ) constexpr noexcept
                    {
                        EntityInstance = EntityI;
                        EntityRemap.insert( {Entity.m_Value, EntityI} );
                        for( auto& E : Children.m_List )
                        {
                            E = CreatePrefabInstance( E, EntityRemap, EntityInstance, isVariant );
                        }
                    });
                }
                else
                {
                    pInstanceArchetype->CreateEntities( 1, Entity, [&]( const xecs::component::entity& EntityI, xecs::component::children& Children ) constexpr noexcept
                    {
                        EntityInstance = EntityI;
                        EntityRemap.insert( {Entity.m_Value, EntityI} );
                        for( auto& E : Children.m_List )
                        {
                            E = CreatePrefabInstance( E, EntityRemap, EntityInstance, isVariant);
                        }
                    });
                }
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
}