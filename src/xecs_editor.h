namespace xecs::editor
{
    struct tag
    {
        constexpr static auto typedef_v = xecs::component::type::exclusive_tag
        {
            .m_pName = "EditorTag"
        };
    };

    //-------------------------------------------------------------------------------
    // The state an entity has in the EDITOR: working aids ("get this out of my way while I work"). They are what the Level Tree's power and eye columns add and remove. They are saved with the scene like any
    // other tag - they are the state the person left the scene in - and the scene/level compiler leaves them out of what the game gets (an editor-only tag is stripped there, with the rest of what the game
    // does not need). The runtime has its own tags for the state the designer authors and the scripts change (xlioncore::disable_tag, xlioncore::no_render_tag): the two never fight over the same tag.
    //
    // disable_tag: EXCLUSIVE, like the runtime one - an entity that has it only matches the queries that name it, so every system ignores it (and so the render: it is in no view).
    // no_render_tag: a regular tag - the render of the EDITOR's view leaves the entity out (the view of the game still draws it, as the scene view of other editors does).
    //-------------------------------------------------------------------------------
    struct disable_tag
    {
        constexpr static auto typedef_v = xecs::component::type::exclusive_tag
        { .m_Guid  = xecs::component::type::guid{ "xecs::editor::disable" }
        , .m_pName = "editor_disable"
        };

        XPROPERTY_DEF("editor_disable", disable_tag)
    };

    struct no_render_tag
    {
        constexpr static auto typedef_v = xecs::component::type::tag
        { .m_Guid  = xecs::component::type::guid{ "xecs::editor::no_render" }
        , .m_pName = "editor_no_render"
        };

        XPROPERTY_DEF("editor_no_render", no_render_tag)
    };

    //-------------------------------------------------------------------------------

    // Real, registered, reflected component tracking a prefab instance's per-property overrides -
    // see dependencies/xECSV2/doc and this session's design: the live component data always holds
    // the CURRENT value (default-from-prefab or overridden); this record only tracks WHICH
    // (component type, property path) pairs are non-default, so save only persists the delta and the
    // inspector can show a "this differs from the prefab" affordance. The property's own type is
    // resolved dynamically through xproperty at both save and revert time, so - unlike the original,
    // never-finished design sketch this is modeled on - no redundant type tag needs to be stored
    // alongside the property name. Sibling (non-nested) structs, matching this codebase's own
    // convention for reflected sub-structs (e.g. xskeleton_desc's bone/mask_group/mask_entry).
    struct prefab_property_override
    {
        std::string             m_PropertyName;

        // A string rendering of the override's current value (via xproperty::settings::AnyToString),
        // kept only for a human/tool reading this record to see what changed without also loading and
        // cross-referencing the component's own live data - the value the ECS actually simulates with
        // still lives solely on the component itself (this is a read-only echo of it, not a second
        // source of truth). Not used by Apply/Revert - both already read the real value directly, one
        // from the instance's live component, the other from the prefab's.
        std::string             m_PropertyValueAsString;

        XPROPERTY_DEF
        ( "PrefabPropertyOverride", prefab_property_override
        , obj_member<"PropertyName",  &prefab_property_override::m_PropertyName>
        , obj_member<"PropertyValue", &prefab_property_override::m_PropertyValueAsString>
        )
    };
    XPROPERTY_REG(prefab_property_override)

    // A member of an instance, addressed (documentation/Editors/prefabs_plan.md, 3.3): the permanent id of the member inside its prefab,
    // and when the member belongs to a nested instance, the chain of ids that crosses each instance boundary ([outer member id, inner
    // member id, ...]). Empty: the entity that carries the prefab_instance (the instance root). The root of a prefab that is itself an
    // instance (a variant) adds no element: its members are addressed as the base prefab's. Reordering, inserting or removing children
    // in a prefab changes no address; a member the prefab no longer has leaves an orphan, which the editor lists.
    using member_address = std::vector<std::uint64_t>;

    struct prefab_component_override
    {
        // Plain uint64 rather than xecs::component::type::guid itself: unlike the resource-system's
        // own guid aliases (type_guid/instance_guid/full_guid/def_guid<T>, bridged generically in
        // xresource_xproperty_bridge.h) or xecs::component::entity (its own dedicated bridge in
        // xecs_entity_xproperty_bridge.h), xECS's internal tagged guids (component::type::guid,
        // archetype::guid, ...) have no xproperty bridge of their own - adding one just for this one
        // field isn't worth the risk of a new, untested var_type<> specialization. Converted to/from
        // xecs::component::type::guid at the two call sites that need it (E29's override bookkeeping).
        std::uint64_t                          m_ComponentTypeGuid;

        // Which member of the instance this override targets (see member_address).
        member_address                         m_Member;

        // Before prefabs_plan.md phase 3: a child-index path from the entity carrying the prefab_instance. Read from old files only
        // (never written): the loader turns it into m_Member, resolving it against the prefab as it is (prefab_instance::m_Format 0).
        std::vector<std::uint32_t>             m_MemberPath;
        std::vector<prefab_property_override>  m_PropertyOverrides;

        XPROPERTY_DEF
        ( "PrefabComponentOverride", prefab_component_override
        , obj_member<"ComponentTypeGuid", &prefab_component_override::m_ComponentTypeGuid>
        , obj_member<"Member",             &prefab_component_override::m_Member>
        , obj_member<"MemberPath",         &prefab_component_override::m_MemberPath, member_flags<flags::DONT_SHOW, flags::DONT_SAVE>>
        , obj_member<"PropertyOverrides",  &prefab_component_override::m_PropertyOverrides>
        )
    };
    XPROPERTY_REG(prefab_component_override)

    // Tracks a component whose PRESENCE differs from the prefab (not a property-level override of a
    // component the prefab already has - that's prefab_component_override above). m_bAdded true means
    // this component exists on the instance but NOT on the prefab (so it has no prefab default to
    // derive from - its full data must be serialized, unlike an overridden-but-prefab-defined
    // component); m_bAdded false means the opposite - a component the prefab DOES define that this
    // instance has deliberately removed, which the load-side prefab-component union must exclude
    // rather than silently re-adding. One merged list with a bool rather than two separate lists -
    // "removed" is just "present here with m_bAdded false," no need for a second container.
    struct prefab_component_diff
    {
        std::uint64_t   m_ComponentTypeGuid;
        bool            m_bAdded;
        member_address  m_Member;           // the member whose components differ (empty: the instance root). The data of a component added to a
                                            // member is kept as property overrides of every one of its properties (a member has no file of its own).

        XPROPERTY_DEF
        ( "PrefabComponentDiff", prefab_component_diff
        , obj_member<"ComponentTypeGuid", &prefab_component_diff::m_ComponentTypeGuid>
        , obj_member<"Added",             &prefab_component_diff::m_bAdded>
        , obj_member<"Member",            &prefab_component_diff::m_Member>
        )
    };
    XPROPERTY_REG(prefab_component_diff)

    // Structural presence vs the prefab: m_bAdded false = a member of the prefab (and its subtree) this instance removed. m_bAdded true = the
    // member has children the prefab does not have: they are ordinary entities of the scene whose parent is that member (prefabs_plan.md 3.2),
    // listed here when the recipe is refreshed so that Apply and Revert know them; the loader does not need it.
    struct prefab_hierarchy_diff
    {
        member_address              m_Member;
        bool                        m_bAdded;
        std::vector<std::uint32_t>  m_MemberPath;       // before phase 3 (child-index path): read from old files only, see prefab_component_override

        XPROPERTY_DEF
        ( "PrefabHierarchyDiff", prefab_hierarchy_diff
        , obj_member<"Member",     &prefab_hierarchy_diff::m_Member>
        , obj_member<"Added",      &prefab_hierarchy_diff::m_bAdded>
        , obj_member<"MemberPath", &prefab_hierarchy_diff::m_MemberPath, member_flags<flags::DONT_SHOW, flags::DONT_SAVE>>
        )
    };
    XPROPERTY_REG(prefab_hierarchy_diff)

    // A prefab instance is a recipe (prefabs_plan.md 3.2): which prefab, and what this instance does differently. In a scene only the entity that
    // carries it is stored; its members are spawned from the prefab when the scene loads, with ids derived from the instance's id and their
    // address (xecs::scene::DeriveMemberId). Inside a prefab, a member that carries one is a nested instance (its members come from its prefab).
    struct prefab_instance
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName            = "EditorPrafabInstance"
        ,   .m_ReferenceMode    = xecs::component::type::reference_mode::NO_REFERENCES
        };

        // m_Format: 0 = written before phase 3 (in a scene, every member was an entity file of the scene, overrides addressed by child-index
        // paths); 1 = a recipe (members spawned, addressed by member_address). An old file has no Format row and reads 0; the loader
        // converts it (xecs::prefab::recipe), and the next save writes 1.
        static constexpr std::uint32_t recipe_format_v = 1;

        xecs::prefab::guid                          m_PrefabInstance;
        std::vector<prefab_component_override>      m_lComponents;
        std::vector<prefab_component_diff>          m_ComponentDiffs;
        std::vector<prefab_hierarchy_diff>          m_HierarchyDiffs;
        std::uint32_t                               m_Format = recipe_format_v;

        XPROPERTY_DEF
        ( "EditorPrafabInstance", prefab_instance
        , obj_member<"Prefab",          &prefab_instance::m_PrefabInstance>
        , obj_member<"Components",      &prefab_instance::m_lComponents>
        , obj_member<"ComponentDiffs",  &prefab_instance::m_ComponentDiffs>
        , obj_member<"HierarchyDiffs",  &prefab_instance::m_HierarchyDiffs>
        , obj_member<"Format",          &prefab_instance::m_Format>
        )
    };
    XPROPERTY_REG(prefab_instance)

    //-------------------------------------------------------------------------------

    struct link
    {
        xecs::component::entity         m_lEntity;
    };
}
