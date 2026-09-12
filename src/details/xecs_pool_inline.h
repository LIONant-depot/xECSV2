namespace xecs::pool
{
    //-------------------------------------------------------------------------------------
    // POOL FAMILY
    //-------------------------------------------------------------------------------------

    //-------------------------------------------------------------------------------------
    constexpr
    family::guid family::ComputeGuid
    ( const xecs::archetype::guid                           Guid
    , std::span<const xecs::component::type::info* const>   Types
    , std::span<const std::byte* const>                     Data 
    ) noexcept
    {
        assert( Types.size() == Data.size() );
        family::guid FinalGuid{ Guid.m_Value };
        for( int i=0; i<Data.size(); i++ )
        {
            FinalGuid.m_Value += xecs::component::type::details::ComputeShareKey( Guid, *Types[i], Data[i] ).m_Value;
        }
        return FinalGuid;
    }

    //-------------------------------------------------------------------------------------

    void family::Initialize
    ( family::guid                                      Guid
    , archetype::instance&                              Archetype
    , std::span<xecs::component::entity>                ShareEntityList
    , std::span<xecs::component::type::share::key>      ShareKeyList
    , std::span<const xecs::component::type::info*>     ShareInfoList
    , std::span<const xecs::component::type::info*>     DataInfoList
    ) noexcept
    {
        assert(!Guid.empty());
        assert(DataInfoList.data());
        assert(ShareEntityList.size() == ShareKeyList.size());
        assert(ShareEntityList.size() == ShareInfoList.size());

        //
        // Do a sanity check
        //
#if _DEBUG

        //
        // Make sure all the keys are correct
        //
        {
            for( int i=0; i< ShareEntityList.size(); ++i )
            {
                auto& Details    = Archetype.m_Mgr.m_GameMgr.m_ComponentMgr.getEntityDetails(ShareEntityList[i]);
                auto  TypeIndex  = Details.m_pPool->findIndexComponentFromInfo(*ShareInfoList[i]);
                auto  pData      = &Details.m_pPool->m_pComponent[TypeIndex][Details.m_PoolIndex.m_Value*ShareInfoList[i]->m_Size];
                auto  Key        = xecs::component::type::details::ComputeShareKey( Archetype.m_Guid, *ShareInfoList[i], pData );
                assert(Key == ShareKeyList[i]);
            }
        }

#endif

        m_Guid               = Guid;
        m_pArchetypeInstance = &Archetype;
        m_ShareInfos         = ShareInfoList;
        for( int i=0;i< ShareKeyList.size(); i++)
        {
            auto& E = m_ShareDetails[i];
            E.m_Entity = ShareEntityList[i];
            E.m_Key    = ShareKeyList[i];
        }

        m_DefaultPool.Initialize( DataInfoList, *this );
    }

    //-------------------------------------------------------------------------------------
    template
    < typename T_FUNCTION
    >
    void family::AppendEntities( int Count, xecs::archetype::mgr& ArchetypeMgr, T_FUNCTION&& Function ) noexcept
    {
        auto pPool = &m_DefaultPool;
        do
        {
            int FreeSpace = pPool->getFreeSpace();
            if (FreeSpace > 0)
            {
                //
                // Update the counts
                //
                const int nAlloc = std::min(Count, FreeSpace);
                Count -= nAlloc;

                //
                // Allocate the entities
                // 
                pool::index Index = pPool->Append(nAlloc);

                //
                // Lock the pool
                //
                ArchetypeMgr.AddToStructuralPendingList(*pPool);
                
                //
                // Call the user's function
                //
                Function( *pPool, Index, nAlloc );

                //
                // Are we done?
                //
                if (Count == 0) return;
            }
            else if (pPool->m_Next.get() == nullptr)
            {
                // Create a new pool
                pPool->m_Next = std::make_unique<pool::instance>();
                pPool->m_Next->Initialize( m_DefaultPool.m_ComponentInfos, *this );
            }
            else
            {
                pPool = pPool->m_Next.get();
            }

        } while( true );
    }

    //-------------------------------------------------------------------------------------

    void family::MoveIn
    ( xecs::game_mgr::instance& GameMgr
    , xecs::pool::family&       FromFamily
    , xecs::pool::instance&     FromPool
    , xecs::pool::index         FromIndex
    ) noexcept
    {
        GameMgr.m_ArchetypeMgr.AddToStructuralPendingList(FromPool);
        AppendEntities( 1, GameMgr.m_ArchetypeMgr, [&]( xecs::pool::instance& ToPool, xecs::pool::index ToIndex, int ) noexcept
        {
            assert( &FromPool != &ToPool );

            const auto  Entity  = FromPool.getComponent<xecs::component::entity>(FromIndex);
            auto&       Details = GameMgr.m_ComponentMgr.m_GlobalEntityInfos.getEntityDetails(Entity);

            // Move the entity
            ToPool.MoveInFromPool( ToIndex, FromIndex, FromPool );

            // Update the global info
            Details.m_pPool     = &ToPool;
            Details.m_PoolIndex = ToIndex;
        });
    }

    //-------------------------------------------------------------------------------------
    // POOL INSTANCE
    //-------------------------------------------------------------------------------------

    instance::~instance(void) noexcept
    {
        if (m_pComponent[0])
        {
            Clear();
            for (int i = 0, end = static_cast<int>(m_ComponentInfos.size()); i < end; ++i)
            {
                if(m_pComponent[i]) VirtualFree(m_pComponent[i], 0, MEM_RELEASE);
            }
        }
    }

    //-------------------------------------------------------------------------------------
    // This function return which page is the last byte of a given entry
    //-------------------------------------------------------------------------------------
    constexpr inline 
    int getPageFromIndex( const component::type::info& Info, int iEntity ) noexcept
    {
        return ((iEntity * static_cast<int>(Info.m_Size))-1) / xecs::settings::virtual_page_size_v;
    }

    //-------------------------------------------------------------------------------------

    void instance::Initialize
    ( std::span<const component::type::info* const >    Span
    , xecs::pool::family&                               Family
    ) noexcept
    {
        assert(Span.size());

        m_ComponentInfos            = Span;
        m_pMyFamily                 = &Family;
        m_pArchetype                = Family.m_pArchetypeInstance;

        // Reserve virtual memory for our data components
        for( int i=0, end = static_cast<int>(m_ComponentInfos.size()); i<end; ++i )
        {
            assert(m_ComponentInfos[i]->m_Size <= xecs::settings::virtual_page_size_v);
            auto nPages     = getPageFromIndex( *m_ComponentInfos[i], xecs::settings::max_entity_count_per_pool_v ) + 1;
            m_pComponent[i] = reinterpret_cast<std::byte*>(VirtualAlloc(nullptr, nPages * static_cast<std::size_t>(xecs::settings::virtual_page_size_v), MEM_RESERVE, PAGE_NOACCESS));
            assert(m_pComponent[i]);
        }
    }

    //-------------------------------------------------------------------------------------

    void instance::Clear( void ) noexcept
    {
        while(m_Size)
        {
            Free( {m_Size-1}, true );
        }
    }

    //-------------------------------------------------------------------------------------
    constexpr
    int instance::getFreeSpace( void ) const noexcept
    {
        return  xecs::settings::max_entity_count_per_pool_v - m_Size;
    }

    //-------------------------------------------------------------------------------------

    index instance::Append( int Count ) noexcept
    {
        assert( (Count + m_Size) <= xecs::settings::max_entity_count_per_pool_v );

        for( int i = 0, end = static_cast<int>(m_ComponentInfos.size()); i < end; ++i )
        {
            const auto&   MyInfo  = *m_ComponentInfos[i];
            auto          NexPage = getPageFromIndex(MyInfo, m_Size+Count);

            // Create pages when needed 
            if( auto Cur = getPageFromIndex(MyInfo, m_Size); m_Size == 0 || Cur != NexPage )
            {
                if( m_Size == 0 ) Cur--;
                auto pNewPagePtr = m_pComponent[i] + xecs::settings::virtual_page_size_v * (Cur+1);
                auto p           = reinterpret_cast<std::byte*>(VirtualAlloc(pNewPagePtr, (NexPage - Cur) * static_cast<std::size_t>(xecs::settings::virtual_page_size_v), MEM_COMMIT, PAGE_READWRITE));
                assert(p == pNewPagePtr);
            }

            // Construct if required
            if( MyInfo.m_pConstructFn ) 
            {
                auto p = &m_pComponent[i][m_Size * MyInfo.m_Size];
                for(int j=Count; j; --j )
                {
                    MyInfo.m_pConstructFn(p);
                    p += MyInfo.m_Size;
                }
            }
        }

        auto Index = m_Size;
        m_Size += Count;
        return { Index };
    }

    //-------------------------------------------------------------------------------------

    bool instance::Free( index Index, bool bCallDestructors ) noexcept
    {
        assert(Index.m_Value>=0);

        if (Index.m_Value >= m_Size) return false;
        assert(m_Size);

        // Subtract one to the total count if we can.
        // If the last entry then happens to be a zombie then keep subtracting since these entries will get deleted later
        // This should make things faster and allow for the moved entries to be deleted properly
        do 
        {
            m_Size--;
            if( reinterpret_cast<xecs::component::entity&>(m_pComponent[0][sizeof(xecs::component::entity) * m_Size]).isZombie() == false )
            {
                if( m_Size == Index.m_Value )
                {
                    // We are not moving anything just just call destructors if we have to
                    if (bCallDestructors)
                    {
                        for (int i = 0; i < m_ComponentInfos.size(); ++i)
                        {
                            const auto& MyInfo = *m_ComponentInfos[i];
                            auto        pData = m_pComponent[i];
                            if (MyInfo.m_pDestructFn) MyInfo.m_pDestructFn(&pData[Index.m_Value * MyInfo.m_Size]);
                        }
                    }

                    return false;
                }

                m_Size++;
                break;
            }

            // We are not moving anything just just call destructors if we have to
            if (bCallDestructors)
            {
                for (int i = 0; i < m_ComponentInfos.size(); ++i)
                {
                    const auto& MyInfo = *m_ComponentInfos[i];
                    auto        pData = m_pComponent[i];
                    if (MyInfo.m_pDestructFn) MyInfo.m_pDestructFn(&pData[Index.m_Value * MyInfo.m_Size]);
                }
            }

        } while (m_Size);

        // Check if we have any entry to move
        if(Index.m_Value < m_Size)
        {
            m_Size--;
            for (int i = 0; i < m_ComponentInfos.size(); ++i)
            {
                const auto& MyInfo = *m_ComponentInfos[i];
                auto        pData  = m_pComponent[i];

                if ( MyInfo.m_pMoveFn )
                {
                    MyInfo.m_pMoveFn( &pData[Index.m_Value * MyInfo.m_Size], &pData[m_Size * MyInfo.m_Size] );
                }
                else
                {
                    if (bCallDestructors && MyInfo.m_pDestructFn) MyInfo.m_pDestructFn(&pData[Index.m_Value * MyInfo.m_Size]);
                    memcpy(&pData[Index.m_Value * MyInfo.m_Size], &pData[m_Size * MyInfo.m_Size], MyInfo.m_Size );
                }
            }
            return true;
        }

        return false;
    }

    //-------------------------------------------------------------------------------------
    constexpr
    int instance::Size( void ) const noexcept
    {
        return m_CurrentCount;
    }

    //-------------------------------------------------------------------------------------

    constexpr
    int instance::findIndexComponentFromInfoInSequence
    ( const xecs::component::type::info& SearchInfo
    , int&                               Sequence
    ) const noexcept
    {
        const auto Backup = Sequence;
        // Pointer equality is the fast same-module path. Across DLL boundaries the same T has
        // distinct info_v<T> addresses (host vs xECSV2.dll), so also match m_Guid - CompareTypeInfos
        // alone cannot return a hit (it only does sorted early-out).
        for( const auto end = static_cast<const int>(m_ComponentInfos.size()); Sequence < end; ++Sequence)
        {
            if (&SearchInfo == m_ComponentInfos[Sequence]) return Sequence++;
            if (SearchInfo.m_Guid == m_ComponentInfos[Sequence]->m_Guid) return Sequence++;
            [[unlikely]] if ( xecs::component::type::details::CompareTypeInfos(&SearchInfo, m_ComponentInfos[Sequence])) break;
        }
        Sequence = Backup;
        return -1;
    }

    //-------------------------------------------------------------------------------------
    constexpr
    int instance::findIndexComponentFromInfo( const xecs::component::type::info& SearchInfo ) const noexcept
    {
        // See findIndexComponentFromInfoInSequence - GUID match required across module boundaries.
        for( int i=0, end = static_cast<int>(m_ComponentInfos.size()); i<end; ++i )
        {
            if( &SearchInfo == m_ComponentInfos[i] ) return i;
            if( SearchInfo.m_Guid == m_ComponentInfos[i]->m_Guid ) return i;
            [[unlikely]] if(xecs::component::type::details::CompareTypeInfos(&SearchInfo, m_ComponentInfos[i] )) return -1;
        }
        return -1;
    }

    //-------------------------------------------------------------------------------------

    template
    < typename T_COMPONENT
    > requires
    ( std::is_same_v<T_COMPONENT, std::decay_t<T_COMPONENT>>
    )
    T_COMPONENT& instance::getComponent( const index EntityIndex ) const noexcept
    {
        if constexpr( std::is_same_v<xecs::component::entity, T_COMPONENT>)
        {
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[0][ EntityIndex.m_Value * sizeof(T_COMPONENT) ]
            );
        }
        else
        {
            const auto iComponent = findIndexComponentFromInfo( xecs::component::type::info_v<T_COMPONENT> );
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[iComponent][ EntityIndex.m_Value * sizeof(T_COMPONENT) ]
            );
        }
    }

    //-------------------------------------------------------------------------------------

    template
    < typename T_COMPONENT
    > requires
    ( std::is_same_v<T_COMPONENT, std::decay_t<T_COMPONENT>>
    )
    T_COMPONENT& instance::getComponentInSequence
    ( index     EntityIndex
    , int&      Sequence
    ) const noexcept
    {
        if constexpr (std::is_same_v<xecs::component::entity, T_COMPONENT>)
        {
            assert(Sequence==0);
            Sequence = 1;
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[0][EntityIndex.m_Value * sizeof(T_COMPONENT)]
            );
        }
        else
        {
            const auto iComponent = findIndexComponentFromInfoInSequence(xecs::component::type::info_v<T_COMPONENT>, Sequence );
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[iComponent][ EntityIndex.m_Value * sizeof(T_COMPONENT) ]
            );
        }
    }

    //-------------------------------------------------------------------------------------

    std::byte* instance::getComponentInSequenceByInfo
    ( const xecs::component::type::info&  Info
    , index                               EntityIndex
    , int&                                Sequence
    ) noexcept
    {
        const auto iComponent = findIndexComponentFromInfoInSequence(Info, Sequence );
        return &m_pComponent[iComponent][EntityIndex.m_Value * Info.m_Size ];
    }

    //-------------------------------------------------------------------------------------

    void instance::UpdateStructuralChanges( xecs::component::mgr& ComponentMgr ) noexcept
    {
        const auto OldSize = m_Size;

        //
        // Delete the regular entries
        //
        while( m_DeleteGlobalIndex != invalid_delete_global_index_v )
        {
            auto&       Entry                   = ComponentMgr.m_GlobalEntityInfos.m_pGlobalInfo[m_DeleteGlobalIndex];
            auto&       PoolEntity              = reinterpret_cast<xecs::component::entity&>(m_pComponent[0][sizeof(xecs::component::entity)*Entry.m_PoolIndex.m_Value]);
            const auto  NextDeleteGlobalIndex   = PoolEntity.m_Validation.m_Generation;
            assert(PoolEntity.m_Validation.m_bZombie);

            if( Free(Entry.m_PoolIndex, true) )
            {
                ComponentMgr.DeleteGlobalEntity(m_DeleteGlobalIndex, PoolEntity);
            }
            else
            {
                ComponentMgr.DeleteGlobalEntity(m_DeleteGlobalIndex);
            }

            m_DeleteGlobalIndex = NextDeleteGlobalIndex;
        }

        //
        // Delete Entities that were moved
        //
        while (m_DeleteMoveIndex != invalid_delete_global_index_v)
        {
            auto&       PoolEntity            = reinterpret_cast<xecs::component::entity&>(m_pComponent[0][sizeof(xecs::component::entity) * m_DeleteMoveIndex]);
            const auto  NextDeleteGlobalIndex = PoolEntity.m_Validation.m_Generation;
            
            if( Free({static_cast<int>(m_DeleteMoveIndex)}, false) )
            {
                ComponentMgr.MovedGlobalEntity({ static_cast<int>(m_DeleteMoveIndex) }, PoolEntity );
            }

            m_DeleteMoveIndex = NextDeleteGlobalIndex;
        }

        //
        // Free pages that we are not using any more
        //
        if( m_Size < OldSize )
        {
            for (int i = 0; i < m_ComponentInfos.size(); ++i)
            {
                const auto& MyInfo          = *m_ComponentInfos[i];
                const auto  LastEntryPage   = getPageFromIndex( MyInfo, OldSize );
                if( auto Cur = getPageFromIndex(MyInfo, m_Size); Cur != LastEntryPage || m_Size == 0 )
                {
                    if( m_Size == 0 ) Cur--;
                    auto pRaw   = &m_pComponent[i][xecs::settings::virtual_page_size_v * (Cur + 1) ];
                    auto nPages = LastEntryPage - Cur;
                    assert(nPages > 0);
                    auto b      = VirtualFree(pRaw, nPages * static_cast<std::size_t>(xecs::settings::virtual_page_size_v), MEM_DECOMMIT);
                    assert(b);
                }
            }
        }

        // Update the current count
        m_CurrentCount = m_Size;
    }

    //-------------------------------------------------------------------------------------
    inline
    void instance::Delete( index Index ) noexcept
    {
        auto& Entity = getComponent<xecs::component::entity>(Index);
        assert(Entity.m_Validation.m_bZombie);
        Entity.m_Validation.m_Generation = m_DeleteGlobalIndex;
        m_DeleteGlobalIndex = Entity.m_GlobalInfoIndex;
    }

    //--------------------------------------------------------------------------------------------
    constexpr
    bool instance::isLastEntry( index Index ) const noexcept
    {
        return Index.m_Value == m_Size - 1;
    }

    //--------------------------------------------------------------------------------------------
    inline
    index instance::MoveInFromPool( index ToNewIndex, index FromIndexToMove, pool::instance& FromPool ) noexcept
    {
        int         iPoolFrom       = 0;
        int         iPoolTo         = 0;
        const int   PoolFromCount   = static_cast<int>(FromPool.m_ComponentInfos.size());
        const int   PoolToCount     = static_cast<int>(m_ComponentInfos.size());
        while( true )
        {
            if( FromPool.m_ComponentInfos[iPoolFrom] == m_ComponentInfos[iPoolTo] )
            {
                auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
                if( Info.m_pMoveFn )
                {
                    Info.m_pMoveFn
                    (
                        &m_pComponent[iPoolTo][ Info.m_Size * ToNewIndex.m_Value ]
                    ,   &FromPool.m_pComponent[iPoolFrom][ Info.m_Size * FromIndexToMove.m_Value ]
                    );
                }
                else
                {
                    std::memcpy
                    ( 
                        &m_pComponent[iPoolTo][ Info.m_Size * ToNewIndex.m_Value ]
                    ,   &FromPool.m_pComponent[iPoolFrom][ Info.m_Size * FromIndexToMove.m_Value ]
                    ,   Info.m_Size
                    );
                }
                iPoolFrom++;
                iPoolTo++;
                if ( iPoolFrom >= PoolFromCount || iPoolTo >= PoolToCount )
                    break;
            }
            else if(FromPool.m_ComponentInfos[iPoolFrom]->m_Guid.m_Value < m_ComponentInfos[iPoolTo]->m_Guid.m_Value )
            {
                auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
                if( Info.m_pDestructFn ) Info.m_pDestructFn( &FromPool.m_pComponent[iPoolFrom][ Info.m_Size * FromIndexToMove.m_Value ] );

                iPoolFrom++;
                if( iPoolFrom >= PoolFromCount )
                    break;
            }
            else
            {
                // This is already constructed so there is nothing for me to do
                iPoolTo++;
                if( iPoolTo >= PoolToCount )
                    break;
            }
        }

        //
        // Destruct any pending component
        //
        while (iPoolFrom < PoolFromCount)
        {
            auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
            if (Info.m_pDestructFn) Info.m_pDestructFn(&FromPool.m_pComponent[iPoolFrom][ Info.m_Size * FromIndexToMove.m_Value ]);
            iPoolFrom++;
        }

        //
        // Put the deleted entity into the move deleted linklist
        //
        {
            auto& Entity = FromPool.getComponent<xecs::component::entity>(FromIndexToMove);
            Entity.m_Validation.m_bZombie    = true;
            Entity.m_Validation.m_Generation = FromPool.m_DeleteMoveIndex;
            FromPool.m_DeleteMoveIndex       = FromIndexToMove.m_Value;
        }

        return ToNewIndex;
    }

    //--------------------------------------------------------------------------------------------
    inline
    void instance::CopyEntity 
    ( index             ToNewIndex
    , index             FromIndex
    , pool::instance&   FromPool            // Pull must be 100% compatible
    ) noexcept
    {
    /*
        for( int i=1; i<m_ComponentInfos.size(); ++i )
        {
            auto& Info = *m_ComponentInfos[i];
            xassert(Info.m_BitID == FromPool.m_ComponentInfos[i]->m_BitID );

            if(Info.m_pCopyFn ) Info.m_pCopyFn( m_pComponent[i] + Info.m_Size * ToNewIndex.m_Value, FromPool.m_pComponent[i] + Info.m_Size * FromIndex.m_Value );
            else                memcpy(m_pComponent[i] + Info.m_Size * ToNewIndex.m_Value, FromPool.m_pComponent[i] + Info.m_Size * FromIndex.m_Value, Info.m_Size );
        }
        */
        int         iPoolFrom     = 1;
        int         iPoolTo       = 1;
        const int   PoolFromCount = static_cast<int>(FromPool.m_ComponentInfos.size());
        const int   PoolToCount   = static_cast<int>(m_ComponentInfos.size());

        // Either pool may hold nothing but the mandatory entity component (index 0) - e.g. a
        // bare entity turned into a "regular" (childless) prefab instance archetype - in which
        // case there is nothing to copy and the loop below must never dereference index 1.
        if (iPoolFrom >= PoolFromCount || iPoolTo >= PoolToCount)
            return;

        while (true)
        {
            if (FromPool.m_ComponentInfos[iPoolFrom] == m_ComponentInfos[iPoolTo])
            {
                auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
                if (Info.m_pCopyFn)
                {
                    Info.m_pCopyFn
                    (
                        &m_pComponent[iPoolTo][Info.m_Size * ToNewIndex.m_Value]
                        , &FromPool.m_pComponent[iPoolFrom][Info.m_Size * FromIndex.m_Value]
                    );
                }
                else
                {
                    std::memcpy
                    (
                          &m_pComponent[iPoolTo][Info.m_Size * ToNewIndex.m_Value]
                        , &FromPool.m_pComponent[iPoolFrom][Info.m_Size * FromIndex.m_Value]
                        , Info.m_Size
                    );
                }
                iPoolFrom++;
                iPoolTo++;
                if (iPoolFrom >= PoolFromCount || iPoolTo >= PoolToCount)
                    break;
            }
            else if (FromPool.m_ComponentInfos[iPoolFrom]->m_Guid.m_Value < m_ComponentInfos[iPoolTo]->m_Guid.m_Value)
            {
                iPoolFrom++;
                if (iPoolFrom >= PoolFromCount)
                    break;
            }
            else
            {
                // This is already constructed so there is nothing for me to do
                iPoolTo++;
                if (iPoolTo >= PoolToCount)
                    break;
            }
        }
    }
}