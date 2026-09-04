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

        xecs::game_mgr::instance&                                   m_GameMgr;
        std::unordered_map<std::uint64_t,xecs::component::entity>   m_PrefabList;
    };
}