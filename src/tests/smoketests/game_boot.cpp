// ================================================================================================
// File: game_boot.cpp
// Brief: Prove native collision/physics, ticks, events and stable fixture-map reloads on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/game_boot.h"
#include "ps2/game/headless_fixture.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Use the retained game class/event hierarchy and interpreter, not the compiler-only substitute.
#include <d3xp/Game_local.h>

namespace
{
static constexpr int kActivationFrame = 4;
static constexpr int kDelayedActivationMs = 90;
static constexpr int kCanceledActivationMs = 120;
static constexpr int kDelayedActivationFrame = 6;
static constexpr int kRemovalFrame = 7;
static constexpr char kFixtureCommand[] = "fixture-activated";
static constexpr float kFixtureGravity = 512.0f;

enum class Motion
{
    Wall,
    Falling,
    Sliding,
};

class LogicProbe final : public idEntity
{
    CLASS_PROTOTYPE(LogicProbe);

  public:
    void Spawn()
    {
        m_physics.SetSelf(this);
        m_physics.SetGravity(vec3_zero);
        m_physics.UseFlyMove(true);
        auto * model = new (TAG_PHYSICS_CLIP) idClipModel(idTraceModel(idBounds(vec3_origin).Expand(2.0f)));
        model->SetContents(CONTENTS_BODY);
        m_physics.SetClipModel(model, 1.0f);
        m_physics.SetClipMask(CONTENTS_SOLID);
        m_physics.SetOrigin(idVec3(0.0f, 0.0f, 16.0f));
        SetPhysics(&m_physics);
        BecomeActive(TH_THINK);
    }
    void Think() override
    {
        ++ticks;
        if (m_motion == Motion::Wall)
        {
            m_physics.SetLinearVelocity(idVec3(512.0f, 0.0f, 0.0f));
        }
        else if (m_motion == Motion::Sliding)
        {
            m_physics.SetLinearVelocity(idVec3(512.0f, 64.0f, 0.0f));
        }
        RunPhysics();
    }
    void SetMotion(Motion motion)
    {
        PS2_Assert(ticks == 0 && motion != Motion::Wall);
        m_motion = motion;
        m_physics.UseFlyMove(false);
        m_physics.SetGravity(idVec3(0.0f, 0.0f, -kFixtureGravity));
        m_physics.SetLinearVelocity(vec3_zero);
        if (motion == Motion::Falling)
        {
            m_physics.SetOrigin(idVec3(-32.0f, -32.0f, 4.0f));
        }
        else
        {
            m_physics.UseVelocityMove(true);
            m_physics.SetMaxStepHeight(0.0f); // Exercise sliding without stepping over obstacles.
            m_physics.SetOrigin(idVec3(0.0f, 32.0f, 2.0f + CM_CLIP_EPSILON));
        }
    }
    const idPhysics_Monster & GetMotionPhysics() const { return m_physics; }
    int ticks = 0;

  private:
    idPhysics_Monster m_physics;
    Motion m_motion = Motion::Wall;
};
CLASS_DECLARATION(idEntity, LogicProbe)
END_CLASS

class PlayerPhysicsProbe final : public idEntity
{
    CLASS_PROTOTYPE(PlayerPhysicsProbe);

  public:
    void Spawn()
    {
        m_physics.SetSelf(this);
        m_physics.SetGravity(idVec3(0.0f, 0.0f, -kFixtureGravity));
        m_physics.SetSpeed(128.0f, 64.0f);
        m_physics.SetMaxStepHeight(0.0f);
        m_physics.SetMaxJumpHeight(16.0f);
        m_physics.SetMovementType(PM_NORMAL);
        // Keep the native standing height and a small footprint within the authored world.
        const idBounds bounds(idVec3(-2.0f, -2.0f, 0.0f), idVec3(2.0f, 2.0f, pm_normalheight.GetFloat()));
        auto * model = new (TAG_PHYSICS_CLIP) idClipModel(idTraceModel(bounds));
        model->SetContents(CONTENTS_BODY);
        m_physics.SetClipModel(model, 1.0f);
        m_physics.SetClipMask(CONTENTS_SOLID);
        m_physics.SetOrigin(idVec3(20.0f, -16.0f, CM_CLIP_EPSILON));
        SetPhysics(&m_physics);
        BecomeActive(TH_THINK | TH_PHYSICS);
    }
    void BindCommands(idUserCmdMgr & commands)
    {
        PS2_Assert(ticks == 0 && m_commands == nullptr);
        m_commands = &commands;
    }
    void Think() override
    {
        if (m_commands == nullptr || !m_commands->HasUserCmdForPlayer(0))
        {
            ps2::FatalError("native player-physics probe requires a queued user command");
        }
        m_command = m_commands->GetUserCmdForPlayer(0);
        m_physics.SetPlayerInput(m_command, idVec3(1.0f, 0.0f, 0.0f));
        ++ticks;
        RunPhysics();
    }
    const idPhysics_Player & GetMotionPhysics() const { return m_physics; }
    const usercmd_t & GetCommand() const { return m_command; }
    int ticks = 0;

  private:
    idPhysics_Player m_physics;
    idUserCmdMgr * m_commands = nullptr;
    usercmd_t m_command;
};
CLASS_DECLARATION(idEntity, PlayerPhysicsProbe)
END_CLASS

bool Check(const char * name, bool passed)
{
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK game/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}
bool Same(const ps2::heap::Stats & a, const ps2::heap::Stats & b)
{
    return a.requestedBytes == b.requestedBytes && a.backingBytes == b.backingBytes && a.allocationCount == b.allocationCount;
}
bool Near(float actual, float expected) { return idMath::Fabs(actual - expected) < 0.05f; }

bool FloorContacts(const idPhysics_Actor & physics)
{
    if (!physics.HasGroundContacts() || physics.GetNumContacts() == 0 || physics.GetGroundEntity() != gameLocal.world)
    {
        return false;
    }
    for (int i = 0; i < physics.GetNumContacts(); ++i)
    {
        const contactInfo_t & contact = physics.GetContact(i);
        if (contact.entityNum == ENTITYNUM_WORLD && contact.normal == idVec3(0.0f, 0.0f, 1.0f) &&
            (contact.contents & static_cast<int>(CONTENTS_SOLID)) != 0)
        {
            return true;
        }
    }
    return false;
}

bool CollisionTests(LogicProbe & probe, int cycle)
{
    const auto & bounds = gameLocal.clip.GetWorldBounds();
    bool passed = Check("collision-world", bounds[0].Compare(idVec3(-64.0f, -64.0f, -8.0f), 0.001f) &&
                                           bounds[1].Compare(idVec3(64.0f, 64.0f, 64.0f), 0.001f));
    trace_t point{}, box{}, miss{};
    const idVec3 start(-16.0f, 16.0f, 16.0f), end(-16.0f, 16.0f, -16.0f);
    const bool pointHit = gameLocal.clip.TracePoint(point, start, end, CONTENTS_SOLID, nullptr);
    const bool boxHit = gameLocal.clip.TraceBounds(box, start, end, idBounds(vec3_origin).Expand(2.0f), CONTENTS_SOLID, nullptr);
    const bool missed = !gameLocal.clip.TracePoint(miss, start, idVec3(16.0f, 16.0f, 16.0f), CONTENTS_SOLID, nullptr);
    passed = Check("collision-world-traces", pointHit && boxHit && missed && miss.fraction == 1.0f &&
                                             Near(point.endpos.z, CM_CLIP_EPSILON) && Near(box.endpos.z, 2.0f + CM_CLIP_EPSILON) &&
                                             Near(point.fraction, (16.0f - CM_CLIP_EPSILON) / 32.0f) &&
                                             Near(box.fraction, (14.0f - CM_CLIP_EPSILON) / 32.0f) &&
                                             point.c.entityNum == ENTITYNUM_WORLD && box.c.entityNum == ENTITYNUM_WORLD &&
                                             point.c.normal == idVec3(0.0f, 0.0f, 1.0f) && (point.c.contents & static_cast<int>(CONTENTS_SOLID)) != 0) &&
             passed;
    const int inside = gameLocal.clip.Contents(idVec3(-16.0f, 16.0f, -4.0f), nullptr, mat3_identity, CONTENTS_SOLID, nullptr);
    const int outside = gameLocal.clip.Contents(start, nullptr, mat3_identity, CONTENTS_SOLID, nullptr);
    // Native point contents queries return all brush bits; a volume query applies the mask.
    const int filtered = gameLocal.clip.Contents(idVec3(-16.0f, 16.0f, -4.0f), probe.GetPhysics()->GetClipModel(),
                                                 mat3_identity, CONTENTS_BODY, &probe);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_COLLISION cycle=%d point=%.5f point_z=%.3f box=%.5f box_z=%.3f inside=%d outside=%d filtered=%d\n",
             cycle, static_cast<double>(point.fraction), static_cast<double>(point.endpos.z),
             static_cast<double>(box.fraction), static_cast<double>(box.endpos.z), inside, outside, filtered);
    passed = Check("collision-contents-mask", inside == CONTENTS_SOLID && outside == 0 && filtered == 0) && passed;
    const idVec3 entityStart(-8.0f, 0.0f, 16.0f), entityEnd(8.0f, 0.0f, 16.0f);
    trace_t entity{}, ignored{}, disabled{}, enabled{};
    const bool entityHit = gameLocal.clip.TracePoint(entity, entityStart, entityEnd, CONTENTS_BODY, nullptr);
    const bool entityIgnored = !gameLocal.clip.TracePoint(ignored, entityStart, entityEnd, CONTENTS_BODY, &probe);
    idClipModel * model = probe.GetPhysics()->GetClipModel();
    PS2_Assert(model != nullptr && model->IsLinked());
    model->Disable();
    const bool entityDisabled = !gameLocal.clip.TracePoint(disabled, entityStart, entityEnd, CONTENTS_BODY, nullptr);
    model->Enable();
    const bool entityEnabled = gameLocal.clip.TracePoint(enabled, entityStart, entityEnd, CONTENTS_BODY, nullptr);
    passed = Check("collision-entity-filter", entityHit && entityEnabled && entityIgnored && entityDisabled &&
                                              entity.c.entityNum == probe.entityNumber && enabled.c.entityNum == probe.entityNumber &&
                                              ignored.fraction == 1.0f && disabled.fraction == 1.0f && Near(entity.endpos.x, -2.25f)) &&
             passed;
    return passed;
}
void Memory(const char * stage)
{
    const auto stats = ps2::heap::GetTotalStats();
    const auto arena = ps2::heap::GetArenaStats();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_MEMORY %s requested=%zu backing=%zu allocations=%zu peak_requested=%zu peak_backing=%zu arena=%zu\n",
             stage, stats.requestedBytes, stats.backingBytes, stats.allocationCount,
             stats.peakRequestedBytes, stats.peakBackingBytes, arena.committedBytes);
}
} // namespace

namespace ps2::smoketests
{
bool RunGameTests(const char * mode)
{
    ::game->Init(); // Installs the versioned imports and initializes native class/event/script registries.
    Memory("initialized");
    bool passed = Check("native-init", ::game == &gameLocal && gameLocal.GameState() == GAMESTATE_NOMAP &&
                                       gameLocal.program.FindFunction("doom_main") != nullptr && idClass::GetClass("idWorldspawn") != nullptr);
    if (idStr::Cmp(mode, "game") != 0)
    {
        gameLocal.InitHeadlessFixture("maps/logic");
        ps2::FatalError("game failure probe returned unexpectedly");
    }
    // This buffer is native and deliberately heap allocated instead of consuming the EE stack.
    idUserCmdMgr * commands = new (TAG_GAME) idUserCmdMgr;
    ps2::heap::Stats warmed{};
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        gameLocal.InitHeadlessFixture("maps/logic");
        passed = Check("logic-world", gameLocal.world != nullptr && gameLocal.GameState() == GAMESTATE_ACTIVE &&
                                      gameRenderWorld == nullptr && gameSoundWorld == nullptr && gameLocal.numClients == 0) &&
                 passed;
        idDict args;
        args.Set("name", "logic_probe");
        args.Set("noclipmodel", "1");
        LogicProbe * probe = static_cast<LogicProbe *>(gameLocal.SpawnEntityType(LogicProbe::Type, &args));
        args.Clear();
        args.Set("name", "logic_target");
        args.Set("noclipmodel", "1");
        args.Set("command", kFixtureCommand);
        idEntity * target = gameLocal.SpawnEntityType(idTarget_SessionCommand::Type, &args);
        args.Clear();
        if (probe == nullptr || target == nullptr || !target->IsType(idTarget_SessionCommand::Type) || !target->RespondsTo(EV_Activate))
        {
            FatalError("native command target did not spawn with its activation event");
        }
        passed = CollisionTests(*probe, cycle) && passed;
        bool physicsPassed = true;
        args.Set("name", "fall_probe");
        args.Set("noclipmodel", "1");
        auto * falling = static_cast<LogicProbe *>(gameLocal.SpawnEntityType(LogicProbe::Type, &args));
        args.Set("name", "slide_probe");
        auto * sliding = static_cast<LogicProbe *>(gameLocal.SpawnEntityType(LogicProbe::Type, &args));
        args.Clear();
        PS2_Assert(falling != nullptr && sliding != nullptr);
        falling->SetMotion(Motion::Falling);
        sliding->SetMotion(Motion::Sliding);
        commands->ResetPlayer(0);
        args.Set("name", "player_physics_probe");
        args.Set("noclipmodel", "1");
        auto * player = static_cast<PlayerPhysicsProbe *>(gameLocal.SpawnEntityType(PlayerPhysicsProbe::Type, &args));
        args.Clear();
        PS2_Assert(player != nullptr);
        player->BindCommands(*commands);
        bool playerQueuePassed = !commands->HasUserCmdForPlayer(0) && commands->readFrame[0] == -1 && commands->writeFrame[0] == 0;
        bool playerWalkPassed = true, playerReleasePassed = true, playerFloorPassed = true;
        float previousPlayerX = 20.0f, previousPlayerVelocity = 0.0f;
        bool gravityPassed = true, floorPassed = true, slidingPassed = true;
        float fallZ = 4.0f, fallVelocity = 0.0f, previousSlideY = 32.0f;
        // Native SetName binds the script's $logic_target reference before the first frame.
        bool commandPassed = gameLocal.FindEntity("logic_target") == target && gameLocal.sessionCommand.Length() == 0;
        idEntityPtr<idEntity> targetRef;
        targetRef = target;
        bool lifetimePassed = targetRef.IsValid() && targetRef.GetEntity() == target;
        // Deadlines fall between 60 Hz frames: 90 ms is serviced at 100 ms, while script
        // removal at 116 ms must cancel the second activation before its 120 ms deadline.
        const bool activationPosted = target->PostEventMS(&EV_Activate, kDelayedActivationMs, probe);
        const bool canceledPosted = target->PostEventMS(&EV_Activate, kCanceledActivationMs, probe);
        passed = Check("delayed-events-posted", activationPosted && canceledPosted) && passed;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_EVENTS cycle=%d activation_ms=%d canceled_ms=%d posted=%d\n",
                 cycle, kDelayedActivationMs, kCanceledActivationMs,
                 static_cast<int>(activationPosted) + static_cast<int>(canceledPosted));
        idVarDef * value = gameLocal.program.GetDef(&type_float, "fixtureTicks", &def_namespace);
        passed = value != nullptr && *value->value.floatPtr == 0.0f && passed;
        for (int frame = 1; frame <= 8; ++frame)
        {
            usercmd_t input;
            input.forwardmove = frame <= 4 ? 127 : frame == 5 ? -127
                                                              : 0;
            input.clientGameMilliseconds = input.serverGameMilliseconds = FRAME_TO_MSEC(frame);
            commands->PutUserCmdForPlayer(0, input);
            playerQueuePassed = commands->HasUserCmdForPlayer(0) &&
                                commands->GetNextUserCmdClientTime(0) == input.clientGameMilliseconds && playerQueuePassed;
            gameReturn_t result{};
            ::game->RunFrame(*commands, result);
            const auto & position = probe->GetPhysics()->GetOrigin();
            const float expectedX = frame < 3 ? 512.0f * static_cast<float>(gameLocal.time) * 0.001f : 21.75f;
            const bool stopped = idMath::Fabs(probe->GetPhysics()->GetLinearVelocity().x) < 1.0f;
            physicsPassed = Near(position.x, expectedX) && Near(position.y, 0.0f) && Near(position.z, 16.0f) &&
                            stopped == (frame >= 3) && probe->GetPhysics()->GetClipModel()->IsLinked() && physicsPassed;
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_PHYSICS cycle=%d frame=%d x=%.3f y=%.3f z=%.3f stopped=%d\n",
                     cycle, gameLocal.GetFrameNum(), static_cast<double>(position.x), static_cast<double>(position.y),
                     static_cast<double>(position.z), static_cast<int>(stopped));
            const auto & fall = falling->GetMotionPhysics();
            const auto & slide = sliding->GetMotionPhysics();
            const float step = static_cast<float>(gameLocal.time - gameLocal.previousTime) * 0.001f;
            // Native airborne integration uses the old velocity, then adds gravity.
            fallZ += fallVelocity * step;
            fallVelocity -= kFixtureGravity * step;
            const bool grounded = fall.OnGround(), resting = fall.IsAtRest();
            const bool floor = FloorContacts(fall);
            gravityPassed = falling->ticks == frame && Near(fall.GetOrigin().x, -32.0f) && Near(fall.GetOrigin().y, -32.0f) &&
                            (frame > 5 || (Near(fall.GetOrigin().z, fallZ) && Near(fall.GetLinearVelocity().z, fallVelocity))) &&
                            grounded == (frame >= 7) && resting == (frame >= 7) && gravityPassed;
            floorPassed = fall.GetOrigin().z >= 2.0f && (frame < 6 || Near(fall.GetOrigin().z, 2.25f)) &&
                          (frame < 7 || (floor && fall.GetLinearVelocity() == vec3_zero)) && floorPassed;
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_FALL cycle=%d frame=%d x=%.3f y=%.3f z=%.3f vz=%.3f ground=%d rest=%d floor=%d\n",
                     cycle, frame, static_cast<double>(fall.GetOrigin().x), static_cast<double>(fall.GetOrigin().y),
                     static_cast<double>(fall.GetOrigin().z), static_cast<double>(fall.GetLinearVelocity().z),
                     static_cast<int>(grounded), static_cast<int>(resting), static_cast<int>(floor));
            const bool blocked = slide.GetSlideMoveEntity() == gameLocal.world;
            const bool slideFloor = FloorContacts(slide);
            const float minimumY = 32.0f + 64.0f * static_cast<float>(gameLocal.time) * 0.001f;
            slidingPassed = sliding->ticks == frame && Near(slide.GetOrigin().x, expectedX) && Near(slide.GetOrigin().z, 2.25f) &&
                            slide.GetOrigin().y > previousSlideY && slide.GetOrigin().y >= minimumY - 0.05f &&
                            slide.GetOrigin().y < minimumY + 0.75f && Near(slide.GetLinearVelocity().y, 64.0f) &&
                            Near(slide.GetLinearVelocity().z, 0.0f) && slide.OnGround() && !slide.IsAtRest() && slideFloor &&
                            blocked == (frame >= 3) && slide.GetMoveResult() == (frame >= 3 ? MM_SLIDING : MM_OK) &&
                            (frame < 3 ? Near(slide.GetLinearVelocity().x, 512.0f) : idMath::Fabs(slide.GetLinearVelocity().x) < 1.0f) &&
                            slidingPassed;
            previousSlideY = slide.GetOrigin().y;
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_SLIDE cycle=%d frame=%d x=%.3f y=%.3f z=%.3f vx=%.3f vy=%.3f ground=%d floor=%d blocked=%d move=%d\n",
                     cycle, frame, static_cast<double>(slide.GetOrigin().x), static_cast<double>(slide.GetOrigin().y),
                     static_cast<double>(slide.GetOrigin().z), static_cast<double>(slide.GetLinearVelocity().x),
                     static_cast<double>(slide.GetLinearVelocity().y), static_cast<int>(slide.OnGround()),
                     static_cast<int>(slideFloor), static_cast<int>(blocked), static_cast<int>(slide.GetMoveResult()));
            const auto & playerPhysics = player->GetMotionPhysics();
            const auto & playerPosition = playerPhysics.GetOrigin();
            const auto & playerVelocity = playerPhysics.GetLinearVelocity();
            const auto & consumed = player->GetCommand();
            const bool pending = commands->HasUserCmdForPlayer(0);
            playerQueuePassed = player->ticks == frame && consumed.forwardmove == input.forwardmove &&
                                consumed.rightmove == 0 && consumed.buttons == 0 &&
                                consumed.clientGameMilliseconds == input.clientGameMilliseconds &&
                                consumed.serverGameMilliseconds == input.serverGameMilliseconds && !pending &&
                                commands->readFrame[0] == frame - 1 && commands->writeFrame[0] == frame && playerQueuePassed;
            const bool playerFloor = FloorContacts(playerPhysics);
            playerFloorPassed = playerFloor && Near(playerPosition.y, -16.0f) && Near(playerPosition.z, CM_CLIP_EPSILON) &&
                                Near(playerVelocity.y, 0.0f) && Near(playerVelocity.z, 0.0f) &&
                                playerPosition.x <= 21.75f && playerPosition.z >= 0.0f && playerPhysics.GetClipModel()->IsLinked() &&
                                !playerPhysics.HasJumped() && !playerPhysics.HasSteppedUp() && !playerPhysics.IsCrouching() &&
                                !playerPhysics.OnLadder() && playerPhysics.GetWaterLevel() == WATERLEVEL_NONE && playerFloorPassed;
            if (frame < 4)
            {
                playerWalkPassed = playerPosition.x > previousPlayerX && playerVelocity.x > previousPlayerVelocity && playerWalkPassed;
            }
            else if (frame == 4)
            {
                playerWalkPassed = Near(playerPosition.x, 21.75f) && idMath::Fabs(playerVelocity.x) < 1.0f && playerWalkPassed;
            }
            else if (frame == 5)
            {
                playerReleasePassed = playerPosition.x < previousPlayerX && playerVelocity.x < -1.0f && playerReleasePassed;
            }
            else if (frame < 8)
            {
                playerReleasePassed = playerPosition.x < previousPlayerX && playerVelocity.x < 0.0f &&
                                      idMath::Fabs(playerVelocity.x) < idMath::Fabs(previousPlayerVelocity) && playerReleasePassed;
            }
            else
            {
                playerReleasePassed = Near(playerPosition.x, previousPlayerX) && playerVelocity == vec3_zero && playerReleasePassed;
            }
            previousPlayerX = playerPosition.x;
            previousPlayerVelocity = playerVelocity.x;
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_PLAYER cycle=%d frame=%d cmd=%d time=%d read=%d written=%d pending=%d x=%.3f y=%.3f z=%.3f vx=%.3f floor=%d\n",
                     cycle, frame, static_cast<int>(consumed.forwardmove), consumed.clientGameMilliseconds,
                     commands->readFrame[0], commands->writeFrame[0], static_cast<int>(pending),
                     static_cast<double>(playerPosition.x), static_cast<double>(playerPosition.y),
                     static_cast<double>(playerPosition.z), static_cast<double>(playerVelocity.x), static_cast<int>(playerFloor));
            const char * expectedCommand = frame == kActivationFrame || frame == kDelayedActivationFrame ? kFixtureCommand : "";
            commandPassed = idStr::Cmp(result.sessionCommand, expectedCommand) == 0 &&
                            gameLocal.sessionCommand.Length() == 0 && commandPassed;
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_COMMAND cycle=%d frame=%d command=%s expected=%s pending=%d\n",
                     cycle, gameLocal.GetFrameNum(), result.sessionCommand[0] != '\0' ? result.sessionCommand : "none",
                     expectedCommand[0] != '\0' ? expectedCommand : "none", gameLocal.sessionCommand.Length());
            // Resolve only the generation-checked handle after RunFrame: script removal may
            // have deleted the original target while native events were being serviced.
            const bool valid = targetRef.IsValid();
            const bool resolved = targetRef.GetEntity() != nullptr;
            const bool named = gameLocal.FindEntity("logic_target") != nullptr;
            const bool expectedAlive = frame < kRemovalFrame;
            lifetimePassed = valid == expectedAlive && resolved == expectedAlive && named == expectedAlive && lifetimePassed;
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_LIFETIME cycle=%d frame=%d valid=%d resolved=%d named=%d\n",
                     cycle, gameLocal.GetFrameNum(), static_cast<int>(valid), static_cast<int>(resolved), static_cast<int>(named));
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_TICK cycle=%d frame=%d time=%d expected=%d think=%d script=%.0f\n",
                     cycle, gameLocal.GetFrameNum(), gameLocal.time, FRAME_TO_MSEC(frame), probe->ticks,
                     static_cast<double>(value != nullptr ? *value->value.floatPtr : -1.0f));
            passed = gameLocal.GetFrameNum() == frame && gameLocal.time == FRAME_TO_MSEC(frame) &&
                     probe->ticks == frame && value != nullptr && *value->value.floatPtr == static_cast<float>(frame) &&
                     result.vibrationLow == 0 && result.vibrationHigh == 0 && passed;
        }
        passed = Check("native-ticks-script-events", passed) && passed;
        passed = Check("physics-wall-stop", physicsPassed) && passed;
        passed = Check("physics-gravity", gravityPassed) && passed;
        passed = Check("physics-floor-rest", floorPassed) && passed;
        passed = Check("physics-ground-slide", slidingPassed) && passed;
        passed = Check("player-physics-walk-wall", playerWalkPassed) && passed;
        passed = Check("player-physics-release-stop", playerReleasePassed) && passed;
        passed = Check("player-physics-floor", playerFloorPassed) && passed;
        passed = Check("script-target-command", commandPassed) && passed;
        passed = Check("entity-removal-cancellation", lifetimePassed && commandPassed && gameLocal.time >= kCanceledActivationMs) && passed;
        Memory("ticked");
        ::game->MapShutdown();
        passed = Check("map-shutdown", gameLocal.GameState() == GAMESTATE_NOMAP && gameLocal.world == nullptr &&
                                       gameLocal.FindEntity("logic_probe") == nullptr && gameLocal.FindEntity("logic_target") == nullptr &&
                                       gameLocal.FindEntity("fall_probe") == nullptr && gameLocal.FindEntity("slide_probe") == nullptr &&
                                       gameLocal.FindEntity("player_physics_probe") == nullptr &&
                                       !targetRef.IsValid() && targetRef.GetEntity() == nullptr &&
                                       gameLocal.program.GetDef(&type_entity, "$logic_target", &def_namespace) == nullptr &&
                                       gameLocal.sessionCommand.Length() == 0 && gameLocal.program.FindFunction("main") == nullptr) &&
                 passed;
        commands->ResetPlayer(0);
        passed = Check("player-command-queue", playerQueuePassed && !commands->HasUserCmdForPlayer(0) &&
                                               commands->readFrame[0] == -1 && commands->writeFrame[0] == 0) &&
                 passed;
        passed = Check("collision-shutdown", ps2::heap::GetStats(TAG_COLLISION).allocationCount == 0 &&
                                             ps2::heap::GetStats(TAG_PHYSICS_CLIP).allocationCount == 0 &&
                                             idClipModel::TraceModelCacheSize() == 0) &&
                 passed;
        const auto stats = ps2::heap::GetTotalStats();
        if (cycle == 0)
        {
            warmed = stats;
        }
        else
        {
            passed = Check("reload-ledger", Same(warmed, stats)) && passed;
        }
    }
    delete commands;
    ::game->Shutdown();
    Memory("shutdown");
    return passed;
}
} // namespace ps2::smoketests
