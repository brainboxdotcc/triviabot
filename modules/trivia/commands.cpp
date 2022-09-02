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

command_t::command_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options, bool is_ephemeral, dpp::slashcommand_contextmenu_type command_type) : creator(_creator), base_command(_base_command), description(descr), opts(options), admin(adm), ephemeral(is_ephemeral), type(command_type)
{
}

command_t::~command_t()
{
}

std::string command_t::_(const std::string &str, const guild_settings_t &settings)
{
	return creator->_(str, settings);
}

void command_t::select_click(const dpp::select_click_t & event, const in_cmd &cmd, guild_settings_t &settings)
{
}

void command_t::button_click(const dpp::button_click_t & event, const in_cmd &cmd, guild_settings_t &settings)
{
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
			"start", new command_start_t(this, "start", false, "Start a game of trivia",
			{
				dpp::command_option(dpp::co_integer, "questions", "Number of questions", false),
				dpp::command_option(dpp::co_string, "categories", "Category list", false)	
			}
		)},
		{
			"quickfire", new command_start_t(this, "quickfire", false, "Start a quickfire game of trivia",
			{
				dpp::command_option(dpp::co_integer, "questions", "Number of questions", false),
				dpp::command_option(dpp::co_string, "categories", "Category list", false)	
			}
		)},
		{
			"qf", new command_start_t(this, "quickfire", false, "", {})
		},
		{
			"hardcore", new command_start_t(this, "hardcore", false, "Start a hardcore game of trivia",
			{
				dpp::command_option(dpp::co_integer, "questions", "Number of questions", false),
				dpp::command_option(dpp::co_string, "categories", "Category list", false)	
			}
		)},
		{
			"hc", new command_start_t(this, "hardcore", false, "", { })
		},
		{
			"trivia", new command_start_t(this, "start", false, "", { })
		},
		{
			"stop", new command_stop_t(this, "stop", false, "Stop a game of trivia",
			{
			}
		)},
		{"dashboard", new command_dashboard_t(this, "dashboard", false, "Show a link to the TriviaBot dashboard", { })},
		{"global", new command_global_t(this, "global", false, "Show a link to the global leaderboard", { })},
		{"dash", new command_dashboard_t(this, "dashboard", false, "", { } )},
		{"vote", new command_vote_t(this, "vote", false, "Information on how to vote for TriviaBot", { })},
		{"invite", new command_invite_t(this, "invite", false, "Show TriviaBot's invite link", { })},
		{"votehint", new command_votehint_t(this, "votehint", false, "Use a vote hint", { })},
		{"vh", new command_votehint_t(this, "votehint", false, "", { } )},
		{"stats", new command_stats_t(this, "stats", false, "Show today's scores", { })},
		{"topteams", new command_topteams_t(this, "topteams", false, "Show the leaderboard of teams", { })},
		{"top", new command_topteams_t(this, "topteams", false, "", { })},
		{"nitro", new command_nitro_t(this, "nitro", false, "Show the monthly nitro leaderboard", { })},
		{"queue", new command_queue_t(this, "queue", true, "Show question queue statistics", { })},
		{"ping", new command_ping_t(this, "ping", false, "Show bot latency statistics", { })},
		{"servertime", new command_servertime_t(this, "servertime", false, "Show current server time and how long until scores reset", { })},
		{"leaderboard", new command_stats_t(this, "stats", false, "", { })},
		{"ach", new command_achievements_t(this, "achievements", false, "", { })},
		{
			"prefix", new command_prefix_t(this, "prefix", false, "Set the prefix for message based commands",
			{
				dpp::command_option(dpp::co_string, "prefix", "Command prefix to set", true)
			}
		)},
		{
			"give", new command_give_t(this, "give", false, "Give a user some brainbucks coins",
			{
				dpp::command_option(dpp::co_user, "user", "User to give coins to", true),
				dpp::command_option(dpp::co_integer, "coins", "Number of coins to give", true)
			}
		)},
		{
			"resetprefix", new command_resetprefix_t(this, "resetprefix", true, "Reset the prefix for message based commands",
			{
				dpp::command_option(dpp::co_string, "guild", "Guild ID to reset prefix for", true)
			}
		)},
		{
			"reassignpremium", new command_reassignpremium_t(this, "reassignpremium", true, "Move premium subscription from one guild to another",
			{
				dpp::command_option(dpp::co_user, "user", "User with premium subscription", true),
				dpp::command_option(dpp::co_string, "guild", "New guild ID for premium subscription", true)
			}
		)},
		{
			"subscription", new command_subscription_t(this, "subscription", true, "Show premium subscription info for a user",
			{
				dpp::command_option(dpp::co_user, "user", "User to show premium subscription information for", true)
			}
		)},
		{
			"shard", new command_shard_t(this, "shard", false, "Show which shard and cluster a server is on",
			{
				dpp::command_option(dpp::co_string, "guild", "Guild ID to show info for", false)
			}
		)},
		{
			"categories", new command_categories_t(this, "categories", false, "Show a list of playable categories",
			{
				dpp::command_option(dpp::co_string, "page", "Page number to show", false)
			}
		)},
		{"info", new command_info_t(this, "info", false, "Show information about TriviaBot", { })},
		{
			"language", new command_language_t(this, "language", false, "Set TriviaBot's language",
			{
				dpp::command_option(dpp::co_string, "language", "Language to set", false)	
			}
		)},
		{"lang", new command_language_t(this, "language", false, "", { })},
		{
			"enable", new command_enable_t(this, "enable", false, "Enable a trivia category",
			{
				dpp::command_option(dpp::co_string, "category", "Category name to enable", true)	
			}
		)},
		{
			"coins", new command_coins_t(this, "coins", false, "Show coin balance of a user",
			{
				dpp::command_option(dpp::co_user, "user", "User's balance to show", false)
			}
		)},
		{
			"profile", new command_profile_t(this, "profile", false, "Show a user's player profile",
			{
				dpp::command_option(dpp::co_user, "user", "User's profile to show", false)
			}
		)},
		{
			"achievements", new command_achievements_t(this, "coins", false, "Show achievements of a user",
			{
				dpp::command_option(dpp::co_user, "user", "User's achievements to show", false)
			}
		)},
		{
			"privacy", new command_privacy_t(this, "privacy", false, "Enable or disable team privacy for your username",
			{
				dpp::command_option(dpp::co_boolean, "enable", "Enable or disable privacy", true)	
			}
		)},
		{
			"disable", new command_disable_t(this, "disable", false, "Disable a trivia category",
			{
				dpp::command_option(dpp::co_string, "category", "Category name to disable", true)	
			}
		)},
		{
			"team", new command_team_t(this, "team", false, "Show information for a trivia team",
			{
				dpp::command_option(dpp::co_string, "name", "Team name to show information for", true)	
			}
		)},
		{
			"join", new command_join_t(this, "join", false, "Join a Trivia team",
			{
				dpp::command_option(dpp::co_string, "team", "Team name to join", true)	
			}
		)},
		{
			"create", new command_create_t(this, "create", false, "Create a new Trivia team",
			{
				dpp::command_option(dpp::co_string, "team", "Team name to create", true)	
			}
		)},
		{
			"forceleave", new command_forceleave_t(this, "forceleave", true, "Force TriviaBot to leave a guild",
			{
				dpp::command_option(dpp::co_string, "guild", "Guild ID to leave", true)
			}
		)},
		{"leave", new command_leave_t(this, "leave", false, "Leave your current Trivia Team", { })},
		{"Start Trivia: Normal", new command_context_user_t(this, "Start Trivia: Normal", false, "Start a round of Trivia", { })},
		{"Start Trivia: Normal", new command_context_message_t(this, "Start Trivia: Normal", false, "Start a round of Trivia", { })},
		{"Start Trivia: Quickfire", new command_context_user_t(this, "Start Trivia: Quickfire", false, "Start a round of Trivia", { })},
		{"Start Trivia: Quickfire", new command_context_message_t(this, "Start Trivia: Quickfire", false, "Start a round of Trivia", { })},
		{"Start Trivia: Hardcore", new command_context_user_t(this, "Start Trivia: Hardcore", false, "Start a round of Trivia", { })},
		{"Start Trivia: Hardcore", new command_context_message_t(this, "Start Trivia: Hardcore", false, "Start a round of Trivia", { })},
		{
			"help", new command_help_t(this, "help", false, "Show help for TriviaBot",
			{
				dpp::command_option(dpp::co_string, "topic", "Help topic", false),
			}
		)}
	};

	if (bot->GetClusterID() == 0) {

		/* Add options to the language command parameter from the database */
		command_t* lang_command = commands.find("language")->second;
		db::resultset langs = db::query("SELECT * FROM languages WHERE live = 1 ORDER BY id", {});
		lang_command->opts[0].choices.clear();
		for (auto & row : langs) {
			lang_command->opts[0].add_choice(dpp::command_option_choice(row["name"], row["isocode"]));
		}

		/* Add options to the categories command parameter from the database */
		command_t* cats_command = commands.find("categories")->second;
		db::resultset q = db::query("SELECT id FROM categories WHERE disabled != 1", {});
		size_t rows = q.size();
		uint32_t length = 25;
		uint32_t pages = ceil((float)rows / (float)length);
		cats_command->opts[0].choices.clear();
		for (uint32_t r = 1; r <= pages; ++r) {
			cats_command->opts[0].add_choice(dpp::command_option_choice(fmt::format("Page {}", r), std::to_string(r)));
		}


		/* Two lists, one for the main set of global commands, and one for admin commands */
		std::vector<dpp::slashcommand> slashcommands;
		std::vector<dpp::slashcommand> adminslash;

		/* Iterate the list and build dpp::slashcommand object vector */
		for (auto & c : commands) {
			if (!c.second->description.empty()) {
				dpp::slashcommand sc;
				sc.set_type(c.second->type)	// Must set type first to prevent lowercasing of context menu items
				  .set_name(c.first)
				  .set_description(c.second->description)
				  .set_application_id(from_string<uint64_t>(bot->GetConfig("application_id"), std::dec));
				for (auto & o : c.second->opts) {
					sc.add_option(o);
				}
				if (c.second->admin) {
					adminslash.push_back(sc);
				} else {
					slashcommands.push_back(sc);
				}
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

	/* Hook interaction events */
	bot->core->on_interaction_create(std::bind(&TriviaModule::HandleInteraction, this, std::placeholders::_1));
	bot->core->on_select_click(std::bind(&TriviaModule::HandleSelect, this, std::placeholders::_1));
	bot->core->on_button_click(std::bind(&TriviaModule::HandleButton, this, std::placeholders::_1));
}

void TriviaModule::HandleSelect(const dpp::select_click_t & event) {
	auto command = commands.find(event.custom_id);
	if (command != commands.end()) {
		in_cmd cmd(event.values[0], event.command.usr.id, event.command.channel_id, event.command.guild_id, false, event.command.usr.username, false, event.command.usr, event.command.member);
		cmd.command_id = event.command.id;
		cmd.interaction_token = event.command.token;
		guild_settings_t settings = GetGuildSettings(cmd.guild_id);
		command->second->select_click(event, cmd, settings);
	}
}

void TriviaModule::HandleButton(const dpp::button_click_t & event) {
	std::string identifier = event.custom_id.substr(0, event.custom_id.find(','));
	std::string remainder = event.custom_id.substr(event.custom_id.find(',') + 1);
	auto command = commands.find(identifier);
	if (command != commands.end()) {
		in_cmd cmd(remainder, event.command.usr.id, event.command.channel_id, event.command.guild_id, false, event.command.usr.username, false, event.command.usr, event.command.member);
		cmd.command_id = event.command.id;
		cmd.interaction_token = event.command.token;
		guild_settings_t settings = GetGuildSettings(cmd.guild_id);
		command->second->button_click(event, cmd, settings);
	}
}

void TriviaModule::HandleInteraction(const dpp::interaction_create_t& event) {
	if (event.command.type == dpp::it_application_command) {
		dpp::command_interaction cmd_interaction = std::get<dpp::command_interaction>(event.command.data);

		std::stringstream message;

		message << cmd_interaction.name;
		for (auto & p : cmd_interaction.options) {
			if (std::holds_alternative<int64_t>(p.value)) {
				message << " " << std::get<int64_t>(p.value);
			} else if (std::holds_alternative<dpp::snowflake>(p.value)) {
				message << " " << std::get<dpp::snowflake>(p.value);
			} else if (std::holds_alternative<std::string>(p.value)) {
				message << " " << std::get<std::string>(p.value);
			}
		}

		in_cmd cmd(message.str(), event.command.usr.id, event.command.channel_id, event.command.guild_id, false, event.command.usr.username, false, event.command.usr, event.command.member);
		cmd.command_id = event.command.id;
		cmd.interaction_token = event.command.token;
		if (cmd_interaction.type == dpp::ctxm_chat_input) {
			bot->core->log(dpp::ll_info, fmt::format("SCMD (USER={}, GUILD={}): <{}> /{}", cmd.author_id, cmd.guild_id, cmd.username, cmd.msg));
		} else {
			bot->core->log(dpp::ll_info, fmt::format("CONTEXT (USER={}, GUILD={}): {} -> '{}'", cmd.author_id, cmd.guild_id, cmd.username, cmd.msg));
		}
		this->handle_command(cmd, event);
	}
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

void TriviaModule::thinking(bool ephemeral, const dpp::interaction_create_t& event) {
	if (event.command.guild_id.empty()) {
		return;
	}
	/* Set 'thinking' state */
	dpp::message msg;
	msg.content = "*";
	msg.guild_id = event.command.guild_id;
	msg.channel_id = event.command.channel_id;
	if (ephemeral) {
		msg.set_flags(dpp::m_ephemeral);
	}
	bot->core->interaction_response_create(event.command.id, event.command.token, dpp::interaction_response(dpp::ir_deferred_channel_message_with_source, msg));

}

bool TriviaModule::has_rl_warn(dpp::snowflake channel_id) {
	std::shared_lock cmd_lock(cmdmutex);
	auto check = last_rl_warning.find(channel_id);
	return check != last_rl_warning.end() && time(NULL) < check->second;
}

bool TriviaModule::has_limit(dpp::snowflake channel_id) {
	std::shared_lock cmd_lock(cmdmutex);
	auto check = limits.find(channel_id);
	return check != limits.end() && time(NULL) < check->second;
}

bool TriviaModule::set_rl_warn(dpp::snowflake channel_id) {
	std::unique_lock cmd_lock(cmdmutex);
	last_rl_warning[channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
	return true;
}

bool TriviaModule::set_limit(dpp::snowflake channel_id) {
	std::unique_lock cmd_lock(cmdmutex);
	limits[channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
	return true;
}

void TriviaModule::handle_command(const in_cmd &cmd, const dpp::interaction_create_t& event) {

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
	
			auto command = commands.end();
			if (!cmd.interaction_token.empty()) {
				/* For interactions, if we can't find the command then it's a context menu action */
				command = commands.find(cmd.msg);
			}
			if (command == commands.end()) {
				command = commands.find(base_command);
			}
			if (command != commands.end()) {

				this->thinking(command->second->ephemeral, event);

				bool can_execute = false;
				if (!this->has_limit(cmd.channel_id)) {
					can_execute = true;
					this->set_limit(cmd.channel_id);
				}

				if (can_execute || cmd.from_dashboard || command->second->ephemeral) {
					bot->core->log(dpp::ll_debug, fmt::format("command_t '{}' routed to handler", command->first));
					command->second->call(cmd, tokens, settings, cmd.username, moderator, c, user);
				} else {
					/* Display rate limit message, but only one per rate limit period */
					if (!this->has_rl_warn(cmd.channel_id)) {
						this->set_rl_warn(cmd.channel_id);
						SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), cmd.channel_id, _("WOAHTHERE", settings));
					}
					bot->core->log(dpp::ll_debug, fmt::format("command_t '{}' NOT routed to handler on channel {}, limiting", base_command, cmd.channel_id));
				}
			} else {
				this->thinking(false, event);

				/* Custom commands handled completely by the API as a REST call */
				bool command_exists = false;
				{
					std::shared_lock cmd_list_lock(cmds_mutex);
					command_exists = (std::find(api_commands.begin(), api_commands.end(), trim(lowercase(base_command))) != api_commands.end());
				}
				if (command_exists) {
					bool can_execute = false;
					if (!this->has_limit(cmd.channel_id)) {
						can_execute = true;
						this->set_limit(cmd.channel_id);
					}

					if (can_execute) {
						std::string rest;
						std::getline(tokens, rest);
						rest = trim(rest);
						CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
						custom_command(cmd.interaction_token, cmd.command_id, settings, this, base_command, trim(rest), cmd.author_id, cmd.channel_id, cmd.guild_id);
					} else {
						/* Display rate limit message, but only one per rate limit period */
						if (!this->has_rl_warn(cmd.channel_id)) {
							this->set_rl_warn(cmd.channel_id);
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

