namespace xecs::prefab
{
/*
    xecs::component::entity CloneEntity( xecs::component::entity Entity ) noexcept
    {
        std::unordered_map<std::uint64_t, xecs::component::entity> RemapTable;

        xecs::component::entity NewEntity;

        auto& EntityInfo = m_GameMgr.m_ComponentMgr.getEntityDetails(Entity);

        EntityInfo.m_pPool->m_pFamily->CloneEntity(Entity);

        getOrCreatePoolFamily(xecs::pool::family & OtherArchetypeFamily) noexcept

        GameMgr.m_ArchetypeMgr.AddToStructuralPendingList(*EntityInfo.m_pPool);
        AppendEntities(1, GameMgr.m_ArchetypeMgr, [&](xecs::pool::instance& Pool, xecs::pool::index Index, int) noexcept
        {
            NewEntity = GameMgr.m_ComponentMgr.AllocInfo(Index, Pool)

            // Copy the entity raw
            Pool.CopyEntity( Index, EntityInfo.m_PoolIndex, EntityInfo.m_pPool );


        });
    }


    xecs::component::entity mgr::CreatePrefabInstance( xecs::prefab::guid PrefabGuid ) noexcept
    {
        // Failed to get the prefab
        if( auto Pair = m_PrefabList.find(PrefabGuid.m_Instance.m_Value ); Pair == m_PrefabList.end() )
            return {};

        auto& EntityInfo = m_GameMgr.m_ComponentMgr.getEntityDetails(Pair.second );
        auto& Archetype  = *EntityInfo.m_pPool->m_pArchetype;

        // Make sure that we have the right thing...
        xassert( Archetype.getComponentBits().getBit( xecs::prefab::master ) && Archetype.getComponentBits().getBit(xecs::prefab::tag) );

        return CloneEntity(Pair.second)
    }

    */
} // end of xecs::prefab
