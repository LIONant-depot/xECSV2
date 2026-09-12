namespace xecs::level
{
    namespace details
    {
        //-----------------------------------------------------------------------------------------
        // On-disk paths - same real GUID-sharded Descriptors/Level/<b0>/<b1>/<guid>.desc/ convention
        // xecs::scene::mgr uses (see xecs_scene_inline.h's identical helper for the full rationale).
        //-----------------------------------------------------------------------------------------
        inline std::wstring LevelFolder( mgr& Mgr, guid LevelGuid ) noexcept
        {
            const auto Value = LevelGuid.m_Instance.m_Value;
            const auto Byte0 = std::format( L"{:02X}", static_cast<std::uint8_t>( Value       & 0xFF) );
            const auto Byte1 = std::format( L"{:02X}", static_cast<std::uint8_t>((Value >> 8) & 0xFF) );
            return Mgr.m_ProjectPath + L"/Descriptors/Level/" + Byte0 + L"/" + Byte1 + L"/" + std::format(L"{:X}", Value) + L".desc";
        }

        inline std::wstring DescriptorPath( mgr& Mgr, guid LevelGuid ) noexcept
        {
            return LevelFolder(Mgr, LevelGuid) + L"/Descriptor.txt";
        }
    }

    //-----------------------------------------------------------------------------------------------

    instance* mgr::Find( guid LevelGuid ) noexcept
    {
        for( auto& Up : m_LevelInstances )
            if( Up->m_Guid == LevelGuid )
                return Up.get();
        return nullptr;
    }

    //-----------------------------------------------------------------------------------------------

    instance& mgr::FindOrCreate( guid LevelGuid ) noexcept
    {
        if( auto* pLevel = Find(LevelGuid) ) return *pLevel;

        auto Up   = std::make_unique<instance>();
        Up->m_Guid = LevelGuid;
        auto& Ref  = *Up;
        m_LevelInstances.push_back( std::move(Up) );
        return Ref;
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::Load( guid LevelGuid ) noexcept
    {
        // Find-first: refuse unknown GUIDs. FindOrCreate alone would mint an empty in-memory
        // Level and Load used to treat a missing descriptor as success (0 scenes) - fine for
        // raw tooling, confusing for OpenLevel/CLI demos. Only register after the descriptor exists.
        const auto Path = details::DescriptorPath(*this, LevelGuid);
        std::error_code Ec;
        if( false == std::filesystem::exists(Path, Ec) || Ec )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Level::mgr::Load: level descriptor not found">();

        auto& Level = FindOrCreate(LevelGuid);

        descriptor                   Descriptor;
        xproperty::settings::context Context;
        if( auto Err = Descriptor.Serialize( true, Path, Context ); Err )
            return Err;

        Level.m_Scenes = std::move(Descriptor.m_Scenes);
        return {};
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::Save( guid LevelGuid ) noexcept
    {
        auto* pLevel = Find(LevelGuid);
        if( pLevel == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Level::mgr::Save: level is not registered - call FindOrCreate first">();

        std::error_code Ec;
        std::filesystem::create_directories( std::filesystem::path(details::LevelFolder(*this, LevelGuid)), Ec );

        descriptor Descriptor;
        Descriptor.m_Scenes = pLevel->m_Scenes;

        xproperty::settings::context Context;
        return Descriptor.Serialize( false, details::DescriptorPath(*this, LevelGuid), Context );
    }

    //-----------------------------------------------------------------------------------------------

    xerr mgr::Activate( guid LevelGuid ) noexcept
    {
        auto* pLevel = Find(LevelGuid);
        if( pLevel == nullptr )
            return xerr::create<xecs::game_mgr::state::FAILURE, "Level::mgr::Activate: level is not registered - call Load first">();

        for( auto& SceneGuid : pLevel->m_Scenes )
        {
            if( auto Err = m_GameMgr.m_SceneMgr.RequestLoad(SceneGuid); Err )
                return Err;
        }
        return {};
    }
}
