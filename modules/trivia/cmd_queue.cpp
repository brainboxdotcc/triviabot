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

#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <sporks/regex.h>
#include <string>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "trivia.h"
#include "commands.h"

using json = nlohmann::json;

command_queue_t::command_queue_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_queue_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	db::resultset access = db::query("SELECT * FROM trivia_access WHERE user_id = '?' AND enabled = 1", {cmd.author_id});

	if (access.size()) {
		db::resultset feedback = db::query("SELECT COUNT(*) AS total FROM feedback WHERE closed_by IS NULL", {});
		db::resultset questions = db::query("SELECT COUNT(*) AS total FROM question_queue", {});

		std::vector<field_t> fields = {
			{ "Total Questions", questions[0]["total"], false },
			{ "Total Open Feedback", feedback[0]["total"], false }
		};
		creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, "", fields, cmd.channel_id, "", "", "https://triviabot.co.uk/images/mortar.png", "Queue information\n[Bot moderation dashboard](https://triviabot.co.uk/admin/)");
	} else {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "This command is for the TriviaBot team only", cmd.channel_id);
	}
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
