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

#include "UINameEntry.h"
#include "UIMenus.h"
#include "UIGame.h"
#include "UIChat.h"
#include "UIEditor.h"
#include "game.h"
#include "gameConnection.h"
#include "config.h"
#include "md5wrapper.h"

#include "SDL/SDL_opengl.h"

#include <string>
#include <math.h>

namespace Zap
{
using namespace std;


void TextEntryUserInterface::onActivate()
{
   if(resetOnActivate)
      lineEditor.clear();
}


void TextEntryUserInterface::render()
{
   glColor3f(1,1,1);

   const S32 fontSize = 20;
   const S32 fontSizeBig = 30;
   const S32 canvasHeight = gScreenInfo.getGameCanvasHeight();

   S32 y = (canvasHeight / 2) - fontSize;

   drawCenteredString(y, fontSize, title);
   y += 45;
   glColor3f(0, 1, 0);
   drawCenteredString(canvasHeight - vertMargin - 2 * fontSize - 5, fontSize, instr1);
   drawCenteredString(canvasHeight - vertMargin - fontSize, fontSize, instr2);

   glColor3f(1,1,1);

   // this will have an effect of shrinking the text to fit on-screen when text get very long
   S32 w = getStringWidthf(fontSizeBig, lineEditor.getDisplayString().c_str());
   if(w > 750)
      w = 750 * fontSizeBig / w;
   else
      w = fontSizeBig;

   S32 x = drawCenteredString(y, w, lineEditor.getDisplayString().c_str());
   lineEditor.drawCursor(x, y, fontSizeBig);
}


void TextEntryUserInterface::idle(U32 timeDelta)
{
   LineEditor::updateCursorBlink(timeDelta);
}


void TextEntryUserInterface::onKeyDown(KeyCode keyCode, char ascii)
{
   switch (keyCode)
   {
      case KEY_ENTER:
         onAccept(lineEditor.c_str());
         break;
      case KEY_BACKSPACE:
         lineEditor.backspacePressed();
         break;
      case KEY_DELETE:
         lineEditor.deletePressed();
         break;
      case KEY_ESCAPE:
         onEscape();
         break;
      default:
         lineEditor.addChar(ascii);
   }
}


void TextEntryUserInterface::setString(string str)
{
   lineEditor.setString(str);
}


////////////////////////////////////////
////////////////////////////////////////

extern IniSettings gIniSettings;

LevelNameEntryUserInterface gLevelNameEntryUserInterface;

void LevelNameEntryUserInterface::onEscape()
{
   UserInterface::playBoop();
   reactivatePrevUI();      //gMainMenuUserInterface
}


void LevelNameEntryUserInterface::onActivate()
{
   Parent::onActivate();
   mLevelIndex = 0;

   mLevels = LevelListLoader::buildLevelList();

   // Remove the extension from the level file
   for(S32 i = 0; i < mLevels.size(); i++)
       mLevels[i] = mLevels[i].substr(0, mLevels[i].find_last_of('.'));

   // See if our current level name is on the list -- if so, set mLevelIndex to that level
   for(S32 i = 0; i < mLevels.size(); i++)
      if(!stricmp(mLevels[i].c_str(), lineEditor.c_str()))
      {
         mLevelIndex = i;
         break;
      }
}


void LevelNameEntryUserInterface::onKeyDown(KeyCode keyCode, char ascii)
{
   if(keyCode == KEY_RIGHT)
   {
      if(mLevels.size() == 0)
         return;

      mLevelIndex++;
      if(mLevelIndex >= mLevels.size())
         mLevelIndex = 0;

      lineEditor.setString(mLevels[mLevelIndex]);
   }
   else if(keyCode == KEY_LEFT)
   {
      if(mLevels.size() == 0)
         return;

      mLevelIndex--;
      if(mLevelIndex < 0)
         mLevelIndex = mLevels.size() - 1;

      lineEditor.setString(mLevels[mLevelIndex]);
   }
   else
      Parent::onKeyDown(keyCode, ascii);
}


void LevelNameEntryUserInterface::onAccept(const char *name)
{
   gEditorUserInterface.setLevelFileName(name);
   UserInterface::playBoop();
   gEditorUserInterface.activate(false);
   
   // Get that baby into the INI file
   gIniSettings.lastEditorName = name;
   saveSettingsToINI();             
}


////////////////////////////////////////
////////////////////////////////////////

void PasswordEntryUserInterface::render()
{
   const S32 canvasWidth = gScreenInfo.getGameCanvasWidth();
   const S32 canvasHeight = gScreenInfo.getGameCanvasHeight();

   if(gClientGame->getConnectionToServer())
   {
      gClientGame->mGameUserInterface->render();
      glColor4f(0, 0, 0, 0.5);
      glEnableBlend;
         glBegin(GL_POLYGON);
            glVertex2f(0,           0);
            glVertex2f(canvasWidth, 0);
            glVertex2f(canvasWidth, canvasHeight);
            glVertex2f(0,           canvasHeight);
         glEnd();
      glDisableBlend;
   }

   Parent::render();
}


////////////////////////////////////////
////////////////////////////////////////

void PreGamePasswordEntryUserInterface::onAccept(const char *text)
{
   joinGame(connectAddress, false, false);      // Not from master, not local
}


void PreGamePasswordEntryUserInterface::onEscape()
{
   gMainMenuUserInterface.activate();
}

////////////////////////////////////////
////////////////////////////////////////

ServerPasswordEntryUserInterface gServerPasswordEntryUserInterface;

// Constructor
ServerPasswordEntryUserInterface::ServerPasswordEntryUserInterface()        
{
   setMenuID(PasswordEntryUI);
   title = "ENTER SERVER PASSWORD:";
   instr1 = "";
   instr2 = "Enter the password required for access to the server";
}


////////////////////////////////////////
////////////////////////////////////////

void InGamePasswordEntryUserInterface::onAccept(const char *text)
{
   GameConnection *gc = gClientGame->getConnectionToServer();
   if(gc)
   {
      submitPassword(gc, text);

      reactivatePrevUI();                                                  // Reactivating clears subtitle message, so reactivate first...
      gGameMenuUserInterface.mMenuSubTitle = "** checking password **";     // ...then set the message
   }
   else
      reactivatePrevUI();                                                  // Otherwise, just reactivate the previous menu
}

void InGamePasswordEntryUserInterface::onEscape()
{
   reactivatePrevUI();
}

////////////////////////////////////////
////////////////////////////////////////

AdminPasswordEntryUserInterface gAdminPasswordEntryUserInterface;

// Constructor
AdminPasswordEntryUserInterface::AdminPasswordEntryUserInterface()      
{
   setMenuID(AdminPasswordEntryUI);
   title = "ENTER ADMIN PASSWORD:";
   instr1 = "";
   instr2 = "Enter the admin password to perform admin tasks and change levels on this server";
}


void AdminPasswordEntryUserInterface::submitPassword(GameConnection *gameConnection, const char *text)
{
   gameConnection->submitAdminPassword(text);
}


////////////////////////////////////////
////////////////////////////////////////

LevelChangePasswordEntryUserInterface gLevelChangePasswordEntryUserInterface;

// Constructor
LevelChangePasswordEntryUserInterface::LevelChangePasswordEntryUserInterface()      
{
   setMenuID(LevelChangePasswordEntryUI);
   title = "ENTER LEVEL CHANGE PASSWORD:";
   instr1 = "";
   instr2 = "Enter the level change password to change levels on this server";
}


void LevelChangePasswordEntryUserInterface::submitPassword(GameConnection *gameConnection, const char *text)
{
   gameConnection->submitLevelChangePassword(text);
}


////////////////////////////////////////
////////////////////////////////////////
};


