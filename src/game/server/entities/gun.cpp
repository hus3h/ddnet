/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/version.h>

#include "character.h"
#include "gun.h"
#include "plasma.h"

CGun::CGun(CGameWorld *pGameWorld, vec2 Pos, bool Freeze, bool Explosive, int Layer, int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Freeze = Freeze;
	m_Explosive = Explosive;
	m_Layer = Layer;
	m_Number = Number;
	m_EvalTick = Server()->Tick();

	mem_zero(m_LastFireTeam, sizeof(m_LastFireTeam));
	mem_zero(m_LastFireSolo, sizeof(m_LastFireSolo));
	GameWorld()->InsertEntity(this);
}

void CGun::Tick()
{
	if(Server()->Tick() % int(Server()->TickSpeed() * 0.15f) == 0)
	{
		int Flags;
		m_EvalTick = Server()->Tick();
		int index = GameServer()->Collision()->IsMover(m_Pos.x, m_Pos.y, &Flags);
		if(index)
		{
			m_Core = GameServer()->Collision()->CpSpeed(index, Flags);
		}
		m_Pos += m_Core;
	}
	if(g_Config.m_SvPlasmaPerSec > 0)
	{
		Fire();
	}
}

void CGun::Fire()
{
	// Create a list of players who are in the range of the turret
	CCharacter *pPlayersInRange[MAX_CLIENTS];
	mem_zero(pPlayersInRange, sizeof(pPlayersInRange));

	int NumPlayersInRange = GameServer()->m_World.FindEntities(m_Pos, g_Config.m_SvPlasmaRange,
		(CEntity **)pPlayersInRange, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	// The closest player (within range) in a team is selected as the target
	int TargetIdInTeam[MAX_CLIENTS];
	bool IsTarget[MAX_CLIENTS];
	int MinDistInTeam[MAX_CLIENTS];
	mem_zero(MinDistInTeam, sizeof(MinDistInTeam));
	mem_zero(IsTarget, sizeof(IsTarget));
	for(int &TargetId : TargetIdInTeam)
	{
		TargetId = -1;
	}

	for(int i = 0; i < NumPlayersInRange; i++)
	{
		CCharacter *pTarget = pPlayersInRange[i];
		const int &TargetTeam = pTarget->Team();
		// Do not fire at super players
		if(TargetTeam == TEAM_SUPER)
		{
			continue;
		}
		// If the turret is disabled for the target's team, the turret will not fire
		if(m_Layer == LAYER_SWITCH && m_Number > 0 &&
			!Switchers()[m_Number].m_Status[TargetTeam])
		{
			continue;
		}

		// Turrets can only shoot at a speed of sv_plasma_per_sec
		const int &TargetClientId = pTarget->GetPlayer()->GetCID();
		const bool &TargetIsSolo = pTarget->Teams()->m_Core.GetSolo(TargetClientId);
		if((TargetIsSolo &&
			   m_LastFireSolo[TargetClientId] + Server()->TickSpeed() / g_Config.m_SvPlasmaPerSec > Server()->Tick()) ||
			(!TargetIsSolo &&
				m_LastFireTeam[TargetTeam] + Server()->TickSpeed() / g_Config.m_SvPlasmaPerSec > Server()->Tick()))
		{
			continue;
		}

		// Turrets can shoot only at reachable, alive players
		int IsReachable = !GameServer()->Collision()->IntersectLine(m_Pos, pTarget->m_Pos, 0, 0);
		if(IsReachable && pTarget->IsAlive())
		{
			// Turrets fire on solo players regardless of the rest of the team
			if(TargetIsSolo)
			{
				IsTarget[TargetClientId] = true;
				m_LastFireSolo[TargetClientId] = Server()->Tick();
			}
			else
			{
				int Distance = distance(pTarget->m_Pos, m_Pos);
				if(MinDistInTeam[TargetTeam] == 0 || MinDistInTeam[TargetTeam] > Distance)
				{
					MinDistInTeam[TargetTeam] = Distance;
					TargetIdInTeam[TargetTeam] = TargetClientId;
				}
			}
		}
	}

	// Set the closest player for each team as a target
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(TargetIdInTeam[i] != -1)
		{
			IsTarget[TargetIdInTeam[i]] = true;
			m_LastFireTeam[i] = Server()->Tick();
		}
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// Fire at each target
		if(IsTarget[i])
		{
			CCharacter *pTarget = GameServer()->GetPlayerChar(i);
			new CPlasma(&GameServer()->m_World, m_Pos, normalize(pTarget->m_Pos - m_Pos), m_Freeze, m_Explosive, i);
		}
	}
}

void CGun::Reset()
{
	m_MarkedForDestroy = true;
}

void CGun::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	int SnappingClientVersion = SnappingClient != SERVER_DEMO_CLIENT ?
					    GameServer()->GetClientVersion(SnappingClient) :
					    CLIENT_VERSIONNR;

	CNetObj_EntityEx *pEntData = 0;
	if(SnappingClientVersion >= VERSION_DDNET_SWITCH)
	{
		pEntData = static_cast<CNetObj_EntityEx *>(Server()->SnapNewItem(NETOBJTYPE_ENTITYEX, GetID(),
			sizeof(CNetObj_EntityEx)));
		if(pEntData)
		{
			pEntData->m_SwitchNumber = m_Number;
			pEntData->m_Layer = m_Layer;

			if(m_Explosive && !m_Freeze)
				pEntData->m_EntityClass = ENTITYCLASS_GUN_NORMAL;
			else if(m_Explosive && m_Freeze)
				pEntData->m_EntityClass = ENTITYCLASS_GUN_EXPLOSIVE;
			else if(!m_Explosive && m_Freeze)
				pEntData->m_EntityClass = ENTITYCLASS_GUN_FREEZE;
			else
				pEntData->m_EntityClass = ENTITYCLASS_GUN_UNFREEZE;
		}
	}
	else
	{
		// Emulate turned off blinking turret for old clients
		CCharacter *pChar = GameServer()->GetPlayerChar(SnappingClient);

		if(SnappingClient != SERVER_DEMO_CLIENT &&
			(GameServer()->m_apPlayers[SnappingClient]->GetTeam() == TEAM_SPECTATORS ||
				GameServer()->m_apPlayers[SnappingClient]->IsPaused()) &&
			GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID != SPEC_FREEVIEW)
			pChar = GameServer()->GetPlayerChar(GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID);

		int Tick = (Server()->Tick() % Server()->TickSpeed()) % 11;
		if(pChar && m_Layer == LAYER_SWITCH && m_Number > 0 &&
			!Switchers()[m_Number].m_Status[pChar->Team()] && (!Tick))
			return;
	}

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(
		NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));

	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;

	if(pEntData)
		pObj->m_StartTick = 0;
	else
		pObj->m_StartTick = m_EvalTick;
}
