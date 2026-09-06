namespace xecs::game_mgr
{
    //---------------------------------------------------------------------------

    instance::instance( void ) noexcept
    {
        // Add the System Mgr to the On New Archetype Event
        m_ArchetypeMgr.m_Events.m_OnNewArchetype.Register<&xecs::system::mgr::OnNewArchetype>(m_SystemMgr);
    }

    //---------------------------------------------------------------------------

    template<typename...T_SYSTEMS>
    requires( std::derived_from< T_SYSTEMS, xecs::system::instance> && ... )
    void instance::RegisterSystems() noexcept
    {
        m_ComponentMgr.LockComponentTypes();
        (m_SystemMgr.RegisterSystem<T_SYSTEMS>(*this), ... );
    }

    //---------------------------------------------------------------------------

    template< typename...T_COMPONENTS >
    void instance::RegisterComponents(void) noexcept
    {
        ((m_ComponentMgr.RegisterComponent<T_COMPONENTS>()), ...);
    }

    //---------------------------------------------------------------------------

    template
    < typename...T_GLOBAL_EVENTS
    > requires
    ( std::derived_from< T_GLOBAL_EVENTS, xecs::event::overrides> 
      && ...
    )
    void instance::RegisterGlobalEvents( void ) noexcept
    {
        m_ComponentMgr.LockComponentTypes();
        ((m_EventMgr.Register<T_GLOBAL_EVENTS>()), ...);
    }

    //---------------------------------------------------------------------------

    template
    < typename      T_GLOBAL_EVENT
    , typename...   T_ARGS
    > requires
    ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
    )
    void instance::SendGlobalEvent( T_ARGS&&... Args ) const noexcept
    {
        m_EventMgr.getEvent<T_GLOBAL_EVENT>().NotifyAll( std::forward<T_ARGS&&>(Args) ... );
    }

    //---------------------------------------------------------------------------

    template
    < typename      T_GLOBAL_EVENT
    > requires
    ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
    )
    T_GLOBAL_EVENT& instance::getGlobalEvent( void ) const noexcept
    {
        return m_EventMgr.getEvent<T_GLOBAL_EVENT>();
    }

    //---------------------------------------------------------------------------

    void instance::DeleteEntity( xecs::component::entity& Entity ) noexcept
    {
        assert(Entity.isZombie() == false);
        auto& Info = m_ComponentMgr.getEntityDetails(Entity);
        assert(Info.m_Validation == Entity.m_Validation);
        Info.m_pPool->m_pArchetype->DestroyEntity(Entity);
    }


    //---------------------------------------------------------------------------

    std::vector<archetype::instance*> instance::Search( const xecs::query::instance& Query ) const noexcept
    {
        std::vector<archetype::instance*> ArchetypesFound;
        for( auto& E : m_ArchetypeMgr.m_lArchetypeBits )
        {
            if( Query.Compare(E.first, E.second) )
            {
                const auto Index = static_cast<std::size_t>(&E - &m_ArchetypeMgr.m_lArchetypeBits[0]);
                ArchetypesFound.push_back(m_ArchetypeMgr.m_lArchetype[Index].get());
            }
        }

        return ArchetypesFound;
    }

    //---------------------------------------------------------------------------

    archetype::instance& instance::getOrCreateArchetype( std::span<const component::type::info* const> Types ) noexcept
    {
        xecs::tools::bits Bits{};
        for( auto& e : Types )
            Bits.setBit( e->m_BitID );

        // Make sure we always include the entity
        Bits.setBit( xecs::component::type::info_v<xecs::component::entity>.m_BitID );

        return m_ArchetypeMgr.getOrCreateArchetype(Bits);
    }

    //---------------------------------------------------------------------------
    inline
    archetype::instance* instance::findArchetype( xecs::archetype::guid ArchetypeGuid ) const noexcept
    {
        if ( auto I = m_ArchetypeMgr.m_ArchetypeMap.find(ArchetypeGuid); I != m_ArchetypeMgr.m_ArchetypeMap.end() )
            return I->second;
        return nullptr;
    }

    //---------------------------------------------------------------------------

    template
    < typename... T_TUPLES_OF_COMPONENTS_OR_COMPONENTS
    > requires
    ( 
        (   (  xecs::tools::valid_tuple_components_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS>
            || xecs::component::type::is_valid_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS> 
            ) &&... )
    )
    archetype::instance& instance::getOrCreateArchetype( void ) noexcept
    {
        return [&]<typename...T>( std::tuple<T...>* ) constexpr noexcept -> archetype::instance&
        {
            xecs::tools::bits Bits;
            Bits.AddFromComponents< T... >();
            return m_ArchetypeMgr.getOrCreateArchetype(Bits);

        }( xecs::types::null_tuple_v<xecs::component::type::details::combined_t<xecs::component::entity, T_TUPLES_OF_COMPONENTS_OR_COMPONENTS... >>);
    }

    //---------------------------------------------------------------------------
    template
    < typename T_FUNCTION
    , auto     T_SHARE_AS_DATA_V
    > requires
    ( xecs::tools::assert_is_callable_v<T_FUNCTION>
        && (xecs::tools::function_return_v<T_FUNCTION, bool >
            || xecs::tools::function_return_v<T_FUNCTION, void >)
    )
    bool instance::Foreach(const std::span<xecs::archetype::instance* const> List, T_FUNCTION&& Function ) noexcept
    {
        std::conditional_t
        < T_SHARE_AS_DATA_V
        , xecs::query::iterator<T_FUNCTION, xecs::query::details::mode::DATA_ONLY>
        , xecs::query::iterator<T_FUNCTION>
        > Iterator(*this);

        for( const auto& pE : List )
        {
            Iterator.UpdateArchetype( *pE );
            for( auto pFamily = pE->getFamilyHead(); pFamily; pFamily = pFamily->m_Next.get() )
            {
                Iterator.UpdateFamilyPool( *pFamily );
                for( auto pPool = &pFamily->m_DefaultPool; pPool; pPool = pPool->m_Next.get() )
                {
                    if( 0 == pPool->Size() ) continue;
                    Iterator.UpdatePool( *pPool );

                    if constexpr (std::is_same_v<typename xecs::query::iterator<T_FUNCTION>::ret_t, bool >)
                    {
                        if( Iterator.ForeachEntity( std::forward<T_FUNCTION&&>(Function) ) )
                            return true;
                    }
                    else
                    {
                        Iterator.ForeachEntity( std::forward<T_FUNCTION&&>(Function));
                    }
                }
            }
        }
        return false;
    }

    //---------------------------------------------------------------------------
    template
    < typename T_FUNCTION
    , auto     T_SHARE_AS_DATA_V
    > requires
    ( xecs::tools::assert_is_callable_v<T_FUNCTION>
        && (xecs::tools::function_return_v<T_FUNCTION, bool >
            || xecs::tools::function_return_v<T_FUNCTION, void >)
    )
    bool instance::Foreach( const std::vector<const xecs::archetype::instance*>& List, T_FUNCTION&& Function ) noexcept
    {
        return Foreach( List, std::forward<T_FUNCTION>(Function) );
    }

    //---------------------------------------------------------------------------

    template< typename T_FUNCTION>
    requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    ) [[nodiscard]] xecs::component::entity 
    instance::findEntity( xecs::component::entity Entity, T_FUNCTION&& Function ) noexcept
    {
        //
        // is the entity already dead?
        //
        if (Entity.isZombie()) return {};

        auto& EntityDetails = m_ComponentMgr.getEntityDetails(Entity);
        if ( Entity.m_Validation != EntityDetails.m_Validation ) return {};

        static_assert(xecs::types::is_const_v<const int&>);

        //
        // does the user want to do anything?
        //
        if constexpr (std::is_same_v< T_FUNCTION, xecs::tools::empty_lambda>) return Entity;

        //
        // Time to do some work...
        //
        else if constexpr ( xecs::tools::function_has_share_component_args_v<T_FUNCTION> )
        {
            using sorted_shares_t = xecs::component::type::details::sort_tuple_t<xecs::component::type::details::share_only_tuple_t< typename xecs::function::traits<T_FUNCTION>::args_tuple >>;

            if constexpr( xecs::tools::tuple_only_const_types_v< xecs::component::type::details::share_only_tuple_t< typename xecs::function::traits<T_FUNCTION>::args_tuple > > )
            {
                using data_t = xecs::component::type::details::data_only_tuple_t < typename xecs::function::traits<T_FUNCTION>::args_tuple >;

                auto ShareTuple = [&]< typename...T_ARGS >( std::tuple<T_ARGS...>* ) constexpr noexcept
                {
                    using share_ptr_tuple   = std::tuple< xecs::types::decay_full_t<T_ARGS>* ... >;
                    auto& Family            = *EntityDetails.m_pPool->m_pMyFamily;
                    int   i                 = 0;

                    return share_ptr_tuple
                    {
                        [&]<typename T>(std::tuple<T>*) constexpr noexcept 
                        {
                            using t = xecs::types::decay_full_t<T>;

                            if constexpr ( std::is_pointer_v<T> )
                            {                                
                                if( i >= Family.m_ShareInfos.size() )
                                {
                                    return static_cast<t*>(nullptr);
                                }
                                else 
                                {
                                    int n = i;
                                    while( !xecs::component::type::IsComponentType<T>(Family.m_ShareInfos[i]) )
                                    {
                                        i++;
                                        if (i >= Family.m_ShareInfos.size())
                                        {
                                            i = n;
                                            return static_cast<t*>(nullptr);
                                        }
                                    }
                                }
                            }
                            else
                            {
                                while( !xecs::component::type::IsComponentType<T>(Family.m_ShareInfos[i]) ) i++;
                            }

                            auto Entity = Family.m_ShareDetails[i].m_Entity;
                            i++;
                            return &m_ComponentMgr.getEntityDetails(Entity).m_pPool->getComponent<t>(EntityDetails.m_PoolIndex);

                        }( xecs::types::make_null_tuple_v<T_ARGS> ) ...
                    };

                }( xecs::types::null_tuple_v<sorted_shares_t> );

                //
                // Collect all the data components
                //
                auto Pointers = xecs::archetype::details::GetDataComponentPointerArray
                ( *EntityDetails.m_pPool
                , EntityDetails.m_PoolIndex
                , xecs::types::null_tuple_v<data_t>
                );

                //
                // Call user function
                //
                [&]<typename...T_ARG>( std::tuple<T_ARG...>* ) constexpr noexcept
                {
                    Function
                    (
                        [&]<typename T>( std::tuple<T>* ) constexpr noexcept -> T
                        {
                            if constexpr ( xecs::component::type::info_v<T>.m_TypeID == xecs::component::type::id::SHARE )
                            {
                                if constexpr ( std::is_pointer_v<T> ) return reinterpret_cast<T>(  std::get<xecs::types::decay_full_t<T>* >(ShareTuple) );
                                else                                  return reinterpret_cast<T>( *std::get<xecs::types::decay_full_t<T>* >(ShareTuple) );
                            }
                            else
                            {
                                if constexpr ( std::is_pointer_v<T> ) return reinterpret_cast<T>(  Pointers[xecs::types::tuple_t2i_v<T, data_t>] ? Pointers[xecs::types::tuple_t2i_v<T, data_t>] : nullptr );
                                else                                  return reinterpret_cast<T>( *Pointers[xecs::types::tuple_t2i_v<T, data_t>] );
                            }
                        }( xecs::types::make_null_tuple_v<T_ARG> )...
                    );

                }(xecs::types::null_tuple_v< typename xecs::function::traits<T_FUNCTION>::args_tuple >);
            }
            else
            {
                [&] < typename...T_ARGS > (std::tuple<T_ARGS...>*)
                {
                    using shares_t        = xecs::component::type::details::sort_tuple_t<xecs::component::type::details::share_only_tuple_t<std::tuple<xecs::types::decay_full_t<T_ARGS>...>>>;
                    using share_index     = std::array< std::int8_t, std::tuple_size_v<shares_t> >;

                    [&]< typename...T_SHARE_ARGS >( std::tuple<T_SHARE_ARGS...>* )
                    {
                        constexpr auto we_have_share_pointers_v = ((std::is_pointer_v< std::tuple_element< xecs::types::tuple_t2i_v<T_SHARE_ARGS, shares_t>, sorted_shares_t > >) || ... );

                        auto&           Family      = *EntityDetails.m_pPool->m_pMyFamily;
                        int             i           = 0;
                        share_index     ShareIndex  {};
                        std::uint64_t   KeySum      = 0;

                        //
                        // Copy all the share data
                        //
                        shares_t Shares{ [&] < typename T_SHARE >(T_SHARE*) constexpr noexcept
                        {
                            if constexpr (std::is_pointer_v< std::tuple_element< xecs::types::tuple_t2i_v<T_SHARE, shares_t>, sorted_shares_t > >)
                            {
                                while (!xecs::component::type::IsComponentType<T_SHARE>(Family.m_ShareInfos[i])) ++i;
                            }
                            else
                            {
                                int n = i;
                                while (!xecs::component::type::IsComponentType<T_SHARE>(Family.m_ShareInfos[i]))
                                {
                                    i++;
                                    if (i == static_cast<int>(Family.m_ShareInfos.size()))
                                    {
                                        i = n;
                                        ShareIndex[xecs::types::tuple_t2i_v<T_SHARE, shares_t>] = -1;
                                        return T_SHARE{};
                                    }
                                }
                            }

                            KeySum += Family.m_ShareDetails[i].m_Key.m_Value;
                            ShareIndex[xecs::types::tuple_t2i_v<T_SHARE, shares_t>] = i;
                            auto& Details = m_ComponentMgr.getEntityDetails(Family.m_ShareDetails[i].m_Entity);

                            // get ready for the next type
                            i++;
                            return Details.m_pPool->getComponent<T_SHARE>(Details.m_PoolIndex);

                        }(static_cast<T_SHARE_ARGS*>(nullptr)) ... };

                        //
                        // get the data pointers
                        //
                        using data_t = xecs::component::type::details::sort_tuple_t<xecs::component::type::details::data_only_tuple_t < typename xecs::function::traits<T_FUNCTION>::args_tuple >>;
                        auto Pointers = xecs::archetype::details::GetDataComponentPointerArray( *EntityDetails.m_pPool, EntityDetails.m_PoolIndex, xecs::types::null_tuple_v<data_t> );

                        //
                        // Call the user function
                        //
                        [&]<typename...J>(std::tuple<J...>*) constexpr noexcept
                        {
                            Function
                            (
                                [&]<typename T>(std::tuple<T>*) constexpr noexcept -> T
                                {
                                    if constexpr ( xecs::component::type::info_v<T>.m_TypeID == xecs::component::type::id::SHARE )
                                    {
                                        if constexpr( std::is_pointer_v<T> )  return reinterpret_cast<T>( ShareIndex[ xecs::types::tuple_t2i_v<T, sorted_shares_t> ] == -1 ? nullptr : &std::get< xecs::types::decay_full_t<T> >(Shares) );
                                        else                                  return std::get< xecs::types::decay_full_t<T> >(Shares);
                                    }
                                    else
                                    {
                                        if constexpr ( std::is_pointer_v<T> ) return reinterpret_cast<T>(  Pointers[ xecs::types::tuple_t2i_v< T, data_t > ] );
                                        else                                  return reinterpret_cast<T>( *Pointers[ xecs::types::tuple_t2i_v< T, data_t > ] );
                                    }
                                }(xecs::types::make_null_tuple_v<J>)...
                            );

                        }(xecs::types::null_tuple_v< typename xecs::function::traits<T_FUNCTION>::args_tuple >);

                        //
                        // Check if we need to move our entity
                        //
                        std::uint64_t                                                                NewKeySum          = 0;
                        std::array< xecs::component::type::share::key, std::tuple_size_v<shares_t> > NewShareKeys       {};
                        std::array< std::byte*, std::tuple_size_v<shares_t> >                        PointerToShares    {};
                        i=0;
                        ( ([&] < typename T_SHARE >(T_SHARE*) constexpr noexcept
                        {
                            if constexpr (std::is_pointer_v< std::tuple_element< xecs::types::tuple_t2i_v<T_SHARE, shares_t>, sorted_shares_t > >)
                            {
                                if( ShareIndex[xecs::types::tuple_t2i_v<T_SHARE, shares_t>] == -1) return;
                            }

                            PointerToShares[i] = reinterpret_cast<std::byte*>(&std::get<T_SHARE>(Shares));
                            NewShareKeys[i]    = xecs::component::type::details::ComputeShareKey(EntityDetails.m_pPool->m_pArchetype->getGuid(), xecs::component::type::info_v<T_SHARE>, PointerToShares[i] );                        
                            NewKeySum         += NewShareKeys[i].m_Value;
                            i++;

                        }(static_cast<T_SHARE_ARGS*>(nullptr)) ), ... );

                        if( NewKeySum != KeySum )
                        {
                            xecs::tools::bits UpdatedComponentsBits;
                            UpdatedComponentsBits.AddFromComponents< xecs::types::decay_full_t<T_SHARE_ARGS>... >();

                            //
                            // Get the new family and move the entity there
                            //
                            EntityDetails.m_pPool->m_pArchetype->getOrCreatePoolFamilyFromSameArchetype
                            ( *EntityDetails.m_pPool->m_pMyFamily
                            , UpdatedComponentsBits
                            , { PointerToShares.data(), static_cast<std::size_t>(i) }
                            , { NewShareKeys.data(), static_cast<std::size_t>(i) }
                            )
                            .MoveIn
                            ( *this
                            , *EntityDetails.m_pPool->m_pMyFamily
                            , *EntityDetails.m_pPool
                            , EntityDetails.m_PoolIndex
                            );
                        }

                    }( xecs::types::null_tuple_v<shares_t> );

                }(xecs::types::null_tuple_v< typename xecs::function::traits<T_FUNCTION>::args_tuple >);
            }
        }
        else
        {
            auto Pointers = xecs::archetype::details::GetDataComponentPointerArray
            ( *EntityDetails.m_pPool
            , EntityDetails.m_PoolIndex
            , xecs::types::null_tuple_v<typename xecs::function::traits<T_FUNCTION>::args_tuple> 
            );
            xecs::archetype::details::CallFunction( std::forward<T_FUNCTION>(Function), Pointers );
        }

        return Entity;
    }

    //---------------------------------------------------------------------------

    [[nodiscard]] xecs::archetype::instance& instance::getArchetype( xecs::component::entity Entity ) const noexcept
    {
        assert(Entity.isZombie() == false);
        auto& Entry = m_ComponentMgr.getEntityDetails(Entity);
        assert( Entity.m_Validation == Entry.m_Validation );
        return *Entry.m_pPool->m_pArchetype;
    }

    //---------------------------------------------------------------------------

    template< typename T_FUNCTION>
    requires( xecs::tools::assert_standard_function_v<T_FUNCTION> )
    [[nodiscard]] xecs::component::entity instance::getEntity( xecs::component::entity Entity, T_FUNCTION&& Function ) noexcept
    {
        auto b = findEntity( Entity, std::forward<T_FUNCTION&&>(Function) );
        assert( b.isValid() );
        return b;
    }

    //---------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
    )
    xecs::component::entity instance::AddOrRemoveComponents
    ( xecs::component::entity                               Entity
    , std::span<const xecs::component::type::info* const>   Add
    , std::span<const xecs::component::type::info* const>   Sub
    , T_FUNCTION&&                                          Function 
    ) noexcept
    {
        xecs::tools::bits AddBits;
        xecs::tools::bits SubBits;

        for( auto& e : Add ) AddBits.setBit(e->m_BitID);
        for (auto& e : Sub ) SubBits.setBit(e->m_BitID);

        return m_ArchetypeMgr.AddOrRemoveComponents
        ( Entity
        , AddBits
        , SubBits
        , std::forward<T_FUNCTION&&>(Function)
        );
    }

    //---------------------------------------------------------------------------

    template
    <   typename T_TUPLE_ADD
    ,   typename T_TUPLE_SUBTRACT
    ,   typename T_FUNCTION
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
        && xecs::types::is_specialized_v<std::tuple, T_TUPLE_ADD>
        && xecs::types::is_specialized_v<std::tuple, T_TUPLE_SUBTRACT>
    )
    xecs::component::entity
instance::AddOrRemoveComponents
    ( xecs::component::entity   Entity
    , T_FUNCTION&&              Function 
    ) noexcept
    {
        xecs::tools::bits AddBits;
        xecs::tools::bits SubBits;

        [&]<typename...T>(std::tuple<T...>*){ AddBits.AddFromComponents<T...>(); }( xecs::types::null_tuple_v<T_TUPLE_ADD> );
        [&]<typename...T>(std::tuple<T...>*){ SubBits.AddFromComponents<T...>(); }( xecs::types::null_tuple_v<T_TUPLE_SUBTRACT> );

        if constexpr ( std::is_same_v<T_FUNCTION, xecs::tools::empty_lambda > )
            return m_ArchetypeMgr.AddOrRemoveComponents
            ( Entity
            , AddBits
            , SubBits
            );
        else
            return m_ArchetypeMgr.AddOrRemoveComponents
            ( Entity
            , AddBits
            , SubBits
            , Function
            );
    }

    //---------------------------------------------------------------------------
    template< typename T_SYSTEM >
    T_SYSTEM* instance::findSystem( void ) noexcept
    {
        return m_SystemMgr.find<T_SYSTEM>();
    }

    //---------------------------------------------------------------------------
    template< typename T_SYSTEM >
    T_SYSTEM& instance::getSystem(void) noexcept
    {
        auto p = m_SystemMgr.find<T_SYSTEM>();
        assert(p);
        return *p;
    }

    //---------------------------------------------------------------------------
    namespace details::create_prefab_instance
    {
        template< typename T >
        struct filter;

        template< typename...T >
        struct filter< std::tuple<T...> >
        {
            // If we are going to have to modify shares we must copy all the components before hand
            // Because we must know how the user modify the shares so that we can know which family we need to put the entity in
            static constexpr bool has_modify_shares_v   = sizeof...(T) > 0 && (((xecs::component::type::info_v<T>.m_TypeID == xecs::component::type::id::SHARE && std::is_const_v<T> == false) || ...));
            using                 func_copy_tuple_t     = xecs::types::tuple_cat_t< std::conditional_t< has_modify_shares_v, std::tuple< xecs::types::decay_full_t<T> >, std::tuple<> > ... >;


            using                   func_tuple_t        = std::tuple<T...>;
            using                         data_t        = xecs::component::type::details::data_only_tuple_t<std::tuple<std::remove_reference_t<T>...>>;
            using                 shorted_data_t        = xecs::component::type::details::sort_tuple_t<data_t>;
            using                       shares_t        = xecs::component::type::details::share_only_tuple_t<std::tuple<std::remove_reference_t<T>...>>;
            using               shorted_shares_t        = xecs::component::type::details::sort_tuple_t<shares_t>;
            using              writable_shares_t        = xecs::types::tuple_cat_t< std::conditional_t<xecs::component::type::info_v<T>.m_TypeID == xecs::component::type::id::SHARE && std::is_const_v<T> == false, std::tuple<xecs::types::decay_full_t<T>>, std::tuple<>> ... >;
        };
    }

    //---------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    ) xecs::prefab::guid instance::CreatePrefab( xecs::tools::bits         ComponentBits
                                               , T_FUNCTION&&              Function
                                               ) noexcept
    {
        xecs::prefab::guid PrefabGuid;

        PrefabGuid.m_Instance.clear();
        PrefabGuid.m_Instance.m_Value |= 1; // always set 1 for resources

        ComponentBits.AddFromComponents<xecs::component::entity, xecs::prefab::tag, xecs::prefab::root>();

        xassert( false == ComponentBits.getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID) );

        // Create the Prefab
        auto PrefabEntity = [&]<typename...T_ARGS>(std::tuple<T_ARGS...>*) noexcept
        {
            return m_ArchetypeMgr.getOrCreateArchetype(ComponentBits)
                .CreateEntity([&](xecs::prefab::root& Root, T_ARGS&... Args) noexcept
                    {
                        Root.m_Guid = PrefabGuid;
                        if constexpr (false == std::is_same_v<T_FUNCTION, xecs::tools::empty_lambda>) Function(Args...);
                    });
        }(xecs::types::null_tuple_v<typename xecs::function::traits<T_FUNCTION>::args_tuple>);

        // Register the prefab
        m_PrefabMgr.m_PrefabList.insert({ PrefabGuid.m_Instance.m_Value, PrefabEntity });

        return PrefabGuid;
    }

    //---------------------------------------------------------------------------

    template
    < typename...T_COMPONENTS
    , typename T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    ) xecs::prefab::guid instance::CreatePrefab( T_FUNCTION&& Function ) noexcept
    {
        xecs::tools::bits ComponentBits;
        ComponentBits.AddFromComponents<T_COMPONENTS...>();
        return CreatePrefab(ComponentBits, std::forward<T_FUNCTION&&>(Function) );
    }

    //---------------------------------------------------------------------------

    template
    < typename T_ADD_TUPLE
    , typename T_SUB_TUPLE
    , typename T_FUNCTION
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION>
    ) void
    instance::CreatePrefabInstance( int Count, xecs::prefab::guid PrefabGuid, T_FUNCTION&& Function, bool bRemoveRoot ) noexcept
    {
        if (auto It = m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value); It != m_PrefabMgr.m_PrefabList.end())
        {
            m_PrefabMgr.CreatePrefabInstance<T_ADD_TUPLE, T_SUB_TUPLE>( Count, It->second, std::forward<T_FUNCTION&&>(Function), bRemoveRoot, false );
        }
        else
        {
            //Unable to find the prefab
            xassert(false);
        }
    }

    //---------------------------------------------------------------------------

    template
    < typename T_ADD_TUPLE
    , typename T_SUB_TUPLE
    , typename T_FUNCTION 
    > requires
    ( xecs::tools::assert_standard_function_v<T_FUNCTION> )
    [[nodiscard]] xecs::prefab::guid
    instance::CreatePrefabVariant(xecs::prefab::guid        PrefabGuid
                                 , T_FUNCTION&&             Function
                                 ) noexcept
    {
        xecs::prefab::guid PrefabVariantGuid;

        if( auto It = m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value); It != m_PrefabMgr.m_PrefabList.end() )
        {
            xecs::component::entity PrefabVarientEntity;

            PrefabVariantGuid.m_Instance.clear();
            PrefabVariantGuid.m_Instance.m_Value |= 1; // always set 1 for resources

            [&] <typename...T_ARGS>( std::tuple<T_ARGS...>* ) noexcept
            {
                PrefabVarientEntity = m_PrefabMgr.CreatePrefabInstance<T_ADD_TUPLE, T_SUB_TUPLE>
                ( 1
                , It->second
                , [&]( xecs::prefab::root& Root, T_ARGS&&... Components ) noexcept
                {
                    Root.m_Guid             = PrefabVariantGuid;
                    Root.m_ParentPrefabGuid = PrefabGuid;

                    if constexpr ( false == std::is_same_v<T_FUNCTION, xecs::tools::empty_lambda> ) Function( std::forward<T_ARGS&&>(Components)... );
                }
                , false
                , true
                );
            }( xecs::types::null_tuple_v<typename xecs::function::traits<T_FUNCTION>::args_tuple> );
            
            // Register the prefab
            m_PrefabMgr.m_PrefabList.insert({ PrefabVariantGuid.m_Instance.m_Value, PrefabVarientEntity });
        }
        else
        {
            // Unable to find the prefab
            xassert(false);
        }
        
        return PrefabVariantGuid;
    }
}