//------------------------------------------------------------------------------
// Copyright Chris Eykamp
// See LICENSE.txt for full copyright information
//------------------------------------------------------------------------------

#include "config.h"

#include "BanList.h"
#include "Colors.h"
#include "GameSettings.h"
#include "IniFile.h"
#include "InputCode.h"
#include "QuickChatMessages.h"
#include "version.h"

#ifndef BF_NO_STATS
#  include "../master/database.h"
#endif

#ifndef ZAP_DEDICATED
#  include "quickChatHelper.h"
#  include "RenderUtils.h"
#endif

#include "physfs.hpp"
#include "stringUtils.h"         // For itos
#include "SystemFunctions.h"
#include "MathUtils.h"           // For MIN

#include "tnlLog.h"

#ifdef _MSC_VER
#  pragma warning (disable: 4996)     // Disable POSIX deprecation, certain security warnings that seem to be specific to VC++
#endif

#ifdef TNL_OS_WIN32
#  include <windows.h>   // For ARRAYSIZE when using ZAP_DEDICATED
#endif

#ifndef BF_NO_STATS
   using namespace DbWriter;
#endif


namespace Zap
{


// Constructor
UserSettings::UserSettings()
{
   for(S32 i = 0; i < LevelCount; i++)
      levelupItemsAlreadySeen[i] = false;
}


// Destructor
UserSettings::~UserSettings() { /* Do nothing */ }


////////////////////////////////////////
////////////////////////////////////////

// bitfighter.org would soon be the same as 199.192.229.168
// 01 Nov 2013: bitfighter.org ip address changed to 209.148.88.166
// 12 Aug 2018: bitfighter.org ip address changed to 172.245.93.119
// 07 Dec 2019: bitfighter.org ip address changed to 107.175.92.56
const char *MASTER_SERVER_LIST_ADDRESS = "bitfighter.org:25955,IP:107.175.92.56:25955,bitfighter.net:25955";



// Vol gets stored as a number from 0 to 10; normalize it to 0-1
static F32 checkVol(const F32 &vol) 
{ 
   F32 v = vol / 10.0f; 
   return CLAMP(v, 0, 1);
}  


static F32 writeVol(const F32 &vol) 
{ 
   return ceilf(vol * 10.0f);
}  


static U32 checkClientFps(const U32 &fps)
{
   // If FPS is not set, make sure it is default
   if(fps < 1)
      return 100;

   return fps;
}


// Constructor: Set default values here
IniSettings::IniSettings()
{

#  define SETTINGS_ITEM(typeName, enumVal, section, key, defaultVal, readValidator, writeValidator, comment)    \
            mSettings.add(                                                                                      \
               new Setting<typeName, IniKey::SettingsItem>(IniKey::enumVal, defaultVal, key,                    \
                                                           section, readValidator, writeValidator, comment)     \
            );
      SETTINGS_TABLE
#  undef SETTINGS_ITEM

   oldDisplayMode = DISPLAY_MODE_UNKNOWN;
}


// Destructor
IniSettings::~IniSettings()
{
   // Do nothing
}


// This list is currently incomplete, will grow as we move our settings into the new structure
#define SECTION_TABLE                                                                                                                                                        \
   SECTION_ITEM("Settings",        "Settings entries contain a number of different options.")                                                                                \
   SECTION_ITEM("Effects",         "Various visual effects.")                                                                                                                \
   SECTION_ITEM("Host",            "Items in this section control how Bitfighter works when you are hosting a game.  See also Host-Voting.")                                 \
   SECTION_ITEM("Host-Voting",     "Control how voting works on the server.  The default values work pretty well, but if you want to tweak them, go ahead!\n"                \
                                   "Yes and No votes, and abstentions, have different weights.  When a vote is conducted, the total value of all votes (or non-votes)\n"     \
                                   "is added up, and if the result is greater than 0, the vote passes.  Otherwise it fails.  You can adjust the weight of the votes below.") \
   SECTION_ITEM("EditorSettings",  "EditorSettings entries relate to items in the editor.")                                                                                  \
   SECTION_ITEM("Updater",         "The Updater section contains entries that control how game updates are handled.")                                                        \
   SECTION_ITEM("Diagnostics",     "Diagnostic entries can be used to enable or disable particular actions for debugging purposes.\n"                                        \
                                   "You probably can't use any of these settings to enhance your gameplay experience.")                                                      \
   SECTION_ITEM("Sounds",          "Sound settings")                                                                                                                        \
   SECTION_ITEM("Testing",         "Experimental and possibly short-lived settings use for testing.  They may be removed at any time,\n"                                     \
                                   "even in the next version of Bitfighter.")                                                                                                \


static const string sections[] =
{
#  define SECTION_ITEM(section, b) section,
      SECTION_TABLE
#  undef SECTION_ITEM
};

static const string headerComments[] = 
{
#  define SECTION_ITEM(a, comment) comment,
      SECTION_TABLE
#  undef SECTION_ITEM
};


// Some static helper methods:

// Set all bits in items[] to false
void IniSettings::clearbits(bool *bitArray, S32 itemCount)
{
   for(S32 i = 0; i < itemCount; i++)
      bitArray[i] = false;
}


// Produce a string of Ys and Ns based on values in bool items[], suitable for storing in the INI in a semi-readable manner
string IniSettings::bitArrayToIniString(const bool *bitArray, S32 itemCount)
{
   string s = "";

   for(S32 i = 0; i < itemCount; i++)
      s += bitArray[i] ? "Y" : "N";

   return s;
}


// Takes a string; we'll set the corresponding bool in items[] to true whenever we encounter a 'Y'
void IniSettings::iniStringToBitArray(const string &vals, bool *bitArray, S32 itemCount)
{
   clearbits(bitArray, itemCount);

   S32 count = MIN((S32)vals.size(), itemCount);

   for(S32 i = 0; i < count; i++)
      if(vals.at(i) == 'Y')
         bitArray[i] = true;
}


Vector<PluginBinding> IniSettings::getDefaultPluginBindings() const
{
   Vector<PluginBinding> bindings;

   static Vector<string> plugins;
   plugins.push_back("Ctrl+;|draw_arcs.lua|Make curves!");
   plugins.push_back("Ctrl+'|draw_stars.lua|Create polygon/star");

   Vector<string> words;

   // Parse the retrieved strings.  They'll be in the form "Key Script Help"
   for(S32 i = 0; i < plugins.size(); i++)
   {
      parseString(trim(plugins[i]), words, '|');

      PluginBinding binding;
      binding.key = words[0];
      binding.script = words[1];
      binding.help = concatenate(words, 2);

      bindings.push_back(binding);
   }

   return bindings;
}


extern string lcase(string strToConvert);


static void loadForeignServerInfo(CIniFile *ini, IniSettings *iniSettings)
{
   // AlwaysPingList will default to broadcast, can modify the list in the INI
   // http://learn-networking.com/network-design/how-a-broadcast-address-works
   iniSettings->alwaysPingList.clear();
   parseString(ini->getValue("Connections", "AlwaysPingList", "IP:Broadcast:28000"), iniSettings->alwaysPingList, ',');

   // These are the servers we found last time we were able to contact the master.
   // In case the master server fails, we can use this list to try to find some game servers. 
   //parseString(ini->GetValue("ForeignServers", "ForeignServerList"), prevServerListFromMaster, ',');
   iniSettings->prevServerListFromMaster.clear();
   ini->getAllValues("RecentForeignServers", iniSettings->prevServerListFromMaster);
}


// Use macro to make code more readable
#define addComment(comment) ini->sectionComment(section, comment);


static void writeLoadoutPresets(CIniFile *ini, GameSettings *settings)
{
   const char *section = "LoadoutPresets";

   ini->addSection(section);      // Create the key, then provide some comments for documentation purposes

   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" Loadout presets are stored here.  You can manage these manually if you like, but it is usually easier");
      addComment(" to let the game do it for you.  Pressing Ctrl-1 will copy your current loadout into the first preset, etc.");
      addComment(" If you do choose to modify these, it is important to note that the modules come first, then the weapons.");
      addComment(" The order is the same as you would enter them when defining a loadout in-game.");
      addComment("----------------");
   }

   for(S32 i = 0; i < GameSettings::LoadoutPresetCount; i++)
   {
      string presetStr = settings->getLoadoutPreset(i).toString(true);

      if(presetStr != "")
         ini->setValue(section, "Preset" + itos(i + 1), presetStr);
   }
}


static void writePluginBindings(CIniFile *ini, IniSettings *iniSettings)
{
   const char *section = "EditorPlugins";

   ini->addSection(section);             

   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" Editor plugins are lua scripts that can add extra functionality to the editor.  You can specify");
      addComment(" here using the following format:");
      addComment(" Plugin1=Key1|ScriptName.lua|Script help string");
      addComment(" ... etc ...");
      addComment(" The names of the presets are not important, and can be changed. Key combos follow the general form of");
      addComment(" Ctrl+Alt+Shift+Meta+Super+key (omit unneeded modifiers, you can get correct Input Strings from the");
      addComment(" diagnostics screen).  Scripts should be stored in the plugins folder in the install directory. Please")
      addComment(" see the Bitfighter wiki for details.");
      addComment(" ");
      addComment(" Note: these key bindings use KeyStrings.  See info at the top of this file for an explanation.");
      addComment("----------------");
   }

   Vector<string> plugins;
   PluginBinding binding;
   for(S32 i = 0; i < iniSettings->pluginBindings.size(); i++)
   {
      binding = iniSettings->pluginBindings[i];
      plugins.push_back(string(binding.key + "|" + binding.script + "|" + binding.help));
   }

   ini->setAllValues(section, "Plugin", plugins);
}


static void writeConnectionsInfo(CIniFile *ini, IniSettings *iniSettings)
{
   const char *section = "Connections";
   
   ini->addSection(section);

   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" AlwaysPingList - Always try to contact these servers (comma separated list); Format: IP:IPAddress:Port");
      addComment("                  Include 'IP:Broadcast:28000' to search LAN for local servers on default port");
      addComment("----------------");
   }

   // Creates comma delimited list
   ini->setValue(section, "AlwaysPingList", listToString(iniSettings->alwaysPingList, ","));
}


static void writeForeignServerInfo(CIniFile *ini, IniSettings *iniSettings)
{
   const char *section = "RecentForeignServers";

   ini->addSection(section);

   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" This section contains a list of the most recent servers seen; used as a fallback if we can't reach the master");
      addComment(" Please be aware that this section will be automatically regenerated, and any changes you make will be overwritten");
      addComment("----------------");
   }

   ini->setAllValues(section, "Server", iniSettings->prevServerListFromMaster);
}


// Read levels, if there are any...
 void loadLevels(CIniFile *ini, IniSettings *iniSettings)
{
   if(ini->findSection("Levels") != ini->noID)
   {
      S32 numLevels = ini->getNumEntries("Levels");
      Vector<string> levelValNames;

      for(S32 i = 0; i < numLevels; i++)
         levelValNames.push_back(ini->valueName("Levels", i));

      levelValNames.sort(alphaSort);

      string level;
      for(S32 i = 0; i < numLevels; i++)
      {
         level = ini->getValue("Levels", levelValNames[i], "");
         if (level != "")
            iniSettings->levelList.push_back(StringTableEntry(level.c_str()));
      }
   }
}


// Read level deleteList, if there are any.  This could probably be made more efficient by not reading the
// valnames in first, but what the heck...
void loadLevelSkipList(CIniFile *ini, GameSettings *settings)
{
   settings->getLevelSkipList()->clear();
   ini->getAllValues("LevelSkipList", *settings->getLevelSkipList());
}


typedef Vector<AbstractSetting<IniKey::SettingsItem> *> SettingsList;

static void loadSettings(CIniFile *ini, IniSettings *iniSettings, const string &section)
{
   // Get all settings from the given section
   SettingsList settings = iniSettings->mSettings.getSettingsInSection(section);

   // Load the INI settings into the settings list, overwriting the defaults
   for(S32 i = 0; i < settings.size(); i++)
      settings[i]->setValFromString(ini->getValue(section, settings[i]->getKey(), settings[i]->getDefaultValueString()));
}


static void loadGeneralSettings(CIniFile *ini, IniSettings *iniSettings)
{
   string section = "Settings";

   // Now read the settings still defined all old school

#ifdef TNL_OS_MOBILE
   // Mobile usually have a single, fullscreen mode
   iniSettings->mSettings.setVal("WindowMode", DISPLAY_MODE_FULL_SCREEN_STRETCHED);
#endif

   iniSettings->oldDisplayMode = iniSettings->mSettings.getVal<DisplayMode>(IniKey::WindowMode);


#ifndef ZAP_DEDICATED
   RenderUtils::setDefaultLineWidth(ini->getValueF(section, "LineWidth", 2));
#endif
}


static void loadLoadoutPresets(CIniFile *ini, GameSettings *settings)
{
   Vector<string> rawPresets(GameSettings::LoadoutPresetCount);

   for(S32 i = 0; i < GameSettings::LoadoutPresetCount; i++)
      rawPresets.push_back(ini->getValue("LoadoutPresets", "Preset" + itos(i + 1), ""));
   
   for(S32 i = 0; i < GameSettings::LoadoutPresetCount; i++)
   {
      LoadoutTracker loadout(rawPresets[i]);
      if(loadout.isValid())
         settings->setLoadoutPreset(&loadout, i);
   }
}


static void loadPluginBindings(CIniFile *ini, IniSettings *iniSettings)
{
   Vector<string> values;
   Vector<string> words;      // Reusable container

   ini->getAllValues("EditorPlugins", values);

   // Parse the retrieved strings.  They'll be in the form "Key Script Help"
   for(S32 i = 0; i < values.size(); i++)
   {
      parseString(trim(values[i]), words, '|');

      if(words.size() < 3)
      {
         logprintf(LogConsumer::LogError, "Error parsing EditorPlugin defnition in INI: too few values (read: %s)", values[i].c_str());
         continue;
      }
      
      PluginBinding binding;
      binding.key = words[0];
      binding.script = words[1];
      binding.help = concatenate(words, 2);

      iniSettings->pluginBindings.push_back(binding);
   }

   // If no plugins we're loaded, add our defaults  (maybe we don't want to do this?)
   if(iniSettings->pluginBindings.size() == 0)
      iniSettings->pluginBindings = iniSettings->getDefaultPluginBindings();
}


// These instructions are written before an any sections containing keyCodes or keyStrings
static void writeGeneralKeybindingInstructions(CIniFile *ini)
{
   if(ini->NumHeaderComments() > 0)
      return;

   ini->headerComment("----------------");
   ini->headerComment(" Key bindings come in two flavors: KeyCodes and KeyStrings.  In-game bindings are done with KeyCodes, whereas editor and");
   ini->headerComment(" special keys (i.e. those that are avaialAble everywhere, like Help or Lobby Chat) are defined with KeyStrings.  With a");
   ini->headerComment(" few exceptions, KeyCodes do not contain modifier keys (Ctrl, Alt, Shift, etc.), which are generally less useful in-game.");
   ini->headerComment(" This also allows these keys to function independently of whether a modifier key is pressed.  KeyStrings, on the other");
   ini->headerComment(" hand, can specify any combination of modifiers, and can differentiate between Ctrl+L and Ctrl+Shift+L (for example).");
   ini->headerComment(" ");
   ini->headerComment(" List of available KeyCodes:");

   Vector<string> lines = InputCodeManager::getValidKeyCodes(115);      // width of 115 looks nice

   for(S32 i = 0; i < lines.size(); i++)
      ini->headerComment("    " + lines[i]);

   ini->headerComment(" ");
   ini->headerComment(" KeyStrings are composed of zero or more modifiers keys, followed by a base key.");

   string modifiers = InputCodeManager::getValidModifiers();
   pair<string,string> goodExampleBadExample = InputCodeManager::getExamplesOfModifiedKeys();

   ini->headerComment("     Valid modifiers: " + modifiers);
   ini->headerComment("     Multiple modifiers can be used, but they MUST appear in the order listed above.  For example: ");
   ini->headerComment("     " + goodExampleBadExample.first + " is valid, but " + goodExampleBadExample.second + " will not work.");
   ini->headerComment("     The base key can be almost any keyboard key (but not modifiers by themselves).  There is no definitive list; They are");
   ini->headerComment("     somewhat system dependent; you may need to experiment a bit.");
   ini->headerComment("----------------");
}


// These instructions are written before an INI section containing keyStrings
static void writeKeyStringInstructions(CIniFile *ini, const string &section)
{
   if(ini->numSectionComments(section) > 0)
      return;

   addComment("----------------");
   addComment(" These key bindings use KeyStrings.  See info at the top of this file for an explanation.");
   addComment("----------------"); 
}


// These instructions are written before an INI section containing keyCodes
static void writeKeyCodeInstructions(CIniFile *ini, const string &section)
{
   if(ini->numSectionComments(section) > 0)
      return;

   addComment("----------------");
   addComment(" These key bindings use KeyCodes.  See info at the top of this file for an explanation.");
   addComment("----------------");
}


static InputCode getInputCode(CIniFile *ini, const string &section, const string &key, InputCode defaultValue)
{
   const char *code = InputCodeManager::inputCodeToString(defaultValue);
   return InputCodeManager::stringToInputCode(ini->getValue(section, key, code).c_str());
}


// Returns a string like "Ctrl+L"
static string getInputString(CIniFile *ini, const string &section, const string &key, const string &defaultValue)
{
   string inputStringFromIni = ini->getValue(section, key, defaultValue);
   string normalizedInputString = InputCodeManager::normalizeInputString(inputStringFromIni);

   // Check if inputString is valid -- we could get passed any ol' garbage that got put in the INI file
   if(InputCodeManager::isValidInputString(normalizedInputString))
   {
      // If normalized binding is different than what is in the INI file, replace the INI version with the good version
      if(normalizedInputString != inputStringFromIni)
         ini->setValue(section, key, normalizedInputString);

      return normalizedInputString;
   }

   // We don't understand what is in the INI file... print a warning, and fall back to the default
   logprintf(LogConsumer::ConfigurationError, "Invalid key binding in INI section [%s]: %s=%s", 
             section.c_str(), key.c_str(), inputStringFromIni.c_str());
   return defaultValue;
}


static void setGameBindings(CIniFile *ini, 
                            InputCodeManager *inputCodeManager,
                            BindingNameEnum bindingName, 
                            InputCode defaultKeyboardBinding,
                            InputCode defaultJoystickBinding)
{
   inputCodeManager->setBinding(bindingName, 
                                InputModeKeyboard,                            	
                                getInputCode(ini, 
                                             "KeyboardKeyBindings", 
                                             InputCodeManager::getBindingName(bindingName), 
                                             defaultKeyboardBinding));                                            
                                                                                          
   inputCodeManager->setBinding(bindingName, 
                                InputModeJoystick,                               
                                getInputCode(ini, 
                                             "JoystickKeyBindings", 
                                             InputCodeManager::getBindingName(bindingName), 
                                             defaultJoystickBinding));
}


// Remember: If you change any of the defaults, you'll need to rebuild your INI file to see the results!
static void loadDefaultKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager)
{                                
// Generate a block of code that calls above function for every entry in BINDING_TABLE
#define BINDING(enumVal, b, c, defaultKeyboardBinding, defaultJoystickBinding) \
   setGameBindings(ini, inputCodeManager, enumVal, defaultKeyboardBinding, defaultJoystickBinding);

   BINDING_TABLE
#undef BINDING
}


// Note that this function, similar to setGameBindings above, uses strings instead of inputCodes to allow more complex
// key chords like Ctrl+P that are generally impractical to use in-game.
static void setSpecialBindings(CIniFile *ini, 
                               InputCodeManager *inputCodeManager,
                               SpecialBindingNameEnum bindingName, 
                               const string &defaultKeyboardBinding,
                               const string &defaultJoystickBinding)
{
   inputCodeManager->setSpecialBinding(bindingName, 
                                       InputModeKeyboard,                                      
                                       getInputString(ini, "SpecialKeyBindings",                               
                                                      InputCodeManager::getSpecialBindingName(bindingName), 
                                                      defaultKeyboardBinding));                         
                                                                                                                  
   inputCodeManager->setSpecialBinding(bindingName, 
                                       InputModeJoystick,                                      
                                       getInputString(ini, "SpecialJoystickBindings",                          
                                                      InputCodeManager::getSpecialBindingName(bindingName), 
                                                      defaultJoystickBinding));
}


// Only called while loading keys from the INI
void loadDefaultSpecialKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager)
{
// Generate a block of code that calls above function for every entry in SPECIAL_BINDING_TABLE
#define SPECIAL_BINDING(specialEnumVal, b, c, defaultSpecialKeyboardBinding, defaultJoystickBinding)  \
   setSpecialBindings(ini, inputCodeManager, specialEnumVal, defaultSpecialKeyboardBinding, defaultSpecialKeyboardBinding);

   SPECIAL_BINDING_TABLE
#undef SPECIAL_BINDING

}


static const string EditorKeyboardKeyBindingSectionName = "EditorKeyboardKeyBindings";


// Only called while loading keys from the INI; Note that this function might not be able to be modernized!
void loadDefaultEditorKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager)
{
   string key;

#define EDITOR_BINDING(editorEnumVal, b, c, defaultEditorKeyboardBinding)                                                     \
      key = InputCodeManager::getEditorBindingName(editorEnumVal);                                                            \
      inputCodeManager->setEditorBinding(editorEnumVal,                                                                       \
                                         getInputString(ini, EditorKeyboardKeyBindingSectionName, key, defaultEditorKeyboardBinding)); 
    EDITOR_BINDING_TABLE
#undef EDITOR_BINDING

   // Now the same thing for the editor key codes
#define EDITOR_BINDING(editorEnumVal, b, c, defaultEditorKeyboardBinding)                                                     \
      key = InputCodeManager::getEditorBindingName(editorEnumVal);                                                            \
      inputCodeManager->setEditorBinding(editorEnumVal,                                                                       \
                                         getInputCode(ini, EditorKeyboardKeyBindingSectionName, key, defaultEditorKeyboardBinding)); 
      EDITOR_BINDING_KEYCODE_TABLE
#undef EDITOR_BINDING

}


static void writeKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager, const string &section, InputMode mode)
{
   writeKeyCodeInstructions(ini, section);

   // Evaluates to:
   // ini->SetValue(section, InputCodeManager::getBindingName(InputCodeManager::BINDING_SELWEAP1),
   //               InputCodeManager::inputCodeToString(inputCodeManager->getBinding(InputCodeManager::BINDING_SELWEAP1, mode)));

#define BINDING(enumVal, b, c, d, e)                                                                            \
      ini->setValue(section, InputCodeManager::getBindingName(enumVal),                                         \
                              InputCodeManager::inputCodeToString(inputCodeManager->getBinding(enumVal, mode))); 
    BINDING_TABLE
#undef BINDING
}


// Note that this function might not be able to be modernized!
static void writeEditorKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager, const string &section)
{
   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" These key bindings use KeyStrings, except for DisableGridSnappingModifier and EnableConstrainedMovementModifier,");
      addComment(" which use KeyCodes.  See info at the top of this file for an explanation.");
      addComment("----------------");
   }

   string key;

   // Expands to:
   // key = InputCodeManager::getEditorBindingName(FlipItemHorizontal); 
   // if(!ini->hasKey(section, key))
   //   ini->SetValue(section, key, inputCodeManager->getEditorBinding(editorEnumVal));

   // Don't overwrite existing bindings for now... there is no way to modify them in-game, and if the user has
   // specified an invalid binding, leaving it wrong will make it easier for them to find and fix the error
#define EDITOR_BINDING(editorEnumVal, b, c, d)                                               \
   key = InputCodeManager::getEditorBindingName(editorEnumVal);                              \
   if(!ini->hasKey(section, key))                                                            \
      ini->setValue(section, key, inputCodeManager->getEditorBinding(editorEnumVal));
    EDITOR_BINDING_TABLE
#undef EDITOR_BINDING

// Now the same thing for the editor key codes
#define EDITOR_BINDING(editorEnumVal, b, c, d)                                               \
   key = InputCodeManager::getEditorBindingName(editorEnumVal);                              \
   if(!ini->hasKey(section, key))                                                            \
      ini->setValue(section, key, InputCodeManager::inputCodeToString(inputCodeManager->getEditorBinding(editorEnumVal)));
   EDITOR_BINDING_KEYCODE_TABLE
#undef EDITOR_BINDING


}


static void writeSpecialKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager, const string &section, InputMode mode)
{
   writeKeyStringInstructions(ini, section);

   string key;

   // Expands to:
   // key = InputCodeManager::getSpecialBindingName(FlipItemHorizontal); 
   // if(!ini->hasKey(section, key))
   //   ini->SetValue(section, key, inputCodeManager->getSpecialBinding(specialEnumVal));

   // Don't overwrite existing bindings for now... there is no way to modify them in-game, and if the user has
   // specified an invalid binding, leaving it wrong will make it easier for them to find and fix the error
#define SPECIAL_BINDING(specialEnumVal, b, c, d, e)                                         \
      key = InputCodeManager::getSpecialBindingName(specialEnumVal);                        \
      if(!ini->hasKey(section, key))                                                        \
         ini->setValue(section, key, inputCodeManager->getSpecialBinding(specialEnumVal, mode));
    SPECIAL_BINDING_TABLE
#undef SPECIAL_BINDING
}


static void writeKeyBindings(CIniFile *ini, InputCodeManager *inputCodeManager)
{
   writeGeneralKeybindingInstructions(ini);     // These codes get appeneded to the INI header comments

   writeKeyBindings       (ini, inputCodeManager, "KeyboardKeyBindings",     InputModeKeyboard);
   writeKeyBindings       (ini, inputCodeManager, "JoystickKeyBindings",     InputModeJoystick);
   writeEditorKeyBindings (ini, inputCodeManager, EditorKeyboardKeyBindingSectionName);
   writeSpecialKeyBindings(ini, inputCodeManager, "SpecialKeyBindings",      InputModeKeyboard);
   writeSpecialKeyBindings(ini, inputCodeManager, "SpecialJoystickBindings", InputModeJoystick);
}


static void insertQuickChatMessageCommonBits(CIniFile *ini, 
                                             const string &key, 
                                             MessageType messageType, 
                                             InputCode keyCode, 
                                             InputCode buttonCode, 
                                             const string &caption)
{
   ini->setValue(key, "Key", InputCodeManager::inputCodeToString(keyCode));
   ini->setValue(key, "Button", InputCodeManager::inputCodeToString(buttonCode));
   ini->setValue(key, "MessageType", Evaluator::toString(messageType));
   ini->setValue(key, "Caption", caption);
}


static void insertQuickChatMessageSection(CIniFile *ini, 
                                          S32 group, 
                                          MessageType messageType, 
                                          InputCode keyCode, 
                                          InputCode buttonCode, 
                                          const string &caption)
{
   const string key = "QuickChatMessagesGroup" + itos(group);

   insertQuickChatMessageCommonBits(ini, key, messageType, keyCode, buttonCode, caption);
}


static void insertQuickChatMessage(CIniFile *ini, 
                                   S32 group, 
                                   S32 messageId, 
                                   MessageType messageType, 
                                   InputCode keyCode, 
                                   InputCode buttonCode, 
                                   const string &caption, 
                                   const string &message)
{
   const string key = "QuickChatMessagesGroup" + itos(group) + "_Message" + itos(messageId);

   insertQuickChatMessageCommonBits(ini, key, messageType, keyCode, buttonCode, caption);
   ini->setValue(key, "Message", message);
}


static void writeDefaultQuickChatMessages(CIniFile *ini)
{
#  define QUICK_CHAT_SECTION(group, messageType, keyCode, buttonCode, caption)                              \
      insertQuickChatMessageSection(ini, group, messageType, keyCode, buttonCode, caption);

#  define QUICK_CHAT_MESSAGE(group, messageId, messageType, keyCode, buttonCode, cation, message)           \
      insertQuickChatMessage(ini, group, messageId, messageType, keyCode, buttonCode, cation, message);

      DEFAULT_QUICK_CHAT_MESSAGE_TABLE

#  undef QUICK_CHAT_MESSAGE
#  undef QUICK_CHAT_SECTION

}


// This is only used when no messages are specified in the INI
static void defineDefaultQuickChatMessages()
{
 #  define QUICK_CHAT_SECTION(group, messageType, keyCode, buttonCode, caption)                             \
      QuickChatHelper::nodeTree.push_back(QuickChatNode(1, messageType, keyCode, buttonCode, caption));

#  define QUICK_CHAT_MESSAGE(group, messageId, messageType, keyCode, buttonCode, caption, message)          \
      QuickChatHelper::nodeTree.push_back(QuickChatNode(2, messageType, keyCode, buttonCode, caption, message));

      DEFAULT_QUICK_CHAT_MESSAGE_TABLE

#  undef QUICK_CHAT_MESSAGE
#  undef QUICK_CHAT_SECTION  
}



/*  INI file looks a little like this:
   [QuickChatMessagesGroup1]
   Key=F
   Button=1
   Caption=Flag

   [QuickChatMessagesGroup1_Message1]
   Key=G
   Button=Button 1
   Caption=Flag Gone!
   Message=Our flag is not in the base!
   MessageType=Team     -or-     MessageType=Global

   == or, a top-tiered message might look like this ==

   [QuickChat_Message1]
   Key=A
   Button=Button 1
   Caption=Hello
   MessageType=Hello there!
*/
static void loadQuickChatMessages(CIniFile *ini)
{
#ifndef ZAP_DEDICATED
   // Add initial node
   QuickChatNode emptyNode;

   QuickChatHelper::nodeTree.push_back(emptyNode);

   S32 keys = ini->getNumSections();
   Vector<string> groups;

   // Read any top-level messages (those starting with "QuickChat_Message")
   Vector<string> messages;
   for(S32 i = 0; i < keys; i++)
   {
      string keyName = ini->getSectionName(i);
      if(keyName.substr(0, 17) == "QuickChat_Message")   // Found message group
         messages.push_back(keyName);
   }

   messages.sort(alphaSort);

   for(S32 i = messages.size() - 1; i >= 0; i--)
      QuickChatHelper::nodeTree.push_back(QuickChatNode(1, ini, messages[i], false));

   // Now search for groups, which have keys matching "QuickChatMessagesGroup123"
   for(S32 i = 0; i < keys; i++)
   {
      string keyName = ini->getSectionName(i);
      if(keyName.substr(0, 22) == "QuickChatMessagesGroup" && keyName.find("_") == string::npos)   // Found message group
         groups.push_back(keyName);
   }

   groups.sort(alphaSort);

   // If no messages were found, insert default messages
   if(messages.size() == 0 && groups.size() == 0)
      defineDefaultQuickChatMessages();

   else
   {
      // Find all the individual message definitions for each key -- match "QuickChatMessagesGroup123_Message456"

      for(S32 i = 0; i < groups.size(); i++)
      {
         messages.clear();

         for(S32 j = 0; j < keys; j++)
         {
            string keyName = ini->getSectionName(j);
            if(keyName.substr(0, groups[i].length() + 1) == groups[i] + "_")
               messages.push_back(keyName);
         }

         messages.sort(alphaSort);

         QuickChatHelper::nodeTree.push_back(QuickChatNode(1, ini, groups[i], true));

         for(S32 j = 0; j < messages.size(); j++)
            QuickChatHelper::nodeTree.push_back(QuickChatNode(2, ini, messages[j], false));
      }
   }

   // Add final node.  Last verse, same as the first.
   QuickChatHelper::nodeTree.push_back(emptyNode);
#endif
}


static void writeQuickChatMessages(CIniFile *ini, IniSettings *iniSettings)
{
   const char *section = "QuickChatMessages";

   ini->addSection(section);
   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" WARNING!  Do not edit this section while Bitfighter is running... your changes will be clobbered!");
      addComment("----------------");
      addComment(" The structure of the QuickChatMessages sections is a bit complicated.  The structure reflects the");
      addComment(" way the messages are displayed in the QuickChat menu, so make sure you are familiar with that before");
      addComment(" you start modifying these items. ");
      addComment(" ");
      addComment(" Messages are grouped, and each group has a Caption (short name");
      addComment(" shown on screen), a Key (the shortcut key used to select the group), and a Button (a shortcut button");
      addComment(" used when in joystick mode).  If the Button is \"Undefined key\", then that item will not be shown");
      addComment(" in joystick mode, unless the setting is true.  Groups can be defined in");
      addComment(" any order, but will be displayed sorted by [section] name.  Groups are designated by the");
      addComment(" [QuickChatMessagesGroupXXX] sections, where XXX is a unique suffix, usually a number.");
      addComment(" ");
      addComment(" Each group can have one or more messages, as specified by the [QuickChatMessagesGroupXXX_MessageYYY]");
      addComment(" sections, where XXX is the unique group suffix, and YYY is a unique message suffix.  Again, messages");
      addComment(" can be defined in any order, and will appear sorted by their [section] name.  Key, Button, and");
      addComment(" Caption serve the same purposes as in the group definitions. Message is the actual message text that");
      addComment(" is sent, and MessageType should be either \"Team\" or \"Global\", depending on which users the");
      addComment(" message should be sent to.  You can mix Team and Global messages in the same section, but it may be");
      addComment(" less confusing not to do so.  MessageType can also be \"Command\", in which case the message will be");
      addComment(" sent to the server, as if it were a /command; see below for more details.");
      addComment(" ");
      addComment(" Messages can also be added to the top-tier of items, by specifying a section like [QuickChat_MessageZZZ].");
      addComment(" ");
      addComment(" Note that quotes are not required around Messages or Captions, and if included, they will be sent as");
      addComment(" part of the message. Also, if you bullocks things up too badly, simply delete all QuickChatMessage");
      addComment(" sections, along with this section and all comments, and a clean set of commands will be regenerated"); 
      addComment(" the next time you run the game (though your modifications will be lost, obviously).");
      addComment(" ");
      addComment(" Note that you can also use the QuickChat functionality to create shortcuts to commonly run /commands");
      addComment(" by setting the MessageType to \"Command\".  For example, if you define a QuickChat message to be");
      addComment(" \"addbots 2\" (without quotes, and without a leading \"/\"), and the MessageType to \"Command\" (also");
      addComment(" without quotes), 2 robots will be added to the game when you select the appropriate message.  You can");
      addComment(" use this functionality to assign commonly used commands to joystick buttons or short key sequences.");
      addComment(" ");
      addComment(" Bindings specified here use KeyCodes.  See info at the top of this file for an explanation.");
      addComment("----------------");
   }


   // Are there any QuickChatMessageGroups?  If not, we'll write the defaults.
   S32 keys = ini->getNumSections();

   for(S32 i = 0; i < keys; i++)
   {
      string keyName = ini->getSectionName(i);
      if((keyName.substr(0, 22) == "QuickChatMessagesGroup" && keyName.find("_") == string::npos) ||
         (keyName.substr(0, 17) == "QuickChat_Message"))
         return;
   }

   writeDefaultQuickChatMessages(ini);
}


static void loadServerBanList(CIniFile *ini, BanList *banList)
{
   Vector<string> banItemList;
   ini->getAllValues("ServerBanList", banItemList);
   banList->loadBanList(banItemList);
}


// Can't be static -- called externally!
void writeServerBanList(CIniFile *ini, BanList *banList)
{
   // Refresh the server ban list
   const char *section = "ServerBanList";
   ini->deleteSection(section);
   ini->addSection(section);

   string delim = banList->getDelimiter();
   string wildcard = banList->getWildcard();


   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" This section contains a list of bans that this dedicated server has enacted");
      addComment(" ");
      addComment(" Bans are in the following format:");
      addComment("   IP Address " + delim + " nickname " + delim + " Start time (ISO time format) " + delim + " Duration in minutes ");
      addComment(" ");
      addComment(" Examples:");
      addComment("   BanItem0=123.123.123.123" + delim + "watusimoto" + delim + "20110131T123000" + delim + "30");
      addComment("   BanItem1=" + wildcard + delim + "watusimoto" + delim + "20110131T123000" + delim + "120");
      addComment("   BanItem2=123.123.123.123" + delim + wildcard + delim + "20110131T123000" + delim + "30");
      addComment(" ");
      addComment(" Note: Wildcards (" + wildcard +") may be used for IP address and nickname" );
      addComment(" ");
      addComment(" Note: ISO time format is in the following format: YYYYMMDDTHH24MISS");
      addComment("   YYYY = four digit year, (e.g. 2011)");
      addComment("     MM = month (01 - 12), (e.g. 01)");
      addComment("     DD = day of the month, (e.g. 31)");
      addComment("      T = Just a one character divider between date and time, (will always be T)");
      addComment("   HH24 = hour of the day (0-23), (e.g. 12)");
      addComment("     MI = minute of the hour, (e.g. 30)");
      addComment("     SS = seconds of the minute, (e.g. 00) (we don't really care about these... yet)");
      addComment("----------------");
   }

   ini->setAllValues(section, "BanItem", banList->banListToString());
}


// This is only called once, during initial initialization
// Is also called from gameType::processServerCommand (why?)
void loadSettingsFromINI(CIniFile *ini, GameSettings *settings)
{
   InputCodeManager *inputCodeManager = settings->getInputCodeManager();
   IniSettings *iniSettings = settings->getIniSettings();

   ini->readFile();                             // Read the INI file

   // New school
   //
   // Load settings from the INI for each section
   // This will eventually replace all of the load...() methods below
   for(U32 i = 0; i < ARRAYSIZE(sections); i++)
      loadSettings(ini, iniSettings, sections[i]);

   // This section can be modernized, the remainder maybe not
   loadGeneralSettings(ini, iniSettings);

   // The following sections are all oddballs for one reason or another, and probably cannot be parsed
   // using a standard settings process
   loadLoadoutPresets(ini, settings);
   loadPluginBindings(ini, iniSettings);

   loadDefaultKeyBindings(ini, inputCodeManager);
   loadDefaultEditorKeyBindings(ini, inputCodeManager);
   loadDefaultSpecialKeyBindings(ini, inputCodeManager);

   loadForeignServerInfo(ini, iniSettings);     // Info about other servers
   loadLevels(ini, iniSettings);                // Read levels, if there are any
   loadLevelSkipList(ini, settings);            // Read level skipList, if there are any

   loadQuickChatMessages(ini);
   loadServerBanList(ini, settings->getBanList());

   saveSettingsToINI(ini, settings);            // Save to fill in any missing settings

   settings->onFinishedLoading();               // Merge INI settings with cmd line settings
}


void IniSettings::loadUserSettingsFromINI(CIniFile *ini, GameSettings *settings)
{
   UserSettings userSettings;

   // Get a list of sections... we should have one per user
   S32 sections = ini->getNumSections();

   for(S32 i = 0; i < sections; i++)
   {
      userSettings.name = ini->getSectionName(i);

      string seenList = ini->getValue(userSettings.name, "LevelupItemsAlreadySeenList", "");
      IniSettings::iniStringToBitArray(seenList, userSettings.levelupItemsAlreadySeen, userSettings.LevelCount);

      settings->addUserSettings(userSettings);
   }
}


static void writeComments(CIniFile *ini, const string &section, const Vector<string> &comments)
{
   for(S32 i = 0; i < comments.size(); i++)
      ini->sectionComment(section, " " + comments[i]);
}


static void writeSettings(CIniFile *ini, IniSettings *iniSettings)
{
   TNLAssert(ARRAYSIZE(sections) == ARRAYSIZE(headerComments), "Mismatch!");

   for(U32 i = 0; i < ARRAYSIZE(sections); i++)
   {
      ini->addSection(sections[i]);

      const string section = sections[i];

      SettingsList settings = iniSettings->mSettings.getSettingsInSection(section);
   
      if(ini->numSectionComments(section) == 0)  
      {
         //ini->deleteSectionComments(section);      // Delete when done testing (harmless but useless)

         ini->sectionComment(section, "----------------");      // ----------------
         writeComments(ini, section, wrapString(headerComments[i], NO_AUTO_WRAP));

         ini->sectionComment(section, "----------------");      // ----------------

         // Write all our section comments for items defined in the new manner
         for(S32 j = 0; j < settings.size(); j++)
         {
            // Pass NO_AUTO_WRAP as width to disable automatic wrapping... we'll rely on \ns to do our wrapping here
            const string prefix = settings[j]->getKey() + " - ";
            writeComments(ini, section, wrapString(prefix + settings[j]->getComment(), NO_AUTO_WRAP, string(prefix.size(), ' ')));
         }

         // Special case 
#ifndef ZAP_DEDICATED
         if(section == "Settings")
            addComment(" LineWidth - Width of a \"standard line\" in pixels (default 2); can set with /linewidth in game");
#endif
         ini->sectionComment(section, "----------------");      // ----------------
      }

      // Write the settings themselves
      for(S32 j = 0; j < settings.size(); j++)
         ini->setValue(section, settings[j]->getKey(), settings[j]->getIniString());
   }


   // And the ones still to be ported to the new system

#ifndef ZAP_DEDICATED
   // Don't save new value if out of range, so it will go back to the old value. 
   // Just in case a user screw up with /linewidth command using value too big or too small.
   if(RenderUtils::DEFAULT_LINE_WIDTH >= 0.5 && RenderUtils::DEFAULT_LINE_WIDTH <= 5)
      ini->setValueF ("Settings", "LineWidth", RenderUtils::DEFAULT_LINE_WIDTH);
#endif
}


static void writeLevels(CIniFile *ini)
{
   const char *section = "Levels";

   // If there is no Levels key, we'll add it here.  Otherwise, we'll do nothing so as not to clobber an existing value
   // We'll write the default level list (which may have been overridden by the cmd line) because there are no levels in the INI
   if(ini->findSection(section) == ini->noID)    // Section doesn't exist... let's write one
      ini->addSection(section);              

   if(ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" All levels in this section will be loaded when you host a game in Server mode.");
      addComment(" You can call the level keys anything you want (within reason), and the levels will be sorted");
      addComment(" by key name and will appear in that order, regardless of the order the items are listed in.");
      addComment(" Example:");
      addComment(" Level1=ctf.level");
      addComment(" Level2=zonecontrol.level");
      addComment(" ... etc ...");
      addComment("This list can be overidden on the command line with the -leveldir, -rootdatadir, or -levels parameters.");
      addComment("----------------");
   }
}


static void writePasswordSection_helper(CIniFile *ini, const string &section)
{
   ini->addSection(section);

   if (ini->numSectionComments(section) == 0)
   {
      addComment("----------------");
      addComment(" This section holds passwords you've entered to gain access to various servers.");
      addComment("----------------");
   }
}


static void writePasswordSection(CIniFile *ini)
{
   writePasswordSection_helper(ini, "SavedLevelChangePasswords");
   writePasswordSection_helper(ini, "SavedAdminPasswords");
   writePasswordSection_helper(ini, "SavedOwnerPasswords");
   writePasswordSection_helper(ini, "SavedServerPasswords");
}


static void writeINIHeader(CIniFile *ini)
{
   if(!ini->NumHeaderComments())
   {
      //ini->headerComment("Bitfighter configuration file");
      //ini->headerComment("=============================");
      //ini->headerComment(" This file is intended to be user-editable, but some settings here may be overwritten by the game.");
      //ini->headerComment(" If you specify any cmd line parameters that conflict with these settings, the cmd line options will be used.");
      //ini->headerComment(" First, some basic terminology:");
      //ini->headerComment(" [section]");
      //ini->headerComment(" key=value");

      string headerComments =
         "Bitfighter configuration file\n"
         "=============================\n"
         "This file is intended to be user-editable, but some settings here may be overwritten by the game. "
         "If you specify any cmd line parameters that conflict with these settings, the cmd line options will be used.\n"
         "\n"
         "First, some basic terminology:\n"
         "\t[section]\n"
         "\tkey=value\n";

      Vector<string> lines = wrapString(headerComments, 100);

      for(S32 i = 0; i < lines.size(); i++)
         ini->headerComment(" " + lines[i]);

      ini->headerComment("");
   }
}


// Save more commonly altered settings first to make them easier to find
void saveSettingsToINI(CIniFile *ini, GameSettings *settings)
{
   writeINIHeader(ini);

   IniSettings *iniSettings = settings->getIniSettings();

   // This is the new way to write out all settings and should eventually
   // replace everything else below it
   writeSettings(ini, iniSettings);

   writeForeignServerInfo(ini, iniSettings);
   writeLoadoutPresets(ini, settings);
   writePluginBindings(ini, iniSettings);
   writeConnectionsInfo(ini, iniSettings);
   writeLevels(ini);
   writeSkipList(ini, settings->getLevelSkipList());
   writePasswordSection(ini);
   writeKeyBindings(ini, settings->getInputCodeManager());
   
   writeQuickChatMessages(ini, iniSettings);  // Does nothing if there are already chat messages in the INI

   // only needed for users using custom joystick 
   // or joystick that maps differenly in LINUX
   // This adds 200+ lines.
   //writeJoystick();
   writeServerBanList(ini, settings->getBanList());


   ini->writeFile();    // Commit the file to disk
}


void IniSettings::saveUserSettingsToINI(const string &name, CIniFile *ini, GameSettings *settings)
{
   const UserSettings *userSettings = settings->getUserSettings(name);

   string val = IniSettings::bitArrayToIniString(userSettings->levelupItemsAlreadySeen, userSettings->LevelCount);

   ini->setValue(name, "LevelupItemsAlreadySeenList", val, true);
   ini->writeFile();
}


// Can't be static -- called externally!
void writeSkipList(CIniFile *ini, const Vector<string> *levelSkipList)
{
   // If there is no LevelSkipList key, we'll add it here.  Otherwise, we'll do nothing so as not to clobber an existing value
   // We'll write our current skip list (which may have been specified via remote server management tools)

   const char *section = "LevelSkipList";

   ini->deleteSection(section);   // Delete all current entries to prevent user renumberings to be corrected from tripping us up
                                  // This may the unfortunate side-effect of pushing this section to the bottom of the INI file

   ini->addSection(section);      // Create the key, then provide some comments for documentation purposes

   addComment("----------------");
   addComment(" Levels listed here will be skipped and will NOT be loaded, even when they are specified in");
   addComment(" on the command line.  You can edit this section, but it is really intended for remote");
   addComment(" server management.  You will experience slightly better load times if you clean this section");
   addComment(" out from time to time.  The names of the keys are not important, and may be changed.");
   addComment(" Example:");
   addComment(" SkipLevel1=skip_me.level");
   addComment(" SkipLevel2=dont_load_me_either.level");
   addComment(" ... etc ...");
   addComment("----------------");

   Vector<string> normalizedSkipList;

   for(S32 i = 0; i < levelSkipList->size(); i++)
   {
      // "Normalize" the name a little before writing it
      string filename = lcase(levelSkipList->get(i));
      if(filename.find(".level") == string::npos)
         filename += ".level";

      normalizedSkipList.push_back(filename);
   }

   ini->setAllValues("LevelSkipList", "SkipLevel", normalizedSkipList);
}


//////////////////////////////////
//////////////////////////////////

// Use this rigamarole so we can replace this function with a different one for testing
static Vector<string> defaultFindAllPlaylistsInFolderFunction(const string &dir)
{
   const string extList[] = { "playlist" };

   return findAllThingsInFolder(dir, extList, ARRAYSIZE(extList));
}



// Constructor
FolderManager::FolderManager()
{
   string root = getExecutableDir();

   rootDataDir = root;

   // root used to specify the following folders
   robotDir      = joindir(root, "robots");          
   luaDir        = joindir(root, "scripts");           
   iniDir        = joindir(root, "");                  
   logDir        = joindir(root, "");                  
   screenshotDir = joindir(root, "screenshots");
   musicDir      = joindir(root, "music");           
   recordDir     = joindir(root, "record");         

   // root not used for these folders
   //addSfxDir("sfx", true);     --> Will be added later in resolveDirs()
   //fontsDir = joindir("", "fonts");

#ifndef BF_NO_STATS
   DbWriter::DatabaseWriter::sqliteFile = logDir + DbWriter::DatabaseWriter::sqliteFile;
#endif

   initialize();
}


// Constructor
FolderManager::FolderManager(const string &levelDir,    const string &robotDir,           const Vector<string> &sfxDirs,  const string &musicDir, 
                             const string &iniDir,      const string &logDir,             const string &screenshotDir,    const string &luaDir,
                             const string &rootDataDir, const Vector<string> &pluginDirs, const Vector<string> &fontDirs, const string &recordDir) :
               levelDir      (levelDir),
               robotDir      (robotDir),
               sfxDirs       (sfxDirs),
               musicDir      (musicDir),
               iniDir        (iniDir),
               logDir        (logDir),
               screenshotDir (screenshotDir),
               luaDir        (luaDir),
               rootDataDir   (rootDataDir),
               pluginDirs    (pluginDirs),
               fontDirs      (fontDirs),
               recordDir     (recordDir)
{
   initialize();
}


// Destructor
FolderManager::~FolderManager()
{
   // Do nothing
}


void FolderManager::initialize()
{
   mResolved = false;
   mFindAllPlaylistsFunction = &defaultFindAllPlaylistsInFolderFunction;
}


static string resolutionHelper(const string &cmdLineDir, const string &rootDataDir, const string &subdir)
{
   if(cmdLineDir != "")             // Direct specification of ini path takes precedence...
      return cmdLineDir;
   else                             // ...over specification via rootdatadir param
      return joindir(rootDataDir, subdir);
}


#define CHK_RESOLVED() TNLAssert(mResolved, "Must call resolveDirs() before using this getter!")

// Getters
string FolderManager::getIniDir()           const { return iniDir; }    // This one is usable before resolveDirs()
string FolderManager::getLevelDir()         const { CHK_RESOLVED();  return levelDir; }
string FolderManager::getRecordDir()        const { CHK_RESOLVED();  return recordDir; }
string FolderManager::getRobotDir()         const { CHK_RESOLVED();  return robotDir; }
string FolderManager::getScreenshotDir()    const { CHK_RESOLVED();  return screenshotDir; }
string FolderManager::getMusicDir()         const { CHK_RESOLVED();  return musicDir; }
string FolderManager::getRootDataDir()      const { CHK_RESOLVED();  return rootDataDir; }
string FolderManager::getLogDir()           const { CHK_RESOLVED();  return logDir; }
string FolderManager::getLuaDir()           const { CHK_RESOLVED();  return luaDir; }


const Vector<string> &FolderManager::getSfxDirs()    const { CHK_RESOLVED();  return sfxDirs;    }
const Vector<string> &FolderManager::getFontDirs()   const { CHK_RESOLVED();  return fontDirs;   }
const Vector<string> &FolderManager::getPluginDirs() const { CHK_RESOLVED();  return pluginDirs; }

#undef CHK_RESOLVED


// Get the named folder
string FolderManager::getDir(const string &folderTypeName) const
{
   return getDir(getFolderType(folderTypeName));
}


// TODO: Place this and the next function in a good ol' x-macro!
string FolderManager::getDir(FolderType folderType) const
{
   switch(folderType)
   {
      case Level:      return getLevelDir();
      case Robot:      return getRobotDir();
      case Music:      return getMusicDir();
      case Ini:        return getIniDir();
      case Log:        return getLogDir();
      case Screenshot: return getScreenshotDir();
      case Scripts:    return getLuaDir();
      case Recording:  return getRecordDir();

      default:
         TNLAssert(false, "Not implemented!");
         return "";
   }
}


FolderManager::FolderType FolderManager::getFolderType(const string &folderTypeName)
{
   if(folderTypeName == "level")
      return Level;
   if(folderTypeName == "robot")
      return Robot;
   if(folderTypeName == "music")
      return Music;
   if(folderTypeName == "ini")
      return Ini;
   if(folderTypeName == "log")
      return Log;
   if(folderTypeName == "screenshot")
      return Screenshot;
   if(folderTypeName == "scripts")
      return Scripts;
   if(folderTypeName == "recording")
      return Recording;

   TNLAssert(false, "Unknown typename!");
   return Level;
}



#define ADD_FOLDER_METHODS(methodName1, methodName2, folderDirs)         \
   void FolderManager::methodName1(const string &dir, bool appendToPath) \
   {                                                                     \
      if(!fileExists(dir))                                               \
         return;                                                         \
                                                                         \
      if(appendToPath)                                                   \
         folderDirs.push_back(dir);                                      \
      else                                                               \
         folderDirs.push_front(dir);                                     \
   }                                                                     \
                                                                         \
   void FolderManager::methodName2(const Vector<string> &dirs)           \
   {                                                                     \
      for(S32 i = 0; i < dirs.size(); i++)                               \
         methodName1(dirs[i], true);                                     \
   }                                                                     \


ADD_FOLDER_METHODS(addPluginDir, addPluginDirs, pluginDirs)
ADD_FOLDER_METHODS(addSfxDir,    addSfxDirs,    sfxDirs)
ADD_FOLDER_METHODS(addFontDir,   addFontDirs,   fontDirs)

#undef ADD_FOLDER_METHODS


// Setters
void FolderManager::setLevelDir(const string &lvlDir)
{
   levelDir = lvlDir;
   PhysFS::mount(levelDir, "/", false);      // Mount the levels folder at the root of our virtual filesystem
}


string makeAbsolute(const string &path)
{
   if(isAbsolute(path))
      return path;
   
   return PhysFS::getBaseDir() + path;
}


// Doesn't handle leveldir -- that one is handled separately, later, because it requires input from the INI file
void FolderManager::resolveDirs(GameSettings *settings)
{
   FolderManager *folderManager = settings->getFolderManager();
   FolderManager cmdLineDirs = settings->getCmdLineFolderManager();     // Versions specified on the cmd line

   string rootDataDir = cmdLineDirs.rootDataDir;

   folderManager->rootDataDir = rootDataDir;

   // Note that we generally rely on Bitfighter being run from its install folder for these paths to be right... at least in Windows
   // We'll convert the paths to absolute paths so that when we show folders in Diagnostics or elsewhere, they'll be easier for
   // people to understand where they are.

   // rootDataDir used to specify the following folders
   folderManager->robotDir = makeAbsolute(resolutionHelper(cmdLineDirs.robotDir, rootDataDir, "robots"));
   folderManager->luaDir = makeAbsolute(resolutionHelper(cmdLineDirs.luaDir, rootDataDir, "scripts"));
   folderManager->iniDir = makeAbsolute(resolutionHelper(cmdLineDirs.iniDir, rootDataDir, ""));
   folderManager->logDir = makeAbsolute(resolutionHelper(cmdLineDirs.logDir, rootDataDir, ""));
   folderManager->screenshotDir = makeAbsolute(resolutionHelper(cmdLineDirs.screenshotDir, rootDataDir, "screenshots"));
   folderManager->musicDir = makeAbsolute(resolutionHelper(cmdLineDirs.musicDir, rootDataDir, "music"));
   folderManager->recordDir = makeAbsolute(resolutionHelper(cmdLineDirs.recordDir, rootDataDir, "record"));

   folderManager->addPluginDirs(cmdLineDirs.pluginDirs);    // TODO: Make these absolute
   folderManager->addPluginDir(makeAbsolute(joindir(rootDataDir, "editor_plugins")), true);

   // rootDataDir not used for these folders
   folderManager->addSfxDirs(cmdLineDirs.sfxDirs);          // TODO: Make these absolute                   // Add any user specified folders
   folderManager->addSfxDir(makeAbsolute(joindir(getInstalledDataDir(), "sfx")), true);     // And add the system default as a fallback

   folderManager->addFontDirs(cmdLineDirs.fontDirs);       // TODO: Make these absolute                    // Add any user specified folders
   folderManager->addFontDir(makeAbsolute(joindir(getInstalledDataDir(), "fonts")), true);  // And add the system default as a fallback
#ifndef BF_NO_STATS
   DatabaseWriter::sqliteFile = makeAbsolute(folderManager->logDir) + DatabaseWriter::sqliteFile;
#endif
   mResolved = true;
}



// Figure out where the levels are.  This is exceedingly complex.
//
// Here are the rules:
//
// rootDataDir is specified on the command line via the -rootdatadir parameter
// levelDir is specified on the command line via the -leveldir parameter
// iniLevelDir is specified in the INI file
//
// Prioritize command line over INI setting, and -leveldir over -rootdatadir
//
// If levelDir exists, just use it (ignoring rootDataDir)
// ...Otherwise...
//
// If rootDataDir is specified then try
//       If levelDir is also specified try
//            rootDataDir/levels/levelDir
//            rootDataDir/levelDir
//       End
//   
//       rootDataDir/levels
// End      ==> Don't use rootDataDir
//      
// If iniLevelDir is specified
//     If levelDir is also specified try
//         iniLevelDir/levelDir
//     End   
//     iniLevelDir
// End    ==> Don't use iniLevelDir
//      
// levels
//
// If none of the above work, no hosting/editing for you!
//
// NOTE: See above for full explanation of what these functions are doing.
// This is a helper function for the main resolveLevelDir function below.
string FolderManager::resolveLevelDir(const string &levelDir) const
{
   if(levelDir == "")
      return "";

   if(fileExists(levelDir))     // Check for a valid absolute path in levelDir
      return makeAbsolute(levelDir);

   if(rootDataDir != "")
   {
      // Check root/levels/leveldir
      string candidate = strictjoindir(rootDataDir, "levels", levelDir);
      if(fileExists(candidate))
         return makeAbsolute(candidate);

      // Check root/leveldir
      candidate = strictjoindir(rootDataDir, levelDir);
      if(fileExists(candidate))
         return makeAbsolute(candidate);
   }

   return "";
}


// Figuring out where the levels are stored is so complex, it needs its own function!
void FolderManager::resolveLevelDir(GameSettings *settings)  
{
   // First, check any dir specified on the command line
   string cmdLineLevelDir = settings->getLevelDir(CMD_LINE);

   string resolved = resolveLevelDir(cmdLineLevelDir);

   if(resolved != "")
   {
      setLevelDir(resolved);
      return;
   }

   // Next, check rootdatadir/levels
   if(rootDataDir != "")
   {
      string candidate = makeAbsolute(strictjoindir(rootDataDir, "levels"));
      if(fileExists(candidate))   
      {
         setLevelDir(candidate);
         return;
      }
   }
   
   // rootDataDir is blank, or nothing using it worked, so
   // let's see if anything was specified in the INI
   string iniLevelDir = settings->getLevelDir(INI);
   
   if(iniLevelDir != "")
   {
      string candidate;

      // Try iniLevelDir/cmdLineLevelDir
      if(cmdLineLevelDir != "")
      {
         candidate = makeAbsolute(strictjoindir(iniLevelDir, cmdLineLevelDir));    // Is cmdLineLevelDir a subfolder of iniLevelDir?
         if(fileExists(candidate))
         {
            setLevelDir(candidate);
            return;
         }
      }
      
      // Ok, forget about cmdLineLevelDir.  Getting desperate here.  Try just the straight folder name specified in the INI file.
      if(fileExists(iniLevelDir))
      {
         setLevelDir(makeAbsolute(iniLevelDir));
         return;
      }
   }

   // Maybe there is just a local folder called levels?
   if(fileExists("levels"))
      setLevelDir(makeAbsolute("levels"));
   else
      setLevelDir("");    // Surrender
}


string FolderManager::findLevelFile(const string &filename) const
{
   return findLevelFile(levelDir, filename);
}


string FolderManager::findPlaylistFile(const string &filename) const
{
   return findPlaylistFile(levelDir, filename);
}


// This function will go away with complete adoption of physfs
string FolderManager::findLevelFile(const string &leveldir, const string &filename)
{
#ifdef TNL_OS_XBOX         // This logic completely untested for OS_XBOX... basically disables -leveldir param
   const char *folders[] = { "d:\\media\\levels\\", "" };
#else
   Vector<string> folders;
   folders.push_back(leveldir);

#endif
   const string extensions[] = { ".level", "" };

   return checkName(filename, folders, extensions);
}


string FolderManager::findPlaylistFile(const string &leveldir, const string &filename)
{
#ifdef TNL_OS_XBOX         // This logic completely untested for OS_XBOX... basically disables -leveldir param
   const char *folders[] = { "d:\\media\\levels\\", "" };
#else
   Vector<string> folders;
   folders.push_back(leveldir);

#endif
   const string extensions[] = { ".playlist", "" };

   return checkName(filename, folders, extensions);
}


Vector<string> FolderManager::getScriptFolderList() const
{
   Vector<string> folders;
   folders.push_back(levelDir);
   folders.push_back(luaDir);

   return folders;
}


Vector<string> FolderManager::getHelperScriptFolderList() const
{
   Vector<string> folders;
   folders.push_back(luaDir);
   folders.push_back(levelDir);
   folders.push_back(robotDir);

   return folders;
}


// Returns first found instance of a file that looks like it could be a levelgen with the specified name
string FolderManager::findLevelGenScript(const string &filename) const
{
   const string extensions[] = { ".levelgen", ".lua", "" };

   return checkName(filename, getScriptFolderList(), extensions);
}


string FolderManager::findScriptFile(const string &filename) const
{
   const string extensions[] = { ".lua", "" };

   return checkName(filename, getHelperScriptFolderList(), extensions);
}


Vector<string> FolderManager::findAllPlaylistsInFolder(const string &dir) const
{
   // Will call defaultFindAllPlaylistsInFolderFunction() except during certain tests
   return (*mFindAllPlaylistsFunction)(dir);     
}


string FolderManager::findPlugin(const string &filename) const
{
   const string extensions[] = { ".lua", "" };

   return checkName(filename, pluginDirs, extensions);
}


string FolderManager::findBotFile(const string &filename) const          
{
   return checkName(filename, robotDir, ".bot");
}


////////////////////////////////////////
////////////////////////////////////////

CmdLineSettings::CmdLineSettings()
{
   init();
}


// Destructor
CmdLineSettings::~CmdLineSettings()
{
   // Do nothing
}


void CmdLineSettings::init()
{
   dedicatedMode = false;

   loss = 0;
   lag = 0;
   stutter = 0;
   forceUpdate = false;
   maxPlayers = -1;
   displayMode = DISPLAY_MODE_UNKNOWN;
   winWidth = -1;
   xpos = -9999;
   ypos = -9999;
};


};
