/************************************************************************************
 * 
 * Sporks, the learning, scriptable Discord bot!
 *
 * Copyright 2019 Craig Edwards <support@sporks.gg>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/

#include <sporks/bot.h>
#include <sporks/modules.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/database.h>

/**
 * Updates presence and counters on a schedule
 */

class PresenceModule : public Module
{
	uint64_t halfminutes;
public:
	PresenceModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), halfminutes(0)
	{
		ml->Attach({ I_OnPresenceUpdate }, this);
	}

	virtual ~PresenceModule()
	{
	}

	virtual std::string GetVersion()
	{
		/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
		std::string version = "$ModVer 10$";
		return "1.0." + version.substr(8,version.length() - 9);
	}

	virtual std::string GetDescription()
	{
		return "Updates presence and stats counters";
	}

	int64_t GetRSS() {
		int64_t ram = 0;
		std::ifstream self_status("/proc/self/status");
		while (self_status) {
			std::string token;
			self_status >> token;
			if (token == "VmRSS:") {
				self_status >> ram;
				break;
			}
		}
		self_status.close();
		return ram;
	}

	virtual bool OnPresenceUpdate()
	{
		int64_t users = 0, channel_count = 0, servers = 0;
		for (auto & s : bot->core->get_shards()) {
			users += s.second->get_member_count();
			channel_count += s.second->get_channel_count();
			servers += s.second->get_guild_count();
		}
		int64_t ram = GetRSS();
		int64_t games = bot->counters.find("activegames") != bot->counters.end() ?  bot->counters["activegames"] : 0;

		db::backgroundquery("INSERT INTO infobot_discord_counts (shard_id, cluster_id, dev, user_count, server_count, shard_count, channel_count, sent_messages, received_messages, memory_usage, games) VALUES(?,?,?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE user_count = ?, server_count = ?, shard_count = ?, channel_count = ?, sent_messages = ?, received_messages = ?, memory_usage = ?, games = ?",
			{
				0, bot->GetClusterID(), bot->IsDevMode(), users, servers, bot->core->get_shards().size(),
				channel_count, bot->sent_messages, bot->received_messages, ram, games,
				users, servers, bot->core->get_shards().size(),
				channel_count, bot->sent_messages, bot->received_messages, ram, games
			}
		);
		if (++halfminutes > 20) {
			/* Reset counters every 10 mins. Chewey stats uses these counters and expects this */
			halfminutes = bot->sent_messages = bot->received_messages = 0;
		}		
		return true;
	}
};

ENTRYPOINT(PresenceModule);

