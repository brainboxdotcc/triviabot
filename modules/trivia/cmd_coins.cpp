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

command_coins_t::command_coins_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_coins_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	dpp::snowflake user_id = 0;
	tokens >> user_id;
	uint64_t balance = 0;
	if (!user_id) {
		user_id = cmd.author_id;
	}

	db::resultset coins = db::query("SELECT * FROM coins WHERE user_id = '?'", {user_id});
	if (coins.size()) {
        	balance = from_string<uint64_t>(coins[0]["balance"], std::dec);
	}

	std::string body = fmt::format(_("COINTOTAL", settings), balance) + "\n\n[" + _("SHOPURLTEXT", settings) + "](https://triviabot.co.uk/coinshop/)";
	creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "", body, cmd.channel_id, _("YOURWALLET", settings), "", "https://triviabot.co.uk/images/coin.gif");
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
