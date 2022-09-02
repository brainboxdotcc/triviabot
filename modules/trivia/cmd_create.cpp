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

command_create_t::command_create_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_create_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::string newteamname;
	std::getline(tokens, newteamname);
	newteamname = trim(newteamname);
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
	std::string teamname = get_current_team(cmd.author_id);
	if (teamname.empty()) {
		newteamname = trim(ReplaceString(newteamname, "@", ""));
		/* Call Neutrino API to filter swearwords */
		creator->censor->contains_bad_word(newteamname, [cmd, settings, username, this, newteamname](const swear_filter_t& cc) {
			std::string cleaned_team_name = newteamname;
			if (!cc.clean) {
				cleaned_team_name = cc.censored_content;
			}
			auto rs = db::query("SELECT * FROM teams WHERE name = '?'", {cleaned_team_name});
			if (rs.empty()) {
				db::query("INSERT INTO teams (name, score) VALUES('?', 0)", {cleaned_team_name});
				try {
					join_team(cmd.author_id, cleaned_team_name, cmd.channel_id);
				}
				catch (const JoinNotQualifiedException& e) {
					creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("CANTCREATE", settings), username), cmd.channel_id);
					return;
				}
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":busts_in_silhouette:", fmt::format(_("CREATED", settings), cleaned_team_name, username), cmd.channel_id, _("ZELDAREFERENCE", settings));
			} else {
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("CANTCREATE", settings), username), cmd.channel_id);
			}
		});
	} else {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("ALREADYMEMBER", settings), username, teamname), cmd.channel_id);
	}
}

