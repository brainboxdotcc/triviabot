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

command_disable_t::command_disable_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_disable_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::string category_name;
	const int MAX_PERCENT_DISABLE = 75;
	const int MIN_QUESTIONS = 1000;
	std::string namefield = "name";

	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);

	std::getline(tokens, category_name);
	category_name = trim(category_name);
	if (settings.language != "en") {
		namefield = "trans_" + settings.language;
	}
	if (!is_moderator)  {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", _("MODONLY", settings), cmd.channel_id);
		return;
	}
	db::resultset cat = db::query("SELECT * FROM categories WHERE " + namefield + " = '?'", {category_name});
	if (!cat.size()) {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("NOSUCHCAT", settings), category_name), cmd.channel_id, _("CATERROR", settings));
		return;
	}

	db::query("INSERT INTO disabled_categories (guild_id, category_id) VALUES('?', '?')", {cmd.guild_id, cat[0]["id"]});
	db::resultset pd = db::query("SELECT count_remaining('?') AS remaining", {cmd.guild_id});
	int remaining = from_string<int>(pd[0]["remaining"], std::dec);
	if (remaining < MIN_QUESTIONS) {
		db::backgroundquery("DELETE FROM disabled_categories WHERE guild_id = ? AND category_id = ?", {cmd.guild_id, cat[0]["id"]});
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("TOOFEWCATS", settings), 100 - MAX_PERCENT_DISABLE), cmd.channel_id, _("CATERROR", settings));
		return;
	}

	creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":white_check_mark:", fmt::format(_("CATDISABLED", settings), cat[0][namefield]), cmd.channel_id, _("CATDONE", settings));
}
