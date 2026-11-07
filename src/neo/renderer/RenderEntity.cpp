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

#pragma hdrstop
#include "../idlib/precompiled.h"

#include "tr_local.h"

idRenderEntityLocal::idRenderEntityLocal() {
	memset( &parms, 0, sizeof( parms ) );
	memset( modelMatrix, 0, sizeof( modelMatrix ) );

	world					= NULL;
	index					= 0;
	lastModifiedFrameNum	= 0;
	archived				= false;
	dynamicModel			= NULL;
	dynamicModelFrameCount	= 0;
	cachedDynamicModel		= NULL;
	localReferenceBounds	= bounds_zero;
	globalReferenceBounds	= bounds_zero;
	viewCount				= 0;
	viewEntity				= NULL;
	decals					= NULL;
	overlays				= NULL;
	entityRefs				= NULL;
	firstInteraction		= NULL;
	lastInteraction			= NULL;
	needsPortalSky			= false;
}

void idRenderEntityLocal::FreeRenderEntity() {
}

void idRenderEntityLocal::UpdateRenderEntity( const renderEntity_t *re, bool forceUpdate ) {
}

void idRenderEntityLocal::GetRenderEntity( renderEntity_t *re ) {
}

void idRenderEntityLocal::ForceUpdate() {
}

int idRenderEntityLocal::GetIndex() {
	return index;
}

void idRenderEntityLocal::ProjectOverlay( const idPlane localTextureAxis[2], const idMaterial *material ) {
}
void idRenderEntityLocal::RemoveDecals() {
}

//======================================================================

idRenderLightLocal::idRenderLightLocal() {
	memset( &parms, 0, sizeof( parms ) );
	memset( lightProject, 0, sizeof( lightProject ) );

	lightHasMoved			= false;
	world					= NULL;
	index					= 0;
	areaNum					= 0;
	lastModifiedFrameNum	= 0;
	archived				= false;
	lightShader				= NULL;
	falloffImage			= NULL;
	globalLightOrigin		= vec3_zero;
	viewCount				= 0;
	viewLight				= NULL;
	references				= NULL;
	foggedPortals			= NULL;
	firstInteraction		= NULL;
	lastInteraction			= NULL;

	baseLightProject.Zero();
	inverseBaseLightProject.Zero();
}

void idRenderLightLocal::FreeRenderLight() {
}
void idRenderLightLocal::UpdateRenderLight( const renderLight_t *re, bool forceUpdate ) {
}
void idRenderLightLocal::GetRenderLight( renderLight_t *re ) {
}
void idRenderLightLocal::ForceUpdate() {
}
int idRenderLightLocal::GetIndex() {
	return index;
}

#if defined( ID_PS2 )
// [PS2_D3BFG]: Moved from tr_frontend_addmodels.cpp, which the PS2 renderer replaces
// (RENDERING_ARCHITECTURE.md 5.2): traces and entity def updates still instantiate dynamic models
// through these. The PS2 creates no interactions, so the dynamic model clear frees no surfaces.
static const float CHECK_BOUNDS_EPSILON = 1.0f;

/*
==================
R_ClearEntityDefDynamicModel

If we know the reference bounds stays the same, we
only need to do this on entity update, not the full
R_FreeEntityDefDerivedData
==================
*/
void R_ClearEntityDefDynamicModel( idRenderEntityLocal *def ) {
	// clear the dynamic model if present
	if ( def->dynamicModel ) {
		// this is copied from cachedDynamicModel, so it doesn't need to be freed
		def->dynamicModel = NULL;
	}
	def->dynamicModelFrameCount = 0;
}

/*
==================
R_IssueEntityDefCallback
==================
*/
bool R_IssueEntityDefCallback( idRenderEntityLocal *def ) {
#if !defined( ID_PS2 ) || PS2_D3BFG_DEBUG	// [PS2_D3BFG]: Developer cvars are debug-only on the PS2.
	idBounds oldBounds = def->localReferenceBounds;

#endif
	def->archived = false;		// will need to be written to the demo file

	bool update;
	if ( tr.viewDef != NULL ) {
		update = def->parms.callback( &def->parms, &tr.viewDef->renderView );
	} else {
		update = def->parms.callback( &def->parms, NULL );
	}
	tr.pc.c_entityDefCallbacks++;

	if ( def->parms.hModel == NULL ) {
		common->Error( "R_IssueEntityDefCallback: dynamic entity callback didn't set model" );
	}

#if !defined( ID_PS2 ) || PS2_D3BFG_DEBUG	// [PS2_D3BFG]: Developer cvars are debug-only on the PS2.
	if ( r_checkBounds.GetBool() ) {
		if (	oldBounds[0][0] > def->localReferenceBounds[0][0] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][1] > def->localReferenceBounds[0][1] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][2] > def->localReferenceBounds[0][2] + CHECK_BOUNDS_EPSILON ||
				oldBounds[1][0] < def->localReferenceBounds[1][0] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][1] < def->localReferenceBounds[1][1] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][2] < def->localReferenceBounds[1][2] - CHECK_BOUNDS_EPSILON ) {
			common->Printf( "entity %i callback extended reference bounds\n", def->index );
		}
	}

#endif

	return update;
}

/*
===================
R_EntityDefDynamicModel

This is also called by the game code for idRenderWorldLocal::ModelTrace(), and idRenderWorldLocal::Trace() which is bad for performance...

Issues a deferred entity callback if necessary.
If the model isn't dynamic, it returns the original.
Returns the cached dynamic model if present, otherwise creates it.
===================
*/
idRenderModel *R_EntityDefDynamicModel( idRenderEntityLocal *def ) {
	if ( def->dynamicModelFrameCount == tr.frameCount ) {
		return def->dynamicModel;
	}

	// allow deferred entities to construct themselves
	bool callbackUpdate;
	if ( def->parms.callback != NULL ) {
		SCOPED_PROFILE_EVENT( "R_IssueEntityDefCallback" );
		callbackUpdate = R_IssueEntityDefCallback( def );
	} else {
		callbackUpdate = false;
	}

	idRenderModel *model = def->parms.hModel;

	if ( model == NULL ) {
		common->Error( "R_EntityDefDynamicModel: NULL model" );
		return NULL;
	}

	if ( model->IsDynamicModel() == DM_STATIC ) {
		def->dynamicModel = NULL;
		def->dynamicModelFrameCount = 0;
		return model;
	}

	// continously animating models (particle systems, etc) will have their snapshot updated every single view
	if ( callbackUpdate || ( model->IsDynamicModel() == DM_CONTINUOUS && def->dynamicModelFrameCount != tr.frameCount ) ) {
		R_ClearEntityDefDynamicModel( def );
	}

	// if we don't have a snapshot of the dynamic model, generate it now
	if ( def->dynamicModel == NULL ) {

		SCOPED_PROFILE_EVENT( "InstantiateDynamicModel" );

		// instantiate the snapshot of the dynamic model, possibly reusing memory from the cached snapshot
		def->cachedDynamicModel = model->InstantiateDynamicModel( &def->parms, tr.viewDef, def->cachedDynamicModel );

#if !defined( ID_PS2 ) || PS2_D3BFG_DEBUG	// [PS2_D3BFG]: Developer cvars are debug-only on the PS2.
		if ( def->cachedDynamicModel != NULL && r_checkBounds.GetBool() ) {
			idBounds b = def->cachedDynamicModel->Bounds();
			if (	b[0][0] < def->localReferenceBounds[0][0] - CHECK_BOUNDS_EPSILON ||
					b[0][1] < def->localReferenceBounds[0][1] - CHECK_BOUNDS_EPSILON ||
					b[0][2] < def->localReferenceBounds[0][2] - CHECK_BOUNDS_EPSILON ||
					b[1][0] > def->localReferenceBounds[1][0] + CHECK_BOUNDS_EPSILON ||
					b[1][1] > def->localReferenceBounds[1][1] + CHECK_BOUNDS_EPSILON ||
					b[1][2] > def->localReferenceBounds[1][2] + CHECK_BOUNDS_EPSILON ) {
				common->Printf( "entity %i dynamic model exceeded reference bounds\n", def->index );
			}
		}

#endif

		def->dynamicModel = def->cachedDynamicModel;
		def->dynamicModelFrameCount = tr.frameCount;
	}

	// set model depth hack value
	if ( def->dynamicModel != NULL && model->DepthHack() != 0.0f && tr.viewDef != NULL ) {
		idPlane eye, clip;
		idVec3 ndc;
		R_TransformModelToClip( def->parms.origin, tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );
		R_TransformClipToDevice( clip, ndc );
		def->parms.modelDepthHack = model->DepthHack() * ( 1.0f - ndc.z );
	} else {
		def->parms.modelDepthHack = 0.0f;
	}

	return def->dynamicModel;
}

#endif
