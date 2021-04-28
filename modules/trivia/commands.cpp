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
#include <nlohmann/json.hpp>
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
#include "piglatin.h"
#include "commands.h"

using json = nlohmann::json;

in_cmd::in_cmd(const std::string &m, int64_t author, int64_t channel, int64_t guild, bool mention, const std::string &user, bool dashboard) : msg(m), author_id(author), channel_id(channel), guild_id(guild), mentions_bot(mention), from_dashboard(dashboard), username(user)
{
}

command_t::command_t(class TriviaModule* _creator, const std::string &_base_command) : creator(_creator), base_command(_base_command)
{
}

command_t::~command_t()
{
}

std::string command_t::_(const std::string &str, const guild_settings_t &settings)
{
	return creator->_(str, settings);
}

void TriviaModule::SetupCommands()
{
	commands = {
		{"start", new command_start_t(this, "start")},
		{"quickfire", new command_start_t(this, "quickfire")},
		{"qf", new command_start_t(this, "quickfire")},
		{"hardcore", new command_start_t(this, "hardcore")},
		{"hc", new command_start_t(this, "hardcore")},
		{"trivia", new command_start_t(this, "start")},
		{"stop", new command_stop_t(this, "stop")},
		{"vote", new command_vote_t(this, "vote")},
		{"votehint", new command_votehint_t(this, "votehint")},
		{"vh", new command_votehint_t(this, "vh")},
		{"stats", new command_stats_t(this, "stats")},
		{"leaderboard", new command_stats_t(this, "stats")},
		{"info", new command_info_t(this, "info")},
		{"join", new command_join_t(this, "join")},
		{"create", new command_create_t(this, "create")},
		{"leave", new command_leave_t(this, "leave")},
		{"help", new command_help_t(this, "help")}
	};
}

void TriviaModule::handle_command(const in_cmd &cmd) {

	try {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == cmd.guild_id) {
	
			std::stringstream tokens(cmd.msg);
			std::string base_command;
			tokens >> base_command;
	
			dpp::channel* c = dpp::find_channel(cmd.channel_id);
			dpp::user* user = dpp::find_user(cmd.author_id);
			if (!c || !user) {
				return;
			}
	
			guild_settings_t settings = GetGuildSettings(cmd.guild_id);
	
			/* Check for moderator status - first check if owner */
			dpp::guild* g = dpp::find_guild(cmd.guild_id);
			bool moderator = (g && g->owner_id == cmd.author_id);
			/* Now iterate the list of moderator roles from settings */
			if (!moderator) {
				if (g) {
					for (auto x = settings.moderator_roles.begin(); x != settings.moderator_roles.end(); ++x) {
						auto i = g->members.find(cmd.author_id);
						if (i != g->members.end()) {
							for (auto y = i->second->roles.begin(); y != i->second->roles.end(); ++y) {
								if (*y == *x) {
									moderator = true;
									break;
								}
							}
						}
					}
				}
			}
	
			base_command = lowercase(base_command);
	
			/* Support for old-style commands e.g. !trivia start instead of !start */
			if (base_command == "trivia") {
				tokens >> base_command;
				base_command = lowercase(base_command);
			}
	
			auto command = commands.find(base_command);
			if (command != commands.end()) {

				bool can_execute = false;
				auto command = commands.find(base_command);
				if (command != commands.end()) {
					std::lock_guard<std::mutex> cmd_lock(cmdmutex);
					auto check = limits.find(cmd.channel_id);
					if (check == limits.end()) {
						can_execute = true;
						limits[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
					} else if (time(NULL) > check->second) {
						can_execute = true;
						limits[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
					}
				}

				if (can_execute || cmd.from_dashboard) {
					bot->core->log(dpp::ll_debug, fmt::format("command_t '{}' routed to handler", base_command));
					command->second->call(cmd, tokens, settings, cmd.username, moderator, c, user);
				} else {
					/* Display rate limit message, but only one per rate limit period */
					bool emit_rl_warning = false;
					std::lock_guard<std::mutex> cmd_lock(cmdmutex);

					auto check = last_rl_warning.find(cmd.channel_id);
					if (check == last_rl_warning.end()) {
						emit_rl_warning = true;
						last_rl_warning[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
					} else if (time(NULL) > check->second) {
						emit_rl_warning = true;
						last_rl_warning[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
					}
					if (emit_rl_warning) {
						SimpleEmbed(settings, ":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), cmd.channel_id, _("WOAHTHERE", settings));
					}
					bot->core->log(dpp::ll_debug, fmt::format("command_t '{}' NOT routed to handler on channel {}, limiting", base_command, cmd.channel_id));
				}
			} else {
				/* Custom commands handled completely by the API as a REST call */
				bool command_exists = false;
				{
					std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
					command_exists = (std::find(api_commands.begin(), api_commands.end(), trim(lowercase(base_command))) != api_commands.end());
				}
				if (command_exists) {
					bool can_execute = false;
					{
						std::lock_guard<std::mutex> cmd_lock(cmdmutex);
						auto check = limits.find(cmd.channel_id);
						if (check == limits.end()) {
							can_execute = true;
							limits[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
						} else if (time(NULL) > check->second) {
							can_execute = true;
							limits[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
						}
					}

					if (can_execute) {
						std::string rest;
						std::getline(tokens, rest);
						rest = trim(rest);
						CacheUser(cmd.author_id, cmd.channel_id);
						std::string reply = trim(custom_command(base_command, trim(rest), cmd.author_id, cmd.channel_id, cmd.guild_id));
						if (!reply.empty()) {
							ProcessEmbed(settings, reply, cmd.channel_id);
						}
					} else {
						/* Display rate limit message, but only one per rate limit period */
						bool emit_rl_warning = false;
						std::lock_guard<std::mutex> cmd_lock(cmdmutex);

						auto check = last_rl_warning.find(cmd.channel_id);
						if (check == last_rl_warning.end()) {
							emit_rl_warning = true;
							last_rl_warning[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
						} else if (time(NULL) > check->second) {
							emit_rl_warning = true;
							last_rl_warning[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
						}
						if (emit_rl_warning) {
							SimpleEmbed(settings, ":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), cmd.channel_id, _("WOAHTHERE", settings));
						}

						bot->core->log(dpp::ll_debug, fmt::format("Command '{}' not sent to API, rate limited", trim(lowercase(base_command))));
					}
				} else {
					bot->core->log(dpp::ll_debug, fmt::format("Command '{}' not known to API", trim(lowercase(base_command))));
				}
			}
		} else {
			bot->core->log(dpp::ll_debug, fmt::format("Dropped command {} due to test mode", cmd.msg));
		}
	}
	catch (std::exception &e) {
		bot->core->log(dpp::ll_debug, fmt::format("command_t exception! - {}", e.what()));
	}
	catch (...) {
		bot->core->log(dpp::ll_debug, "command_t exception! - non-object");
	}
}

/**
 * Emit help using a json file in the help/ directory. Missing help files emit a generic error message.
 */
void TriviaModule::GetHelp(const std::string &section, dpp::snowflake channelID, const std::string &botusername, dpp::snowflake botid, const std::string &author, dpp::snowflake authorid, const guild_settings_t &settings)
{
	json embed_json;
	char timestamp[256];
	time_t timeval = time(NULL);
	int32_t colour = settings.embedcolour;

	std::ifstream t("../help/" + settings.language + "/" + (section.empty() ? "basic" : section) + ".json");
	if (!t) {
		t = std::ifstream("../help/" + settings.language + "/error.json");
	}
	std::string json((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());

	tm _tm;
	gmtime_r(&timeval, &_tm);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &_tm);

	json = ReplaceString(json, ":section:" , section);
	json = ReplaceString(json, ":user:", botusername);
	json = ReplaceString(json, ":id:", std::to_string(botid));
	json = ReplaceString(json, ":author:", author);
	json = ReplaceString(json, ":ts:", timestamp);
	json = ReplaceString(json, ":color:", std::to_string(colour));

	try {
		embed_json = json::parse(json);
	}
	catch (const std::exception &e) {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == settings.guild_id) {
			bot->core->message_create(dpp::message(channelID, fmt::format(_("HERPDERP", settings), authorid)));
			bot->sent_messages++;
		}
		bot->core->log(dpp::ll_error, fmt::format("Malformed help file {}.json!", section));
		return;
	}

	if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == settings.guild_id) {
		bot->core->message_create(dpp::message(channelID, dpp::embed(&embed_json)));
		bot->sent_messages++;
	}
}

