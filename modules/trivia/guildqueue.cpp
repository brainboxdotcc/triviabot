/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004 Craig Edwards <support@brainbox.cc>
 *
 * Core based on Sporks, the Learning Discord Bot, Craig Edwards (c) 2019.
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

#include <sporks/modules.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "trivia.h"

void TriviaModule::ProcessGuildQueue()
{
	while (!terminating) {
		std::vector<guild_cache_queued_t> to_process;
		{

			std::lock_guard<std::mutex> guild_queue_lock(guildqueuemutex);
			if (guilds_to_update.size()) {
				for (auto& g : guilds_to_update) {
					to_process.push_back(g);
				}
				guilds_to_update.clear();
			}
		}
		if (to_process.size() > 0) {
			bot->core.log->debug("Processing guild queue of {} entries", to_process.size());
			for (auto& g : to_process) {
				db::query("INSERT INTO trivia_guild_cache (snowflake_id, name, icon, owner_id) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE name = '?', icon = '?', owner_id = '?', kicked = 0", {g.guild_id, g.name, g.icon,  g.owner_id, g.name, g.icon,  g.owner_id});
			}
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}
