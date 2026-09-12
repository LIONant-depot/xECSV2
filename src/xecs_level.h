namespace xecs::level
{
    // Level is a full resource too, same treatment as xecs::scene::guid: its identity IS a real
    // resource guid, gets the automatic drag-drop resource-picker widget through xproperty's
    // built-in member_ui<xresource::def_guid<T>> specialization, no bridging/conversion needed.
    inline constexpr auto type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("Level"));
    using guid = xresource::def_guid<type_guid_v>;

    struct instance
    {
        guid                            m_Guid;
        std::string                     m_Name;
        std::vector<xecs::scene::guid>  m_Scenes;
    };

    struct mgr
    {
        mgr( xecs::game_mgr::instance& GameMgr ) noexcept : m_GameMgr{ GameMgr } {}

        inline
        instance*   Find        ( guid LevelGuid ) noexcept;

        // Finds an already-registered level instance, or registers a brand new (empty) one. Used
        // internally by Load and by authoring/tooling code (e.g. the level editor) that needs to
        // build up an in-memory level before saving.
        inline
        instance&   FindOrCreate( guid LevelGuid ) noexcept;

        // Loads the level descriptor from disk (GUID-sharded Descriptors/Level/... layout).
        // Find-first: fails if the descriptor is missing (does not mint an empty Level).
        // Use FindOrCreate when authoring a brand-new Level before Save.
        inline
        xerr        Load        ( guid LevelGuid ) noexcept;
        inline
        xerr        Save        ( guid LevelGuid ) noexcept;

        // Loads every scene this level lists. Conditional/streaming loading (only some scenes
        // loading by default, others on-demand) is deferred - for now "activating" a level just
        // means requesting every scene it owns, cascading through xecs::scene::mgr as normal.
        inline
        xerr        Activate    ( guid LevelGuid ) noexcept;

        xecs::game_mgr::instance&                m_GameMgr;
        std::vector<std::unique_ptr<instance>>   m_LevelInstances;

        // The project's root path (matching e.g. e10::library_mgr::m_ProjectPath) - same convention
        // as xecs::scene::mgr::m_ProjectPath.
        std::wstring                              m_ProjectPath;
    };
}
