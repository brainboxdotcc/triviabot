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

command_reassignpremium_t::command_reassignpremium_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_reassignpremium_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	dpp::snowflake user_id, guild_id;
	tokens >> user_id >> guild_id;

	db::resultset access = db::query("SELECT * FROM trivia_access WHERE user_id = '?' AND enabled = 1", {cmd.author_id});

	if (access.size() && guild_id && user_id) {
		db::resultset user = db::query("SELECT * FROM premium_credits WHERE user_id = ? ORDER BY active DESC, since DESC", {user_id});
		if (user.size()) {
			std::string notice;
			if (user.size() > 1) {
				notice = "\n\n**WARNING**: __Multiple subscriptions for this user__. The most recent active subscription was updated.";
			}
			if (user[0]["active"] == "0") {
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "Premium subscription is not active!", cmd.channel_id);
				return;
			}
			db::resultset oldprem = db::query("SELECT * FROM trivia_guild_cache WHERE snowflake_id = ?", {user[0]["guild_id"]});
			db::resultset newprem = db::query("SELECT * FROM trivia_guild_cache WHERE snowflake_id = ?", {guild_id});
			if (newprem.size()) {
				std::string newname = newprem[0]["name"];
				std::string oldname = (oldprem.size() ? oldprem[0]["name"] : "<not assigned>");
				db::query("CALL assign_premium(?, ?)", {user_id, guild_id});
				creator->SimpleEmbed(
					cmd.interaction_token,
					cmd.command_id, settings,
					":white_check_mark:",
					fmt::format("Premium subscription {} reassigned to guild {}\n**Old** guild name: {}\n**New** guild name: {}{}", user[0]["subscription_id"], guild_id, oldname, newname, notice),
					cmd.channel_id
				);
			} else {
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "Guild to move to is not visible to the bot yet. Do a command first.", cmd.channel_id);		
			}
		} else {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "This user does not have TriviaBot Premium!", cmd.channel_id);	
		}
	} else {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "This command is for the TriviaBot team only", cmd.channel_id);
	}
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
