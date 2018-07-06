#include <shlobj.h>
#include "f4se_common/f4se_version.h"
#include "f4se_common/Relocation.h"
#include "f4se_common/SafeWrite.h"
#include "f4se_common/Utilities.h"
#include "f4se/PapyrusArgs.h"
#include "f4se/PapyrusVM.h"
#include "f4se/PapyrusValue.h"
#include "f4se/PapyrusNativeFunctions.h"
#include "f4se/PapyrusStruct.h"
#include "f4se/PapyrusDelayFunctors.h"
#include "f4se/PapyrusObjectReference.h"
#include "f4se/PapyrusEvents.h"
#include "f4se/PluginManager.h"

#include "f4se/GameForms.h"
#include "f4se/GameTypes.h"

#include "f4se/PapyrusUtilities.h"
#include "f4se/GameAPI.h"
#include "F4SE/GameMenus.h"
#include "f4se/GameData.h"
#include "F4SE/GameEvents.h"
#include "f4se/GameReferences.h"
#include "f4se/GameObjects.h"
#include "f4se/GameExtraData.h"
#include "f4se/GameThreads.h"
#include "f4se/GameRTTI.h"
#include "f4se/GameCamera.h"
#include "f4se/GameInput.h"
#include "f4se/GameWorkshop.h"
#include "f4se/NiObjects.h"
#include "f4se/NiNodes.h"
#include "f4se/NiExtraData.h"
#include <map>
#include <set>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <locale>
#include <string>
#include <stdio.h>
#include <vector>
#include <chrono>
#include <ctime>
#include <cctype>
#include <sstream>
#include <windows.h>

#define VERSION_TO_STRING(a) std::to_string(GET_EXE_VERSION_MAJOR(a)) + "." + std::to_string(GET_EXE_VERSION_MINOR(a)) + "." + std::to_string(GET_EXE_VERSION_BUILD(a)) + (GET_EXE_VERSION_SUB(a) > 0? "." + GET_EXE_VERSION_SUB(a): "");

const char pluginName[] = {"ClipboardExtension"};
const UInt32 pluginVersion = MAKE_EXE_VERSION_EX(2,7,6,0);
const std::string pluginVersionString = VERSION_TO_STRING(pluginVersion);
const long double PI = acos(-1.0L);
const int TOOL_OFFSET = 64;
const UInt32 BOTTLECAP_FORM_ID = 0xF;
const std::string MANNEQUIN_RACE_EDITOR_ID = "DLC05ArmorRackRace";

struct TemplateItem {
	std::string plugin;
	UInt32	formId;
};

PluginHandle g_pluginHandle = kPluginHandle_Invalid;
F4SEPapyrusInterface * g_papyrus = NULL;
F4SEMessagingInterface * g_messaging = NULL;
F4SEObjectInterface * g_object = nullptr;
F4SETaskInterface * g_task = nullptr;
extern PluginManager g_pluginManager;
UInt32 nullHandle = *g_invalidRefHandle;
std::ofstream logOutStream;
BGSKeyword * actorTypeTurretKeyword;
BGSKeyword * actorTypeCreatureKeyword;
BGSKeyword * workshopItemKeyword;
BGSKeyword * workshopKeyword;
BGSKeyword * clipboardSelectedKeyword;
bool logInitialized = false;
bool enableLogging = false;
bool enableAnimalSelection = false;
bool enableToolCellObjectSelection = false;
bool enableWorkshopCellObjectSelection = false;
std::vector<TemplateItem> blackListedForms;

extern RelocAddr<_PlaceAtMe_Native> PlaceAtMe_Native;
extern RelocAddr<_GetLinkedRef_Native> GetLinkedRef_Native;
extern RelocAddr<_SetLinkedRef_Native> SetLinkedRef_Native;
extern RelocAddr<_MoveRefrToPosition> MoveRefrToPosition;

typedef UInt32(*_Enable_Native)(TESObjectREFR * ref, bool unkBool);
typedef UInt32(*_Disable_Native)(TESObjectREFR * ref, bool unkBool);
typedef void(*_EffectShaderPlay)(TESObjectREFR * ref, TESEffectShader * shader, float time, void* unk1, UInt32 unk2, void* unk3, void* unk4, UInt32 unk5);
typedef void(*_EffectShaderStop)(void* unk1, TESObjectREFR * ref, TESEffectShader * shader);

RelocAddr <_Enable_Native> Enable_Native(0x0040D970);
RelocAddr <_Disable_Native> Disable_Native(0x004E4300);
RelocAddr <_EffectShaderPlay> EffectShaderPlay(0x00422060);
RelocAddr <_EffectShaderStop> EffectShaderStop(0x00F0DF40);

RelocPtr <void*> qword_145907F18(0x05907F18);

DECLARE_STRUCT(SelectionDetails, "ClipboardExtension");
DECLARE_STRUCT(PatternObjectEntry, "ClipboardExtension");
DECLARE_STRUCT(PatternWireEntry, "ClipboardExtension");
DECLARE_STRUCT(PatternGeneralEntry, "ClipboardExtension");
DECLARE_STRUCT(PatternReferenceEntry, "ClipboardExtension");
DECLARE_STRUCT(ComponentEntry, "ClipboardExtension");

// trim from start (in place)
static inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
	ltrim(s);
	rtrim(s);
}

// equals, ignore case
static inline bool iequals(const std::string& a, const std::string& b) {
	return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) {
		return tolower(a) == tolower(b);
	});
}

// contains only digits.
static bool IsNumber(const std::string& s) {
	return !s.empty() && std::find_if(s.begin(), s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}

// safely convert to float, 0 on error
static float ToFloat(const std::string floatString) {
	try {
		return std::stof(floatString);
	}
	catch (...) {
		return 0;
	}
}

// safely convert to UInt32, 0 on error
static UInt32 ToInt(const std::string intString) {
	try {
		return std::stoi(intString);
	}
	catch (...) {
		return 0;
	}
}

// safely convert to UInt64, 0 on error
static UInt64 ToInt64(const std::string intString) {
	try {
		return std::stoll(intString);
	}
	catch (...) {
		return 0;
	}
}

// convert to string without chance of scientific notation or rounding.
static std::string ToString(const long double val) {
	std::ostringstream stream;
	stream << std::fixed << val;
	return stream.str();
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string GetCurrentDateTimeString() {
	const time_t now = time(0);
	struct tm tstruct = *localtime(&now);
	char buf[80];
	strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
	return buf;
}

// Path to log file.
const std::string & GetLogFilePath(){
	const std::string runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()){
		return runtimePath + "Data\\F4SE\\plugins\\clipboard.log";
	}
	return nullptr;
}

// Path to MCM settings file.
const std::string & GetSettingFilePath(){
	std::string	runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()) {
		return runtimePath + "Data\\MCM\\Settings\\clipboard.ini";
	}
	return nullptr;
}

// Path to default settings file.
const std::string & GetDefaultSettingFilePath(){
	std::string	runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()){
		return runtimePath + "Data\\MCM\\Config\\Clipboard\\settings.ini";
	}
	return nullptr;
}

// Join together the given vector of string using the given delimiter between them.
std::string Join(const std::vector<std::string>& v, const char del) {
	std::string rtnString;

	for (std::vector<std::string>::const_iterator p = v.begin(); p != v.end(); ++p) {
		rtnString += *p;
		if (p != v.end() - 1)
			rtnString += del;
	}

	return rtnString;
}

// Split the given string into a vector of string using the given delimiter.
std::vector<std::string> split(const std::string str, const char del) {
	std::stringstream stream(str);
	std::vector<std::string> result;

	while (stream.good()){
		std::string substr;
		std::getline(stream, substr, del);
		result.push_back(substr.c_str());
	}

	return result;
}

// Initialize the log on first call. Write given text to the log iff logging is enabled
void AppendToLog(const std::string text) {
	if (!logInitialized) {
		logInitialized = true;
		//Clear the log from previous session.
		std::ifstream logClearStream(GetLogFilePath(), std::ifstream::out | std::ifstream::trunc);
		if (logClearStream.is_open())
			logClearStream.close();
	}

	if (enableLogging) {
		if (!logOutStream.is_open())
			logOutStream.open(GetLogFilePath(), std::ios::app);
		logOutStream << GetCurrentDateTimeString() << ": " << text << std::endl;
	}
}

//Scope logging
#define logFunctionStart() if (enableLogging) AppendToLog("Start '" + std::string(__FUNCTION__) + "'");
#define logFunctionEnd(endIndex) if (enableLogging) AppendToLog("End " + std::to_string(endIndex) + " '" + std::string(__FUNCTION__) + "'");

// Retreive the setting value from (In ascending order or priority)
//		1. The privded default value
//		2. The default settings file
//		3. The MCM settings file
std::string GetSettingValue(const char * section, const char * key, const char * defaultValue){
	logFunctionStart();

	const std::string & defaultConfigPath = GetDefaultSettingFilePath();
	AppendToLog("Loading [" + std::string(section) + "].[" + std::string(key) + "] Default: " + std::string(defaultValue));

	std::string defaultFileValue;
	if (!defaultConfigPath.empty()) {
		char	defaultBuf[256];
		defaultBuf[0] = 0;
		UInt32	resultLen = GetPrivateProfileString(section, key, defaultValue, defaultBuf, sizeof(defaultBuf), defaultConfigPath.c_str());
		defaultFileValue = defaultBuf;
		AppendToLog("Clipboard Setting [" + std::string(section) + "].[" + std::string(key) + "] Value: " + defaultFileValue);
	}
	else {
		defaultFileValue = defaultValue;
		AppendToLog("Clipboard Setting Not Found");
	}

	const std::string & configPath = GetSettingFilePath();
	std::string result;
	if (!configPath.empty()) {
		char	resultBuf[256];
		resultBuf[0] = 0;
		UInt32	resultLen = GetPrivateProfileString(section, key, defaultFileValue.c_str(), resultBuf, sizeof(resultBuf), configPath.c_str());
		result = resultBuf;
		AppendToLog("MCM Setting [" + std::string(section) + "].[" + std::string(key) + "] Value: " + result);
	}
	else {
		result = defaultFileValue;
		AppendToLog("MCM Setting Not Found");
	}

	AppendToLog("Found [" + std::string(section) + "].[" + std::string(key) + "] Value: " + std::string(result));
	logFunctionEnd(0);
	return result;
}

// Retreieve a string setting.
BSFixedString GetSettingValueString(StaticFunctionTag *base, const BSFixedString section, const BSFixedString key, const BSFixedString defaultValue) {
	return BSFixedString(GetSettingValue(section, key, defaultValue).c_str());
}

// Retreieve an int setting.
UInt32 GetSettingValueInt(StaticFunctionTag *base, const BSFixedString section, const BSFixedString key, const UInt32 defaultValue) {
	std::string resultString = GetSettingValue(section, key, std::to_string(defaultValue).c_str());
	return (UInt32)std::stoi(resultString);
}

// Retreieve a float setting.
float GetSettingValueFloat(StaticFunctionTag *base, const BSFixedString section, const BSFixedString key, const float defaultValue) {
	std::string resultString = GetSettingValue(section, key, std::to_string(defaultValue).c_str());
	return (float)std::stof(resultString);
}

// Retreieve a boolean setting.
bool GetSettingValueBool(StaticFunctionTag *base, const BSFixedString section, const BSFixedString key, const bool defaultValue) {
	std::string resultString = GetSettingValue(section, key, (defaultValue ? "true" : "false"));
	return iequals(resultString, "true") || iequals(resultString, "1");
}

// Retreieve a vector of strings setting.
std::vector<std::string> GetSettingValueStringArray(StaticFunctionTag *base, const BSFixedString section, const BSFixedString key, const std::vector<std::string> defaultValue) {
	std::string resultString = GetSettingValue(section, key, Join(defaultValue, ',').c_str());
	return split(resultString, ',');
}

// Get the first keyword form with the provided name.
BGSKeyword * GetKeywordByName(const BSFixedString editorID){
	logFunctionStart();
	DataHandler* theDH = *g_dataHandler.GetPtr();
	for (UInt32 i = 0; i < theDH->arrKYWD.count; i++)
	{
		if (0 == _stricmp(theDH->arrKYWD[i]->keyword, editorID)) {
			logFunctionEnd(0);
			return theDH->arrKYWD[i];
		}
	}
	logFunctionEnd(1);
	return NULL;
}

// Load setting values and setup all persistent variables.
void LoadSettings() {
	logFunctionStart();
	enableLogging = GetSettingValueBool(nullptr,"Debug", "bEnableLogging", false);
	enableAnimalSelection = GetSettingValueBool(nullptr, "Selection", "bEnableAnimalSelection", false);
	enableToolCellObjectSelection = GetSettingValueBool(nullptr, "Selection", "bEnableToolCellObjectSelection", false);
	enableWorkshopCellObjectSelection = GetSettingValueBool(nullptr, "Selection", "bEnableWorkshopCellObjectSelection", false);

	actorTypeTurretKeyword = GetKeywordByName("ActorTypeTurret");
	actorTypeCreatureKeyword = enableAnimalSelection ? GetKeywordByName("ActorTypeCreature") : nullptr;

	clipboardSelectedKeyword = GetKeywordByName("ClipboardSelected");
	workshopItemKeyword = GetKeywordByName("workshopItemKeyword");
	workshopKeyword = GetKeywordByName("WorkshopKeyword");

	blackListedForms.clear();
	std::vector<std::string> defaultBlackList;
	defaultBlackList.push_back("SimSettlements.esm#103182");

	std::vector<std::string> blackListEntries = GetSettingValueStringArray(nullptr, "Selection", "aFormBlackList", defaultBlackList);
	for (const std::string& blackListEntry : blackListEntries) {
		AppendToLog("BlackList: " + std::string(blackListEntry.c_str()));
		std::vector<std::string> blackListEntryParts = split(blackListEntry, '#');
		if (blackListEntryParts.size() == 2) {
			AppendToLog("BlackList Item: " + blackListEntryParts[0] + " # " + std::to_string(ToInt(blackListEntryParts[1].c_str())));
			TemplateItem tItem = TemplateItem();
			tItem.plugin = blackListEntryParts[0];
			tItem.formId = ToInt(blackListEntryParts[1].c_str());
			blackListedForms.push_back(tItem);
		}
	}
	logFunctionEnd(0);
}

// Reload settings call made accessible to papyrus.
void ReloadSettings(StaticFunctionTag * base) {
	LoadSettings();
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string GetCurrentDateTime() {
	time_t     now = time(0);
	struct tm  tstruct; 
	char       buf[80];
	tstruct = *localtime(&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
	return buf;
}

//Get the base form in a way that supports both NPCs and objects.
const TESForm* GetBaseForm(const TESObjectREFR * obj) {
	logFunctionStart()
	TESForm* pBaseForm = obj->baseForm;

	BSExtraData* pExtraData = obj->extraDataList->GetByType(kExtraData_LeveledCreature);
	if (pExtraData != nullptr)
	{
		auto leveledBaseForm = *reinterpret_cast<TESForm**>((uintptr_t)pExtraData + 0x18);
		pBaseForm = (leveledBaseForm != nullptr) ? leveledBaseForm : obj->baseForm;
	}
	logFunctionEnd(0)
	return pBaseForm;
}

//Filter the given list of objects by those that are in the given box area.
VMArray<TESObjectREFR*> GetObjectsInBox(StaticFunctionTag *base, VMArray<TESObjectREFR*> objs, TESObjectREFR* toolObj, UInt32 xLength, UInt32 yLength, UInt32 zLength) {
	logFunctionStart();
	int halfHeight = zLength * 250;
	int halfWidth = xLength * 250;
	int halfLength = yLength * 250;

	long double cos1 = cos(-toolObj->rot.z);
	long double sin1 = sin(-toolObj->rot.z);

	float centerX = -(long double)(halfLength + TOOL_OFFSET) * sin1 + toolObj->pos.x;
	float centerY = (halfLength + TOOL_OFFSET) * cos1 + toolObj->pos.y;

	long double cos2 = cos(toolObj->rot.z);
	long double sin2 = sin(toolObj->rot.z);
	long double rotatedX;
	long double rotatedY;

	float relativeX;
	float relativeY;
	float relativeZ;

	VMArray<TESObjectREFR*> foundObjs;
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		relativeX = centerX - obj->pos.x;
		relativeY = centerY - obj->pos.y;
		relativeZ = toolObj->pos.z - obj->pos.z;
		rotatedX = relativeX * cos2 - relativeY * sin2;
		rotatedY = relativeY * cos2 + relativeX * sin2;

		if (relativeZ < -halfHeight || relativeZ > halfHeight) {
			continue;
		}

		rotatedX = relativeX * cos2 - relativeY * sin2;
		if (rotatedX < -halfWidth || rotatedX > halfWidth) {
			continue;
		}

		rotatedY = relativeY * cos2 + relativeX * sin2;
		if (rotatedY < -halfLength || rotatedY > halfLength) {
			continue;
		}

		foundObjs.Push(&obj);
	}
	logFunctionEnd(0);
	return foundObjs;
}

//Filter the given list of objects by those that are in the given cylinder area.
VMArray<TESObjectREFR*> GetObjectsInCylinder(StaticFunctionTag *base, VMArray<TESObjectREFR*> objs, TESObjectREFR* toolObj, UInt32 radius) {

	logFunctionStart(); 
	long double cos1 = cos(-toolObj->rot.z);
	long double sin1 = sin(-toolObj->rot.z);

	float centerX = -(long double)(radius + TOOL_OFFSET) * sin1 + toolObj->pos.x;
	float centerY = (radius + TOOL_OFFSET) * cos1 + toolObj->pos.y;

	VMArray<TESObjectREFR*> foundObjs;
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);

		if (sqrt(pow(obj->pos.x - centerX, 2) + pow(obj->pos.y - centerY, 2)) <= radius) {
			foundObjs.Push(&obj);
		}
	}
	logFunctionEnd(0);
	return foundObjs;
}

//Filter the given list of objects by those that are in the given sphere area.
VMArray<TESObjectREFR*> GetObjectsInSphere(StaticFunctionTag *base, VMArray<TESObjectREFR*> objs, TESObjectREFR* toolObj, UInt32 radius) {

	logFunctionStart(); 
	long double cos1 = cos(-toolObj->rot.z);
	long double sin1 = sin(-toolObj->rot.z);

	float centerX = -(long double)(radius + TOOL_OFFSET) * sin1 + toolObj->pos.x;
	float centerY = (radius + TOOL_OFFSET) * cos1 + toolObj->pos.y;

	VMArray<TESObjectREFR*> foundObjs;
	TESObjectREFR* obj;
	double d[] = {0,0,0};
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj,i);

		d[0] = abs(centerX - obj->pos.x);
		d[1] = abs(centerY - obj->pos.y);
		d[2] = abs(toolObj->pos.z - obj->pos.z);

		if (d[0] < d[1]) 
			std::swap(d[0], d[1]);
		if (d[0] < d[2]) 
			std::swap(d[0], d[2]);

		if (d[0] * sqrt(1.0 + d[1] / d[0] + d[2] / d[0]) <= radius) {
			foundObjs.Push(&obj);
		}
	}
	logFunctionEnd(0);
	return foundObjs;
}

/*
* Copied from PapyrusObjectReference.cpp to avoid build complications.
*/
bool TransmitConnectedPowerLatent(UInt32 stackId, TESObjectREFR * refr)
{
	if (!refr)
		return false;

	NiNode * root = refr->GetObjectRootNode();
	if (!root) {
		return false;
	}

	BGSKeyword * keyword = nullptr;
	BGSDefaultObject * workshopItemDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopItem");
	if (workshopItemDefault) {
		keyword = DYNAMIC_CAST(workshopItemDefault->form, TESForm, BGSKeyword);
	}

	// No workshop keyword is bad
	if (!keyword) {
		return false;
	}

	// Get the workshop by keyword
	TESObjectREFR * workshopRef = GetLinkedRef_Native(refr, keyword);
	if (!workshopRef) {
		return false;
	}

	// Workshop ref isn't a workshop!
	BSExtraData* extraDataWorkshop = workshopRef->extraDataList->GetByType(ExtraDataType::kExtraData_WorkshopExtraData);
	if (!extraDataWorkshop) {
		return false;
	}

	BSConnectPoint::Parents * extraData = (BSConnectPoint::Parents *)Runtime_DynamicCast(root->GetExtraData("CPA"), RTTI_NiExtraData, RTTI_BSConnectPoint__Parents);
	if (!extraData) {
		return false;
	}

	for (UInt32 i = 0; i < extraData->points.count; i++)
	{
		BSConnectPoint::Parents::ConnectPoint * connectPoint = extraData->points[i];
		if (!connectPoint)
			continue;

		NiPoint3 localPos = connectPoint->pos;
		NiAVObject * parent = nullptr;
		if (connectPoint->parent == "")
			parent = root;
		else
		{
			NiAVObject * child = root->GetObjectByName(&connectPoint->parent);
			if (child)
				parent = child;
		}

		NiPoint3 worldPos = localPos;
		if (parent) {
			worldPos = parent->m_worldTransform.rot.Transpose() * localPos + parent->m_worldTransform.pos;
		}

		float scale = connectPoint->scale;
		if (parent != root && refr->parentCell) {
			bhkWorld * world = CALL_MEMBER_FN(refr->parentCell, GetHavokWorld)();
			if (world) {
				TESObjectREFR * connected = GetObjectAtConnectPoint(refr, &worldPos, world, 8.0f);
				if (connected) {
					try // Probably wont make a difference but doesnt hurt to try
					{
						LinkPower_Internal(extraDataWorkshop, refr, connected, nullptr);
						LinkPower2_Internal(connected, extraDataWorkshop);
					}
					catch (...)
					{
						_MESSAGE("Power link error!");
					}
				}
			}
		}
	}

	LinkPower2_Internal(refr, extraDataWorkshop);
	return true;
}

/*
 * Copied from PapyrusObjectReference.cpp to avoid build complications.
*/
TESObjectREFR* AttachWireLatent(UInt32 stackId, TESObjectREFR* refA, TESObjectREFR* refB, TESForm* splineForm)
{
	logFunctionStart();
	TESObjectREFR * wireRef = nullptr;
	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;

	if (!splineForm) {
		BGSDefaultObject * splineDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopSplineObject");
		if (splineDefault) {
			splineForm = splineDefault->form;
		}
	}

	// No specified spline
	if (!splineForm ) {
		logFunctionEnd(0);
		return nullptr;
	}

	//  no refA
	if (!refA ) {
		logFunctionEnd(1);
		return nullptr;
	}

	//  no refB
	if (!refB) {
		logFunctionEnd(2);
		return nullptr;
	}

	// refs are same item
	if (refA == refB) {
		logFunctionEnd(3);
		return nullptr;
	}

	// no refA 3D loaded
	if (!refA->GetObjectRootNode() ) {
		logFunctionEnd(4);
		return nullptr;
	}

	// no refB 3D loaded
	if (!refB->GetObjectRootNode()) {
		logFunctionEnd(5);
		return nullptr;
	}

	// See if the two references are already linked by the same wire i.e. they have the same entry in their PowerLinks listing
	std::set<UInt64> linkedWires;
	ExtraDataList * extraDataRefA = refA->extraDataList;
	ExtraDataList * extraDataRefB = refB->extraDataList;
	if (extraDataRefA && extraDataRefB)
	{
		ExtraPowerLinks * powerLinksA = (ExtraPowerLinks*)extraDataRefA->GetByType(kExtraData_PowerLinks);
		ExtraPowerLinks * powerLinksB = (ExtraPowerLinks*)extraDataRefB->GetByType(kExtraData_PowerLinks);
		if (powerLinksA && powerLinksB) // Both items must have power links to check
		{
			tArray<UInt64> * connectionSearch;
			tArray<UInt64> * connectionPopulate;
			if (powerLinksA->connections.count < powerLinksB->connections.count) // Pick the smaller list to be the set
			{
				connectionPopulate = &powerLinksA->connections;
				connectionSearch = &powerLinksB->connections;
			}
			else
			{
				connectionPopulate = &powerLinksB->connections;
				connectionSearch = &powerLinksA->connections;
			}

			// Add the items from the smaller list to the set
			for (int i = 0; i < connectionPopulate->count; i++)
			{
				UInt64 formID = 0;
				connectionPopulate->GetNthItem(i, formID);
				linkedWires.insert(formID);
			}

			// Search the other listing for items that exist in the set
			for (int i = 0; i < connectionSearch->count; i++)
			{
				UInt64 formID = 0;
				connectionSearch->GetNthItem(i, formID);

				// This wire exists in the other list, it is invalid to wire the same objects twice
				if (linkedWires.find(formID) != linkedWires.end()) {
					logFunctionEnd(6);
					return nullptr;
				}
			}
		}
	}

	BGSBendableSpline * spline = DYNAMIC_CAST(splineForm, TESForm, BGSBendableSpline);
	BGSBendableSpline * splineA = DYNAMIC_CAST(refA->baseForm, TESForm, BGSBendableSpline);
	BGSBendableSpline * splineB = DYNAMIC_CAST(refB->baseForm, TESForm, BGSBendableSpline);

	BGSKeyword * keyword = nullptr;
	BGSDefaultObject * workshopItemDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopItem");
	if (workshopItemDefault) {
		keyword = DYNAMIC_CAST(workshopItemDefault->form, TESForm, BGSKeyword);
	}

	// No workshop keyword is bad
	// Connecting a wire to another wire or connecting a non-wire is invalid
	if (!keyword || !spline || splineA || splineB) {
		logFunctionEnd(7);
		return nullptr;
	}

	// Get the workshop by keyword
	TESObjectREFR * workshopRef = GetLinkedRef_Native(refA, keyword);
	if (!workshopRef) {
		logFunctionEnd(8);
		return nullptr;
	}

	// Workshop ref isn't a workshop!
	BSExtraData* extraDataWorkshop = workshopRef->extraDataList->GetByType(ExtraDataType::kExtraData_WorkshopExtraData);
	if (!extraDataWorkshop) {
		logFunctionEnd(9);
		return nullptr;
	}

	// Create our wire instance
	wireRef = PlaceAtMe_Native(vm, stackId, &refA, spline, 1, true, true, false);
	if (!wireRef) {
		logFunctionEnd(10);
		return nullptr;
	}

	UInt32 nullHandle = *g_invalidRefHandle;
	TESObjectCELL* parentCell = wireRef->parentCell;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(wireRef, GetWorldspace)();

	NiPoint3 rot;
	MoveRefrToPosition(wireRef, &nullHandle, parentCell, worldspace, &refA->pos, &rot);

	// Set the wire's linked ref to the workshop
	SetLinkedRef_Native(wireRef, workshopRef, keyword);

	LocationData locData(*g_player);
	FinalizeWireLink(&locData, wireRef, refB, 0, refA, 0);
	SetWireEndpoints_Internal(refA, 0, refB, 0, wireRef);

	ExtraBendableSplineParams * splineParams = (ExtraBendableSplineParams*)wireRef->extraDataList->GetByType(kExtraData_BendableSplineParams);
	if (splineParams) {
		splineParams->thickness = 1.5f;
	}

	LinkPower3_Internal(extraDataWorkshop, wireRef);
	LinkPower_Internal(extraDataWorkshop, refA, refB, wireRef);
	LinkPower2_Internal(refA, extraDataWorkshop);
	LinkPower2_Internal(refB, extraDataWorkshop);
	LinkPower4_Internal(wireRef);
	logFunctionEnd(11);
	return wireRef;
}

class TESForm_Clipboard : public BaseFormComponent{
public:
	struct Mods	{
		ModInfo ** entries;		// 00
		UInt32	size;			// 08
		UInt32	pad0C;			// 0C
	};

	Mods	* mods;		// 08
	UInt32	flags;		// 10
	UInt32	formID;		// 14
};

// Get the scale value of the given object, converted to a float.
float GetScale(const TESObjectREFR* obj) {
	logFunctionStart();
	return (((float)obj->scale) * 0.01);
}

//Get the index of the cell in the given list with the given id.
//TODO generalize to work with all TESForms
int GetCellIndexById(VMArray<TESObjectCELL*> objs, const UInt64 id) {

	logFunctionStart();
	TESObjectCELL* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj->formID == id) {
			logFunctionEnd(0);
			return i;
		}
	}
	logFunctionEnd(1);
	return -1;
}


//Get the index of the obj in the given list with the given id.
//TODO generalize to work with all TESForms
int GetObjectReferenceIndexById(VMArray<TESObjectREFR*> objs, const UInt64 id) {

	logFunctionStart();
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj) {
			if (obj->formID == id) {
				logFunctionEnd(0);
				return i;
			}
		}
	}
	logFunctionEnd(1);
	return -1;
}

//Get the path of the clipboard pattern in the given slot.
const std::string & GetPatternFilePath(const UInt32 slot){
	logFunctionStart();
	static std::string s_configPath;

	std::string	runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty())	{
		s_configPath = runtimePath;
		s_configPath += "Data\\F4SE\\plugins\\clipboard\\";
		s_configPath += std::to_string(slot);
		s_configPath += "\\pattern.ini";
	}
	logFunctionEnd(0);
	return s_configPath;
}

//Get the path of the blueprint in the given slot.
// INCOMPLETE, WORK IN PROGRESS
const std::string GetBlueprintFilePath(const UInt32 slot){
	logFunctionStart();
	static std::string s_configPath;

	std::string	runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty())	{
		s_configPath = runtimePath;
		s_configPath += "Data\\F4SE\\plugins\\TransferSettlements\blueprints\\";
		s_configPath += std::to_string(slot);
		WIN32_FIND_DATAA FindFileData;
		HANDLE h = FindFirstFileA((s_configPath + "\\*.json").c_str(), &FindFileData);
		if (h == INVALID_HANDLE_VALUE) {
			AppendToLog("FindFirstFile.  Err=" + GetLastError());
			logFunctionEnd(0);
			return nullptr;
		}
		AppendToLog("Found Blueprint: " + std::string(FindFileData.cFileName));
		logFunctionEnd(1);
		return s_configPath + "\\" + FindFileData.cFileName;
	}
	logFunctionEnd(2);
	return s_configPath;
}

// Empties the clipboard pattern in the given slot.
void ClearPattern( const UInt32 slot){
	logFunctionStart();
	std::ifstream inFile(GetPatternFilePath(slot).c_str(), std::ifstream::out | std::ifstream::trunc);
	if (inFile.is_open())
		inFile.close();
	logFunctionEnd(0);
}

// Reads a single value from the given clipboard pattern.
BSFixedString ReadPatternSectionValue(const UInt32 slot, const BSFixedString section, const BSFixedString key){
	logFunctionStart();
	std::string configPath = GetPatternFilePath(slot);

	std::string sectionLine(section);
	sectionLine = "[" + sectionLine + "]";

	std::ifstream inFile;
	std::string targetKey(key);
	trim(targetKey);
	std::string currentKey;
	std::string line;
	BSFixedString lineFixed;
	int valueIndex;
	bool inSection = false;
	inFile.open(configPath.c_str());
	if (inFile) {
		while (std::getline(inFile, line))
		{
			if (line.at(0) == '[' && line.back() == ']')
			{
				if (inSection)
					break;

				AppendToLog("Found Section: " + line);
				inSection = iequals(sectionLine, line);
			}
			else if (inSection)
			{
				AppendToLog("Found Line: " + line);
				valueIndex = line.find('=');
				currentKey = line.substr(0, valueIndex);
				trim(currentKey);
				AppendToLog("Found Key: " + currentKey);
				if (currentKey == targetKey)
				{
					line = line.substr(valueIndex + 1);
					trim(line);
					AppendToLog("Found Value: " + line);
					logFunctionEnd(0);
					return line.c_str();
				}
			}
		}
		inFile.close();
	}

	logFunctionEnd(1);
	return BSFixedString("");
}

// Get a list of all objects currently selected.
VMArray<TESObjectREFR*> GetSelectedObjectReferences(StaticFunctionTag *base, TESObjectREFR* refObj){
	logFunctionStart();
	tArray<TESObjectREFR*> linkedObjs;
	VMArray<TESObjectREFR*> objects;
	TESObjectREFR* currentObj = refObj;
	do {
		currentObj = (TESObjectREFR*)GetLinkedRef_Native(currentObj, clipboardSelectedKeyword);;

		if (!currentObj) {
			break;
		}
		else if (refObj == currentObj ) {
			break;
		}
		else if (!currentObj->formID) {
			break;
		}
		else if (currentObj->formID <= 0 ) {
			break;
		}
		else if (linkedObjs.GetItemIndex(currentObj) > 0) {
			break;
		}

		objects.Push(&currentObj);
		linkedObjs.Push(currentObj);
	} while (true);
	AppendToLog("Selected Objects Found: " + std::to_string(objects.Length()));
	logFunctionEnd(0);
	return objects;
}

// Count how many of the given objects are fully loaded.
UInt32 GetFullyLoadedCount(StaticFunctionTag *base, VMArray<TESObjectREFR*> objs) {
	int count = 0;
	logFunctionStart();
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);

		if (obj && obj->GetObjectRootNode()) {
			count++;
		}
	}
	logFunctionEnd(0);
	return count;
}

// Check if all selected objects have their 3d modals fullt loaded.
bool IsSelectionFullyLoaded(StaticFunctionTag *base, TESObjectREFR* refObj){
	logFunctionStart();
	tArray<TESObjectREFR*> linkedObjs;
	TESObjectREFR* currentObj = refObj;
	do {
		currentObj = (TESObjectREFR*)GetLinkedRef_Native(currentObj, clipboardSelectedKeyword);;

		if (!currentObj) {
			break;
		}
		else if (refObj == currentObj) {
			break;
		}
		else if (!currentObj->formID) {
			break;
		}
		else if (currentObj->formID <= 0) {
			break;
		}
		else if (linkedObjs.GetItemIndex(currentObj) > 0) {
			break;
		}
		else if (!currentObj->GetObjectRootNode()) {
			logFunctionEnd(0);
			return false;
		}

		linkedObjs.Push(currentObj);
	} while (true);

	logFunctionEnd(1);
	return true;
}

// Count how many objects are currently selected.
UInt32 GetSelectionCount(StaticFunctionTag *base, TESObjectREFR* refObj) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base,refObj);
	return objs.Length();
}

// Builds the full form id from its base form id and the plugin name.
UInt32 GetFullFormId(const UInt32 lowerFormId, const BSFixedString pluginName) {
	logFunctionStart();
	UInt32 formId = 0;

	if (pluginName == "Fallout4.esm") {
		AppendToLog("Vanilla Form " + std::to_string(lowerFormId));
		logFunctionEnd(0); 
		return lowerFormId;
	}

	auto lightMods = (*g_dataHandler)->modList.lightMods;
	for (int i = 0; i < lightMods.count; i++) {
		if (iequals(lightMods[i]->name, pluginName.c_str())) {
			UInt32 formId = 0xFE000000 | (i << 12) | (lowerFormId & 0xFFF);
			AppendToLog("Light Plugin " + std::string(pluginName) + "#" + std::to_string(lowerFormId) + " = " + std::to_string(formId));
			logFunctionEnd(1); 
			return formId;
		}
	}

	auto loadedMods = (*g_dataHandler)->modList.loadedMods;
	for (int i = 0; i < loadedMods.count; i++) {
		if (iequals(loadedMods[i]->name, pluginName.c_str())) {
			UInt32 formId = (i << 24) | (lowerFormId & 0xFFFFFF);
			AppendToLog("Normal Plugin " + std::string(pluginName) + "#" + std::to_string(lowerFormId) + " = " + std::to_string(formId));
			logFunctionEnd(2); 
			return formId;
		}
	}

	AppendToLog("Mod Missing! " + std::string(pluginName) + "#" + std::to_string(lowerFormId));
	logFunctionEnd(3);
	return BOTTLECAP_FORM_ID;
}

// Get the form of the given object with the plugin index data removed.
UInt32 GetLowerFormId(const TESForm* baseObj) {
	logFunctionStart();
	if (baseObj->formID >= 0xFE000000 && baseObj->formID <= 0xFFFFFFFF) {
		UInt32 formId = baseObj->formID & 0xFFF;
		AppendToLog("Light Plugin Form " + std::to_string(baseObj->formID) + " = " + std::to_string(formId));
		return formId;
	}
	UInt32 formId = baseObj->formID & 0xFFFFFF;
	AppendToLog("Normal Plugin Form " + std::to_string(baseObj->formID) + " = " + std::to_string(formId));
	logFunctionEnd(1);
	return formId;
}

//Get the name of the plugin that the given objects came from.
BSFixedString GetPluginName(const TESForm* baseObj) {
	logFunctionStart();
	TESForm_Clipboard * baseForm = (TESForm_Clipboard *)baseObj;
	if (baseForm->formID >= 0x01000000 && baseForm->formID <= 0xFFFFFFFF && baseForm->mods && baseForm->mods->entries && baseForm->mods->entries[0])
	{
		logFunctionEnd(0);
		return baseForm->mods->entries[0]->name;
	}
	logFunctionEnd(1);
	return "Fallout4.esm";
}

// Play the given shader form on the given object.
// TODO: Pass the shader as a TESEffectShader initially.
void ApplyShaderEffect(StaticFunctionTag *base, TESObjectREFR* obj, TESForm* effectShaderForm) {
	TESEffectShader  * effectShader = DYNAMIC_CAST(effectShaderForm, TESForm, TESEffectShader);
	if(effectShader)
		EffectShaderPlay(obj, effectShader, -1, nullptr, 0, nullptr, nullptr, 0);
}

// Stop the given shader form on the given object.
// TODO: Pass the shader as a TESEffectShader initially.
void RemoveShaderEffect(StaticFunctionTag *base, TESObjectREFR* obj, TESForm* effectShaderForm) {
	TESEffectShader  * effectShader = DYNAMIC_CAST(effectShaderForm, TESForm, TESEffectShader);
	if (effectShader)
		EffectShaderStop(*qword_145907F18, obj, effectShader);
}

// Play the given shader form on all selected objects.
// TODO: Pass the shader as a TESEffectShader initially.
void ApplyShaderEffectToSelection(StaticFunctionTag *base, TESObjectREFR* refObj, TESForm* effectShaderForm) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		ApplyShaderEffect(base, obj, effectShaderForm);
	}
}

// Stop the given shader form on all selected objects.
// TODO: Pass the shader as a TESEffectShader initially.
void RemoveShaderEffectToSelection(StaticFunctionTag *base, TESObjectREFR* refObj, TESForm* effectShaderForm) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		RemoveShaderEffect(base, obj, effectShaderForm);
	}
}

// Deselect all currently selected objects.
// TODO: Pass the shader as a TESEffectShader initially.
UInt32 ClearSelection(StaticFunctionTag *base, TESObjectREFR* refObj, TESForm* effectShader) {
	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* currentObj;

	for (int i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&currentObj, i);
		SetLinkedRef_Native(currentObj, nullptr, clipboardSelectedKeyword);
		RemoveShaderEffect(base, currentObj, effectShader);
	}
	SetLinkedRef_Native(refObj, nullptr, clipboardSelectedKeyword);
	logFunctionEnd(0);
	return selectedObjs.Length();
}

// Deselect the given object.
// TODO: Pass the shader as a TESEffectShader initially.
bool Deselect(StaticFunctionTag *base, TESObjectREFR* refObj, TESObjectREFR* obj, TESForm* effectShader) {
	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(nullptr, refObj);
	int index = GetObjectReferenceIndexById(selectedObjs, obj->formID);
	if (index < 0) {
		logFunctionEnd(0);
		return false;
	}

	TESObjectREFR* prevObj;
	if (index == 0) {
		prevObj = refObj;
	} else {
		selectedObjs.Get(&prevObj,index-1);
	}

	TESObjectREFR* nextObj = GetLinkedRef_Native(obj, clipboardSelectedKeyword);
	SetLinkedRef_Native(prevObj, nextObj, clipboardSelectedKeyword);
	SetLinkedRef_Native(obj, nullptr, clipboardSelectedKeyword);
	RemoveShaderEffect(base, obj, effectShader);

	VMVariable objVar;
	objVar.Set(&obj);

	VMArray<VMVariable> params;
	params.Push(&objVar);
	CallFunctionNoWait(refObj, "RemoveShaderEffect", params);
	logFunctionEnd(1);
	return true;
}

// Deselect all the given objects.
// TODO: Pass the shader as a TESEffectShader initially.
UInt32 DeselectAll(StaticFunctionTag *base, TESObjectREFR* refObj, VMArray<TESObjectREFR*> objs, TESForm* effectShader) {
	logFunctionStart();
	TESObjectREFR * currentObj;
	UInt32 count = 0;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&currentObj, i);
		if (Deselect(base, refObj, currentObj, effectShader)) {
			count++;
		}
	}

	logFunctionEnd(0);
	return count;
}

// Get the end of the chain of selected objects
// TODO: Provide a way to directly access the last selected object without having to walk the chain. (A second keyword link?)
TESObjectREFR * GetLastSelectedObject(VMArray<TESObjectREFR*> selectedObjs, TESObjectREFR* refObj) {
	logFunctionStart();
	TESObjectREFR * lastObj;
	if (selectedObjs.Length() > 0) {
		selectedObjs.Get(&lastObj, selectedObjs.Length() - 1);
	}
	else {
		lastObj = refObj;
	}
	logFunctionEnd(0);
	return lastObj;
}

//Test if the given form is a workshop.
bool IsWorkshop(const TESForm* thisForm){
	if (!thisForm)
		return false;

	UInt32 workbenchKeywordFormId = workshopKeyword->formID;
	BGSKeywordForm* pKeywords = DYNAMIC_CAST(thisForm, TESForm, BGSKeywordForm);
	if (pKeywords) {
		for (UInt32 i = 0; i < pKeywords->numKeywords; i++)
		{
			if (pKeywords->keywords[i] && pKeywords->keywords[i]->formID == workbenchKeywordFormId)
				return true;
		}
	}
	return false;
}

//Filter Actor's using their race.
//   * Allow Manneqins by race name.
//   * Allow Turrets using the actor type keyword. actorTypeTurretKeyword
//   * Allow Creatures using the actor type keyword. actorTypeCreatureKeyword
bool FilterByRace(const TESRace * race) {
	logFunctionStart();
	if (MANNEQUIN_RACE_EDITOR_ID == race->editorId.c_str()) {
		return true;
	}

	for (int i = 0; i < race->keywordForm.numKeywords; i++) {
		BGSKeyword * keyword = race->keywordForm.keywords[i];
		if (keyword) {
			AppendToLog("\t\t" + std::to_string(keyword->formID) + " " + keyword->GetFullName());
			if (keyword->formID == actorTypeTurretKeyword->formID) {
				logFunctionEnd(0);
				return true;
			}
			if (actorTypeCreatureKeyword && keyword->formID == actorTypeCreatureKeyword->formID) {
				logFunctionEnd(1);
				return true;
			}
		}
	}
	logFunctionEnd(2);
	return false;
}

//Check if an object can selected or not.
//   * Must not already be selected.
//   * Must not be a wire.
//   * Must not be a workshop.
//   * Must not be disabled or deleted.
//   * Must not inherit from a workshop form.
//   * Must not be introduced by Clipboard.
//   * Must not be in the blacklist.
//   * Must pass the FilterByRace function.
bool FilterSelection(const VMArray<TESObjectREFR*> * selectedObjs, const TESObjectREFR* obj) {
	logFunctionStart();
	BGSDefaultObject * splineDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopSplineObject");
	UInt64 splineFormId = splineDefault->form->formID;
	// Don't select if empty or a wire.
	if (!obj || obj->formID < 0 || obj->baseForm->formID == splineFormId) {
		logFunctionEnd(0);
		return false;
	}
	// Don't select settlement workbenches.
	if (IsWorkshop(obj)) {
		logFunctionEnd(1);
		return false;
	}
	// Don't select if already selected.
	if (selectedObjs) {
		if (GetObjectReferenceIndexById(*selectedObjs, obj->formID) >= 0) {
			logFunctionEnd(2);
			return false;
		}
	}
	//Don't select if disabled or deleted.
	if ((obj->flags & (obj->kFlag_IsDeleted | obj->kFlag_IsDisabled)) > 0) {
		logFunctionEnd(3);
		return false;
	}

	const TESForm* baseForm = GetBaseForm(obj);

	// Don't select workbenches.
	if (IsWorkshop(baseForm)) {
		logFunctionEnd(4);
		return false;
	}
	//Don't select if from the clipboard mod.
	std::string pluginName = GetPluginName(baseForm);
	if (pluginName.find("Clipboard.") == 0) {
		logFunctionEnd(5);
		return false;
	}

	//Check if the form has been blacklisted.
	UInt32 lowerFormId = GetLowerFormId(baseForm);
	for (TemplateItem& blackListForm : blackListedForms) {
		if(pluginName == blackListForm.plugin && lowerFormId == blackListForm.formId) {
			logFunctionEnd(6);
			return false;
		}
	}

	Actor * actor = DYNAMIC_CAST(obj, TESForm, Actor);
	if (actor) {
		//Don't select if a non-turret npc.
		TESNPC * npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (npc) {
			if (FilterByRace(npc->race.race)) {
				logFunctionEnd(7);
				return true;
			}
			if (npc->templateNPC && npc->templateNPC->race.race && FilterByRace(npc->templateNPC->race.race)) {
				logFunctionEnd(8);
				return true;
			}
			logFunctionEnd(9);
			return false;
		}
	}
	logFunctionEnd(10);
	return true;
}

// Select the given object.
// TODO: Pass the shader as a TESEffectShader initially.
bool Select(StaticFunctionTag *base, TESObjectREFR* refObj, TESObjectREFR* obj, TESForm* effectShader) {
	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* lastSelectedObj = GetLastSelectedObject(selectedObjs,refObj);

	if (refObj != obj && FilterSelection(&selectedObjs,obj)) {
		SetLinkedRef_Native(lastSelectedObj, obj, clipboardSelectedKeyword);
		SetLinkedRef_Native(obj, nullptr, clipboardSelectedKeyword);
		ApplyShaderEffect(base, obj, effectShader);
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}

// Select all of the given objects.
// TODO: Pass the shader as a TESEffectShader initially.
UInt32 SelectAll(StaticFunctionTag *base, TESObjectREFR* refObj, VMArray<TESObjectREFR*> objs, TESForm* effectShader) {
	logFunctionStart();

	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* lastSelectedObj = GetLastSelectedObject(selectedObjs, refObj);

	TESObjectREFR * currentObj;
	UInt32 count = 0;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&currentObj, i);
		if (currentObj && refObj != currentObj && FilterSelection(&selectedObjs, currentObj)) {
			SetLinkedRef_Native(lastSelectedObj, currentObj, clipboardSelectedKeyword);
			ApplyShaderEffect(base, currentObj, effectShader);
			lastSelectedObj = currentObj;
			count++;
		}
	}

	SetLinkedRef_Native(lastSelectedObj, nullptr, clipboardSelectedKeyword);
	logFunctionEnd(0);

	return count;
}

// Fix wire positions after programmatically moving/scaling the objects they are attached to.
VMArray<TESObjectREFR*> UpdateSelectedWires(VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	BGSDefaultObject * splineDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopSplineObject");
	UInt64 splineFormId = splineDefault->form->formID;
	LocationData locData(*g_player);
	VMArray<TESObjectREFR*> wires;
	TESObjectREFR* obj;
	UInt64 formID = 0;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj) {
			ExtraDataList * extraDataList = obj->extraDataList;
			if (extraDataList && extraDataList->HasType(kExtraData_PowerLinks)) {
				ExtraPowerLinks * powerLinks = (ExtraPowerLinks*)extraDataList->GetByType(kExtraData_PowerLinks);
				if (powerLinks) {
					for (int conIndex = 0; conIndex < powerLinks->connections.count; conIndex++) {
						powerLinks->connections.GetNthItem(conIndex, formID);
						TESForm * form = LookupFormByID(formID);
						if (form) {
							TESObjectREFR * wireRef = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
							if (wireRef->baseForm->formID == splineFormId) {
								//We Found a Wire!
								ExtraPowerLinks * wirePowerLinks = (ExtraPowerLinks*)wireRef->extraDataList->GetByType(kExtraData_PowerLinks);
								if (wirePowerLinks && wirePowerLinks->connections.count == 2) {

									wirePowerLinks->connections.GetNthItem(0, formID);
									TESObjectREFR * obj1Ref = DYNAMIC_CAST(LookupFormByID(formID), TESForm, TESObjectREFR);

									wirePowerLinks->connections.GetNthItem(1, formID);
									TESObjectREFR * obj2Ref = DYNAMIC_CAST(LookupFormByID(formID), TESForm, TESObjectREFR);

									FinalizeWireLink(&locData, wireRef, obj2Ref, 0, obj1Ref, 0);

									wires.Push(&wireRef);
								}
							}
						}
					}
				}
			}
		}
	}
	logFunctionEnd(0);
	return wires;
}

VMArray<TESObjectREFR*> UpdateSelectedWires_(StaticFunctionTag *base, TESObjectREFR* refObj) {
	logFunctionStart();
	return UpdateSelectedWires(GetSelectedObjectReferences(base, refObj));
}

// Rotate all selected objects around the z-axis of the given origin by the given number of degrees.
void RotateSelectionZ(StaticFunctionTag *base, TESObjectREFR* refObj, const float zRotation, const float originX, const float originY) {
	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);

	UInt32 nullHandle = *g_invalidRefHandle;
	TESObjectCELL* parentCell = refObj->parentCell;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(refObj, GetWorldspace)();

	long double zRotationMod = ((long double)zRotation) / 180.0l * PI;

	float cos1 = cos(-zRotationMod);
	float sin1 = sin(-zRotationMod);

	float relativeX;
	float relativeY;

	float newX;
	float newY;

	TESObjectREFR* obj;
	for (int i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (obj) {
			//Get relative Position
			relativeX = obj->pos.x - originX;
			relativeY = obj->pos.y - originY;

			//Apply Rotation
			newX = relativeX * cos1 - relativeY * sin1;
			newY = relativeY * cos1 + relativeX * sin1;

			//Get Absolute Position
			newX += originX;
			newY += originY;

			//Set object's new angle
			NiPoint3 newRot;
			newRot.x = obj->rot.x;
			newRot.y = obj->rot.y;
			newRot.z = obj->rot.z + zRotationMod;

			//Set object's new position
			NiPoint3 newPos;
			newPos.x = newX;
			newPos.y = newY;
			newPos.z = obj->pos.z;

			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &newRot);
			// Call again to fix jitter.
			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &newRot);
		}
	}
	logFunctionEnd(0);
}

//Move all selected objects by the given amounts along the x, y, and z axises.
void MoveSelection(StaticFunctionTag *base, TESObjectREFR* refObj, const float xMovement, const float yMovement, const float zMovement) {
	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(nullptr, refObj);

	UInt32 nullHandle = *g_invalidRefHandle;
	TESObjectCELL* parentCell = refObj->parentCell;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(refObj, GetWorldspace)();

	float cos1 = cos(refObj->rot.z);
	float sin1 = sin(refObj->rot.z);
	float cos2 = cos(-refObj->rot.z);
	float sin2 = sin(-refObj->rot.z);

	float relativeX;
	float relativeY;

	float newX;
	float newY;
	float newZ;

	TESObjectREFR* obj;
	for (int i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (obj) {
			//Get relative Position
			relativeX = obj->pos.x - refObj->pos.x;
			relativeY = obj->pos.y - refObj->pos.y;

			//Remove Rotation
			newX = relativeX * cos1 - relativeY * sin1;
			newY = relativeY * cos1 + relativeX * sin1;

			//Apply movement amount
			newX += xMovement;
			newY += yMovement;
			newZ = obj->pos.z + zMovement;

			//Remove Rotation
			relativeX = newX * cos2 - newY * sin2;
			relativeY = newY * cos2 + newX * sin2;

			//Get Absolute Position
			newX = relativeX + refObj->pos.x;
			newY = relativeY + refObj->pos.y;

			//Set object's new position
			NiPoint3 newPos;
			newPos.x = newX;
			newPos.y = newY;
			newPos.z = newZ;
			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &obj->rot);
			//Call again to fix jitter.
			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &obj->rot);
		}
	}
	logFunctionEnd(0);
};

//Get an array of all wires between the given objects.
VMArray<TESObjectREFR*> GetSelectedWires(VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	BGSDefaultObject * splineDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopSplineObject");
	UInt64 splineFormId = splineDefault->form->formID;
	VMArray<TESObjectREFR*> selectedWires;
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if(obj){
			ExtraDataList * extraDataList = obj->extraDataList;
			if (extraDataList){
				if (extraDataList->HasType(kExtraData_PowerLinks)){
					ExtraPowerLinks * powerLinks = (ExtraPowerLinks*)extraDataList->GetByType(kExtraData_PowerLinks);
					if (powerLinks)	{
						for (int conIndex = 0; conIndex < powerLinks->connections.count; conIndex++){
							UInt64 formID = 0;
							powerLinks->connections.GetNthItem(conIndex, formID);

							const TESForm * form = LookupFormByID(formID);
							if (form){
								TESObjectREFR * wireRef = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
								if (wireRef->baseForm->formID == splineFormId) {
									//We Found a Wire!
									ExtraPowerLinks * wirePowerLinks = (ExtraPowerLinks*)wireRef->extraDataList->GetByType(kExtraData_PowerLinks);
									if (wirePowerLinks && wirePowerLinks->connections.count == 2) {

										UInt64 formID = 0;
										wirePowerLinks->connections.GetNthItem(0, formID);
										if (obj->formID == formID) { //Check if we are at the starting end.
											wirePowerLinks->connections.GetNthItem(1, formID);
											if (GetObjectReferenceIndexById(objs, formID) >= 0)
												selectedWires.Push(&wireRef);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	logFunctionEnd(0);
	return selectedWires;
}

//Count the number of wires between selected objects.
UInt32 GetSelectionWireCount(StaticFunctionTag *base, TESObjectREFR* refObj) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base,refObj);
	if (objs.Length() <= 1) {
		return 0;
	}
	VMArray<TESObjectREFR*> wires = GetSelectedWires(objs);
	return wires.Length();
}

//Get the index of a given form's source plugin in the given list of plugins.
int GetSelectedPluginIndex(const TESForm* form, VMArray<BSFixedString> plugins) {

	logFunctionStart();
	BSFixedString plugin;
	BSFixedString pluginName = GetPluginName(form);
	for (int plgIndex = 0; plgIndex < plugins.Length(); plgIndex++) {
		plugins.Get(&plugin, plgIndex);
		if (plugin == pluginName) {
			return plgIndex;
		}
	}
	logFunctionEnd(0);
	return -1;
}

//Get an array of all plugins with forms in the given list.
//   Includes the plugin that sources the workshop these objects are attached to.
VMArray<BSFixedString> GetPlugins(TESObjectREFR* refObj,VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	std::vector<BSFixedString> plugins;
	VMArray<BSFixedString> pluginsArray;
	std::vector<BSFixedString>::iterator pluginIterator;

	TESObjectREFR* obj = (TESObjectREFR *)GetLinkedRef_Native(refObj, workshopItemKeyword);
	pluginsArray.Push(&GetPluginName(obj));

	int index = GetSelectedPluginIndex(refObj->parentCell, pluginsArray);
	if (index < 0) {
		pluginsArray.Push(&GetPluginName(refObj->parentCell));
	}

	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		const TESForm * baseForm = GetBaseForm(obj);
		int index = GetSelectedPluginIndex(baseForm,pluginsArray);
		if (index < 0) {
			pluginsArray.Push(&GetPluginName(baseForm));
		}
	}
	logFunctionEnd(0);

	return pluginsArray;
}

VMArray<BSFixedString> GetSelectedPlugins_(StaticFunctionTag *base, TESObjectREFR* refObj) {
	logFunctionStart();
	return GetPlugins(refObj, GetSelectedObjectReferences(base, refObj));
}

VMArray<BSFixedString> GetPlugins_(StaticFunctionTag *base, TESObjectREFR* refObj, VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	return GetPlugins(refObj, objs);
}

// Calculates position, area, scale, and count data for the set of selected objects.
SelectionDetails GetSelectionDetails(StaticFunctionTag *base, TESObjectREFR* refObj) {
	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);

	SelectionDetails details;
	details.SetNone(false);
	if (selectedObjs.Length() == 0) {
		details.Set<UInt32>("objectCount", 0);
		details.Set<UInt32>("wireCount", 0);
		details.Set<UInt32>("pluginCount", 0);

		details.Set<float>("areaX", 0);
		details.Set<float>("areaY", 0);
		details.Set<float>("areaZ", 0);

		details.Set<float>("centerX", 0);
		details.Set<float>("centerY", 0);
		details.Set<float>("centerZ", 0);

		details.Set<float>("minimumScale", 1);
		details.Set<float>("maximumScale", 1);
		details.Set<float>("averageScale", 1);
		return details;
	}
	VMArray<TESObjectREFR*> selectedWires = GetSelectedWires(selectedObjs);
	VMArray<BSFixedString> selectedPlugins = GetPlugins(refObj, selectedObjs);

	TESObjectREFR* obj;
	selectedObjs.Get(&obj, 0);

	float minimumX = obj->pos.x;
	float minimumY = obj->pos.y;
	float minimumZ = obj->pos.z;
	float minimumScale = GetScale(obj);

	float maximumX = minimumX;
	float maximumY = minimumY;
	float maximumZ = minimumZ;
	float maximumScale = minimumScale;

	float totalScale = minimumScale;

	for (int i = 1; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (obj) {
			float currentScale = GetScale(obj);

			if (obj->pos.x < minimumX) {
				minimumX = obj->pos.x;
			}
			else if (obj->pos.x > maximumX) {
				maximumX = obj->pos.x;
			}

			if (obj->pos.y < minimumY) {
				minimumY = obj->pos.y;
			}
			else if (obj->pos.y > maximumY) {
				maximumY = obj->pos.y;
			}

			if (obj->pos.z < minimumZ) {
				minimumZ = obj->pos.z;
			}
			else if (obj->pos.z > maximumZ) {
				maximumZ = obj->pos.z;
			}

			if (currentScale < minimumScale) {
				minimumScale = currentScale;
			}
			else if (currentScale > maximumScale) {
				maximumScale = currentScale;
			}

			totalScale += currentScale;
		}
	}

	details.Set<UInt32>("objectCount", selectedObjs.Length());
	details.Set<UInt32>("wireCount", selectedWires.Length());
	details.Set<UInt32>("pluginCount", selectedPlugins.Length());

	details.Set<float>("areaX", maximumX - minimumX);
	details.Set<float>("areaY", maximumY - minimumY);
	details.Set<float>("areaZ", maximumZ - minimumZ);

	details.Set<float>("centerX", (float)((minimumX + maximumX) / 2.0));
	details.Set<float>("centerY", (float)((minimumY + maximumY) / 2.0));
	details.Set<float>("centerZ", (float)((minimumZ + maximumZ) / 2.0));

	details.Set<float>("minimumScale", minimumScale);
	details.Set<float>("maximumScale", maximumScale);
	details.Set<float>("averageScale", totalScale/(float)selectedObjs.Length());

	logFunctionEnd(0);
	return details;
}

//Writes the general information section of a clipboard pattern.
void WriteSelectedGeneralInformation(std::ofstream& patternFileStream, BSFixedString patternName, BSFixedString characterName, TESObjectREFR* referenceObject, int wireCount, int objectCount, VMArray<BSFixedString> plugins) {
	logFunctionStart(); 
	TESObjectREFR * workshopRef = (TESObjectREFR *)GetLinkedRef_Native(referenceObject, workshopItemKeyword);
	UInt32 pluginFormId = workshopRef->formID & (UInt32)0xFFFFFF;
	int pluginIndex = GetSelectedPluginIndex(workshopRef, plugins);

	patternFileStream << "[general]\n";
	patternFileStream << "pattern_name=" << patternName << "\n";
	patternFileStream << "copied_on=" << GetCurrentDateTime() << "\n";
	patternFileStream << "character=" << characterName << "\n";
	patternFileStream << "workshop_id=" << std::to_string(pluginFormId) << "\n";
	patternFileStream << "workshop_plugin=" << std::to_string(pluginIndex) << "\n";
	patternFileStream << "plugin_count=" << plugins.Length() << "\n";
	patternFileStream << "wire_count=" << wireCount << "\n";
	patternFileStream << "object_count=" << objectCount << "\n";
	patternFileStream << "clipboard_version=" << pluginVersionString << std::endl;
	logFunctionEnd(0);
}

//Writes the reference information section of a clipboard pattern.
void WriteSelectedReferenceInformation(std::ofstream& patternFileStream, TESObjectREFR* referenceObject, VMArray<BSFixedString> plugins) {

	logFunctionStart(); 
	UInt32 cellFormId = referenceObject->parentCell->formID & (UInt32)0xFFFFFF;
	int pluginIndex = GetSelectedPluginIndex(referenceObject->parentCell, plugins);

	patternFileStream << "[reference]\n";
	patternFileStream << "cell_id=" << cellFormId << "\n";
	patternFileStream << "cell_plugin=" << pluginIndex << "\n";
	patternFileStream << "position_x=" << ToString(referenceObject->pos.x) << "\n";
	patternFileStream << "position_y=" << ToString(referenceObject->pos.y) << "\n";
	patternFileStream << "position_z=" << ToString(referenceObject->pos.z) << "\n";
	patternFileStream << "angle_x=" << ToString(((long double)referenceObject->rot.x) / PI * 180.0) << "\n";
	patternFileStream << "angle_y=" << ToString(((long double)referenceObject->rot.y) / PI * 180.0) << "\n";
	patternFileStream << "angle_z=" << ToString(((long double)referenceObject->rot.z) / PI * 180.0) << std::endl;
	logFunctionEnd(0);
}

//Writes the plugins section of a clipboard pattern.
void WriteSelectedPlugins(std::ofstream& patternFileStream, VMArray<BSFixedString> selectedPlugins) {

	logFunctionStart();
	patternFileStream << "[plugins]";
	BSFixedString pluginName;
	for (int i = 0; i < selectedPlugins.Length(); i++) {
		selectedPlugins.Get(&pluginName, i);
		patternFileStream << "\n" << i << "=" << pluginName;
	}
	patternFileStream << std::endl;
	logFunctionEnd(0);
}

//Writes the wires section of a clipboard pattern.
void WriteSelectedWires(std::ofstream& patternFileStream, VMArray<TESObjectREFR*> selectedWires, VMArray<TESObjectREFR*> selectedObjects){

	logFunctionStart(); 
	if (selectedWires.Length() > 0) {
		patternFileStream << "[wires]";
		TESObjectREFR * wire;
		UInt64 attachmentFormId1 = 0;
		UInt64 attachmentFormId2 = 0;
		int attachmentIndex1;
		int attachmentIndex2;
		for (int i = 0; i < selectedWires.Length(); i++) {
			selectedWires.Get(&wire, i);
			if (wire) {
				ExtraPowerLinks * wirePowerLinks = (ExtraPowerLinks*)wire->extraDataList->GetByType(kExtraData_PowerLinks);
				wirePowerLinks->connections.GetNthItem(0, attachmentFormId1);
				wirePowerLinks->connections.GetNthItem(1, attachmentFormId2);
				attachmentIndex1 = GetObjectReferenceIndexById(selectedObjects, attachmentFormId1);
				attachmentIndex2 = GetObjectReferenceIndexById(selectedObjects, attachmentFormId2);
				patternFileStream << "\n" << i << "=" << attachmentIndex1 << "|" << attachmentIndex2;
			}
			else {
				patternFileStream << "\n" << i << "=" << 0 << "|" << 0;
			}
		}
		patternFileStream << std::endl;
	}
	logFunctionEnd(0);
}

//Writes the objects section of a clipboard pattern.
void WriteSelectedObjects(std::ofstream& patternFileStream, TESObjectREFR* referenceObject, VMArray<TESObjectREFR*> selectedObjects, VMArray<BSFixedString> plugins) {

	logFunctionStart(); 
	TESObjectREFR *obj = nullptr;
	BSFixedString plugin;

	float selectionReferenceCos = cos(referenceObject->rot.z);
	float selectionReferenceSin = sin(referenceObject->rot.z);

	patternFileStream << "[objects]\n";
	for (int objIndex = 0; objIndex < selectedObjects.Length(); objIndex++)	{
		selectedObjects.Get(&obj, objIndex);
		if (obj) {
			BSFixedString pluginName = GetPluginName(obj);
			const TESForm * baseForm = GetBaseForm(obj);
			int pluginIndex = GetSelectedPluginIndex(baseForm, plugins);

			float relativeX = obj->pos.x - referenceObject->pos.x;
			float relativeY = obj->pos.y - referenceObject->pos.y;
			float relativeZ = obj->pos.z - referenceObject->pos.z;

			long double relativeAngleX = ((long double)obj->rot.x) / PI * 180.0;
			long double relativeAngleY = ((long double)obj->rot.y) / PI * 180.0;
			long double relativeAngleZ = ((long double)(obj->rot.z - referenceObject->rot.z)) / PI * 180.0;

			float finalX = relativeX * selectionReferenceCos - relativeY * selectionReferenceSin;
			float finalY = relativeY * selectionReferenceCos + relativeX * selectionReferenceSin;

			UInt32 pluginFormId = pluginFormId = GetLowerFormId(GetBaseForm(obj));

			patternFileStream << objIndex << "=";
			patternFileStream << pluginIndex << "|";
			patternFileStream << pluginFormId << "|";
			patternFileStream << ToString(GetScale(obj)) << "|";
			patternFileStream << ToString(finalX) << "|";
			patternFileStream << ToString(finalY) << "|";
			patternFileStream << ToString(relativeZ) << "|";
			patternFileStream << ToString(relativeAngleX) << "|";
			patternFileStream << ToString(relativeAngleY) << "|";
			patternFileStream << ToString(relativeAngleZ) << "\n";
		}
		else {

			int pluginIndex = GetSelectedPluginIndex(GetBaseForm(*g_player), plugins);

			patternFileStream << objIndex << "=";
			patternFileStream << pluginIndex << "|";
			patternFileStream << BOTTLECAP_FORM_ID << "|";
			patternFileStream << "1|0|0|0|0|0|0";
		}
	}
	patternFileStream << std::endl;
	logFunctionEnd(0);
}

//Reads all values from a given section of a clipboard pattern in the given slot.
VMArray<BSFixedString> ReadPatternSectionValues(UInt32 slot, BSFixedString section){
	logFunctionStart();
	VMArray<BSFixedString> result;
	std::string configPath = GetPatternFilePath(slot);
	
	std::string sectionLine(section);
	sectionLine = "[" + sectionLine + "]";
	bool inSection = false;

	std::ifstream inFile;
	std::string line;
	BSFixedString lineFixed;
	int valueIndex;
	inFile.open(configPath.c_str());
	if (!inFile) {
		logFunctionEnd(1);
		return result;
	}
	while (std::getline(inFile, line)){	
		if (!line.empty()) 	{
			if (line.at(0) == '[' && line.back() == ']'){
				if (inSection)
					break;

				AppendToLog("Found Section: " + line);
				inSection = iequals(sectionLine, line);
			}
			else if (inSection && line.length() > 0){
				AppendToLog("Found Line: " + line);
				valueIndex = line.find('=');
				if (valueIndex > 0) {
					line = line.substr(valueIndex + 1);
					trim(line);
					AppendToLog("Found Value: " + line);
					lineFixed = line.c_str();
					result.Push(&lineFixed);
				}
			}
		}
	}
	inFile.close();
	logFunctionEnd(0);
	return result;
}

//Reads all keys from a given section of a clipboard pattern in the given slot.
VMArray<BSFixedString> ReadPatternSectionKeys(UInt32 slot, BSFixedString section){
	logFunctionStart();
	VMArray<BSFixedString> result;
	std::string configPath = GetPatternFilePath(slot);

	std::string sectionLine(section);
	sectionLine = "[" + sectionLine + "]";
	bool inSection = false;

	std::ifstream inFile;
	std::string line;
	BSFixedString lineFixed;
	int valueIndex;
	inFile.open(configPath.c_str());
	if (!inFile) {
		logFunctionEnd(1);
		return result;
	}
	while (std::getline(inFile, line))	{
		if (line.at(0) == '[' && line.back() == ']'){
			if (inSection)
				break;

			AppendToLog("Found Section: " + line);
			inSection = iequals(sectionLine, line);
		}
		else if (inSection)	{

			AppendToLog("Found Line: " + line);
			valueIndex = line.find('=');
			line = line.substr(0, valueIndex);
			trim(line);

			AppendToLog("Found Key: " + line);
			lineFixed = line.c_str();
			result.Push(&lineFixed);
		}
	}
	inFile.close();
	logFunctionEnd(0);
	return result;
}

BSFixedString ReadPatternSectionValue_(StaticFunctionTag *base, UInt32 slot, BSFixedString section, BSFixedString key){
	logFunctionStart();
	return ReadPatternSectionValue(slot, section, key);
}

VMArray<BSFixedString> ReadPatternSectionValues_(StaticFunctionTag *base, UInt32 slot, BSFixedString section){
	logFunctionStart();
	return ReadPatternSectionValues(slot, section);
}

VMArray<BSFixedString> ReadPatternSectionKeys_(StaticFunctionTag *base, UInt32 slot, BSFixedString section){
	logFunctionStart();
	return ReadPatternSectionKeys(slot, section);
}

//Reads all object data from the clipboard pattern at the given slot.
VMArray<PatternObjectEntry> GetPatternObjects(StaticFunctionTag *base, UInt32 slot){
	logFunctionStart();
	VMArray<PatternObjectEntry> objects;
	BSFixedString objectLine;
	std::string elementString;
	VMArray<BSFixedString> objectLines = ReadPatternSectionValues(slot, "objects");
	for (int i = 0; i < objectLines.Length(); i++) {
		PatternObjectEntry entry;
		entry.SetNone(false);
		objectLines.Get(&objectLine, i);
		std::string line = objectLine;
		AppendToLog("New Object Line: " + line);
		int elementIndex = 0;
		int startIndex = 0;
		int endIndex;
		if (line.length() == 0) {
			//Found an empty line, time to escape.
			break;
		}
		while (startIndex >= 0) {
			endIndex = line.find('|', startIndex);
			if (endIndex >= 0) {
				elementString = line.substr(startIndex, endIndex);
				startIndex = endIndex + 1;
			}
			else {
				elementString = line.substr(startIndex);
				startIndex = -1;
			}

			if (elementIndex == 0) {
				AppendToLog("Found pluginIndex: " + elementString);
				entry.Set<UInt32>("pluginIndex", (UInt32)ToInt(elementString));
			}
			else if (elementIndex == 1) {
				AppendToLog("Found formId: " + elementString);
				UInt64 formId = ToInt64(elementString);
				entry.Set<UInt32>("formId", (UInt32)(formId & 0xFFFFFF));
			}
			else if (elementIndex == 2) {
				AppendToLog("Found scale: " + elementString);
				entry.Set<float>("scale", ToFloat(elementString));
			}
			else if (elementIndex == 3) {
				AppendToLog("Found positionX: " + elementString + ", " + std::to_string(ToFloat(elementString)));
				entry.Set<float>("positionX", ToFloat(elementString));
			}
			else if (elementIndex == 4) {
				AppendToLog("Found positionY: " + elementString + ", " + std::to_string(ToFloat(elementString)));
				entry.Set<float>("positionY", ToFloat(elementString));
			}
			else if (elementIndex == 5) {
				AppendToLog("Found positionZ: " + elementString);
				entry.Set<float>("positionZ", ToFloat(elementString));
			}
			else if (elementIndex == 6) {
				AppendToLog("Found angleX: " + elementString);
				entry.Set<float>("angleX", (float)(((long double)ToFloat(elementString)) / 180.0l * PI));
			}
			else if (elementIndex == 7) {
				AppendToLog("Found angleY: " + elementString);
				entry.Set<float>("angleY", (float)(((long double)ToFloat(elementString)) / 180.0l * PI));
			}
			else if (elementIndex == 8) {
				AppendToLog("Found angleZ: " + elementString);
				entry.Set<float>("angleZ", (float)(((long double)ToFloat(elementString)) / 180.0l * PI));
			}

			elementIndex++;
		}
		objects.Push(&entry);
	}
	logFunctionEnd(0);

	return objects;
}

//Reads all wire data from the clipboard pattern at the given slot.
VMArray<PatternWireEntry> GetPatternWires(StaticFunctionTag *base, UInt32 slot){
	logFunctionStart();
	VMArray<PatternWireEntry> wires;
	BSFixedString wireLine;
	std::string elementString;
	VMArray<BSFixedString> wireLines = ReadPatternSectionValues(slot, "wires");
	for (int i = 0; i < wireLines.Length(); i++) {
		PatternWireEntry entry;
		entry.SetNone(false);
		VMArray<BSFixedString> object;
		wireLines.Get(&wireLine, i);
		std::string line = wireLine;
		AppendToLog("New Wire Line: " + line);
		int elementIndex = 0;
		int startIndex = 0;
		int endIndex;
		if (line.length() == 0) {
			//Found an empty line, time to escape.
			break;
		}
		while (startIndex >= 0) {
			endIndex = line.find('|', startIndex);
			if (endIndex >= 0) {
				elementString = line.substr(startIndex, endIndex);
				startIndex = endIndex + 1;
			}
			else {
				elementString = line.substr(startIndex);
				startIndex = -1;
			}

			if (elementIndex == 0) {
				AppendToLog("Found attachmentIndex1: " + elementString);
				entry.Set<UInt32>("attachmentIndex1", ToInt(elementString));
			}
			else if (elementIndex == 1) {
				AppendToLog("Found attachmentIndex2: " + elementString);
				entry.Set<UInt32>("attachmentIndex2", ToInt(elementString));
			}

			elementIndex++;
		}
		wires.Push(&entry);
	}

	logFunctionEnd(0);
	return wires;
}

//Reads all plugin data from the clipboard pattern at the given slot.
VMArray<BSFixedString> GetPatternPlugins(StaticFunctionTag *base, UInt32 slot)
{
	logFunctionStart();
	return ReadPatternSectionValues(slot, "plugins");
}

//Reads the general information for the clipboard pattern in the given slot.
PatternGeneralEntry GetPatternGeneralInformation(StaticFunctionTag *base, UInt32 slot)
{
	logFunctionStart();
	PatternGeneralEntry entry;
	entry.SetNone(false);
	VMArray<BSFixedString> keys = ReadPatternSectionKeys(slot, "general");
	VMArray<BSFixedString> values = ReadPatternSectionValues(slot, "general");
	BSFixedString elementKey;
	BSFixedString elementValue;
	for (int i = 0; i < keys.Length(); i++) {
		keys.Get(&elementKey, i);
		values.Get(&elementValue, i);
		std::string elementValueString = elementValue;
		if (elementKey == "pattern_name") {
			AppendToLog("Found patternName: " + elementValueString);
			entry.Set("patternName", (BSFixedString)elementValue);
		}
		else if (elementKey == "character") {
			AppendToLog("Found characterName: " + elementValueString);
			entry.Set("characterName", (BSFixedString)elementValue);
		}
		else if (elementKey == "workshop_id") {
			AppendToLog("Found workshopId: " + elementValueString);
			UInt64 workshopId = ToInt64(elementValue.c_str());
			entry.Set<UInt32>("workshopId", (UInt32)(workshopId & 0xFFFFFF));
		}
		else if (elementKey == "workshop_plugin") {
			AppendToLog("Found workshopPlugin: " + elementValueString);
			entry.Set<UInt32>("workshopPlugin", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "plugin_count") {
			AppendToLog("Found pluginCount: " + elementValueString);
			entry.Set<UInt32>("pluginCount", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "wire_count") {
			AppendToLog("Found wireCount: " + elementValueString);
			entry.Set<UInt32>("wireCount", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "object_count") {
			AppendToLog("Found objectCount: " + elementValueString);
			entry.Set<UInt32>("objectCount", ToInt(elementValue.c_str()));
		}
	}

	logFunctionEnd(0);
	return entry;
}

//Get the number of populated clipboard patterns in the given range of slots.
UInt32 GetPatternCount(StaticFunctionTag *base, UInt32 firstSlot, UInt32 lastSlot) {
	int count = 0;
	for (int i = firstSlot; i <= lastSlot;i++) {
		PatternGeneralEntry generalEntry = GetPatternGeneralInformation(base,i);
		UInt32 objectCount;
		generalEntry.Get<UInt32>("objectCount", &objectCount);

		if (objectCount>0) {
			count++;
		}
	}
	return count;
}

//Reads the reference information for the clipboard pattern in the given slot.
PatternReferenceEntry GetPatternReferenceInformation(StaticFunctionTag *base, UInt32 slot){
	logFunctionStart();
	PatternReferenceEntry entry;
	entry.SetNone(false);
	VMArray<BSFixedString> keys = ReadPatternSectionKeys(slot, "reference");
	VMArray<BSFixedString> values = ReadPatternSectionValues(slot, "reference");
	BSFixedString elementKey;
	BSFixedString elementValue;
	for (int i = 0; i < keys.Length(); i++) {
		keys.Get(&elementKey, i);
		values.Get(&elementValue, i);
		std::string elementValueString = elementValue;

		if (elementKey == "cell_id") {
			AppendToLog("Found cellId: " + elementValueString);
			entry.Set("cellId", ToInt(elementValue.c_str()));
		}
		if (elementKey == "cell_plugin") {
			AppendToLog("Found cellPlugin: " + elementValueString);
			entry.Set("cellPlugin", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "position_x") {
			AppendToLog("Found positionX: " + elementValueString);
			entry.Set("positionX", ToFloat(elementValue.c_str()));
		}
		else if (elementKey == "position_x") {
			AppendToLog("Found positionX: " + elementValueString);
			entry.Set("positionX", ToFloat(elementValue.c_str()));
		}
		else if (elementKey == "position_y") {
			AppendToLog("Found positionY: " + elementValueString);
			entry.Set("positionY", ToFloat(elementValue.c_str()));
		}
		else if (elementKey == "position_z") {
			AppendToLog("Found positionZ: " + elementValueString);
			entry.Set("positionZ", ToFloat(elementValue.c_str()));
		}
		else if (elementKey == "angle_x") {
			AppendToLog("Found angleX: " + elementValueString);
			entry.Set("angleX", (float)(((long double)ToFloat(elementValue.c_str())) / 180.0l * PI));
		}
		else if (elementKey == "angle_y") {
			AppendToLog("Found angleY: " + elementValueString);
			entry.Set("angleY", (float)(((long double)ToFloat(elementValue.c_str())) / 180.0l * PI));
		}
		else if (elementKey == "angle_z") {
			AppendToLog("Found angleZ: " + elementValueString);
			entry.Set("angleZ", (float)(((long double)ToFloat(elementValue.c_str())) / 180.0l * PI));
		}
	}
	logFunctionEnd(0);
	return entry;
}

//Get the first constructible object that builds the given formid.
BGSConstructibleObject* GetConstructibleObjectByCreatedObject_(StaticFunctionTag *base, UInt32 formId) {
	logFunctionStart();
	DataHandler* theDH = *g_dataHandler.GetPtr();
	for (UInt32 i = 0; i < theDH->arrCOBJ.count; i++) {
		BGSConstructibleObject* conObj = theDH->arrCOBJ[i];
		if (!conObj->createdObject || !conObj->createdObject->formID) {
			continue;
		}

		if (conObj->createdObject->formType == 94) { //links to a formlist
			BGSListForm * createdFormList = DYNAMIC_CAST(conObj->createdObject, TESForm, BGSListForm);
			for (int f = 0; f < createdFormList->forms.count; f++) {
				TESForm * createdForm;
				createdFormList->forms.GetNthItem(f, createdForm);
				if (createdForm && createdForm->formID && createdForm->formID == formId) {
					logFunctionEnd(0);
					return conObj;
				}
			}
		} else if (conObj->createdObject->formID == formId) {
			logFunctionEnd(1);
			return conObj;
		}
	}
	logFunctionEnd(2);
	return nullptr;
}

//Get an array of all constructible objects.
//TODO: Confirm unused, remove.
VMArray<BGSConstructibleObject*> GetAllConstructibleObjects_(StaticFunctionTag *base){
	logFunctionStart();
	VMArray<BGSConstructibleObject*> result;
	DataHandler* theDH = *g_dataHandler.GetPtr();
	for (UInt32 i = 0; i < theDH->arrCOBJ.count; i++)
	{
		result.Push(&(theDH->arrCOBJ[i]));
	}
	logFunctionEnd(0);
	return result;
}

//Get a full form id from a pattern object, using a bottlecap as the fallback object.
UInt32 ToFormID(PatternObjectEntry objectEntry, VMArray<BSFixedString> plugins) {
	//Retreive and process form identifiers.
	UInt32 formId;
	objectEntry.Get<UInt32>("formId", &formId);

	//Retreive and process plugin.
	SInt32 pluginIndex;
	objectEntry.Get<SInt32>("pluginIndex", &pluginIndex);
	BSFixedString plugin;
	if (pluginIndex<plugins.Length()) {
		plugins.Get(&plugin, pluginIndex);
	}
	else {
		AppendToLog("Invalid plugin Index, use a bottlecap.");
		return BOTTLECAP_FORM_ID;
	}

	return GetFullFormId(formId, plugin);
}

//Get the full component cost of all objects in the clipboard pattern at the given slot.
VMArray<ComponentEntry> GetPatternComponentCost(StaticFunctionTag *base, UInt32 slot) {
	logFunctionStart();
	VMArray<PatternObjectEntry> objects = GetPatternObjects(base,slot);
	VMArray<ComponentEntry> components;
	std::map <UInt32, UInt32> countMap;
	std::map <UInt32, BSFixedString> nameMap;

	VMArray<BSFixedString> plugins = GetPatternPlugins(nullptr, slot);
	PatternObjectEntry objectEntry;

	for (int i = 0; i < objects.Length(); i++) {
		objects.Get(&objectEntry, i);
		UInt32 formId = ToFormID(objectEntry, plugins);

		BGSConstructibleObject * conObj = GetConstructibleObjectByCreatedObject_(base, formId);
		if (conObj) {
			BGSConstructibleObject::Component comp;
			for (int j = 0; j < conObj->components->count; j++) {
				conObj->components->GetNthItem(j, comp);
				std::map<UInt32, UInt32>::iterator search = countMap.find(comp.component->formID);
				if (search != countMap.end()) {
					countMap[comp.component->formID] += comp.count;
				}
				else {
					countMap[comp.component->formID] = comp.count;
					nameMap[comp.component->formID] = comp.component->GetFullName();
				}
			}
		}
	}

	for (std::map<UInt32, BSFixedString>::iterator it = nameMap.begin(); it != nameMap.end(); ++it) {
		ComponentEntry compEntry;
		compEntry.Set<UInt32>("formId", it->first);
		compEntry.Set<BSFixedString>("name", it->second);
		compEntry.Set<UInt32>("count", countMap[it->first]);

		components.Push(&compEntry);
	}

	logFunctionEnd(0);
	return components;
}

//Get the full component cost of all objects in the given array.
VMArray<ComponentEntry> GetComponentCost(StaticFunctionTag *base, VMArray<TESObjectREFR*> objs) {
	logFunctionStart();

	VMArray<ComponentEntry> components;
	std::map <UInt32, UInt32> countMap;
	std::map <UInt32, BSFixedString> nameMap;

	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		BGSConstructibleObject * conObj = GetConstructibleObjectByCreatedObject_(nullptr, obj->baseForm->formID);
		try {
			if (conObj && conObj->components && conObj->components->count > 0) {
				BGSConstructibleObject::Component comp;
				for (int j = 0; j < conObj->components->count; j++) {
					conObj->components->GetNthItem(j, comp);
					std::map<UInt32, UInt32>::iterator search = countMap.find(comp.component->formID);
					if (search != countMap.end()) {
						countMap[comp.component->formID] += comp.count;
					}
					else {
						countMap[comp.component->formID] = comp.count;
						nameMap[comp.component->formID] = comp.component->GetFullName();
					}
				}
			}
		}
		catch (...) {
			AppendToLog("ERROR: "+std::to_string(i) + " " + obj->baseForm->GetFullName() + " Constructible Object component failed.");
		}
	}

	for (std::map<UInt32, BSFixedString>::iterator it = nameMap.begin(); it != nameMap.end(); ++it) {
		ComponentEntry compEntry;
		compEntry.Set<UInt32>("formId", it->first);
		compEntry.Set<BSFixedString>("name", it->second);
		compEntry.Set<UInt32>("count", countMap[it->first]);
		components.Push(&compEntry);
	}

	logFunctionEnd(0);
	return components;
}

//Get the full component cost of all selected objects.
VMArray<ComponentEntry> GetSelectionComponentCost(StaticFunctionTag *base, TESObjectREFR * refObj) {
	logFunctionStart();
	return GetComponentCost(base, GetSelectedObjectReferences(base, refObj));
}

//Adds all objects in the given cell to the given array of objects. Blocks Duplicates.
void AppendObjectByCell(VMArray<TESObjectREFR*> * objList, TESObjectCELL* cell) {
	logFunctionStart();
	TESObjectREFR* obj;
	for (int i = 0; i < cell->objectList.count; i++)	{
		cell->objectList.GetNthItem(i, obj);

		if (!FilterSelection(nullptr, obj)) {
			continue;
		}
		if (GetObjectReferenceIndexById(*objList, obj->formID) >= 0) {
			continue;
		}

		objList->Push(&obj);
	}
	logFunctionEnd(0);
}

//Gets an array of all selectible objects. Requires an array of workshop objects to be passed in.
//TODO: Access workshop objects directly.
VMArray<TESObjectREFR*> GetSelectableObjectPool(StaticFunctionTag *base, VMArray<TESObjectREFR*> workshopObjects, TESObjectREFR* refObj) {
	logFunctionStart();
	VMArray<TESObjectREFR*> result;
	VMArray<TESObjectCELL*> usedCells;
	TESObjectREFR* obj;

	for (int i = 0; i < workshopObjects.Length(); i++)	{
		workshopObjects.Get(&obj, i);
		
		if (!FilterSelection(nullptr, obj)) {
			continue;
		}

		if (GetObjectReferenceIndexById(result, obj->formID) >= 0) {
			continue;
		}

		result.Push(&obj);
	}
	AppendToLog("Results Size: "+ std::to_string(result.Length()));
	if (enableToolCellObjectSelection) {
		if (GetCellIndexById(usedCells, refObj->parentCell->formID) < 0) {
			AppendObjectByCell(&result, refObj->parentCell);
			usedCells.Push(&refObj->parentCell);
		}
	}
	AppendToLog("Results Size: " + std::to_string(result.Length()));

	if (enableWorkshopCellObjectSelection) {
		TESObjectREFR* workshop = (TESObjectREFR *)GetLinkedRef_Native(refObj, workshopItemKeyword);
		if (GetCellIndexById(usedCells, workshop->parentCell->formID) < 0) {
			AppendObjectByCell(&result, workshop->parentCell);
			usedCells.Push(&workshop->parentCell);
		}
	}

	/*AppendToLog("Results Size: " + std::to_string(result.Length()));
	if (enablePlayerCellObjectSelection) {
		if (GetCellIndexById(usedCells, (*g_player)->parentCell->formID) < 0) {
			AppendObjectByCell(&result, (*g_player)->parentCell);
			usedCells.Push(&(*g_player)->parentCell);
		}
	}*/
	AppendToLog("Results Size: " + std::to_string(result.Length()));

	logFunctionEnd(0);
	return result;
}

//Gets an array of all objects in the given cell.
//TODO: Check if unused, remove.
VMArray<TESObjectREFR*> GetObjectsByCell(StaticFunctionTag *base, TESObjectCELL* cell) {
	logFunctionStart();
	VMArray<TESObjectREFR*> result;
	TESObjectREFR* obj;
	for (int i = 0; i < cell->objectList.count; i++) {
		cell->objectList.GetNthItem(i, obj);
		result.Push(&obj);
	}
	logFunctionEnd(0);
	return result;
}

//Gets an array of all objects in the given list that were sourced by the given plugin.
VMArray<TESObjectREFR*> FilterObjectsByPlugin(StaticFunctionTag *base, VMArray<TESObjectREFR*> selectionPool, BSFixedString pluginName) {
	logFunctionStart();
	VMArray<TESObjectREFR*> result;
	TESObjectREFR* obj;
	for (int i = 0; i < selectionPool.Length(); i++) {
		selectionPool.Get(&obj,i);
		BSFixedString objPluginName = GetPluginName(obj->baseForm? obj->baseForm : obj);
		if (objPluginName == pluginName) {
			result.Push(&obj);
		}
	}
	logFunctionEnd(0);
	return result;
}

//Write the full clipboard pattern.
PatternGeneralEntry WritePatternFile(StaticFunctionTag *base, UInt32 slot, BSFixedString patternName, BSFixedString characterName, TESObjectREFR* referenceObject) {

	logFunctionStart();
	VMArray<TESObjectREFR*> selectedObjects = GetSelectedObjectReferences(base, referenceObject);
	VMArray<TESObjectREFR*> selectedWires = GetSelectedWires(selectedObjects);
	VMArray<BSFixedString> selectedPlugins = GetPlugins(referenceObject,selectedObjects);

	ClearPattern(slot);

	std::ofstream patternFileStream(GetPatternFilePath(slot), std::ios::app);
	WriteSelectedGeneralInformation(patternFileStream, patternName, characterName, referenceObject, selectedWires.Length(), selectedObjects.Length(), selectedPlugins);
	WriteSelectedReferenceInformation(patternFileStream, referenceObject, selectedPlugins);
	WriteSelectedPlugins(patternFileStream, selectedPlugins);
	WriteSelectedWires(patternFileStream, selectedWires, selectedObjects);
	WriteSelectedObjects(patternFileStream, referenceObject, selectedObjects, selectedPlugins);
	patternFileStream.close();

	logFunctionEnd(0);
	return GetPatternGeneralInformation(base, slot);
}

//Trigger a settings load when game data first becomes available.
void MessageCallback(F4SEMessagingInterface::Message * msg){
	if (msg->type == F4SEMessagingInterface::kMessage_GameDataReady) {
		LoadSettings();
		AppendToLog("MessageCallback: Game Data Ready");
	}
} 

//Build the full selection box
VMArray<TESObjectREFR*> CreateSelectionBoxLatent(UInt32 stackId, StaticFunctionTag * base, TESObjectREFR* toolObj, TESForm* wallForm, UInt32 xLength, UInt32 yLength, UInt32 zLength) {
	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(toolObj, GetWorldspace)();
	VMArray<TESObjectREFR*> newObjs;

	float sin1 = sin(-toolObj->rot.z);
	float cos1 = cos(-toolObj->rot.z);

	float Y_OFFSET = TOOL_OFFSET;
	float SEGMENT_SIZE = 500.0;
	float WALL_BASE_SIZE = 256.0;
	float scale = SEGMENT_SIZE / WALL_BASE_SIZE;

	float halfHeight = zLength * 250;
	float halfWidth = xLength * 250;
	float halfLength = yLength * 250;
	float yUpperBound = yLength * 500 + Y_OFFSET;

	for (int z = 0; z < zLength; z++) {
		float zPos = -halfHeight + z * SEGMENT_SIZE;
		for (int x = 0; x < xLength; x++) {
			float xPos = -halfWidth + SEGMENT_SIZE / 2.0 + x * SEGMENT_SIZE;

			TESObjectREFR * newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {
				float relativeX = xPos * cos1 - Y_OFFSET * sin1;
				float relativeY = Y_OFFSET * cos1 + xPos * sin1;
				AppendToLog("Created Selection Wall 1 z: " + std::to_string(z) + " x: " + std::to_string(x));
				newObj->pos.x = toolObj->pos.x + relativeX;
				newObj->pos.y = toolObj->pos.y + relativeY;
				newObj->pos.z = toolObj->pos.z + zPos;
				newObj->rot.z = toolObj->rot.z + PI / 2.0;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);

				Enable_Native(newObj,false);
				newObjs.Push(&newObj);
			}

			newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {

				float relativeX = xPos * cos1 - yUpperBound * sin1;
				float relativeY = yUpperBound * cos1 + xPos * sin1;
				AppendToLog("Created Selection Wall 2 z: " + std::to_string(z) + " x: " + std::to_string(x));
				newObj->pos.x = toolObj->pos.x + relativeX;
				newObj->pos.y = toolObj->pos.y + relativeY;
				newObj->pos.z = toolObj->pos.z + zPos;
				newObj->rot.z = toolObj->rot.z + PI / 2.0;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);
				//Call twice to fix jitter effect
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}
		}



		for (int y = 0; y < yLength; y++) {
			float yPos = SEGMENT_SIZE / 2.0 + y * SEGMENT_SIZE + Y_OFFSET;

			TESObjectREFR * newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {
				float relativeX = -halfWidth * cos1 - yPos * sin1;
				float relativeY = yPos * cos1 - halfWidth * sin1;
				AppendToLog("Created Selection Wall 1 z: " + std::to_string(z) + " y: " + std::to_string(y));
				newObj->pos.x = toolObj->pos.x + relativeX;
				newObj->pos.y = toolObj->pos.y + relativeY;
				newObj->pos.z = toolObj->pos.z + zPos;
				newObj->rot.z = toolObj->rot.z;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);
				//Call twice to fix jitter effect
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}

			newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {

				float relativeX = halfWidth * cos1 - yPos * sin1;
				float relativeY = yPos * cos1 + halfWidth * sin1;
				AppendToLog("Created Selection Wall 2 z: " + std::to_string(z) + " x: " + std::to_string(y));
				newObj->pos.x = toolObj->pos.x + relativeX;
				newObj->pos.y = toolObj->pos.y + relativeY;
				newObj->pos.z = toolObj->pos.z + zPos;
				newObj->rot.z = toolObj->rot.z;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);
				//Call twice to fix jitter effect
				MoveRefrToPosition(newObj, &nullHandle, toolObj->parentCell, worldspace, &newObj->pos, &newObj->rot);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}
		}
	}
	return newObjs;
}

//Send the given workshop event to all selected objects. 
//Send the given workshop event to the workshop for each selected object.
void SendWorkshopEventToSelectedObjectsLatent(UInt32 stackId, StaticFunctionTag *base, TESObjectREFR* refObj, BSFixedString functionName) {
	TESObjectREFR * workshopRef = GetLinkedRef_Native(refObj, workshopItemKeyword);
	
	VMVariable workshopVar;
	workshopVar.Set(&workshopRef, true);

	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base,refObj);
	TESObjectREFR* obj;
	for (int i = 0; i < objs.Length();i++) {
		objs.Get(&obj,i);

		VMVariable objVar;
		objVar.Set(&obj, true);

		VMArray<VMVariable> workshopParams;
		workshopParams.Push(&objVar);
		CallFunctionNoWait(workshopRef, functionName, workshopParams);

		VMArray<VMVariable> objParams;
		objParams.Push(&workshopVar);
		CallFunctionNoWait(obj, functionName, objParams);
	}
}

//Call Enable on all objects in the given array.
void EnableObjectsLatent(UInt32 stackId, StaticFunctionTag *base, VMArray<TESObjectREFR*> objs) {

	logFunctionStart();
	TESObjectREFR * obj;
	for (int j = 0; j < objs.Length(); j++) {
		objs.Get(&obj, j);
		if (obj) {
			Enable_Native(obj, false);
		}
	}
	logFunctionEnd(0);
}

//Call Disable on all objects in the given array.
void DisableObjectsLatent(UInt32 stackId, StaticFunctionTag *base, VMArray<TESObjectREFR*> objs) {

	logFunctionStart();
	TESObjectREFR * obj;
	for (int j = 0; j < objs.Length(); j++) {
		objs.Get(&obj, j);
		if (obj) {
			Disable_Native(obj, false);
		}
	}
	logFunctionEnd(0);
}

//Scrap all objects in the given array.
//   First scraps first attached.
//   Does not return components.
void ScrapObjectsLatent(UInt32 stackId, StaticFunctionTag *base, VMArray<TESObjectREFR*> objs) {

	logFunctionStart();
	BGSDefaultObject * splineDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopSplineObject");
	UInt64 splineFormId = splineDefault->form->formID;

	LocationData locData(*g_player);
	TESObjectREFR * obj;
	for (int j = 0; j < objs.Length(); j++) {
		objs.Get(&obj, j);
		if (obj) {
			//Scrap attached wires first
			ExtraDataList * extraDataList = obj->extraDataList;
			if (extraDataList && extraDataList->HasType(kExtraData_PowerLinks)) {
				ExtraPowerLinks * powerLinks = (ExtraPowerLinks*)extraDataList->GetByType(kExtraData_PowerLinks);
				if (powerLinks) {
					for (int conIndex = 0; conIndex < powerLinks->connections.count; conIndex++) {
						UInt64 formID;
						powerLinks->connections.GetNthItem(conIndex, formID);
						TESForm * form = LookupFormByID(formID);
						if (form) {
							TESObjectREFR * wireRef = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
							if (wireRef->baseForm->formID == splineFormId) {
								SetLinkedRef_Native(wireRef, nullptr, workshopItemKeyword);
								ScrapReference(&locData, &wireRef, nullptr);
							}
						}

					}
				}
			}

			SetLinkedRef_Native(obj, nullptr, workshopItemKeyword);
			ScrapReference(&locData, &obj, nullptr);
		}
	}
	logFunctionEnd(0);
}

//Scrap all selected objects.
//   First scraps first attached.
//   Does not return components.
void ScrapSelectionLatent(UInt32 stackId, StaticFunctionTag *base, TESObjectREFR* refObj ) {

	logFunctionStart(); 
	BGSDefaultObject * splineDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopSplineObject");
	UInt64 splineFormId = splineDefault->form->formID;
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);

	SetLinkedRef_Native(refObj, nullptr, clipboardSelectedKeyword);

	LocationData locData(*g_player);
	TESObjectREFR * obj;
	for (int j = 0; j < selectedObjs.Length(); j++) {
		selectedObjs.Get(&obj,j);
		if (obj) {
			ExtraDataList * extraDataList = obj->extraDataList;
			if (extraDataList && extraDataList->HasType(kExtraData_PowerLinks)) {
				ExtraPowerLinks * powerLinks = (ExtraPowerLinks*)extraDataList->GetByType(kExtraData_PowerLinks);
				if (powerLinks) {
					for (int conIndex = 0; conIndex < powerLinks->connections.count; conIndex++) {
						UInt64 formID;
						powerLinks->connections.GetNthItem(conIndex, formID);
						TESForm * form = LookupFormByID(formID);
						if (form) {
							TESObjectREFR * wireRef = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
							if (wireRef->baseForm->formID == splineFormId) {
								SetLinkedRef_Native(wireRef, nullptr, workshopItemKeyword);
								ScrapReference(&locData, &wireRef, nullptr);
							}
						}

					}
				}
			}

			SetLinkedRef_Native(obj, nullptr, workshopItemKeyword);
			SetLinkedRef_Native(obj, nullptr, clipboardSelectedKeyword);

			ScrapReference(&locData, &obj, nullptr);
		}
	}
	logFunctionEnd(0);
}

//Scale all selected objects by the given amount.
//   If maintainShape, scale the relative position to the reference object as well.
//   Minimum resultent scale is 0.01
//   Maximum resultent scale is 10.0
void ScaleSelectionLatent(UInt32 stackId, StaticFunctionTag *base, TESObjectREFR* refObj, float scale, bool maintainShape) {

	logFunctionStart();
	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
	SelectionDetails details = GetSelectionDetails(nullptr, refObj);

	UInt32 nullHandle = *g_invalidRefHandle;
	TESObjectCELL* parentCell = refObj->parentCell;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(refObj, GetWorldspace)();

	float minimumScale = 1;
	float averageScale = 1;
	float maximumScale = 1;
	float centerX = refObj->pos.x;
	float centerY = refObj->pos.y;
	float minimumZ = refObj->pos.z;
	details.Get<float>("minimumScale", &minimumScale);
	details.Get<float>("averageScale", &averageScale);
	details.Get<float>("maximumScale", &maximumScale);
	if (maintainShape) {
		details.Get<float>("centerX", &centerX);
		details.Get<float>("centerY", &centerY);
		details.Get<float>("minimumZ", &minimumZ);
	}

	float finalScale;
	if (scale <= 0) {
		finalScale = 1.0 / averageScale;
	}
	else if (minimumScale * scale < 0.01) {
		finalScale = 0.01 / minimumScale;
	}
	else if (minimumScale * scale > 10.0) {
		finalScale = 10.0 / minimumScale;
	}
	else {
		finalScale = scale;
	}

	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (int i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);

		CALL_MEMBER_FN(obj, SetScale)(GetScale(obj) * finalScale);
		if (maintainShape) {
			obj->pos.x = (obj->pos.x - centerX) * finalScale + centerX;
			obj->pos.y = (obj->pos.y - centerY) * finalScale + centerY;
			obj->pos.z = (obj->pos.z - minimumZ) * finalScale + minimumZ;
			MoveRefrToPosition(obj, &nullHandle, obj->parentCell, worldspace, &obj->pos, &obj->rot);
		}
	}
	logFunctionEnd(0);
}

//Calls transmit power on all power generators in the pool of selected objects.
void TransmitPowerInSelectionLatent(UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObj) {
	logFunctionStart();
	TESForm * form = LookupFormByID(0x32E);
	ActorValueInfo * generatedPowerInfo;
	if (form && form->formType == ActorValueInfo::kTypeID)
		generatedPowerInfo = (ActorValueInfo *)form;

	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (int i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (!obj) {
			continue;
		}

		float power = obj->actorValueOwner.GetValue(generatedPowerInfo);
		if (power > 0) {
			AppendToLog("Power generator: " + std::to_string(power));
			TransmitConnectedPowerLatent(stackId, obj);
		}
	}
	logFunctionEnd(0);
}

//Creates wires between selected objects as defines by the clipboard pattern.
VMArray<TESObjectREFR*> PastePatternWiresLatent(UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObj, UInt32 slot) {
	logFunctionStart(); 
	TESWorldSpace* worldspace = CALL_MEMBER_FN(refObj, GetWorldspace)();
	VMArray<PatternWireEntry> wireEntries = GetPatternWires(base, slot);
	VMArray<TESObjectREFR*> selectedObjects = GetSelectedObjectReferences(base, refObj);
	VMArray<TESObjectREFR*> newWires;
	PatternWireEntry wireEntry;
	UInt32 attachmentIndex;
	TESObjectREFR * obj1;
	TESObjectREFR * obj2;
	for (int i = 0; i < wireEntries.Length(); i++) {
		wireEntries.Get(&wireEntry, i);
		wireEntry.Get<UInt32>("attachmentIndex1", &attachmentIndex);
		if (attachmentIndex>= selectedObjects.Length()) {
			AppendToLog("WARNING: attachmentIndex1 out of selection size!");
			continue;
		}
		selectedObjects.Get(&obj1, attachmentIndex);
		wireEntry.Get<UInt32>("attachmentIndex2", &attachmentIndex);
		if (attachmentIndex >= selectedObjects.Length()) {
			AppendToLog("WARNING: attachmentIndex2 out of selection size!");
			continue;
		}
		selectedObjects.Get(&obj2, attachmentIndex);
		if (obj1 && obj1->baseForm->formID != BOTTLECAP_FORM_ID && obj2 && obj2->baseForm->formID != BOTTLECAP_FORM_ID) {
			TESObjectREFR * wireRef = AttachWireLatent(stackId, obj1, obj2, nullptr);
			if (wireRef) {
				newWires.Push(&wireRef);
			}
		}
	}
	logFunctionEnd(0);
	return newWires;
}

//Creates objects as defined by the clipboard pattern.
VMArray<TESObjectREFR*> PastePatternObjectsLatent(UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObj, UInt32 slot) {
	logFunctionStart(); 

	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(refObj, GetWorldspace)();
	TESObjectREFR * workshopRef = GetLinkedRef_Native(refObj, workshopItemKeyword);
	VMArray<TESObjectREFR*> newObjs;
	VMArray<BSFixedString> plugins = GetPatternPlugins(nullptr, slot);
	float cos1 = cos(-refObj->rot.z);
	float sin1 = sin(-refObj->rot.z);
	VMArray<PatternObjectEntry> objectEntries = GetPatternObjects(base, slot);
	PatternObjectEntry objectEntry;
	TESObjectREFR * lastObj = refObj;
	for (int i = 0; i < objectEntries.Length(); i++) {
		AppendToLog("Start Pasting " + std::to_string(i));
		objectEntries.Get(&objectEntry, i); 
		float scale;
		objectEntry.Get<float>("scale", &scale);
		
		//Retreive and process position.
		NiPoint3 pos;
		objectEntry.Get<float>("positionX", &pos.x);
		objectEntry.Get<float>("positionY", &pos.y);
		objectEntry.Get<float>("positionZ", &pos.z);
		float tempX = pos.x * cos1 - pos.y * sin1;
		float tempY = pos.y * cos1 + pos.x * sin1;
		pos.x = tempX + refObj->pos.x;
		pos.y = tempY + refObj->pos.y;
		pos.z += refObj->pos.z;

		//Retreive and process rotation.
		NiPoint3 rot;
		objectEntry.Get<float>("angleX", &rot.x);
		objectEntry.Get<float>("angleY", &rot.y);
		objectEntry.Get<float>("angleZ", &rot.z);
		rot.z += refObj->rot.z;
		
		TESForm * form;
		try {
			form = LookupFormByID(ToFormID(objectEntry, plugins));
			if (!form) {
				AppendToLog("Missing form, use a bottlecap");
				form = LookupFormByID(BOTTLECAP_FORM_ID);
			}
		}
		catch (...) {
			AppendToLog("Error loading form, use a bottlecap.");
			form = LookupFormByID(BOTTLECAP_FORM_ID);
		}

		try {
			TESObjectREFR * newObj = PlaceAtMe_Native(vm, stackId, &refObj, form, 1, true, true, false);

			if (newObj) {
				AppendToLog("Successfully created new object");
				SetLinkedRef_Native(newObj, workshopRef, workshopItemKeyword);

				//Apply scale
				if (scale != 1.0 && scale >= 0.01 && scale <= 10.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}

				//Set Position/Rotation
				MoveRefrToPosition(newObj, &nullHandle, refObj->parentCell, worldspace, &pos, &rot);

				//Add to selection chain
				SetLinkedRef_Native(lastObj, newObj, clipboardSelectedKeyword);

				newObjs.Push(&newObj);

				lastObj = newObj;
			}
			else {
				newObjs.Push(nullptr);
				AppendToLog("Failed to create new object");
			}
		}
		catch (...) {
			newObjs.Push(nullptr);
			AppendToLog("Error creating new object.");
		}
	}
	SetLinkedRef_Native(lastObj, nullptr, clipboardSelectedKeyword);

	logFunctionEnd(0);
	return newObjs;
}

DECLARE_DELAY_FUNCTOR(CreateSelectionBoxFunctor, 5, CreateSelectionBoxLatent, StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, TESForm*, UInt32, UInt32, UInt32);
DECLARE_DELAY_FUNCTOR(ScrapSelectionFunctor, 1, ScrapSelectionLatent, StaticFunctionTag, void, TESObjectREFR*);
DECLARE_DELAY_FUNCTOR(SendWorkshopEventToSelectedObjectsFunctor, 2, SendWorkshopEventToSelectedObjectsLatent, StaticFunctionTag, void, TESObjectREFR*, BSFixedString);
DECLARE_DELAY_FUNCTOR(DisableObjectsFunctor, 1, DisableObjectsLatent, StaticFunctionTag, void, VMArray<TESObjectREFR*>);
DECLARE_DELAY_FUNCTOR(EnableObjectsFunctor, 1, EnableObjectsLatent, StaticFunctionTag, void, VMArray<TESObjectREFR*>);
DECLARE_DELAY_FUNCTOR(ScrapObjectsFunctor, 1, ScrapObjectsLatent, StaticFunctionTag, void, VMArray<TESObjectREFR*>);
DECLARE_DELAY_FUNCTOR(ScaleSelectionFunctor, 3, ScaleSelectionLatent, StaticFunctionTag, void, TESObjectREFR*, float, bool);
DECLARE_DELAY_FUNCTOR(TransmitPowerInSelectionFunctor, 1, TransmitPowerInSelectionLatent, StaticFunctionTag, void, TESObjectREFR*);
DECLARE_DELAY_FUNCTOR(PastePatternWiresFunctor, 2, PastePatternWiresLatent, StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32);
DECLARE_DELAY_FUNCTOR(PastePatternObjectsFunctor, 2, PastePatternObjectsLatent, StaticFunctionTag, VMArray<TESObjectREFR*>,TESObjectREFR*, UInt32);

bool CreateSelectionBox(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * toolObj, TESForm* wallForm,UInt32 xLength, UInt32 yLength, UInt32 zLength) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new CreateSelectionBoxFunctor(CreateSelectionBoxLatent, vm, stackId, base, toolObj, wallForm, xLength, yLength, zLength));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}
bool ScrapSelection(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObject) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new ScrapSelectionFunctor(ScrapSelectionLatent, vm, stackId, base, refObject));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}
bool SendWorkshopEventToSelectedObjects(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObject, BSFixedString eventFunctionName) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new SendWorkshopEventToSelectedObjectsFunctor(SendWorkshopEventToSelectedObjectsLatent, vm, stackId, base, refObject, eventFunctionName));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}

bool DisableObjects(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new DisableObjectsFunctor(DisableObjectsLatent, vm, stackId, base, objs));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}
bool EnableObjects(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new EnableObjectsFunctor(EnableObjectsLatent, vm, stackId, base, objs));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}
bool ScrapObjects(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, VMArray<TESObjectREFR*> objs) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new ScrapObjectsFunctor(ScrapObjectsLatent, vm, stackId, base, objs));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}
bool ScaleSelection(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObject, float scale, bool maintainShape) {
	logFunctionStart(); 
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new ScaleSelectionFunctor(ScaleSelectionLatent, vm, stackId, base, refObject, scale, maintainShape));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}


bool TransmitPowerInSelection(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObject) {
	logFunctionStart();
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new TransmitPowerInSelectionFunctor( TransmitPowerInSelectionLatent, vm, stackId, base, refObject));
		logFunctionEnd(0);
		return true;
	} 
	logFunctionEnd(1); 
	return false;
}

bool PastePatternWires(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObject, UInt32 slot) {
	logFunctionStart(); 
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new PastePatternWiresFunctor(PastePatternWiresLatent, vm, stackId, base, refObject, slot));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
}

bool PastePatternObjects(VirtualMachine * vm, UInt32 stackId, StaticFunctionTag * base, TESObjectREFR * refObject, UInt32 slot) {
	logFunctionStart(); 
	if (g_object) {
		g_object->GetDelayFunctorManager().Enqueue(new PastePatternObjectsFunctor(PastePatternObjectsLatent, vm, stackId, base, refObject, slot));
		logFunctionEnd(0);
		return true;
	}
	logFunctionEnd(1);
	return false;
} 

bool RegisterFuncs(VirtualMachine* vm)
{
	_MESSAGE("RegisterFuncs"); 
	F4SEObjectRegistry& f4seObjRegistry = g_object->GetObjectRegistry();
	f4seObjRegistry.RegisterClass<CreateSelectionBoxFunctor>();
	f4seObjRegistry.RegisterClass<ScrapSelectionFunctor>();
	f4seObjRegistry.RegisterClass<SendWorkshopEventToSelectedObjectsFunctor>();
	f4seObjRegistry.RegisterClass<DisableObjectsFunctor>();
	f4seObjRegistry.RegisterClass<EnableObjectsFunctor>();
	f4seObjRegistry.RegisterClass<ScrapObjectsFunctor>();
	f4seObjRegistry.RegisterClass<ScaleSelectionFunctor>();
	f4seObjRegistry.RegisterClass<TransmitPowerInSelectionFunctor>();
	f4seObjRegistry.RegisterClass<PastePatternObjectsFunctor>();
	f4seObjRegistry.RegisterClass<PastePatternWiresFunctor>();

	vm->RegisterFunction(new LatentNativeFunction5<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, TESForm*, UInt32, UInt32, UInt32>("CreateSelectionBox", pluginName, CreateSelectionBox, vm));
	vm->RegisterFunction(new LatentNativeFunction1<StaticFunctionTag, void, TESObjectREFR*>("ScrapSelection", pluginName, ScrapSelection, vm));
	vm->RegisterFunction(new LatentNativeFunction1<StaticFunctionTag, void, VMArray<TESObjectREFR*>>("ScrapObjects", pluginName, ScrapObjects, vm));
	vm->RegisterFunction(new LatentNativeFunction2<StaticFunctionTag, void, TESObjectREFR*, BSFixedString>("SendWorkshopEventToSelectedObjects", pluginName, SendWorkshopEventToSelectedObjects, vm));
	vm->RegisterFunction(new LatentNativeFunction1<StaticFunctionTag, void, VMArray<TESObjectREFR*>>("EnableObjects", pluginName, EnableObjects, vm));
	vm->RegisterFunction(new LatentNativeFunction1<StaticFunctionTag, void, VMArray<TESObjectREFR*>>("DisableObjects", pluginName, DisableObjects, vm));
	vm->RegisterFunction(new LatentNativeFunction3<StaticFunctionTag, void, TESObjectREFR*, float, bool>("ScaleSelection", pluginName, ScaleSelection, vm));
	vm->RegisterFunction(new LatentNativeFunction1<StaticFunctionTag, void, TESObjectREFR*>("TransmitPowerInSelection", pluginName, TransmitPowerInSelection, vm));
	vm->RegisterFunction(new LatentNativeFunction2<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32>("PastePatternObjects", pluginName, PastePatternObjects, vm));
	vm->RegisterFunction(new LatentNativeFunction2<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32>("PastePatternWires", pluginName, PastePatternWires, vm));

	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR* >("UpdateSelectedWires", pluginName, UpdateSelectedWires_, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, SelectionDetails, TESObjectREFR* >("GetSelectionDetails", pluginName, GetSelectionDetails, vm));
	vm->RegisterFunction(new NativeFunction4<StaticFunctionTag, void, TESObjectREFR*, float, float, float >("RotateSelectionZ", pluginName, RotateSelectionZ, vm));
	vm->RegisterFunction(new NativeFunction4<StaticFunctionTag, void, TESObjectREFR*, float, float, float >("MoveSelection", pluginName, MoveSelection, vm));

	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, UInt32, TESObjectREFR*, TESForm*>("ClearSelection", pluginName, ClearSelection, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, UInt32, TESObjectREFR*, VMArray<TESObjectREFR*>, TESForm* >("DeselectAll", pluginName, DeselectAll, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, bool, TESObjectREFR*, TESObjectREFR*, TESForm*>("Deselect", pluginName, Deselect, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, UInt32, TESObjectREFR*, VMArray<TESObjectREFR*>, TESForm*>("SelectAll", pluginName, SelectAll, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, bool, TESObjectREFR*, TESObjectREFR*, TESForm*>("Select", pluginName, Select, vm));

	vm->RegisterFunction(new NativeFunction4<StaticFunctionTag, PatternGeneralEntry, UInt32, BSFixedString, BSFixedString, TESObjectREFR* >("WritePatternFile", pluginName, WritePatternFile, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<BSFixedString>, TESObjectREFR* >("GetSelectedPlugins", pluginName, GetSelectedPlugins_, vm));
	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<BSFixedString>, TESObjectREFR*, VMArray<TESObjectREFR*> >("GetPlugins", pluginName, GetPlugins_, vm));
	
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR* >("GetSelectedObjectReferences", pluginName, GetSelectedObjectReferences, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, bool, TESObjectREFR* >("IsSelectionFullyLoaded", pluginName, IsSelectionFullyLoaded, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, UInt32, VMArray<TESObjectREFR*> >("GetFullyLoadedCount", pluginName, GetFullyLoadedCount, vm));
	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, UInt32, UInt32, UInt32 >("GetPatternCount", pluginName, GetPatternCount, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, UInt32, TESObjectREFR* >("GetSelectionCount", pluginName, GetSelectionCount, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, UInt32, TESObjectREFR* >("GetSelectionWireCount", pluginName, GetSelectionWireCount, vm));

	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<PatternObjectEntry>, UInt32 >("GetPatternObjects", pluginName, GetPatternObjects, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<PatternWireEntry>, UInt32 >("GetPatternWires", pluginName, GetPatternWires, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<BSFixedString>, UInt32 >("GetPatternPlugins", pluginName, GetPatternPlugins, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, PatternGeneralEntry, UInt32 >("GetPatternGeneralInformation", pluginName, GetPatternGeneralInformation, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, PatternReferenceEntry, UInt32 >("GetPatternReferenceInformation", pluginName, GetPatternReferenceInformation, vm));

	vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, VMArray<BGSConstructibleObject*>>("GetAllConstructibleObjects", pluginName, GetAllConstructibleObjects_, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, BGSConstructibleObject *, UInt32> ("GetConstructibleObjectByCreatedObject", pluginName, GetConstructibleObjectByCreatedObject_, vm));

	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<TESObjectREFR*>, VMArray<TESObjectREFR*>, TESObjectREFR*>("GetSelectableObjectPool", pluginName, GetSelectableObjectPool, vm));
	vm->RegisterFunction(new NativeFunction5<StaticFunctionTag, VMArray<TESObjectREFR*>, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32, UInt32, UInt32>("GetObjectsInBox", pluginName, GetObjectsInBox, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, VMArray<TESObjectREFR*>, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32>("GetObjectsInCylinder", pluginName, GetObjectsInCylinder, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, VMArray<TESObjectREFR*>, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32>("GetObjectsInSphere", pluginName, GetObjectsInSphere, vm));

	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, void, TESObjectREFR*, TESForm*>("ApplyShaderEffect", pluginName, ApplyShaderEffect, vm));
	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, void, TESObjectREFR*, TESForm*>("RemoveShaderEffect", pluginName, RemoveShaderEffect, vm));
	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, void, TESObjectREFR*, TESForm*>("ApplyShaderEffectToSelection", pluginName, ApplyShaderEffectToSelection, vm));
	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, void, TESObjectREFR*, TESForm*>("RemoveShaderEffectToSelection", pluginName, RemoveShaderEffectToSelection, vm));

	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, BSFixedString, BSFixedString, BSFixedString, BSFixedString >("GetSettingValueString", pluginName, GetSettingValueString, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, UInt32, BSFixedString, BSFixedString, UInt32>("GetSettingValueInt", pluginName, GetSettingValueInt, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, float, BSFixedString, BSFixedString, float >("GetSettingValueFloat", pluginName, GetSettingValueFloat, vm));
	vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, bool, BSFixedString, BSFixedString, bool >("GetSettingValueBool", pluginName, GetSettingValueBool, vm));
	vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, void>("ReloadSettings", pluginName, ReloadSettings, vm));

	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<ComponentEntry>, TESObjectREFR*>("GetSelectionComponentCost", pluginName, GetSelectionComponentCost, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<ComponentEntry>, UInt32>("GetPatternComponentCost", pluginName, GetPatternComponentCost, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<ComponentEntry>, VMArray<TESObjectREFR*>>("GetComponentCost", pluginName, GetComponentCost, vm));
		
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectCELL*>("GetObjectsByCell", pluginName, GetObjectsByCell, vm));
	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<TESObjectREFR*>, VMArray<TESObjectREFR*>, BSFixedString>("FilterObjectsByPlugin", pluginName, FilterObjectsByPlugin, vm));

	vm->SetFunctionFlags(pluginName, "CreateSelectionBoxFunctor", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "ScrapSelection", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "SendWorkshopEventToSelectedObjects", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "DisableObjects", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "EnableObjects", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "ScrapObjects", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "ScaleSelection", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "TransmitPowerInSelection", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "PastePatternObjects", IFunction::kFunctionFlag_NoWait);
	vm->SetFunctionFlags(pluginName, "PastePatternWires", IFunction::kFunctionFlag_NoWait);

	return true;
}

extern "C" {
	bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info) {
		// populate info structure
		info->infoVersion =	PluginInfo::kInfoVersion;
		info->name =		pluginName;
		info->version =		pluginVersion;

		if (f4se->runtimeVersion != RUNTIME_VERSION_1_10_98) {
			UInt32 runtimeVersion = RUNTIME_VERSION_1_10_98;
			char buf[512];
			sprintf_s(buf, "Clipboard\nExpected Version: %d.%d.%d.%d\nFound Version: %d.%d.%d.%d",
				GET_EXE_VERSION_MAJOR(runtimeVersion),
				GET_EXE_VERSION_MINOR(runtimeVersion),
				GET_EXE_VERSION_BUILD(runtimeVersion),
				GET_EXE_VERSION_SUB(runtimeVersion),
				GET_EXE_VERSION_MAJOR(f4se->runtimeVersion),
				GET_EXE_VERSION_MINOR(f4se->runtimeVersion),
				GET_EXE_VERSION_BUILD(f4se->runtimeVersion),
				GET_EXE_VERSION_SUB(f4se->runtimeVersion));
			MessageBox(NULL, buf, "Game Version Error", MB_OK | MB_ICONEXCLAMATION);
			_FATALERROR("unsupported runtime version %08X", f4se->runtimeVersion);
			return false;
		}

		// store plugin handle so we can identify ourselves later
		g_pluginHandle = f4se->GetPluginHandle();
		if(f4se->isEditor){
			_MESSAGE("loaded in editor, marking as incompatible");
			return false;
		}

		g_object = (F4SEObjectInterface *)f4se->QueryInterface(kInterface_Object);
		if (!g_object){
			_MESSAGE("couldn't get object interface");
			return false;
		}

		// get the papyrus interface and query its version
		g_papyrus = (F4SEPapyrusInterface *)f4se->QueryInterface(kInterface_Papyrus);
		if(!g_papyrus){
			_MESSAGE("couldn't get papyrus interface");
			return false;
		}

		if(g_papyrus->interfaceVersion < F4SEPapyrusInterface::kInterfaceVersion){
			_MESSAGE("papyrus interface too old (%d expected %d)", g_papyrus->interfaceVersion, F4SEPapyrusInterface::kInterfaceVersion);
			return false;
		}

		// get the papyrus interface and query its version
		g_messaging = (F4SEMessagingInterface *)f4se->QueryInterface(kInterface_Messaging);
		if(!g_messaging){
			_MESSAGE("couldn't get papyrus interface");
			return false;
		}

		if(g_messaging->interfaceVersion < F4SEMessagingInterface::kInterfaceVersion){
			_MESSAGE("messaging interface too old (%d expected %d)", g_messaging->interfaceVersion, F4SEMessagingInterface::kInterfaceVersion);
			return false;
		}

		g_task = (F4SETaskInterface *)f4se->QueryInterface(kInterface_Task);
		if (!g_task){
			_WARNING("couldn't get task interface");
		}
		// ### do not do anything else in this callback
		// ### only fill out PluginInfo and return true/false

		// supported runtime version
		_MESSAGE("%s query successful.", pluginName);
		return true;
	}

	bool F4SEPlugin_Load(const F4SEInterface * f4se){
		_MESSAGE("%s loading...", pluginName);
		g_papyrus->Register(RegisterFuncs);
		g_messaging->RegisterListener(g_pluginHandle, "F4SE", MessageCallback);
		_MESSAGE("%s load successful.", pluginName);
		return true;
	}
};