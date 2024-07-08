/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <algorithm>
#include <locale>
#include <string>
#include <vector>
#include <cctype>

#include "DamageArrayHandler.h"
#include "Game/GameVersion.h"
#include "Lua/LuaParser.h"
#include "System/creg/STL_Map.h"
#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "System/StringUtil.h"

#include "System/Misc/TracyDefs.h"

CR_BIND(CDamageArrayHandler, )

CR_REG_METADATA(CDamageArrayHandler, (
	CR_MEMBER(armorDefNameIdxMap),
	CR_MEMBER(armorDefKeys)
))


CDamageArrayHandler damageArrayHandler;
static constexpr int DEFAULT_ARMOR_ID = 0;

void CDamageArrayHandler::Init(std::vector <UnitDef> & unitDefs)
{
	RECOIL_DETAILED_TRACY_ZONE;

	armorDefNameIdxMap.clear();
	armorDefNameIdxMap.insert({"default", DEFAULT_ARMOR_ID});

	int armorID = 1;
	for (auto &unitDef : unitDefs) {
		if (unitDef.armorName == "") {
			unitDef.armorType = DEFAULT_ARMOR_ID;
			continue;
		}

		const auto [it, inserted] = armorDefNameIdxMap.insert({unitDef.armorName, armorID});
		if (inserted)
			++ armorID;
		unitDef.armorType = it->second;
	}

	LOG_L(L_INFO, "number of ArmorDefs: " _STPF_, __FUNCTION__, armorDefNameIdxMap.size());
	for (const auto &unitDef : unitDefs)
		LOG_L(L_DEBUG, "armortype for \"%s\": \"%s\" -> %d", unitDef.name, unitDef.armorName, unitDef.armorType);
}



int CDamageArrayHandler::GetTypeFromName(const std::string& name) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto it = armorDefNameIdxMap.find(StringToLower(name));

	if (it != armorDefNameIdxMap.end())
		return it->second;

	return 0; // 'default' armor index
}

