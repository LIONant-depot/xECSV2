namespace xecs::prefab
{
    // Prefab's real, resource-pipeline-integrated descriptor. Deliberately thin: this pass's prefabs
    // are single-entity (no variant/parent-prefab chains yet, see xecs::prefab::root::m_ParentPrefabGuid
    // for that separate, not-yet-wired concept), so there's no structural relationship data to persist
    // here the way Scene's m_ParentScenes/m_ExternalRefTable is - m_ComponentTypeGuids is purely a
    // read-only diagnostic (what component types this prefab has), kept in sync by mgr::Save. The
    // prefab's actual component DATA is a separate file (Descriptors/Prefab/<b0>/<b1>/<guid>.desc/
    // Entity.txt, written/read by mgr::Save/EnsureLoaded) - the same split Scene uses between its own
    // Descriptor.txt and its entity_db files, and for the same reason: one entity's component set is
    // runtime-variable, not a fixed set of reflected fields a descriptor object could hold directly.
    struct descriptor : xresource_pipeline::descriptor::base
    {
        void SetupFromSource( std::string_view ) override {}
        void Validate       ( std::vector<std::string>& ) const noexcept override {}

        std::vector<std::uint64_t> m_ComponentTypeGuids = {};

        XPROPERTY_VDEF
        ( "Prefab", descriptor
        , obj_member<"ComponentTypeGuids", &descriptor::m_ComponentTypeGuids, member_flags<flags::SHOW_READONLY>>
        )
    };
    XPROPERTY_VREG(descriptor)

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor( void ) const noexcept override
        {
            return std::make_unique<descriptor>();
        }

        xresource::type_guid ResourceTypeGUID( void ) const noexcept override
        {
            return type_guid_v;
        }

        const char* ResourceTypeName( void ) const noexcept override
        {
            return "Prefab";
        }

        const xproperty::type::object& ResourceXPropertyObject( void ) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };
    // See xecs_scene_descriptor.h's own GetFactory for why this is a class-static-member holder
    // rather than a namespace-scope `inline static` (internal linkage, duplicates per TU) or a
    // function-local static (would change eager registration to lazy).
    namespace details { struct factory_holder { inline static factory s_Instance{}; }; }
    inline factory& GetFactory() noexcept { return details::factory_holder::s_Instance; }
}
