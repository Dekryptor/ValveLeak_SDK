//========= Copyright � 1996-2003, Valve LLC, All rights reserved. ============
//
// Purpose: A hideous, putrescent, pus-filled undead carcass atop which a vile
//			nest of filthy poisonous headcrabs lurks.
//
//			Anyway, this guy has two range attacks: at short range, headcrabs
//			will leap from the nest to attack. At long range he will wrench a
//			headcrab from his back to throw it at his enemy.
//
//=============================================================================

#include "cbase.h"
#include "ai_basenpc.h"
#include "AI_Default.h"
#include "AI_Schedule.h"
#include "AI_Hull.h"
#include "AI_Motor.h"
#include "game.h"
#include "NPC_Headcrab.h"
#include "NPCEvent.h"
#include "EntityList.h"
#include "AI_Task.h"
#include "activitylist.h"
#include "engine/IEngineSound.h"
#include "NPC_BaseZombie.h"

#define BREATH_VOL_MAX  0.6

//
// Controls how soon he throws the first headcrab after seeing his enemy (also when the first headcrab leaps off)
//
#define ZOMBIE_THROW_FIRST_MIN_DELAY	1	// min seconds before first crab throw
#define ZOMBIE_THROW_FIRST_MAX_DELAY	2	// max seconds before first crab throw

//
// Controls how often he throws headcrabs (also how often headcrabs leap off)
//
#define ZOMBIE_THROW_MIN_DELAY	4			// min seconds between crab throws
#define ZOMBIE_THROW_MAX_DELAY	10			// max seconds between crab throws

//
// Ranges for throwing headcrabs.
//
#define ZOMBIE_THROW_RANGE_MIN	250
#define ZOMBIE_THROW_RANGE_MAX	800
#define ZOMBIE_THROW_CONE		0.6

//
// Ranges for headcrabs leaping off.
//
#define ZOMBIE_HC_LEAP_RANGE_MIN	12
#define ZOMBIE_HC_LEAP_RANGE_MAX	256
#define ZOMBIE_HC_LEAP_CONE		0.6


#define ZOMBIE_BODYGROUP_NEST_BASE		2	// First nest crab, +2 more
#define ZOMBIE_BODYGROUP_THROW			5	// The crab in our hand for throwing

#define ZOMBIE_ENEMY_BREATHE_DIST		300	// How close we must be to our enemy before we start breathing hard.


envelopePoint_t envPoisonZombieMoanVolumeFast[] =
{
	{	1.0f, 1.0f,
		0.1f, 0.1f,
	},
	{	0.0f, 0.0f,
		0.2f, 0.3f,
	},
};


//
// Turns the breathing off for a second, then back on.
//
envelopePoint_t envPoisonZombieBreatheVolumeOffShort[] =
{
	{	0.0f, 0.0f,
		0.1f, 0.1f,
	},
	{	0.0f, 0.0f,
		2.0f, 2.0f,
	},
	{	BREATH_VOL_MAX, BREATH_VOL_MAX,
		1.0f, 1.0f,
	},
};


//
// Custom schedules.
//
enum
{
	SCHED_ZOMBIE_POISON_RANGE_ATTACK2 = LAST_BASE_ZOMBIE_SCHEDULE,
};


//-----------------------------------------------------------------------------
// The maximum number of headcrabs we can have riding on our back.
// NOTE: If you change this value you must also change the lookup table in Spawn!
//-----------------------------------------------------------------------------
#define MAX_CRABS	3	


//-----------------------------------------------------------------------------
// Animation events.
//-----------------------------------------------------------------------------
enum
{
	AE_ZOMBIE_POISON_THROW_WARN_SOUND = 60,
	AE_ZOMBIE_POISON_PICKUP_CRAB,
	AE_ZOMBIE_POISON_THROW_SOUND,
	AE_ZOMBIE_POISON_THROW_CRAB,

	AE_ZOMBIE_POISON_SPIT,
};


//-----------------------------------------------------------------------------
// The model we use for our legs when we get blowed up.
//-----------------------------------------------------------------------------
static const char *s_szLegsModel = "models/zombie/classic_legs.mdl";


//-----------------------------------------------------------------------------
// The classname of the headcrab that jumps off of this kind of zombie.
//-----------------------------------------------------------------------------
static const char *s_szHeadcrabClassname = "npc_headcrab_poison";

/*
static const char *pFastBreathSounds[] =
{
	"npc/zombie_poison/pz_breathe_loop2.wav",
};
*/

static const char *pMoanSounds[] =
{
//	"npc/zombie_poison/pz_breathe_loop1.wav",
	"NPC_PoisonZombie.Moan1",
};


//-----------------------------------------------------------------------------
// Skill settings.
//-----------------------------------------------------------------------------
ConVar sk_zombie_poison_health( "sk_zombie_poison_health", "0");
ConVar sk_zombie_poison_dmg_spit( "sk_zombie_poison_dmg_spit","0");


class CNPC_PoisonZombie : public CNPC_BaseZombie
{
	DECLARE_CLASS( CNPC_PoisonZombie, CNPC_BaseZombie );

public:

	//
	// CBaseZombie implemenation.
	//
	virtual void BecomeTorso( const Vector &vecTorsoForce, const Vector &vecLegsForce );
	bool ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold );

	//
	// CAI_BaseNPC implementation.
	//
	virtual float MaxYawSpeed( void );

	virtual int MeleeAttack1Conditions( float flDot, float flDist );
	virtual int RangeAttack1Conditions( float flDot, float flDist );
	virtual int RangeAttack2Conditions( float flDot, float flDist );

	virtual void PrescheduleThink( void );
	virtual void BuildScheduleTestBits( void );
	virtual int SelectSchedule( void );
	virtual int SelectFailSchedule( int nFailedSchedule, int nFailedTask, AI_TaskFailureCode_t eTaskFailCode );
	virtual int TranslateSchedule( int scheduleType );

	virtual bool ShouldPlayIdleSound( void );

	//
	// CBaseAnimating implementation.
	//
	virtual void HandleAnimEvent( animevent_t *pEvent );

	//
	// CBaseEntity implementation.
	//
	virtual void Spawn( void );
	virtual void Precache( void );
	virtual void SetZombieModel( void );

	virtual Class_T Classify( void );
	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual int OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo );

	DECLARE_DATADESC();
	DEFINE_CUSTOM_AI;

	void PainSound( void );
	void AlertSound( void );
	void IdleSound( void );
	void AttackSound( void );
	void AttackHitSound( void );
	void AttackMissSound( void );
	void FootstepSound( bool fRightFoot );
	void FootscuffSound( bool fRightFoot ) {};

	virtual void StopLoopingSounds( void );

protected:

	virtual void MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize );
	virtual bool MustCloseToAttack( void );

	virtual const char *GetMoanSound( int nSoundIndex );
	virtual const char *GetLegsModel( void );
	virtual const char *GetHeadcrabClassname( void );

private:

	void BreatheOffShort( void );

	void EnableCrab( int nCrab, bool bEnable );
	int RandomThrowCrab( void );
	void EvacuateNest( bool bExplosion, float flDamage, CBaseEntity *pAttacker );

	CSoundPatch *m_pFastBreathSound;
	CSoundPatch *m_pSlowBreathSound;

	int m_nCrabCount;				// How many headcrabs we have on our back.
	bool m_bCrabs[MAX_CRABS];		// Which crabs in particular are on our back.
	int m_flNextCrabThrowTime;		// The next time we are allowed to throw a headcrab.

	float m_flNextPainSoundTime;
	float m_flNextFlinchTime;

	bool m_bNearEnemy;

	// NOT serialized:
	int m_nThrowCrab;				// The crab we are about to throw.
};

LINK_ENTITY_TO_CLASS( npc_poisonzombie, CNPC_PoisonZombie );


BEGIN_DATADESC( CNPC_PoisonZombie )

	DEFINE_SOUNDPATCH( CNPC_PoisonZombie, m_pFastBreathSound ),
	DEFINE_SOUNDPATCH( CNPC_PoisonZombie, m_pSlowBreathSound ),
	DEFINE_KEYFIELD( CNPC_PoisonZombie, m_nCrabCount, FIELD_INTEGER, "crabcount" ),
	DEFINE_ARRAY( CNPC_PoisonZombie, m_bCrabs, FIELD_BOOLEAN, MAX_CRABS ),
	DEFINE_FIELD( CNPC_PoisonZombie, m_flNextCrabThrowTime, FIELD_TIME ),

	DEFINE_FIELD( CNPC_PoisonZombie, m_flNextPainSoundTime, FIELD_TIME ),
	DEFINE_FIELD( CNPC_PoisonZombie, m_flNextFlinchTime, FIELD_TIME ),

	DEFINE_FIELD( CNPC_PoisonZombie, m_bNearEnemy, FIELD_BOOLEAN ),

	// NOT serialized:
	//DEFINE_FIELD( CNPC_PoisonZombie, m_nThrowCrab, FIELD_INTEGER ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::Precache( void )
{
	engine->PrecacheModel("models/zombie/poison.mdl");

	// This zombie uses the classic zombie for pieces that aren't done yet.
	engine->PrecacheModel( "models/zombie/classic_torso.mdl" );
	engine->PrecacheModel( "models/zombie/classic_legs.mdl" );

	PRECACHE_SOUND_ARRAY( pMoanSounds );
//	PRECACHE_SOUND_ARRAY( pFastBreathSounds );

	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::Spawn( void )
{
	Precache();

	m_fIsTorso = m_fIsHeadless = false;

	SetBloodColor( BLOOD_COLOR_YELLOW );
	m_iHealth = sk_zombie_poison_health.GetFloat();
	m_flFieldOfView = 0.2;

	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_MOVE_GROUND | bits_CAP_INNATE_MELEE_ATTACK1 | bits_CAP_INNATE_RANGE_ATTACK1 | bits_CAP_INNATE_RANGE_ATTACK2 | bits_CAP_DOORS_GROUP );

	BaseClass::Spawn();

	CPASAttenuationFilter filter( this, ATTN_IDLE );
//	m_pFastBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter, entindex(), CHAN_ITEM, RANDOM_SOUND_ARRAY( pFastBreathSounds ), ATTN_IDLE );
	m_pFastBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter, entindex(), CHAN_ITEM, "NPC_PoisonZombie.FastBreath", ATTN_IDLE );
	ENVELOPE_CONTROLLER.Play( m_pFastBreathSound, 0.0f, 100 );

	CPASAttenuationFilter filter2( this );
//	m_pSlowBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter2, entindex(), CHAN_ITEM, RANDOM_SOUND_ARRAY( pMoanSounds ), ATTN_NORM );
	m_pSlowBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter2, entindex(), CHAN_ITEM, "NPC_PoisonZombie.Moan1", ATTN_NORM );
	ENVELOPE_CONTROLLER.Play( m_pSlowBreathSound, BREATH_VOL_MAX, 100 );

	int nCrabs = m_nCrabCount;
	if ( !nCrabs )
	{
		nCrabs = MAX_CRABS;
	}
	m_nCrabCount = 0;

	//
	// Generate a random set of crabs based on the crab count
	// specified by the level designer.
	//
	int nBits[] = 
	{
		// One bit
		0x01,
		0x02,
		0x04,

		// Two bits
		0x03,
		0x05,
		0x06,
	};

	int nBitMask = 7;
	if (nCrabs == 1)
	{
		nBitMask = nBits[random->RandomInt( 0, 2 )];
	}
	else if (nCrabs == 2)
	{
		nBitMask = nBits[random->RandomInt( 3, 5 )];
	}

	for ( int i = 0; i < MAX_CRABS; i++ )
	{
		EnableCrab( i, ( nBitMask & ( 1 << i ) ) != 0 );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Returns a moan sound for this class of zombie.
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombie::GetMoanSound( int nSound )
{
	return pMoanSounds[nSound % ARRAYSIZE( pMoanSounds )];
}


//-----------------------------------------------------------------------------
// Purpose: Returns the model to use for our legs ragdoll when we are blown in twain.
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombie::GetLegsModel( void )
{
	return s_szLegsModel;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the classname (ie "npc_headcrab") to spawn when our headcrab bails.
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombie::GetHeadcrabClassname( void )
{
	return s_szHeadcrabClassname;
}


//-----------------------------------------------------------------------------
// Purpose: Turns the given crab on or off.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::EnableCrab( int nCrab, bool bEnable )
{
	ASSERT( ( nCrab >= 0 ) && ( nCrab < MAX_CRABS ) );

	if ( ( nCrab >= 0 ) && ( nCrab < MAX_CRABS ) )
	{
		if (m_bCrabs[nCrab] != bEnable)
		{
			m_nCrabCount += bEnable ? 1 : -1;
		}

		m_bCrabs[nCrab] = bEnable;
		SetBodygroup( ZOMBIE_BODYGROUP_NEST_BASE + nCrab, bEnable );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::StopLoopingSounds( void )
{
	ENVELOPE_CONTROLLER.SoundDestroy( m_pFastBreathSound );
	m_pFastBreathSound = NULL;

	ENVELOPE_CONTROLLER.SoundDestroy( m_pSlowBreathSound );
	m_pSlowBreathSound = NULL;

	BaseClass::StopLoopingSounds();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : info - 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::Event_Killed( const CTakeDamageInfo &info )
{
	if ( !( info.GetDamageType() & ( DMG_BLAST | DMG_ALWAYSGIB) ) ) 
	{
		EmitSound( "NPC_PoisonZombie.Die" );
	}

	if ( !m_fIsTorso )
	{
		EvacuateNest(info.GetDamageType() == DMG_BLAST, info.GetDamage(), info.GetAttacker() );
	}

	BaseClass::Event_Killed( info );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputInfo - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{
	//
	// Calculate what percentage of the creature's max health
	// this amount of damage represents (clips at 1.0).
	//
	float flDamagePercent = min( 1, inputInfo.GetDamage() / m_iMaxHealth );

	//
	// Throw one crab for every 20% damage we take.
	//
	if ( flDamagePercent >= 0.2 )
	{
		m_flNextCrabThrowTime = gpGlobals->curtime;
	}

	return BaseClass::OnTakeDamage_Alive( inputInfo );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CNPC_PoisonZombie::MaxYawSpeed( void )
{
	switch( GetActivity() )
	{
		case ACT_TURN_LEFT:
		case ACT_TURN_RIGHT:
		{
			return 120;
			break;
		}

		case ACT_RUN:
		{
			return 160;
			break;
		}

		default:
		{
			return 45;
			break;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Class_T	CNPC_PoisonZombie::Classify( void )
{
	return CLASS_ZOMBIE;
}


//-----------------------------------------------------------------------------
// Purpose: 
//
// NOTE: This function is still heavy with common code (found at the bottom).
//		 we should consider moving some into the base class! (sjb)
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::SetZombieModel( void )
{
	Hull_t lastHull = GetHullType();

	if ( m_fIsTorso )
	{
		SetModel( "models/zombie/classic_torso.mdl" );
		SetHullType(HULL_TINY);
	}
	else
	{
		SetModel( "models/zombie/poison.mdl" );
		SetHullType(HULL_HUMAN);
	}

	SetBodygroup( ZOMBIE_BODYGROUP_HEADCRAB, !m_fIsHeadless );

	SetHullSizeNormal( true );
	SetDefaultEyeOffset();
	SetActivity( ACT_IDLE );

	// hull changed size, notify vphysics
	// UNDONE: Solve this generally, systematically so other
	// NPCs can change size
	if ( lastHull != GetHullType() )
	{
		if ( VPhysicsGetObject() )
		{
			SetupVPhysicsHull();
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flDot - 
//			flDist - 
// Output :
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::MeleeAttack1Conditions( float flDot, float flDist )
{
	if (flDist > 70)
	{
		return COND_TOO_FAR_TO_ATTACK;
	}
	else if (flDot < 0.7)
	{
		return COND_NOT_FACING_ATTACK;
	}

	return COND_CAN_MELEE_ATTACK1;
}


//-----------------------------------------------------------------------------
// Purpose: Checks conditions for letting a headcrab leap off our back at our enemy.
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::RangeAttack1Conditions( float flDot, float flDist )
{
	if ( !m_nCrabCount )
	{
		//Msg("Range1: No crabs\n");
		return 0;
	}

	if ( m_flNextCrabThrowTime > gpGlobals->curtime )
	{
		//Msg("Range1: Too soon\n");
		return 0;
	}

	if ( flDist < ZOMBIE_HC_LEAP_RANGE_MIN )
	{
		//Msg("Range1: Too close to attack\n");
		return COND_TOO_CLOSE_TO_ATTACK;
	}
	
	if ( flDist > ZOMBIE_HC_LEAP_RANGE_MAX )
	{
		//Msg("Range1: Too far to attack\n");
		return COND_TOO_FAR_TO_ATTACK;
	}

	if ( flDot < ZOMBIE_HC_LEAP_CONE )
	{
		//Msg("Range1: Not facing\n");
		return COND_NOT_FACING_ATTACK;
	}

	m_nThrowCrab = RandomThrowCrab();

	//Msg("*** Range1: Can range attack\n");
	return COND_CAN_RANGE_ATTACK1;
}


//-----------------------------------------------------------------------------
// Purpose: Checks conditions for throwing a headcrab leap at our enemy.
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::RangeAttack2Conditions( float flDot, float flDist )
{
	if ( !m_nCrabCount )
	{
		//Msg("Range2: No crabs\n");
		return 0;
	}

	if ( m_flNextCrabThrowTime > gpGlobals->curtime )
	{
		//Msg("Range2: Too soon\n");
		return 0;
	}

	if ( flDist < ZOMBIE_THROW_RANGE_MIN )
	{
		//Msg("Range2: Too close to attack\n");
		return COND_TOO_CLOSE_TO_ATTACK;
	}
	
	if ( flDist > ZOMBIE_THROW_RANGE_MAX )
	{
		//Msg("Range2: Too far to attack\n");
		return COND_TOO_FAR_TO_ATTACK;
	}

	if ( flDot < ZOMBIE_THROW_CONE )
	{
		//Msg("Range2: Not facing\n");
		return COND_NOT_FACING_ATTACK;
	}

	m_nThrowCrab = RandomThrowCrab();

	//Msg("*** Range2: Can range attack\n");
	return COND_CAN_RANGE_ATTACK2;
}


//-----------------------------------------------------------------------------
// Purpose: Do whatever's necessary to reduce to a torso from a whole body. This
//			is called if we are chopped by a sharp physics object. It should not
//			be called because of explosive damage because we overload
//			ShouldBecomeTorso to prevent that.
// Input  : vecTorsoForce - 
//			vecLegsForce - 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::BecomeTorso( const Vector &vecTorsoForce, const Vector &vecLegsForce )
{
	EvacuateNest( true, vecTorsoForce.Length(), NULL );

	// No more ranged attacks!
	CapabilitiesRemove( bits_CAP_INNATE_RANGE_ATTACK1 );

	BaseClass::BecomeTorso( vecTorsoForce, vecLegsForce );
}


//-----------------------------------------------------------------------------
// Purpose: Turns off our breath so we can play another vocal sound.
//			TODO: pass in duration
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::BreatheOffShort( void )
{
	if ( m_bNearEnemy )
	{
		ENVELOPE_CONTROLLER.SoundPlayEnvelope( m_pFastBreathSound, SOUNDCTRL_CHANGE_VOLUME, envPoisonZombieBreatheVolumeOffShort, ARRAYSIZE(envPoisonZombieBreatheVolumeOffShort) );
	}
	else
	{
		ENVELOPE_CONTROLLER.SoundPlayEnvelope( m_pSlowBreathSound, SOUNDCTRL_CHANGE_VOLUME, envPoisonZombieBreatheVolumeOffShort, ARRAYSIZE(envPoisonZombieBreatheVolumeOffShort) );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Catches the monster-specific events that occur when tagged animation
//			frames are played.
// Input  : pEvent - 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::HandleAnimEvent( animevent_t *pEvent )
{
	switch ( pEvent->event )
	{
		case AE_ZOMBIE_POISON_PICKUP_CRAB:
		{
			EnableCrab( m_nThrowCrab, false );
			SetBodygroup( ZOMBIE_BODYGROUP_THROW, 1 );
			break;
		}

		case AE_ZOMBIE_POISON_THROW_WARN_SOUND:
		{
			BreatheOffShort();
			EmitSound( "NPC_PoisonZombie.ThrowWarn" );
			break;
		}

		case AE_ZOMBIE_POISON_THROW_SOUND:
		{
			BreatheOffShort();
			EmitSound( "NPC_PoisonZombie.Throw" );
			break;
		}

		case AE_ZOMBIE_POISON_THROW_CRAB:
		{
			SetBodygroup( ZOMBIE_BODYGROUP_THROW, 0 );

			CBlackHeadcrab *pCrab = (CBlackHeadcrab *)CreateNoSpawn( GetHeadcrabClassname(), EyePosition(), vec3_angle, this );
			pCrab->AddSpawnFlags( SF_NPC_FALL_TO_GROUND );
			
			// make me the crab's owner to avoid collision issues
			pCrab->SetOwnerEntity( this );

			pCrab->Spawn();

			pCrab->SetLocalAngles( GetLocalAngles() );
			pCrab->SetActivity( ACT_RANGE_ATTACK1 );
			pCrab->SetNextThink( gpGlobals->curtime );
			pCrab->PhysicsSimulate();

			pCrab->GetMotor()->SetIdealYaw( GetAbsAngles().y );

			if ( IsOnFire() )
			{
				pCrab->Ignite( 100.0 );
			}

			CBaseEntity *pEnemy = GetEnemy();
			if ( pEnemy )
			{
				Vector vecEnemyEyePos = pEnemy->EyePosition();
				pCrab->ThrowAt( vecEnemyEyePos );
			}

			m_flNextCrabThrowTime = gpGlobals->curtime + random->RandomInt( ZOMBIE_THROW_MIN_DELAY, ZOMBIE_THROW_MAX_DELAY );
			break;
		}

		case AE_ZOMBIE_POISON_SPIT:
		{
			Vector forward;
			QAngle qaPunch( 45, random->RandomInt(-5, 5), random->RandomInt(-5, 5) );
			AngleVectors( GetLocalAngles(), &forward );
			forward = forward * 200;
			ClawAttack( 70, sk_zombie_poison_dmg_spit.GetFloat(), qaPunch, forward );
			break;
		}

		default:
		{
			BaseClass::HandleAnimEvent( pEvent );
			break;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Returns the index of a randomly chosen crab to throw.
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::RandomThrowCrab( void )
{
	// FIXME: this could take a long time, theoretically
	int nCrab = -1;
	do
	{
		int nTest = random->RandomInt( 0, 2 );
		if ( m_bCrabs[nTest] )
		{
			nCrab = nTest;
		}
	} while ( nCrab == -1 );
	
	return nCrab;
}


//-----------------------------------------------------------------------------
// Purpose: The nest is dead! Evacuate the nest!
// Input  : bExplosion - We were evicted by an explosion so we should go a-flying.
//			flDamage - The damage that was done to cause the evacuation.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::EvacuateNest( bool bExplosion, float flDamage, CBaseEntity *pAttacker )
{
	// HACK: if we were in mid-throw, drop the throwing crab also.
	if ( GetBodygroup( ZOMBIE_BODYGROUP_THROW ) )
	{
		SetBodygroup( ZOMBIE_BODYGROUP_THROW, 0 );
		m_nCrabCount++;
	}

	for ( int i = 0; i < m_nCrabCount; i++ )
	{
		Vector vecPos = EyePosition(); // + random->RandomInt( 10, 50 ) * Vector( random->RandomFloat( -1.0, 1.0 ), random->RandomFloat( -1.0, 1.0 ), random->RandomFloat( 0.5, 1.0 ) );
		QAngle vecAngles = QAngle( 0, random->RandomFloat( 0, 360 ), 0 );

		CBlackHeadcrab *pCrab = (CBlackHeadcrab *)CreateNoSpawn( GetHeadcrabClassname(), vecPos, vecAngles, this );
		pCrab->Spawn();

		float flVelocityScale = 2.0f;
		if ( bExplosion && ( flDamage > 10 ) )
		{
			flVelocityScale = 0.1 * flDamage;
		}

		if (IsOnFire())
		{
			pCrab->Ignite( 100.0 );
		}

		pCrab->Eject( vecAngles, flVelocityScale, pAttacker );
	}

	EnableCrab( 0, false );
	EnableCrab( 1, false );
	EnableCrab( 2, false );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::PrescheduleThink( void )
{
	if ( HasCondition( COND_NEW_ENEMY ) )
	{
		m_flNextCrabThrowTime = gpGlobals->curtime + random->RandomInt( ZOMBIE_THROW_FIRST_MIN_DELAY, ZOMBIE_THROW_FIRST_MAX_DELAY );
	}

	if (  gpGlobals->curtime > m_flNextFlinchTime )
	{
		Forget( bits_MEMORY_FLINCHED );
	}

	bool bNearEnemy = false;
	if ( GetEnemy() != NULL )
	{
		float flDist = (GetEnemy()->GetAbsOrigin() - GetAbsOrigin()).Length();
		if ( flDist < ZOMBIE_ENEMY_BREATHE_DIST )
		{
			bNearEnemy = true;
		}
	}

	if ( bNearEnemy )
	{
		if ( !m_bNearEnemy )
		{
			// Our enemy is nearby. Breathe faster.
			float duration = random->RandomFloat( 1.0f, 2.0f );
			ENVELOPE_CONTROLLER.SoundChangeVolume( m_pFastBreathSound, BREATH_VOL_MAX, duration );
			ENVELOPE_CONTROLLER.SoundChangePitch( m_pFastBreathSound, random->RandomInt( 100, 120 ), random->RandomFloat( 1.0f, 2.0f ) );

			ENVELOPE_CONTROLLER.SoundChangeVolume( m_pSlowBreathSound, 0.0f, duration );

			m_bNearEnemy = true;
		}
	}
	else if ( m_bNearEnemy )
	{
		// Our enemy is far away. Slow our breathing down.
		float duration = random->RandomFloat( 2.0f, 4.0f );
		ENVELOPE_CONTROLLER.SoundChangeVolume( m_pFastBreathSound, BREATH_VOL_MAX, duration );
		ENVELOPE_CONTROLLER.SoundChangeVolume( m_pSlowBreathSound, 0.0f, duration );
//		ENVELOPE_CONTROLLER.SoundChangePitch( m_pBreathSound, random->RandomInt( 80, 100 ), duration );

		m_bNearEnemy = false;
	}

	BaseClass::PrescheduleThink();
}


//-----------------------------------------------------------------------------
// Purpose: Allows for modification of the interrupt mask for the current schedule.
//			In the most cases the base implementation should be called first.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::BuildScheduleTestBits( void )
{
	if ( IsCurSchedule( SCHED_CHASE_ENEMY ) )
	{
		SetCustomInterruptCondition( COND_LIGHT_DAMAGE );
		SetCustomInterruptCondition( COND_HEAVY_DAMAGE );
	}
	else if ( IsCurSchedule( SCHED_RANGE_ATTACK1 ) || IsCurSchedule( SCHED_RANGE_ATTACK2 ) )
	{
		ClearCustomInterruptCondition( COND_LIGHT_DAMAGE );
		ClearCustomInterruptCondition( COND_HEAVY_DAMAGE );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::SelectFailSchedule( int nFailedSchedule, int nFailedTask, AI_TaskFailureCode_t eTaskFailCode )
{
	int nSchedule = BaseClass::SelectFailSchedule( nFailedSchedule, nFailedTask, eTaskFailCode );

	if ( nSchedule == SCHED_CHASE_ENEMY_FAILED )
	{
		return SCHED_ESTABLISH_LINE_OF_FIRE;
	}

	return nSchedule;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::SelectSchedule( void )
{
	int nSchedule = BaseClass::SelectSchedule();

	if ( nSchedule == SCHED_SMALL_FLINCH )
	{
		 m_flNextFlinchTime = gpGlobals->curtime + random->RandomFloat( 1, 3 );
	}

	return nSchedule;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scheduleType - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_PoisonZombie::TranslateSchedule( int scheduleType )
{
	if ( scheduleType == SCHED_RANGE_ATTACK2 )
	{
		return SCHED_ZOMBIE_POISON_RANGE_ATTACK2;
	}

	return BaseClass::TranslateSchedule( scheduleType );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombie::ShouldPlayIdleSound( void )
{
	return CAI_BaseNPC::ShouldPlayIdleSound();
}


//-----------------------------------------------------------------------------
// Purpose: Play a random attack hit sound
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::AttackHitSound( void )
{
	EmitSound( "Zombie.AttackHit" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack miss sound
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::AttackMissSound( void )
{
	EmitSound( "Zombie.AttackMiss" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::AttackSound( void )
{
	EmitSound( "NPC_PoisonZombie.Attack" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random idle sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::IdleSound( void )
{
	// HACK: base zombie code calls IdleSound even when not idle!
	if ( m_NPCState != NPC_STATE_COMBAT )
	{
		BreatheOffShort();
		EmitSound( "NPC_PoisonZombie.Idle" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random pain sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::PainSound( void )
{
	// Don't make pain sounds too often.
	if ( m_flNextPainSoundTime <= gpGlobals->curtime )
	{	
		BreatheOffShort();
		EmitSound( "NPC_PoisonZombie.Pain" );
		m_flNextPainSoundTime = gpGlobals->curtime + random->RandomFloat( 4.0, 7.0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random alert sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::AlertSound( void )
{
	BreatheOffShort();

	EmitSound( "NPC_PoisonZombie.Alert" );
}


//-----------------------------------------------------------------------------
// Purpose: Sound of a footstep
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::FootstepSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound( "NPC_PoisonZombie.FootstepRight" );
	}
	else
	{
		EmitSound( "NPC_PoisonZombie.FootstepLeft" );
	}

	if( ShouldPlayFootstepMoan() )
	{
		m_flNextMoanSound = gpGlobals->curtime;
		MoanSound( envPoisonZombieMoanVolumeFast, ARRAYSIZE( envPoisonZombieMoanVolumeFast ) );
	}
}


//-----------------------------------------------------------------------------
// Purpose: If we don't have any headcrabs to throw, we must close to attack our enemy.
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombie::MustCloseToAttack(void)
{
	return (m_nCrabCount == 0);
}


//-----------------------------------------------------------------------------
// Purpose: Open a window and let a little bit of the looping moan sound
//			come through.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombie::MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize )
{
	if( !m_pMoanSound )
	{
		// Don't set this up until the code calls for it.
		const char *pszSound = GetMoanSound( m_iMoanSound );
		m_flMoanPitch = random->RandomInt( 98, 110 );

		CPASAttenuationFilter filter( this, 1.5 );
		//m_pMoanSound = ENVELOPE_CONTROLLER.SoundCreate( entindex(), CHAN_STATIC, pszSound, ATTN_NORM );
		m_pMoanSound = ENVELOPE_CONTROLLER.SoundCreate( filter, entindex(), CHAN_STATIC, pszSound, 1.5 );

		ENVELOPE_CONTROLLER.Play( m_pMoanSound, 0.5, m_flMoanPitch );
	}

	envPoisonZombieMoanVolumeFast[ 1 ].durationMin = 0.1;
	envPoisonZombieMoanVolumeFast[ 1 ].durationMax = 0.4;

	if ( random->RandomInt( 1, 2 ) == 1 )
	{
		IdleSound();
	}

	float duration = ENVELOPE_CONTROLLER.SoundPlayEnvelope( m_pMoanSound, SOUNDCTRL_CHANGE_VOLUME, pEnvelope, iEnvelopeSize );

	float flPitchShift = random->RandomInt( -4, 4 );
	ENVELOPE_CONTROLLER.SoundChangePitch( m_pMoanSound, m_flMoanPitch + flPitchShift, 0.3 );

	m_flNextMoanSound = gpGlobals->curtime + duration + 9999;
}


//-----------------------------------------------------------------------------
// Purpose: Overloaded so that explosions don't split the poison zombie in twain.
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombie::ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold )
{
	return false;
}


int ACT_ZOMBIE_POISON_THREAT;


AI_BEGIN_CUSTOM_NPC( npc_poisonzombie, CNPC_PoisonZombie )

	DECLARE_ACTIVITY( ACT_ZOMBIE_POISON_THREAT )

	DEFINE_SCHEDULE
	(
		SCHED_ZOMBIE_POISON_RANGE_ATTACK2,

		"	Tasks"
		"		TASK_STOP_MOVING						0"
		"		TASK_FACE_IDEAL							0"
		"		TASK_PLAY_PRIVATE_SEQUENCE_FACE_ENEMY	ACTIVITY:ACT_ZOMBIE_POISON_THREAT"
		"		TASK_FACE_IDEAL							0"
		"		TASK_RANGE_ATTACK2						0"

		"	Interrupts"
		"		COND_ENEMY_OCCLUDED"
		"		COND_NO_PRIMARY_AMMO"
	)

AI_END_CUSTOM_NPC()


