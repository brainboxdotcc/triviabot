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
#include <dpp/nlohmann/json.hpp>
#include "state.h"
#include "trivia.h"
#include "webrequest.h"
#include "commands.h"

using json = nlohmann::json;

command_achievements_t::command_achievements_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_achievements_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	dpp::snowflake user_id = 0;
	tokens >> user_id;
	if (!user_id) {
		user_id = cmd.author_id;
	}


	/* SQL: This is safe as all values are numeric from the database */
	std::string in_q;
	for (auto& showoff : *(creator->achievements)) {
		in_q += showoff["id"].get<std::string>() + ",";
	}
	if (in_q.length() > 0) {
		in_q = in_q.substr(0, in_q.length() - 1);
	}

	db::query("SELECT *, date_format(unlocked, '%d-%b-%Y') AS unlocked_friendly FROM achievements WHERE user_id = ? AND achievement_id IN (" + in_q + ")", {user_id}, [cmd, this, settings](db::resultset r) {
		
		uint32_t unlock_count = r.size();
		std::string trophies = fmt::format(_("ACHCOUNT", settings), unlock_count, creator->achievements->size() - unlock_count) + "\n";
		std::vector<field_t> fields;

		for (auto& inf : r) {
			auto & showoff = *(creator->achievements);
			for (auto& s : *(creator->achievements)) {
				if (s["id"].get<std::string>() == inf["achievement_id"]) {
					showoff = s;
					break;
				}
			}
			fields.push_back({
				"<:" + showoff["image"].get<std::string>() + ":" + showoff["emoji_unlocked"].get<std::string>() + "> - " + _(showoff["name"].get<std::string>(), settings),
				_(showoff["desc"].get<std::string>(), settings) + " (*" + inf["unlocked_friendly"] + "*)\n<:blank:667278047006949386>",
				false
			});
		}
		creator->EmbedWithFields(
			cmd.interaction_token, cmd.command_id, settings,
			_("TROPHYCABINET", settings), fields, cmd.channel_id,
			"https://triviabot.co.uk/", "", "",
			"<:blank:667278047006949386>\n" + trophies + "<:blank:667278047006949386>"
		);
	});
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
