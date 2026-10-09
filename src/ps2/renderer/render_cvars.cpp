// ================================================================================================
// File: render_cvars.cpp
// Brief: Preserve controls consumed by retained renderer frontend code without enabling a device.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include <idlib/precompiled.h>
#include <renderer/tr_local.h>

// Native defaults, flags, bounds and completions remain valid metadata for the later logical frontend.
// The interface stubs reject work independently of these controls.
idCVar r_useLightPortalFlow("r_useLightPortalFlow", "1", CVAR_RENDERER | CVAR_BOOL, "use a more precise area reference determination");
idCVar r_checkBounds("r_checkBounds", "0", CVAR_RENDERER | CVAR_BOOL, "compare all surface bounds with precalculated ones");
idCVar r_useConstantMaterials("r_useConstantMaterials", "1", CVAR_RENDERER | CVAR_BOOL, "use pre-calculated material registers if possible");
idCVar r_useSilRemap("r_useSilRemap", "1", CVAR_RENDERER | CVAR_BOOL, "consider verts with the same XYZ, but different ST the same for shadows");
idCVar r_useNodeCommonChildren("r_useNodeCommonChildren", "1", CVAR_RENDERER | CVAR_BOOL, "stop pushing reference bounds early when possible");
idCVar r_useShadowSurfaceScissor("r_useShadowSurfaceScissor", "1", CVAR_RENDERER | CVAR_BOOL, "scissor shadows by the scissor rect of the interaction surfaces");
idCVar r_useCachedDynamicModels("r_useCachedDynamicModels", "1", CVAR_RENDERER | CVAR_BOOL, "cache snapshots of dynamic models");
idCVar r_znear("r_znear", "3", CVAR_RENDERER | CVAR_FLOAT, "near Z clip plane distance", 0.001f, 200.0f);
idCVar r_jitter("r_jitter", "0", CVAR_RENDERER | CVAR_BOOL, "randomly subpixel jitter the projection matrix");
idCVar r_skipSuppress("r_skipSuppress", "0", CVAR_RENDERER | CVAR_BOOL, "ignore the per-view suppressions");
idCVar r_skipDeforms("r_skipDeforms", "0", CVAR_RENDERER | CVAR_BOOL, "leave all deform materials in their original state");
idCVar r_skipFrontEnd("r_skipFrontEnd", "0", CVAR_RENDERER | CVAR_BOOL, "bypasses all front end work, but 2D gui rendering still draws");
idCVar r_skipUpdates("r_skipUpdates", "0", CVAR_RENDERER | CVAR_BOOL, "1 = don't accept any entity or light updates, making everything static");
idCVar r_skipDecals("r_skipDecals", "0", CVAR_RENDERER | CVAR_BOOL, "skip decal surfaces");
idCVar r_skipOverlays("r_skipOverlays", "0", CVAR_RENDERER | CVAR_BOOL, "skip overlay surfaces");
idCVar r_skipSubviews("r_skipSubviews", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = don't render any gui elements on surfaces");
idCVar r_skipGuiShaders("r_skipGuiShaders", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all gui elements on surfaces, 2 = skip drawing but still handle events, 3 = draw but skip events", 0, 3, idCmdSystem::ArgCompletion_Integer<0, 3>);
idCVar r_skipParticles("r_skipParticles", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all particle systems", 0, 1, idCmdSystem::ArgCompletion_Integer<0, 1>);
idCVar r_useLightPortalCulling("r_useLightPortalCulling", "1", CVAR_RENDERER | CVAR_INTEGER, "0 = none, 1 = cull frustum corners to plane, 2 = exact clip the frustum faces", 0, 2, idCmdSystem::ArgCompletion_Integer<0, 2>);
idCVar r_useLightAreaCulling("r_useLightAreaCulling", "1", CVAR_RENDERER | CVAR_BOOL, "0 = off, 1 = on");
idCVar r_useLightScissors("r_useLightScissors", "3", CVAR_RENDERER | CVAR_INTEGER, "0 = no scissor, 1 = non-clipped scissor, 2 = near-clipped scissor, 3 = fully-clipped scissor", 0, 3, idCmdSystem::ArgCompletion_Integer<0, 3>);
idCVar r_useEntityPortalCulling("r_useEntityPortalCulling", "1", CVAR_RENDERER | CVAR_INTEGER, "0 = none, 1 = cull frustum corners to plane, 2 = exact clip the frustum faces", 0, 2, idCmdSystem::ArgCompletion_Integer<0, 2>);
idCVar r_subviewOnly("r_subviewOnly", "0", CVAR_RENDERER | CVAR_BOOL, "1 = don't render main view, allowing subviews to be debugged");
idCVar r_flareSize("r_flareSize", "1", CVAR_RENDERER | CVAR_FLOAT, "scale the flare deforms from the material def");
idCVar r_skipPrelightShadows("r_skipPrelightShadows", "0", CVAR_RENDERER | CVAR_BOOL, "skip the dmap generated static shadow volumes");
idCVar r_useShadowDepthBounds("r_useShadowDepthBounds", "1", CVAR_RENDERER | CVAR_BOOL, "use depth bounds test on individual shadow volumes to reduce shadow fill");
idCVar r_screenFraction("r_screenFraction", "100", CVAR_RENDERER | CVAR_INTEGER, "for testing fill rate, the resolution of the entire screen can be changed");
idCVar r_usePortals("r_usePortals", "1", CVAR_RENDERER | CVAR_BOOL, " 1 = use portals to perform area culling, otherwise draw everything");
idCVar r_singleLight("r_singleLight", "-1", CVAR_RENDERER | CVAR_INTEGER, "suppress all but one light");
idCVar r_singleEntity("r_singleEntity", "-1", CVAR_RENDERER | CVAR_INTEGER, "suppress all but one entity");
idCVar r_singleSurface("r_singleSurface", "-1", CVAR_RENDERER | CVAR_INTEGER, "suppress all but one surface on each entity");
idCVar r_singleArea("r_singleArea", "0", CVAR_RENDERER | CVAR_BOOL, "only draw the portal area the view is actually in");
idCVar r_lightAllBackFaces("r_lightAllBackFaces", "0", CVAR_RENDERER | CVAR_BOOL, "light all the back faces, even when they would be shadowed");
idCVar r_showUpdates("r_showUpdates", "0", CVAR_RENDERER | CVAR_BOOL, "report entity and light updates and ref counts");
idCVar r_showLightScissors("r_showLightScissors", "0", CVAR_RENDERER | CVAR_BOOL, "show light scissor rectangles");
idCVar r_useEntityCallbacks("r_useEntityCallbacks", "1", CVAR_RENDERER | CVAR_BOOL, "if 0, issue the callback immediately at update time, rather than defering");
idCVar r_showSkel("r_showSkel", "0", CVAR_RENDERER | CVAR_INTEGER, "draw the skeleton when model animates, 1 = draw model with skeleton, 2 = draw skeleton only", 0, 2, idCmdSystem::ArgCompletion_Integer<0, 2>);
idCVar r_jointNameScale("r_jointNameScale", "0.02", CVAR_RENDERER | CVAR_FLOAT, "size of joint names when r_showskel is set to 1");
idCVar r_jointNameOffset("r_jointNameOffset", "0.5", CVAR_RENDERER | CVAR_FLOAT, "offset of joint names when r_showskel is set to 1");
idCVar r_debugArrowStep("r_debugArrowStep", "120", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "step size of arrow cone line rotation in degrees", 0, 120);
idCVar r_materialOverride("r_materialOverride", "", CVAR_RENDERER, "overrides all materials", idCmdSystem::ArgCompletion_Decl<DECL_MATERIAL>);
idCVar stereoRender_swapEyes("stereoRender_swapEyes", "0", CVAR_BOOL | CVAR_ARCHIVE, "reverse eye adjustments");
