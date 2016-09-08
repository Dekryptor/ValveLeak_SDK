//========= Copyright � 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "vcollide_parse.h"
#include "filesystem.h"
#include "engine/IStaticPropMgr.h"
#include "solidsetdefaults.h"
#include "engine/IEngineSound.h"
#include "vphysics_sound.h"
#include "movevars_shared.h"
#include "engine/ivmodelinfo.h"
#include "fx.h"
#include "tier0/vprof.h"
#include "c_world.h"

// file system interface
extern IFileSystem *filesystem;

ConVar	cl_phys_timescale( "cl_phys_timescale", "1", 0, "Sets the scale of time for client-side physics (ragdolls)" );

extern ConVar phys_rolling_drag;

//FIXME: Replicated from server end, consolidate?

enum
{
	TOUCH_START=0,
	TOUCH_END,
};

struct touchevent_t
{
	CBaseEntity *pEntity0;
	CBaseEntity *pEntity1;
	int			touchType;
};

const float FLUID_TIME_MAX = 2.0f; // keep track of last time hitting fluid for up to 2 seconds 
struct fluidevent_t
{
	CBaseEntity		*pEntity;
	float			impactTime;
};

extern IVEngineClient *engine;

class CCollisionEvent : public IPhysicsCollisionEvent, public IPhysicsCollisionSolver
{
public:
	CCollisionEvent( void );

	void	ObjectSound( int index, vcollisionevent_t *pEvent );
	void	PreCollision( vcollisionevent_t *pEvent ) {}
	void	PostCollision( vcollisionevent_t *pEvent );
	void	Friction( IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit, IPhysicsCollisionData *pData );

	void	BufferTouchEvents( bool enable ) { m_bBufferTouchEvents = enable; }

	void	StartTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData );
	void	EndTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData );

	void	FluidStartTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid );
	void	FluidEndTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid );
	void	PostSimulationFrame() {}

	virtual void ObjectEnterTrigger( IPhysicsObject *pTrigger, IPhysicsObject *pObject ) {}
	virtual void ObjectLeaveTrigger( IPhysicsObject *pTrigger, IPhysicsObject *pObject ) {}

	float	DeltaTimeSinceLastFluid( CBaseEntity *pEntity );
	void	FrameUpdate( void );

	void	UpdateFluidEvents( void );
	void	UpdateTouchEvents( void );

	// IPhysicsCollisionSolver
	int		ShouldCollide( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1 );
	int		ShouldSolvePenetration( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1, float dt );

#if 0
	friction_t *FindFriction( CBaseEntity *pObject )
	void ShutdownFriction( friction_t &friction )
	void UpdateFriction( void )

private:
	friction_t	m_current[8];
#endif

private:
	
	void	AddTouchEvent( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1, int touchType );
	void	DispatchStartTouch( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1 );
	void	DispatchEndTouch( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1 );

	CUtlVector<fluidevent_t>	m_fluidEvents;
	CUtlVector<touchevent_t>	m_touchEvents;
	bool						m_bBufferTouchEvents;
};

CCollisionEvent g_Collisions;

bool PhysicsDLLInit( CreateInterfaceFn physicsFactory )
{
	if (!(physics = (IPhysics *)physicsFactory( VPHYSICS_INTERFACE_VERSION, NULL )) ||
		!(physprops = (IPhysicsSurfaceProps *)physicsFactory( VPHYSICS_SURFACEPROPS_INTERFACE_VERSION, NULL )) ||
		!(physcollision = (IPhysicsCollision *)physicsFactory( VPHYSICS_COLLISION_INTERFACE_VERSION, NULL )))
	{
		return false;
	}

	PhysParseSurfaceData( "scripts/surfaceproperties.txt", physprops, filesystem );
	return true;
}

void PhysicsLevelInit( void )
{
	physenv = physics->CreateEnvironment();
	assert( physenv );

	physenv->SetGravity( Vector(0, 0, -sv_gravity.GetFloat() ) );
	physenv->SetSimulationTimestep( 0.015 ); // 15 ms per tick
	physenv->SetCollisionEventHandler( &g_Collisions );
	physenv->SetCollisionSolver( &g_Collisions );

	g_PhysWorldObject = PhysCreateWorld_Shared( GetClientWorldEntity(), modelinfo->GetVCollide(1), g_PhysDefaultObjectParams );

	staticpropmgr->CreateVPhysicsRepresentations( physenv, &g_SolidSetup, NULL );
}

void PhysicsReset()
{
	if ( !physenv )
		return;

	physenv->ResetSimulationClock();
}


ConVar cl_ragdoll_collide( "cl_ragdoll_collide", "0" );

int CCollisionEvent::ShouldCollide( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1 )
{
	C_BaseEntity *pEntity0 = static_cast<C_BaseEntity *>(pGameData0);
	C_BaseEntity *pEntity1 = static_cast<C_BaseEntity *>(pGameData1);

	if ( !pEntity0 || !pEntity1 )
		return 1;

	unsigned short gameFlags0 = pObj0->GetGameFlags();
	unsigned short gameFlags1 = pObj1->GetGameFlags();

	if ( pEntity0 == pEntity1 )
	{
		// allow all-or-nothing per-entity disable
		if ( (gameFlags0 | gameFlags1) & FVPHYSICS_NO_SELF_COLLISIONS )
			return 0;

		// otherwise sub-object collisions must be explicitly disabled on a per-physics-object basis
		return 1;
	}
	// Obey collision group rules
//	Assert(GameRules());
	if( !GameRules() )
		return 0; // VXP: Yes, we can

	if ( GameRules() )
	{
		if (!GameRules()->ShouldCollide( pEntity0->GetCollisionGroup(), pEntity1->GetCollisionGroup() ))
			return 0;
	}

	if ( (pObj0->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL) && (pObj1->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL) )
	{
		if ( !cl_ragdoll_collide.GetBool() )
			return 0;
	}

	return 1;
}

int CCollisionEvent::ShouldSolvePenetration( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1, float dt )
{
	// solve it yourself here and return 0, or have the default implementation do it
	return 1;
}


// This just encloses a change I (MikeD) made, in case it causes problems.
//#define DECAL_FIX


// A class that implements an IClientSystem for physics
class CPhysicsSystem : public CAutoGameSystem
{
public:
	// HACKHACK: PhysicsDLLInit() is called explicitly because it requires a parameter
	virtual bool Init();
	virtual void Shutdown();

	// Level init, shutdown
	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();

	// The level is shutdown in two parts
	virtual void LevelShutdownPreEntity();
	
	virtual void LevelShutdownPostEntity();

	// Called before rendering
	virtual void PreRender ( );

	void AddImpactSound( void *pGameData, IPhysicsObject *pObject, int surfaceProps, float volume );

	virtual void Update( float frametime );

	void PhysicsSimulate();

private:
	physicssound::soundlist_t m_impactSounds;
};

static CPhysicsSystem g_PhysicsSystem;
// singleton to hook into the client system
IGameSystem *PhysicsGameSystem( void )
{
	return &g_PhysicsSystem;
}


// HACKHACK: PhysicsDLLInit() is called explicitly because it requires a parameter
bool CPhysicsSystem::Init()
{
	return true;
}

void CPhysicsSystem::Shutdown()
{
}

// Level init, shutdown
void CPhysicsSystem::LevelInitPreEntity( void )
{
	m_impactSounds.RemoveAll();
}

void CPhysicsSystem::LevelInitPostEntity( void )
{
	PhysicsLevelInit();
}

// The level is shutdown in two parts
void CPhysicsSystem::LevelShutdownPreEntity()
{
	if ( physenv )
	{
		// we may have deleted multiple objects including the world by now, so 
		// don't try to wake them up
		physenv->SetQuickDelete( true );
	}
}

void CPhysicsSystem::LevelShutdownPostEntity()
{
	if ( physenv )
	{
		// environment destroys all objects
		// entities are gone, so this is safe now
		physics->DestroyEnvironment( physenv );
	}

	physenv = NULL;
	g_PhysWorldObject = NULL;
}

// Called before rendering
void CPhysicsSystem::PreRender ( )
{
}


void CPhysicsSystem::AddImpactSound( void *pGameData, IPhysicsObject *pObject, int surfaceProps, float volume )
{
	physicssound::AddImpactSound( m_impactSounds, pGameData, SOUND_FROM_WORLD, CHAN_STATIC, pObject, surfaceProps, volume );
}


void CPhysicsSystem::Update( float frametime )
{
	#ifndef DECAL_FIX
		PhysicsSimulate();
	#endif
}


void CPhysicsSystem::PhysicsSimulate()
{
	VPROF_BUDGET( "CPhysicsSystem::PhysicsSimulate", VPROF_BUDGETGROUP_PHYSICS );
	float frametime = gpGlobals->frametime;

	if ( physenv )
	{
		g_Collisions.BufferTouchEvents( true );
		physenv->Simulate( frametime * cl_phys_timescale.GetFloat() );

		int activeCount = physenv->GetActiveObjectCount();
		IPhysicsObject **pActiveList = NULL;
		if ( activeCount )
		{
			pActiveList = (IPhysicsObject **)stackalloc( sizeof(IPhysicsObject *)*activeCount );
			physenv->GetActiveObjects( pActiveList );

			for ( int i = 0; i < activeCount; i++ )
			{
				C_BaseEntity *pEntity = reinterpret_cast<C_BaseEntity *>(pActiveList[i]->GetGameData());
				if ( pEntity )
				{
					pEntity->VPhysicsUpdate( pActiveList[i] );
				}
			}
		}
		
		g_Collisions.BufferTouchEvents( false );
		g_Collisions.FrameUpdate();
	}
	physicssound::PlayImpactSounds( m_impactSounds );
}


void PhysicsSimulate()
{
	#ifdef DECAL_FIX
		g_PhysicsSystem.PhysicsSimulate();
	#endif
}



CCollisionEvent::CCollisionEvent( void ) 
{ 
}

void CCollisionEvent::ObjectSound( int index, vcollisionevent_t *pEvent )
{
	IPhysicsObject *pObject = pEvent->pObjects[index];
	if ( !pObject || pObject->IsStatic() )
		return;

	float speed = pEvent->collisionSpeed * pEvent->collisionSpeed;
	int surfaceProps = pEvent->surfaceProps[index];

	void *pGameData = pObject->GetGameData();
		
	if ( pGameData )
	{
		float volume = speed * (1.0f/(320.0f*320.0f));	// max volume at 320 in/s
		
		if ( volume > 1.0f )
			volume = 1.0f;

		if ( surfaceProps >= 0 )
		{
			g_PhysicsSystem.AddImpactSound( pGameData, pObject, surfaceProps, volume );
		}
	}
}

void CCollisionEvent::PostCollision( vcollisionevent_t *pEvent )
{
	int i;

	for ( i = 0; i < 2; i++ )
	{
		IPhysicsObject *pObject = pEvent->pObjects[i];
		if ( pObject )
		{
			if ( pEvent->deltaCollisionTime < 1.0 && phys_rolling_drag.GetBool() )
			{
				PhysApplyRollingDrag( pObject, pEvent->deltaCollisionTime );
			}
		}
	}

	if ( pEvent->deltaCollisionTime > 0.1f && pEvent->collisionSpeed > 70 )
	{
		ObjectSound( 0, pEvent );
		ObjectSound( 1, pEvent );
	}
}

void CCollisionEvent::Friction( IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit, IPhysicsCollisionData * )
{
#if 0
	if ( energy < 0.05f || surfaceProps < 0 )
		return;

	CBaseEntity *pEntity = (CBaseEntity *)pObject->GetGameData();
	if ( pEntity )
	{
		energy *= energy;
		float volume = energy * (1.0f/(10.0f*10.0f));
		
		// cut out the quiet sounds
		// UNDONE: Separate threshold for starting a sound vs. continuing?
		if ( volume > (1.0f/128.0f) )
		{
			friction_t *pFriction = FindFriction( pEntity );
			if ( !pFriction )
				return;

			if ( volume > 1.0f )
				volume = 1.0f;
			else if ( volume < 0.05f )
				volume = 0.05f;

			surfacedata_t *psurf = physprops->GetSurfaceData( surfaceProps );
			int offset = 0;
			if ( psurf->scrapeCount )
			{
				offset = random->RandomLong(0, psurf->scrapeCount-1);
			}
			const char *pSound = physprops->GetString( psurf->scrape, offset );

			CSoundParameters params;
			if ( !CBaseEntity::GetSoundParameters( pSound, params ) )
				return;

			if ( !pFriction->pObject )
			{
				pFriction->pObject = pEntity;
				pFriction->patch = CSoundEnvelopeController::GetController().SoundCreate( pEntity->pev, CHAN_BODY, params.soundname, ATTN_STATIC );
				CSoundEnvelopeController::GetController().Play( pFriction->patch, params.volume * volume, params.pitch );
//					engine->AlertMessage( at_console, "Scrape Start %s \n", STRING(pEntity->m_iClassname) );
			}
			else
			{
				float pitch = (volume * (params.pitchhigh - params.pitchlow)) + params.pitchlow;
				CSoundEnvelopeController::GetController().SoundChangeVolume( pFriction->patch, params.volume * volume, 0.1f );
				CSoundEnvelopeController::GetController().SoundChangePitch( pFriction->patch, pitch, 0.1f );
			}

			pFriction->update = gpGlobals->time;
		}
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollisionEvent::FrameUpdate( void )
{
	UpdateTouchEvents();
	UpdateFluidEvents();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollisionEvent::UpdateTouchEvents( void )
{
	for ( int i = 0; i < m_touchEvents.Count(); i++ )
	{
		const touchevent_t &event = m_touchEvents[i];
		if ( event.touchType == TOUCH_START )
		{
			DispatchStartTouch( event.pEntity0, event.pEntity1 );
		}
		else
		{
			// TOUCH_END
			DispatchEndTouch( event.pEntity0, event.pEntity1 );
		}
	}

	m_touchEvents.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity0 - 
//			*pEntity1 - 
//			touchType - 
//-----------------------------------------------------------------------------
void CCollisionEvent::AddTouchEvent( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1, int touchType )
{
	if ( !pEntity0 || !pEntity1 )
		return;

	int index = m_touchEvents.AddToTail();
	touchevent_t &event = m_touchEvents[index];
	event.pEntity0 = pEntity0;
	event.pEntity1 = pEntity1;
	event.touchType = touchType;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject1 - 
//			*pObject2 - 
//			*pTouchData - 
//-----------------------------------------------------------------------------
void CCollisionEvent::StartTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData )
{
	C_BaseEntity *pEntity1 = static_cast<C_BaseEntity *>(pObject1->GetGameData());
	C_BaseEntity *pEntity2 = static_cast<C_BaseEntity *>(pObject2->GetGameData());

	if ( !m_bBufferTouchEvents )
	{
		DispatchStartTouch( pEntity1, pEntity2 );
	}
	else
	{
		AddTouchEvent( pEntity1, pEntity2, TOUCH_START );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity0 - 
//			*pEntity1 - 
//-----------------------------------------------------------------------------
void CCollisionEvent::DispatchStartTouch( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1 )
{
	touchlink_t *link;
	link = pEntity0->PhysicsMarkEntityAsTouched( pEntity1 );
	if ( link )
	{
		// mark these links as event driven so they aren't untouched the next frame
		// when the physics doesn't refresh them
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
	link = pEntity1->PhysicsMarkEntityAsTouched( pEntity0 );
	if ( link )
	{
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject1 - 
//			*pObject2 - 
//			*pTouchData - 
//-----------------------------------------------------------------------------
void CCollisionEvent::EndTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData )
{
	C_BaseEntity *pEntity1 = static_cast<C_BaseEntity *>(pObject1->GetGameData());
	C_BaseEntity *pEntity2 = static_cast<C_BaseEntity *>(pObject2->GetGameData());
	
	if ( !m_bBufferTouchEvents )
	{
		DispatchEndTouch( pEntity1, pEntity2 );
	}
	else
	{
		AddTouchEvent( pEntity1, pEntity2, TOUCH_END );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity0 - 
//			*pEntity1 - 
//-----------------------------------------------------------------------------
void CCollisionEvent::DispatchEndTouch( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1 )
{
	// frees the event-driven touchlinks
	pEntity0->PhysicsNotifyOtherOfUntouch( pEntity0, pEntity1 );
	pEntity1->PhysicsNotifyOtherOfUntouch( pEntity1, pEntity0 );
}

#if 0
friction_t *CCollisionEvent::FindFriction( CBaseEntity *pObject )
{
	friction_t *pFree = NULL;

	for ( int i = 0; i < ARRAYSIZE(m_current); i++ )
	{
		if ( !m_current[i].pObject && !pFree )
			pFree = &m_current[i];

		if ( m_current[i].pObject == pObject )
			return &m_current[i];
	}

	return pFree;
}

void CCollisionEvent::ShutdownFriction( friction_t &friction )
{
//		engine->AlertMessage( at_console, "Scrape Stop %s \n", STRING(friction.pObject->m_iClassname) );
	CSoundEnvelopeController::GetController().Shutdown( friction.patch );
	friction.patch = NULL;
	friction.pObject = NULL;
}
void CCollisionEvent::UpdateFriction( void )
{
	for ( int i = 0; i < ARRAYSIZE(m_current); i++ )
	{
		if ( m_current[i].patch )
		{
			if ( m_current[i].update < (gpGlobals->time-0.1f) )
			{
				ShutdownFriction( m_current[i] );
			}

		}
	}
}

#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &matrix - 
//			&normal - 
// Output : static int
//-----------------------------------------------------------------------------
static int BestAxisMatchingNormal( matrix3x4_t &matrix, const Vector &normal )
{
	float bestDot = -1;
	int best = 0;
	for ( int i = 0; i < 3; i++ )
	{
		Vector tmp;
		MatrixGetColumn( matrix, i, tmp );
		float dot = fabs(DotProduct( tmp, normal ));
		if ( dot > bestDot )
		{
			bestDot = dot;
			best = i;
		}
	}

	return best;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pFluid - 
//			*pObject - 
//			*pEntity - 
//-----------------------------------------------------------------------------
void PhysicsSplash( IPhysicsFluidController *pFluid, IPhysicsObject *pObject, CBaseEntity *pEntity )
{
	//FIXME: For now just allow ragdolls for E3 - jdw
	if ( ( pObject->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL ) == false )
		return;

	Vector normal;
	float dist;
	pFluid->GetSurfacePlane( &normal, &dist );

	matrix3x4_t &matrix = pEntity->EntityToWorldTransform();
	
	// Find the local axis that best matches the water surface normal
	int bestAxis = BestAxisMatchingNormal( matrix, normal );

	Vector tangent, binormal;
	MatrixGetColumn( matrix, (bestAxis+1)%3, tangent );
	binormal = CrossProduct( normal, tangent );
	VectorNormalize( binormal );
	tangent = CrossProduct( binormal, normal );
	VectorNormalize( tangent );

	// Now we have a basis tangent to the surface that matches the object's local orientation as well as possible
	// compute an OBB using this basis
	
	// Get object extents in basis
	Vector tanPts[2], binPts[2];
	tanPts[0] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetAbsOrigin(), pEntity->GetAbsAngles(), -tangent );
	tanPts[1] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetAbsOrigin(), pEntity->GetAbsAngles(), tangent );
	binPts[0] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetAbsOrigin(), pEntity->GetAbsAngles(), -binormal );
	binPts[1] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetAbsOrigin(), pEntity->GetAbsAngles(), binormal );

	// now compute the centered bbox
	float mins[2], maxs[2], center[2], extents[2];
	mins[0] = DotProduct( tanPts[0], tangent );
	maxs[0] = DotProduct( tanPts[1], tangent );

	mins[1] = DotProduct( binPts[0], binormal );
	maxs[1] = DotProduct( binPts[1], binormal );

	center[0] = 0.5 * (mins[0] + maxs[0]);
	center[1] = 0.5 * (mins[1] + maxs[1]);

	extents[0] = maxs[0] - center[0];
	extents[1] = maxs[1] - center[1];

	Vector centerPoint = center[0] * tangent + center[1] * binormal + dist * normal;

	Vector axes[2];
	axes[0] = (maxs[0] - center[0]) * tangent;
	axes[1] = (maxs[1] - center[1]) * binormal;

	// visualize OBB hit
	/*
	Vector corner1 = centerPoint - axes[0] - axes[1];
	Vector corner2 = centerPoint + axes[0] - axes[1];
	Vector corner3 = centerPoint + axes[0] + axes[1];
	Vector corner4 = centerPoint - axes[0] + axes[1];
	NDebugOverlay::Line( corner1, corner2, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner2, corner3, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner3, corner4, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner4, corner1, 0, 0, 255, false, 10 );
	*/

	Vector	corner[4];

	corner[0] = centerPoint - axes[0] - axes[1];
	corner[1] = centerPoint + axes[0] - axes[1];
	corner[2] = centerPoint + axes[0] + axes[1];
	corner[3] = centerPoint - axes[0] + axes[1];

	FX_GunshotSplash( centerPoint, normal, random->RandomFloat( 8, 10 ) );
	
	int		splashes = 4;
	Vector	point;

	for ( int i = 0; i < splashes; i++ )
	{
		point = RandomVector( -32.0f, 32.0f );
		point[2] = 0.0f;

		point += corner[i];

		FX_GunshotSplash( point, normal, random->RandomFloat( 4, 6 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollisionEvent::UpdateFluidEvents( void )
{
	for ( int i = m_fluidEvents.Count()-1; i >= 0; --i )
	{
		if ( (gpGlobals->curtime - m_fluidEvents[i].impactTime) > FLUID_TIME_MAX )
		{
			m_fluidEvents.FastRemove(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
// Output : float
//-----------------------------------------------------------------------------
float CCollisionEvent::DeltaTimeSinceLastFluid( CBaseEntity *pEntity )
{
	for ( int i = m_fluidEvents.Count()-1; i >= 0; --i )
	{
		if ( m_fluidEvents[i].pEntity == pEntity )
		{
			return gpGlobals->curtime - m_fluidEvents[i].impactTime;
		}
	}

	int index = m_fluidEvents.AddToTail();
	m_fluidEvents[index].pEntity = pEntity;
	m_fluidEvents[index].impactTime = gpGlobals->curtime;
	return FLUID_TIME_MAX;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject - 
//			*pFluid - 
//-----------------------------------------------------------------------------
void CCollisionEvent::FluidStartTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid )
{
	if ( ( pObject == NULL ) || ( pFluid == NULL ) )
		return;

	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	
	if ( pEntity )
	{
		float timeSinceLastCollision = DeltaTimeSinceLastFluid( pEntity );
		
		if ( timeSinceLastCollision < 0.5f )
			return;

		PhysicsSplash( pFluid, pObject, pEntity );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject - 
//			*pFluid - 
//-----------------------------------------------------------------------------
void CCollisionEvent::FluidEndTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid )
{
	//FIXME: Do nothing for now
}

IPhysicsObject *GetWorldPhysObject ( void )
{
	return g_PhysWorldObject;
}
