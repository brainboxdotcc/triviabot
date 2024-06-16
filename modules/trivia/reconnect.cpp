/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004,2005,2020,2021,2024 Craig Edwards <support@brainbox.cc>
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

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <string>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "trivia.h"

/* Check for shards we have been asked to reconnect */
void TriviaModule::CheckReconnects() {
	db::resultset rs = db::query("SELECT * FROM infobot_shard_status WHERE forcereconnect = 1 AND cluster_id = ?", {bot->GetClusterID()});
	if (!rs.empty()) {
		for (auto r = rs.begin(); r != rs.end(); ++r) {
			try {
				bool reconnected = false;
				dpp::discord_client* s = bot->core->get_shard(from_string<uint32_t>((*r)["id"], std::dec));
				if (s) {
					bot->core->log(dpp::ll_info, fmt::format("Forced reconnection of shard {}", (*r)["id"]));
					db::backgroundquery("UPDATE infobot_shard_status SET reconnect_status = '?' WHERE id = ?", {std::string("Disconnecting..."), (*r)["id"]});
					s->close();
					db::backgroundquery("UPDATE infobot_shard_status SET reconnect_status = '?' WHERE id = ?", {std::string("Connecting..."), (*r)["id"]});
					reconnected = true;
				}
				if (!reconnected) {
					db::backgroundquery("UPDATE infobot_shard_status SET reconnect_status = '?' WHERE id = ?", {std::string("Can't find shard to reconnect it, please restart the cluster"), (*r)["id"]});
				} else {
					db::backgroundquery("UPDATE infobot_shard_status SET reconnect_status = '?' WHERE id = ?", {std::string("Shard reconnected"), (*r)["id"]});
				}
			}
			catch (...) {
				bot->core->log(dpp::ll_error, fmt::format("Unable to get shard {} to reconnect it! Something broked!", (*r)["id"]));
				db::backgroundquery("UPDATE infobot_shard_status SET reconnect_status = '?' WHERE id = ?", {std::string("Shard reconnection error, please restart the cluster"), (*r)["id"]});
			}
			db::backgroundquery("UPDATE infobot_shard_status SET forcereconnect = 0 WHERE id = '?'", {(*r)["id"]});
		}
	}
}

