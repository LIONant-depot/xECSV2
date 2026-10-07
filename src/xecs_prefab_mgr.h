namespace xecs::prefab
{
    struct baked;       // xecs_prefab_recipe_inline.h

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
        // pOutside (optional) receives the references the members held to entities outside the group (the prefab keeps them null); pMemberIds
        // (optional) each source entity's id in the prefab (keyed by the source's entity value).
        inline
        guid        CreatePrefabFromEntity ( xecs::component::entity Source, guid PrefabGuid, std::vector<outside_reference>* pOutside = nullptr, std::unordered_map<std::uint64_t, local_id>* pMemberIds = nullptr ) noexcept;

        // Loads (if not already resident in m_PrefabList) every member of a prefab group from disk
        // into live entities, wiring parent/children and remapping any intra-group reference back up
        // - the read-side counterpart of CreatePrefabFromEntity, needed so instancing/reverting a
        // prefab works even in a freshly-opened editor session. m_PrefabList still only ever ends up
        // holding the group's ROOT entity - every other member stays reachable transitively via
        // xecs::component::children from there, exactly as CreatePrefabInstance's own recursive walk
        // already expects. Reads the scene format, or the old one (Entity.txt), which the next Save
        // converts. A prefab that fails to load leaves nothing behind.
        inline
        xerr        EnsureLoaded          ( guid PrefabGuid ) noexcept;

        // Persists every member of a resident prefab group (found via m_PrefabList/m_PrefabGroups -
        // call EnsureLoaded first if it might not be resident) to disk, in the scene format: one entity
        // file per member, the descriptor (root, members, names), ComponentDeps.txt (a prefab is always
        // saved wholesale; the files of members that are gone, and an old-format Entity.txt, are removed).
        // Refuses, writing nothing, when a member references a live entity outside the prefab.
        // When a prefab is open in a Prefab Editor, that editor is its one writer (prefabs_plan.md 3.7): a Save of the prefab from somewhere else (Apply Overrides of an
        // instance in a Level, its undo) does not write the file - it writes the saved state to a new folder and hands that folder to the editor (m_pDeliver), which takes it as
        // a change of its document. Null: every Save writes the prefab's own folder. Set by the editor on its world (xECSEditor::SetPrefabSaveRedirect).
        struct save_redirect
        {
            void* m_pUser = nullptr;
            bool (*m_pTakes  )( void* pUser, guid Prefab ) noexcept = nullptr;                                  // true: another editor holds the prefab as its document
            bool (*m_pDeliver)( void* pUser, guid Prefab, const std::wstring& Folder ) noexcept = nullptr;      // the folder the saved state was written to (the receiver owns it from here); false: nobody took it, and nothing was written
        };

        inline
        xerr        Save                  ( guid PrefabGuid ) noexcept;

        // The same, to Folder instead of the prefab's own (a snapshot): the old-format file and the stray member files of the prefab's own folder are not cleaned (Folder is not it).
        // Like Save it drops every baked plan first (the template is not touched, but one prefab's plan holds what it nests, so a save invalidates them all).
        inline
        xerr        SaveTo                ( guid PrefabGuid, const std::wstring& Folder ) noexcept;

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
        // nested-prefab-instance-is-a-leaf exclusion). Called without pClones, it is the whole clone:
        // the references of the clones are then moved to the clones they point at, and the names the
        // sources have in their scenes are given to the clones.
        inline
        xecs::component::entity CloneEntityIntoPrefabGroup( xecs::component::entity Source, group_bookkeeping& Group, bool bIsRoot, std::unordered_map<std::uint64_t, xecs::component::entity>* pClones = nullptr ) noexcept;

        // The whole clone of Source's subtree into Group as members (not a root): the clones' references are moved to the clones they point at,
        // or to the template entity pKnown maps a live entity to (Apply: the instance's root and members), and are null otherwise (listed in
        // pOutside when given); the clones get their sources' names. pOutClones (optional) receives source -> clone.
        inline
        xecs::component::entity CloneSubtreeIntoPrefab( xecs::component::entity Source, group_bookkeeping& Group, bool bIsRoot
                                                      , const std::unordered_map<std::uint64_t, xecs::component::entity>* pKnown
                                                      , std::unordered_map<std::uint64_t, xecs::component::entity>*       pOutClones
                                                      , std::vector<outside_reference>*                                    pOutside ) noexcept;

        // The baked plan of a prefab (documentation/Editors/prefabs_plan.md 3.5; xecs_prefab_recipe_inline.h): every entity a spawn makes, the
        // nested recipes applied, where each member's references go. Made at the first need (EnsureLoaded first); nullptr for a prefab that is not
        // a scene-format group (one made in code with CreatePrefab<T...>). Valid until a prefab is saved or made (InvalidateBaked).
        inline
        baked*      getBaked              ( guid PrefabGuid ) noexcept;

        // The member of a prefab at an address as an instance of it starts from: the baked one, with the recipes of the prefab's nested instances applied. (recipe::FindTemplate gives the template
        // member of the prefab that owns the member - the inner prefab's own value, without what the outer prefab's nested recipe says.) Invalid when the prefab has no such member; EnsureLoaded first.
        inline
        xecs::component::entity FindBakedMember( guid PrefabGuid, std::span<const std::uint64_t> Address ) noexcept;

        // The resident template of a prefab and the plans baked from it are dropped: the prefab changed on disk (a Prefab Editor saved it) and the next instance of it reads the file again.
        // Instances already made are untouched (live update of them is phase 6).
        inline
        void        DropTemplate          ( guid PrefabGuid ) noexcept;

        // Drops every baked plan (and their inert entities): called when a template may have changed (Save, CreatePrefabFromEntity). Every plan
        // goes, because one prefab's plan holds the prefabs it nests.
        inline
        void        InvalidateBaked       ( void ) noexcept;

        // Spawns Count instances of a prefab - the game's call, and the editor's InstantiatePrefab: each member made Count times in one call,
        // built when the world runs builder systems (staged, as a scene load does), its parent, children and references written from the baked
        // plan. Callback runs on each root once its instance is complete. Returns every entity made, member-major ([iNode * Count + k], node 0
        // the roots), valid until the next spawn; empty when the prefab cannot be baked.
        template< typename T_CALLBACK = xecs::tools::empty_lambda >
        std::span<const xecs::component::entity> Spawn( guid PrefabGuid, int Count, T_CALLBACK&& Callback = xecs::tools::empty_lambda{} ) noexcept;

        xecs::game_mgr::instance&                                   m_GameMgr;
        std::unordered_map<std::uint64_t,xecs::component::entity>   m_PrefabList;

        // The resident templates as scenes (ids <-> live members, names) - alongside m_PrefabList (which
        // keeps its exact original shape/role, storing only the root). See xecs::prefab::group_bookkeeping's
        // own comment: the prefab manager's, never in the scene manager's list.
        std::unordered_map<std::uint64_t,group_bookkeeping>         m_PrefabGroups;

        // The project's root path, same convention as xecs::scene::mgr::m_ProjectPath /
        // xecs::level::mgr::m_ProjectPath.
        std::wstring                                                m_ProjectPath;

        std::unordered_map<std::uint64_t,std::unique_ptr<baked>>    m_Baked;

        save_redirect*                                              m_pRedirect = nullptr;
    };
}