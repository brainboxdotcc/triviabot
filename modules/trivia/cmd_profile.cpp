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

	db::query("SELECT *, get_all_emojis(snowflake_id) as emojis FROM trivia_user_cache WHERE snowflake_id = '?'", {user_id}, [this, cmd, settings, user_id](db::resultset _user) {
		if (_user.size()) {
			db::query("SELECT *, date_format(unlocked, '%d-%b-%Y') AS unlocked_friendly FROM achievements WHERE user_id = ?", {user_id}, [this, cmd, settings, user_id, _user](db::resultset inf) {
				std::string oa;
				for (auto & ac : inf) {
					for (auto& ach : *(creator->achievements)) {
						if (ach["id"].get<std::string>() == ac["achievement_id"]) {
							oa += "<:" + ach["image"].get<std::string>() + ":" + ach["emoji_unlocked"].get<std::string>() + ">";
						}
					}
				}

				db::query("SELECT SUM(score) AS score, SUM(weekscore) AS weekscore, SUM(dayscore) AS dayscore, SUM(monthscore) AS score FROM scores WHERE name = '?'", {user_id}, [this, cmd, settings, user_id, _user, oa](db::resultset sc) {

					std::string a = oa;
					db::row _this_user = _user[0];
					uint64_t lifetime = from_string<uint64_t>(sc[0]["score"], std::dec);
					uint64_t weekly = from_string<uint64_t>(sc[0]["weekscore"], std::dec);
					uint64_t daily = from_string<uint64_t>(sc[0]["dayscore"], std::dec);
					uint64_t monthly = from_string<uint64_t>(sc[0]["monthscore"], std::dec);

					std::string dl = fmt::format("{:32s}{:8d}", _("DAILY", settings), daily);
					std::string wl = fmt::format("{:32s}{:8d}", _("WEEKLY", settings), weekly);
					std::string ml = fmt::format("{:32s}{:8d}", _("MONTHLY", settings), monthly);
					std::string ll = fmt::format("{:32s}{:8d}", _("MONTHLY", settings), lifetime);

					std::string scores = "```" + dl + "\n" + wl + "\n" + ml + "\n" + ll + "```";

					a += "<:blank:667278047006949386>";
					std::string emojis = _this_user["emojis"] + "<:blank:667278047006949386>";

					creator->EmbedWithFields(
						cmd.interaction_token, cmd.command_id, settings,
						fmt::format("{0}#{1:04d} {2}", _this_user["username"], from_string<uint32_t>(_this_user["discriminator"], std::dec), _("PROFILETITLE", settings)),
						{
							{ _("BADGES", settings), emojis, true },
							{ _("ACHIEVEMENTS", settings), a, true },
							{ "<:blank:667278047006949386>", scores, false }
						}, cmd.channel_id, "https://triviabot.co.uk/profile" + std::to_string(user_id), "",
						"https://triviabot.co.uk/images/busts.png",
						"[" + _("CLICKHEREPROFILE", settings) + "](https://triviabot.co.uk/profile/" + std::to_string(user_id) + ")"
					);
				});
			});
		} else {
			creator->GetBot()->core->log(dpp::ll_warning, fmt::format("profile: No such user: {}", user_id));
		}
	});

	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
