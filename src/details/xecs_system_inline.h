namespace xecs::system
{
    namespace details
    {
        template< typename T_USER_SYSTEM >
        requires( std::derived_from< T_USER_SYSTEM, xecs::system::instance > )
        struct compleated final : T_USER_SYSTEM
        {
            T_USER_SYSTEM::events m_Events;

            //---------------------------------------------------------------------------------------

            __inline
            compleated( xecs::game_mgr::instance& GameMgr ) noexcept
            : T_USER_SYSTEM{ GameMgr }
            {}
            compleated( void ) noexcept = delete;

            //---------------------------------------------------------------------------------------

            __inline
            void Run( void ) noexcept
            {
                //
                // Call the user OnUpdate or process directly
                //
                if constexpr ( T_USER_SYSTEM::typedef_v.id_v == xecs::system::type::id::UPDATE
                              || xecs::types::is_specialized_v<xecs::system::type::child_update, std::decay_t<decltype(T_USER_SYSTEM::typedef_v)> > )
                {
                    XCORE_PERF_ZONE_SCOPED_N(xecs::system::type::info_v<T_USER_SYSTEM>.m_pName)
                    
                    if constexpr (&T_USER_SYSTEM::OnPreUpdate != &instance::OnPreUpdate)
                    {
                        T_USER_SYSTEM::OnPreUpdate();
                    }

                    if constexpr (&T_USER_SYSTEM::OnUpdate != &instance::OnUpdate)
                    {
                        T_USER_SYSTEM::OnUpdate();
                    }
                    else
                    {
                        auto ArchetypeList = T_USER_SYSTEM::m_GameMgr.Search(type::info_v<T_USER_SYSTEM>.m_Query);
                        if constexpr( xecs::tools::is_share_as_data_v<T_USER_SYSTEM::query> )
                        {
                            T_USER_SYSTEM::m_GameMgr.Foreach<decltype(*this),true>(ArchetypeList, *this);
                        }
                        else
                        {
                            T_USER_SYSTEM::m_GameMgr.Foreach(ArchetypeList, *this);
                        }
                    }

                    if constexpr (&T_USER_SYSTEM::OnPostUpdate != &instance::OnPostUpdate)
                    {
                        T_USER_SYSTEM::OnPostUpdate();
                    }

                    //
                    // Ask the game to update all the pending structural changes
                    //
                    {
                        XCORE_PERF_ZONE_SCOPED_N("UpdateStructuralChanges")
                        T_USER_SYSTEM::m_GameMgr.m_ArchetypeMgr.UpdateStructuralChanges();
                    }

                    //
                    // Check to see if the user would like to be notified 
                    //
                    if constexpr ( &T_USER_SYSTEM::OnPostStructuralChanges != &instance::OnPostStructuralChanges )
                    {
                        T_USER_SYSTEM::OnPostStructuralChanges();
                    }
                }
            }

            //---------------------------------------------------------------------------------------

            __inline
            void Notify( xecs::component::entity& Entity ) noexcept
            {
                if constexpr (T_USER_SYSTEM::typedef_v.is_notifier_v )
                {
                    XCORE_PERF_ZONE_SCOPED_N(xecs::system::type::info_v<T_USER_SYSTEM>.m_pName)
                    if constexpr (&T_USER_SYSTEM::OnNotify != &instance::OnNotify)
                    {
                        T_USER_SYSTEM::OnNotify(Entity);
                    }
                    else
                    {
                        using function_args = typename xecs::function::traits<compleated<T_USER_SYSTEM>>::args_tuple;
                        auto& Entry         = T_USER_SYSTEM::m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
                        {
                            auto CachedArray = xecs::archetype::details::GetDataComponentPointerArray
                            ( *Entry.m_pPool
                            , Entry.m_PoolIndex
                            , xecs::types::null_tuple_v<function_args>
                            );
                            xecs::archetype::details::CallFunction( *this, CachedArray );
                        }
                    }
                }
            }
        };
    }

    //-------------------------------------------------------------------------------------------

    namespace type::details
    {
        template< typename T_SYSTEM >
        consteval type::info CreateInfo( void ) noexcept
        {
            return type::info
            {
                .m_Guid                 = !T_SYSTEM::typedef_v.m_Guid.empty()
                                             ? T_SYSTEM::typedef_v.m_Guid
                                             : type::guid{ __FUNCSIG__ }
            ,   .m_NotifierRegistration = (T_SYSTEM::typedef_v.id_v == type::id::UPDATE)
                                           ? nullptr
                                           : []( xecs::archetype::instance&     Archetype
                                            , xecs::system::instance&           System 
                                            ) noexcept
                                            {
                                                using real_system = xecs::system::details::compleated<T_SYSTEM>;
                                                if constexpr ( T_SYSTEM::typedef_v.is_notifier_v == false )
                                                {
                                                    // NOTHING TO DO...
                                                }
                                                else if constexpr (T_SYSTEM::typedef_v.id_v == type::id::NOTIFY_CREATE )
                                                {
                                                    Archetype.m_Events.m_OnEntityCreated.Register<&real_system::Notify>(static_cast<real_system&>(System));
                                                }
                                                else if constexpr (T_SYSTEM::typedef_v.id_v == type::id::NOTIFY_DESTROY)
                                                {
                                                    Archetype.m_Events.m_OnEntityDestroyed.Register<&real_system::Notify>(static_cast<real_system&>(System));
                                                }
                                                else if constexpr (T_SYSTEM::typedef_v.id_v == type::id::NOTIFY_MOVE_IN)
                                                {
                                                    Archetype.m_Events.m_OnEntityMovedIn.Register<&real_system::Notify>(static_cast<real_system&>(System));
                                                }
                                                else if constexpr (T_SYSTEM::typedef_v.id_v == type::id::NOTIFY_MOVE_OUT)
                                                {
                                                    Archetype.m_Events.m_OnEntityMovedOut.Register<&real_system::Notify>(static_cast<real_system&>(System));
                                                }
                                                else if constexpr (T_SYSTEM::typedef_v.id_v == type::id::POOL_FAMILY_CREATE)
                                                {
                                                    Archetype.m_Events.m_OnPoolFamilyCreated.Register<&real_system::OnPoolFamily>(static_cast<real_system&>(System));
                                                }
                                                else if constexpr (T_SYSTEM::typedef_v.id_v == type::id::POOL_FAMILY_DESTROY)
                                                {
                                                    Archetype.m_Events.m_OnPoolFamilyDestroy.Register<&real_system::OnPoolFamily>(static_cast<real_system&>(System));
                                                }
                                                else
                                                {
                                                    static_assert( xecs::types::always_false_v<T_SYSTEM>, "Case is not supported for right now");
                                                }
                                            }
            ,   .m_DestroyFunction      = []( xecs::system::instance& This ) noexcept
                                            {
                                                std::destroy_at(&static_cast< xecs::system::details::compleated<T_SYSTEM>& >(This));
                                            }
            ,   .m_pName                = T_SYSTEM::typedef_v.m_pName
            ,   .m_ID                   = T_SYSTEM::typedef_v.id_v
            };
        }
    }

    //-------------------------------------------------------------------------------------------

    constexpr
    instance::instance( xecs::game_mgr::instance& G ) noexcept
    : m_GameMgr( G )
    { }

    //-------------------------------------------------------------------------------------------

    template< typename T_EVENT, typename T_CLASS, typename...T_ARGS>
    requires( std::derived_from<T_CLASS, xecs::system::instance>
                && (false == std::is_same_v<typename T_CLASS::events, xecs::system::overrides::events>)
                && !!(xecs::types::tuple_t2i_v<T_EVENT, typename T_CLASS::events > +1)
                ) constexpr
    void instance::SendEventFrom(T_CLASS* pThis, T_ARGS&&... Args) noexcept
    {
        std::get<T_EVENT>
        ( static_cast< details::compleated<T_CLASS>* >( pThis )
          ->m_Events
        )
        .NotifyAll( std::forward<T_ARGS&&>(Args) ... );
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename      T_GLOBAL_EVENT
    > requires
    ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
    ) constexpr
    T_GLOBAL_EVENT& instance::getGlobalEvent( void ) const noexcept
    {
        return m_GameMgr.getGlobalEvent<T_GLOBAL_EVENT>();
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename      T_GLOBAL_EVENT
    , typename...   T_ARGS
    > requires
    ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
    ) constexpr
    void instance::SendGlobalEvent( T_ARGS&&... Args ) const noexcept
    {
        m_GameMgr.SendGlobalEvent<T_GLOBAL_EVENT, T_ARGS...>( std::forward<T_ARGS&&>(Args) ... );
    }

    //-------------------------------------------------------------------------------------------
    template
    < typename... T_TUPLES_OF_COMPONENTS_OR_COMPONENTS
    > requires
    ( 
        (   (  xecs::tools::valid_tuple_components_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS>
            || xecs::component::type::is_valid_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS> 
            ) &&... )
    ) constexpr
    archetype::instance& instance::getOrCreateArchetype( void ) const noexcept
    {
        return m_GameMgr.getOrCreateArchetype< T_TUPLES_OF_COMPONENTS_OR_COMPONENTS...>();
    }

    //-------------------------------------------------------------------------------------------
    template
    < typename... T_COMPONENTS
    > constexpr
    [[nodiscard]] std::vector<archetype::instance*>
    instance::Search( const xecs::query::instance& Query ) const noexcept
    {
        return m_GameMgr.Search(Query);
    }

    //-------------------------------------------------------------------------------------------

    template
    <   typename T_TUPLE_ADD
    ,   typename T_TUPLE_SUBTRACT   
    ,   typename T_FUNCTION         
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
    && xecs::types::is_specialized_v<std::tuple, T_TUPLE_ADD>
    && xecs::types::is_specialized_v<std::tuple, T_TUPLE_SUBTRACT>
    ) constexpr
    [[nodiscard]] xecs::component::entity
instance::AddOrRemoveComponents
    ( xecs::component::entity   Entity
    , T_FUNCTION&&              Function
    ) const noexcept
    {
        return m_GameMgr.AddOrRemoveComponents< T_TUPLE_ADD, T_TUPLE_SUBTRACT >( Entity, std::forward<T_FUNCTION&&>(Function));
    }

    //-------------------------------------------------------------------------------------------
    
    void instance::DeleteEntity( xecs::component::entity& Entity ) const noexcept
    {
        return m_GameMgr.DeleteEntity(Entity);
    }

    //-------------------------------------------------------------------------------------------
    
    bool instance::isEntityDead( const xecs::component::entity Entity ) const noexcept
    {
        if( Entity.isZombie() ) return true;
        const auto& GlobalEntity = m_GameMgr.m_ComponentMgr.getEntityDetails( Entity );
        return GlobalEntity.m_Validation != Entity.m_Validation;
    }

    //-------------------------------------------------------------------------------------------

    xecs::archetype::instance& instance::getArchetype(xecs::component::entity Entity) const noexcept
    {
        return m_GameMgr.getArchetype(Entity);
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename...T_COMPONENTS
    , typename   T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    )  
    xecs::prefab::guid instance::CreatePrefab( T_FUNCTION&& Function ) noexcept
    {
        return m_GameMgr.CreatePrefab<T_COMPONENTS...>( std::forward<T_FUNCTION&&>( Function) );
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    )  
    xecs::prefab::guid instance::CreatePrefab( const xecs::tools::bits& ComponentBits, T_FUNCTION&& Function ) noexcept
    {
        return m_GameMgr.CreatePrefab(ComponentBits, std::forward<T_FUNCTION&&>(Function));
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_ADD_TUPLE
    , typename T_SUB_TUPLE
    , typename T_FUNCTION 
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    )  
    void instance::CreatePrefabInstance ( int Count, xecs::prefab::guid PrefabGuid, T_FUNCTION&& Function, bool bRemoveRoot ) noexcept
    {
        m_GameMgr.CreatePrefabInstance<T_ADD_TUPLE, T_SUB_TUPLE>( Count, PrefabGuid, std::forward<T_FUNCTION&&>(Function), bRemoveRoot );
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_ADD_TUPLE
    , typename T_SUB_TUPLE
    , typename T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    )  
    xecs::prefab::guid instance::CreatePrefabVariant( xecs::prefab::guid PrefabGuid, T_FUNCTION&& Function ) noexcept
    {
        return m_GameMgr.CreatePrefabInstance<T_ADD_TUPLE, T_SUB_TUPLE>( PrefabGuid, std::forward<T_FUNCTION&&>(Function) );
    }

    //-------------------------------------------------------------------------------------------
    template
    < typename... T_COMPONENTS
    > requires
    ( xecs::tools::assert_valid_tuple_components_v< std::tuple<T_COMPONENTS...> >
    ) __inline
    bool instance::hasComponents( xecs::component::entity Entity ) const noexcept
    {
        auto& Archetype = getArchetype(Entity);
        auto& Bits      = Archetype.getComponentBits();
        return ( Bits.getBit( xecs::component::type::info_v<T_COMPONENTS>.m_BitID ) && ... );
    }

    //-------------------------------------------------------------------------------------------

    template
    <   typename T_FUNCTION
    ,   auto     T_SHARE_AS_DATA
    > requires
    ( xecs::tools::assert_is_callable_v<T_FUNCTION>
        && (   xecs::tools::function_return_v<T_FUNCTION, bool >
            || xecs::tools::function_return_v<T_FUNCTION, void > )
    ) constexpr
    bool
instance::Foreach
    ( std::span<xecs::archetype::instance* const>    List
    , T_FUNCTION&&                                   Function 
    ) const noexcept
    {
        return m_GameMgr.Foreach<T_FUNCTION,T_SHARE_AS_DATA>( List, std::forward<T_FUNCTION&&>(Function) );
    }

    //-------------------------------------------------------------------------------------------
    template
    < typename T_SYSTEM
    > constexpr
    T_SYSTEM* instance::findSystem( void ) const noexcept
    {
        return m_GameMgr.findSystem<T_SYSTEM>();
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
      && (false == xecs::tools::function_has_share_component_args_v<T_FUNCTION>)
    ) __inline
    bool instance::findEntity
    ( xecs::component::entity Entity
    , T_FUNCTION&&            Function
    ) noexcept
    {
        return m_GameMgr.findEntity( Entity, std::forward<T_FUNCTION>(Function) );
    }

    //-------------------------------------------------------------------------------------------
    template
    < typename T_SYSTEM
    > constexpr
    T_SYSTEM& instance::getSystem( void ) const noexcept
    {
        return m_GameMgr.getSystem<T_SYSTEM>();
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    >requires
    ( xecs::tools::assert_is_callable_v<T_FUNCTION>
        && (xecs::tools::function_return_v<T_FUNCTION, bool >
           || xecs::tools::function_return_v<T_FUNCTION, void >)
    )
    bool instance::Foreach( const xecs::component::share_filter& ShareFilter, const xecs::query::instance& Query, T_FUNCTION&& Function ) noexcept
    {
        xecs::query::iterator<T_FUNCTION> Iterator(m_GameMgr);

        for(const auto& ShareFilterEntry : ShareFilter.m_lEntries )
        {
            // Make sure this archetype matches are query
            if( Query.Compare(ShareFilterEntry.m_pArchetype->getComponentBits(), ShareFilterEntry.m_pArchetype->getExclusiveTagBits() ) == false )
                continue;

            Iterator.UpdateArchetype(*ShareFilterEntry.m_pArchetype);

            for( auto F : ShareFilterEntry.m_lFamilies )
            {
                Iterator.UpdateFamilyPool( *F );
                for( auto p = &F->m_DefaultPool; p ; p = p->m_Next.get() )
                {
                    if( 0 == p->Size() ) continue;

                    // Update the pool
                    Iterator.UpdatePool(*p);

                    // Do all entities
                    if constexpr (xecs::tools::function_return_v<T_FUNCTION, bool>)
                    {
                        if (Iterator.ForeachEntity(std::forward<T_FUNCTION&&>(Function))) return true;
                    }
                    else
                    {
                        Iterator.ForeachEntity(std::forward<T_FUNCTION&&>(Function));
                    }
                }
            }
        }
        return false;
    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_SHARE_COMPONENT
    > __inline
    const xecs::component::share_filter* instance::findShareFilter( T_SHARE_COMPONENT&& ShareComponent, xecs::archetype::guid ArchetypeGuid ) noexcept
    {
        static_assert(xecs::component::type::info_v<T_SHARE_COMPONENT>.m_bBuildShareFilter );
        assert( xecs::component::type::info_v<T_SHARE_COMPONENT>.m_bGlobalScoped ? ArchetypeGuid.empty() : !ArchetypeGuid.empty() );

        return findShareFilter
        (   xecs::component::type::details::ComputeShareKey
            (
                ArchetypeGuid
            ,   xecs::component::type::info_v<T_SHARE_COMPONENT>
            ,   reinterpret_cast<std::byte*>(&ShareComponent)
            )
        );
    }

    //-------------------------------------------------------------------------------------------

    __inline
    const xecs::component::share_filter* instance::findShareFilter( xecs::component::type::share::key Key ) noexcept
    {
        auto&   ShareMap    = m_GameMgr.m_ArchetypeMgr.m_ShareComponentEntityMap;
        auto    It          = ShareMap.find(Key);
        if( It == ShareMap.end() ) return nullptr;

        auto& EntityDetails = m_GameMgr.m_ComponentMgr.getEntityDetails(It->second);
        return &EntityDetails.m_pPool->getComponent<xecs::component::share_filter>(EntityDetails.m_PoolIndex);
    }
}