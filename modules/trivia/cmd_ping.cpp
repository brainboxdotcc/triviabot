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

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include <sporks/database.h>
#include "state.h"
#include "trivia.h"
#include "webrequest.h"
#include "commands.h"

command_ping_t::command_ping_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_ping_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	/* Get REST and DB ping times. REST time is given to us by D++, get simple DB time by timing a query */
	dpp::cluster* cluster = this->creator->GetBot()->core;
	double discord_api_ping = cluster->rest_ping * 1000;
	double db_start = dpp::utility::time_f();
	db::query("SHOW TABLES", {}, [this, cmd, settings, cluster, discord_api_ping, db_start](db::resultset q) {
		double db_ping = (dpp::utility::time_f() - db_start) * 1000;
		double rq_start = dpp::utility::time_f();

		/* Get TriviaBot API time by timing an arbitrary request */
		cluster->request(std::string(creator->GetBot()->IsDevMode() ? BACKEND_HOST_DEV : BACKEND_HOST_LIVE) + "/api/", dpp::m_get, [cluster, discord_api_ping, db_ping, rq_start, this, cmd, settings](auto callback) {
			double tb_api_ping = (dpp::utility::time_f() - rq_start) * 1000;
			/* Get shards from database, as we can't directly see shards on other clusters */
			db::query("SELECT * FROM infobot_shard_status ORDER BY cluster_id, id", {}, [this, cmd, settings, cluster, tb_api_ping, db_ping, discord_api_ping](db::resultset shardq) {
				bool shardstatus = true;
				long lastcluster = -1;
				std::vector<field_t> fields = {
					{_("DISCPING", settings), fmt::format("{:.02f} ms\n<:blank:667278047006949386>", discord_api_ping), true },
					{_("APIPING", settings), fmt::format("{:.02f} ms\n<:blank:667278047006949386>", tb_api_ping), true },
					{_("DBPING", settings), fmt::format("{:.02f} ms\n<:blank:667278047006949386>", db_ping), true },
				};
				field_t f;
				std::string desc;
				for (auto & shard : shardq) {
					/* Arrange shards by cluster, each cluster in an embed field */
					if (lastcluster != std::stol(shard["cluster_id"])) {
						if (lastcluster != -1) {
							fields.push_back(f);
						}
						f = { _("CLUSTER", settings) + " " + shard["cluster_id"], "", true };
					}
					lastcluster = std::stol(shard["cluster_id"]);
					/* Green circle: Shard UP
					* Wrench emoji: Shard down for less than 15 mins; Under maintainence
					* Red circle: Shard down over 15 mins; DOWN
					*/
					f.value += "`" + fmt::format("{:02d}", std::stoul(shard["id"])) + "`: " + (shard["connected"] == "1" && shard["online"] == "1" ? ":green_circle: ": (shard["down_since"].empty() && time(nullptr) - stoull(shard["down_since"]) > 60 * 15 ? ":red_circle: " : "<:wrench:546395191892901909> ")) + "\n";
					if (shard["connected"] == "0" || shard["online"] == "0") {
						shardstatus = false;
					}
				}
				fields.push_back(f);
				creator->EmbedWithFields(
					cmd.interaction_token, cmd.command_id, settings,
					_("PONG", settings), fields, cmd.channel_id,
					"https://triviabot.co.uk/", "", "",
					":ping_pong: " + ((shardstatus == true && tb_api_ping < 200 && discord_api_ping < 800 && db_ping < 3 ? _("OKPING", settings) : _("BADPING", settings))) + "\n\n**" + _("PINGKEY", settings) + "**\n<:blank:667278047006949386>"
				);
			});
		});
	});
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
