namespace xecs::component
{
    namespace details
    {
        //------------------------------------------------------------------------------

        void global_info_mgr::Initialize( int LastKnownSceneRanged ) noexcept
        {
            xassert(LastKnownSceneRanged >= 0);

            m_MaxSceneRange = std::max(xecs::component::ranges::runtime_range_count_v, LastKnownSceneRanged) + xecs::component::ranges::editor_scene_range_count_overflow_v;
            m_pGlobalInfo = reinterpret_cast<entity::global_info*>(VirtualAlloc
            (nullptr
                , m_MaxSceneRange
                * xecs::component::ranges::sub_ranges_count_per_range_v
                * static_cast<std::size_t>(xecs::component::ranges::sub_range_byte_count_v)
                , MEM_RESERVE
                , PAGE_NOACCESS
            ));
        }

        //------------------------------------------------------------------------------

        global_info_mgr::~global_info_mgr( void ) noexcept
        {
            // Free up all the allocated memory that we have if any...
            if (m_pGlobalInfo) VirtualFree(m_pGlobalInfo, 0, MEM_RELEASE);
        }

        //------------------------------------------------------------------------------

        entity::global_info& global_info_mgr::getEntityDetails( xecs::component::entity Entity ) noexcept
        {
            assert(Entity.isValid());
            auto& Entry = m_pGlobalInfo[Entity.m_GlobalInfoIndex];
            assert(Entry.m_Validation == Entity.m_Validation);
            return Entry;
        }

        //------------------------------------------------------------------------------
        const entity::global_info& global_info_mgr::getEntityDetails( xecs::component::entity Entity ) const noexcept
        {
            assert(Entity.isValid());
            auto& Entry = m_pGlobalInfo[Entity.m_GlobalInfoIndex];
            assert(Entry.m_Validation == Entity.m_Validation);
            return Entry;
        }

        //---------------------------------------------------------------------------
        void global_info_mgr::AppendNewSubrange   ( void ) noexcept
        {
            // If the user has not construct anything than we assume that we should do it ourselves
            if( m_pGlobalInfo == nullptr ) Initialize(0);

            m_LastRuntimeSubrange++;

            // We run out of runtime entities.... that must be over 100 million!!!
            xassert( m_LastRuntimeSubrange < xecs::component::ranges::runtime_range_count_v * xecs::component::ranges::sub_ranges_count_per_range_v );

            // Allocate a new sub range
            {
                void* pNewPtr = reinterpret_cast<std::byte*>(m_pGlobalInfo) + (m_LastRuntimeSubrange * xecs::component::ranges::sub_range_byte_count_v);
                auto p = VirtualAlloc
                ( pNewPtr
                , static_cast<std::size_t>(xecs::component::ranges::sub_range_byte_count_v )
                , MEM_COMMIT
                , PAGE_READWRITE
                );
                xassert(p == pNewPtr);

                const auto IndexOffset = m_LastRuntimeSubrange * xecs::component::ranges::sub_range_entity_count_v;
                for( int i=0; i< xecs::component::ranges::sub_range_entity_count_v; ++i )
                {
                    auto& E = reinterpret_cast<entity::global_info*>(pNewPtr)[i];
                    E.m_PoolIndex.m_Value       = 1 + i + IndexOffset;
                    E.m_Validation.m_Value      = 0;
                }

                reinterpret_cast<entity::global_info*>(pNewPtr)[xecs::component::ranges::sub_range_entity_count_v-1].m_PoolIndex.m_Value = -1;
                m_EmptyHead = IndexOffset;
            }
        }

        //---------------------------------------------------------------------------

        entity global_info_mgr::AllocInfo( pool::index PoolIndex, xecs::pool::instance& Pool ) noexcept
        {
            //
            // Map physical memory to virtual memory if we need to
            //
            if( m_EmptyHead == -1 ) AppendNewSubrange();

            //
            // Allocate one entity
            //
            auto  iEntityIndex = m_EmptyHead;
            auto& Entry = m_pGlobalInfo[iEntityIndex];
            m_EmptyHead = Entry.m_PoolIndex.m_Value;

            Entry.m_PoolIndex = PoolIndex;
            Entry.m_pPool     = &Pool;
            return
            {
                .m_GlobalInfoIndex = static_cast<std::uint32_t>(iEntityIndex)
            ,   .m_Validation = Entry.m_Validation
            };
        }

        //---------------------------------------------------------------------------

        void global_info_mgr::FreeInfo( std::uint32_t GlobalIndex, xecs::component::entity& SwappedEntity ) noexcept
        {
            auto& Entry = m_pGlobalInfo[GlobalIndex];
            m_pGlobalInfo[SwappedEntity.m_GlobalInfoIndex].m_PoolIndex = Entry.m_PoolIndex;

            Entry.m_Validation.m_Generation++;
            Entry.m_Validation.m_bZombie = false;
            Entry.m_PoolIndex.m_Value    = m_EmptyHead;
            m_EmptyHead = static_cast<int>(GlobalIndex);
        }

        //---------------------------------------------------------------------------

        void global_info_mgr::FreeInfo( std::uint32_t GlobalIndex ) noexcept
        {
            auto& Entry = m_pGlobalInfo[GlobalIndex];
            Entry.m_Validation.m_Generation++;
            Entry.m_Validation.m_bZombie = false;
            Entry.m_PoolIndex.m_Value    = m_EmptyHead;
            m_EmptyHead = static_cast<int>(GlobalIndex);
        }


    }

    //------------------------------------------------------------------------------
    // COMPONENT MGR
    //------------------------------------------------------------------------------

    //------------------------------------------------------------------------------

    mgr::mgr(void) noexcept
    {
        // Register Key system components
        RegisterComponent<xecs::component::entity>();
        RegisterComponent<xecs::component::share_as_data_exclusive_tag>();
        RegisterComponent<xecs::component::ref_count>();
        RegisterComponent<xecs::component::share_filter>();
        RegisterComponent<xecs::component::parent>();
        RegisterComponent<xecs::component::children>();
        RegisterComponent<xecs::prefab::tag>();
        RegisterComponent<xecs::prefab::root>();
    }

    //------------------------------------------------------------------------------

    template< typename T_COMPONENT >
    requires (xecs::component::type::is_valid_v<T_COMPONENT>)
    void mgr::RegisterComponent(void) noexcept
    {
        assert( s_isLocked == false );
        if (component::type::info_v<T_COMPONENT>.m_BitID == type::info::invalid_bit_id_v)
        {
            if constexpr( component::type::info_v<T_COMPONENT>.m_TypeID == xecs::component::type::id::SHARE )
            {
                T_COMPONENT X{};
                component::type::info_v<T_COMPONENT>.m_DefaultShareKey = component::type::info_v<T_COMPONENT>.m_pComputeKeyFn(reinterpret_cast<const std::byte*>(&X));
            }

            // Put there an invalid bitUD that indicates that we are waiting to be assign the right ID
            component::type::info_v<T_COMPONENT>.m_BitID = type::info::invalid_bit_id_v-1;

            s_BitsToInfo[s_nTypes++] = &component::type::info_v<T_COMPONENT>;
        }
    }

    //---------------------------------------------------------------------------

    const entity::global_info& mgr::getEntityDetails( entity Entity ) const noexcept
    {
        return m_GlobalEntityInfos.getEntityDetails(Entity);
    }

    //---------------------------------------------------------------------------

    void mgr::DeleteGlobalEntity( std::uint32_t GlobalIndex, xecs::component::entity& SwappedEntity ) noexcept
    {
        m_GlobalEntityInfos.FreeInfo(GlobalIndex, SwappedEntity);
    }

    //---------------------------------------------------------------------------

    void mgr::DeleteGlobalEntity(std::uint32_t GlobalIndex ) noexcept
    {
        m_GlobalEntityInfos.FreeInfo(GlobalIndex);
    }

    //---------------------------------------------------------------------------

    void mgr::MovedGlobalEntity( xecs::pool::index PoolIndex, xecs::component::entity& SwappedEntity ) noexcept
    {
        m_GlobalEntityInfos.m_pGlobalInfo[SwappedEntity.m_GlobalInfoIndex].m_PoolIndex = PoolIndex;
    }

    //---------------------------------------------------------------------------

    entity mgr::AllocNewEntity( pool::index PoolIndex, xecs::archetype::instance& Archetype, xecs::pool::instance& Pool) noexcept
    {
        return m_GlobalEntityInfos.AllocInfo(PoolIndex, Pool );
    }

    //---------------------------------------------------------------------------

    inline
    void mgr::LockComponentTypes( void ) noexcept
    {
        if(s_isLocked) return;
        s_isLocked = true;

        //
        // Short the final list of component types infos 
        //
        std::sort
        ( s_BitsToInfo.begin()
        , s_BitsToInfo.begin() + s_nTypes
        , xecs::component::type::details::CompareTypeInfos
        );

        //
        // Officially register each of the components
        //
        for( int i=0; i<s_nTypes; ++i )
        {
            // Everyone should be waiting for us to assign their BitID
            assert( s_BitsToInfo[i]->m_BitID == (type::info::invalid_bit_id_v - 1) );

            // Ok we just officially assing their ID now in shorted order
            s_BitsToInfo[i]->m_BitID = i;

            // Add to the info map
            m_ComponentInfoMap.emplace( std::pair{ s_BitsToInfo[i]->m_Guid, s_BitsToInfo[i] } );

            // Now we are ready to assign the IDs...
            switch( s_BitsToInfo[i]->m_TypeID )
            {
                case xecs::component::type::id::DATA:           s_DataBits.setBit(s_BitsToInfo[i]->m_BitID);
                                                                break;
                case xecs::component::type::id::TAG:            s_TagsBits.setBit(s_BitsToInfo[i]->m_BitID);
                                                                if( s_BitsToInfo[i]->m_bExclusiveTag )
                                                                    s_ExclusiveTagsBits.setBit(s_BitsToInfo[i]->m_BitID);
                                                                break;
                case xecs::component::type::id::SHARE:          s_ShareBits.setBit(s_BitsToInfo[i]->m_BitID);
                                                                break;
                default: assert(false);
            }
        }
    }

    //---------------------------------------------------------------------------

    const xecs::component::type::info* mgr::findComponentTypeInfo( xecs::component::type::guid Guid ) noexcept
    {
        auto It = m_ComponentInfoMap.find(Guid);
        if( It == m_ComponentInfoMap.end() ) return nullptr;
        return It->second;
    }

    //---------------------------------------------------------------------------
    inline 
    void mgr::resetRegistrations( void ) noexcept
    {
        // Reset all known components
        for( int i=0; i< s_nTypes; ++i)
        {
            s_BitsToInfo[i]->m_BitID = xecs::component::type::info::invalid_bit_id_v;
        }

        s_ShareBits         = xecs::tools::bits{};
        s_DataBits          = xecs::tools::bits{};
        s_TagsBits          = xecs::tools::bits{};
        s_ExclusiveTagsBits = xecs::tools::bits{};

        s_UniqueID          = 0;
        s_BitsToInfo        = bits_to_info_array{};
        s_nTypes            = 0;
        s_isLocked          = false;
    }
}