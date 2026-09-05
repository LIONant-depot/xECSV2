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
        // component types), this recursively clones Source's (and, via xecs::component::children, its
        // whole live descendant subtree's) CURRENT components into a brand new prefab group -
        // "Scene-Prefab" in this engine's own terminology, since it's persisted and instanced exactly
        // like a small scene fragment. A childless Source naturally produces a 1-member group, so
        // there is no separate single-entity code path any more. PrefabGuid must already be minted and
        // registered with the asset library (e.g. via library_mgr::NewAsset) by the caller - this
        // function only owns the ECS-side content (Descriptor.txt/Entity.txt via Save), not the
        // asset's info.txt/folder placement, so a prefab created this way is a real, browsable,
        // properly-filed asset from the start rather than an orphaned file only this session's
        // m_PrefabList knows about. Force-adds prefab::tag (every member) / prefab::root (Source only)
        // exactly like CreatePrefab<T...> does, and registers the ROOT into m_PrefabList the same way,
        // so the existing CreatePrefabInstance(Count, PrefabGuid, ...) overload works on the result
        // unmodified - it already recurses through children on its own (see CreatePrefabInstance's own
        // "Scene-Prefab" branch).
        inline
        guid        CreatePrefabFromEntity ( xecs::component::entity Source, guid PrefabGuid ) noexcept;

        // Loads (if not already resident in m_PrefabList) every member of a prefab group from disk
        // into live entities, wiring parent/children and remapping any intra-group reference back up
        // - the read-side counterpart of CreatePrefabFromEntity, needed so instancing/reverting a
        // prefab works even in a freshly-opened editor session. m_PrefabList still only ever ends up
        // holding the group's ROOT entity - every other member stays reachable transitively via
        // xecs::component::children from there, exactly as CreatePrefabInstance's own recursive walk
        // already expects.
        inline
        xerr        EnsureLoaded          ( guid PrefabGuid ) noexcept;

        // Persists every member of a resident prefab group (found via m_PrefabList/m_PrefabGroups -
        // call EnsureLoaded first if it might not be resident) to disk, as one atomic file (a prefab
        // is always loaded/saved wholesale, unlike a Scene, so there's no per-entity incremental-IO
        // concern to design around here).
        inline
        xerr        Save                  ( guid PrefabGuid ) noexcept;

        // mgr::CreatePrefabInstance(Entity,Remap,Parent,isVariant)'s new nested-prefab-instance
        // branch: when the entity being cloned during ordinary instancing itself carries
        // xecs::editor::prefab_instance (it's a member of an outer group that is ITSELF an instance
        // of a different, inner prefab), route through the full instantiation process for that inner
        // prefab instead of a plain component-copy - this is what makes prefab composition/variants
        // recursive. Declared here (not just inline in the .cpp-equivalent) since
        // CreatePrefabInstance's own early-return branch needs to call it.
        inline
        xecs::component::entity CreateNestedPrefabInstance( xecs::component::entity Entity, std::unordered_map<std::uint64_t, xecs::component::entity>& Remap, xecs::component::entity ParentEntity, bool isVariant ) noexcept;

        // CreatePrefabFromEntity's recursive clone step - see its own definition's comment for the
        // full rationale (clone, not in-place mutation; local-id-before-recursion ordering; the
        // nested-prefab-instance-is-a-leaf exclusion).
        inline
        xecs::component::entity CloneEntityIntoPrefabGroup( xecs::component::entity Source, group_bookkeeping& Group, bool bIsRoot ) noexcept;

        xecs::game_mgr::instance&                                   m_GameMgr;
        std::unordered_map<std::uint64_t,xecs::component::entity>   m_PrefabList;

        // Save/EnsureLoaded's own local-id bookkeeping for a resident prefab group's members - purely
        // additive alongside m_PrefabList (which keeps its exact original shape/role, storing only the
        // root), so every existing m_PrefabList reader keeps compiling unmodified. See
        // xecs::prefab::group_bookkeeping's own comment.
        std::unordered_map<std::uint64_t,group_bookkeeping>         m_PrefabGroups;

        // The project's root path, same convention as xecs::scene::mgr::m_ProjectPath /
        // xecs::level::mgr::m_ProjectPath.
        std::wstring                                                m_ProjectPath;
    };
}