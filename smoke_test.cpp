#include "xecs.h"
#include <cstdio>
#include <cstdlib>

//------------------------------------------------------------------------------------------------
// Minimal smoke test: exercises component registration, prefab creation/instancing, system
// execution (Run), queries, and a full SerializeGameState save+load round trip.
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data
    {
        .m_pName = "Position"
    };

    xerr Serialize( xecs::serializer::stream& TextFile, bool ) noexcept
    {
        return TextFile.Field("Value", m_X, m_Y);
    }

    float m_X{};
    float m_Y{};

    XPROPERTY_DEF
    ( "Position", position
    , obj_member<"X", &position::m_X>
    , obj_member<"Y", &position::m_Y>
    )
};
XPROPERTY_REG(position)

struct velocity
{
    constexpr static auto typedef_v = xecs::component::type::data
    {
        .m_pName = "Velocity"
    };

    xerr Serialize( xecs::serializer::stream& TextFile, bool ) noexcept
    {
        return TextFile.Field("Value", m_X, m_Y);
    }

    float m_X{};
    float m_Y{};

    XPROPERTY_DEF
    ( "Velocity", velocity
    , obj_member<"X", &velocity::m_X>
    , obj_member<"Y", &velocity::m_Y>
    )
};
XPROPERTY_REG(velocity)

struct move_system : xecs::system::instance
{
    constexpr static auto typedef_v = xecs::system::type::update
    {
        .m_pName = "move_system"
    };

    void operator()( position& Position, const velocity& Velocity ) const noexcept
    {
        Position.m_X += Velocity.m_X;
        Position.m_Y += Velocity.m_Y;
    }
};

//------------------------------------------------------------------------------------------------

static int g_Failures = 0;
#define CHECK(EXPR) do { if(!(EXPR)) { std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #EXPR); ++g_Failures; } } while(false)
#define STEP(MSG) do { std::printf("STEP: %s\n", MSG); std::fflush(stdout); } while(false)

int main()
{
    constexpr auto file_v = L"smoke_test_save.txt";

    xecs::prefab::guid PrefabGuid;

    //
    // Phase 1: create, populate, run, save
    //
    {
        STEP("constructing game_mgr");
        xecs::game_mgr::instance GameMgr;

        STEP("RegisterComponents");
        GameMgr.RegisterComponents<position, velocity>();
        STEP("RegisterSystems");
        GameMgr.RegisterSystems<move_system>();

        STEP("CreatePrefab");
        PrefabGuid = GameMgr.CreatePrefab<position, velocity>([]( position& P, velocity& V ) noexcept
        {
            P.m_X = 1.0f; P.m_Y = 2.0f;
            V.m_X = 0.5f; V.m_Y = -0.5f;
        });

        CHECK( !PrefabGuid.empty() );

        STEP("CreatePrefabInstance");
        GameMgr.CreatePrefabInstance( 5, PrefabGuid, []( position&, velocity& ) noexcept {} );

        // Run a few frames - move_system should advance position by velocity each frame
        STEP("Run x3");
        for( int i = 0; i < 3; ++i ) GameMgr.Run();

        // Verify via a direct query that positions moved as expected (1 + 3*0.5 = 2.5, 2 + 3*-0.5 = 0.5)
        STEP("Search+Foreach");
        int Count = 0;
        xecs::query::instance Query;
        Query.m_Must.AddFromComponents<position, velocity>();
        auto S = GameMgr.Search(Query);
        GameMgr.Foreach(S, [&]( const position& P, const velocity& ) noexcept
        {
            ++Count;
            CHECK( std::abs(P.m_X - 2.5f) < 0.001f );
            CHECK( std::abs(P.m_Y - 0.5f) < 0.001f );
        });
        CHECK( Count == 5 );

        std::printf("Phase 1: %d entities checked, positions after 3 Run() calls verified.\n", Count);
        std::fflush(stdout);

        STEP("SerializeGameState save");
        auto Error = GameMgr.SerializeGameState( "smoke_test_save.txt", false, false );
        CHECK( !Error );
        if(Error) std::printf("Save error: %s\n", Error.getMessage().data());
        STEP("Phase 1 done");
    }

    //
    // Phase 2: fresh instance, load, verify data survived the round trip
    //
    {
        STEP("resetRegistrations");
        xecs::component::mgr::resetRegistrations();
        STEP("constructing game_mgr #2");
        xecs::game_mgr::instance GameMgr;
        STEP("RegisterComponents #2");
        GameMgr.RegisterComponents<position, velocity>();
        STEP("RegisterSystems #2");
        GameMgr.RegisterSystems<move_system>();

        STEP("SerializeGameState load");
        auto Error = GameMgr.SerializeGameState( "smoke_test_save.txt", true, false );
        CHECK( !Error );
        if(Error) std::printf("Load error: %s\n", Error.getMessage().data());
        STEP("load done");

        int Count = 0;
        xecs::query::instance Query;
        Query.m_Must.AddFromComponents<position, velocity>();
        auto S = GameMgr.Search(Query);
        STEP("Search #2 done, Foreach next");
        GameMgr.Foreach(S, [&]( const position& P, const velocity& V ) noexcept
        {
            ++Count;
            CHECK( std::abs(P.m_X - 2.5f) < 0.001f );
            CHECK( std::abs(P.m_Y - 0.5f) < 0.001f );
            CHECK( std::abs(V.m_X - 0.5f) < 0.001f );
            CHECK( std::abs(V.m_Y - (-0.5f)) < 0.001f );
        });
        CHECK( Count == 5 );

        std::printf("Phase 2: %d entities reloaded from disk, values verified.\n", Count);
    }

    if( g_Failures == 0 ) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return 1;
}
