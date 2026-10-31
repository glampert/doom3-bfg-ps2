/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __SYS_CVAR_H__
#define __SYS_CVAR_H__

extern idCVar	developer;

extern idCVar	g_cinematic;
extern idCVar	g_cinematicMaxSkipTime;

DEV_CVAR( extern idCVar	g_monsters );	// [PS2_D3BFG]
extern idCVar	g_decals;
extern idCVar	g_knockback;
extern idCVar	g_skill;
extern idCVar	g_gravity;
DEV_CVAR( extern idCVar	g_skipFX );	// [PS2_D3BFG]
extern idCVar	g_bloodEffects;
extern idCVar	g_projectileLights;
extern idCVar	g_muzzleFlash;

DEV_CVAR( extern idCVar	g_disasm );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugBounds );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugAnim );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugMove );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugDamage );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugWeapon );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugScript );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugMover );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugTriggers );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugCinematic );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_stopTime );	// [PS2_D3BFG]
extern idCVar	g_armorProtection;
// [PS2_D3BFG]: Multiplayer cvar.
#if !defined( ID_PS2 )
extern idCVar	g_armorProtectionMP;
#endif
extern idCVar	g_damageScale;
extern idCVar	g_useDynamicProtection;
extern idCVar	g_healthTakeTime;
extern idCVar	g_healthTakeAmt;
extern idCVar	g_healthTakeLimit;

DEV_CVAR( extern idCVar	g_showPVS );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showTargets );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showTriggers );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showCollisionWorld );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showCollisionModels );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showCollisionTraces );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_maxShowDistance );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showEntityInfo );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showviewpos );	// [PS2_D3BFG]
extern idCVar	g_showcamerainfo;
DEV_CVAR( extern idCVar	g_showTestModelFrame );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showActiveEntities );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_showEnemies );	// [PS2_D3BFG]

DEV_CVAR( extern idCVar	g_frametime );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_timeentities );	// [PS2_D3BFG]

DEV_CVAR( extern idCVar	ai_debugScript );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	ai_debugMove );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	ai_debugTrajectory );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	ai_testPredictPath );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	ai_showCombatNodes );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	ai_showPaths );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	ai_showObstacleAvoidance );	// [PS2_D3BFG]
extern idCVar	ai_blockedFailSafe;
DEV_CVAR( extern idCVar	ai_showHealth );	// [PS2_D3BFG]

extern idCVar	g_dvTime;
extern idCVar	g_dvAmplitude;
extern idCVar	g_dvFrequency;

extern idCVar	g_kickTime;
extern idCVar	g_kickAmplitude;
extern idCVar	g_blobTime;
extern idCVar	g_blobSize;

DEV_CVAR( extern idCVar	g_testHealthVision );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_editEntityMode );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_dragEntity );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_dragDamping );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_dragShowSelection );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_dropItemRotation );	// [PS2_D3BFG]

extern idCVar	g_vehicleVelocity;
extern idCVar	g_vehicleForce;
extern idCVar	g_vehicleSuspensionUp;
extern idCVar	g_vehicleSuspensionDown;
extern idCVar	g_vehicleSuspensionKCompress;
extern idCVar	g_vehicleSuspensionDamping;
extern idCVar	g_vehicleTireFriction;
DEV_CVAR( extern idCVar	g_vehicleDebug );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_debugShockwave );	// [PS2_D3BFG]
extern idCVar	g_enablePortalSky;

extern idCVar	ik_enable;
DEV_CVAR( extern idCVar	ik_debug );	// [PS2_D3BFG]

extern idCVar	af_useLinearTime;
extern idCVar	af_useImpulseFriction;
extern idCVar	af_useJointImpulseFriction;
extern idCVar	af_useSymmetry;
DEV_CVAR( extern idCVar	af_skipSelfCollision );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_skipLimits );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_skipFriction );	// [PS2_D3BFG]
extern idCVar	af_forceFriction;
extern idCVar	af_maxLinearVelocity;
extern idCVar	af_maxAngularVelocity;
DEV_CVAR( extern idCVar	af_timeScale );	// [PS2_D3BFG]
extern idCVar	af_jointFrictionScale;
extern idCVar	af_contactFrictionScale;
DEV_CVAR( extern idCVar	af_highlightBody );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_highlightConstraint );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showTimings );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showConstraints );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showConstraintNames );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showConstrainedBodies );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showPrimaryOnly );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showTrees );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showLimits );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showBodies );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showBodyNames );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showMass );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showTotalMass );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showInertia );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showVelocity );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_showActive );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	af_testSolid );	// [PS2_D3BFG]

DEV_CVAR( extern idCVar	rb_showTimings );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	rb_showBodies );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	rb_showMass );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	rb_showInertia );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	rb_showVelocity );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	rb_showActive );	// [PS2_D3BFG]

extern idCVar	pm_jumpheight;
extern idCVar	pm_stepsize;
extern idCVar	pm_crouchspeed;
extern idCVar	pm_walkspeed;
extern idCVar	pm_runspeed;
extern idCVar	pm_noclipspeed;
extern idCVar	pm_spectatespeed;
extern idCVar	pm_spectatebbox;
extern idCVar	pm_usecylinder;
extern idCVar	pm_minviewpitch;
extern idCVar	pm_maxviewpitch;
extern idCVar	pm_stamina;
extern idCVar	pm_staminathreshold;
extern idCVar	pm_staminarate;
extern idCVar	pm_crouchheight;
extern idCVar	pm_crouchviewheight;
extern idCVar	pm_normalheight;
extern idCVar	pm_normalviewheight;
extern idCVar	pm_deadheight;
extern idCVar	pm_deadviewheight;
extern idCVar	pm_crouchrate;
extern idCVar	pm_bboxwidth;
extern idCVar	pm_crouchbob;
extern idCVar	pm_walkbob;
extern idCVar	pm_runbob;
extern idCVar	pm_runpitch;
extern idCVar	pm_runroll;
extern idCVar	pm_bobup;
extern idCVar	pm_bobpitch;
extern idCVar	pm_bobroll;
extern idCVar	pm_thirdPersonRange;
extern idCVar	pm_thirdPersonHeight;
extern idCVar	pm_thirdPersonAngle;
extern idCVar	pm_thirdPersonClip;
extern idCVar	pm_thirdPerson;
extern idCVar	pm_thirdPersonDeath;
extern idCVar	pm_modelView;
extern idCVar	pm_airMsec;

extern idCVar	g_showPlayerShadow;
extern idCVar	g_showHud;
extern idCVar	g_showProjectilePct;
extern idCVar	g_showBrass;
extern idCVar	g_gun_x;
extern idCVar	g_gun_y;
extern idCVar	g_gun_z;
extern idCVar	g_gunScale;
extern idCVar	g_viewNodalX;
extern idCVar	g_viewNodalZ;
extern idCVar	g_fov;
DEV_CVAR( extern idCVar	g_testDeath );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_skipViewEffects );	// [PS2_D3BFG]
// [PS2_D3BFG]: Multiplayer cvar.
#if !defined( ID_PS2 )
extern idCVar   g_mpWeaponAngleScale;
#endif

DEV_CVAR( extern idCVar	g_testParticle );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_testParticleName );	// [PS2_D3BFG]

DEV_CVAR( extern idCVar	g_testPostProcess );	// [PS2_D3BFG]

DEV_CVAR( extern idCVar	g_testModelRotate );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_testModelAnimate );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_testModelBlend );	// [PS2_D3BFG]
extern idCVar	g_flushSave;

DEV_CVAR( extern idCVar	g_enableSlowmo );	// [PS2_D3BFG]
extern idCVar	g_slowmoStepRate;
DEV_CVAR( extern idCVar	g_testFullscreenFX );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_testHelltimeFX );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_testMultiplayerFX );	// [PS2_D3BFG]
// [PS2_D3BFG]: Multiplayer cvar.
#if !defined( ID_PS2 )
extern idCVar	g_moveableDamageScale;
#endif
DEV_CVAR( extern idCVar	g_testBloomIntensity );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	g_testBloomNumPasses );	// [PS2_D3BFG]

extern idCVar	g_grabberHoldSeconds;
extern idCVar	g_grabberEnableShake;
extern idCVar	g_grabberRandomMotion;
extern idCVar	g_grabberHardStop;
extern idCVar	g_grabberDamping;

extern idCVar	g_xp_bind_run_once;

DEV_CVAR( extern idCVar	aas_test );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_showAreas );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_showPath );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_showFlyPath );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_showWallEdges );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_showHideArea );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_pullPlayer );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_randomPullPlayer );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_goalArea );	// [PS2_D3BFG]
DEV_CVAR( extern idCVar	aas_showPushIntoArea );	// [PS2_D3BFG]

// [PS2_D3BFG]: Multiplayer cvar.
#if !defined( ID_PS2 )
extern idCVar	net_clientPredictGUI;
#endif

extern idCVar	si_timeLimit;
extern idCVar	si_fragLimit;
extern idCVar	si_spectators;

extern idCVar si_flagDropTimeLimit;
extern idCVar si_midnight;

// [PS2_D3BFG]: CTF flag carrying does not exist on the PS2.
#if !defined( ID_PS2 )
extern idCVar g_flagAttachJoint;
extern idCVar g_flagAttachOffsetX;
extern idCVar g_flagAttachOffsetY;
extern idCVar g_flagAttachOffsetZ;
extern idCVar g_flagAttachAngleX;
extern idCVar g_flagAttachAngleY;
extern idCVar g_flagAttachAngleZ;
#endif

// [PS2_D3BFG]: Multiplayer cvar.
#if !defined( ID_PS2 )
extern idCVar g_CTFArrows;
#endif

// [PS2_D3BFG]: Multiplayer cvar.
#if !defined( ID_PS2 )
extern idCVar	net_clientSelfSmoothing;
#endif

#endif /* !__SYS_CVAR_H__ */
