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
    // The one, physical definition of mgr::s_Registry lives in details/xecs_game_mgr.cpp, NOT here -
    // this file is a `_inline.h` header, included by every separate translation unit that pulls in
    // xecs.h (xecs.cpp, E29's own .cpp, smoke_test.cpp, ...), so a plain out-of-line static
    // definition placed here would violate ODR (multiple definitions) the moment more than one such
    // TU links into the same binary - exactly the "each TU gets its own copy" bug class this whole
    // registry consolidation exists to eliminate, just reintroduced at the definition site instead
    // of the declaration. xecs_game_mgr.cpp is the one file guaranteed to be compiled exactly once
    // per binary (it's `#include`d directly into xecs.cpp itself, never compiled standalone).
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
    void mgr::RegisterComponent(xecs::plugin::token Owner) noexcept
    {
        assert( s_Registry.m_isLocked == false );
        if (component::type::info_v<T_COMPONENT>.m_BitID == type::info::invalid_bit_id_v)
        {
            if constexpr( component::type::info_v<T_COMPONENT>.m_TypeID == xecs::component::type::id::SHARE )
            {
                T_COMPONENT X{};
                component::type::info_v<T_COMPONENT>.m_DefaultShareKey = component::type::info_v<T_COMPONENT>.m_pComputeKeyFn(reinterpret_cast<const std::byte*>(&X));
            }

            // Put there an invalid bitUD that indicates that we are waiting to be assign the right ID
            component::type::info_v<T_COMPONENT>.m_BitID = type::info::invalid_bit_id_v-1;

            s_Registry.m_Owner[s_Registry.m_nTypes]      = Owner;
            s_Registry.m_BitsToInfo[s_Registry.m_nTypes++] = &component::type::info_v<T_COMPONENT>;
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
        if(s_Registry.m_isLocked) return;
        s_Registry.m_isLocked = true;

        //
        // Short the final list of component types infos - m_Owner must move in lockstep with
        // m_BitsToInfo (RegisterComponent recorded both at the same pre-sort index), so this sorts
        // a temporary array of paired {info*, owner} entries rather than m_BitsToInfo alone, which
        // would otherwise silently desync ownership from the type it actually belongs to.
        //
        struct sort_entry { const xecs::component::type::info* m_pInfo; xecs::plugin::token m_Owner; };
        std::array<sort_entry, xecs::settings::max_component_types_v> SortArray;
        for( int i = 0; i < s_Registry.m_nTypes; ++i )
            SortArray[i] = { s_Registry.m_BitsToInfo[i], s_Registry.m_Owner[i] };

        std::sort
        ( SortArray.begin()
        , SortArray.begin() + s_Registry.m_nTypes
        , []( const sort_entry& A, const sort_entry& B ) noexcept
          { return xecs::component::type::details::CompareTypeInfos(A.m_pInfo, B.m_pInfo); }
        );

        for( int i = 0; i < s_Registry.m_nTypes; ++i )
        {
            s_Registry.m_BitsToInfo[i] = SortArray[i].m_pInfo;
            s_Registry.m_Owner[i]      = SortArray[i].m_Owner;
        }

        //
        // Officially register each of the components
        //
        for( int i=0; i<s_Registry.m_nTypes; ++i )
        {
            // Everyone should be waiting for us to assign their BitID
            assert( s_Registry.m_BitsToInfo[i]->m_BitID == (type::info::invalid_bit_id_v - 1) );

            // Ok we just officially assing their ID now in shorted order
            s_Registry.m_BitsToInfo[i]->m_BitID = i;

            // Add to the info map - insert_or_assign, not emplace: a GUID is a compile-time
            // constant, identical across every reload generation, so emplace's own "no-op if the
            // key already exists" behavior would silently keep a STALE pointer from a previous
            // generation around if resetRegistrations() were ever incomplete again (exactly the bug
            // this map's own missing .clear() caused - see resetRegistrations' own comment). This is
            // the belt to that fix's suspenders, not a substitute for it.
            s_Registry.m_ComponentInfoMap.insert_or_assign( s_Registry.m_BitsToInfo[i]->m_Guid, s_Registry.m_BitsToInfo[i] );

            // Now we are ready to assign the IDs...
            switch( s_Registry.m_BitsToInfo[i]->m_TypeID )
            {
                case xecs::component::type::id::DATA:           s_Registry.m_DataBits.setBit(s_Registry.m_BitsToInfo[i]->m_BitID);
                                                                break;
                case xecs::component::type::id::TAG:            s_Registry.m_TagsBits.setBit(s_Registry.m_BitsToInfo[i]->m_BitID);
                                                                if( s_Registry.m_BitsToInfo[i]->m_bExclusiveTag )
                                                                    s_Registry.m_ExclusiveTagsBits.setBit(s_Registry.m_BitsToInfo[i]->m_BitID);
                                                                break;
                case xecs::component::type::id::SHARE:          s_Registry.m_ShareBits.setBit(s_Registry.m_BitsToInfo[i]->m_BitID);
                                                                break;
                default: assert(false);
            }
        }
    }

    //---------------------------------------------------------------------------

    const xecs::component::type::info* mgr::findComponentTypeInfo( xecs::component::type::guid Guid ) noexcept
    {
        auto It = s_Registry.m_ComponentInfoMap.find(Guid);
        if( It == s_Registry.m_ComponentInfoMap.end() ) return nullptr;
        return It->second;
    }

    //---------------------------------------------------------------------------
    inline
    void mgr::EnsureLocalBitID( const xecs::component::type::info& Info ) noexcept
    {
        // Fast path: local BitID still names this GUID in the locked registry (no hash).
        if( Info.m_BitID != type::info::invalid_bit_id_v
         && Info.m_BitID < s_Registry.m_nTypes
         && s_Registry.m_BitsToInfo[Info.m_BitID]->m_Guid == Info.m_Guid )
            return;

        // Miss / unset / stale after reload: one map lookup, write back into THIS module's info_v.
        if( auto* pReg = findComponentTypeInfo(Info.m_Guid) )
            Info.m_BitID = pReg->m_BitID;
    }

    //---------------------------------------------------------------------------
    inline
    void mgr::resetRegistrations( void ) noexcept
    {
        // Reset all known components
        for( int i=0; i< s_Registry.m_nTypes; ++i)
        {
            s_Registry.m_BitsToInfo[i]->m_BitID = xecs::component::type::info::invalid_bit_id_v;
        }

        s_Registry.m_ShareBits         = xecs::tools::bits{};
        s_Registry.m_DataBits          = xecs::tools::bits{};
        s_Registry.m_TagsBits          = xecs::tools::bits{};
        s_Registry.m_ExclusiveTagsBits = xecs::tools::bits{};

        s_Registry.m_UniqueID          = 0;
        s_Registry.m_BitsToInfo        = type::registry::bits_to_info_array{};
        s_Registry.m_Owner             = type::registry::bits_to_owner_array{};
        s_Registry.m_nTypes            = 0;
        s_Registry.m_isLocked          = false;

        // Was missing - left every GUID->info* entry for a plugin-owned type (e.g. a Game.dll
        // component) pointing at that generation's own info_v<T>, which lives inside the plugin
        // module about to be FreeLibrary'd. LockComponentTypes' own m_ComponentInfoMap.emplace(...)
        // is a no-op for a GUID already present (a compile-time constant, identical across every
        // reload generation) - so without this clear, the NEXT generation's registration silently
        // never overwrote the stale entry, and every later GUID lookup/iteration (findComponentTypeInfo,
        // or any UI walking the whole map, e.g. the "Add Component" list) read freed memory. Confirmed
        // live: a bad pInfo pointer after a few Game.dll reload cycles.
        s_Registry.m_ComponentInfoMap.clear();
    }

    //---------------------------------------------------------------------------

    void mgr::UnregisterPlugin( xecs::plugin::token Token ) noexcept
    {
        // The host itself is never "unregistered" as a plugin - it's the one owner that survives
        // every reload.
        xassert( !(Token == xecs::plugin::host_v) );

        // See this method's own declaration comment (xecs_component_mgr.h) for why a full reset is
        // the correct operation here, and why this assert is the honest stand-in for the narrower
        // "only detach what this token owns, leave everyone else's BitIDs untouched" behavior a
        // future multi-plugin scenario (Phase 8B) would need instead.
        for( int i = 0; i < s_Registry.m_nTypes; ++i )
        {
            xassert( s_Registry.m_Owner[i] == Token || s_Registry.m_Owner[i] == xecs::plugin::host_v );
        }

        resetRegistrations();
    }
}