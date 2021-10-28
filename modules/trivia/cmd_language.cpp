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
#include <dpp/nlohmann/json.hpp>
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

using json = nlohmann::json;

command_language_t::command_language_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_language_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::string lang_name;
	std::string namefield = "name";
	
	std::getline(tokens, lang_name);
	lang_name = trim(lang_name);

	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);

	if (lang_name.empty()) {
		db::resultset langs = db::query("SELECT * FROM languages WHERE live = 1 ORDER BY id", {});
		std::vector<field_t> fields;
		for (auto & row : langs) {
			field_t field;
			field.name = row["isocode"] + " " + row["emoji"];
			field.value = row["name"];
			field._inline = true;
			fields.push_back(field);
		}
		creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, _("SUPPORTEDLANGS", settings), fields, cmd.channel_id, "https://triviabot.co.uk", "", "", _("HOWTOCHANGE", settings));
		return;
	}

	if (!is_moderator)  {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", _("MODONLY", settings), cmd.channel_id);
		return;
	}

	db::resultset r = db::query("SELECT * FROM languages WHERE live = 1 AND isocode = '?' ORDER BY id", {lang_name});
	if (r.size() == 0) {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", _("BADLANG", settings), cmd.channel_id);
	} else {
		settings.language = lang_name;
		db::backgroundquery("UPDATE bot_guild_settings SET language = '?' WHERE snowflake_id = '?'", {lang_name, cmd.guild_id});
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":white_check_mark:", _("LANGCHANGE", settings), cmd.channel_id, _("CATDONE", settings));
	}

}
