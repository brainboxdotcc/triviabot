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

command_categories_t::command_categories_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_categories_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	uint32_t page = 0;

	tokens >> page;
	if (!page) {
		page = 1;
	}

	db::query("SELECT id FROM categories WHERE disabled != 1", {}, [cmd, this, settings, page](db::resultset counter) {
		size_t rows = counter.size();
		uint32_t start_record = (page - 1) * 25;
		uint32_t length = 25;
		uint32_t pages = ceil((float)rows / (float)length);
		db::query("SELECT categories.*, (SELECT COUNT(*) FROM questions WHERE questions.category = categories.id) AS total, IF(guild_id IS NULL, 0, 1) AS local_disabled FROM categories LEFT JOIN disabled_categories ON categories.id = disabled_categories.category_id AND guild_id = '?' WHERE disabled != 1 ORDER BY categories.name LIMIT ?, ?", {cmd.guild_id, start_record, length}, [cmd, this, settings, page, pages](db::resultset q) {
			std::string desc = fmt::format("{} {:9s}  {}\n-----------------------------------------\n", "Enabled", "Questions", "Name");
			std::string namefield = "name";
			if (settings.language != "en") {
				namefield = "trans_" + settings.language;
			}

			for (auto & cat : q) {
				desc += fmt::format("{} {:9s}  {}\n", cat["local_disabled"] == "1" ? "🔴    " : "🟢    ", cat["total"], cat[namefield]);
			}

			desc += "\n" + fmt::format(_("PAGES", settings), page, pages) + "\n";

			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "", "```\n" + desc + "\n```\n" + _("CATHINT", settings), cmd.channel_id, _("CATLIST", settings));

		});
	});

	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
