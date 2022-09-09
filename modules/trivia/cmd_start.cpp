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

command_start_t::command_start_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

std::vector<uint64_t> GetChannelWhitelist(uint64_t guild_id)
{
	std::vector<uint64_t> wl;
	db::resultset rs = db::query("SELECT channel_id FROM channel_whitelist WHERE guild_id = ?", {guild_id});
	for (auto row : rs) {
		wl.push_back(from_string<uint64_t>(row["channel_id"], std::dec));
	}
	return wl;
}

void command_start_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	int32_t questions;
	std::string str_q;
	std::string category;
	bool categories_only = false;

	tokens >> str_q;

	/* Don't allow banned users to start games at all */
	db::resultset rs2 = db::query("SELECT snowflake_id FROM bans WHERE play_ban = 1 AND snowflake_id = ?", {cmd.author_id});
	if (rs2.size()) {
		return;
	}

	if (str_q.empty()) {
		questions = 10;
	} else {
		questions = from_string<int32_t>(str_q, std::dec);
		if (questions == 0) {
			/* Handle non-numeric or missing inputs */
			questions = 10;
			/* In this case the entire parameter string is a category list (or empty) */
			category = trim(tokens.str());
			category = trim(category.substr(category.find_first_of(' '), category.length()));
			categories_only = true;
		}
	}

	if (!categories_only) {
		/* If we don't already have a category list, check second parameter (remaining string) for them */
		std::getline(tokens, category);
		category = trim(category);
	}

	bool quickfire = (base_command == "quickfire" || base_command == "qf");
	bool hintless = (base_command == "hardcore" || base_command == "hc");

	json document;
	std::ifstream configfile("../config.json");
	json shitlist = document["shitlist"];
	configfile >> document;
	for (auto entry = shitlist.begin(); entry != shitlist.end(); ++entry) {
		int64_t sl_guild_id = from_string<int64_t>(entry->get<std::string>(), std::dec);
		if (cmd.channel_id == sl_guild_id) {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("SHITLISTED", settings), username, creator->GetBot()->user.id), cmd.channel_id);
			return;
		}
	}

	if (!cmd.from_dashboard && settings.only_mods_start) {
		if (!is_moderator) {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("AREYOUSTARTINGSOMETHING", settings), username), cmd.channel_id);
			return;
		}
	}

	std::vector<uint64_t> whitelist = GetChannelWhitelist(cmd.guild_id);
	std::string whitelist_str;
	bool allowed = true;
	if (!cmd.from_dashboard && whitelist.size()) {
		allowed = false;
		for (uint64_t cid : whitelist) {
			whitelist_str.append(fmt::format(" <#{0}>", cid));
			if (cid == cmd.channel_id) {
				allowed = true;
			}
		}
	}

	if (!allowed) {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("NOTINWHITELIST", settings), whitelist_str), cmd.channel_id);
		return;
	}

	if (!settings.premium) {
		std::lock_guard<std::mutex> states_lock(creator->states_mutex);
		for (auto j = creator->states.begin(); j != creator->states.end(); ++j) {
			if (j->second.guild_id == cmd.guild_id && j->second.gamestate != TRIV_END && j->second.channel_id != cmd.channel_id) {
				/* Start commands from dashbord supercede other running games and stop them */
				if (cmd.from_dashboard) {
					creator->SimpleEmbed(settings, ":octagonal_sign:", _("DASH_STOP", settings), j->first, _("STOPPING", settings));
					log_game_end(cmd.guild_id, j->first);
					creator->states.erase(j);
					break;
				} else {
					creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, _("NOWAY", settings), {
						{_("ALREADYACTIVE", settings), fmt::format(_("CHANNELREF", settings), j->first), false},
						{_("GETPREMIUM", settings), _("PREMDETAIL1", settings), false}
					}, cmd.channel_id);
					return;
				}
			}
		}
	} else {
		size_t number_of_games = 0;
		std::lock_guard<std::mutex> states_lock(creator->states_mutex);
		for (auto j = creator->states.begin(); j != creator->states.end(); ++j) {
			if (j->second.guild_id == cmd.guild_id) {
				if (cmd.from_dashboard && j->second.gamestate != TRIV_END && j->second.channel_id != cmd.channel_id) {
					creator->SimpleEmbed(settings, ":octagonal_sign:", _("DASH_STOP", settings), j->first, _("STOPPING", settings));
					log_game_end(cmd.guild_id, j->first);
					creator->states.erase(j);
					break;
				} else {
					number_of_games++;
				}
			}
		}
		if (number_of_games >= 2) {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":octagonal_sign:", _("TOO_MANY_PREM", settings), cmd.channel_id);
			return;
		}
	}

	/* Stop and REPLACE existing games if from dashboard */
	bool already_running = false;
	{
		std::lock_guard<std::mutex> states_lock(creator->states_mutex);
		already_running = (creator->states.find(cmd.channel_id) != creator->states.end());
	}

	if (already_running && cmd.from_dashboard) {
		creator->SimpleEmbed(settings, ":octagonal_sign:", _("DASH_STOP", settings), cmd.channel_id, _("STOPPING", settings));
		log_game_end(cmd.guild_id, cmd.channel_id);
		already_running = false;
	}

	if (!already_running) {
		creator->GetBot()->core->log(dpp::ll_debug, fmt::format("start game, no existing state"));

		int32_t max_quickfire = (settings.premium ? 200 : 15);
		int32_t max_normal = 200;

		if ((!quickfire && (questions < 5 || questions > max_normal)) || (quickfire && (questions < 5 || questions > max_quickfire))) {
			if (quickfire) {
				if (questions > max_quickfire && !settings.premium) {
					creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, _("MAX15", settings), {{_("GETPREMIUM", settings), _("PREMDETAIL2", settings), false}}, cmd.channel_id);
				} else {
					creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("MAX15DETAIL", settings), username, max_quickfire), cmd.channel_id);
				}
			} else {
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("MAX200", settings), username), cmd.channel_id);
			}
			return;
		}

		if (!quickfire) {
			if (hintless) {
				if (settings.max_hardcore_round < questions) {
					questions = settings.max_hardcore_round;
					creator->SimpleEmbed(settings, ":warning:", fmt::format(_("LIMITED", settings), settings.max_hardcore_round), cmd.channel_id);
				}
			} else {
				if (settings.max_normal_round < questions) {
					questions = settings.max_normal_round;
					creator->SimpleEmbed(settings, ":warning:", fmt::format(_("LIMITED", settings), settings.max_normal_round), cmd.channel_id);
				}
			}
		} else {
			if (settings.max_quickfire_round < questions) {
				questions = settings.max_quickfire_round;
				creator->SimpleEmbed(settings, ":warning:", fmt::format(_("LIMITED", settings), settings.max_quickfire_round), cmd.channel_id);
			}
		}

		std::vector<std::string> sl = fetch_shuffle_list(cmd.guild_id, category);
		if (sl.size() == 1) {
			if (sl[0] == "*** No such category ***") {
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", _("START_BAD_CATEGORY", settings), cmd.channel_id);
			} else if (sl[0] == "*** Category too small ***") {
				creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", _((settings.premium ? "START_TOO_SMALL_PREM" : "START_TOO_SMALL"), settings), cmd.channel_id);
			}
			return;
		}
		if (sl.size() < 50) {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("SPOOPY2", settings), username), cmd.channel_id, _("BROKED", settings));
			return;
		} else  {

			if (quickfire && !settings.premium) {
				db::resultset lastinsane = db::query("SELECT * FROM insane_cooldown WHERE guild_id = ?", {cmd.guild_id});
				if (lastinsane.size()) {
					time_t last_insane_time = from_string<time_t>(lastinsane[0]["last_started"], std::dec);
					if (time(NULL) - last_insane_time < 900) {
						int64_t seconds = 900 - (time(NULL) - last_insane_time);
						int64_t minutes = floor((float)seconds / 60.0f);
						seconds = seconds % 60;
						creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("INSANE_COOLDOWN", settings), minutes, seconds), cmd.channel_id);
						return;
					}
				}
				db::backgroundquery("INSERT INTO insane_cooldown (guild_id, last_started) VALUES(?, UNIX_TIMESTAMP()) ON DUPLICATE KEY UPDATE last_started = UNIX_TIMESTAMP()", {cmd.guild_id});
			}

			check_create_webhook(settings, creator, cmd.channel_id);

			uint32_t currstreak = 1;
			uint64_t lastanswered = 0;

			{
				std::lock_guard<std::mutex> locker(creator->cs_mutex);
				auto i = creator->last_channel_streaks.find(cmd.channel_id);
				if (i != creator->last_channel_streaks.end()) {
					if (time(NULL) < i->second.time + 600) {
						lastanswered = i->second.lastanswered;
						currstreak = i->second.streak;
						creator->GetBot()->core->log(dpp::ll_debug, fmt::format("Carrying over streak from previous game on channel id {}, last to answer: {}, streak: {}", cmd.channel_id, lastanswered, currstreak));
					} else {
						creator->last_channel_streaks.erase(i);
						creator->GetBot()->core->log(dpp::ll_debug, fmt::format("Streak for channel id {} not carried over, as it is over 10 minutes old", cmd.channel_id));
					}
				}
			}
			
			{
				std::lock_guard<std::mutex> states_lock(creator->states_mutex);

				creator->states[cmd.channel_id] = state_t(
					creator,
					questions+1,
					currstreak,
					lastanswered,
					1,
					(quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL),
					cmd.channel_id,
					hintless,
					sl,
					TRIV_ASK_QUESTION,
					cmd.guild_id
				);

				creator->GetBot()->core->log(dpp::ll_info, fmt::format("Started game on guild {}, channel {}, {} questions [{}] [category: {}]", cmd.guild_id, cmd.channel_id, questions, quickfire ? "quickfire" : "normal", (category.empty() ? "<ALL>" : category)));

				std::vector<field_t> fields = {{_("QUESTION", settings), fmt::format("{}", questions), false}};
				if (!category.empty()) {
					fields.push_back({_("CATEGORY", settings), category, false});
				}
				fields.push_back({_("GETREADY", settings), _("FIRSTCOMING", settings), false});
				fields.push_back({_("HOWPLAY", settings), _("INSTRUCTIONS", settings), false});
			}

			std::vector<field_t> fields = {{_("QUESTION", settings), fmt::format("{}", questions), false}};
			if (!category.empty()) {
				fields.push_back({_("CATEGORY", settings), category, false});
			}
			fields.push_back({_("GETREADY", settings), _("FIRSTCOMING", settings), false});
			fields.push_back({_("HOWPLAY", settings), _("INSTRUCTIONS", settings), false});
			creator->EmbedWithFields(cmd.interaction_token, cmd.command_id, settings, fmt::format(_((hintless ? "NEWROUND_NH" : "NEWROUND"), settings), (hintless ? "**HARDCORE** " : (quickfire ? "**QUICKFIRE** " : "")),  _("STARTED", settings), username), fields, cmd.channel_id);

			creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
			log_game_start(cmd.guild_id, cmd.channel_id, questions, quickfire, c->name, cmd.author_id, sl, hintless);
			creator->GetBot()->core->log(dpp::ll_debug, fmt::format("returning from start game"));
			return;
		}
	} else {
		creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("ALREADYRUN", settings), username), cmd.channel_id);
		return;
	}
}

