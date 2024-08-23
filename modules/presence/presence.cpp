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
#include <fstream>
#include <sporks/database.h>

/**
 * Updates presence and counters on a schedule
 */

class PresenceModule : public Module
{
	uint64_t half_minutes{};

public:
	PresenceModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), half_minutes(0)
	{
		ml->Attach({ I_OnPresenceUpdate, I_OnGuildCreate, I_OnGuildDelete, I_OnGuildUpdate }, this);
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

	virtual bool OnGuildCreate(const dpp::guild_create_t &event)
	{
		if (event.created->is_unavailable()) {
			return true;
		}
		db::backgroundquery(
			"INSERT INTO guild_temp_cache (id, user_count) VALUES(?,?) ON DUPLICATE KEY UPDATE user_count = ?",
			{ event.created->id, event.created->member_count, event.created->member_count }
		);
		return true;
	}

	virtual bool OnGuildUpdate(const dpp::guild_update_t &gu)
	{
		db::backgroundquery("UPDATE guild_temp_cache SET user_count = ? WHERE id = ?", { gu.updated->member_count, gu.updated->id });
		return true;
	}

	virtual bool OnGuildDelete(const dpp::guild_delete_t &gd)
	{
		if (gd.deleted.is_unavailable()) {
			return true;
		}
		db::backgroundquery("DELETE FROM guild_temp_cache WHERE id = ?", { gd.deleted.id });
		return true;
	}


	virtual bool OnPresenceUpdate()
	{
		int64_t ram = GetRSS();
		int64_t games = bot->counters.find("activegames") != bot->counters.end() ?  bot->counters["activegames"] : 0;

		db::backgroundquery(
			"INSERT INTO infobot_discord_counts (shard_id, cluster_id, dev, server_count, user_count, shard_count, channel_count, sent_messages, received_messages, memory_usage, games) "
			"VALUES('?','?','?',(SELECT COUNT(id) FROM guild_temp_cache), (SELECT SUM(user_count) FROM guild_temp_cache) ,'?','?','?','?','?','?') "
			"ON DUPLICATE KEY UPDATE user_count = (SELECT SUM(user_count) FROM guild_temp_cache), server_count = (SELECT COUNT(id) FROM guild_temp_cache), "
			"shard_count = '?', channel_count = '?', sent_messages = '?', received_messages = '?', memory_usage = '?', games = '?'",
			{
				0, bot->GetClusterID(), bot->IsDevMode(), bot->core->get_shards().size(),
				0, bot->sent_messages, bot->received_messages, ram, games,
				bot->core->get_shards().size(),
				0, bot->sent_messages, bot->received_messages, ram, games
			}
		);
		if (++half_minutes > 20) {
			/* Reset counters every 10 minutes. Chewey stats uses these counters and expects this */
			half_minutes = bot->sent_messages = bot->received_messages = 0;
		}		
		return true;
	}
};

ENTRYPOINT(PresenceModule);

