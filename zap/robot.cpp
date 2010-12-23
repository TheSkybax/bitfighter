//-----------------------------------------------------------------------------------
//
// Bitfighter - A multiplayer vector graphics space game
// Based on Zap demo released for Torque Network Library by GarageGames.com
//
// Derivative work copyright (C) 2008-2009 Chris Eykamp
// Original work copyright (C) 2004 GarageGames.com, Inc.
// Other code copyright as noted
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful (and fun!),
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//------------------------------------------------------------------------------------

#include "robot.h"
#include "item.h"

#include "gameType.h"
#include "sparkManager.h"
#include "projectile.h"
#include "gameLoader.h"
#include "sfx.h"
#include "UI.h"
#include "UIMenus.h"
#include "UIGame.h"
#include "gameConnection.h"
#include "shipItems.h"
#include "playerInfo.h"          // For RobotPlayerInfo constructor
#include "gameItems.h"
#include "gameWeapons.h"
#include "gameObjectRender.h"
#include "flagItem.h"
#include "goalZone.h"
#include "loadoutZone.h"
#include "soccerGame.h"          // For lua object defs
#include "huntersGame.h"         // For lua object defs
#include "engineeredObjects.h"   // For lua object defs
#include "teleporter.h"          // ""
#include "../lua/luaprofiler-2.0.2/src/luaprofiler.h"      // For... the profiler!
#include "config.h"
#include "BotNavMeshZone.h"      // For BotNavMeshZone class definition
#include "luaGameInfo.h"
#include "luaUtil.h"
#include "glutInclude.h"

#define hypot _hypot    // Kill some warnings

namespace Zap
{

static Vector<DatabaseObject *> fillVector;      // Reusable workspace for writing lists of objects

static EventManager eventManager;                // Singleton event manager, one copy is used by all bots


// Constructor
LuaRobot::LuaRobot(lua_State *L) : LuaShip((Robot *)lua_touserdata(L, 1))
{
   lua_atpanic(L, luaPanicked);                  // Register our panic function
   thisRobot = (Robot *)lua_touserdata(L, 1);    // Register our robot
   thisRobot->mLuaRobot = this;

   // Initialize all subscriptions to unsubscribed
   for(S32 i = 0; i < EventManager::EventTypes; i++)
      subscriptions[i] = false;

   // The following sets scads of global vars in the Lua instance that mimic the use of the enums we use everywhere

   // Game Objects
   setEnum(ShipType);
   setEnum(BarrierType);
   setEnum(MoveableType);

   setEnum(BulletType);
   setEnum(MineType);
   setEnum(SpyBugType);

   setEnum(ResourceItemType);
   setEnum(ForceFieldType);
   setEnum(LoadoutZoneType);
   setEnum(TestItemType);
   setEnum(FlagType);
   setEnum(TurretTargetType);
   setEnum(SlipZoneType);
   setEnum(NexusType);
   setEnum(BotNavMeshZoneType);
   setEnum(RobotType);
   setEnum(TeleportType);
   setEnum(GoalZoneType);
   setEnum(AsteroidType);
   setEnum(RepairItemType);
   setEnum(EnergyItemType);
   setEnum(SoccerBallItemType);
   setEnum(TurretType);
   setEnum(ForceFieldProjectorType);


   // Modules
   setEnum(ModuleShield);
   setEnum(ModuleBoost);
   setEnum(ModuleSensor);
   setEnum(ModuleRepair);
   setEnum(ModuleEngineer);
   setEnum(ModuleCloak);
   setEnum(ModuleArmor);

   // Weapons
   setEnum(WeaponPhaser);
   setEnum(WeaponBounce);
   setEnum(WeaponTriple);
   setEnum(WeaponBurst);
   setEnum(WeaponMine);
   setEnum(WeaponSpyBug);
   setEnum(WeaponTurret);

      // Game Types
   setGTEnum(BitmatchGame);
   setGTEnum(CTFGame);
   setGTEnum(HTFGame);
   setGTEnum(NexusGame);
   setGTEnum(RabbitGame);
   setGTEnum(RetrieveGame);
   setGTEnum(SoccerGame);
   setGTEnum(ZoneControlGame);

   // Scoring Events
   setGTEnum(KillEnemy);
   setGTEnum(KillSelf);
   setGTEnum(KillTeammate);
   setGTEnum(KillEnemyTurret);
   setGTEnum(KillOwnTurret);
   setGTEnum(KilledByAsteroid);
   setGTEnum(KilledByTurret);
   setGTEnum(CaptureFlag);
   setGTEnum(CaptureZone);
   setGTEnum(UncaptureZone);
   setGTEnum(HoldFlagInZone);
   setGTEnum(RemoveFlagFromEnemyZone);
   setGTEnum(RabbitHoldsFlag);
   setGTEnum(RabbitKilled);
   setGTEnum(RabbitKills);
   setGTEnum(ReturnFlagsToNexus);
   setGTEnum(ReturnFlagToZone);
   setGTEnum(LostFlag);
   setGTEnum(ReturnTeamFlag);
   setGTEnum(ScoreGoalEnemyTeam);
   setGTEnum(ScoreGoalHostileTeam);
   setGTEnum(ScoreGoalOwnTeam);

   // Event handler events
   setEventEnum(ShipSpawnedEvent);
   setEventEnum(ShipKilledEvent);
   setEventEnum(MsgReceivedEvent);
   setEventEnum(PlayerJoinedEvent);
   setEventEnum(PlayerLeftEvent);


   // A few misc constants -- in Lua, we reference the teams as first team == 1, so neutral will be 0 and hostile -1
   lua_pushinteger(L, 0); lua_setglobal(L, "NeutralTeamIndx");
   lua_pushinteger(L, -1); lua_setglobal(L, "HostileTeamIndx");
}

// Destructor
LuaRobot::~LuaRobot()
{
   // Make sure we're unsubscribed to all those events we subscribed to.  Don't want to
   // send an event to a dead bot, after all...
   for(S32 i = 0; i < EventManager::EventTypes; i++)
      if(subscriptions[i])
         eventManager.unsubscribeImmediate(thisRobot->getL(), (EventManager::EventType)i);

   logprintf(LogConsumer::LogLuaObjectLifecycle, "Deleted Lua Robot Object (%p)\n", this);
}


// Define the methods we will expose to Lua
// Methods defined here need to be defined in the LuaRobot in robot.h

Lunar<LuaRobot>::RegType LuaRobot::methods[] = {
   method(LuaRobot, getClassID),

   method(LuaRobot, getCPUTime),
   method(LuaRobot, getTime),

   // These inherited from LuaShip
   method(LuaRobot, isAlive),

   method(LuaRobot, getLoc),
   method(LuaRobot, getRad),
   method(LuaRobot, getVel),
   method(LuaRobot, getTeamIndx),

   method(LuaRobot, isModActive),
   method(LuaRobot, getEnergy),
   method(LuaRobot, getHealth),
   method(LuaRobot, hasFlag),
   method(LuaRobot, getFlagCount),

   method(LuaRobot, getAngle),
   method(LuaRobot, getActiveWeapon),
   // End inherited methods

   method(LuaRobot, getZoneCenter),
   method(LuaRobot, getGatewayFromZoneToZone),
   method(LuaRobot, getZoneCount),
   method(LuaRobot, getCurrentZone),

   method(LuaRobot, setAngle),
   method(LuaRobot, setAnglePt),
   method(LuaRobot, getAnglePt),
   method(LuaRobot, hasLosPt),

   method(LuaRobot, getWaypoint),

   method(LuaRobot, setThrust),
   method(LuaRobot, setThrustPt),
   method(LuaRobot, setThrustToPt),

   method(LuaRobot, fire),
   method(LuaRobot, setWeapon),
   method(LuaRobot, setWeaponIndex),
   method(LuaRobot, hasWeapon),

   method(LuaRobot, activateModule),
   method(LuaRobot, activateModuleIndex),
   method(LuaRobot, setReqLoadout),
   method(LuaRobot, getCurrLoadout),
   method(LuaRobot, getReqLoadout),

   method(LuaRobot, subscribe),
   method(LuaRobot, unsubscribe),

   method(LuaRobot, globalMsg),
   method(LuaRobot, teamMsg),

   method(LuaRobot, getActiveWeapon),

   method(LuaRobot, findItems),
   method(LuaRobot, findGlobalItems),
   method(LuaRobot, getFiringSolution),
   method(LuaRobot, getInterceptCourse),     // Doesn't work well...

   {0,0}    // End method list
};


S32 LuaRobot::getClassID(lua_State *L)
{
   return returnInt(L, RobotType);
}


// Return CPU time... use for timing things
S32 LuaRobot::getCPUTime(lua_State *L)
{
   return returnInt(L, gServerGame->getCurrentTime());
}


// Turn to angle a (in radians, or toward a point)
S32 LuaRobot::setAngle(lua_State *L)
{
   static const char *methodName = "Robot:setAngle()";

   if(lua_isnumber(L, 1))
   {
      checkArgCount(L, 1, methodName);

      Move move = thisRobot->getCurrentMove();
      move.angle = getFloat(L, 1, methodName);
      thisRobot->setCurrentMove(move);
   }
   else  // Could be a point?
   {
      checkArgCount(L, 1, methodName);
      Point point = getPoint(L, 1, methodName);

      Move move = thisRobot->getCurrentMove();
      move.angle = thisRobot->getAnglePt(point);
      thisRobot->setCurrentMove(move);
   }

   return 0;
}


// Turn towards point XY
S32 LuaRobot::setAnglePt(lua_State *L)
{
   static const char *methodName = "Robot:setAnglePt()";
   checkArgCount(L, 1, methodName);
   Point point = getPoint(L, 1, methodName);

   Move move = thisRobot->getCurrentMove();
   move.angle = thisRobot->getAnglePt(point);
   thisRobot->setCurrentMove(move);

   return 0;
}

// Get angle toward point
S32 LuaRobot::getAnglePt(lua_State *L)
{
   static const char *methodName = "Robot:getAnglePt()";
   checkArgCount(L, 1, methodName);
   Point point = getPoint(L, 1, methodName);

   lua_pushnumber(L, thisRobot->getAnglePt(point));
   return 1;
}


// Thrust at velocity v toward angle a
S32 LuaRobot::setThrust(lua_State *L)
{
   static const char *methodName = "Robot:setThrust()";
   checkArgCount(L, 2, methodName);
   F32 vel = getFloat(L, 1, methodName);
   F32 ang = getFloat(L, 2, methodName);

   Move move = thisRobot->getCurrentMove();

   move.up = sin(ang) <= 0 ? -vel * sin(ang) : 0;
   move.down = sin(ang) > 0 ? vel * sin(ang) : 0;
   move.right = cos(ang) >= 0 ? vel * cos(ang) : 0;
   move.left = cos(ang) < 0 ? -vel * cos(ang) : 0;

   thisRobot->setCurrentMove(move);

   return 0;
}


extern bool FindLowestRootInInterval(F32 inA, F32 inB, F32 inC, F32 inUpperBound, F32 &outX);

bool calcInterceptCourse(GameObject *target, Point aimPos, F32 aimRadius, S32 aimTeam, F32 aimVel, F32 aimLife, bool ignoreFriendly, F32 &interceptAngle)
{
   Point offset = target->getActualPos() - aimPos;    // Account for fact that robot doesn't fire from center
   offset.normalize(aimRadius * 1.2);    // 1.2 is a fudge factor to prevent robot from not shooting because it thinks it will hit itself
   aimPos += offset;

   if(target->getObjectTypeMask() & ( ShipType | RobotType))
   {
      Ship *potential = (Ship*)target;

      // Is it dead or cloaked?  If so, ignore
      if((potential->isModuleActive(ModuleCloak) && !potential->areItemsMounted()) || potential->hasExploded)
         return false;
   }

   if(ignoreFriendly && target->getTeam() == aimTeam)      // Is target on our team?
      return false;                                        // ...if so, skip it!

   // Calculate where we have to shoot to hit this...
   Point Vs = target->getActualVel();

   Point d = target->getActualPos() - aimPos;

   F32 t;      // t is set in next statement
   if(!FindLowestRootInInterval(Vs.dot(Vs) - aimVel * aimVel, 2 * Vs.dot(d), d.dot(d), aimLife * 0.001f, t))
      return false;

   Point leadPos = target->getActualPos() + Vs * t;

   // Calculate distance
   Point delta = (leadPos - aimPos);

   // Make sure we can see it...
   Point n;
   if(target->findObjectLOS(BarrierType, MoveObject::ActualState, aimPos, target->getActualPos(), t, n))
      return false;

   // See if we're gonna clobber our own stuff...
   target->disableCollision();
   Point delta2 = delta;
   delta2.normalize(aimLife * aimVel / 1000);
   GameObject *hitObject = target->findObjectLOS(ShipType | RobotType | BarrierType | EngineeredType, 0, aimPos, aimPos + delta2, t, n);
   target->enableCollision();

   if(ignoreFriendly && hitObject && hitObject->getTeam() == aimTeam)
      return false;

   interceptAngle = delta.ATAN2();

   return true;
}


// Given an object, which angle do we need to be at to fire to hit it?
// Returns nil if a workable solution can't be found
// Logic adapted from turret aiming algorithm
// Note that bot WILL fire at teammates if you ask it to!
S32 LuaRobot::getFiringSolution(lua_State *L)
{
   static const char *methodName = "Robot:getFiringSolution()";
   checkArgCount(L, 2, methodName);
   U32 type = getInt(L, 1, methodName);
   GameObject *target = getItem(L, 2, type, methodName)->getGameObject();

   WeaponInfo weap = gWeapons[thisRobot->getSelectedWeapon()];    // Robot's active weapon

   F32 interceptAngle;

   if(calcInterceptCourse(target, thisRobot->getActualPos(), thisRobot->getRadius(), thisRobot->getTeam(), weap.projVelocity, weap.projLiveTime, false, interceptAngle))
      return returnFloat(L, interceptAngle);

   return returnNil(L);
}


// Given an object, what angle do we need to fly toward in order to collide with an object?  This
// works a lot like getFiringSolution().
S32 LuaRobot::getInterceptCourse(lua_State *L)
{
   static const char *methodName = "Robot:getInterceptCourse()";
   checkArgCount(L, 2, methodName);
   U32 type = getInt(L, 1, methodName);
   GameObject *target = getItem(L, 2, type, methodName)->getGameObject();

   WeaponInfo weap = gWeapons[thisRobot->getSelectedWeapon()];    // Robot's active weapon

   F32 interceptAngle;
   bool ok = calcInterceptCourse(target, thisRobot->getActualPos(), thisRobot->getRadius(), thisRobot->getTeam(), 256, 3000, false, interceptAngle);
   if(!ok)
      return returnNil(L);

   return returnFloat(L, interceptAngle);
}


// Thrust at velocity v toward point x,y
S32 LuaRobot::setThrustPt(lua_State *L)      // (number, point)
{
   static const char *methodName = "Robot:setThrustPt()";
   checkArgCount(L, 2, methodName);
   F32 vel = getFloat(L, 1, methodName);
   Point point = getPoint(L, 2, methodName);

   F32 ang = thisRobot->getAnglePt(point) - 0 * FloatHalfPi;

   Move move = thisRobot->getCurrentMove();

   move.up = sin(ang) < 0 ? -vel * sin(ang) : 0;
   move.down = sin(ang) > 0 ? vel * sin(ang) : 0;
   move.right = cos(ang) > 0 ? vel * cos(ang) : 0;
   move.left = cos(ang) < 0 ? -vel * cos(ang) : 0;

   thisRobot->setCurrentMove(move);

  return 0;
}


// Thrust toward specified point, but slow speed so that we land directly on that point if it is within range
S32 LuaRobot::setThrustToPt(lua_State *L)
{
   static const char *methodName = "Robot:setThrustToPt()";
   checkArgCount(L, 1, methodName);
   Point point = getPoint(L, 1, methodName);

   F32 ang = thisRobot->getAnglePt(point) - 0 * FloatHalfPi;

   Move move = thisRobot->getCurrentMove();

   F32 dist = thisRobot->getActualPos().distanceTo(point);

   F32 vel = dist / ((F32) move.time);      // v = d / t, t is in ms

   move.up = sin(ang) < 0 ? -vel * sin(ang) : 0;
   move.down = sin(ang) > 0 ? vel * sin(ang) : 0;
   move.right = cos(ang) > 0 ? vel * cos(ang) : 0;
   move.left = cos(ang) < 0 ? -vel * cos(ang) : 0;

   thisRobot->setCurrentMove(move);

  return 0;
}

extern Vector<SafePtr<BotNavMeshZone> > gBotNavMeshZones;

// Get the coords of the center of mesh zone z
S32 LuaRobot::getZoneCenter(lua_State *L)
{
   static const char *methodName = "Robot:getZoneCenter()";
   checkArgCount(L, 1, methodName);
   S32 z = getInt(L, 1, methodName);

   // In case this gets called too early...
   if(gBotNavMeshZones.size() == 0)
      return returnNil(L);

   // Bounds checking...
   if(z < 0 || z >= gBotNavMeshZones.size())
      return returnNil(L);

   return returnPoint(L, gBotNavMeshZones[z]->getCenter());
}


// Get the coords of the gateway to the specified zone.  Returns point, nil if requested zone doesn't border current zone.
S32 LuaRobot::getGatewayFromZoneToZone(lua_State *L)
{
   static const char *methodName = "Robot:getGatewayFromZoneToZone()";
   checkArgCount(L, 2, methodName);
   S32 from = getInt(L, 1, methodName);
   S32 to = getInt(L, 2, methodName);

   // In case this gets called too early...
   if(gBotNavMeshZones.size() == 0)
      return returnNil(L);

   // Bounds checking...
   if(from < 0 || from >= gBotNavMeshZones.size() || to < 0 || to >= gBotNavMeshZones.size())
      return returnNil(L);

   // Is requested zone a neighbor?
   for(S32 i = 0; i < gBotNavMeshZones[from]->mNeighbors.size(); i++)
   {
      if(gBotNavMeshZones[from]->mNeighbors[i].zoneID == to)
      {
         Rect r(gBotNavMeshZones[from]->mNeighbors[i].borderStart, gBotNavMeshZones[from]->mNeighbors[i].borderEnd);
         return returnPoint(L, r.getCenter());
      }
   }

   // Did not find requested neighbor... returning nil
   return returnNil(L);
}


// Get the zone this robot is currently in.  If not in a zone, return nil
S32 LuaRobot::getCurrentZone(lua_State *L)
{
   S32 zone = thisRobot->getCurrentZone();
   return (zone == -1) ? returnNil(L) : returnInt(L, zone);
}


// Get a count of how many nav zones we have
S32 LuaRobot::getZoneCount(lua_State *L)
{
   return returnInt(L, gBotNavMeshZones.size());
}


// Fire current weapon if possible
S32 LuaRobot::fire(lua_State *L)
{
   Move move = thisRobot->getCurrentMove();
   move.fire = true;
   thisRobot->setCurrentMove(move);

   return 0;
}


// Can robot see point P?
S32 LuaRobot::hasLosPt(lua_State *L)      // Now takes a point or an x,y
{
   static const char *methodName = "Robot:hasLosPt()";
   //checkArgCount(L, 1, methodName);
   Point point = getPointOrXY(L, 1, methodName);

   return returnBool(L, thisRobot->canSeePoint(point));
}


// Set weapon to index
S32 LuaRobot::setWeaponIndex(lua_State *L)
{
   static const char *methodName = "Robot:setWeaponIndex()";
   checkArgCount(L, 1, methodName);
   U32 weap = getInt(L, 1, methodName, 1, ShipWeaponCount);    // Acceptable range = (1, ShipWeaponCount)
   thisRobot->selectWeapon(weap - 1);     // Correct for the fact that index in C++ is 0 based

   return 0;
}


// Set weapon to specified weapon, if we have it
S32 LuaRobot::setWeapon(lua_State *L)
{
   static const char *methodName = "Robot:setWeapon()";
   checkArgCount(L, 1, methodName);
   U32 weap = getInt(L, 1, methodName, 0, WeaponCount - 1);

   for(S32 i = 0; i < ShipWeaponCount; i++)
      if((U32)thisRobot->getWeapon(i) == weap)
      {
         thisRobot->selectWeapon(i);
         break;
      }

   // If we get here without having found our weapon, then nothing happens.  Better luck next time!
   return 0;
}


// Do we have a given weapon in our current loadout?
S32 LuaRobot::hasWeapon(lua_State *L)
{
   static const char *methodName = "Robot:hasWeapon()";
   checkArgCount(L, 1, methodName);
   U32 weap = getInt(L, 1, methodName, 0, WeaponCount - 1);

   for(S32 i = 0; i < ShipWeaponCount; i++)
      if((U32)thisRobot->getWeapon(i) == weap)
         return returnBool(L, true);      // We have it!

   return returnBool(L, false);           // We don't!
}


// Activate module this cycle --> takes module index
S32 LuaRobot::activateModuleIndex(lua_State *L)
{
   static const char *methodName = "Robot:activateModuleIndex()";

   checkArgCount(L, 1, methodName);
   U32 indx = getInt(L, 1, methodName, 0, ShipModuleCount);

   thisRobot->activateModule(indx);

   return 0;
}


// Activate module this cycle --> takes module enum.
// If specified module is not part of the loadout, does nothing.
S32 LuaRobot::activateModule(lua_State *L)
{
   static const char *methodName = "Robot:activateModule()";

   checkArgCount(L, 1, methodName);
   ShipModule mod = (ShipModule) getInt(L, 1, methodName, 0, ModuleCount - 1);

   for(S32 i = 0; i < ShipModuleCount; i++)
      if(thisRobot->getModule(i) == mod)
      {
         thisRobot->activateModule(i);
         break;
      }

   return 0;
}


// Sets loadout to specified --> takes 2 modules, 3 weapons
S32 LuaRobot::setReqLoadout(lua_State *L)
{
   checkArgCount(L, 1, "Robot:setReqLoadout()");

   LuaLoadout *loadout = Lunar<LuaLoadout>::check(L, 1);
   Vector<U32> vec;

   for(S32 i = 0; i < ShipModuleCount + ShipWeaponCount; i++)
      vec.push_back(loadout->getLoadoutItem(i));

   thisRobot->setLoadout(vec);

   return 0;
}


// Return current loadout
S32 LuaRobot::getCurrLoadout(lua_State *L)
{
   U32 loadoutItems[ShipModuleCount + ShipWeaponCount];

   for(S32 i = 0; i < ShipModuleCount; i++)
      loadoutItems[i] = (U32) thisRobot->getModule(i);

   for(S32 i = 0; i < ShipWeaponCount; i++)
      loadoutItems[i + ShipModuleCount] = (U32) thisRobot->getWeapon(i);

   LuaLoadout *loadout = new LuaLoadout(loadoutItems);
   Lunar<LuaLoadout>::push(L, loadout, false);     // true will allow Lua to delete this object when it goes out of scope

   return 1;
}


extern Vector<LoadoutItem> gLoadoutModules;
extern Vector<LoadoutItem> gLoadoutWeapons;

// Return requested loadout
S32 LuaRobot::getReqLoadout(lua_State *L)
{
   U32 loadoutItems[ShipModuleCount + ShipWeaponCount];

   for(S32 i = 0; i < ShipModuleCount; i++)
      loadoutItems[i] = (U32) gLoadoutModules[thisRobot->getModule(i)].index;

   for(S32 i = 0; i < ShipWeaponCount; i++)
      loadoutItems[i + ShipModuleCount] = (U32) gLoadoutWeapons[thisRobot->getWeapon(i)].index;

   LuaLoadout *loadout = new LuaLoadout(loadoutItems);
   Lunar<LuaLoadout>::push(L, loadout, true);     // true will allow Lua to delete this object when it goes out of scope

   return 1;
}


// Send message to all players
S32 LuaRobot::globalMsg(lua_State *L)
{
   static const char *methodName = "Robot:globalMsg()";
   checkArgCount(L, 1, methodName);

   const char *message = getString(L, 1, methodName);

   GameType *gt = gServerGame->getGameType();
   if(gt)
   {
      gt->s2cDisplayChatMessage(true, thisRobot->getName(), message);

      // Fire our event handler
      Robot::getEventManager().fireEvent(thisRobot->getL(), EventManager::MsgReceivedEvent, message, thisRobot->getPlayerInfo(), true);
   }

   return 0;
}


// Send message to team (what happens when neutral/enemytoall robot does this???)
S32 LuaRobot::teamMsg(lua_State *L)
{
   static const char *methodName = "Robot:teamMsg()";
   checkArgCount(L, 1, methodName);

   const char *message = getString(L, 1, methodName);

   GameType *gt = gServerGame->getGameType();
   if(gt)
   {
      gt->s2cDisplayChatMessage(true, thisRobot->getName(), message);

      // Fire our event handler
      Robot::getEventManager().fireEvent(thisRobot->L, EventManager::MsgReceivedEvent, message, thisRobot->getPlayerInfo(), false);
   }

   return 0;
}


S32 LuaRobot::getTime(lua_State *L)
{
   return returnInt(L, thisRobot->getCurrentMove().time);
}


// Return list of all items of specified type within normal visible range... does no screening at this point
S32 LuaRobot::findItems(lua_State *L)
{
   Point pos = thisRobot->getActualPos();
   Rect queryRect(pos, pos);
   queryRect.expand(gServerGame->computePlayerVisArea(thisRobot));

   return doFindItems(L, queryRect);
}


extern Rect gServerWorldBounds;

// Same but gets all visible items from whole game... out-of-scope items will be ignored
S32 LuaRobot::findGlobalItems(lua_State *L)
{
   return doFindItems(L, gServerWorldBounds);
}


S32 LuaRobot::doFindItems(lua_State *L, Rect scope)
{
   // objectType is a bitmask of all the different object types we might want to find.  We need to build it up here because
   // lua can't do the bitwise or'ing itself.
   U32 objectType = 0;

   S32 index = 1;
   S32 pushed = 0;      // Count of items actually pushed onto the stack

   while(lua_isnumber(L, index))
   {
      objectType |= (U32) lua_tointeger(L, index);
      index++;
   }

   clearStack(L);

   fillVector.clear();

   thisRobot->findObjects(objectType, fillVector, scope);    // Get other objects on screen-visible area only


   lua_createtable(L, fillVector.size(), 0);    // Create a table, with enough slots pre-allocated for our data

   for(S32 i = 0; i < fillVector.size(); i++)
   {
      if(fillVector[i]->getObjectTypeMask() & (ShipType | RobotType))      // Skip cloaked ships & robots!
      {
         Ship *ship = dynamic_cast<Ship *>(fillVector[i]);

         if(dynamic_cast<Robot *>(fillVector[i]) == thisRobot)             // Do not find self
            continue;

         // Ignore ship/robot if it's dead or cloaked
         if((ship->isModuleActive(ModuleCloak) && !ship->areItemsMounted()) || ship->hasExploded)
            continue;
      }

      GameObject *obj = dynamic_cast<GameObject *>(fillVector[i]);
      obj->push(L);
      pushed++;      // Increment pushed before using it because Lua uses 1-based arrays
      lua_rawseti(L, 1, pushed);
   }

   return 1;
}


extern S32 findZoneContaining(const Vector<SafePtr<BotNavMeshZone> > &zones, const Point &p);

// Get next waypoint to head toward when traveling from current location to x,y
// Note that this function will be called frequently by various robots, so any
// optimizations will be helpful.
S32 LuaRobot::getWaypoint(lua_State *L)  // Takes a luavec or an x,y
{
   static const char *methodName = "Robot:getWaypoint()";

   Point target = getPointOrXY(L, 1, methodName);

   // If we can see the target, go there directly
   if(gServerGame->getGridDatabase()->pointCanSeePoint(thisRobot->getActualPos(), target))
      return returnPoint(L, target);

   // TODO: cache destination point; if it hasn't moved, then skip ahead.

   S32 targetZone = findZoneContaining(gBotNavMeshZones, target);       // Where we're going

   if(targetZone == -1)       // Our target is off the map.  See if it's visible from any of our zones, and, if so, go there
   {
      targetZone = findClosestZone(target);
 
      if(targetZone == -1)
         return returnNil(L);
   }

   // Make sure target is still in the same zone it was in when we created our flightplan.
   // If we're not, our flightplan is invalid, and we need to skip forward and build a fresh one.
   if(thisRobot->flightPlan.size() > 0 && targetZone == thisRobot->flightPlanTo)
   {

      // In case our target has moved, replace final point of our flightplan with the current target location
      thisRobot->flightPlan[0] = target;

      // First, let's scan through our pre-calculated waypoints and see if we can see any of them.
      // If so, we'll just head there with no further rigamarole.  Remember that our flightplan is
      // arranged so the closest points are at the end of the list, and the target is at index 0.
      Point dest;
      bool found = false;
      bool first = true;

      while(thisRobot->flightPlan.size() > 0)
      {
         Point last = thisRobot->flightPlan.last();

         // We'll assume that if we could see the point on the previous turn, we can
         // still see it, even though in some cases, the turning of the ship around a
         // protruding corner may make it technically not visible.  This will prevent
         // rapidfire recalcuation of the path when it's not really necessary.
         if(first || thisRobot->canSeePoint(last))
         {
            dest = last;
            found = true;
            first = false;
            thisRobot->flightPlan.pop_back();   // Discard now possibly superfluous waypoint
         }
         else
            break;
      }

      // If we found one, that means we found a visible waypoint, and we can head there...
      if(found)
      {
         thisRobot->flightPlan.push_back(dest);    // Put dest back at the end of the flightplan
         return returnPoint(L, dest);
      }
   }

   // We need to calculate a new flightplan
   thisRobot->flightPlan.clear();

   S32 currentZone = thisRobot->getCurrentZone();     // Where we are
   if(currentZone == -1)      // We don't really know where we are... bad news!  Let's find closest visible zone and go that way.

      currentZone = findClosestZone(thisRobot->getActualPos());
      if(currentZone == -1)      // That didn't go so well...
         return returnNil(L);

   // We're in, or on the cusp of, the zone containing our target.  We're close!!
   if(currentZone == targetZone)
   {
      Point p;
      if(!thisRobot->canSeePoint(target))           // Possible, if we're just on a boundary, and a protrusion's blocking a ship edge
      {
         p = gBotNavMeshZones[targetZone]->getCenter();
         thisRobot->flightPlan.push_back(p);
      }
      else
         p = target;

      thisRobot->flightPlan.push_back(target);
      return returnPoint(L, p);
   }

   // If we're still here, then we need to find a new path.  Either our original path was invalid for some reason,
   // or the path we had no longer applied to our current location
   thisRobot->flightPlanTo = targetZone;
   thisRobot->flightPlan = AStar::findPath(currentZone, targetZone, target);

   if(thisRobot->flightPlan.size() > 0)
      return returnPoint(L, thisRobot->flightPlan.last());
   else
      return returnNil(L);    // Out of options, end of the road
}
//
//
//// Encapsulate some ugliness
//Point LuaRobot::getNextWaypoint()
//{
//   TNLAssert(thisRobot->flightPlan.size() > 1, "FlightPlan has too few zones!");
//
//   S32 currentzone = thisRobot->getCurrentZone();
//
//   S32 nextzone = thisRobot->flightPlan[thisRobot->flightPlan.size() - 2];
//
//   // Note that getNeighborIndex could return -1.  It shouldn't, but it could.
//   S32 neighborZoneIndex = gBotNavMeshZones[currentzone]->getNeighborIndex(nextzone);
//   TNLAssert(neighborZoneIndex >= 0, "Invalid neighbor zone index!");
//
//
//            /*   string plan = "";
//               for(S32 i = 0; i < thisRobot->flightPlan.size(); i++)
//               {
//                  char z[100];
//                  itoa(thisRobot->flightPlan[i], z, 10);
//                  plan = plan + " ==>"+z;
//               }*/
//
//
//   S32 i = 0;
//   Point currentwaypoint = gBotNavMeshZones[currentzone]->getCenter();
//   Point nextwaypoint = gBotNavMeshZones[currentzone]->mNeighbors[neighborZoneIndex].borderCenter;
//
//   while(thisRobot->canSeePoint(nextwaypoint))
//   {
//      currentzone = nextzone;
//      currentwaypoint = nextwaypoint;
//      i++;
//
//      if(thisRobot->flightPlan.size() - 2 - i < 0)
//         return nextwaypoint;
//
//      nextzone = thisRobot->flightPlan[thisRobot->flightPlan.size() - 2 - i];
//      S32 neighborZoneIndex = gBotNavMeshZones[currentzone]->getNeighborIndex(nextzone);
//      TNLAssert(neighborZoneIndex >= 0, "Invalid neighbor zone index!");
//
//      nextwaypoint = gBotNavMeshZones[currentzone]->mNeighbors[neighborZoneIndex].borderCenter;
//   }
//
//
//   return currentwaypoint;
//
//
//
//   //if(thisRobot->canSeePoint( gBotNavMeshZones[currentZone]->mNeighbors[neighborZoneIndex].center))
//   //   return gBotNavMeshZones[currentZone]->mNeighbors[neighborZoneIndex].center;
//   //else if(thisRobot->canSeePoint( gBotNavMeshZones[currentZone]->mNeighbors[neighborZoneIndex].borderCenter))
//   //   return gBotNavMeshZones[currentZone]->mNeighbors[neighborZoneIndex].borderCenter;
//   //else
//   //   return gBotNavMeshZones[currentZone]->getCenter();
//}
//

// Another helper function: finds closest zone to a given point
S32 LuaRobot::findClosestZone(Point point)
{
	// Make two passes, first with a short distance, second with a longer one.  Hope we find it in the first pass because
   // the second pass checks all zones, and that could take a while.
   F32 distsq = 262144;     // 512^2
   S32 closest = -3;
 
   while(closest < -1)
   {
      for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
      {
         Point center = gBotNavMeshZones[i]->getCenter();
 
         F32 d = center.distSquared(point);     // Use cheaper test first
         if(d < distsq)
         {
            if(gServerGame->getGridDatabase()->pointCanSeePoint(center, point))     // This is an expensive test
            {
               closest = i;
               distsq = d;
            }
         }
      }
      if(closest < 0) // Didn't find any matches on the first pass, let's expand our radius and try again
      {
         closest++;
         distsq=F32_MAX;
      }
   }
 
   return closest;
}


S32 LuaRobot::findAndReturnClosestZone(lua_State *L, Point point)
{
   S32 closest = findClosestZone(point);

   if(closest != -1)
      return returnPoint(L, gBotNavMeshZones[closest]->getCenter());
   else
      return returnNil(L);    // Really stuck
}


S32 LuaRobot::subscribe(lua_State *L)
{
   // Get the event off the stack
   static const char *methodName = "Robot:subscribe()";
   checkArgCount(L, 1, methodName);

   S32 eventType = getInt(L, 0, methodName);
   if(eventType < 0 || eventType >= EventManager::EventTypes)
      return 0;

   eventManager.subscribe(L, (EventManager::EventType) eventType);
   subscriptions[eventType] = true;
   return 0;
}


S32 LuaRobot::unsubscribe(lua_State *L)
{
   // Get the event off the stack
   static const char *methodName = "Robot:unsubscribe()";
   checkArgCount(L, 1, methodName);

   S32 eventType = getInt(L, 0, methodName);
   if(eventType < 0 || eventType >= EventManager::EventTypes)
      return 0;

   eventManager.unsubscribe(L, (EventManager::EventType) eventType);
   subscriptions[eventType] = false;
   return 0;
}


const char LuaRobot::className[] = "LuaRobot";     // This is the class name as it appears to the Lua scripts


////////////////////////////////////////
////////////////////////////////////////


bool EventManager::anyPending = false; 
Vector<lua_State *> EventManager::subscriptions[EventTypes];
Vector<lua_State *> EventManager::pendingSubscriptions[EventTypes];
Vector<lua_State *> EventManager::pendingUnsubscriptions[EventTypes];

void EventManager::subscribe(lua_State *L, EventType eventType)
{
   // First, see if we're already subscribed
   if(!isSubscribed(L, eventType) && !isPendingSubscribed(L, eventType))
   {
      removeFromPendingUnsubscribeList(L, eventType);
      pendingSubscriptions[eventType].push_back(L);
      anyPending = true;
   }
}


void EventManager::unsubscribe(lua_State *L, EventType eventType)
{
   if(isSubscribed(L, eventType) && !isPendingUnsubscribed(L, eventType))
   {
      removeFromPendingSubscribeList(L, eventType);
      pendingUnsubscriptions[eventType].push_back(L);
      anyPending = true;
   }
}


void EventManager::removeFromPendingSubscribeList(lua_State *subscriber, EventType eventType)
{
   for(S32 i = 0; i < pendingSubscriptions[eventType].size(); i++)
      if(pendingSubscriptions[eventType][i] == subscriber)
      {
         pendingSubscriptions[eventType].erase_fast(i);
         return;
      }
}


void EventManager::removeFromPendingUnsubscribeList(lua_State *unsubscriber, EventType eventType)
{
   for(S32 i = 0; i < pendingUnsubscriptions[eventType].size(); i++)
      if(pendingUnsubscriptions[eventType][i] == unsubscriber)
      {
         pendingUnsubscriptions[eventType].erase_fast(i);
         return;
      }
}


void EventManager::removeFromSubscribedList(lua_State *subscriber, EventType eventType)
{
   for(S32 i = 0; i < subscriptions[eventType].size(); i++)
      if(subscriptions[eventType][i] == subscriber)
      {
         subscriptions[eventType].erase_fast(i);
         return;
      }
}


// Unsubscribe an event bypassing the pending unsubscribe queue, when we know it will be OK
void EventManager::unsubscribeImmediate(lua_State *L, EventType eventType)
{
   removeFromSubscribedList(L, eventType);
   removeFromPendingSubscribeList(L, eventType);
   removeFromPendingUnsubscribeList(L, eventType);    // Probably not really necessary...
}


// Check if we're subscribed to an event
bool EventManager::isSubscribed(lua_State *L, EventType eventType)
{
   for(S32 i = 0; i < subscriptions[eventType].size(); i++)
      if(subscriptions[eventType][i] == L)
         return true;

   return false;
}


bool EventManager::isPendingSubscribed(lua_State *L, EventType eventType)
{
   for(S32 i = 0; i < pendingSubscriptions[eventType].size(); i++)
      if(pendingSubscriptions[eventType][i] == L)
         return true;

   return false;
}


bool EventManager::isPendingUnsubscribed(lua_State *L, EventType eventType)
{
   for(S32 i = 0; i < pendingUnsubscriptions[eventType].size(); i++)
      if(pendingUnsubscriptions[eventType][i] == L)
         return true;

   return false;
}


// Process all pending subscriptions and unsubscriptions
void EventManager::update()
{
   if(anyPending)
   {
      for(S32 i = 0; i < EventTypes; i++)
         for(S32 j = 0; j < pendingUnsubscriptions[i].size(); j++)     // Unsubscribing first means less searching!
            removeFromSubscribedList(pendingUnsubscriptions[i][j], (EventType) i);

      for(S32 i = 0; i < EventTypes; i++)
         for(S32 j = 0; j < pendingSubscriptions[i].size(); j++)     
            subscriptions[i].push_back(pendingSubscriptions[i][j]);

      for(S32 i = 0; i < EventTypes; i++)
      {
         pendingSubscriptions[i].clear();
         pendingUnsubscriptions[i].clear();
      }
      anyPending = false;
   }
}


// This is a list of the function names to be called in the bot when a particular event is fired
static const char *eventFunctions[] = {
   "onShipSpawned",
   "onShipKilled",
   "onPlayerJoined",
   "onPlayerLeft",
   "onMsgReceived",
};


void EventManager::fireEvent(EventType eventType)
{
   for(S32 i = 0; i < subscriptions[eventType].size(); i++)
   {
      lua_State *L = subscriptions[eventType][i];
      try
      {
         lua_getglobal(L, "onMsgSent");

         if (lua_pcall(L, 0, 0, 0) != 0)
            throw LuaException(lua_tostring(L, -1));
      }
      catch(LuaException &e)
      {
         logprintf(LogConsumer::LogError, "Robot error firing event %d: %s.", eventType, e.what());
         return;
      }
   }
}


void EventManager::fireEvent(EventType eventType, Ship *ship)
{
   for(S32 i = 0; i < subscriptions[eventType].size(); i++)
   {
      lua_State *L = subscriptions[eventType][i];
      try
      {
         lua_getglobal(L, eventFunctions[eventType]);
         ship->push(L);

         if (lua_pcall(L, 1, 0, 0) != 0)
            throw LuaException(lua_tostring(L, -1));
      }
      catch(LuaException &e)
      {
         logprintf(LogConsumer::LogError, "Robot error firing event %d: %s.", eventType, e.what());
         return;
      }
   }
}


void EventManager::fireEvent(lua_State *caller_L, EventType eventType, const char *message, LuaPlayerInfo *player, bool global)
{
   for(S32 i = 0; i < subscriptions[eventType].size(); i++)
   {
      lua_State *L = subscriptions[eventType][i];

      if(L == caller_L)    // Don't alert bot about own message!
         continue;

      try
      {
         lua_getglobal(L, eventFunctions[eventType]);  
         lua_pushstring(L, message);
         player->push(L);
         lua_pushboolean(L, global);

         if (lua_pcall(L, 3, 0, 0) != 0)
            throw LuaException(lua_tostring(L, -1));
      }
      catch(LuaException &e)
      {
         logprintf(LogConsumer::LogError, "Robot error firing event %d: %s.", eventType, e.what());
         return;
      }
   }
}


// PlayerJoined, PlayerLeft
void EventManager::fireEvent(lua_State *caller_L, EventType eventType, LuaPlayerInfo *player)
{
   for(S32 i = 0; i < subscriptions[eventType].size(); i++)
   {
      lua_State *L = subscriptions[eventType][i];
      
      if(L == caller_L)    // Don't alert bot about own joinage or leavage!
         continue;

      try   
      {
         lua_getglobal(L, eventFunctions[eventType]);  
         player->push(L);

         if (lua_pcall(L, 1, 0, 0) != 0)
            throw LuaException(lua_tostring(L, -1));
      }
      catch(LuaException &e)
      {
         logprintf(LogConsumer::LogError, "Robot error firing event %d: %s.", eventType, e.what());
         return;
      }
   }
}


////////////////////////////////////////
////////////////////////////////////////

TNL_IMPLEMENT_NETOBJECT(Robot);


Vector<Robot *> Robot::robots;

// Constructor, runs on client and server
Robot::Robot(StringTableEntry robotName, S32 team, Point pt, F32 mass) : Ship(robotName, false, team, pt, mass, true)
{
   mObjectTypeMask = RobotType | MoveableType | CommandMapVisType | TurretTargetType;     // Override typemask set by ship

   L = NULL;
   mCurrentZone = -1;
   flightPlanTo = -1;

   mLastMoveTime = 0;

   // Need to provide some time on here to get timer to trigger robot to spawn.  It's timer driven.
   respawnTimer.reset(100, RobotRespawnDelay);

   hasExploded = true;     // Becase we start off "dead", but will respawn real soon now...

   disableCollision();

   mPlayerInfo = new RobotPlayerInfo(this);
   mScore = 0;
   mTotalScore = 0;

   for(S32 i = 0; i < ModuleCount; i++)         // Here so valgrind won't complain if robot updates before initialize is run
      mModuleActive[i] = false;
}


// Destructor, runs on client and server
Robot::~Robot()
{
   // Close down our Lua interpreter
   LuaObject::cleanupAndTerminate(L);

   if(isGhost())
   {
      delete mPlayerInfo;     // If this is on the server, will be deleted below, after event is fired
      return;
   }

   // Server only from here on down

   // Remove this robot from the list of all robots
   for(S32 i = 0; i < robots.size(); i++)
      if(robots[i] == this)
      {
         robots.erase_fast(i);
         break;
      }

   mPlayerInfo->setDefunct();
   eventManager.fireEvent(L, EventManager::PlayerLeftEvent, getPlayerInfo());
   delete mPlayerInfo;     

   logprintf(LogConsumer::LogLuaObjectLifecycle, "Robot terminated [%s] (%d)", mFilename.c_str(), robots.size());
}


// Reset everything on the robot back to the factory settings
// Only runs on server!
bool Robot::initialize(Point &pos)
{
   respawnTimer.clear();
   flightPlan.clear();

   mCurrentZone = -1;   // Correct value will be calculated upon first request

   Parent::initialize(pos);

   enableCollision();

   // WarpPositionMask triggers the spinny spawning visual effect
   setMaskBits(RespawnMask | HealthMask | LoadoutMask | PositionMask | MoveMask | ModulesMask | WarpPositionMask);      // Send lots to the client

   TNLAssert(!isGhost(), "Didn't expect ghost here...");

   runMain();
   eventManager.update();       // Ensure registrations made during bot initialization are ready to go

   return true;
} 


// Loop through all our bots, start their interpreters, and run their main() functions
void Robot::startBots()
{
   for(S32 i = 0; i < robots.size(); i++)
      robots[i]->startLua();     

   //for(S32 i = 0; i < robots.size(); i++)
   //   robots[i]->runMain();

   //eventManager.update();       // Ensure registrations made during bot initialization are ready to go
}


extern ConfigDirectories gConfigDirs;
extern string joindir(const string &path, const string &filename);

bool Robot::startLua()
{
   LuaObject::cleanupAndTerminate(L);

   L = lua_open();    // Create a new Lua interpreter


   // Register our connector types with Lua

   Lunar<LuaUtil>::Register(L);

   Lunar<LuaGameInfo>::Register(L);
   Lunar<LuaTeamInfo>::Register(L);
   Lunar<LuaPlayerInfo>::Register(L);
   //Lunar<LuaTimer>::Register(L);

   Lunar<LuaWeaponInfo>::Register(L);
   Lunar<LuaModuleInfo>::Register(L);

   Lunar<LuaLoadout>::Register(L);
   Lunar<LuaPoint>::Register(L);

   Lunar<LuaRobot>::Register(L);
   Lunar<LuaShip>::Register(L);

   Lunar<RepairItem>::Register(L);
   Lunar<ResourceItem>::Register(L);
   Lunar<TestItem>::Register(L);
   Lunar<Asteroid>::Register(L);
   Lunar<Turret>::Register(L);
   Lunar<Teleporter>::Register(L);

   Lunar<ForceFieldProjector>::Register(L);
   Lunar<FlagItem>::Register(L);
   Lunar<SoccerBallItem>::Register(L);
   //Lunar<HuntersFlagItem>::Register(L);
   Lunar<ResourceItem>::Register(L);

   Lunar<LuaProjectile>::Register(L);
   Lunar<Mine>::Register(L);
   Lunar<SpyBug>::Register(L);

   Lunar<GoalZone>::Register(L);
   Lunar<LoadoutZone>::Register(L);

#ifdef USE_PROFILER
   init_profiler(L);
#endif

   LuaUtil::openLibs(L);
   LuaUtil::setModulePath(L);

   // Push a pointer to this Robot to the Lua stack,
   // then set the global name of this pointer.  This is the name that we'll use to refer
   // to this robot from our Lua code.  
   // Note that all globals need to be set before running lua_helper_functions, which makes it more difficult to set globals
   lua_pushlightuserdata(L, (void *)this);
   lua_setglobal(L, "Robot");

   LuaObject::setLuaArgs(L, mFilename, mArgs);    // Put our args in to the Lua table "args"


   if(!loadLuaHelperFunctions(L, "robot"))
      return false;

   string robotfname = joindir(gConfigDirs.luaDir, "robot_helper_functions.lua");

   if(luaL_loadfile(L, robotfname.c_str()))
   {
      logError("Error loading robot helper functions %s.  Shutting robot down.", robotfname.c_str());
      return false;
   }

   // Now run the loaded code
   if(lua_pcall(L, 0, 0, 0))     // Passing 0 params, getting none back
   {
      logError("Error during initializing robot helper functions: %s.  Shutting robot down.", lua_tostring(L, -1));
      return false;
   }
  
   // Load the bot
   if(luaL_loadfile(L, mFilename.c_str()))
   {
      logError("Error loading file: %s.  Shutting robot down.", lua_tostring(L, -1));
      return false;
   }

   // Run the bot -- this loads all the functions into the global namespace
   if(lua_pcall(L, 0, 0, 0))     // Passing 0 params, getting none back
   {
      logError("Robot error during initialization: %s.  Shutting robot down.", lua_tostring(L, -1));
      return false;
   }

   string name;

   // Run the getName() function in the bot (will default to the one in robot_helper_functions if it's not overwritten by the bot)
   lua_getglobal(L, "getName");

   if (!lua_isfunction(L, -1) || lua_pcall(L, 0, 1, 0))     // Passing 0 params, getting one back
   {
      name = "Nancy";
      logError("Robot error retrieving name (%s).  Using \"%s\".", lua_tostring(L, -1), name.c_str());
   }
   else
   {
      name = lua_tostring(L, -1);
      lua_pop(L, 1);
   }

   // Make sure name is unique
   mPlayerName = GameConnection::makeUnique(name).c_str();
   mIsAuthenticated = false;

   // Note main() will be run later, after all bots have been loaded
   return true;
}


// TODO: This is almost identical to the same-named function in luaLevelGenerator.cpp, but each call their own logError function.  How can we combine?
bool Robot::loadLuaHelperFunctions(lua_State *L, const char *caller)
{
   // Load our standard robot library  TODO: Read the file into memory, store that as a static string in the bot code, and then pass that to Lua rather than rereading this
   // every time a bot is created.
   string fname = joindir(gConfigDirs.luaDir, "lua_helper_functions.lua");

   if(luaL_loadfile(L, fname.c_str()))
   {
      logError("Error loading lua helper functions %s: %s.  Can't run %s...", fname.c_str(), lua_tostring(L, -1), caller);
      return false;
   }

   // Now run the loaded code
   if(lua_pcall(L, 0, 0, 0))     // Passing 0 params, getting none back
   {
      logError("Error during initializing lua helper functions %s: %s.  Can't run %s...", fname.c_str(), lua_tostring(L, -1), caller);
      return false;
   }

   return true;
}


// Don't forget to update the eventManager after running a robot's main function!
void Robot::runMain()
{
   try
   {
      lua_getglobal(L, "_main");
      if(lua_pcall(L, 0, 0, 0) != 0)
         throw LuaException(lua_tostring(L, -1));
   }
   catch(LuaException &e)
   {
      logError("Robot error running main(): %s.  Shutting robot down.", e.what());
      delete this;
   }
}


EventManager Robot::getEventManager()
{
   return eventManager;
}


// This only runs the very first time the robot is added to the level
// Runs on client and server
void Robot::onAddedToGame(Game *game)
{
   Parent::onAddedToGame(game);
   
   if(isGhost())
      return;

   // Server only from here on out

   //setScopeAlways();          // Make them always visible on cmdr map --> del
   robots.push_back(this);    // Add this robot to the list of all robots (can't do this in constructor or else it gets run on client side too...)
   eventManager.fireEvent(L, EventManager::PlayerJoinedEvent, getPlayerInfo());
}


// Basically exists to override Ship::kill(info)
 void Robot::kill(DamageInfo *theInfo)
{
   GameConnection *killer = theInfo->damagingObject ? theInfo->damagingObject->getOwner() : NULL;
   ClientRef *killerRef = killer ? killer->getClientRef() : NULL;

   if(killerRef)
      killerRef->mStatistics.addKill();

   kill();
}


void Robot::kill()
{
   hasExploded = true;
   respawnTimer.reset();
   setMaskBits(ExplosionMask);

   disableCollision();

   // Dump mounted items
   for(S32 i = mMountedItems.size() - 1; i >= 0; i--)
      mMountedItems[i]->onMountDestroyed();
}


bool Robot::processArguments(S32 argc, const char **argv)
{
   if(argc < 2)               // Two required: team and bot file
      return false;

   mTeam = atoi(argv[0]);     // Need some sort of bounds check here??

   mFilename = gConfigDirs.findBotFile(argv[1]);
   if(mFilename == "")
   {
      logprintf("Could not find bot file %s", argv[1]);     // TODO: Better handling here
      return false;
   }

   // Collect our arguments to be passed into the args table in the robot (starting with the robot name)
   // Need to make a copy or containerize argv[i] somehow,  because otherwise new data will get written
   // to the string location subsequently, and our vals will change from under us.  That's bad!

   // We're using string here as a stupid way to get done what we need to do... perhaps there is a better way.

   for(S32 i = 2; i < argc; i++)
      mArgs.push_back(string(argv[i]));

   return true;
}


// Some rudimentary robot error logging.  Perhaps, someday, will become a sort of in-game error console.
// For now, though, pass all errors through here.
void Robot::logError(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   char buffer[2048];

   vsnprintf(buffer, sizeof(buffer), format, args);
   logprintf(LogConsumer::LuaBotMessage, "***ROBOT ERROR*** in %s ::: %s", mFilename.c_str(), buffer);

   va_end(args);
}


S32 Robot::getCurrentZone()
{
   // We're in uncharted territory -- try to get the current zone
   if(mCurrentZone == -1)
   {
      mCurrentZone = findZoneContaining(gBotNavMeshZones, getActualPos());
   }
   return mCurrentZone;
}


// Setter method, not a robot function!
void Robot::setCurrentZone(S32 zone)
{
   mCurrentZone = zone;
}


F32 Robot::getAnglePt(Point point)
{
   return getActualPos().angleTo(point);
}


extern bool PolygonContains2(const Point *inVertices, int inNumVertices, const Point &inPoint);

// Return coords of nearest ship... and experimental robot routine
bool Robot::findNearestShip(Point &loc)
{
   Vector<DatabaseObject *> foundObjects;
   Point closest;

   Point pos = getActualPos();
   Point extend(2000, 2000);
   Rect r(pos - extend, pos + extend);

   findObjects(ShipType, foundObjects, r);

   if(!foundObjects.size())
      return false;

   F32 dist = F32_MAX;
   bool found = false;

   for(S32 i = 0; i < foundObjects.size(); i++)
   {
      GameObject *foundObject = dynamic_cast<GameObject *>(foundObjects[i]);
      F32 d = foundObject->getActualPos().distanceTo(pos);
      if(d < dist && d > 0)      // d == 0 means we're comparing to ourselves
      {
         dist = d;
         loc = foundObject->getActualPos();     // use set here?
         found = true;
      }
   }
   return found;
}


bool Robot::canSeePoint(Point point)
{
   // Need to check the two edge points perpendicular to the direction of looking to ensure we have an unobstructed
   // flight lane to point.  Radius of the robot is mRadius.  This keeps the ship from getting hung up on
   // obstacles that appear visible from the center of the ship, but are actually blocked.

   F32 ang = getActualPos().angleTo(point);
   F32 cosang = cos(ang) * mRadius;
   F32 sinang = sin(ang) * mRadius;

   Point edgePoint1 = getActualPos() + Point(sinang, -cosang);
   Point edgePoint2 = getActualPos() + Point(-sinang, cosang);

   return(
      gServerGame->getGridDatabase()->pointCanSeePoint(edgePoint1, point) &&
      gServerGame->getGridDatabase()->pointCanSeePoint(edgePoint2, point) );
}


void Robot::idle(GameObject::IdleCallPath path)
{
   U32 deltaT;

   if(path == GameObject::ServerIdleMainLoop)      // Running on server... but then, aren't we always??
   {
      U32 ms = Platform::getRealMilliseconds();
      deltaT = (ms - mLastMoveTime);

      if(deltaT == 0)      // If deltaT is 0, this may cause problems down the line.  Best thing is just to skip this round.
         return;

      mLastMoveTime = ms;
      mCurrentMove.time = deltaT;

      // Check to see if we need to respawn this robot
      if(hasExploded)
      {
         if(respawnTimer.update(mCurrentMove.time))
         {
            try
            {
               gServerGame->getGameType()->spawnRobot(this);
            }
            catch(LuaException &e)
            {
               logError("Robot error during spawn: %s.  Shutting robot down.", e.what());
               delete this;
            }
         }
         return;
      }
   }

   // Don't process exploded ships
   if(hasExploded)
      return;

   if(path == GameObject::ServerIdleMainLoop)      // Running on server
   {
      // Clear out current move.  It will get set just below with the lua call, but if that function
      // doesn't set the various move components, we want to make sure that they default to 0.
      mCurrentMove.fire = false;
      mCurrentMove.up = 0;
      mCurrentMove.down = 0;
      mCurrentMove.right = 0;
      mCurrentMove.left = 0;

      for(S32 i = 0; i < ShipModuleCount; i++)
         mCurrentMove.module[i] = false;

      try
      {
         lua_getglobal(L, "_onTick");
         Lunar<LuaRobot>::push(L, this->mLuaRobot);

         lua_pushnumber(L, deltaT);    // Pass the time elapsed since we were last here

         if (lua_pcall(L, 2, 0, 0) != 0)
            throw LuaException(lua_tostring(L, -1));
      }
      catch(LuaException &e)
      {
         logError("Robot error running _onTick(): %s.  Shutting robot down.", e.what());
         delete this;
         return;
      }


      // If we've changed the mCurrentMove, then we need to set
      // the MoveMask to ensure that it is sent to the clients
      if(!mCurrentMove.isEqualMove(&mLastMove))
         setMaskBits(MoveMask);

      processMove(ActualState);

      // Apply impulse vector and reset it
      mMoveState[ActualState].vel += mImpulseVector;
      mImpulseVector.set(0,0);

      // Update the render state on the server to match
      // the actual updated state, and mark the object
      // as having changed Position state.  An optimization
      // here would check the before and after positions
      // so as to not update unmoving ships.

      mMoveState[RenderState] = mMoveState[ActualState];
      setMaskBits(PositionMask);
   }
   else if(path == GameObject::ClientIdleMainRemote)  // Running on client (but not replaying saved game)
   {
      // On the client, update the interpolation of this object, unless we are replaying control moves
      mInterpolating = (getActualVel().lenSquared() < MoveObject::InterpMaxVelocity*MoveObject::InterpMaxVelocity);
      updateInterpolation();
   }

   updateExtent();            // Update the object in the game's extents database
   mLastMove = mCurrentMove;  // Save current move

   // Update module timers
   mSensorZoomTimer.update(mCurrentMove.time);
   mCloakTimer.update(mCurrentMove.time);

   if(path == GameObject::ServerIdleMainLoop)    // Was ClientIdleControlReplay
   {
      // Process weapons and energy on controlled object objects
      processWeaponFire();
      processEnergy();
   }

   if(path == GameObject::ClientIdleMainRemote)       // Probably should be server
   {
      // For ghosts, find some repair targets for rendering the repair effect
      if(isModuleActive(ModuleRepair))
         findRepairTargets();
   }

   if(false && isModuleActive(ModuleRepair))     // Probably should be server
      repairTargets();

   // If we're on the client, do some effects
   if(path == GameObject::ClientIdleControlMain ||
      path == GameObject::ClientIdleMainRemote)
   {
      mWarpInTimer.update(mCurrentMove.time);
      // Emit some particles, trail sections and update the turbo noise
      emitMovementSparks();
      for(U32 i=0; i<TrailCount; i++)
         mTrail[i].tick(mCurrentMove.time);
      updateModuleSounds();
   }
}

};

