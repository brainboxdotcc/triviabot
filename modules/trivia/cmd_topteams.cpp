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

#include <sporks/regex.h>
#include <string>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "trivia.h"
#include "commands.h"

command_topteams_t::command_topteams_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_topteams_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::string desc;
	uint8_t rank = 1;

	db::resultset q = db::query("SELECT * FROM teams ORDER BY score DESC LIMIT 10", {});
	for (auto & team : q) {
		desc += "**#" + std::to_string(rank) + "** `" + team["name"] + "` (*" + team["score"] + "*)";
		if (rank++ == 1) {
			desc += " <:crown:722808671888736306>";
		}
		desc += "\n";
	}

	creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "", desc, cmd.channel_id, _("TOP10TEAMS", settings));
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}

