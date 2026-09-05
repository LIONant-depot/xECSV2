namespace xecs::scene
{
    // Scene's real, resource-pipeline-integrated descriptor: the dependency edges to parent scenes
    // and the interned external-reference table (see xecs_scene_inline.h's Encode/DecodeRef), saved
    // and loaded through the same descriptor::base::Serialize every other resource type uses - no
    // compiler plugin ever reads this, it's consumed directly by xecs::scene::mgr at runtime.
    struct descriptor : xresource_pipeline::descriptor::base
    {
        void SetupFromSource( std::string_view ) override {}
        void Validate       ( std::vector<std::string>& ) const noexcept override {}

        std::vector<guid>                      m_ParentScenes     = {};
        std::vector<external_entity_address>   m_ExternalRefTable = {};   // auto-populated by SaveEntity's interning

        // Editor-organizational entity grouping (see xecs_scene.h's folder comment) - never read by
        // the runtime load path beyond round-tripping it back into instance::m_Folders.
        std::vector<folder>                    m_Folders          = {};

        // The authoritative list of this scene's currently-valid entities. Load reads ONLY these ids
        // (not a directory scan of entity_db - see xecs_scene_inline.h's LoadSceneDescriptor/
        // EnsureLoaded), so removing an id here is what actually makes an entity "deleted": its
        // <id>.entity file can keep existing on disk (unlink now, physical reclaim deferred/batched
        // later) without ever being rediscovered and resurrected on the next load.
        std::vector<permanent_id>              m_ActiveEntities   = {};

        XPROPERTY_VDEF
        ( "Scene", descriptor
        , obj_member<"ParentScenes",    &descriptor::m_ParentScenes>
        , obj_member<"ExternalRefs",    &descriptor::m_ExternalRefTable, member_flags<flags::SHOW_READONLY>>
        , obj_member<"ActiveEntities",  &descriptor::m_ActiveEntities,   member_flags<flags::SHOW_READONLY>>
        , obj_member<"Folders",         &descriptor::m_Folders,          member_flags<flags::SHOW_READONLY>>
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
            return "Scene";
        }

        const xproperty::type::object& ResourceXPropertyObject( void ) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };
    inline static factory g_Factory{};
}
