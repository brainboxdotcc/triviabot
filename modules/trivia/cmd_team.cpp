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

command_team_t::command_team_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_team_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::string name;
	std::string desc;
	std::string url_key;
	uint32_t rank = 1;
	bool premium = false;

	std::getline(tokens, name);
	name = trim(name);

	db::resultset team = db::query("SELECT *, date_format(create_date, '%d-%b-%Y') as fmt_create_date, (SELECT COUNT(*) FROM team_membership WHERE team_membership.team = teams.name) AS mc FROM teams LEFT JOIN trivia_user_cache ON snowflake_id = owner_id WHERE teams.name = '?'", {name});

	if (team.size()) {
		db::resultset p = db::query("SELECT active FROM premium_credits WHERE user_id = '?' AND cancel_date IS NULL AND active = 1", {team[0]["owner_id"]});
		premium = (p.size() > 0);
		url_key = team[0]["url_key"];
		if (premium && !team[0]["team_url"].empty()) {
			url_key = team[0]["team_url"];
		}
	}

	if (team.size() == 0 || url_key.empty()) {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "", fmt::format(_("NOSUCHTEAM", settings), name), cmd.channel_id, _("NOTQUITERIGHT", settings));
		return;
	}

	db::resultset q = db::query("SELECT name FROM teams WHERE score >= ? ORDER BY score DESC", {team[0]["score"]});
	rank = q.size();

	if (!team[0]["description"].empty()) {
		desc = dpp::utility::utf8substr(team[0]["description"], 0, 932) + "\n" + BLANK_EMOJI + std::string("\n");
	}

	desc += fmt::format(_("TEAMHUB", settings), url_key) +  "\n" + BLANK_EMOJI;

	std::vector<field_t> fields = {
		{ _("NAME", settings), team[0]["name"], true},
		{ _("FOUNDER", settings), team[0]["username"], true},
		{ _("MEMBERCOUNT", settings), team[0]["mc"], true},
		{ _("POINTSTOTAL", settings), team[0]["score"], true},
		{ _("GLOBALRANK", settings), std::to_string(rank), true},
		{ _("DATEFOUNDED", settings), team[0]["fmt_create_date"], true}
	};
	creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, _("TINFO", settings), fields, cmd.channel_id, "https://triviabot.co.uk/team/" + url_key, team[0]["image_url"], "", dpp::utility::utf8substr(desc, 0, 2048));
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}

