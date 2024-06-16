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

command_profile_t::command_profile_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_profile_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	dpp::snowflake user_id = 0;
	tokens >> user_id;
	if (!user_id) {
		user_id = cmd.author_id;
	}

	db::resultset _user = db::query("SELECT *, get_all_emojis(snowflake_id) as emojis FROM trivia_user_cache WHERE snowflake_id = '?'", {user_id});
	if (_user.size()) {
		std::string a;
		for (auto& ach : *(creator->achievements)) {
			db::resultset inf = db::query("SELECT *, date_format(unlocked, '%d-%b-%Y') AS unlocked_friendly FROM achievements WHERE user_id = ? AND achievement_id = ?", {user_id, ach["id"].get<uint32_t>()});
			if (inf.size()) {
				a += "<:" + ach["image"].get<std::string>() + ":" + ach["emoji_unlocked"].get<std::string>() + ">";
			}
		}

		db::resultset srs = db::query("SELECT * FROM global_scores WHERE name = '?'", {user_id});
		uint64_t lifetime = 0;
		uint64_t weekly = 0;
		uint64_t daily = 0;
		uint64_t monthly = 0;
		if (srs.size()) {
			lifetime = from_string<uint64_t>(srs[0]["score"], std::dec);
			weekly = from_string<uint64_t>(srs[0]["weekscore"], std::dec);
			daily = from_string<uint64_t>(srs[0]["dayscore"], std::dec);
			monthly = from_string<uint64_t>(srs[0]["monthscore"], std::dec);
		}
		std::string dl = fmt::format("{:32s}{:8d}", _("DAILY", settings), daily);
		std::string wl = fmt::format("{:32s}{:8d}", _("WEEKLY", settings), weekly);
		std::string ml = fmt::format("{:32s}{:8d}", _("MONTHLY", settings), monthly);
		std::string ll = fmt::format("{:32s}{:8d}", _("LIFETIME", settings), lifetime);

		std::string scores = "```" + dl + "\n" + wl + "\n" + ml + "\n" + ll + "```";

		a += BLANK_EMOJI;
		std::string emojis = _user[0]["emojis"] + BLANK_EMOJI;

		creator->EmbedWithFields(
			cmd.interaction_token, cmd.command_id, settings,
			fmt::format("{0} {1}", _user[0]["username"], _("PROFILETITLE", settings)),
			{
				{ _("BADGES", settings), emojis, true },
				{ _("ACHIEVEMENTS", settings), a, true },
				{ BLANK_EMOJI, scores, false }
			}, cmd.channel_id, "https://triviabot.co.uk/profile" + std::to_string(user_id), "",
			"https://triviabot.co.uk/images/busts.png",
			"[" + _("CLICKHEREPROFILE", settings) + "](https://triviabot.co.uk/profile/" + std::to_string(user_id) + ")"
		);

		creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
	} else {
		creator->GetBot()->core->log(dpp::ll_warning, fmt::format("profile: No such user: {}", user_id));
	}

}
