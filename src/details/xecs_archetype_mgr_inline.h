namespace xecs::archetype
{
    //-------------------------------------------------------------------------------------
    // ARCHETYPE MANAGER
    //-------------------------------------------------------------------------------------

    //-------------------------------------------------------------------------------------

    mgr::mgr( xecs::game_mgr::instance& GameMgr ) noexcept
    : m_GameMgr{ GameMgr }
    {
    }

    //-------------------------------------------------------------------------------------

    archetype::instance&
mgr::getOrCreateArchetype
    ( const xecs::tools::bits& ComponentBits
    ) noexcept
    {
        xecs::archetype::guid ArchetypeGuid{ ComponentBits.GenerateUniqueID() };

        // Make sure the entity is part of the list at this point
        assert(ComponentBits.getBit(xecs::component::type::info_v<xecs::component::entity>.m_BitID) );

        // Return the archetype
        if( auto I = m_ArchetypeMap.find(ArchetypeGuid); I != m_ArchetypeMap.end() )
            return *I->second;

        //
        // Create Archetype...
        //
        return CreateArchetype( ArchetypeGuid, ComponentBits );
    }

    //-------------------------------------------------------------------------------------

    void mgr::UpdateStructuralChanges( void ) noexcept
    {
        //
        // Update all the pools
        //
        for (auto p = m_pPoolStructuralPending; p != reinterpret_cast<xecs::pool::instance*>(end_structural_changes_v); )
        {
            auto pNext = p->m_pPendingStructuralChanges;
            p->UpdateStructuralChanges(m_GameMgr.m_ComponentMgr);
            p->m_pPendingStructuralChanges = nullptr;
            p = pNext;
        }
        m_pPoolStructuralPending = reinterpret_cast<xecs::pool::instance*>(end_structural_changes_v);

        //
        // Update all the archetypes
        //
        for( auto p = m_pArchetypeStrututalPending; p != reinterpret_cast<xecs::archetype::instance*>(end_structural_changes_v); )
        {
            auto pNext = p->m_pPendingStructuralChanges;
            p->_UpdateStructuralChanges();
            p->m_pPendingStructuralChanges = nullptr;
            p = pNext;
        }
        m_pArchetypeStrututalPending = reinterpret_cast<xecs::archetype::instance*>(end_structural_changes_v);
    }

    //-------------------------------------------------------------------------------------

    void mgr::AddToStructuralPendingList( instance& Archetype ) noexcept
    {
        if( nullptr == Archetype.m_pPendingStructuralChanges )
        {
            Archetype.m_pPendingStructuralChanges = m_pArchetypeStrututalPending;
            m_pArchetypeStrututalPending = &Archetype;
        }
    }

    //-------------------------------------------------------------------------------------

    void mgr::AddToStructuralPendingList( pool::instance& Pool ) noexcept
    {
        if( nullptr == Pool.m_pPendingStructuralChanges )
        {
            Pool.m_pPendingStructuralChanges = m_pPoolStructuralPending;
            m_pPoolStructuralPending = &Pool;
        }
    }

    //-------------------------------------------------------------------------------------
    __inline
    archetype::instance* mgr::findArchetype( xecs::archetype::guid Guid ) noexcept
    {
        auto It = m_ArchetypeMap.find( Guid );
        return It == m_ArchetypeMap.end() ? nullptr : It->second;
    }

    //-------------------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
    )
    xecs::component::entity
mgr::AddOrRemoveComponents
    ( xecs::component::entity       Entity
    , const xecs::tools::bits&      Add
    , const xecs::tools::bits&      Sub
    , T_FUNCTION&&                  Function 
    ) noexcept
    {
        // Make sure that the entity is not in the list of things to delete
        assert( !Sub.getBit( 0 ) );

        //
        // Get the entry and make sure we are dealing with a valid entity
        //
        assert(Entity.isZombie() == false);
        auto& Entry = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        assert(Entry.m_Validation.m_bZombie == false);

        //
        // Add or remove Components
        //
        auto Bits = Entry.m_pPool->m_pArchetype->m_ComponentBits;
        for( int i=0; i<Bits.m_Bits.size(); ++i )
        {
            Bits.m_Bits[i] = (Bits.m_Bits[i] & ~Sub.m_Bits[i]) | Add.m_Bits[i];
        }

        //
        // Deal with events
        //
        {
            auto& GlobalEntity = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            if (GlobalEntity.m_Validation != Entity.m_Validation) return { 0xffffffffffffffff };

            auto& FromArchetype = *GlobalEntity.m_pPool->m_pArchetype;
            if (FromArchetype.m_Events.m_OnEntityMovedOut.m_Delegates.size())
            {
                auto& Pool       = *GlobalEntity.m_pPool;
                auto& PoolEntity = Pool.getComponent<xecs::component::entity>(GlobalEntity.m_PoolIndex);
                GlobalEntity.m_pPool->m_pArchetype->m_Events.m_OnEntityMovedOut.NotifyAll(PoolEntity);
                Entity = PoolEntity;
                if (GlobalEntity.m_Validation.m_bZombie) return { 0xffffffffffffffff };
                if (GlobalEntity.m_Validation != Entity.m_Validation) return { 0xffffffffffffffff };
            }
        }

        //
        // Search for the right archetype
        //
        const auto                  NewArchetypeGuid = xecs::archetype::guid{ Bits.GenerateUniqueID() };
        xecs::archetype::instance*  pArchetype       = findArchetype(NewArchetypeGuid);

        if( pArchetype == nullptr )
        {
            // Fail to find the archetype which means that we must build one
            pArchetype = &CreateArchetype(NewArchetypeGuid, Bits);
        }

        //
        // Move the Entity
        //
        auto& PoolFamily = pArchetype->getOrCreatePoolFamilyFromDifferentArchetype(Entity);

        if constexpr (std::is_same_v<T_FUNCTION, xecs::tools::empty_lambda >)
            return pArchetype->_MoveInEntity(Entity, PoolFamily );
        else
            return pArchetype->_MoveInEntity(Entity, PoolFamily, std::forward<T_FUNCTION&&>(Function));
    }

    //-------------------------------------------------------------------------------------

    archetype::instance& mgr::CreateArchetype( archetype::guid NewArchetypeGuid, const tools::bits& Bits ) noexcept
    {
        //
        // Create Archetype...
        //
        auto  UniqueArchetype = std::make_unique<archetype::instance>(*this);
        auto& Archetype       = *UniqueArchetype;

        m_ArchetypeMap.emplace(NewArchetypeGuid, &Archetype);

        Archetype.Initialize( NewArchetypeGuid, Bits );

        m_lArchetype.push_back(std::move(UniqueArchetype));
        m_lArchetypeBits.push_back({ Bits, Archetype.m_ExclusiveTagsBits });

        // Notify anyone interested
        m_Events.m_OnNewArchetype.NotifyAll(Archetype);

        return Archetype;
    }
}