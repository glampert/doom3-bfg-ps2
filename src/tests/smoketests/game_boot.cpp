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
        if ((thinkFlags & TH_THINK) == 0)
        {
            return;
        }
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
        if ((thinkFlags & TH_THINK) == 0)
        {
            return;
        }
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

void PlayerArgs(idDict & args)
{
    args.Clear();
    args.Set("classname", "fixture_player");
    args.Set("name", "native_player");
    args.Set("spawn_entnum", "0");
    args.Set("noclipmodel", "1");
    args.Set("scriptobject", "fixture_player");
    args.Set("origin", "-32 0 0.25");
    args.Set("health", "100");
}

bool NativePlayerTests(idUserCmdMgr & commands, int cycle, idEntityPtr<idPlayer> & reference)
{
    commands.ResetPlayer(0);
    idDict args;
    PlayerArgs(args);
    auto * player = static_cast<idPlayer *>(gameLocal.SpawnEntityType(idPlayer::Type, &args));
    args.Clear();
    PS2_Assert(player != nullptr);
    reference = player;
    idVarDef * constructs = gameLocal.program.GetDef(&type_float, "nativePlayerConstructs", &def_namespace);
    idVarDef * ticks = gameLocal.program.GetDef(&type_float, "nativePlayerTicks", &def_namespace);
    const auto * physics = static_cast<const idPhysics_Player *>(player->GetPlayerPhysics());
    bool passed = Check("native-player-spawn", gameLocal.GetLocalPlayer() == player && gameLocal.entities[0] == player &&
                                               player->entityNumber == 0 && player->GetType() == &idPlayer::Type && gameLocal.numClients == 1 &&
                                               reference.IsValid() && player->GetPhysics() == physics &&
                                               physics->GetBounds()[0] == idVec3(-16.0f, -16.0f, 0.0f) &&
                                               physics->GetBounds()[1] == idVec3(16.0f, 16.0f, 74.0f) &&
                                               physics->GetOrigin() == idVec3(-32.0f, 0.0f, CM_CLIP_EPSILON));
    bool scriptPassed = constructs != nullptr && *constructs->value.floatPtr == 1.0f && ticks != nullptr && *ticks->value.floatPtr == 0.0f;
    bool queuePassed = true, movementPassed = true, statePassed = true;
    float previousX = -32.0f, previousVelocity = 0.0f;
    for (int frame = 89; frame <= 99; ++frame)
    {
        usercmd_t input;
        input.forwardmove = frame <= 92 ? 127 : 0;
        input.clientGameMilliseconds = input.serverGameMilliseconds = FRAME_TO_MSEC(frame);
        commands.PutUserCmdForPlayer(0, input);
        queuePassed = commands.HasUserCmdForPlayer(0) && queuePassed;
        gameReturn_t result{};
        ::game->RunFrame(commands, result);
        const auto & position = physics->GetOrigin();
        const auto & velocity = physics->GetLinearVelocity();
        const bool floor = FloorContacts(*physics), pending = commands.HasUserCmdForPlayer(0);
        const int tick = frame - 88;
        queuePassed = player->usercmd.forwardmove == input.forwardmove && player->usercmd.clientGameMilliseconds == FRAME_TO_MSEC(frame) &&
                      player->usercmd.serverGameMilliseconds == FRAME_TO_MSEC(frame) && !pending &&
                      commands.readFrame[0] == tick - 1 && commands.writeFrame[0] == tick &&
                      gameLocal.GetLastClientUsercmdMilliseconds(0) == FRAME_TO_MSEC(frame) &&
                      player->usercmd.pos == position && queuePassed;
        scriptPassed = constructs != nullptr && *constructs->value.floatPtr == 1.0f && ticks != nullptr &&
                       *ticks->value.floatPtr == static_cast<float>(tick) && scriptPassed;
        statePassed = player->health == 100 && player->inventory.maxHealth == 100 && player->inventory.armor == 0 &&
                      player->inventory.weapons == 0 && player->weapon.GetEntity() == nullptr && player->flashlight.GetEntity() == nullptr &&
                      player->hudManager == nullptr && player->hud == nullptr && player->pdaMenu == nullptr &&
                      player->GetRenderView() == nullptr && player->GetRenderEntity()->hModel == nullptr &&
                      gameRenderWorld == nullptr && gameSoundWorld == nullptr && gameLocal.NumAAS() == 0 &&
                      floor && player->AI_ONGROUND && !player->AI_JUMP && !player->AI_CROUCH && !player->AI_DEAD &&
                      player->AI_FORWARD == (frame <= 92) && !player->AI_BACKWARD && !player->AI_RUN && !player->AI_ATTACK_HELD &&
                      !player->AI_ONLADDER && physics->GetClipModel()->IsLinked() &&
                      gameLocal.GetFrameNum() == frame && gameLocal.time == FRAME_TO_MSEC(frame) &&
                      result.sessionCommand[0] == '\0' && result.vibrationHigh == 0 && result.vibrationLow == 0 && statePassed;
        movementPassed = Near(position.y, 0.0f) && Near(position.z, CM_CLIP_EPSILON) && velocity.y == 0.0f && velocity.z == 0.0f &&
                         position.x >= previousX && position.x < -16.0f &&
                         (frame <= 92 ? velocity.x > previousVelocity : velocity.x <= previousVelocity) &&
                         (frame < 99 || (velocity == vec3_zero && Near(position.x, previousX))) && movementPassed;
        previousX = position.x;
        previousVelocity = velocity.x;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_NATIVE_PLAYER cycle=%d frame=%d cmd=%d time=%d read=%d written=%d pending=%d x=%.3f y=%.3f z=%.3f vx=%.3f floor=%d health=%d script=%.0f constructs=%.0f\n",
                 cycle, frame, static_cast<int>(player->usercmd.forwardmove), player->usercmd.clientGameMilliseconds,
                 commands.readFrame[0], commands.writeFrame[0], static_cast<int>(pending),
                 static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z),
                 static_cast<double>(velocity.x), static_cast<int>(floor), player->health,
                 static_cast<double>(ticks != nullptr ? *ticks->value.floatPtr : -1.0f),
                 static_cast<double>(constructs != nullptr ? *constructs->value.floatPtr : -1.0f));
    }
    passed = Check("native-player-command-path", queuePassed) && passed;
    passed = Check("native-player-script", scriptPassed) && passed;
    passed = Check("native-player-movement", movementPassed) && passed;
    passed = Check("native-player-simulation-state", statePassed) && passed;
    return passed;
}

float ScriptFloat(const char * name)
{
    const idVarDef * value = gameLocal.program.GetDef(&type_float, name, &def_namespace);
    if (value == nullptr)
    {
        ps2::FatalError("missing native player fixture global: %s", name);
    }
    return *value->value.floatPtr;
}

bool NativePlayerPostureTests(idPlayer & player, idUserCmdMgr & commands, int cycle)
{
    const auto & physics = *static_cast<const idPhysics_Player *>(player.GetPlayerPhysics());
    const idVec3 start = physics.GetOrigin();
    const float gravity = g_gravity.GetFloat(), jumpHeight = pm_jumpheight.GetFloat();
    const float launchSpeed = idMath::Sqrt(2.0f * gravity * jumpHeight);
    // Native contacts detect the floor on the 600 ms sample just above it. The
    // following WalkMove settles overclip velocity; vertical rest keeps that height
    // until crouched movement steps down to the collision margin.
    const float landingOffset = launchSpeed * 0.6f - 0.5f * gravity * 0.6f * 0.6f;
    const idEventDef * getState = idEventDef::FindEvent("getState");
    if (getState == nullptr || !player.RespondsTo(*getState))
    {
        ps2::FatalError("native player actor state query is unavailable");
    }
    bool queuePassed = true, jumpPassed = true, crouchPassed = true, scriptPassed = true, conditionsPassed = true;
    float peakZ = start.z, expectedX = start.x, expectedVx = 0.0f, expectedEye = pm_normalviewheight.GetFloat();
    for (int frame = 100; frame <= 195; ++frame)
    {
        const bool crouched = frame >= 181 && frame <= 189;
        const bool moving = frame >= 181 && frame <= 184;
        const bool airborne = frame < 135 || (frame >= 142 && frame < 177);
        const bool landing = frame == 135 || frame == 136 || frame == 177 || frame == 178;
        usercmd_t input;
        input.buttons = crouched ? BUTTON_CROUCH | BUTTON_JUMP : frame <= 140 || frame == 142 ? BUTTON_JUMP
                                                                                              : 0;
        input.forwardmove = moving ? 127 : 0;
        input.clientGameMilliseconds = input.serverGameMilliseconds = FRAME_TO_MSEC(frame);
        commands.PutUserCmdForPlayer(0, input);
        gameReturn_t result{};
        ::game->RunFrame(commands, result);
        const auto & position = physics.GetOrigin();
        const auto & velocity = physics.GetLinearVelocity();
        const bool floor = FloorContacts(physics), jumped = physics.HasJumped();
        const int tick = frame - 88;
        queuePassed = player.usercmd.buttons == input.buttons && player.usercmd.forwardmove == input.forwardmove &&
                      player.usercmd.clientGameMilliseconds == FRAME_TO_MSEC(frame) &&
                      player.usercmd.serverGameMilliseconds == FRAME_TO_MSEC(frame) && player.usercmd.pos == position &&
                      gameLocal.GetLastClientUsercmdMilliseconds(0) == FRAME_TO_MSEC(frame) &&
                      commands.readFrame[0] == tick - 1 && commands.writeFrame[0] == tick &&
                      !commands.HasUserCmdForPlayer(0) && queuePassed;
        const int launch = frame < 142 ? 100 : 142;
        const float elapsed = static_cast<float>(FRAME_TO_MSEC(frame) - FRAME_TO_MSEC(launch - 1)) * 0.001f;
        const bool ballistic = frame <= 135 || (frame >= 142 && frame <= 177);
        const float launchZ = start.z + (frame < 142 ? 0.0f : landingOffset);
        const float restZ = frame < 142 ? start.z + landingOffset : frame < 181 ? start.z + 2.0f * landingOffset
                                                                                : start.z;
        const float z = ballistic ? launchZ + launchSpeed * elapsed - 0.5f * gravity * elapsed * elapsed : restZ;
        const float vz = ballistic ? launchSpeed - gravity * elapsed : frame == 136 || frame == 178 ? 0.289f
                                                                                                    : 0.0f;
        jumpPassed = jumped == (frame == 100 || frame == 142) && floor == !airborne &&
                     Near(position.z, z) && Near(velocity.z, vz) && Near(position.y, start.y) && velocity.y == 0.0f &&
                     position.z >= start.z - 0.01f && position.z <= launchZ + jumpHeight + 0.05f && jumpPassed;
        if (position.z > peakZ)
        {
            peakZ = position.z;
        }
        const float dt = static_cast<float>(gameLocal.time - gameLocal.previousTime) * 0.001f;
        expectedVx = Max(0.0f, expectedVx - Max(100.0f, expectedVx) * 6.0f * dt);
        if (moving)
        {
            expectedVx = Min(pm_crouchspeed.GetFloat(), expectedVx + pm_crouchspeed.GetFloat() * 10.0f * dt);
        }
        expectedX += expectedVx * dt;
        const float rate = pm_crouchrate.GetFloat();
        expectedEye = expectedEye * rate + (crouched ? pm_crouchviewheight.GetFloat() : pm_normalviewheight.GetFloat()) * (1.0f - rate);
        crouchPassed = physics.IsCrouching() == crouched && Near(physics.GetBounds()[1].z, crouched ? 38.0f : 74.0f) &&
                       Near(position.x, expectedX) && Near(velocity.x, expectedVx) && Near(player.EyeHeight(), expectedEye) && crouchPassed;
        const int state = airborne ? 2 : crouched ? 3
                                                  : 1;
        const int transitions = frame < 135 ? 2 : frame < 142       ? 3
                                                  : frame < 177     ? 4
                                                    : frame < 181   ? 5
                                                      : frame < 190 ? 6
                                                                    : 7;
        const int starts = frame < 142 ? 1 : 2, landings = frame < 135 ? 0 : frame < 177 ? 1
                                                                                         : 2;
        const int softTicks = frame < 135 ? 0 : frame == 135     ? 1
                                                : frame < 177    ? 2
                                                  : frame == 177 ? 3
                                                                 : 4;
        const char * stateName = airborne ? "FixtureAir" : crouched ? "FixtureCrouch"
                                                                    : "FixtureIdle";
        const function_t * stateFunction = player.scriptObject.GetFunction(stateName);
        const bool nativeState = player.ProcessEvent(getState);
        scriptPassed = nativeState && stateFunction != nullptr &&
                       idStr::Cmp(gameLocal.program.returnStringDef->value.stringPtr, stateFunction->Name()) == 0 &&
                       ScriptFloat("nativePlayerState") == static_cast<float>(state) &&
                       ScriptFloat("nativePlayerTransitions") == static_cast<float>(transitions) &&
                       ScriptFloat("nativePlayerJumpStarts") == static_cast<float>(starts) &&
                       ScriptFloat("nativePlayerLandings") == static_cast<float>(landings) &&
                       ScriptFloat("nativePlayerSoftLandingTicks") == static_cast<float>(softTicks) &&
                       ScriptFloat("nativePlayerTicks") == static_cast<float>(tick) &&
                       ScriptFloat("nativePlayerConstructs") == 1.0f && ScriptFloat("fixtureTicks") == 8.0f && scriptPassed;
        conditionsPassed = player.AI_JUMP == jumped && player.AI_ONGROUND == floor && player.AI_CROUCH == crouched &&
                           player.AI_SOFTLANDING == landing && !player.AI_HARDLANDING && !player.AI_DEAD &&
                           !player.AI_ONLADDER && player.AI_FORWARD == moving && player.health == 100 &&
                           player.inventory.weapons == 0 && player.weapon.GetEntity() == nullptr &&
                           player.GetRenderView() == nullptr && gameRenderWorld == nullptr && gameSoundWorld == nullptr &&
                           gameLocal.GetFrameNum() == frame && gameLocal.time == FRAME_TO_MSEC(frame) &&
                           gameLocal.FindEntity("logic_target") == nullptr && result.sessionCommand[0] == '\0' &&
                           result.vibrationLow == 0 && result.vibrationHigh == 0 && conditionsPassed;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_PLAYER_STATE cycle=%d frame=%d buttons=%d cmd=%d time=%d read=%d written=%d pending=%d x=%.3f y=%.3f z=%.3f vx=%.3f vz=%.3f height=%.3f eye=%.3f floor=%d jumped=%d crouched=%d soft=%d health=%d script=%.0f state=%.0f transitions=%.0f starts=%.0f landings=%.0f soft_ticks=%.0f constructs=%.0f native=%s\n",
                 cycle, frame, static_cast<int>(player.usercmd.buttons), static_cast<int>(player.usercmd.forwardmove),
                 player.usercmd.clientGameMilliseconds, commands.readFrame[0], commands.writeFrame[0],
                 static_cast<int>(commands.HasUserCmdForPlayer(0)), static_cast<double>(position.x), static_cast<double>(position.y),
                 static_cast<double>(position.z), static_cast<double>(velocity.x), static_cast<double>(velocity.z),
                 static_cast<double>(physics.GetBounds()[1].z), static_cast<double>(player.EyeHeight()),
                 static_cast<int>(floor), static_cast<int>(jumped), static_cast<int>(physics.IsCrouching()),
                 static_cast<int>(player.AI_SOFTLANDING), player.health, static_cast<double>(ScriptFloat("nativePlayerTicks")),
                 static_cast<double>(ScriptFloat("nativePlayerState")), static_cast<double>(ScriptFloat("nativePlayerTransitions")),
                 static_cast<double>(ScriptFloat("nativePlayerJumpStarts")), static_cast<double>(ScriptFloat("nativePlayerLandings")),
                 static_cast<double>(ScriptFloat("nativePlayerSoftLandingTicks")), static_cast<double>(ScriptFloat("nativePlayerConstructs")),
                 gameLocal.program.returnStringDef->value.stringPtr);
    }
    bool passed = Check("native-player-posture-commands", queuePassed);
    passed = Check("native-player-jump-held-release-land", jumpPassed && Near(peakZ, start.z + landingOffset + jumpHeight)) && passed;
    passed = Check("native-player-crouch-speed-view", crouchPassed) && passed;
    passed = Check("native-player-script-transitions", scriptPassed) && passed;
    passed = Check("native-player-posture-conditions", conditionsPassed) && passed;
    return passed;
}

bool NativePlayerHeadroomTests(idPlayer & player, idUserCmdMgr & commands, int cycle)
{
    PS2_Assert(gameLocal.GetFrameNum() == 195 && !commands.HasUserCmdForPlayer(0));
    auto & physics = *static_cast<idPhysics_Player *>(player.GetPlayerPhysics());
    // The completed probes retain collision models after thinking stops. Isolate
    // this lane from their bodies, then place the same player in authored test space.
    for (const char * name : { "logic_probe", "fall_probe", "slide_probe", "player_physics_probe" })
    {
        idEntity * probe = gameLocal.FindEntity(name);
        if (probe == nullptr || probe->GetPhysics()->GetClipModel() == nullptr)
        {
            ps2::FatalError("missing completed headroom fixture probe: %s", name);
        }
        probe->GetPhysics()->GetClipModel()->Disable();
    }
    const idVec3 start(-48.0f, 40.0f, CM_CLIP_EPSILON), end(-8.0f, 40.0f, CM_CLIP_EPSILON);
    const idBounds standing(idVec3(-16.0f, -16.0f, 0.0f), idVec3(16.0f, 16.0f, 74.0f));
    const idBounds crouching(idVec3(-16.0f, -16.0f, 0.0f), idVec3(16.0f, 16.0f, 38.0f));
    trace_t standingTrace{}, crouchTrace{};
    const bool standingHit = gameLocal.clip.TraceBounds(standingTrace, start, end, standing, CONTENTS_SOLID, &player);
    const bool crouchHit = gameLocal.clip.TraceBounds(crouchTrace, start, end, crouching, CONTENTS_SOLID, &player);
    const int roof = gameLocal.clip.Contents(idVec3(-20.0f, 40.0f, 48.0f), nullptr, mat3_identity, CONTENTS_SOLID, &player);
    const int gap = gameLocal.clip.Contents(idVec3(-20.0f, 40.0f, 40.0f), nullptr, mat3_identity, CONTENTS_SOLID, &player);
    bool passed = Check("native-player-headroom-world", standingHit && standingTrace.c.entityNum == ENTITYNUM_WORLD &&
                                                        standingTrace.c.normal == idVec3(-1.0f, 0.0f, 0.0f) &&
                                                        Near(standingTrace.endpos.x, -40.25f) && Near(standingTrace.fraction, 7.75f / 40.0f) &&
                                                        !crouchHit && crouchTrace.fraction == 1.0f && roof == CONTENTS_SOLID && gap == 0);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_HEADROOM_WORLD cycle=%d standing=%.5f standing_x=%.3f crouch=%.5f roof=%d gap=%d\n",
             cycle, static_cast<double>(standingTrace.fraction), static_cast<double>(standingTrace.endpos.x),
             static_cast<double>(crouchTrace.fraction), roof, gap);
    player.SetOrigin(start);
    physics.SetLinearVelocity(vec3_zero);
    const idEventDef * getState = idEventDef::FindEvent("getState");
    if (getState == nullptr || !player.RespondsTo(*getState))
    {
        ps2::FatalError("native player actor state query is unavailable");
    }
    bool queuePassed = true, standingPassed = true, crouchPassed = true, releasePassed = true, scriptPassed = true;
    float previousX = start.x, expectedX = -40.25f, expectedVx = 0.0f, expectedEye = player.EyeHeight();
    for (int frame = 196; frame <= 275; ++frame)
    {
        const bool moving = frame <= 231 || (frame >= 240 && frame <= 264);
        const bool crouchButton = frame >= 212 && frame <= 231;
        const bool expectedCrouch = frame >= 212 && frame <= 264;
        usercmd_t input;
        input.buttons = crouchButton ? BUTTON_CROUCH : 0;
        input.forwardmove = moving ? 127 : 0;
        input.clientGameMilliseconds = input.serverGameMilliseconds = FRAME_TO_MSEC(frame);
        commands.PutUserCmdForPlayer(0, input);
        gameReturn_t result{};
        ::game->RunFrame(commands, result);
        const auto & position = physics.GetOrigin();
        const auto & velocity = physics.GetLinearVelocity();
        const bool floor = FloorContacts(physics), crouched = physics.IsCrouching();
        const int tick = frame - 88;
        queuePassed = player.usercmd.buttons == input.buttons && player.usercmd.forwardmove == input.forwardmove &&
                      player.usercmd.clientGameMilliseconds == FRAME_TO_MSEC(frame) &&
                      player.usercmd.serverGameMilliseconds == FRAME_TO_MSEC(frame) && player.usercmd.pos == position &&
                      gameLocal.GetLastClientUsercmdMilliseconds(0) == FRAME_TO_MSEC(frame) &&
                      commands.readFrame[0] == tick - 1 && commands.writeFrame[0] == tick &&
                      !commands.HasUserCmdForPlayer(0) && queuePassed;
        trace_t headroom{};
        gameLocal.clip.TraceBounds(headroom, position, position + idVec3(0.0f, 0.0f, 36.0f),
                                   crouching, CONTENTS_SOLID, &player);
        if (frame < 212)
        {
            standingPassed = !crouched && position.x >= previousX - 0.01f && position.x <= -40.2f &&
                             (frame < 210 || (Near(position.x, -40.25f) && idMath::Fabs(velocity.x) < 1.0f)) && standingPassed;
        }
        else
        {
            const float dt = static_cast<float>(gameLocal.time - gameLocal.previousTime) * 0.001f;
            expectedVx = Max(0.0f, expectedVx - Max(100.0f, expectedVx) * 6.0f * dt);
            if (moving)
            {
                expectedVx = Min(80.0f, expectedVx + 80.0f * 10.0f * dt);
            }
            expectedX += expectedVx * dt;
            crouchPassed = Near(position.x, expectedX) && Near(velocity.x, expectedVx) &&
                           (frame != 231 || position.x > -26.0f) && crouchPassed;
            if (frame >= 232 && frame <= 263)
            {
                releasePassed = input.buttons == 0 && crouched && headroom.fraction < 1.0f &&
                                headroom.c.entityNum == ENTITYNUM_WORLD && headroom.c.normal == idVec3(0.0f, 0.0f, -1.0f) &&
                                Near(headroom.fraction, 5.5f / 36.0f) && releasePassed;
            }
            if (frame >= 265)
            {
                releasePassed = !crouched && headroom.fraction == 1.0f && position.x > 0.25f &&
                                (frame < 273 || velocity == vec3_zero) && releasePassed;
            }
        }
        expectedEye = expectedEye * pm_crouchrate.GetFloat() +
                      (expectedCrouch ? pm_crouchviewheight.GetFloat() : pm_normalviewheight.GetFloat()) * (1.0f - pm_crouchrate.GetFloat());
        const char * stateName = expectedCrouch ? "FixtureCrouch" : "FixtureIdle";
        const function_t * stateFunction = player.scriptObject.GetFunction(stateName);
        const bool nativeState = player.ProcessEvent(getState);
        scriptPassed = crouched == expectedCrouch && player.AI_CROUCH == expectedCrouch && player.AI_ONGROUND &&
                       !player.AI_JUMP && !player.AI_SOFTLANDING && !player.AI_HARDLANDING && !player.AI_DEAD &&
                       player.AI_FORWARD == (moving && velocity.x > 5.0f) && floor && !physics.HasJumped() &&
                       Near(position.y, start.y) && Near(position.z, start.z) && velocity.y == 0.0f && velocity.z == 0.0f &&
                       Near(physics.GetBounds()[1].z, expectedCrouch ? 38.0f : 74.0f) && Near(player.EyeHeight(), expectedEye) &&
                       nativeState && stateFunction != nullptr &&
                       idStr::Cmp(gameLocal.program.returnStringDef->value.stringPtr, stateFunction->Name()) == 0 &&
                       ScriptFloat("nativePlayerState") == (expectedCrouch ? 3.0f : 1.0f) &&
                       ScriptFloat("nativePlayerTransitions") == (frame < 212 ? 7.0f : frame < 265 ? 8.0f
                                                                                                   : 9.0f) &&
                       ScriptFloat("nativePlayerTicks") == static_cast<float>(tick) && ScriptFloat("nativePlayerConstructs") == 1.0f &&
                       ScriptFloat("nativePlayerJumpStarts") == 2.0f && ScriptFloat("nativePlayerLandings") == 2.0f &&
                       ScriptFloat("nativePlayerSoftLandingTicks") == 4.0f && ScriptFloat("fixtureTicks") == 8.0f &&
                       player.health == 100 && player.inventory.weapons == 0 && player.weapon.GetEntity() == nullptr &&
                       gameRenderWorld == nullptr && gameSoundWorld == nullptr && gameLocal.GetFrameNum() == frame &&
                       gameLocal.time == FRAME_TO_MSEC(frame) && result.sessionCommand[0] == '\0' &&
                       result.vibrationLow == 0 && result.vibrationHigh == 0 && scriptPassed;
        previousX = position.x;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_HEADROOM cycle=%d frame=%d buttons=%d cmd=%d time=%d read=%d written=%d pending=%d x=%.3f y=%.3f z=%.3f vx=%.3f height=%.3f eye=%.3f floor=%d crouched=%d forward=%d health=%d headroom=%.5f script=%.0f state=%.0f transitions=%.0f native=%s\n",
                 cycle, frame, static_cast<int>(player.usercmd.buttons), static_cast<int>(player.usercmd.forwardmove),
                 player.usercmd.clientGameMilliseconds, commands.readFrame[0], commands.writeFrame[0],
                 static_cast<int>(commands.HasUserCmdForPlayer(0)), static_cast<double>(position.x), static_cast<double>(position.y),
                 static_cast<double>(position.z), static_cast<double>(velocity.x), static_cast<double>(physics.GetBounds()[1].z),
                 static_cast<double>(player.EyeHeight()), static_cast<int>(floor), static_cast<int>(crouched),
                 static_cast<int>(player.AI_FORWARD), player.health,
                 static_cast<double>(headroom.fraction), static_cast<double>(ScriptFloat("nativePlayerTicks")),
                 static_cast<double>(ScriptFloat("nativePlayerState")), static_cast<double>(ScriptFloat("nativePlayerTransitions")),
                 gameLocal.program.returnStringDef->value.stringPtr);
    }
    passed = Check("native-player-headroom-command-path", queuePassed) && passed;
    passed = Check("native-player-ceiling-standing-block", standingPassed) && passed;
    passed = Check("native-player-ceiling-crouch-pass", crouchPassed) && passed;
    passed = Check("native-player-ceiling-release-restore", releasePassed) && passed;
    passed = Check("native-player-ceiling-script-view", scriptPassed) && passed;
    return passed;
}

bool PlayerPostureTests(PlayerPhysicsProbe & player, idUserCmdMgr & commands, idVarDef * scriptTicks, int cycle)
{
    // Continue the same native simulation after the initial movement/event checks.
    // Hold jump through landing, release/rearm it, then try jumping while crouched.
    const idVec3 start = player.GetMotionPhysics().GetOrigin();
    bool queuePassed = true, jumpPassed = true, heldPassed = true, crouchPassed = true, standPassed = true;
    float peakZ = start.z;
    for (int frame = 9; frame <= 88; ++frame)
    {
        usercmd_t input;
        if (frame <= 42 || frame == 44)
        {
            input.buttons = BUTTON_JUMP;
        }
        else if (frame >= 76 && frame <= 84)
        {
            input.buttons = BUTTON_CROUCH | BUTTON_JUMP;
        }
        input.clientGameMilliseconds = input.serverGameMilliseconds = FRAME_TO_MSEC(frame);
        commands.PutUserCmdForPlayer(0, input);
        queuePassed = commands.HasUserCmdForPlayer(0) &&
                      commands.GetNextUserCmdClientTime(0) == input.clientGameMilliseconds && queuePassed;
        gameReturn_t result{};
        ::game->RunFrame(commands, result);
        const auto & physics = player.GetMotionPhysics();
        const auto & position = physics.GetOrigin();
        const auto & velocity = physics.GetLinearVelocity();
        const auto & consumed = player.GetCommand();
        const bool floor = FloorContacts(physics), jumped = physics.HasJumped(), crouched = physics.IsCrouching();
        const float height = physics.GetClipModel()->GetBounds()[1].z;
        const bool pending = commands.HasUserCmdForPlayer(0);
        queuePassed = player.ticks == frame && consumed.buttons == input.buttons && consumed.forwardmove == 0 &&
                      consumed.rightmove == 0 && consumed.clientGameMilliseconds == FRAME_TO_MSEC(frame) &&
                      consumed.serverGameMilliseconds == FRAME_TO_MSEC(frame) && !pending &&
                      commands.readFrame[0] == frame - 1 && commands.writeFrame[0] == frame && queuePassed;
        // Native SlideMove uses averaged vertical velocity, giving a 128 units/s launch
        // and a 16-unit ballistic apex at gravity 512. Landing clips velocity at the floor.
        const int launch = frame < 44 ? 9 : 44;
        const int elapsedMs = FRAME_TO_MSEC(frame) - FRAME_TO_MSEC(launch - 1);
        const float elapsed = static_cast<float>(elapsedMs) * 0.001f;
        const float ballisticZ = start.z + 128.0f * elapsed - 0.5f * kFixtureGravity * elapsed * elapsed;
        const bool airborne = elapsedMs < 500;
        jumpPassed = jumped == (frame == 9 || frame == 44) && position.z >= CM_CLIP_EPSILON - 0.01f &&
                     position.z <= start.z + 16.05f && Near(position.x, start.x) && Near(position.y, start.y) &&
                     Near(velocity.x, 0.0f) && Near(velocity.y, 0.0f) && !physics.OnLadder() &&
                     physics.GetWaterLevel() == WATERLEVEL_NONE && physics.GetClipModel()->IsLinked() &&
                     (frame >= 76 || (Near(position.z, airborne ? ballisticZ : start.z) &&
                                      (airborne ? Near(velocity.z, 128.0f - kFixtureGravity * elapsed) : idMath::Fabs(velocity.z) < 1.0f) &&
                                      floor == !airborne)) &&
                     jumpPassed;
        if (frame <= 42 && position.z > peakZ)
        {
            peakZ = position.z;
        }
        if (frame >= 40 && frame <= 43)
        {
            heldPassed = floor && !jumped && Near(position.z, start.z) && velocity.LengthSqr() < 0.01f && heldPassed;
        }
        const bool expectCrouch = frame >= 76 && frame <= 84;
        crouchPassed = crouched == expectCrouch && Near(height, expectCrouch ? pm_crouchheight.GetFloat() : pm_normalheight.GetFloat()) &&
                       (frame < 76 || (floor && !jumped && Near(position.z, start.z) && velocity == vec3_zero)) && crouchPassed;
        if (frame >= 85)
        {
            standPassed = !crouched && floor && Near(height, pm_normalheight.GetFloat()) && standPassed;
        }
        const bool simulation = gameLocal.GetFrameNum() == frame && gameLocal.time == FRAME_TO_MSEC(frame) &&
                                scriptTicks != nullptr && *scriptTicks->value.floatPtr == 8.0f &&
                                gameLocal.FindEntity("logic_target") == nullptr && result.sessionCommand[0] == '\0' &&
                                gameLocal.sessionCommand.Length() == 0 && result.vibrationLow == 0 && result.vibrationHigh == 0;
        queuePassed = simulation && queuePassed;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_POSTURE cycle=%d frame=%d buttons=%d time=%d read=%d written=%d pending=%d x=%.3f y=%.3f z=%.3f vz=%.3f height=%.3f floor=%d jumped=%d crouched=%d script=%.0f\n",
                 cycle, frame, static_cast<int>(consumed.buttons), consumed.clientGameMilliseconds,
                 commands.readFrame[0], commands.writeFrame[0], static_cast<int>(pending),
                 static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z),
                 static_cast<double>(velocity.z), static_cast<double>(height), static_cast<int>(floor),
                 static_cast<int>(jumped), static_cast<int>(crouched),
                 static_cast<double>(scriptTicks != nullptr ? *scriptTicks->value.floatPtr : -1.0f));
    }
    bool passed = Check("player-posture-command-queue", queuePassed);
    passed = Check("player-jump-land", jumpPassed && Near(peakZ, start.z + 16.0f)) && passed;
    passed = Check("player-jump-held-release", heldPassed && jumpPassed) && passed;
    passed = Check("player-crouch-shape-jump", crouchPassed) && passed;
    passed = Check("player-stand-restore", standPassed && crouchPassed) && passed;
    return passed;
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
        if (idStr::Cmp(mode, "game-player-args") == 0 || idStr::Cmp(mode, "game-player-script") == 0 ||
            idStr::Cmp(mode, "game-player-command") == 0 || idStr::Cmp(mode, "game-player-state") == 0)
        {
            idDict args;
            PlayerArgs(args);
            if (idStr::Cmp(mode, "game-player-args") == 0)
            {
                args.Set("model", "unsupported.md5mesh");
            }
            gameLocal.SpawnEntityType(idPlayer::Type, &args);
            if (idStr::Cmp(mode, "game-player-command") == 0 || idStr::Cmp(mode, "game-player-state") == 0)
            {
                auto * commands = new (TAG_GAME) idUserCmdMgr;
                usercmd_t input;
                input.buttons = idStr::Cmp(mode, "game-player-command") == 0 ? BUTTON_ATTACK : BUTTON_JUMP;
                input.clientGameMilliseconds = input.serverGameMilliseconds = FRAME_TO_MSEC(1);
                commands->PutUserCmdForPlayer(0, input);
                gameReturn_t result{};
                ::game->RunFrame(*commands, result);
                delete commands;
            }
        }
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
        // The monster probes have completed their checks; prevent continued sliding
        // from leaving the bounded floor while the player finishes its posture checks.
        probe->BecomeInactive(TH_THINK | TH_PHYSICS);
        falling->BecomeInactive(TH_THINK | TH_PHYSICS);
        sliding->BecomeInactive(TH_THINK | TH_PHYSICS);
        // Native physics deactivation schedules a visual update. These probes have
        // no Present call to clear it; finish deactivation before the next frame.
        probe->BecomeInactive(TH_UPDATEVISUALS);
        falling->BecomeInactive(TH_UPDATEVISUALS);
        sliding->BecomeInactive(TH_UPDATEVISUALS);
        passed = PlayerPostureTests(*player, *commands, value, cycle) && passed;
        player->BecomeInactive(TH_THINK | TH_PHYSICS);
        player->BecomeInactive(TH_UPDATEVISUALS);
        idEntityPtr<idPlayer> nativePlayer;
        passed = NativePlayerTests(*commands, cycle, nativePlayer) && passed;
        passed = NativePlayerPostureTests(*nativePlayer.GetEntity(), *commands, cycle) && passed;
        passed = NativePlayerHeadroomTests(*nativePlayer.GetEntity(), *commands, cycle) && passed;
        Memory("ticked");
        ::game->MapShutdown();
        passed = Check("map-shutdown", gameLocal.GameState() == GAMESTATE_NOMAP && gameLocal.world == nullptr &&
                                       gameLocal.FindEntity("logic_probe") == nullptr && gameLocal.FindEntity("logic_target") == nullptr &&
                                       gameLocal.FindEntity("fall_probe") == nullptr && gameLocal.FindEntity("slide_probe") == nullptr &&
                                       gameLocal.FindEntity("player_physics_probe") == nullptr &&
                                       gameLocal.FindEntity("native_player") == nullptr && gameLocal.entities[0] == nullptr &&
                                       !nativePlayer.IsValid() && nativePlayer.GetEntity() == nullptr &&
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
                                             ps2::heap::GetStats(TAG_PHYSICS_CLIP_ENTITY).allocationCount == 0 &&
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
