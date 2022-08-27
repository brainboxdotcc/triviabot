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
#include "wlower.h"

std::mutex m;

struct in_flight {
	int state = 0;
	std::string token;
	std::vector<std::string> categories;
	int page;
	std::string type;
	time_t creation;
	int questions;
};

std::unordered_map<dpp::snowflake, in_flight> active_menus;

command_context_message_t::command_context_message_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options)
	: command_context_user_t(_creator, _base_command, adm, descr, options)
{
	this->type = dpp::ctxm_message;
}

command_context_user_t::command_context_user_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options)
	: command_t(_creator, _base_command, adm, descr, options, true, dpp::ctxm_user)
{
}

void command_context_message_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	command_context_user_t::call(cmd, tokens, settings, username, is_moderator, c, user);
}

void command_context_user_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);

	dpp::message msg;
	std::string dummy, round_type;
	tokens >> dummy >> round_type;
	round_type = lowercase(round_type);

	std::string real_interaction_token{cmd.interaction_token};
	if (real_interaction_token.substr(0, 9) == "EPHEMERAL") {
		real_interaction_token = real_interaction_token.substr(9, real_interaction_token.length() - 9);
	}

	/* Give the user the start options */
	msg.set_flags(dpp::m_ephemeral);
	msg.add_embed(
		dpp::embed()
			.set_title(fmt::format(_("START_INTERACTIVE", settings), round_type))
			.set_footer(
				dpp::embed_footer()
					.set_text(_("POWERED_BY", settings))
					.set_icon("https://triviabot.co.uk/images/triviabot_tl_icon.png")
			)
			.set_color(settings.embedcolour)
			.set_description(_("SELECT_PROMPT_1", settings))
	);
	msg.add_component(
		dpp::component().add_component(
			dpp::component()
				.set_placeholder(_("SELECT_QUESTIONS", settings))
				.set_type(dpp::cot_selectmenu)
				.set_id(this->base_command)
				.set_required(true)
		)
	);
	int max_questions = 200;
	if (round_type == "quickfire") {
		max_questions = settings.premium ? 200 : 15;
	}
	int step;
	if (max_questions < 25) {
		step = 1;
	} else {
		step = max_questions / 25;
	}
	for (int i = 5; i <= max_questions; i += step) {
		if (i == 197) {
			i = 200;
		}
		msg.components[0].components[0].add_select_option(
			dpp::select_option(std::to_string(i) + " Questions", nlohmann::json(
				{
					{"questions", i},
					{"type", round_type},
				}
			).dump()).set_emoji(u8"❓")
		);
	}

	creator->GetBot()->core->interaction_response_edit(
		real_interaction_token,
		msg
	);

	in_flight infl {0, real_interaction_token, {}, 1, round_type, time(nullptr), 0};
	{
		std::lock_guard lock(m);
		active_menus[cmd.user.id] = infl;
	}
}

void RefreshMessage(in_flight &infl, guild_settings_t& settings, std::string base_command, TriviaModule* creator)
{
	dpp::message msg;
	msg.add_embed(
		dpp::embed()
			.set_title(fmt::format(creator->_("START_INTERACTIVE", settings), infl.type))
			.set_footer(
				dpp::embed_footer()
					.set_text(creator->_("POWERED_BY", settings))
					.set_icon("https://triviabot.co.uk/images/triviabot_tl_icon.png")
			)
			.set_color(settings.embedcolour)
			.set_description(creator->_("SELECT_PROMPT_2", settings))
			.add_field(creator->_("QUESTIONS", settings), std::to_string(infl.questions))
	);
	if (infl.categories.empty()) {
		msg.embeds[0].add_field(creator->_("SEL_CATEGORIES", settings), creator->_("SEL_NONE", settings));
	} else {
		std::string catlist;
		for (auto & c : infl.categories) {
			catlist += "📚 " + c + "\n";
		}
		msg.embeds[0].add_field(creator->_("SEL_CATEGORIES", settings), catlist);
	}
	msg.add_component(
		dpp::component().add_component(
			dpp::component()
				.set_placeholder(creator->_("SELECT_CATEGORIES", settings))
				.set_type(dpp::cot_selectmenu)
				.set_id(base_command)
				.set_required(true)
		)
	);
	db::resultset q = db::query("SELECT * FROM categories WHERE disabled != 1", {});
	size_t rows = q.size();
	uint32_t length = 25;
	uint32_t pages = ceil((float)rows / (float)length);
	msg.add_component(dpp::component());
	for (uint32_t r = 1; r <= pages; ++r) {
		msg.components[1].add_component(
			dpp::component().set_label(fmt::format(creator->_("PAGE", settings), r)).set_style(dpp::cos_primary).set_id(base_command + "," + std::to_string(r))
		);
	}
	msg.add_component(dpp::component()
		.add_component(
			dpp::component().set_label(creator->_("STARTGAME", settings)).set_style(dpp::cos_success).set_id(base_command + ",start")
		)
		.add_component(
			dpp::component().set_label(creator->_("CANCELGAME", settings)).set_style(dpp::cos_danger).set_id(base_command + ",cancel")
		)
	);
	q = db::query("SELECT * FROM categories WHERE disabled != 1 ORDER BY name LIMIT ?,?", {(infl.page - 1) * length, length});
	for (auto& nrow : q) {
		msg.components[0].components[0].add_select_option(
			dpp::select_option(
				"📚 " + nrow["name"], nlohmann::json(
					{
						{"category", nrow["name"]},
						{"type", infl.type},
						{"questions", infl.questions},
						{"page", infl.page},
					}
				).dump()
			)
		);
	}

	creator->GetBot()->core->interaction_response_edit(
		infl.token,
		msg
	);
}

void command_context_user_t::button_click(const dpp::button_click_t & event, const in_cmd &cmd, guild_settings_t &settings)
{
	in_flight infl;
	std::string message;
	event.reply();
	{
		std::lock_guard lock(m);
		auto i = active_menus.find(event.command.usr.id);
		if (i != active_menus.end()) {
			infl = i->second;
		} else {
			return;
		}
	}

	if (cmd.msg == "start") {
		/* Start game */
		std::string msg = (infl.type == "normal" ? "start" : infl.type) + " " + std::to_string(infl.questions) + " ";
		for (auto& c : infl.categories) {
			msg += c + ";";
		}
		msg = msg.substr(0, msg.length() - 1);
		in_cmd newcmd(msg, event.command.usr.id, event.command.channel_id, event.command.guild_id, false, event.command.usr.username, false, event.command.usr, event.command.member);
		creator->handle_command(newcmd, dpp::interaction_create_t(event.from, ""));
		message = _("GAME_STARTED", settings);
	}
	if (cmd.msg == "cancel") {
		message = _("GAME_CANCELLED", settings);

	}
	if (cmd.msg == "cancel" || cmd.msg == "start") {
		/* Cancel prompt */
		dpp::message msg;
		msg.add_embed(
			dpp::embed()
				.set_title(fmt::format(_("END_INTERACTIVE", settings), infl.type))
				.set_footer(
					dpp::embed_footer()
						.set_text(_("POWERED_BY", settings))
						.set_icon("https://triviabot.co.uk/images/triviabot_tl_icon.png")
				)
				.set_color(settings.embedcolour)
				.set_description(message)
		);
		creator->GetBot()->core->interaction_response_edit(
			infl.token,
			msg
		);
		{
			std::lock_guard lock(m);
			auto i = active_menus.find(cmd.user.id);
			if (i != active_menus.end()) {
				active_menus.erase(i);
			}
		}
		return;
	}
	if (atoi(cmd.msg.c_str()) > 0) {
		infl.page = std::stoi(cmd.msg);
		RefreshMessage(infl, settings, this->base_command, creator);
	}

	{
		std::lock_guard lock(m);
		active_menus[cmd.user.id] = infl;
	}
}


void command_context_user_t::select_click(const dpp::select_click_t & event, const in_cmd &cmd, guild_settings_t &settings)
{
	in_flight infl;
	event.reply();
	{
		std::lock_guard lock(m);
		auto i = active_menus.find(event.command.usr.id);
		if (i != active_menus.end()) {
			infl = i->second;
		} else {
			return;
		}
	}

	nlohmann::json j = nlohmann::json::parse(cmd.msg);
	infl.questions = j.at("questions").get<int>();
	std::string type = j.at("type").get<std::string>();

	switch (infl.state) {
		case 1: {
			std::string category = j.at("category").get<std::string>();
			infl.page = j.at("page").get<int>();
			if (std::find(infl.categories.begin(), infl.categories.end(), category) == infl.categories.end()) {
				infl.categories.emplace_back(category);
			}
		}
		case 0: {
			RefreshMessage(infl, settings, this->base_command, creator);
			infl.state = 1;
		}
		break;
	}
	{
		std::lock_guard lock(m);
		active_menus[cmd.user.id] = infl;
	}
}


