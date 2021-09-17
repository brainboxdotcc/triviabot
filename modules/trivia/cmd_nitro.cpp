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
#include <sporks/regex.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include <sporks/database.h>
#include "trivia.h"
#include "webrequest.h"
#include "commands.h"
#include <fmt/format.h>


command_nitro_t::command_nitro_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_nitro_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::string desc;
	uint8_t rank = 1;

	db::resultset q = db::query("SELECT vw_monthly_totals.*, trivia_user_cache.*, get_emojis(trivia_user_cache.snowflake_id) AS emojis FROM vw_monthly_totals INNER JOIN trivia_user_cache ON snowflake_id = name LEFT JOIN trivia_access ON name = trivia_access.user_id WHERE (trivia_access.id is null OR trivia_access.enabled = 0) AND (SELECT COUNT(*) FROM bans WHERE nitro_ban = 1 AND bans.snowflake_id = trivia_user_cache.snowflake_id) = 0 ORDER BY score DESC, trivia_user_cache.snowflake_id LIMIT 10", {});
	for (auto & user : q) {
		desc += "**#" + std::to_string(rank) + "** `" + user["username"] + "#" + fmt::format("{:04d}", from_string<int>(user["discriminator"], std::dec)) + "` (*" + user["score"] + "*) " + user["emojis"];
		if (rank++ == 1) {
			desc += " <:crown:722808671888736306>";
		}
		desc += "\n";
	}

	creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "", desc, cmd.channel_id, _("MONTHLYNITRO", settings));
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}

