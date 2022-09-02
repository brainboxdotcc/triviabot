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

command_subscription_t::command_subscription_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_subscription_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	dpp::snowflake user_id;
	tokens >> user_id;

	db::resultset access = db::query("SELECT * FROM trivia_access WHERE user_id = '?' AND enabled = 1", {cmd.author_id});

	if (access.size() && user_id) {
		db::resultset user = db::query("SELECT *, coalesce(date_format(since, '%d-%b-%Y %H:%i'), '<not set>') as hr_since, coalesce(date_format(cancel_date, '%d-%b-%Y %H:%i'), '<not set>') as hr_cancel_date, coalesce(date_format(payment_failed_date, '%d-%b-%Y %H:%i'), '<not set>') as hr_payment_failed_date, coalesce(date_format(manual_expiry_date, '%d-%b-%Y'), '<not set>') as hr_manual_expiry_date FROM premium_credits WHERE user_id = ?", {user_id});
		if (user.size()) {
			db::resultset premguild = db::query("SELECT * FROM trivia_guild_cache WHERE snowflake_id = ?", {user[0]["guild_id"]});
			std::vector<field_t> fields = {
				{ "Active", _((user[0]["active"] == "1" ? "TICKYES" : "CROSSNO"), settings), true },
				{ "Subscription ID", user[0]["subscription_id"] + BLANK_EMOJI, true },
				{ "Payment Has Failed", _((user[0]["payment_failed"] == "1" ? "TICKYES" : "CROSSNO"), settings), true },
				{ "Plan Type", ReplaceString(user[0]["plan_id"], "triviabot-premium-", "") + BLANK_EMOJI, true },
				{ "Guild", user[0]["guild_id"] + "\n" + (premguild.size() ? premguild[0]["name"] : "<not assigned>"), true },
				{ "Subscribed Since", user[0]["hr_since"] + BLANK_EMOJI, true },
				{ "Cancelled Date", user[0]["hr_cancel_date"] + BLANK_EMOJI, true },
				{ "Last Payment Failed Date", user[0]["hr_payment_failed_date"] + BLANK_EMOJI, true },
				{ "Manual Expiry Date", user[0]["hr_manual_expiry_date"] + BLANK_EMOJI, true },
			};
			creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, "Premium Subscription Details", fields, cmd.channel_id, "", "", "https://triviabot.co.uk/images/crown.png", "Subscription Information");
		} else {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "This user does not have TriviaBot Premium!", cmd.channel_id);	
		}
	} else {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", "This command is for the TriviaBot team only", cmd.channel_id);
	}
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
