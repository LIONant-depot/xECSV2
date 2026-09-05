namespace xecs::prefab
{
    struct mgr
    {
        mgr(xecs::game_mgr::instance& GameMgr ) : m_GameMgr{ GameMgr }{}

        template
        < typename T_ADD_TUPLE = std::tuple<>
        , typename T_SUB_TUPLE = std::tuple<>
        , typename T_CALLBACK  = xecs::tools::empty_lambda
        > requires
        (    ( std::is_same_v< std::tuple<>, T_ADD_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_ADD_TUPLE> )
          && ( std::is_same_v< std::tuple<>, T_SUB_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_SUB_TUPLE> )
          && xecs::tools::assert_standard_function_v<T_CALLBACK>
          && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        bool CreatePrefabInstance( int Count, xecs::prefab::guid PrefabGuid, T_CALLBACK&& Callback, bool bRemoveRoot = true, bool isVariant = false ) noexcept;

        template
        < typename T_ADD_TUPLE = std::tuple<>
        , typename T_SUB_TUPLE = std::tuple<>
        , typename T_CALLBACK  = xecs::tools::empty_lambda
        > requires
        (    ( std::is_same_v< std::tuple<>, T_ADD_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_ADD_TUPLE> )
          && ( std::is_same_v< std::tuple<>, T_SUB_TUPLE> || xecs::tools::assert_valid_tuple_components_v<T_SUB_TUPLE> )
          && xecs::tools::assert_standard_function_v<T_CALLBACK>
          && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        xecs::component::entity CreatePrefabInstance( int Count, xecs::component::entity PrefabEntity, T_CALLBACK&& Callback, bool bRemoveRoot = true, bool isVariant = false ) noexcept;

        __inline
        xecs::component::entity CreatePrefabInstance( xecs::component::entity PrefabEntity, std::unordered_map< std::uint64_t, xecs::component::entity >& Remap, xecs::component::entity ParentEntity, bool isVariant ) noexcept;

        // Runtime, GUID-driven prefab creation - unlike CreatePrefab<T...>() (which needs compile-time
        // component types), this snapshots Source's CURRENT live components into a new prefab root
        // entity. PrefabGuid must already be minted and registered with the asset library (e.g. via
        // library_mgr::NewAsset) by the caller - this function only owns the ECS-side content
        // (Descriptor.txt/Entity.txt via Save), not the asset's info.txt/folder placement, so a
        // prefab created this way is a real, browsable, properly-filed asset from the start rather
        // than an orphaned file only this session's m_PrefabList knows about. Force-adds
        // prefab::tag/prefab::root exactly like CreatePrefab<T...> does, and registers into
        // m_PrefabList the same way, so the existing CreatePrefabInstance(Count, PrefabGuid, ...)
        // overload works on the result unmodified.
        inline
        guid        CreatePrefabFromEntity ( xecs::component::entity Source, guid PrefabGuid ) noexcept;

        // Loads (if not already resident in m_PrefabList) a prefab's root entity from disk into a
        // live entity, and registers it - the read-side counterpart of CreatePrefabFromEntity, needed
        // so instancing/reverting a prefab works even in a freshly-opened editor session.
        inline
        xerr        EnsureLoaded          ( guid PrefabGuid ) noexcept;

        // Persists a prefab's root entity (found via m_PrefabList - call EnsureLoaded first if it
        // might not be resident) to disk.
        inline
        xerr        Save                  ( guid PrefabGuid ) noexcept;

        xecs::game_mgr::instance&                                   m_GameMgr;
        std::unordered_map<std::uint64_t,xecs::component::entity>   m_PrefabList;

        // The project's root path, same convention as xecs::scene::mgr::m_ProjectPath /
        // xecs::level::mgr::m_ProjectPath.
        std::wstring                                                m_ProjectPath;
    };
}