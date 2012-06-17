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

#include "teleporter.h"

using namespace TNL;
#include "loadoutZone.h"          // For when ship teleports onto a loadout zone
#include "gameLoader.h"
#include "gameObjectRender.h"
#include "ClientInfo.h"

#include "Colors.h"
#include "SoundSystem.h"

#include "stringUtils.h"

#ifndef ZAP_DEDICATED
#   include "ClientGame.h"
#   include "sparkManager.h"
#   include "SDL_opengl.h"
#   include "UI.h"
#endif

#include <math.h>

namespace Zap
{

TNL_IMPLEMENT_NETOBJECT(Teleporter);

static Vector<DatabaseObject *> foundObjects;

// Constructor --> need to set the pos and dest via methods like processArguments to make sure
// that we get the multiple destination aspect of teleporters right
Teleporter::Teleporter(Point pos, Point dest) : Engineerable()
{
   mObjectTypeNumber = TeleportTypeNumber;
   mNetFlags.set(Ghostable);

   timeout = 0;
   mTime = 0;
   mTeleporterDelay = TeleporterDelay;
   setTeam(TEAM_NEUTRAL);

   mNeedsEndpoint = false;

   setVert(pos, 0);
   setVert(dest, 1);

   mHasExploded = false;
   mStartingHealth = 1.0f;
}

// Destructor
Teleporter::~Teleporter()
{
   // Do nothing
}


Teleporter *Teleporter::clone() const
{
   return new Teleporter(*this);
}



void Teleporter::onAddedToGame(Game *theGame)
{
   Parent::onAddedToGame(theGame);

   if(!isGhost())
      setScopeAlways();    // Always in scope!
}


bool Teleporter::processArguments(S32 argc2, const char **argv2, Game *game)
{
   S32 argc = 0;
   const char *argv[8];

   for(S32 i = 0; i < argc2; i++)      // The idea here is to allow optional R3.5 for rotate at speed of 3.5
   {
      char firstChar = argv2[i][0];    // First character of arg

      if((firstChar >= 'a' && firstChar <= 'z') || (firstChar >= 'A' && firstChar <= 'Z'))  // starts with a letter
      {
         if(!strnicmp(argv2[i], "Delay=", 6))
            mTeleporterDelay = U32(atof(&argv2[i][6]) * 1000);
      }
      else
      {
         if(argc < 8)
         {  
            argv[argc] = argv2[i];
            argc++;
         }
      }
   }

   if(argc != 4)
      return false;

   Point pos, dest;

   pos.read(argv);
   dest.read(argv + 2);

   pos *= game->getGridSize();
   dest *= game->getGridSize();

   setVert(pos, 0);
   setVert(dest, 1);

   // See if we already have any teleports with this pos... if so, this is a "multi-dest" teleporter

#ifndef ZAP_DEDICATED
   if(!dynamic_cast<ClientGame *>(game))              // Editor handles multi-dest teleporters as separate single dest items
#endif
   {
      foundObjects.clear();
      game->getGameObjDatabase()->findObjects(TeleportTypeNumber, foundObjects, Rect(pos, 1));

      for(S32 i = 0; i < foundObjects.size(); i++)
      {
         Teleporter *tel = dynamic_cast<Teleporter *>(foundObjects[i]);
         if(tel->getVert(0).distSquared(pos) < 1)     // i.e These are really close!  Must be the same!
         {
            tel->mDests.push_back(dest);
            destroySelf();    // Since this is really part of a different teleporter, delete this one
            return true;      // There will only be one!
         }
      }

      // New teleporter origin
      mDests.push_back(dest);
      computeExtent(); // for ServerGame extent
   }
#ifndef ZAP_DEDICATED
   else
   {
      mDests.push_back(dest);
      setExtent(calcExtents()); // for editor
   }
#endif

   return true;
}


string Teleporter::toString(F32 gridSize) const
{
   string out = string(getClassName()) + " " + geomToString(gridSize);
   if(mTeleporterDelay != TeleporterDelay)
      out += " Delay=" + ftos(mTeleporterDelay / 1000.f, 3);
   return out;
}


bool Teleporter::checkDeploymentPosition(const Point &position, GridDatabase *gb)
{
   Rect queryRect(position, TELEPORTER_RADIUS * 2);
	Point outPoint;  // only used as a return value in polygonCircleIntersect

   foundObjects.clear();
   gb->findObjects((TestFunc) isWallType, foundObjects, queryRect);

   Vector<Point> foundObjectBounds;
   for(S32 i = 0; i < foundObjects.size(); i++)
   {
      foundObjectBounds.clear();
      static_cast<BfObject *>(foundObjects[i])->getCollisionPoly(foundObjectBounds);

      // If they intersect, then bad deployment position
      if(polygonCircleIntersect(foundObjectBounds.address(), foundObjectBounds.size(), position, TELEPORTER_RADIUS * TELEPORTER_RADIUS, outPoint))
         return false;
   }

   return true;
}


U32 Teleporter::packUpdate(GhostConnection *connection, U32 updateMask, BitStream *stream)
{
   if(stream->writeFlag(updateMask & InitMask))
   {
      getVert(0).write(stream);

      stream->writeFlag(mEngineered);

      stream->writeInt(mDests.size(), 16);

      for(S32 i = 0; i < mDests.size(); i++)
         mDests[i].write(stream);

      if(stream->writeFlag(mTeleporterDelay != TeleporterDelay))  // most teleporter will be at a default timing
         stream->writeInt(mTeleporterDelay, 32);

      if(mTeleporterDelay != 0 && stream->writeFlag(timeout != 0))
         stream->writeInt(timeout, 32);  // a player might join while this teleporter is in the middle of delay.
   }
   else if(stream->writeFlag(updateMask & TeleportMask))    // Basically, this gets triggered if a ship passes through
      stream->write(mLastDest);     // Where ship is going

   // If we've adjusted the exit point, needed with engineering teleports
   if(stream->writeFlag(updateMask & ExitPointChangedMask))
      getVert(1).write(stream);

   // If we're not destroyed and health has changed
   stream->writeFlag(mHasExploded);

   // Health has changed
   if(!mHasExploded)
   {
      if(stream->writeFlag(updateMask & HealthMask))
         stream->writeFloat(mStartingHealth, 6);
   }

   return 0;
}


void Teleporter::unpackUpdate(GhostConnection *connection, BitStream *stream)
{
   if(stream->readFlag())
   {
      U32 count;
      Point pos;
      pos.read(stream);
      setVert(pos, 0);

      mEngineered = stream->readFlag();

      count = stream->readInt(16);
      mDests.resize(count);

      for(U32 i = 0; i < count; i++)
         mDests[i].read(stream);
      
      computeExtent();

      if(stream->readFlag())
         mTeleporterDelay = stream->readInt(32);

      if(mTeleporterDelay != 0 && stream->readFlag())
         timeout = stream->readInt(32);
   }
   else if(stream->readFlag() && isGhost())
   {
      S32 dest;
      stream->read(&dest);

#ifndef ZAP_DEDICATED
      TNLAssert(dynamic_cast<ClientGame *>(getGame()) != NULL, "Not a ClientGame");
      static_cast<ClientGame *>(getGame())->emitTeleportInEffect(mDests[dest], 0);

      SoundSystem::playSoundEffect(SFXTeleportIn, mDests[dest]);

      SoundSystem::playSoundEffect(SFXTeleportOut, getVert(0));
#endif
      timeout = mTeleporterDelay;
   }

   // ExitPointChangedMask
   if(stream->readFlag())
   {
      // Set the destination point properly on the client
      Point dest;
      dest.read(stream);
      setVert(dest, 1);
      mDests.clear();
      mDests.push_back(dest);

      // Update the object extents
      Rect rect(getVert(0), getVert(1));
      rect.expand(Point(TELEPORTER_RADIUS, TELEPORTER_RADIUS));
      setExtent(rect);
   }

   // mHasExploded
   if(stream->readFlag())
   {
      mStartingHealth = 0;
      if(!mHasExploded)
      {
         mHasExploded = true;
         disableCollision();
         mExplosionTimer.reset(TeleporterExplosionTime);
         mFinalExplosionTriggered = false;
      }
   }

   // HealthMask
   else if(stream->readFlag())
      mStartingHealth = stream->readFloat(6);
}


void Teleporter::damageObject(DamageInfo *theInfo)
{
   // Only engineered teleports can be damaged
   if(!mEngineered)
      return;

   if(mHasExploded)
      return;

   mStartingHealth -= theInfo->damageAmount;
   setMaskBits(HealthMask);

   // Destroyed!
   if(mStartingHealth <= 0 && mResource.isValid())
   {
      mHasExploded = true;

      mResource->addToDatabase(getGame()->getGameObjDatabase());
      mResource->setPos(getVert(0));

      deleteObject(TeleporterExplosionTime + 500);  // Guarantee our explosion effect will complete
      setMaskBits(DestroyedMask);
   }
}


bool Teleporter::collide(BfObject *otherObject)
{
   // Only engineered teleports have collision
   if(!mEngineered)
      return false;

   // Only projectiles should collide
   if(isProjectileType(otherObject->getObjectTypeNumber()))
      return true;

   return false;
}


bool Teleporter::getCollisionCircle(U32 state, Point &center, F32 &radius) const
{
   center = getVert(0);
   radius = TELEPORTER_RADIUS / 2;
   return true;
}


bool Teleporter::getCollisionPoly(Vector<Point> &polyPoints) const
{
   return false;
}


void Teleporter::computeExtent()
{
   setExtent(Rect(getVert(0), (F32)TELEPORTER_RADIUS));
}


void Teleporter::onConstructed()
{
   // Make destination the entry point so we have at least one destination
   mDests.push_back(getVert(0));
   mNeedsEndpoint = true;
   setMaskBits(ExitPointChangedMask);
}


bool Teleporter::needsEndpoint()
{
   return mNeedsEndpoint;
}

// Server only
void Teleporter::setEndpoint(const Point &point)
{
   if(mNeedsEndpoint)
   {
      mDests.clear();
      mDests.push_back(point);
      setVert(point, 1);

      setMaskBits(ExitPointChangedMask);

      mNeedsEndpoint = false;
   }
}


void Teleporter::idle(BfObject::IdleCallPath path)
{
   U32 deltaT = mCurrentMove.time;
   mTime += deltaT;

   // Deal with our timeout...  could rewrite with a timer!
   if(timeout > deltaT)
   {
      timeout -= deltaT;
      return;
   }
   else
      timeout = 0;

   // Client only
   if(path == BfObject::ClientIdleMainRemote)
   {
      // Update Explosion Timer
      if(mHasExploded)
      {
         if(mExplosionTimer.getCurrent() != 0)
            mExplosionTimer.update(deltaT);
      }
   }

   // Server only from here on down
   if(path != BfObject::ServerIdleMainLoop)
      return;

   // Check for players within range.  If found, send them to dest.
   Rect queryRect(getVert(0), (F32)TELEPORTER_RADIUS);

   foundObjects.clear();
   findObjects((TestFunc)isShipType, foundObjects, queryRect);

   // First see if we're triggered...
   bool isTriggered = false;
   Point pos = getVert(0);

   for(S32 i = 0; i < foundObjects.size(); i++)
   {
      Ship *s = dynamic_cast<Ship *>(foundObjects[i]);
      if((pos - s->getActualPos()).len() < TeleporterTriggerRadius)
      {
         isTriggered = true;
         timeout = mTeleporterDelay;    // Temporarily disable teleporter
         // break; <=== maybe, need to test
      }
   }

   if(!isTriggered)
      return;
   
   // We've triggered the teleporter.  Relocate ship.
   for(S32 i = 0; i < foundObjects.size(); i++)
   {
      Ship *ship = dynamic_cast<Ship *>(foundObjects[i]);
      if((pos - ship->getRenderPos()).len() < TELEPORTER_RADIUS + ship->getRadius())
      {
         mLastDest = TNL::Random::readI(0, mDests.size() - 1);
         Point newPos = ship->getActualPos() - pos + mDests[mLastDest];    
         ship->setActualPos(newPos, true);
         setMaskBits(TeleportMask);

         if(ship->getClientInfo() && ship->getClientInfo()->getStatistics())
            ship->getClientInfo()->getStatistics()->mTeleport++;

         // See if we've teleported onto a loadout zone
         BfObject *zone = ship->isInZone(LoadoutZoneTypeNumber);
         if(zone)
            zone->collide(ship);
      }
   }
}


void Teleporter::render()
{
#ifndef ZAP_DEDICATED
   // Render at a different radius depending on if a ship has just gone into the teleport
   // and we are waiting for the teleport timeout to expire
   F32 radiusFraction;
   if(!mHasExploded)
   {
      if(timeout == 0)
         radiusFraction = 1;
      else if(timeout > TeleporterExpandTime - TeleporterDelay + mTeleporterDelay)
         radiusFraction = (timeout - TeleporterExpandTime + TeleporterDelay - mTeleporterDelay) / F32(TeleporterDelay - TeleporterExpandTime);
      else if(mTeleporterDelay < TeleporterExpandTime)
         radiusFraction = F32(mTeleporterDelay - timeout + TeleporterExpandTime - TeleporterDelay) / F32(mTeleporterDelay + TeleporterExpandTime - TeleporterDelay);
      else if(timeout < TeleporterExpandTime)
         radiusFraction = F32(TeleporterExpandTime - timeout) / F32(TeleporterExpandTime);
      else
         radiusFraction = 0;
   }
   else
   {
      // If the teleport has been destroyed, adjust the radius larger/smaller for a neat effect
      U32 halfPeriod = mExplosionTimer.getPeriod() / 2;
      if(mExplosionTimer.getCurrent() > halfPeriod)
         radiusFraction = 2.f - (F32(mExplosionTimer.getCurrent() - halfPeriod) / F32(halfPeriod));
      else
         radiusFraction = 2 * (F32(mExplosionTimer.getCurrent()) / F32(halfPeriod));

      // Add ending explosion
      if(mExplosionTimer.getCurrent() == 0 && !mFinalExplosionTriggered)
      {
         doExplosion();
      }
   }

   if(radiusFraction != 0)
   {
      F32 zoomFraction = static_cast<ClientGame *>(getGame())->getCommanderZoomFraction();
      U32 renderStyle = mEngineered ? 2 : 0;
      renderTeleporter(getVert(0), renderStyle, true, mTime, zoomFraction, radiusFraction, (F32)TELEPORTER_RADIUS, 1.0, mDests, false);
   }

   if(mEngineered)
   {
      // We render the exit point of engineered teleports with an outline
      renderTeleporterOutline(getVert(1), (F32)TELEPORTER_RADIUS, Colors::richGreen);
   }
#endif
}

#ifndef ZAP_DEDICATED
void Teleporter::doExplosion()
{
   mFinalExplosionTriggered = true;
   const S32 EXPLOSION_COLOR_COUNT = 12;

   static Color ExplosionColors[EXPLOSION_COLOR_COUNT] = {
         Colors::green,
         Color(0, 1, 0.5),
         Colors::white,
         Colors::yellow,
         Colors::green,
         Color(0, 0.8, 1.0),
         Color(0, 1, 0.5),
         Colors::white,
         Colors::green,
         Color(0, 1, 0.5),
         Colors::white,
         Colors::yellow,
   };

   SoundSystem::playSoundEffect(SFXShipExplode, getPos());

   F32 a = TNL::Random::readF() * 0.4f + 0.5f;
   F32 b = TNL::Random::readF() * 0.2f + 0.9f;
   F32 c = TNL::Random::readF() * 0.15f + 0.125f;
   F32 d = TNL::Random::readF() * 0.2f + 0.9f;

   ClientGame *game = static_cast<ClientGame *>(getGame());

   Point pos = getPos();

   game->emitExplosion(pos, 0.65f, ExplosionColors, EXPLOSION_COLOR_COUNT);
   game->emitBurst(pos, Point(a,c) * 0.6f, Colors::yellow, Colors::green);
   game->emitBurst(pos, Point(b,d) * 0.6f, Colors::yellow, Colors::green);
}
#endif

void Teleporter::renderEditorItem()
{
#ifndef ZAP_DEDICATED
   glColor(Colors::green);

   glLineWidth(gLineWidth3);
   drawPolygon(getVert(0), 12, (F32)TELEPORTER_RADIUS, 0);
   glLineWidth(gDefaultLineWidth);
#endif
}


Color Teleporter::getEditorRenderColor()
{
   return Colors::green;
}


void Teleporter::onAttrsChanging()
{
   /* Do nothing */
}


void Teleporter::onGeomChanging()
{
   /* Do nothing */
}


const char *Teleporter::getEditorHelpString()
{
   return "Teleports ships from one place to another. [T]";
}


const char *Teleporter::getPrettyNamePlural()
{
   return "Teleporters";
}


const char *Teleporter::getOnDockName()
{
   return "Teleport";
}


const char *Teleporter::getOnScreenName()
{
   return "Teleport";
}


bool Teleporter::hasTeam()
{
   return false;
}


bool Teleporter::canBeHostile()
{
   return false;
}


bool Teleporter::canBeNeutral()
{
   return false;
}


// Lua methods

const char Teleporter::className[] = "Teleporter";      // Class name as it appears to Lua scripts

// Lua constructor
Teleporter::Teleporter(lua_State *L)
{
   // Do nothing
}


// Define the methods we will expose to Lua
Lunar<Teleporter>::RegType Teleporter::methods[] =
{
   // Standard gameItem methods
   method(Teleporter, getClassID),
   method(Teleporter, getLoc),
   method(Teleporter, getRad),
   method(Teleporter, getVel),
   method(Teleporter, getTeamIndx),

   {0,0}    // End method list
};


S32 Teleporter::getClassID(lua_State *L)
{
   return returnInt(L, TeleportTypeNumber);
}


void Teleporter::push(lua_State *L)
{
   Lunar<Teleporter>::push(L, this);
}


S32 Teleporter::getRad(lua_State *L)
{
   return returnInt(L, TeleporterTriggerRadius);
}


S32 Teleporter::getVel(lua_State *L)
{
   return returnPoint(L, Point(0, 0));
}


};

