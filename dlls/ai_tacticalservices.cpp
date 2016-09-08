//========= Copyright � 2003, Valve LLC, All rights reserved. ==========
//
// Purpose:
//
//=============================================================================

#include "cbase.h"

#include "bitstring.h"

#include "ai_tacticalservices.h"
#include "ai_basenpc.h"
#include "ai_node.h"
#include "ai_network.h"
#include "ai_link.h"
#include "ai_moveprobe.h"
#include "ai_pathfinder.h"
#include "ai_networkmanager.h"

#ifdef DEBUG_FIND_COVER
int g_AIDebugFindCoverNode = -1;
#define DebugFindCover( node, from, to, r, g, b ) if (g_AIDebugFindCoverNode != -1 && g_AIDebugFindCoverNode != node) ; else NDebugOverlay::Line( from, to, r, g, b, false, 1 )
#else
#define DebugFindCover( node, from, to, r, g, b ) ((void)0)
#endif


//-----------------------------------------------------------------------------

BEGIN_SIMPLE_DATADESC(CAI_TacticalServices)
	//						m_pNetwork	(not saved)
	//						m_pPathfinder	(not saved)
END_DATADESC();

//-------------------------------------

void CAI_TacticalServices::Init( CAI_Network *pNetwork )
{
	Assert( pNetwork );
	m_pNetwork = pNetwork;
	m_pPathfinder = GetOuter()->GetPathfinder();
	Assert( m_pPathfinder );
}
	
//-------------------------------------

bool CAI_TacticalServices::FindLos(const Vector &threatPos, const Vector &threatEyePos, float minThreatDist, float maxThreatDist, float blockTime, const Vector &threatFacing, Vector *pResult)
{
	int node = FindLosNode( threatPos, threatEyePos, 
											 minThreatDist, maxThreatDist, 
											 blockTime, threatFacing );
	
	if (node == NO_NODE)
		return false;

	*pResult = GetNodePos( node );
	return true;
}

//-------------------------------------

bool CAI_TacticalServices::FindLos(const Vector &threatPos, const Vector &threatEyePos, float minThreatDist, float maxThreatDist, float blockTime, Vector *pResult)
{
	return FindLos(threatPos, threatEyePos, minThreatDist, maxThreatDist, blockTime, vec3_origin, pResult );
}

//-------------------------------------

bool CAI_TacticalServices::FindBackAwayPos( const Vector &vecThreat, Vector *pResult )
{
	int node = FindBackAwayNode( vecThreat );
	
	if (node == NO_NODE)
		return false;

	*pResult = GetNodePos( node );
	return true;
}

//-------------------------------------

bool CAI_TacticalServices::FindCoverPos( const Vector &vThreatPos, const Vector &vThreatEyePos, float flMinDist, float flMaxDist, Vector *pResult )
{
	return FindCoverPos( GetLocalOrigin(), vThreatPos, vThreatEyePos, flMinDist, flMaxDist, pResult );
}

//-------------------------------------

bool CAI_TacticalServices::FindCoverPos( const Vector &vNearPos, const Vector &vThreatPos, const Vector &vThreatEyePos, float flMinDist, float flMaxDist, Vector *pResult )
{
	int node = FindCoverNode( vNearPos, vThreatPos, vThreatEyePos, flMinDist, flMaxDist );
	
	if (node == NO_NODE)
		return false;

	*pResult = GetNodePos( node );
	return true;
}

//-------------------------------------
// Checks lateral cover
//-------------------------------------
bool CAI_TacticalServices::TestLateralCover( const Vector &vecCheckStart, const Vector &vecCheckEnd )
{
	trace_t	tr;

	// it's faster to check the SightEnt's visibility to the potential spot than to check the local move, so we do that first.
	AI_TraceLine( vecCheckStart, vecCheckEnd + GetOuter()->GetViewOffset(),
		MASK_OPAQUE, GetOuter(), COLLISION_GROUP_NONE, &tr);

	if (GetOuter()->IsCoverPosition(vecCheckStart, vecCheckEnd + GetOuter()->GetViewOffset()))
	{
		if ( GetOuter()->IsValidCover ( vecCheckEnd, NULL ) )
		{
			AIMoveTrace_t moveTrace;
			GetOuter()->GetMoveProbe()->MoveLimit( NAV_GROUND, GetLocalOrigin(), vecCheckEnd, MASK_NPCSOLID, NULL, &moveTrace );
			if (moveTrace.fStatus == AIMR_OK)
			{
				return true;
			}
		}
	}

	return false;
}


//-------------------------------------
// FindLateralCover - attempts to locate a spot in the world
// directly to the left or right of the caller that will
// conceal them from view of pSightEnt
//-------------------------------------

#define	COVER_CHECKS	5// how many checks are made
#define COVER_DELTA		48// distance between checks
bool CAI_TacticalServices::FindLateralCover ( const Vector &vecThreat, Vector *pResult )
{
	Vector	vecLeftTest;
	Vector	vecRightTest;
	Vector	vecStepRight;
	Vector  vecCheckStart;
	int		i;

	if ( TestLateralCover( vecThreat, GetLocalOrigin() ) )
	{
		*pResult = GetLocalOrigin();
		return true;
	}

	Vector right;
	AngleVectors( GetLocalAngles(), NULL, &right, NULL );
	vecStepRight = right * COVER_DELTA;
	vecStepRight.z = 0;

	vecLeftTest = vecRightTest = GetLocalOrigin();
 	vecCheckStart = vecThreat;

	for ( i = 0 ; i < COVER_CHECKS ; i++ )
	{
		vecLeftTest = vecLeftTest - vecStepRight;
		vecRightTest = vecRightTest + vecStepRight;

		if (TestLateralCover( vecCheckStart, vecLeftTest ))
		{
			*pResult = vecLeftTest;
			return true;
		}

		if (TestLateralCover( vecCheckStart, vecRightTest ))
		{
			*pResult = vecRightTest;
			return true;
		}
	}

	return false;
}

//-------------------------------------
// Purpose: Find a nearby node that further away from the enemy than the
//			min range of my current weapon if there is one or just futher
//			away than my current location if I don't have a weapon.  
//			Used to back away for attacks
//-------------------------------------

int CAI_TacticalServices::FindBackAwayNode(const Vector &vecThreat )
{
	if ( !CAI_NetworkManager::NetworksLoaded() )
	{
		DevWarning( 2, "Graph not ready for FindBackAwayNode!\n" );
		return NO_NODE;
	}

	int iMyNode			= GetNetwork()->NearestNodeToNPCAtPoint( GetOuter(), GetLocalOrigin() );
	int iThreatNode		= GetNetwork()->NearestNodeToNPCAtPoint( GetOuter(), vecThreat );

	if ( iMyNode == NO_NODE )
	{
		DevWarning( 2, "FindBackAwayNode() - %s has no nearest node!\n", GetEntClassname());
		return NO_NODE;
	}
	if ( iThreatNode == NO_NODE )
	{
		// DevWarning( 2, "FindBackAwayNode() - Threat has no nearest node!\n" );
		iThreatNode = iMyNode;
		// return false;
	}

	// Get my current distance from the threat
	float flCurDist = ( GetLocalOrigin() - vecThreat ).Length();

	// Check my neighbors to find a node that's further away
	for (int link = 0; link < GetNetwork()->GetNode(iMyNode)->NumLinks(); link++) 
	{
		CAI_Link *nodeLink = GetNetwork()->GetNode(iMyNode)->GetLinkByIndex(link);

		if ( !m_pPathfinder->IsLinkUsable( nodeLink, iMyNode ) )
			continue;

		int destID = nodeLink->DestNodeID(iMyNode);

		float flTestDist = ( vecThreat - GetNetwork()->GetNode(destID)->GetPosition(GetHullType()) ).Length();

		if ( flTestDist > flCurDist )
		{
			return destID;
		}
	}
	return NO_NODE;
}

//-------------------------------------
// FindCover - tries to find a nearby node that will hide
// the caller from its enemy. 
//
// If supplied, search will return a node at least as far
// away as MinDist, but no farther than MaxDist. 
// if MaxDist isn't supplied, it defaults to a reasonable 
// value
//-------------------------------------

int CAI_TacticalServices::FindCoverNode(const Vector &vThreatPos, const Vector &vThreatEyePos, float flMinDist, float flMaxDist )
{
	return FindCoverNode(GetLocalOrigin(), vThreatPos, vThreatEyePos, flMinDist, flMaxDist );
}

//-------------------------------------

int CAI_TacticalServices::FindCoverNode(const Vector &vNearPos, const Vector &vThreatPos, const Vector &vThreatEyePos, float flMinDist, float flMaxDist )
{
	if ( !CAI_NetworkManager::NetworksLoaded() )
		return NO_NODE;

	int iMyNode = GetNetwork()->NearestNodeToNPCAtPoint( GetOuter(), vNearPos );

	if ( iMyNode == NO_NODE )
	{
		DevWarning( 2, "FindCover() - %s has no nearest node!\n", GetEntClassname());
		return NO_NODE;
	}

	if ( !flMaxDist )
	{
		// user didn't supply a MaxDist, so work up a crazy one.
		flMaxDist = 784;
	}

	if ( flMinDist > 0.5 * flMaxDist)
	{
		flMinDist = 0.5 * flMaxDist;
	}

	// ------------------------------------------------------------------------------------
	// We're going to search for a cover node by expanding to our current node's neighbors
	// and then their neighbors, until cover is found, or all nodes are beyond MaxDist
	// ------------------------------------------------------------------------------------
	AI_NearNode_t *pBuffer = (AI_NearNode_t *)stackalloc( sizeof(AI_NearNode_t) * GetNetwork()->NumNodes() );
	CNodeList list( pBuffer, GetNetwork()->NumNodes() );
	CBitString wasVisited(GetNetwork()->NumNodes());	// Nodes visited

	// mark start as visited
	list.Insert( AI_NearNode_t(iMyNode, 0) ); 
	wasVisited.SetBit( iMyNode );
	float flMinDistSqr = flMinDist*flMinDist;
	float flMaxDistSqr = flMaxDist*flMaxDist;

	static int nSearchRandomizer = 0;		// tries to ensure the links are searched in a different order each time;

	// Search until the list is empty
	while( list.Count() )
	{
		// Get the node that is closest in the number of steps and remove from the list
		int nodeIndex = list.ElementAtHead().nodeIndex;
		list.RemoveAtHead();

		CAI_Node *pNode = GetNetwork()->GetNode(nodeIndex);

		Vector nodeOrigin = pNode->GetPosition(GetHullType());
		Activity nCoverActivity = GetOuter()->GetCoverActivity( pNode->GetHint() );
		Vector   vEyePos		= nodeOrigin + GetOuter()->EyeOffset(nCoverActivity);

		float dist = (vNearPos - nodeOrigin).LengthSqr();
		
		if (dist >= flMinDistSqr && dist < flMaxDistSqr)
		{
			// Check if this location will block the threat's line of sight to me
			if (GetOuter()->IsCoverPosition(vThreatEyePos, vEyePos))
			{
				if ( GetOuter()->IsValidCover( nodeOrigin, pNode->GetHint() ) )
				{
					// --------------------------------------------------------
					// Don't let anyone else use this node for a while
					// --------------------------------------------------------
					pNode->Lock( 1.0 );
					GetOuter()->m_pHintNode = pNode->GetHint();

					// The next NPC who searches should use a slight different pattern
					nSearchRandomizer = nodeIndex;
					DebugFindCover( pNode->GetId(), vEyePos, vThreatEyePos, 0, 255, 0 );
					return nodeIndex;
				}
				else
				{
					DebugFindCover( pNode->GetId(), vEyePos, vThreatEyePos, 0, 0, 255 );
				}
			}
			else
			{
				DebugFindCover( pNode->GetId(), vEyePos, vThreatEyePos, 255, 0, 0 );
			}
		}

		// Add its children to the search list
		// Go through each link
		// UNDONE: Pass in a cost function to measure each link?
		for ( int link = 0; link < GetNetwork()->GetNode(nodeIndex)->NumLinks(); link++ ) 
		{
			int index = (link + nSearchRandomizer) % GetNetwork()->GetNode(nodeIndex)->NumLinks();
			CAI_Link *nodeLink = GetNetwork()->GetNode(nodeIndex)->GetLinkByIndex(index);

			if ( !m_pPathfinder->IsLinkUsable( nodeLink, iMyNode ) )
				continue;

			int newID = nodeLink->DestNodeID(nodeIndex);

			// If not already on the closed list, add to it and set its distance
			if (!wasVisited.GetBit(newID))
			{
				// Don't accept climb nodes or nodes that aren't ready to use yet
				if ( GetNetwork()->GetNode(newID)->GetType() != NODE_CLIMB && !GetNetwork()->GetNode(newID)->IsLocked() )
				{
					// UNDONE: Shouldn't we really accumulate the distance by path rather than
					// absolute distance.  After all, we are performing essentially an A* here.
					nodeOrigin = GetNetwork()->GetNode(newID)->GetPosition(GetHullType());
					dist = (vNearPos - nodeOrigin).LengthSqr();

					// use distance to threat as a heuristic to keep AIs from running toward
					// the threat in order to take cover from it.
					float threatDist = (vThreatPos - nodeOrigin).LengthSqr();

					// Now check this node is not too close towards the threat
					if ( dist < threatDist * 1.5 )
					{
						list.Insert( AI_NearNode_t(newID, dist) );
					}
				}
				// mark visited
				wasVisited.SetBit(newID);
			}
		}
	}

	// We failed.  Not cover node was found
	// Clear hint node used to set ducking
	GetOuter()->m_pHintNode = NULL;
	return NO_NODE;
}


//-------------------------------------
// Purpose:  Return node ID that has line of sight to target I want to shoot
//
// Input  :	pNPC			- npc that's looking for a place to shoot from
//			vThreatPos		- position of entity/location I'm trying to shoot
//			vThreatEyePos	- eye position of entity I'm trying to shoot. If 
//							  entity has no eye position, just give vThreatPos again
//			flMinThreatDist	- minimum distance that node must be from vThreatPos
//			flMaxThreadDist	- maximum distance that node can be from vThreadPos
//			vThreatFacing	- optional argument.  If given the returned node
//							  will also be behind the given facing direction (flanking)
//			flBlockTime		- how long to block this node from use
// Output :	int				- ID number of node that meets qualifications
//-------------------------------------

int CAI_TacticalServices::FindLosNode(const Vector &vThreatPos, const Vector &vThreatEyePos, float flMinThreatDist, float flMaxThreatDist, float flBlockTime, const Vector &vThreatFacing)
{
	if ( !CAI_NetworkManager::NetworksLoaded() )
		return NO_NODE;

	int iMyNode	= GetNetwork()->NearestNodeToNPCAtPoint( GetOuter(), GetLocalOrigin() );

	if ( iMyNode == NO_NODE )
	{
		DevWarning( 2, "FindLos() - %s has no nearest node!\n", GetEntClassname());
		return NO_NODE;
	}

	// ------------------------------------------------------------------------------------
	// We're going to search for a shoot node by expanding to our current node's neighbors
	// and then their neighbors, until a shooting position is found, or all nodes are beyond MaxDist
	// ------------------------------------------------------------------------------------
	AI_NearNode_t *pBuffer = (AI_NearNode_t *)stackalloc( sizeof(AI_NearNode_t) * GetNetwork()->NumNodes() );
	CNodeList list( pBuffer, GetNetwork()->NumNodes() );
	CBitString wasVisited(GetNetwork()->NumNodes());	// Nodes visited

	// mark start as visited
	wasVisited.SetBit( iMyNode );
	list.Insert( AI_NearNode_t(iMyNode, 0) );

	static int nSearchRandomizer = 0;		// tries to ensure the links are searched in a different order each time;

	while ( list.Count() )
	{
		int nodeIndex = list.ElementAtHead().nodeIndex;
		// remove this item from the list
		list.RemoveAtHead();

		Vector nodeOrigin = GetNetwork()->GetNode(nodeIndex)->GetPosition(GetHullType());

		// HACKHACK: Can't we rework this loop and get rid of this?
		// skip the starting node, or we probably wouldn't have called this function.
		if ( nodeIndex != iMyNode )
		{
			bool skip = false;
			// If threat facing direction was given, reject if not flanking the threat
			if (vThreatFacing != vec3_origin)
			{
				Vector nodeToThreat = (vThreatPos-nodeOrigin);
				if (DotProduct(nodeToThreat, vThreatFacing)<0)
				{
					skip = true;
				}
			}
			// Don't accept climb nodes, and assume my nearest node isn't valid because
			// we decided to make this check in the first place.  Keep moving
			if ( !skip && !GetNetwork()->GetNode(nodeIndex)->IsLocked() &&
				GetNetwork()->GetNode(nodeIndex)->GetType() != NODE_CLIMB )
			{
				// Now check its distance and only accept if in range
				float flThreatDist = ( nodeOrigin - vThreatPos ).Length();

				if ( flThreatDist < flMaxThreatDist &&
					 flThreatDist > flMinThreatDist )
				{
					if ( GetOuter()->IsValidShootPosition ( nodeOrigin, GetNetwork()->GetNode(nodeIndex)->GetHint() ) )
					{
						if (GetOuter()->WeaponLOSCondition(nodeOrigin,vThreatEyePos,false))
						{
							// Note when this node was used, so we don't try 
							// to use it again right away.
							GetNetwork()->GetNode(nodeIndex)->Lock( flBlockTime );

							// This used to not be set, why? (kenb)
							GetOuter()->m_pHintNode = GetNetwork()->GetNode(nodeIndex)->GetHint();

							// The next NPC who searches should use a slight different pattern
							nSearchRandomizer = nodeIndex;
							return nodeIndex;
						}
					}
				}
			}
		}

		// Go through each link and add connected nodes to the list
		for (int link=0; link < GetNetwork()->GetNode(nodeIndex)->NumLinks();link++) 
		{
			int index = (link + nSearchRandomizer) % GetNetwork()->GetNode(nodeIndex)->NumLinks();
			CAI_Link *nodeLink = GetNetwork()->GetNode(nodeIndex)->GetLinkByIndex(index);

			if ( !m_pPathfinder->IsLinkUsable( nodeLink, iMyNode ) )
				continue;

			int newID = nodeLink->DestNodeID(nodeIndex);

			// If not already visited, add to the list
			if (!wasVisited.GetBit(newID))
			{
				float dist = (GetLocalOrigin() - GetNetwork()->GetNode(newID)->GetPosition(GetHullType())).LengthSqr();
				list.Insert( AI_NearNode_t(newID, dist) );
				wasVisited.SetBit( newID );
			}
		}
	}
	// We failed.  No range attack node node was found
	return NO_NODE;
}

//-------------------------------------
// Checks lateral LOS
//-------------------------------------
bool CAI_TacticalServices::TestLateralLos( const Vector &vecCheckStart, const Vector &vecCheckEnd )
{
	trace_t	tr;

	// it's faster to check the SightEnt's visibility to the potential spot than to check the local move, so we do that first.
	AI_TraceLine( vecCheckStart, vecCheckEnd + GetOuter()->GetViewOffset(),
		MASK_OPAQUE, GetOuter(), COLLISION_GROUP_NONE, &tr);

	if (tr.fraction == 1.0)
	{
		if ( GetOuter()->IsValidShootPosition( vecCheckEnd, NULL ) )
			{
				if (GetOuter()->WeaponLOSCondition(vecCheckEnd,vecCheckStart,false))
				{
					AIMoveTrace_t moveTrace;
					GetOuter()->GetMoveProbe()->MoveLimit( NAV_GROUND, GetLocalOrigin(), vecCheckEnd, MASK_NPCSOLID, NULL, &moveTrace );
					if (moveTrace.fStatus == AIMR_OK)
					{
						return true;
					}
				}
		}
	}

	return false;
}


//-------------------------------------

bool CAI_TacticalServices::FindLateralLos( const Vector &vecThreat, Vector *pResult )
{
	Vector	vecLeftTest;
	Vector	vecRightTest;
	Vector	vecStepRight;
	Vector  vecCheckStart;
	int		i;

	if ( TestLateralLos( vecThreat, GetLocalOrigin() ) )
	{
		*pResult = GetLocalOrigin();
		return true;
	}

	Vector right;
	AngleVectors( GetLocalAngles(), NULL, &right, NULL );
	vecStepRight = right * COVER_DELTA;
	vecStepRight.z = 0;

	vecLeftTest = vecRightTest = GetLocalOrigin();
 	vecCheckStart = vecThreat;

	for ( i = 0 ; i < COVER_CHECKS ; i++ )
	{
		vecLeftTest = vecLeftTest - vecStepRight;
		vecRightTest = vecRightTest + vecStepRight;

		if (TestLateralLos( vecCheckStart, vecLeftTest ))
		{
			*pResult = vecLeftTest;
			return true;
		}

		if (TestLateralLos( vecCheckStart, vecRightTest ))
		{
			*pResult = vecRightTest;
			return true;
		}
	}

	return false;
}

//-------------------------------------

Vector CAI_TacticalServices::GetNodePos( int node )
{
	return GetNetwork()->GetNode((int)node)->GetPosition(GetHullType());
}

//-----------------------------------------------------------------------------
