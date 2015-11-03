#include <game/gamecore.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <game/layers.h>
#include "gamecontext.h"

#include "gamemodes/ctf.h"

#include "bot.h"
#include "player.h"
#include "entities/character.h"
#include "entities/flag.h"

CBot::CBot(CBotEngine *pBotEngine, CPlayer *pPlayer)
{
	m_pBotEngine = pBotEngine;
	m_pPlayer = pPlayer;
	m_pGameServer = pBotEngine->GameServer();
	m_State = BOT_IDLE;
	m_HookLength = Tuning()->m_HookLength*0.9f;
	m_Flags = 0;
	m_LastCheck = 0;
	mem_zero(m_AmmoCount, sizeof(m_AmmoCount));
	m_AmmoCount[WEAPON_HAMMER] = -1;
	m_Weapon = WEAPON_GUN;
	mem_zero(&m_InputData, sizeof(m_InputData));
	m_LastData = m_InputData;

	m_SnapID = GameServer()->Server()->SnapNewID();
	m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
}

CBot::~CBot()
{
	m_WalkingEdge.Reset();
	GameServer()->Server()->SnapFreeID(m_SnapID);
}

void CBot::OnReset()
{
	mem_zero(m_AmmoCount, sizeof(m_AmmoCount));
	m_AmmoCount[WEAPON_HAMMER] = -1;
	m_Weapon = WEAPON_GUN;
	m_Flags = 0;
	m_WalkingEdge.Reset();
	m_TargetClient = -1;
	m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
}

void CBot::UpdateTarget()
{
	bool FindNewTarget = m_ComputeTarget.m_Type == CTarget::TARGET_EMPTY;
	if(m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER && !(GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID] && GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()))
		FindNewTarget = true;

	if(m_ComputeTarget.m_Type == CTarget::TARGET_AIR)
	{
		int dist = distance(m_pPlayer->GetCharacter()->GetPos(), m_ComputeTarget.m_Pos);
		if(dist < 60)
			FindNewTarget = true;
	}
	if(m_ComputeTarget.m_Type > CTarget::TARGET_PLAYER)
	{
		int dist = distance(m_pPlayer->GetCharacter()->GetPos(), m_ComputeTarget.m_Pos);
		if(dist < 20)
			FindNewTarget = true;
	}

	if(FindNewTarget)
	{
		m_ComputeTarget.m_NeedUpdate = true;
		m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
		vec2 NewTarget;
		if(GameServer()->m_pController->IsFlagGame()) {
			int Team = m_pPlayer->GetTeam();
			CGameControllerCTF *pController = (CGameControllerCTF*)GameServer()->m_pController;
			CFlag **apFlags = pController->m_apFlags;
			if(apFlags[Team^1])
			{
				// Go to enemy flagstand
				if(apFlags[Team^1]->IsAtStand())
				{
					m_ComputeTarget.m_Pos = BotEngine()->GetFlagStandPos(Team^1);
					m_ComputeTarget.m_Type = CTarget::TARGET_FLAG;
					return;
				}
				// Go to base carrying flag
				if(apFlags[Team^1]->GetCarrier() == m_pPlayer->GetCharacter())// && (!apFlags[Team] || apFlags[Team]->IsAtStand())
				{
					m_ComputeTarget.m_Pos = BotEngine()->GetFlagStandPos(Team);
					m_ComputeTarget.m_Type = CTarget::TARGET_FLAG;
					return;
				}
			}
			if(apFlags[Team])
			{
				// Retrieve missing flag
				if(!apFlags[Team]->IsAtStand() && !apFlags[Team]->GetCarrier())
				{
					m_ComputeTarget.m_Pos = apFlags[Team]->GetPos();
					m_ComputeTarget.m_Type = CTarget::TARGET_FLAG;
					return;
				}
				// Target flag carrier
				if(!apFlags[Team]->IsAtStand() && apFlags[Team]->GetCarrier())
				{
					m_ComputeTarget.m_Pos = apFlags[Team]->GetPos();
					m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;
					m_ComputeTarget.m_PlayerCID = apFlags[Team]->GetCarrier()->GetPlayer()->GetCID();
					return;
				}
			}
		}
		if(random_int()&1)
		{
			int Team = m_pPlayer->GetTeam();
			int Count = 0;
			for(int c = 0; c < MAX_CLIENTS; c++)
				if(c != m_pPlayer->GetCID() && GameServer()->m_apPlayers[c] && GameServer()->m_apPlayers[c]->GetCharacter() && (GameServer()->m_apPlayers[c]->GetTeam() != Team || !GameServer()->m_pController->IsTeamplay()))
					Count++;

			if(Count)
			{
				Count = random_int()%Count+1;
				int c = 0;
				for(; Count; c++)
					if(c != m_pPlayer->GetCID() && GameServer()->m_apPlayers[c] && GameServer()->m_apPlayers[c]->GetCharacter() && (GameServer()->m_apPlayers[c]->GetTeam() != Team || !GameServer()->m_pController->IsTeamplay()))
						Count--;
				c--;
				m_ComputeTarget.m_Pos = GameServer()->m_apPlayers[c]->GetCharacter()->GetPos();
				m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;
				m_ComputeTarget.m_PlayerCID = c;
				return;
			}
		}
		// Random destination
		int r = random_int()%BotEngine()->GetGraph()->m_NumVertices;
		m_ComputeTarget.m_Pos = BotEngine()->GetGraph()->m_pVertices[r].m_Pos;
		m_ComputeTarget.m_Type = CTarget::TARGET_AIR;
		return;
	}

	if(m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID];
		if(Collision()->IntersectLine(m_ComputeTarget.m_Pos, pPlayer->GetCharacter()->GetPos(),0,0))
		{
			m_ComputeTarget.m_NeedUpdate = true;
			m_ComputeTarget.m_Pos = pPlayer->GetCharacter()->GetPos();
		}
	}
}

bool CBot::IsGrounded()
{
	vec2 Pos = m_pPlayer->GetCharacter()->GetPos();

	float PhysSize = 28.0f;

	// get ground state
	bool Grounded = false;
	if(Collision()->CheckPoint(Pos.x+PhysSize/2, Pos.y+PhysSize/2+5))
		Grounded = true;
	if(Collision()->CheckPoint(Pos.x-PhysSize/2, Pos.y+PhysSize/2+5))
		Grounded = true;
	return Grounded;
}

CNetObj_PlayerInput CBot::GetInputData()
{
	CheckState();
	if(!m_pPlayer->GetCharacter())
		return m_InputData;
	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->GetCore();
	int Team = m_pPlayer->GetTeam();

	UpdateTarget();

	UpdateEdge();

	mem_zero(&m_InputData, sizeof(m_InputData));

	vec2 Pos = pMe->m_Pos;

	bool InSight = false;
	if(m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER)
	{
		const CCharacterCore *pClosest = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->GetCore();
		InSight = !Collision()->IntersectLine(Pos, pClosest->m_Pos,0,0);
		m_Target = pClosest->m_Pos - Pos;
	}

	MakeChoice2(InSight);

	if(m_pPlayer->GetCharacter()->m_ReloadTimer <= 0)
		HandleWeapon(InSight);

	m_RealTarget = m_Target+Pos;

	HandleHook(InSight);

	if(m_Flags & BFLAG_LEFT)
			m_InputData.m_Direction = -1;
	if(m_Flags & BFLAG_RIGHT)
			m_InputData.m_Direction = 1;
	if(m_Flags & BFLAG_JUMP)
			m_InputData.m_Jump = 1;
	// else if(!m_InputData.m_Fire && m_Flags & BFLAG_FIRE && m_LastData.m_Fire == 0)
	// 	m_InputData.m_Fire = 1;

	// if(InSight && diffPos.y < - Close && diffVel.y < 0)
	// 	m_InputData.m_Jump = 1;

	m_InputData.m_TargetX = m_ComputeTarget.m_Pos.x;
	m_InputData.m_TargetX = m_ComputeTarget.m_Pos.y;
	if(m_InputData.m_Hook || m_InputData.m_Fire) {
		m_InputData.m_TargetX = m_Target.x;
		m_InputData.m_TargetY = m_Target.y;
	}


	if(!g_Config.m_SvBotAllowMove) {
		m_InputData.m_Direction = 0;
		m_InputData.m_Jump = 0;
		m_InputData.m_Hook = 0;
	}
	if(!g_Config.m_SvBotAllowHook)
		m_InputData.m_Hook = 0;

	m_LastData = m_InputData;
	return m_InputData;
}

void CBot::HandleHook(bool SeeTarget)
{
	CCharacterCore *pMe = m_pPlayer->GetCharacter()->GetCore();

	if(!pMe)
		return;
	int CurTile = GetTile(pMe->m_Pos.x, pMe->m_Pos.y);
	if(pMe->m_HookState == HOOK_FLYING)
	{
		m_InputData.m_Hook = 1;
		return;
	}
	if(SeeTarget)
	{
		const CCharacterCore *pClosest = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->GetCore();
		float dist = distance(pClosest->m_Pos,pMe->m_Pos);
		if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == m_ComputeTarget.m_PlayerCID)
			m_InputData.m_Hook = 1;
		else if(!m_InputData.m_Fire)
		{
			if(dist < m_HookLength*0.9f)
				m_InputData.m_Hook = m_LastData.m_Hook^1;
			SeeTarget = dist < m_HookLength*0.9f;
		}
	}

	if(!SeeTarget)
	{
		if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == -1)
		{
			vec2 HookVel = normalize(pMe->m_HookPos-pMe->m_Pos)*GameServer()->Tuning()->m_HookDragAccel;

			// from gamecore;cpp
			if(HookVel.y > 0)
				HookVel.y *= 0.3f;
			if((HookVel.x < 0 && pMe->m_Input.m_Direction < 0) || (HookVel.x > 0 && pMe->m_Input.m_Direction > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;

			HookVel += vec2(0,1)*GameServer()->Tuning()->m_Gravity;

			vec2 Target = m_Target;
			float ps = dot(Target, HookVel);
			if(ps > 0 || (CurTile & BTILE_HOLE && m_Target.y < 0 && pMe->m_Vel.y > 0.f && pMe->m_HookTick < SERVER_TICK_SPEED + SERVER_TICK_SPEED/2))
				m_InputData.m_Hook = 1;
			if(pMe->m_HookTick > 4*SERVER_TICK_SPEED || length(pMe->m_HookPos-pMe->m_Pos) < 20.0f)
				m_InputData.m_Hook = 0;
			// if(Flags & BFLAG_HOOK && ps < dot(Target,HookVel-Accel))
			// 	Flags ^= BFLAG_RIGHT | BFLAG_LEFT;
		}
		if(pMe->m_HookState == HOOK_FLYING)
			m_InputData.m_Hook = 1;
		// do random hook
		if(!m_InputData.m_Fire && m_LastData.m_Hook == 0 && pMe->m_HookState == HOOK_IDLE && (random_int()%10 == 0 || (CurTile & BTILE_HOLE && random_int()%4 == 0)))
		{
			int NumDir = BOT_HOOK_DIRS;
			vec2 HookDir(0.0f,0.0f);
			float MaxForce = (CurTile & BTILE_HOLE) ? -10000.0f : 0;
			vec2 Target = m_Target;
			for(int i = 0 ; i < NumDir; i++)
			{
				float a = 2*i*pi / NumDir;
				vec2 dir = direction(a);
				vec2 Pos = pMe->m_Pos+dir*m_HookLength;

				if((Collision()->IntersectLine(pMe->m_Pos,Pos,&Pos,0) & (CCollision::COLFLAG_SOLID | CCollision::COLFLAG_NOHOOK)) == CCollision::COLFLAG_SOLID)
				{
					vec2 HookVel = dir*GameServer()->Tuning()->m_HookDragAccel;

					// from gamecore.cpp
					if(HookVel.y > 0)
						HookVel.y *= 0.3f;
					if((HookVel.x < 0 && pMe->m_Input.m_Direction < 0) || (HookVel.x > 0 && pMe->m_Input.m_Direction > 0))
						HookVel.x *= 0.95f;
					else
						HookVel.x *= 0.75f;

					HookVel += vec2(0,1)*GameServer()->Tuning()->m_Gravity;

					float ps = dot(Target, HookVel);
					if( ps > MaxForce)
					{
						MaxForce = ps;
						HookDir = Pos - pMe->m_Pos;
					}
				}
			}
			if(length(HookDir) > 32.f)
			{
				m_Target = HookDir;
				m_InputData.m_Hook = 1;
				// if(Collision()->CheckPoint(pMe->m_Pos+normalize(vec2(0,m_Target.y))*28) && absolute(Target.x) < 30)
				// 	Flags = (Flags & (~BFLAG_LEFT)) | BFLAG_RIGHT;
			}
		}
	}
}

void CBot::HandleWeapon(bool SeeTarget)
{
	CCharacter *pMe = m_pPlayer->GetCharacter();

	if(!pMe)
		return;

	int Team = m_pPlayer->GetTeam();
	vec2 Pos = pMe->GetCore()->m_Pos;
	vec2 Vel = pMe->GetCore()->m_Vel;

	for(int c = 0 ; c < MAX_CLIENTS ; c++)
	{
		if(SeeTarget && c != m_ComputeTarget.m_PlayerCID)
			continue;
		if(c == m_pPlayer->GetCID())
		 	continue;
		if(!GameServer()->m_apPlayers[c] || !GameServer()->m_apPlayers[c]->GetCharacter() || (GameServer()->m_apPlayers[c]->GetTeam() == Team && GameServer()->m_pController->IsTeamplay()))
			continue;
		CCharacterCore* pTarget = GameServer()->m_apPlayers[c]->GetCharacter()->GetCore();

		float ClosestRange = distance(Pos, pTarget->m_Pos);
		float Close = 65.0f;
		vec2 Target = pTarget->m_Pos - Pos;

		int Weapon = -1;

		if(ClosestRange < Close)
		{
			Weapon = WEAPON_HAMMER;
		}
		else if(pMe->m_aWeapons[WEAPON_LASER].m_Ammo != 0 && ClosestRange < GameServer()->Tuning()->m_LaserReach && !Collision()->IntersectLine(Pos, pTarget->m_Pos, 0, 0))
	  {
	    Weapon = WEAPON_LASER;
	  }
		else
		{
			int GoodDir = -1;

			vec2 aProjectilePos[BOT_HOOK_DIRS];

			for(int i = 0 ; i < BOT_HOOK_DIRS ; i++) {
				vec2 dir = direction(2*i*pi / BOT_HOOK_DIRS);
				aProjectilePos[i] = Pos + dir*28.*0.75;
			}
			int Weapons[] = {WEAPON_GRENADE, WEAPON_SHOTGUN, WEAPON_GUN};
			for(int j = 0 ; j < 3 ; j++)
			{
				if(!pMe->m_aWeapons[Weapons[j]].m_Ammo)
					continue;
				float Curvature, Speed, DTime;
				switch(Weapons[j])
				{
					case WEAPON_GRENADE:
						Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
						Speed = GameServer()->Tuning()->m_GrenadeSpeed;
						DTime = GameServer()->Tuning()->m_GrenadeLifetime / 10.;
						break;

					case WEAPON_SHOTGUN:
						Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
						Speed = GameServer()->Tuning()->m_ShotgunSpeed;
						DTime = GameServer()->Tuning()->m_ShotgunLifetime / 10.;
						break;

					case WEAPON_GUN:
						Curvature = GameServer()->Tuning()->m_GunCurvature;
						Speed = GameServer()->Tuning()->m_GunSpeed;
						DTime = GameServer()->Tuning()->m_GunLifetime / 10.;
						break;
				}

				int DTick = (int) (DTime*GameServer()->Server()->TickSpeed());
				DTime *= Speed;

				vec2 TargetPos = pTarget->m_Pos;
				vec2 TargetVel = pTarget->m_Vel*DTick;

				int aIsDead[BOT_HOOK_DIRS] = {0};

				for(int k = 0; k < 10 && GoodDir == -1; k++) {
					for(int i = 0; i < BOT_HOOK_DIRS; i++) {
						if(aIsDead[i])
							continue;
						vec2 dir = direction(2*i*pi / BOT_HOOK_DIRS);
						vec2 NextPos = aProjectilePos[i];
						NextPos.x += dir.x*DTime;
						NextPos.y += dir.y*DTime + Curvature/10000*(DTime*DTime)*(2*k+1);
						aIsDead[i] = Collision()->IntersectLine(aProjectilePos[i], NextPos, &NextPos, 0);
						vec2 InterPos = closest_point_on_line(aProjectilePos[i],NextPos, TargetPos);
						if(distance(TargetPos, InterPos)< 28) {
							GoodDir = i;
						}
						aProjectilePos[i] = NextPos;
					}
					Collision()->IntersectLine(TargetPos, TargetPos+TargetVel, 0, &TargetPos);
					TargetVel.y += GameServer()->Tuning()->m_Gravity*DTick*DTick;
				}
				if(GoodDir != -1)
				{
					Target = direction(2*GoodDir*pi / BOT_HOOK_DIRS)*50;
					Weapon = Weapons[j];
					break;
				}
			}
		}
		if(Weapon > -1)
		{
			if(m_LastData.m_WantedWeapon != Weapon+1)
			{
				m_InputData.m_WantedWeapon = Weapon+1;
				m_InputData.m_Fire = 0;
			}
			else
				m_InputData.m_Fire = m_LastData.m_Fire^1;
			if(m_InputData.m_Fire)
				m_Target = Target;
			break;
		}
	}

	// Accuracy
	// float Angle = angle(m_Target) + (random_int()%64-32)*pi / 1024.0f;
	// m_Target = direction(Angle)*length(m_Target);
}

void CBot::UpdateEdge()
{
	vec2 Pos = m_pPlayer->GetCharacter()->GetPos();
	if(m_ComputeTarget.m_Type == CTarget::TARGET_EMPTY)
		return;
	if(m_ComputeTarget.m_NeedUpdate)
	{
		m_WalkingEdge.Reset();
		m_WalkingEdge = BotEngine()->GetPath(Pos, m_ComputeTarget.m_Pos);
		m_ComputeTarget.m_NeedUpdate = false;
		dbg_msg("bot", "new path of size=%d", m_WalkingEdge.m_Size);
		// for(int i = 0; i < m_WalkingEdge.m_Size; i++)
		// 	dbg_msg("bot", "\t(%f, %f)", m_WalkingEdge.m_pPath[i].x, m_WalkingEdge.m_pPath[i].y);
	}
}

void CBot::MakeChoice2(bool UseTarget)
{
	if(UseTarget)
	{
		MakeChoice();
		return;
	}
	vec2 Pos = m_pPlayer->GetCharacter()->GetPos();

	if(m_WalkingEdge.m_Size)
	{
		int dist = BotEngine()->FarestPointOnEdge(m_WalkingEdge, Pos, &m_Target);
		if(dist >= 0)
		{
			UseTarget = true;
			m_Target -= Pos;
		}
	}
	MakeChoice();
}

void CBot::MakeChoice()
{
	int Flags = 0;
	CCharacterCore *pMe = m_pPlayer->GetCharacter()->GetCore();
	CCharacterCore TempChar = *pMe;
	TempChar.m_Input = m_InputData;
	vec2 CurPos = TempChar.m_Pos;

	int CurTile = GetTile(TempChar.m_Pos.x, TempChar.m_Pos.y);
	bool Grounded = IsGrounded();

	TempChar.m_Input.m_Direction = (m_Target.x > 28.f) ? 1 : (m_Target.x < -28.f) ? -1:0;
	CWorldCore TempWorld;
	TempWorld.m_Tuning = *GameServer()->Tuning();
	TempChar.Init(&TempWorld, Collision());
	TempChar.Tick(true);
	TempChar.Move();
	TempChar.Quantize();

	int NextTile = GetTile(TempChar.m_Pos.x, TempChar.m_Pos.y);
	vec2 NextPos = TempChar.m_Pos;

	if(TempChar.m_Input.m_Direction > 0)
		Flags |= BFLAG_RIGHT;

	if(TempChar.m_Input.m_Direction < 0)
		Flags |= BFLAG_LEFT;

	if(CurTile & BTILE_SAFE && NextTile & BTILE_HOLE && Grounded)
	{
		if(m_Target.y < 0)
			Flags |= BFLAG_JUMP;
	}
	if(CurTile & BTILE_SAFE && NextTile & BTILE_SAFE)
	{
		static bool tried = false;
		if(absolute(CurPos.x - NextPos.x) < 1.0f && TempChar.m_Input.m_Direction)
		{
			if(Grounded)
			{
				Flags |= BFLAG_JUMP;
				tried = true;
			}
			else if(tried && !(TempChar.m_Jumped) && TempChar.m_Vel.y > 0)
				Flags |= BFLAG_JUMP;
			else if(tried && TempChar.m_Jumped & 2 && TempChar.m_Vel.y > 0)
				Flags ^= BFLAG_RIGHT | BFLAG_LEFT;
		}
		else
			tried = false;
		// if(m_Target.y < 0 && TempChar.m_Vel.y > 1.f && !(TempChar.m_Jumped) && !Grounded)
		// 	Flags |= BFLAG_JUMP;
	}

	if(!(pMe->m_Jumped))
	{
		vec2 Vel(pMe->m_Vel.x, min(pMe->m_Vel.y, 0.0f));
		if(Collision()->IntersectLine(pMe->m_Pos,pMe->m_Pos+Vel*10.0f,0,0) && !Collision()->IntersectLine(pMe->m_Pos,pMe->m_Pos+(Vel-vec2(0,TempWorld.m_Tuning.m_AirJumpImpulse))*10.0f,0,0) && (m_Target.y < 0))
			Flags |= BFLAG_JUMP;
		if(m_Target.y < 0 && absolute(m_Target.x) < 28.f && pMe->m_Vel.y > -1.f)
			Flags |= BFLAG_JUMP;
	}
	// if(Flags & BFLAG_JUMP || pMe->m_Vel.y < 0)
	// 	m_InputData.m_WantedWeapon = WEAPON_GRENADE +1;
	// if(m_Target.y < -400 && pMe->m_Vel.y < 0 && absolute(m_Target.x) < 30 && Collision()->CheckPoint(pMe->m_Pos+vec2(0,50)))
	// {
	// 	Flags &= ~BFLAG_HOOK;
	// 	Flags |= BFLAG_FIRE;
	// 	m_Target = vec2(0,28);
	// }
	// else if(m_Target.y < -300 && pMe->m_Vel.y < 0 && absolute(m_Target.x) < 30 && Collision()->CheckPoint(pMe->m_Pos+vec2(32,48)))
	// {
	// 	Flags &= ~BFLAG_HOOK;
	// 	Flags |= BFLAG_FIRE;
	// 	m_Target = vec2(14,28);
	// }
	// else if(m_Target.y < -300 && pMe->m_Vel.y < 0 && absolute(m_Target.x) < 30 && Collision()->CheckPoint(pMe->m_Pos+vec2(-32,48)))
	// {
	// 	Flags &= ~BFLAG_HOOK;
	// 	Flags |= BFLAG_FIRE;
	// 	m_Target = vec2(-14,28);
	// }
	m_Flags = Flags;
}

void CBot::CheckState()
{
	if(time_get() - m_LastCheck > BOT_CHECK_TIME)
	{
		//Say(0, "I am 100% computer controlled.");
	}
}

void CBot::Snap(int SnappingClient)
{
	if(SnappingClient == -1)
		return;

	CCharacter *pMe = m_pPlayer->GetCharacter();
	if(!pMe)
		return;

	vec2 Pos = pMe->GetCore()->m_Pos;
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(GameServer()->Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)(m_RealTarget.x);
		pObj->m_Y = (int)(m_RealTarget.y);
		pObj->m_FromX = (int)Pos.x;
		pObj->m_FromY = (int)Pos.y;
		pObj->m_StartTick = GameServer()->Server()->Tick();
	}
	// for(int l = 1 ; l < m_WalkingEdge.m_Size-1 ; l++)
	// {
	// 	vec2 From = m_WalkingEdge.m_pPath[l-1];
	// 	vec2 To = m_WalkingEdge.m_pPath[l];
	// 	if(BotEngine()->NetworkClipped(SnappingClient, To) && BotEngine()->NetworkClipped(SnappingClient, From))
	// 		continue;
	// 	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(GameServer()->Server()->SnapNewItem(NETOBJTYPE_LASER, m_WalkingEdge.m_pSnapID[l], sizeof(CNetObj_Laser)));
	// 	if(!pObj)
	// 		return;
	// 	pObj->m_X = (int) To.x;
	// 	pObj->m_Y = (int) To.y;
	// 	pObj->m_FromX = (int) From.x;
	// 	pObj->m_FromY = (int) From.y;
	// 	pObj->m_StartTick = GameServer()->Server()->Tick();
	// }
}

const char *CBot::GetName() {
	return g_BotName[m_pPlayer->GetCID()];
}