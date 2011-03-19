//----------------------------------------------------------------------------------
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

//#define usep2t

#include "BotNavMeshZone.h"
#include "GeomUtils.h"
#include "robot.h"
#include "UIMenus.h"
#include "UIGame.h"           // for access to mGameUserInterface.mDebugShowMeshZones
#include "gameObjectRender.h"
#include "teleporter.h"
#include "barrier.h"             // For Barrier methods in generating zones
#include "../recast/Recast.h"    // For zone generation
#include "../recast/RecastAlloc.h"

#include <vector>

#include "../clipper/clipper.h"

#ifdef usept2
#include "../poly2tri/poly2tri/poly2tri.h"
#endif

extern "C" {
#include "../Triangle/triangle.h"      // For Triangle!
}


// triangulate wants quit on error
// will probably crash on error when this function exits
//extern "C" void triexit(int status)
//{
//   TNLAssert(false, "Triangulate error");
//   logprintf(LogConsumer::LogError, "While generating bot zones, Triangulate error");
//   // here may be a good place to print lots of data.
//   //throw;  // any better ways?
//}



namespace Zap
{

TNL_IMPLEMENT_NETOBJECT(BotNavMeshZone);


Vector<SafePtr<BotNavMeshZone> > gBotNavMeshZones;     // List of all our zones

// Constructor
BotNavMeshZone::BotNavMeshZone()
{
   mGame = NULL;
   mObjectTypeMask = BotNavMeshZoneType | CommandMapVisType;
   //  The Zones is now rendered without the network interface, if client is hosting.
   //mNetFlags.set(Ghostable);    // For now, so we can see them on the client to help with debugging... when too many zones causes huge lag
   mZoneId = gBotNavMeshZones.size();

   gBotNavMeshZones.push_back(this);
}


// Destructor
BotNavMeshZone::~BotNavMeshZone()
{
   for(S32 i = gBotNavMeshZones.size() - 1; i >= 0; i--)  // For speed, check in reverse order. Game::cleanUp() clears on reverse order.
   if(gBotNavMeshZones[i] == this)
   {
      gBotNavMeshZones.erase_fast(i);
      break;
   }
   if(mGame)
   {
      removeFromDatabase();
      mGame = NULL;
   }
}


// Return the center of this zone
Point BotNavMeshZone::getCenter()
{
   return getExtent().getCenter();     // Good enough for government work
}


void BotNavMeshZone::render(S32 layerIndex)    
{
   if(!gClientGame->mGameUserInterface->mDebugShowMeshZones)
      return;

   if(mPolyFill.size() == 0)    //Need to process PolyFill here, rendering server objects into client.
         Triangulate::Process(mPolyBounds, mPolyFill);

   if(layerIndex == 0)
      renderNavMeshZone(mPolyBounds, mPolyFill, mCentroid, mZoneId, mConvex);
   else if(layerIndex == 1)
      renderNavMeshBorders(mNeighbors);
}


// Use this to help keep track of which robots are where
// Only gets run on the server, never on client, obviously, because that's where the bots are!!!
bool BotNavMeshZone::collide(GameObject *hitObject)
{
   // This does not get run anymore, it is in a seperate database.
   if(hitObject->getObjectTypeMask() & RobotType)     // Only care about robots...
   {
      Robot *r = (Robot *) hitObject;
      r->setCurrentZone(mZoneId);
   }
   return false;
}


S32 BotNavMeshZone::getRenderSortValue()
{
   return -2;
}

GridDatabase *BotNavMeshZone::getGridDatabase()
{
   return &mGame->mDatabaseForBotZones;
}


extern bool isConvex(const Vector<Point> &verts);

// Create objects from parameters stored in level file
bool BotNavMeshZone::processArguments(S32 argc, const char **argv)
{
   if(argc < 6)
      return false;

   processPolyBounds(argc, argv, 0, mGame->getGridSize());
   computeExtent();  // Computes extent so we can insert this into the BotNavMesh object database
   mConvex = isConvex(mPolyBounds);

   return true;
}


void BotNavMeshZone::addToGame(Game *game)
{
   // Ordinarily, we'd call GameObject::addToGame() here, but the BotNavMeshZones don't need to be added to the game
   // the way an ordinary game object would be.  So we won't.
   mGame = game;
   addToDatabase();
}


void BotNavMeshZone::onAddedToGame(Game *theGame)
{
   TNLAssert(false, "Should not be added to game");
}


// Bounding box for quick collision-possibility elimination
void BotNavMeshZone::computeExtent()
{
   Rect extent(mPolyBounds);
   setExtent(extent);
}


// More precise boundary for precise collision detection
bool BotNavMeshZone::getCollisionPoly(Vector<Point> &polyPoints)
{
   for(S32 i = 0; i < mPolyBounds.size(); i++)
      polyPoints.push_back(mPolyBounds[i]);

   return true;
}


// These methods will be empty later...
 U32 BotNavMeshZone::packUpdate(GhostConnection *connection, U32 updateMask, BitStream *stream)
{
   stream->writeInt(mZoneId, 16);

   Polygon::packUpdate(connection, stream);

   stream->writeInt(mNeighbors.size(), 8);

   for(S32 i = 0; i < mNeighbors.size(); i++)
   {
      stream->write(mNeighbors[i].borderStart.x);
      stream->write(mNeighbors[i].borderStart.y);

      stream->write(mNeighbors[i].borderEnd.x);
      stream->write(mNeighbors[i].borderEnd.y);

      stream->write(mNeighbors[i].borderCenter.x);
      stream->write(mNeighbors[i].borderCenter.y);

      stream->write(mNeighbors[i].zoneID);
      stream->write(mNeighbors[i].distTo);
      stream->write(mNeighbors[i].center.x);
      stream->write(mNeighbors[i].center.y);
   }

   return 0;
}


void BotNavMeshZone::unpackUpdate(GhostConnection *connection, BitStream *stream)
{
   mZoneId = stream->readInt(16);

   if(Polygon::unpackUpdate(connection, stream))
   {
      computeExtent();
      mConvex = isConvex(mPolyBounds);
   }

   U32 size = stream->readInt(8);
   Point p1, p2;

   for(U32 i = 0; i < size; i++)
   {
      NeighboringZone n;

      stream->read(&p1.x);
      stream->read(&p1.y);
   
      stream->read(&p2.x);
      stream->read(&p2.y);

      n.borderStart = p1;
      n.borderEnd = p2;
      stream->read(&n.borderCenter.x);
      stream->read(&n.borderCenter.y);
      stream->read(&n.zoneID);
      stream->read(&n.distTo);
      stream->read(&n.center.x);
      stream->read(&n.center.y);
      mNeighbors.push_back(n);
     // mNeighborRenderPoints.push_back(Border(p1, p2));
   }
}


// Returns ID of zone containing specified point
U16 BotNavMeshZone::findZoneContaining(const Point &p)
{
   Vector<DatabaseObject *> fillVector;
   gServerGame->mDatabaseForBotZones.findObjects(BotNavMeshZoneType, fillVector, 
                                                      Rect(p - Point(0.1f,0.1f),p + Point(0.1f,0.1f)));  // Slightly extend Rect, it can be on the edge of zone

   for(S32 i = 0; i < fillVector.size(); i++)
   {
      // First a quick, crude elimination check then more comprehensive one
      // Since our zones are convex, we can use the faster method!  Yay!
      // Actually, we can't, as it is not reliable... reverting to more comprehensive (and working) version.
      BotNavMeshZone *zone = dynamic_cast<BotNavMeshZone *>(fillVector[i]);

      TNLAssert(zone, "NULL zone in findZoneContaining");
      if( zone->getExtent().contains(p) 
                        && (PolygonContains2(zone->mPolyBounds.address(), zone->mPolyBounds.size(), p)) )
         return zone->mZoneId;
   }

   return U16_MAX;
}


void testBotNavMeshZoneConnections()
{
   return;

   //for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
   //   for(S32 j = 0; j < gBotNavMeshZones.size(); j++)
   //   {
   //      Vector<S32> path = AStar::findPath(i, j);
   //      for(S32 k = 0; k < path.size(); k++)
   //         logprintf("Path %d from %d to %d: %d", k, i, j, path[k]);
   //      logprintf("");
   //   }
}


// Returns index of neighboring zone, or -1 if zone is not a neighbor
S32 BotNavMeshZone::getNeighborIndex(S32 zoneID)
{
   for(S32 i = 0; i < mNeighbors.size(); i++)
      if(mNeighbors[i].zoneID == zoneID)
         return i;

   return -1;
}


//const F32 MinZoneSize = 32;

//static void makeBotMeshZones(F32 x1, F32 y1, F32 x2, F32 y2)
//{
//
//	if(gBotNavMeshZones.size() >= MAX_ZONES)      // Don't add too many zones...
//      return;   
//
//	GridDatabase *gb = gServerGame->getGridDatabase();
//
//	bool canseeX = gb->pointCanSeePoint(Point(x1, y1), Point(x2, y1)) && gb->pointCanSeePoint(Point(x1, y2), Point(x2, y2));
//	bool canseeY = gb->pointCanSeePoint(Point(x1, y1), Point(x1, y2)) && gb->pointCanSeePoint(Point(x2, y1), Point(x2, y2));
//	bool canseeD = gb->pointCanSeePoint(Point(x1, y1), Point(x2, y2)) && gb->pointCanSeePoint(Point(x1, y2), Point(x2, y1));
//
//	if(canseeX && canseeY && canseeD)
//	{
//		BotNavMeshZone *botzone = new BotNavMeshZone();
//
//		botzone->mPolyBounds.push_back(Point(x1, y1));
//		botzone->mPolyBounds.push_back(Point(x2, y1));
//		botzone->mPolyBounds.push_back(Point(x2, y2));
//		botzone->mPolyBounds.push_back(Point(x1, y2));
//      botzone->mCentroid.set((x1 + x2) / 2, (y1 + y2) / 2);
//
//		botzone->mConvex = true;             // Avoid random red and green on /dzones, if this is uninitalized
//		botzone->addToGame(gServerGame);
//		botzone->computeExtent();            // Adds zone to the database, needed for computation of further zones
//	}
//	else
//	{
//		if((canseeX && canseeY) || canseeD) 
//      { 
//         canseeX = false; 
//         canseeY = false; 
//      };
//
//		if(!canseeX && x2 - x1 >= MinZoneSize && (canseeY || x2 - x1 > y2 - y1))
//		{
//			F32 x3 = (x1 + x2) / 2;
//			makeBotMeshZones(x1, y1, x3, y2);
//			makeBotMeshZones(x3, y1, x2, y2);
//		}
//		else if(!canseeY && y2 - y1 >= MinZoneSize)
//		{
//			F32 y3 = (y1 + y2)/2;
//			makeBotMeshZones(x1, y1, x2, y3);
//			makeBotMeshZones(x1, y3, x2, y2);
//		}
//	}
//}


S32 QSORT_CALLBACK pointDataSort(Point *a, Point *b)
{
   if(a->x == b->x)
   {
      if(a->y == b->y)
         return 0;
      else if(a->y > b->y)
         return 1;
      else
         return -1;
   }
   else if(a->x > b->x)
      return 1;
   else
      return -1;
}

bool isBotZoneCollideWithOtherZone(Point *p1, Point *p2, Point *p3)
{
   GridDatabase *gb = &gServerGame->mDatabaseForBotZones;
   Vector<Point> pts(3);
   pts.setSize(3);
   //const F32 mult1 = 0.000244140625;
   //const F32 mult2 = 1 - mult1*2;
   // Slightly reduce size to avoid false positive when one line touch the other.
   //pts[0].x = p1->x * mult2 + p2->x * mult1 + p3->x * mult1;
   //pts[0].y = p1->y * mult2 + p2->y * mult1 + p3->y * mult1;
   //pts[1].x = p2->x * mult2 + p1->x * mult1 + p3->x * mult1;
   //pts[1].y = p2->y * mult2 + p1->y * mult1 + p3->y * mult1;
   //pts[2].x = p3->x * mult2 + p1->x * mult1 + p2->x * mult1;
   //pts[2].y = p3->y * mult2 + p1->y * mult1 + p2->y * mult1;
   pts[0].setPolar(0.1, p1->angleTo(*p2));
   pts[1].setPolar(0.1, p2->angleTo(*p1));
   pts[2].setPolar(0.1, p3->angleTo(*p1));
   Point tmp;
   tmp.setPolar(0.1, p1->angleTo(*p3));
   pts[0] += tmp + *p1;
   tmp.setPolar(0.1, p2->angleTo(*p3));
   pts[1] += tmp + *p2;
   tmp.setPolar(0.1, p3->angleTo(*p2));
   pts[2] += tmp + *p3;
   Vector<DatabaseObject *> objects;
   Rect rect = Rect(Point(min(min(p1->x, p2->x), p3->x), min(min(p1->y, p2->y), p3->y)), Point(max(max(p1->x, p2->x), p3->x), max(max(p1->y, p2->y), p3->y)));
   gb->findObjects(BotNavMeshZoneType, objects, rect);
   for(S32 i=0; i<objects.size(); i++)
   {
      BotNavMeshZone *zone = dynamic_cast<BotNavMeshZone *>(objects[i]);
      Vector<Point> otherPolygon;
      zone->getCollisionPoly(otherPolygon);
      if(polygonsIntersect(pts, otherPolygon))
         return true;
   }
   gb = gServerGame->getGridDatabase();
   objects.clear();
   gb->findObjects(BarrierType, objects, rect);
   for(S32 i=0; i<objects.size(); i++)
   {
      Barrier *zone = dynamic_cast<Barrier *>(objects[i]);
      Vector<Point> otherPolygon;
      zone->getCollisionPoly(otherPolygon);
      if(polygonsIntersect(pts, otherPolygon))
         return true;
   }
   return false;
}


//// from gridDB.cpp
//extern bool polygonLineIntersect(Point *poly, U32 vertexCount, bool format, const Point &start, const Point &end, 
//                                 float &collisionTime, Point &normal);
//
//// Fills output with location of all intersections between poly specified in input, and ray start->end
//void getPolygonLineCollisionPoints(Vector<Point> &output, const Vector<Point> &input, Point start, Point end)
//{
//   F32 dist;                  // Ranges between 0 and 1
//   Point normal_unused;
//   Point intersection;        
//
//   while(polygonLineIntersect(input.address(), input.size(), true, start, end, dist, normal_unused))     // Sets dist at point of first intersection
//   {
//      intersection = start * (1 - dist) + end * dist;     // Advance intersection to point of intersection
//      output.push_back(intersection);                     // Save point
//      dist += 0.01;                                       // Advance distance just a bit
//
//      if(dist >= 1.0)                                     // We've arrived at the end of the ray, so we're done
//         break;
//
//		intersection = start;                               // Temporarily move intersection to start so we can use it to compare
//      start = start * (1 - dist) + end * dist;            // Advance start to just past the intersection, so we can search for the next one
//
//		if(start == intersection)                           // Avoid endless loop -- should never happen!
//         break;   
//   }
//}
//
//
//void getBarrierLineCollisionPoints(Vector<Point> &output, GridDatabase *gb, Point p1, Point p2)
//{
//   S32 startOut = output.size();
//   Vector<DatabaseObject *> objects;
//   Rect rect(p1, p2);
//   gb->findObjects(BarrierType, objects, rect);
//   for(S32 i=0; i<objects.size(); i++)
//   {
//      Barrier *obj = dynamic_cast<Barrier *>(objects[i]);
//      getPolygonLineCollisionPoints(output, obj->mPoints, p1, p2);
//   }
//}
//
//
//void makeBotMeshZones2part(Vector<Point> &data)
//{
//   const F32 minSize = 10;
//   data.sort(pointDataSort);
//   for(S32 i=data.size()-2; i>=0; i--) // remove duplicate
//   {
//      if(data[i+1] == data[i])
//         data.erase(i);
//   }
//
//   GridDatabase *gb = gServerGame->getGridDatabase();
//   for(S32 i=0; i<data.size(); i++)
//   {
//      for(S32 j=i+1; j<data.size(); j++)
//      {
//            bool botZoneAdded = false;
//            const F32 ShortRange = 262144;     // 512^2
//            for(S32 k=j+1; k<data.size() && !botZoneAdded; k++)
//            {
//               if( //&& gb->pointCanSeePoint(data[k].point,data[i].point)
//                  //&& gb->pointCanSeePoint(data[k].point,data[j].point)
//                 !isBotZoneCollideWithOtherZone(&data[i], &data[j], &data[k]) &&
//                 (data[i].distanceTo(data[k]) > minSize
//                 || data[j].distanceTo(data[k]) > minSize
//                 || data[i].distanceTo(data[j]) > minSize) )
//               {
//		            BotNavMeshZone *botzone = new BotNavMeshZone();
//
//            		botzone->mPolyBounds.push_back(data[i]);
//		            botzone->mPolyBounds.push_back(data[j]);
//		            botzone->mPolyBounds.push_back(data[k]);
//                  botzone->mCentroid = (data[i] + data[j] + data[k]) * 0.33333333f;  //  maybe (/ 3) instead of (* 0.333333) ?
//
//		            botzone->mConvex = true;             // Avoid random red and green on /dzones, if this is uninitalized
//		            botzone->addToGame(gServerGame);
//		            botzone->computeExtent();            // Adds zone to the database, needed for computation of further zones
//
//               }
//            }
//      }
//   }
//}

//
//static void makeBotMeshZones2(Rect& bounds)
//{
//   F32 x1 = bounds.min.x;
//   F32 y1 = bounds.min.y;
//   F32 x2 = bounds.max.x;
//   F32 y2 = bounds.max.y;
//
//   S32 dataX = S32((x2-x1)*.0035)+1;
//   S32 dataY = S32((y2-y1)*.0035)+1;
//   F32 multX = dataX/(x2-x1);
//   F32 multY = dataY/(y2-y1);
//   Vector<Vector<Point> > data;
//   data.setSize(dataX * dataY);
//
//   // since there is nothing in bot zone database, we can change BucketWidth size.
//   gServerGame->mDatabaseForBotZones.BucketWidth = S32(max(y2-y1,x2-x1)/GridDatabase::BucketRowCount) + 1;
//  
//   for(S32 i=0; i < gServerGame->mGameObjects.size(); i++)
//   {
//      Barrier *object = dynamic_cast<Barrier *>(gServerGame->mGameObjects[i]);
//      if(object && object->getObjectTypeMask() & BarrierType)
//      {
//         Vector<Point> points;
//         if(object->getCollisionPoly(points))
//         {
//            /*
//            Point prevPoint;
//            Point currentPoint = points[points.size()-2];
//            Point nextPoint = points.last();
//            for(S32 j=0; j<points.size(); j++)
//            {
//               prevPoint = currentPoint;
//               currentPoint = nextPoint;
//               nextPoint = points[j];
//               S32 x = S32(((currentPoint.x - x1) * multX));
//               S32 y = S32(((currentPoint.y - y1) * multY));
//               if(x<0) x=0;
//               if(x>=dataX) x=dataX-1;
//               if(y<0) y=0;
//               if(y>=dataY) y=dataY-1;
//               data[y*dataX + x].push_back(currentPoint);
//            }*/
//            object->prepareRenderingGeometry2();
//            for(S32 j = 0; j < object->mRenderLineSegments.size(); j++)
//            {
//               Point currentPoint = object->mRenderLineSegments[j];
//               S32 x = S32(((currentPoint.x - x1) * multX));
//               S32 y = S32(((currentPoint.y - y1) * multY));
//               if(x<0) x=0;
//               if(x>=dataX) x=dataX-1;
//               if(y<0) y=0;
//               if(y>=dataY) y=dataY-1;
//               data[y*dataX + x].push_back(currentPoint);
//            }
//         }
//      }
//   }
//   for(S32 x=0; x<dataX; x++)
//   {
//      for(S32 y=0; y<dataY; y++)
//      {
//         S32 d=y*dataX + x;
//         S32 p = data[d].size();
//         data[d].push_back(Point( x    / multX + x1, y    / multY + y1));
//         data[d].push_back(Point((x+1) / multX + x1, y    / multY + y1));
//         data[d].push_back(Point((x+1) / multX + x1,(y+1) / multY + y1));
//         data[d].push_back(Point( x    / multX + x1,(y+1) / multY + y1));
//         getBarrierLineCollisionPoints(data[d], gServerGame->getGridDatabase(), data[d][p], data[d][p+1]);
//         getBarrierLineCollisionPoints(data[d], gServerGame->getGridDatabase(), data[d][p+1], data[d][p+2]);
//         getBarrierLineCollisionPoints(data[d], gServerGame->getGridDatabase(), data[d][p+2], data[d][p+3]);
//         getBarrierLineCollisionPoints(data[d], gServerGame->getGridDatabase(), data[d][p+3], data[d][p]);
//         //for(S32 i=0; i<data[d].size(); i++)
//         //{
//         //   logprintf("%i = %i,%i", d, S32(data[d][i].x), S32(data[d][i].y));
//         //}
//         makeBotMeshZones2part(data[d]);
//      }
//   }
//}



extern Rect gServerWorldBounds;

// Only runs on server
static void removeUnusedNavMeshZones()
{
   Vector<U16> inProcessList;      

   for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
      gBotNavMeshZones[i]->flag = false;

   GameType *gameType = gServerGame->getGameType();
   TNLAssert(gameType, "Invalid gametype... cannot proceed!");

   if(!gameType)
      return;

   // Start with list of all spawns and teleport outtakes --> these are the places a bot could "appear"
   // First the spawns
   for(S32 i = 0; i < gameType->mTeams.size(); i++)
      for(S32 j = 0; j < gameType->mTeams[i].spawnPoints.size(); j++)
      {
         U16 zoneIndex = BotNavMeshZone::findZoneContaining(gameType->mTeams[i].spawnPoints[j]);

         if(zoneIndex < U16_MAX)
         {
            gBotNavMeshZones[zoneIndex]->flag = true;        // Mark zone as processed
            inProcessList.push_back(zoneIndex);
         }
      }

   // Then the teleporters
   Vector<DatabaseObject *> teleporters;

   // Find all teleporters in the game
   gServerGame->getGridDatabase()->findObjects(TeleportType, teleporters, gServerWorldBounds);

   for(S32 i = 0; i < teleporters.size(); i++)
   {
      Teleporter *teleporter = dynamic_cast<Teleporter *>(teleporters[i]);

      if(teleporter)
         for(S32 j = 0; j < teleporter->mDest.size(); j++)
         {
            U16 zoneIndex = BotNavMeshZone::findZoneContaining(teleporter->mDest[j]);
            if(zoneIndex < U16_MAX)
            {
               gBotNavMeshZones[zoneIndex]->flag = true;        // Mark zone as processed
               inProcessList.push_back(zoneIndex);
            }
         }
   }

   // From here on down, very inefficient, but ok for testing the idea.  Need to precompute some of these things!
   // Since the order in which we process the zones doesn't matter, work from the end of the last towards the front; it's more efficient 
   // that way...

   Point start, end;    // Reusable container

   while(inProcessList.size() > 0)
   {
      S32 zoneIndex = inProcessList.last();
      inProcessList.erase(inProcessList.size() - 1);     // Remove last element      

      // Visit all neighboring zones
      for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
      {
         if(i == zoneIndex)
            continue;      // Don't check self...

         // Do zones i and j touch?  First a quick and dirty bounds check:
         if(!gBotNavMeshZones[zoneIndex]->getExtent().intersectsOrBorders(gBotNavMeshZones[i]->getExtent()))
            continue;

         if(zonesTouch(*gBotNavMeshZones[zoneIndex]->getPolyBoundsPtr(), *gBotNavMeshZones[i]->getPolyBoundsPtr(), 
                       1 / gServerGame->getGridSize(), start, end))
         {
            if(!gBotNavMeshZones[i]->flag)           // If zone hasn't been processed...
            {
               inProcessList.push_back(i);
               gBotNavMeshZones[i]->flag = true;     // Mark zone as "in"
            }
         }
      }
   }

   // Anything not marked as in at this point is out.  Delete it.
   for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
   {
      if(!gBotNavMeshZones[i]->flag)
      {
         gBotNavMeshZones[i]->removeFromDatabase();
         gBotNavMeshZones.erase_fast(i);
         i--;
      }
   }

   // Make a final pass and recalculate the zone ids to be equal to the index; some of our processes depend on this
   // Also calc the centroid and add to the zone database
   for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
   {
      gBotNavMeshZones[i]->setZoneId(i);

		gBotNavMeshZones[i]->mConvex = true;             // avoid random red and green on /dzones, was uninitalized.
		gBotNavMeshZones[i]->addToGame(gServerGame);
		gBotNavMeshZones[i]->computeExtent();

      // As long as our zones are rectangular, this shortcut will work
      gBotNavMeshZones[i]->mCentroid.set(gBotNavMeshZones[i]->getExtent().getCenter());
   }
}


struct rcEdge
{
	unsigned short vert[2];    // from, to verts
	unsigned short poly[2];    // left, right poly
};

// Build connections between zones using the adjacency data created in recast
static bool buildBotNavMeshZoneConnectionsRecastStyle(rcPolyMesh mesh, const Vector<S32> &polyToZoneMap)    
{
   if(gBotNavMeshZones.size() == 0)
      return true;

   // We'll reuse these objects throughout the following block, saving the cost of creating and destructing them
   Point bordStart, bordEnd, bordCen;
   Rect rect;
   NeighboringZone neighbor;
   Vector<DatabaseObject *> objects;


   /////////////////////////////
   // Based on recast's interpretation of code by Eric Lengyel:
	// http://www.terathon.com/code/edges.php
	
   int maxEdgeCount = mesh.npolys*mesh.nvp;
	unsigned short* firstEdge = (unsigned short*)rcAlloc(sizeof(unsigned short)*(mesh.nverts + maxEdgeCount), RC_ALLOC_TEMP);
	if (!firstEdge)      // Allocation went bad
		return false;
	unsigned short* nextEdge = firstEdge + mesh.nverts;
	int edgeCount = 0;
	
	rcEdge* edges = (rcEdge*)rcAlloc(sizeof(rcEdge)*maxEdgeCount, RC_ALLOC_TEMP);
	if (!edges)
	{
		rcFree(firstEdge);
		return false;
	}
	

	for (int i = 0; i < mesh.nverts; i++)
		firstEdge[i] = RC_MESH_NULL_IDX;
	
   // First process edges where 1st node < 2nd node
	for (int i = 0; i < mesh.npolys; ++i)
	{
      unsigned short* t = &mesh.polys[i*mesh.nvp];

      // Skip "missing" polygons
      if(*t == U16_MAX)
         continue;

		for (int j = 0; j < mesh.nvp; ++j)
		{
			unsigned short v0 = t[j];           // jth vert

         if(v0 == RC_MESH_NULL_IDX)
            break;

			unsigned short v1 = (j+1 >= mesh.nvp || t[j+1] == RC_MESH_NULL_IDX) ? t[0] : t[j+1];  // j+1th vert

			if (v0 < v1)      
			{
				rcEdge& edge = edges[edgeCount];       // edge connecting v0 and v1
				edge.vert[0] = v0;
				edge.vert[1] = v1;
				edge.poly[0] = (unsigned short)i;      // left poly
				edge.poly[1] = (unsigned short)i;      // right poly, will be recalced later -- the fact that both are the same is used as a marker

				nextEdge[edgeCount] = firstEdge[v0];        // Next edge on the previous vert now points to whatever was in firstEdge previously
				firstEdge[v0] = (unsigned short)edgeCount;  // First edge of this vert 

				edgeCount++;                           // edgeCount never resets -- each edge gets a unique id
			}
		}
	}
	
   // Now process edges where 2nd node is > 1st node
	for (int i = 0; i < mesh.npolys; ++i)
	{
		unsigned short* t = &mesh.polys[i*mesh.nvp];

      // Skip "missing" polygons
      if(*t == U16_MAX)
         continue;

		for (int j = 0; j < mesh.nvp; ++j)
		{
			unsigned short v0 = t[j];
         if(v0 == RC_MESH_NULL_IDX)
            break;

			unsigned short v1 = (j+1 >= mesh.nvp || t[j+1] == RC_MESH_NULL_IDX) ? t[0] : t[j+1];

			if (v0 > v1)      
			{
				for (unsigned short e = firstEdge[v1]; e != RC_MESH_NULL_IDX; e = nextEdge[e])
				{
					rcEdge& edge = edges[e];
					if (edge.vert[1] == v0 && edge.poly[0] == edge.poly[1])
               {
						edge.poly[1] = (unsigned short)i;
                  break;
               }
				}
			}
		}
	}

	// Now create our neighbor data
	for (int i = 0; i < edgeCount; i++)
	{
		const rcEdge& e = edges[i];

		if (e.poly[0] != e.poly[1])      // Should normally be the case
		{
         U16 *v;

         v = &mesh.verts[e.vert[0] * 2];
         neighbor.borderStart.set(v[0]-S16_MAX, v[1]-S16_MAX);     // TODO: Replace this with FIX

         v = &mesh.verts[e.vert[1] * 2];
         neighbor.borderEnd.set(v[0]-S16_MAX, v[1]-S16_MAX);

         neighbor.borderCenter.set((neighbor.borderStart + neighbor.borderEnd) * 0.5);

         neighbor.zoneID = polyToZoneMap[e.poly[1]];  
         gBotNavMeshZones[polyToZoneMap[e.poly[0]]]->mNeighbors.push_back(neighbor);  // (copies neighbor implicitly)

         neighbor.zoneID = polyToZoneMap[e.poly[0]];   
         gBotNavMeshZones[polyToZoneMap[e.poly[1]]]->mNeighbors.push_back(neighbor);
		}
	}
	
	rcFree(firstEdge);
	rcFree(edges);
	
	return true;
}


 //  // Now create paths representing the teleporters
 //  Vector<DatabaseObject *> teleporters, dests;

	//gServerGame->getGridDatabase()->findObjects(TeleportType, teleporters, gServerWorldBounds);

	//for(S32 i = 0; i < teleporters.size(); i++)
	//{
	//	Teleporter *teleporter = dynamic_cast<Teleporter *>(teleporters[i]);

 //     if(!teleporter)
 //        continue;

 //     BotNavMeshZone *origZone = findZoneContainingPoint(teleporter->getActualPos());

 //     if(origZone != NULL)
	//	for(S32 j = 0; j < teleporter->mDest.size(); j++)     // Review each teleporter destination
	//	{
 //        BotNavMeshZone *destZone = findZoneContainingPoint(teleporter->mDest[j]);

	//		if(destZone != NULL && origZone != destZone)      // Ignore teleporters that begin and end in the same zone
	//		{
	//			// Teleporter is one way path
	//			neighbor.zoneID = destZone->mZoneID;
	//			neighbor.borderStart.set(teleporter->getActualPos());
	//			neighbor.borderEnd.set(teleporter->mDest[j]);
	//			neighbor.borderCenter.set(teleporter->getActualPos());

 //           // Teleport instantly, at no cost -- except this is wrong... if teleporter has multiple dests, actual cost could be quite high.
 //           // This should be the average of the costs of traveling from each dest zone to the target zone
	//			neighbor.distTo = 0;                                    
	//			neighbor.center.set(teleporter->getActualPos());

	//			origZone->mNeighbors.push_back(neighbor);
	//		}
	//	}
   //}
//}


extern bool loadBarrierPoints(const BarrierRec &barrier, Vector<Point> &points);

#define combine(x,y) pair<F32,F32>((x),(y))

using namespace clipper;


#ifdef TNL_DEBUG
//#define DUMP_DATA    // outputs data before triangulate and before / after clipper
#define DUMP_TIMER
#endif

// Use the Triangle library to create zones.  Optionally use modified Recast to aggregate zones
static void makeBotMeshZones3(Rect& bounds, Game* game, bool useRecast)
{
   // Just for fun, let's triangulate!
   Vector<F32> coords;
   Vector<F32> holes;
   Vector<S32> edges;


   F32 minx = bounds.min.x;  F32 miny = bounds.min.y;
   F32 maxx = bounds.max.x;  F32 maxy = bounds.max.y;

   coords.push_back(minx);   coords.push_back(miny);     // Point 0
   coords.push_back(minx);   coords.push_back(maxy);     // Point 1
   coords.push_back(maxx);   coords.push_back(maxy);     // Point 2
   coords.push_back(maxx);   coords.push_back(miny);     // Point 3

   edges.push_back(0);       edges.push_back(1);
   edges.push_back(1);       edges.push_back(2);
   edges.push_back(2);       edges.push_back(3);
   edges.push_back(3);       edges.push_back(0);

   S32 nextPt = 4;

#ifdef DUMP_TIMER
   U32 starttime = Platform::getRealMilliseconds();
#endif

   Point ctr;     // Reusable container for hole calculations

   TPolyPolygon solution;
   TPolygon inputPoly;
   Clipper clipper;
   clipper.IgnoreOrientation(true);

   for(S32 i = 0; i < game->mGameObjects.size(); i++)
   {
      if(game->mGameObjects[i]->getObjectTypeMask() & BarrierType)
      {
         Barrier *barrier = dynamic_cast<Barrier *>(game->mGameObjects[i]);

         inputPoly.clear();
         for (S32 j = 0; j < barrier->mBotZoneBufferGeometry.size(); j++)
            inputPoly.push_back(DoublePoint(barrier->mBotZoneBufferGeometry[j].x,barrier->mBotZoneBufferGeometry[j].y));

#ifdef DUMP_DATA
         for (S32 j = 0; j < barrier->mBotZoneBufferGeometry.size(); j++)
			   logprintf("Before Clipper Point: %f %f", barrier->mBotZoneBufferGeometry[j].x, barrier->mBotZoneBufferGeometry[j].y);
#endif

         clipper.AddPolygon(inputPoly, ptSubject);

         ctr = barrier->getExtent().getCenter();
         holes.push_back(ctr.x);
         holes.push_back(ctr.y);
      }
   }

   clipper.Execute(ctUnion, solution, pftNonZero, pftNonZero);

   TPolygon poly;
   for (U32 j = 0; j < solution.size(); j++)
   {
      poly = solution[j];

      if(poly.size() == 0)
         continue;

      S32 first = nextPt;
      for (U32 k = 0; k < poly.size(); k++)
      {
         coords.push_back(poly[k].X);
         coords.push_back(poly[k].Y);

         if(k > 0)
         {
            edges.push_back(nextPt);
            edges.push_back(nextPt + 1);
            nextPt++;
         }
      }

      edges.push_back(nextPt);
      edges.push_back(first);
      nextPt++;
   }


#ifdef DUMP_DATA
   // Dump points
   for(S32 i = 0; i < coords.size(); i+=2)
      logprintf("Point %d: %f, %f", i/2, coords[i], coords[i+1]);


   // Dump edges
   for(S32 i = 0; i < edges.size(); i+=2)
      logprintf("Edge %d, %d-%d", i/2, edges[i], edges[i+1]);
#endif
#ifdef DUMP_TIMER
   U32 done1 = Platform::getRealMilliseconds();
#endif
#ifdef usep2t

   p2t::Point p0(bounds.min.x, bounds.min.y);
   p2t::Point p1(bounds.max.x, bounds.min.y);
   p2t::Point p2(bounds.max.x, bounds.max.y);
   p2t::Point p3(bounds.min.x, bounds.max.y);

   vector<p2t::Point *> boundBox;

   boundBox.push_back(&p0);
   boundBox.push_back(&p1);
   boundBox.push_back(&p2);
   boundBox.push_back(&p3);

   p2t::CDT cdt(boundBox);

   // Add holes (i.e. walls)

   cdt.Triangulate();      // Make the triangles

   vector<p2t::Triangle *> tris = cdt.GetTriangles();

   if(useRecast)
   {
      // Recast only handles 16 bit coordinates
      TNLAssert((bounds.min.x > S16_MIN && bounds.min.y > S16_MIN && bounds.max.x < S16_MAX && bounds.max.y < S16_MAX), "Level out of bounds!");

      S32 FIX = S16_MAX;

      int ntris = tris.size();
      Vector<int> intPoints(ntris * 3 * 2);     // 2 entries per point: x,y
      Vector<int> trilist(ntris * 3);

      for(U32 i = 0; i < tris.size(); i++)
      {
         p2t::Triangle *tri = tris[i];

         for(S32 j = 0; j < 3; j++)
         {
            intPoints[i*3 + j*2] = S32(floor(tri->GetPoint(j)->x + .5)) + FIX;
            intPoints[i*3 + j*2 + 1] = S32(floor(tri->GetPoint(j)->y + .5)) + FIX;
            trilist[i*3 + j] = i*3 +j;
         }
      }

      rcPolyMesh mesh;

      // TODO: Delete mesh memory allocations

      // TODO: Put these into real tests, and handle conditions better  
      //TNLAssert(out.numberofpoints > 0, "No output points!");
      //TNLAssert(out.numberoftriangles > 0, "No output triangles!");
      //TNLAssert(out.numberofpoints < 0xffe, "Too many points!");


      // 6 is arbitrary --> smaller numbers require less memory
      bounds.offset(Point(FIX,FIX));
      rcBuildPolyMesh(6, intPoints.address(), ntris * 3, trilist.address(), ntris, mesh);     
      
      BotNavMeshZone *botzone = NULL;
   
      const S32 bytesPerVertex = sizeof(U16);      // Recast coords are U16s
      Vector<S32> polyToZoneMap(mesh.npolys);

      // Visualize rcPolyMesh
      for(S32 i = 0; i < mesh.npolys; i++)
      {
         S32 firstx = S32_MAX;
         S32 firsty = S32_MAX;
         S32 lastx = S32_MAX;
         S32 lasty = S32_MAX;

         S32 j = 0;
         botzone = NULL;

         while(j < mesh.nvp)
         {
            if(mesh.polys[(i * mesh.nvp + j)] == U16_MAX)
               break;

            const U16 *vert = &mesh.verts[mesh.polys[(i * mesh.nvp + j)] * bytesPerVertex];

            if(vert[0] == U16_MAX)
               break;

            if(j == 0)
            {
               botzone = new BotNavMeshZone();
               polyToZoneMap[i] = botzone->getZoneId();
            }

            botzone->mPolyBounds.push_back(Point(vert[0] - FIX, vert[1] - FIX));
            j++;
         }
   
         if(botzone != NULL)
         {
            botzone->mCentroid.set(findCentroid(botzone->mPolyBounds));

		      botzone->mConvex = true;             
		      botzone->addToGame(gServerGame);
		      botzone->computeExtent();   
         }
      }


      logprintf("Recast built %d zones!", gBotNavMeshZones.size() );

      buildBotNavMeshZoneConnectionsRecastStyle(mesh, polyToZoneMap);
   }
#else
   triangulateio in, out;

   initIoStruct(&in);
   initIoStruct(&out);

   in.numberofpoints = coords.size() / 2;
   in.pointlist = coords.address();

   in.segmentlist = edges.address();
   in.numberofsegments = edges.size() / 2;

   in.numberofholes = holes.size() / 2;
   in.holelist = holes.address();

   // Note the q param seems to make no difference in speed of trinagulation, but makes much prettier triangles!
   // Removing q does make a big difference in the speed of the aggregation of the triangles, at the cost of uglier zones
   // X option makes small but consistent improvement in performance

#ifdef DUMP_TIMER
   U32 done3 = Platform::getRealMilliseconds();
#endif
   triangulate((char*)"zpV", &in, &out, NULL);  // TODO: Replace V with Q after debugging
      // triangulate 'X' option have problem with crashing error in windows
#ifdef DUMP_TIMER
   U32 done4 = Platform::getRealMilliseconds();
#endif

   if(useRecast)
   {
      // Recast only handles 16 bit coordinates
      TNLAssert((bounds.min.x > S16_MIN && bounds.min.y > S16_MIN && bounds.max.x < S16_MAX && bounds.max.y < S16_MAX), "Level out of bounds!");

      S32 FIX = S16_MAX;

      int ntris = out.numberoftriangles;
      Vector<int> intPoints(out.numberofpoints * 2);     // 2 entries per point: x,y

      for(S32 i = 0; i < out.numberofpoints * 2; i++)
         intPoints[i] = S32(out.pointlist[i] < 0 ? out.pointlist[i] - 0.5 : out.pointlist[i] + 0.5) + FIX;  
 
      // cont.nverts = out.numberofpoints;
      // cont.verts = intPoints.address();      //<== pointer to array of points in int format
      // tris = out.trianglelist


      rcPolyMesh mesh;

      // TODO: Delete mesh memory allocations

      // TODO: Put these into real tests, and handle conditions better  
      TNLAssert(out.numberofpoints > 0, "No output points!");
      TNLAssert(out.numberoftriangles > 0, "No output triangles!");
      TNLAssert(out.numberofpoints < 0xffe, "Too many points!");


      // 6 is arbitrary --> smaller numbers require less memory
      bounds.offset(Point(FIX,FIX));
      rcBuildPolyMesh(6, intPoints.address(), out.numberofpoints, out.trianglelist, out.numberoftriangles, mesh);     
      
      BotNavMeshZone *botzone = NULL;
   
      const S32 bytesPerVertex = sizeof(U16);      // Recast coords are U16s
      Vector<S32> polyToZoneMap(mesh.npolys);

      // Visualize rcPolyMesh
      for(S32 i = 0; i < mesh.npolys; i++)
      {
         S32 firstx = S32_MAX;
         S32 firsty = S32_MAX;
         S32 lastx = S32_MAX;
         S32 lasty = S32_MAX;

         S32 j = 0;
         botzone = NULL;

         while(j < mesh.nvp)
         {
            if(mesh.polys[(i * mesh.nvp + j)] == U16_MAX)
               break;

            const U16 *vert = &mesh.verts[mesh.polys[(i * mesh.nvp + j)] * bytesPerVertex];

            if(vert[0] == U16_MAX)
               break;

            if(j == 0)
            {
               botzone = new BotNavMeshZone();
               polyToZoneMap[i] = botzone->getZoneId();
            }

            botzone->mPolyBounds.push_back(Point(vert[0] - FIX, vert[1] - FIX));
            j++;
         }
   
         if(botzone != NULL)
         {
            botzone->mCentroid.set(findCentroid(botzone->mPolyBounds));

		      botzone->mConvex = true;             
		      botzone->addToGame(gServerGame);
		      botzone->computeExtent();   
         }
      }


      logprintf("Recast built %d zones!", gBotNavMeshZones.size() );

      buildBotNavMeshZoneConnectionsRecastStyle(mesh, polyToZoneMap);
   }
   else
   {
      // Visualize triangle output
      for(S32 i = 0; i < out.numberoftriangles * 3; i+=3)
      {
         BotNavMeshZone *botzone = new BotNavMeshZone();

		   botzone->mPolyBounds.push_back(Point(F32(S32(out.pointlist[out.trianglelist[i]*2])),   F32(S32(out.pointlist[out.trianglelist[i]*2 + 1]))));
		   botzone->mPolyBounds.push_back(Point(F32(S32(out.pointlist[out.trianglelist[i+1]*2])), F32(S32(out.pointlist[out.trianglelist[i+1]*2 + 1]))));
		   botzone->mPolyBounds.push_back(Point(F32(S32(out.pointlist[out.trianglelist[i+2]*2])), F32(S32(out.pointlist[out.trianglelist[i+2]*2 + 1]))));
         botzone->mCentroid.set(findCentroid(botzone->mPolyBounds));

		   botzone->mConvex = true;             // Avoid random red and green on /dzones, if this is uninitalized
		   botzone->addToGame(gServerGame);
		   botzone->computeExtent();   
      }

      BotNavMeshZone::buildBotNavMeshZoneConnections();
   }

#ifdef DUMP_TIMER
   U32 done5 = Platform::getRealMilliseconds();
   logprintf("Timings: %d %d %d %d", done1-starttime, done3-done1, done4-done3, done5-done4);
#endif

   // TODO: free memory allocated by triangles in out struct
   trifree(out.pointlist);
   trifree(out.pointattributelist);
   trifree(out.pointmarkerlist);
   trifree(out.trianglelist);
   trifree(out.triangleattributelist);
   trifree(out.segmentlist);
   trifree(out.segmentmarkerlist);
   trifree(out.edgelist);
   trifree(out.edgemarkerlist);
   trifree(out.normlist);
   trifree(out.neighborlist);
#endif
}

// Server only
void BotNavMeshZone::buildBotMeshZones(Game *game)
{

	Rect bounds = game->computeWorldObjectExtents();
	bounds.expand(Point(30,30));

   if(gIniSettings.botZoneGeneratorMode == 0) //disabled
      return;
   if(gIniSettings.botZoneGeneratorMode == 1 || gIniSettings.botZoneGeneratorMode == 2) // rectangle bot zone
   {
      //makeBotMeshZones(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y);
      if(gIniSettings.botZoneGeneratorMode == 2) removeUnusedNavMeshZones();
      return;
   }

   if(gIniSettings.botZoneGeneratorMode == 3 || gIniSettings.botZoneGeneratorMode == 4) // simple triangle bot zones
   {
      //makeBotMeshZones2(bounds);
      if(gIniSettings.botZoneGeneratorMode == 4) removeUnusedNavMeshZones();
      return;
   }

   if(gIniSettings.botZoneGeneratorMode == 5 || gIniSettings.botZoneGeneratorMode == 6) // triangle with Triangle library
   {
      // Triangulate and Recast
      bool useRecast = gIniSettings.botZoneGeneratorMode == 6;

      // try and except allows continue running after error, but no zones get generated - windows only?
#ifdef TNL_OS_WIN32
      __try{
#endif
         makeBotMeshZones3(bounds, game, useRecast);
#ifdef TNL_OS_WIN32
      }__except(1){
         logprintf("Error in makeBotMeshZones3");
      }
#endif

      return;
   }

   // put default here if none of the above
}


Vector<DatabaseObject *> zones;
extern Rect gServerWorldBounds;

// Returns index of zone containing specified point
static BotNavMeshZone *findZoneContainingPoint(const Point &point)
{
   Rect rect(point, 0.01f);
   zones.clear();
   gServerGame->mDatabaseForBotZones.findObjects(BotNavMeshZoneType, zones, rect); 

   // If there is more than one possible match, pick the first arbitrarily (could happen if dest is right on a zone border)
   for(S32 i = 0; i < zones.size(); i++)
   {
      BotNavMeshZone *zone = dynamic_cast<BotNavMeshZone *>(zones[i]);     
      if(zone)
      {
         if(PolygonContains2(zone->mPolyBounds.address(), zone->mPolyBounds.size(), point))
            return zone;   // Make sure point is inside the polygon
      }
   }

   if(zones.size() != 0)  // in case of point was close to polygon, but not inside the zone?
      return dynamic_cast<BotNavMeshZone *>(zones[0]);

   return NULL;
}


// Only runs on server
void BotNavMeshZone::buildBotNavMeshZoneConnections()    
{
   if(gBotNavMeshZones.size() == 0)
      return;

   // We'll reuse these objects throughout the following block, saving the cost of creating and destructing them
   Point bordStart, bordEnd, bordCen;
   Rect rect;
   NeighboringZone neighbor;
   Vector<DatabaseObject *> objects;

   // Figure out which zones are adjacent to which, and find the "gateway" between them
   for(S32 i = 0; i < gBotNavMeshZones.size(); i++)
   {
      for(S32 j = i; j < gBotNavMeshZones.size(); j++)
      {
         if(i == j)
            continue;      // Don't check self...

         // Do zones i and j touch?  First a quick and dirty bounds check:
         if(!gBotNavMeshZones[i]->getExtent().intersectsOrBorders(gBotNavMeshZones[j]->getExtent()))
            continue;

         if(zonesTouch(gBotNavMeshZones[i]->mPolyBounds, gBotNavMeshZones[j]->mPolyBounds, 1.0, bordStart, bordEnd))
         {
            rect.set(bordStart, bordEnd);
            bordCen.set(rect.getCenter());

            // Zone j is a neighbor of i
            neighbor.zoneID = j;
            neighbor.borderStart.set(bordStart);
            neighbor.borderEnd.set(bordEnd);
            neighbor.borderCenter.set(bordCen);

            neighbor.distTo = gBotNavMeshZones[i]->getExtent().getCenter().distanceTo(bordCen);     // Whew!
            neighbor.center.set(gBotNavMeshZones[j]->getCenter());
            gBotNavMeshZones[i]->mNeighbors.push_back(neighbor);

            // Zone i is a neighbor of j
            neighbor.zoneID = i;
            neighbor.borderStart.set(bordStart);
            neighbor.borderEnd.set(bordEnd);
            neighbor.borderCenter.set(bordCen);

            neighbor.distTo = gBotNavMeshZones[j]->getExtent().getCenter().distanceTo(bordCen);     
            neighbor.center.set(gBotNavMeshZones[i]->getCenter());
            gBotNavMeshZones[j]->mNeighbors.push_back(neighbor);
         }
      }
   }
		
   // Now create paths representing the teleporters
   Vector<DatabaseObject *> teleporters, dests;

	gServerGame->getGridDatabase()->findObjects(TeleportType, teleporters, gServerWorldBounds);

	for(S32 i = 0; i < teleporters.size(); i++)
	{
		Teleporter *teleporter = dynamic_cast<Teleporter *>(teleporters[i]);

      if(!teleporter)
         continue;

      BotNavMeshZone *origZone = findZoneContainingPoint(teleporter->getActualPos());

      if(origZone != NULL)
		for(S32 j = 0; j < teleporter->mDest.size(); j++)     // Review each teleporter destination
		{
         BotNavMeshZone *destZone = findZoneContainingPoint(teleporter->mDest[j]);

			if(destZone != NULL && origZone != destZone)      // Ignore teleporters that begin and end in the same zone
			{
				// Teleporter is one way path
				neighbor.zoneID = destZone->mZoneId;
				neighbor.borderStart.set(teleporter->getActualPos());
				neighbor.borderEnd.set(teleporter->mDest[j]);
				neighbor.borderCenter.set(teleporter->getActualPos());

            // Teleport instantly, at no cost -- except this is wrong... if teleporter has multiple dests, actual cost could be quite high.
            // This should be the average of the costs of traveling from each dest zone to the target zone
				neighbor.distTo = 0;                                    
				neighbor.center.set(teleporter->getActualPos());

				origZone->mNeighbors.push_back(neighbor);
			}
		}
   }
}



// Rough guess as to distance from fromZone to toZone
F32 AStar::heuristic(S32 fromZone, S32 toZone)
{
   return gBotNavMeshZones[fromZone]->getCenter().distanceTo( gBotNavMeshZones[toZone]->getCenter() );
}

static const S32 MAX_ZONES = 10000;     // Don't make this go above S16 max - 1 (32,766), AStar::findPath is limited

// Returns a path, including the startZone and targetZone 
Vector<Point> AStar::findPath(S32 startZone, S32 targetZone, const Point &target)
{
   // Because of these variables...
   static U16 onClosedList = 0;
   static U16 onOpenList;

   // ...these arrays can be reused without further initialization
   static U16 whichList[MAX_ZONES];       // Record whether a zone is on the open or closed list
   static S16 openList[MAX_ZONES + 1]; 
   static S16 openZone[MAX_ZONES]; 
   static S16 parentZones[MAX_ZONES]; 

   static F32 Fcost[MAX_ZONES];	
   static F32 Gcost[MAX_ZONES]; 	
   static F32 Hcost[MAX_ZONES];	

	S16 numberOfOpenListItems = 0;
	bool foundPath;

	S32 newOpenListItemID = 0;         // Used for creating new IDs for zones to make heap work

   Vector<Point> path;


   // This block here lets us repeatedly reuse the whichList array without resetting it or recreating it
   // which, for larger numbers of zones should be a real time saver.  It's not clear if it is particularly
   // more efficient for the zone counts we typically see in Bitfighter levels.

   if(onClosedList > U16_MAX - 3 ) // Reset whichList when we've run out of headroom
	{
		for(S32 i = 0; i < MAX_ZONES; i++) 
		   whichList[i] = 0;
		onClosedList = 0;	
	}
	onClosedList = onClosedList + 2; // Changing the values of onOpenList and onClosed list is faster than redimming whichList() array
	onOpenList = onClosedList - 1;

	Gcost[startZone] = 0;         // That's the cost of going from the startZone to the startZone!
   Fcost[0] = Hcost[0] = heuristic(startZone, targetZone);

	numberOfOpenListItems = 1;    // Start with one open item: the startZone

	openList[1] = 0;              // Start with 1 item in the open list (must be index 1), which is maintained as a binary heap
	openZone[0] = startZone;

   // Loop until a path is found or deemed nonexistent.
	while(true)
	{
	   if(numberOfOpenListItems == 0)      // List is empty, we're done
	   {
		   foundPath = false; 
         break;
	   }  
      else
	   {
         // The open list is not empty, so take the first cell off of the list.
         //	Since the list is a binary heap, this will be the lowest F cost cell on the open list.
	      S32 parentZone = openZone[openList[1]];

         if(parentZone == targetZone)
         {
            foundPath = true; 
            break;
         }

         whichList[parentZone] = onClosedList;   // add the item to the closed list
         numberOfOpenListItems--;	

         //	Open List = Binary Heap: Delete this item from the open list, which
         // is maintained as a binary heap. For more information on binary heaps, see:
         //	http://www.policyalmanac.org/games/binaryHeaps.htm
      		
         //	Delete the top item in binary heap and reorder the heap, with the lowest F cost item rising to the top.
	      openList[1] = openList[numberOfOpenListItems + 1];   // Move the last item in the heap up to slot #1
	      S16 v = 1; 

         //	Loop until the new item in slot #1 sinks to its proper spot in the heap.
	      while(true) // ***
	      {
	         S16 u = v;		
	         if (2 * u + 1 < numberOfOpenListItems) // if both children exist
	         {
	 	         //Check if the F cost of the parent is greater than each child.
		         //Select the lowest of the two children.
		         if(Fcost[openList[u]] >= Fcost[openList[2*u]]) 
			         v = 2 * u;
		         if(Fcost[openList[v]] >= Fcost[openList[2*u+1]]) 
			         v = 2 * u + 1;		
	         }
	         else if (2 * u < numberOfOpenListItems) // if only child (#1) exists
	         {
 	            // Check if the F cost of the parent is greater than child #1	
		         if(Fcost[openList[u]] >= Fcost[openList[2*u]]) 
			         v = 2 * u;
	         }

	         if(u != v) // If parent's F is > one of its children, swap them...
	         {
               S16 temp = openList[u];
               openList[u] = openList[v];
               openList[v] = temp;			
	         }
	         else
		         break; // ...otherwise, exit loop
	      } // ***

         // Check the adjacent zones. (Its "children" -- these path children
         //	are similar, conceptually, to the binary heap children mentioned
         //	above, but don't confuse them. They are different. 
         // Add these adjacent child squares to the open list
         //	for later consideration if appropriate.

         Vector<NeighboringZone> neighboringZones = gBotNavMeshZones[parentZone]->mNeighbors;

         for(S32 a = 0; a < neighboringZones.size(); a++)
         {
            NeighboringZone zone = neighboringZones[a];
            S32 zoneID = zone.zoneID;

            //	Check if zone is already on the closed list (items on the closed list have
            //	already been considered and can now be ignored).			
            if(whichList[zoneID] == onClosedList) 
               continue;

            //	Add zone to the open list if it's not already on it
            TNLAssert(newOpenListItemID < MAX_ZONES, "Too many nav zones... try increasing MAX_ZONES!");
            if(whichList[zoneID] != onOpenList && newOpenListItemID < MAX_ZONES) 
            {	
               // Create a new open list item in the binary heap
               newOpenListItemID = newOpenListItemID + 1;   // Give each new item a unique id
               S32 m = numberOfOpenListItems + 1;
               openList[m] = newOpenListItemID;             // Place the new open list item (actually, its ID#) at the bottom of the heap
               openZone[newOpenListItemID] = zoneID;        // Record zone as a newly opened

               Hcost[openList[m]] = heuristic(zoneID, targetZone);
               Gcost[zoneID] = Gcost[parentZone] + zone.distTo;
               Fcost[openList[m]] = Gcost[zoneID] + Hcost[openList[m]];
               parentZones[zoneID] = parentZone; 

               // Move the new open list item to the proper place in the binary heap.
               // Starting at the bottom, successively compare to parent items,
               // swapping as needed until the item finds its place in the heap
               // or bubbles all the way to the top (if it has the lowest F cost).
               while(m > 1 && Fcost[openList[m]] <= Fcost[openList[m/2]]) 
               {
                  S16 temp = openList[m/2];
                  openList[m/2] = openList[m];
                  openList[m] = temp;
                  m = m/2;
               }

               // Finally, put zone on the open list
               whichList[zoneID] = onOpenList;
               numberOfOpenListItems++;
            }

            // If zone is already on the open list, check to see if this 
            //	path to that cell from the starting location is a better one. 
            //	If so, change the parent of the cell and its G and F costs.

            else // zone was already on the open list
            {
               // Figure out the G cost of this possible new path
               S32 tempGcost = (S32)(Gcost[parentZone] + zone.distTo);
         		
               // If this path is shorter (G cost is lower) then change
               // the parent cell, G cost and F cost. 		
               if (tempGcost < Gcost[zoneID])
               {
                  parentZones[zoneID] = parentZone; // Change the square's parent
                  Gcost[zoneID] = tempGcost;        // and its G cost			

                  // Because changing the G cost also changes the F cost, if
                  // the item is on the open list we need to change the item's
                  // recorded F cost and its position on the open list to make
                  // sure that we maintain a properly ordered open list.

                  for(S32 i = 1; i <= numberOfOpenListItems; i++) // Look for the item in the heap
                  {
                     if(openZone[openList[i]] == zoneID) 
                     {
	                     Fcost[openList[i]] = Gcost[zoneID] + Hcost[openList[i]]; // Change the F cost
            				
	                     // See if changing the F score bubbles the item up from it's current location in the heap
	                     S32 m = i;
	                     while(m > 1 && Fcost[openList[m]] < Fcost[openList[m/2]]) 
	                     {
		                     S16 temp = openList[m/2];
		                     openList[m/2] = openList[m];
		                     openList[m] = temp;
		                     m = m/2;
	                     }
	                
                        break; 
                     } 
                  }
               }

            } // else if whichList(zoneID) = onOpenList	
         } // for loop looping through neighboring zones list
	   }  

	   // If target is added to open list then path has been found.
	   if(whichList[targetZone] == onOpenList)
	   {
		   foundPath = true; 
         break;
	   }
	}

   // Save the path if it exists.
	if(foundPath)
	{
      // Working backwards from the target to the starting location by checking
      //	each cell's parent, figure out the length of the path.
      // Fortunately, we want our list to have the closest zone last (see getWaypoint),
      // so it all works out nicely.
      // We'll store both the zone center and the gateway to the neighboring zone.  This
      // will help keep the robot from getting hung up on blocked but technically visible
      // paths, such as when we are trying to fly around a protruding wall stub.

      path.push_back(target);                                     // First point is the actual target itself
      path.push_back(gBotNavMeshZones[targetZone]->getCenter());  // Second is the center of the target's zone
      
      S32 zone = targetZone;

	   while(zone != startZone)
	   {
		   path.push_back(findGateway(parentZones[zone], zone));   // don't switch findGateway arguments, some path is one way (teleporters).

		   zone = parentZones[zone];		// Find the parent of the current cell	

         path.push_back(gBotNavMeshZones[zone]->getCenter());
	   }
      path.push_back(gBotNavMeshZones[startZone]->getCenter());

	   return path;
	}

   // else there is no path to the selected target
   TNLAssert(path.size() == 0, "Expected empty path!");
	return path;
}


// Return a point representing gateway between zones
Point AStar::findGateway(S32 zone1, S32 zone2)
{
   S32 neighborIndex = gBotNavMeshZones[zone1]->getNeighborIndex(zone2);
   TNLAssert(neighborIndex >= 0, "Invalid neighbor index!!");

   return gBotNavMeshZones[zone1]->mNeighbors[neighborIndex].borderCenter;
}


};


