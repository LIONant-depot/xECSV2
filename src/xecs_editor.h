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

        // Which member of the group this override targets, as a child-index path walked from the
        // entity that actually carries this prefab_instance component: empty means "the
        // prefab_instance-carrying entity itself" (the only case that existed before multi-entity
        // groups), {i} means "my children.m_List[i]", {i,j} means that child's own children.m_List[j],
        // etc. A path, not a stored id, because a placed instance's non-root members have no id of
        // their own at all - the instance is a structural clone of the prefab's own root/children tree
        // (see [[xecs_multientity_prefab_architecture]]), so the SAME index path found by walking down
        // from the instance root also finds the corresponding member walking down from the PREFAB's own
        // root, with no extra bookkeeping needed on either side. Stops at the nearest containing
        // instance (does not cross into a nested instance's own subtree) - matches this session's
        // existing scope narrowing for nested-instance diffs.
        std::vector<std::uint32_t>             m_MemberPath;
        std::vector<prefab_property_override>  m_PropertyOverrides;

        XPROPERTY_DEF
        ( "PrefabComponentOverride", prefab_component_override
        , obj_member<"ComponentTypeGuid", &prefab_component_override::m_ComponentTypeGuid>
        , obj_member<"MemberPath",         &prefab_component_override::m_MemberPath>
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

        XPROPERTY_DEF
        ( "PrefabComponentDiff", prefab_component_diff
        , obj_member<"ComponentTypeGuid", &prefab_component_diff::m_ComponentTypeGuid>
        , obj_member<"Added",             &prefab_component_diff::m_bAdded>
        )
    };
    XPROPERTY_REG(prefab_component_diff)

    // Structural (child) presence vs the prefab - same compositional idea as prefab_component_diff,
    // but the key is a MemberPath (child-index path from the PI root), not a component type guid.
    // m_bAdded false = a prefab-defined child this instance removed; true = a child added only on
    // the instance (wired for symmetry; removed-child is the first consumer).
    struct prefab_hierarchy_diff
    {
        std::vector<std::uint32_t>  m_MemberPath;
        bool                        m_bAdded;

        XPROPERTY_DEF
        ( "PrefabHierarchyDiff", prefab_hierarchy_diff
        , obj_member<"MemberPath", &prefab_hierarchy_diff::m_MemberPath>
        , obj_member<"Added",      &prefab_hierarchy_diff::m_bAdded>
        )
    };
    XPROPERTY_REG(prefab_hierarchy_diff)

    struct prefab_instance
    {
        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName            = "EditorPrafabInstance"
        ,   .m_ReferenceMode    = xecs::component::type::reference_mode::NO_REFERENCES
        };

        xecs::prefab::guid                          m_PrefabInstance;
        std::vector<prefab_component_override>      m_lComponents;
        std::vector<prefab_component_diff>          m_ComponentDiffs;
        std::vector<prefab_hierarchy_diff>          m_HierarchyDiffs;

        XPROPERTY_DEF
        ( "EditorPrafabInstance", prefab_instance
        , obj_member<"Prefab",          &prefab_instance::m_PrefabInstance>
        , obj_member<"Components",      &prefab_instance::m_lComponents>
        , obj_member<"ComponentDiffs",  &prefab_instance::m_ComponentDiffs>
        , obj_member<"HierarchyDiffs",  &prefab_instance::m_HierarchyDiffs>
        )
    };
    XPROPERTY_REG(prefab_instance)

    //-------------------------------------------------------------------------------

    struct link
    {
        xecs::component::entity         m_lEntity;
    };
}
