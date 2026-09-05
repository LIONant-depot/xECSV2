#include "xecs.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>

//------------------------------------------------------------------------------------------------
// Milestone 1 smoke test for xecs::scene: dependency-ordered load/unload + permanent-ID entity
// serialization with cross-scene reference remapping.
//
// Builds a parent scene (one entity with a position) and a child scene (two entities with a "link"
// component: one pointing cross-scene at the parent's entity, one pointing same-scene at the other
// child entity), saves both to disk, resets all in-memory ECS state, then reloads via
// RequestLoad(child) - which must transitively load the parent first - and verifies both references
// resolved to live entities with correct data. Finally verifies residency: ReleaseLoad(child)
// cascades to unload the parent (nothing else references it), and a second independent RequestLoad
// keeps a scene resident after the first caller releases it.
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "ScenePosition" };

    float m_X{};
    float m_Y{};

    XPROPERTY_DEF
    ( "ScenePosition", position
    , obj_member<"X", &position::m_X>
    , obj_member<"Y", &position::m_Y>
    )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "SceneLink" };

    xecs::component::entity m_Target{};

    XPROPERTY_DEF
    ( "SceneLink", link
    , obj_member<"Target", &link::m_Target>
    )
};
XPROPERTY_REG(link)

//------------------------------------------------------------------------------------------------

static int g_Failures = 0;
#define CHECK(EXPR) do { if(!(EXPR)) { std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #EXPR); ++g_Failures; } } while(false)
#define STEP(MSG) do { std::printf("STEP: %s\n", MSG); std::fflush(stdout); } while(false)

int main()
{
    const std::wstring RootPath = L"smoke_test_scene_data";
    std::error_code Ec;
    std::filesystem::remove_all( std::filesystem::path(RootPath), Ec );

    const xecs::scene::guid ParentGuid{ "SmokeTestParentScene" };
    const xecs::scene::guid ChildGuid { "SmokeTestChildScene"  };

    xecs::component::entity SavedParentEntity1;
    xecs::component::entity SavedChildEntity1;
    xecs::component::entity SavedChildEntity2;

    //
    // Phase 1: author two scenes in memory, save them to disk
    //
    {
        STEP("constructing game_mgr");
        xecs::game_mgr::instance GameMgr;

        STEP("RegisterComponents");
        GameMgr.RegisterComponents<position, link>();
        GameMgr.RegisterSystems<>(); // locks component bit IDs - required before any archetype/entity creation

        GameMgr.m_SceneMgr.m_ProjectPath = RootPath;

        STEP("create parent entity");
        auto& PositionArchetype = GameMgr.getOrCreateArchetype<position>();
        auto  ParentEntity1     = PositionArchetype.CreateEntity([]( position& P ) noexcept
        {
            P.m_X = 10.0f; P.m_Y = 20.0f;
        });
        CHECK( ParentEntity1.isValid() );

        STEP("create child entities");
        auto& LinkArchetype = GameMgr.getOrCreateArchetype<link>();
        auto  ChildEntity1  = LinkArchetype.CreateEntity([&]( link& L ) noexcept
        {
            L.m_Target = ParentEntity1; // cross-scene reference
        });
        auto  ChildEntity2  = LinkArchetype.CreateEntity([&]( link& L ) noexcept
        {
            L.m_Target = ChildEntity1; // same-scene reference
        });
        CHECK( ChildEntity1.isValid() );
        CHECK( ChildEntity2.isValid() );

        STEP("declare dependency edge (child -> parent)");
        auto& ParentScene = GameMgr.m_SceneMgr.FindOrCreate(ParentGuid);
        auto& ChildScene  = GameMgr.m_SceneMgr.FindOrCreate(ChildGuid);
        ChildScene.m_ParentScenes.push_back(ParentGuid);
        (void)ParentScene;

        STEP("SaveEntity x3");
        {
            auto Error = GameMgr.m_SceneMgr.SaveEntity(ParentGuid, 1, ParentEntity1);
            CHECK(!Error); if(Error) std::printf("SaveEntity(parent,1) error: %s\n", Error.getMessage().data());
        }
        {
            auto Error = GameMgr.m_SceneMgr.SaveEntity(ChildGuid, 100, ChildEntity1);
            CHECK(!Error); if(Error) std::printf("SaveEntity(child,100) error: %s\n", Error.getMessage().data());
        }
        {
            auto Error = GameMgr.m_SceneMgr.SaveEntity(ChildGuid, 101, ChildEntity2);
            CHECK(!Error); if(Error) std::printf("SaveEntity(child,101) error: %s\n", Error.getMessage().data());
        }

        STEP("SaveSceneDescriptor x2");
        {
            auto Error = GameMgr.m_SceneMgr.SaveSceneDescriptor(ParentGuid);
            CHECK(!Error); if(Error) std::printf("SaveSceneDescriptor(parent) error: %s\n", Error.getMessage().data());
        }
        {
            auto Error = GameMgr.m_SceneMgr.SaveSceneDescriptor(ChildGuid);
            CHECK(!Error); if(Error) std::printf("SaveSceneDescriptor(child) error: %s\n", Error.getMessage().data());
        }

        SavedParentEntity1 = ParentEntity1;
        SavedChildEntity1  = ChildEntity1;
        SavedChildEntity2  = ChildEntity2;

        STEP("Phase 1 done");
    }

    //
    // Phase 2: fresh ECS state, reload the child scene (must transitively load the parent), verify
    //
    {
        STEP("resetRegistrations");
        xecs::component::mgr::resetRegistrations();

        STEP("constructing game_mgr #2");
        xecs::game_mgr::instance GameMgr;

        STEP("RegisterComponents #2");
        GameMgr.RegisterComponents<position, link>();
        GameMgr.RegisterSystems<>(); // locks component bit IDs - required before any archetype/entity creation

        GameMgr.m_SceneMgr.m_ProjectPath = RootPath;

        STEP("RequestLoad(child)");
        auto Error = GameMgr.m_SceneMgr.RequestLoad(ChildGuid);
        CHECK(!Error); if(Error) std::printf("RequestLoad(child) error: %s\n", Error.getMessage().data());

        auto* pParent = GameMgr.m_SceneMgr.Find(ParentGuid);
        auto* pChild  = GameMgr.m_SceneMgr.Find(ChildGuid);
        CHECK( pParent != nullptr );
        CHECK( pChild  != nullptr );

        if( pParent && pChild )
        {
            CHECK( pParent->m_State == xecs::scene::state::Active );
            CHECK( pChild->m_State  == xecs::scene::state::Active );
            CHECK( pParent->m_DependentSceneCount == 1 ); // held resident by the child, not an explicit request
            CHECK( pParent->m_ExplicitRequests    == 0 );

            auto ParentEntity1 = pParent->m_LocalToRuntime.at(1);
            auto ChildEntity1  = pChild->m_LocalToRuntime.at(100);
            auto ChildEntity2  = pChild->m_LocalToRuntime.at(101);

            STEP("verify parent position data");
            auto& PD  = GameMgr.m_ComponentMgr.getEntityDetails(ParentEntity1);
            auto& Pos = PD.m_pPool->getComponent<position>(PD.m_PoolIndex);
            CHECK( std::abs(Pos.m_X - 10.0f) < 0.001f );
            CHECK( std::abs(Pos.m_Y - 20.0f) < 0.001f );

            STEP("verify cross-scene reference resolved correctly");
            auto& L1D = GameMgr.m_ComponentMgr.getEntityDetails(ChildEntity1);
            auto& L1  = L1D.m_pPool->getComponent<link>(L1D.m_PoolIndex);
            CHECK( L1.m_Target.isValid() );
            CHECK( L1.m_Target.m_Value == ParentEntity1.m_Value );

            STEP("verify same-scene reference resolved correctly");
            auto& L2D = GameMgr.m_ComponentMgr.getEntityDetails(ChildEntity2);
            auto& L2  = L2D.m_pPool->getComponent<link>(L2D.m_PoolIndex);
            CHECK( L2.m_Target.isValid() );
            CHECK( L2.m_Target.m_Value == ChildEntity1.m_Value );

            STEP("residency: second independent RequestLoad on the parent");
            auto Error2 = GameMgr.m_SceneMgr.RequestLoad(ParentGuid);
            CHECK(!Error2);
            CHECK( pParent->m_ExplicitRequests == 1 );

            STEP("ReleaseLoad(child) - parent must stay resident (independent request + nothing else)");
            auto Error3 = GameMgr.m_SceneMgr.ReleaseLoad(ChildGuid);
            CHECK(!Error3);
            CHECK( pChild->m_State  == xecs::scene::state::Unloaded );
            CHECK( pParent->m_DependentSceneCount == 0 );
            CHECK( pParent->m_State == xecs::scene::state::Active ); // still held by the independent RequestLoad above

            STEP("ReleaseLoad(parent) - now it should unload");
            auto Error4 = GameMgr.m_SceneMgr.ReleaseLoad(ParentGuid);
            CHECK(!Error4);
            CHECK( pParent->m_State == xecs::scene::state::Unloaded );
        }

        STEP("Phase 2 done");
    }

    if( g_Failures == 0 ) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return 1;
}
