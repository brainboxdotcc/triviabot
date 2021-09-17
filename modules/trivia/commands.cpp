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
#include "state.h"
#include "trivia.h"
#include "webrequest.h"
#include "piglatin.h"
#include "commands.h"

using json = nlohmann::json;

in_cmd::in_cmd(const std::string &m, uint64_t author, uint64_t channel, uint64_t guild, bool mention, const std::string &_user, bool dashboard, dpp::user u, dpp::guild_member gm) : msg(m), author_id(author), channel_id(channel), guild_id(guild), mentions_bot(mention), from_dashboard(dashboard), username(_user), user(u), member(gm), interaction_token(""), command_id(0)
{
}

command_t::command_t(class TriviaModule* _creator, const std::string &_base_command, const std::string& descr, std::vector<dpp::command_option> options) : creator(_creator), base_command(_base_command), description(descr), opts(options)
{
}

command_t::~command_t()
{
}

std::string command_t::_(const std::string &str, const guild_settings_t &settings)
{
	return creator->_(str, settings);
}

/* Register slash commands and build the array of standard message commands */
void TriviaModule::SetupCommands()
{
	/* This is a list of commands which are handled by both the message based oldschool command
	 * handler and also via slash commands. Any entry here with an empty description is a message
	 * command alias, and won't be registered as a slash command (no point - we have tab completion there)
	 */
	commands = {
		{
			"start", new command_start_t(this, "start", "Start a game of trivia",
			{
				dpp::command_option(dpp::co_integer, "questions", "Number of questions", false),
				dpp::command_option(dpp::co_string, "categories", "Category list", false)	
			}
		)},
		{
			"quickfire", new command_start_t(this, "quickfire", "Start a quickfire game of trivia",
			{
				dpp::command_option(dpp::co_integer, "questions", "Number of questions", false),
				dpp::command_option(dpp::co_string, "categories", "Category list", false)	
			}
		)},
		{
			"qf", new command_start_t(this, "quickfire", "", {})
		},
		{
			"hardcore", new command_start_t(this, "hardcore", "Start a hardcore game of trivia",
			{
				dpp::command_option(dpp::co_integer, "questions", "Number of questions", false),
				dpp::command_option(dpp::co_string, "categories", "Category list", false)	
			}
		)},
		{
			"hc", new command_start_t(this, "hardcore", "", { })
		},
		{
			"trivia", new command_start_t(this, "start", "", { })
		},
		{
			"stop", new command_stop_t(this, "stop", "Stop a game of trivia",
			{
			}
		)},
		{"dashboard", new command_dashboard_t(this, "dashboard", "Show a link to the TriviaBot dashboard", { })},
		{"global", new command_global_t(this, "global", "Show a link to the global leaderboard", { })},
		{"dash", new command_dashboard_t(this, "dashboard", "", { } )},
		{"vote", new command_vote_t(this, "vote", "Information on how to vote for TriviaBot", { })},
		{"votehint", new command_votehint_t(this, "votehint", "Use a vote hint", { })},
		{"vh", new command_votehint_t(this, "votehint", "", { } )},
		{"stats", new command_stats_t(this, "stats", "Show today's scores", { })},
		{"leaderboard", new command_stats_t(this, "stats", "", { })},
		{"info", new command_info_t(this, "info", "Show information about TriviaBot", { })},
		{
			"join", new command_join_t(this, "join", "Join a Trivia team",
			{
				dpp::command_option(dpp::co_string, "team", "Team name to join", true)	
			}
		)},
		{
			"create", new command_create_t(this, "create", "Create a new Trivia team",
			{
				dpp::command_option(dpp::co_string, "team", "Team name to create", true)	
			}
		)},
		{"leave", new command_leave_t(this, "leave", "Leave your current Trivia Team", { })},
		{
			"help", new command_help_t(this, "help", "Show help for TriviaBot",
			{
				dpp::command_option(dpp::co_string, "topic", "Help topic", false),
			}
		)}
	};

	if (bot->GetClusterID() == 0) {

		/* Two lists, one for the main set of global commands, and one for admin commands */
		std::vector<dpp::slashcommand> slashcommands;
		std::vector<dpp::slashcommand> adminslash;

		/* Iterate the list and build dpp::slashcommand object vector */
		for (auto & c : commands) {
			if (!c.second->description.empty()) {
				dpp::slashcommand sc;
				sc.set_name(c.first).set_description(c.second->description).set_application_id(from_string<uint64_t>(bot->GetConfig("application_id"), std::dec));
				for (auto & o : c.second->opts) {
					sc.add_option(o);
				}
				slashcommands.push_back(sc);
			}
		}

		/* Add externals */
		DoExternalCommands(slashcommands, adminslash);

		bot->core->log(dpp::ll_info, fmt::format("Registering {} global and {} local slash commands", slashcommands.size(), adminslash.size()));

		/* Now register all the commands */
		if (bot->IsDevMode()) {
			/*
			 * Development mode - all commands are guild commands, and all are attached to the test server.
			 */
			std::copy(std::begin(adminslash), std::end(adminslash), std::back_inserter(slashcommands));
			bot->core->guild_bulk_command_create(slashcommands, from_string<uint64_t>(bot->GetConfig("test_server"), std::dec), [this](const dpp::confirmation_callback_t &callback) {
				if (callback.is_error()) {
					this->bot->core->log(dpp::ll_error, fmt::format("Failed to register guild slash commands (dev mode, main set): {}", callback.http_info.body));
				} else {
					dpp::slashcommand_map sm = std::get<dpp::slashcommand_map>(callback.value);
					this->bot->core->log(dpp::ll_info, fmt::format("Registered {} guild commands to test server", sm.size()));
				}

			});
		} else {
			/*
			 * Live mode/test mode - main set are global slash commands (with their related cache delay, ew)
			 * Admin set are guild commands attached to the support server.
			 */
			bot->core->global_bulk_command_create(slashcommands, [this](const dpp::confirmation_callback_t &callback) {
				if (callback.is_error()) {
					this->bot->core->log(dpp::ll_error, fmt::format("Failed to register global slash commands (live mode, main set): {}", callback.http_info.body));
				} else {
					dpp::slashcommand_map sm = std::get<dpp::slashcommand_map>(callback.value);
					this->bot->core->log(dpp::ll_info, fmt::format("Registered {} global commands", sm.size()));
				}
			});
			bot->core->guild_bulk_command_create(adminslash, from_string<uint64_t>(bot->GetConfig("home"), std::dec), [this](const dpp::confirmation_callback_t &callback) {
				if (callback.is_error()) {
					this->bot->core->log(dpp::ll_error, fmt::format("Failed to register guild slash commands (live mode, admin set): {}", callback.http_info.body));
				} else {
					dpp::slashcommand_map sm = std::get<dpp::slashcommand_map>(callback.value);
					this->bot->core->log(dpp::ll_info, fmt::format("Registered {} guild commands to support server", sm.size()));
				}
			});
		}

	}

	/* Hook interaction event */
	bot->core->on_interaction_create(std::bind(&TriviaModule::HandleInteraction, this, std::placeholders::_1));
}

void TriviaModule::HandleInteraction(const dpp::interaction_create_t& event) {
	dpp::command_interaction cmd_interaction = std::get<dpp::command_interaction>(event.command.data);

	std::stringstream message;
	/* Set 'thinking' state */
	dpp::message msg;
	msg.content = "*";
	msg.guild_id = event.command.guild_id;
	msg.channel_id = event.command.channel_id;
	bot->core->interaction_response_create(event.command.id, event.command.token, dpp::interaction_response(dpp::ir_deferred_channel_message_with_source, msg));

	message << cmd_interaction.name;
	for (auto & p : cmd_interaction.options) {
		if (std::holds_alternative<int32_t>(p.value)) {
			message << " " << std::get<int32_t>(p.value);
		} else if (std::holds_alternative<dpp::snowflake>(p.value)) {
			message << " " << std::get<dpp::snowflake>(p.value);
		} else if (std::holds_alternative<std::string>(p.value)) {
			message << " " << std::get<std::string>(p.value);
		}
	}

	in_cmd cmd(message.str(), event.command.usr.id, event.command.channel_id, event.command.guild_id, false, event.command.usr.username, false, event.command.usr, event.command.member);
	cmd.command_id = event.command.id;
	cmd.interaction_token = event.command.token;
	this->handle_command(cmd);
}

void TriviaModule::DoExternalCommands(std::vector<dpp::slashcommand>& normal, std::vector<dpp::slashcommand>& admin) {
	/* This adds commands to the slash command system that are external to the bot's executable */
	std::string path(getenv("HOME"));
	path += "/www/api/commands/commands.json";
	json commandlist;
	std::ifstream slashfile(path);
	slashfile >> commandlist;
	/* Iterate all commands */
	for (auto & command : commandlist) {
		dpp::slashcommand sc;
		sc.set_name(command["name"].get<std::string>()).set_description(command["description"].get<std::string>()).set_application_id(from_string<uint64_t>(bot->GetConfig("application_id"), std::dec));
		for (auto & o : command["params"]) {
			dpp::command_option_type cot = dpp::co_string;
			std::string stringtype = o["type"].get<std::string>();
			/* Transpose minimal supported set of types */
			if (stringtype == "number") {
				cot = dpp::co_integer;
			} else if (stringtype == "string") {
				cot = dpp::co_string;
			} else if (stringtype == "user") {
				cot = dpp::co_user;
			}
			dpp::command_option opt(cot, o["name"].get<std::string>(), o["description"].get<std::string>(), o["required"].get<bool>());
			sc.add_option(opt);
		}
		/* Add command to correct set */
		if (command["admin"].get<bool>() == true) {
			admin.push_back(sc);
		} else {
			normal.push_back(sc);
		}
	}
}

dpp::user dashboard_dummy;

void TriviaModule::handle_command(const in_cmd &cmd) {

	try {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == cmd.guild_id) {
	
			std::stringstream tokens(cmd.msg);
			std::string base_command;
			tokens >> base_command;
	
			dpp::channel* c = dpp::find_channel(cmd.channel_id);
			dpp::user* user = (dpp::user*)&cmd.user;
			if (cmd.from_dashboard) {
				dashboard_dummy.username = "Dashboard";
				dashboard_dummy.flags = 0;
				user = &dashboard_dummy;
			}
			if (!c) {
				return;
			}
	
			guild_settings_t settings = GetGuildSettings(cmd.guild_id);
	
			/* Check for moderator status - first check if owner */
			dpp::guild* g = dpp::find_guild(cmd.guild_id);
			bool moderator = (g && (cmd.from_dashboard || g->owner_id == cmd.author_id));
			/* Now iterate the list of moderator roles from settings */
			if (!moderator) {
				if (g) {
					for (auto modrole = settings.moderator_roles.begin(); modrole != settings.moderator_roles.end(); ++modrole) {
						/* Check for when user cache is off, and guild member passed in via the message */
						for (auto role = cmd.member.roles.begin(); role != cmd.member.roles.end(); ++role) {
							if (*role == *modrole) {
								moderator = true;
								break;
							}
						}

						if (moderator) {
							/* Short-circuit out of outer loop */
							break;
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
						SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), cmd.channel_id, _("WOAHTHERE", settings));
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
						CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
						custom_command(cmd.interaction_token, cmd.command_id, settings, this, base_command, trim(rest), cmd.author_id, cmd.channel_id, cmd.guild_id);
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
							SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), cmd.channel_id, _("WOAHTHERE", settings));
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
void TriviaModule::GetHelp(const std::string& interaction_token, dpp::snowflake command_id, const std::string &section, dpp::snowflake channelID, const std::string &botusername, dpp::snowflake botid, const std::string &author, dpp::snowflake authorid, const guild_settings_t &settings)
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

	ProcessEmbed(interaction_token, command_id, settings, json, channelID);
}

