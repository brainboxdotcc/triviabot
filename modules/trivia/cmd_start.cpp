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

command_start_t::command_start_t(class TriviaModule* _creator, const std::string &_base_command) : command_t(_creator, _base_command) { }

void command_start_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, aegis::channel* c, aegis::user* user, state_t* state)
{
	int32_t questions;
	std::string str_q;
	tokens >> str_q;
	if (str_q.empty()) {
		questions = 10;
	} else {
		questions = from_string<int32_t>(str_q, std::dec);
	}
	bool quickfire = (base_command == "quickfire" || base_command == "qf");
	bool hintless = (base_command == "hardcore" || base_command == "hc");
	bool resumed = false;

	json document;
	std::ifstream configfile("../config.json");
	configfile >> document;
	json shitlist = document["shitlist"];
	for (auto entry = shitlist.begin(); entry != shitlist.end(); ++entry) {
		int64_t sl_guild_id = from_string<int64_t>(entry->get<std::string>(), std::dec);
		if (cmd.channel_id == sl_guild_id) {
			creator->SimpleEmbed(":warning:", fmt::format(_("SHITLISTED", settings), username, creator->bot->user.id.get()), cmd.channel_id);
			return;
		}
	}

	if (settings.only_mods_start) {
		if (!is_moderator) {
			creator->SimpleEmbed(":warning:", fmt::format(_("AREYOUSTARTINGSOMETHING", settings), username), cmd.channel_id);
			return;
		}
	}


	if (!settings.premium) {
		std::lock_guard<std::mutex> user_cache_lock(creator->states_mutex);
		for (auto j = creator->states.begin(); j != creator->states.end(); ++j) {
			if (j->second->guild_id == cmd.guild_id && j->second->gamestate != TRIV_END) {
				aegis::channel* active_channel = creator->bot->core.find_channel(j->second->channel_id);
				if (active_channel) {
					creator->EmbedWithFields(_("NOWAY", settings), {{_("ALREADYACTIVE", settings), fmt::format(_("CHANNELREF", settings), active_channel->get_id().get()), false},
							{_("GETPREMIUM", settings), _("PREMDETAIL1", settings), false}}, cmd.channel_id);
					return;
				}
			}
		}
	}

	if (!state) {
		creator->GetBot()->core.log->debug("start game, no existing state");
		int32_t max_quickfire = (settings.premium ? 200 : 15);
		if ((!quickfire && (questions < 5 || questions > 200)) || (quickfire && (questions < 5 || questions > max_quickfire))) {
			if (quickfire) {
				if (questions > max_quickfire && !settings.premium) {
					creator->EmbedWithFields(_("MAX15", settings), {{_("GETPREMIUM", settings), _("PREMDETAIL2", settings), false}}, cmd.channel_id);
				} else {
					creator->SimpleEmbed(":warning:", fmt::format(_("MAX15DETAIL", settings), username, max_quickfire), cmd.channel_id);
				}
			} else {
				creator->SimpleEmbed(":warning:", fmt::format(_("MAX200", settings), username), cmd.channel_id);
			}
			return;
		}
		std::vector<std::string> sl = fetch_shuffle_list(cmd.guild_id);
		if (sl.size() < 50) {
			creator->SimpleEmbed(":warning:", fmt::format(_("SPOOPY2", settings), username), c->get_id().get(), _("BROKED", settings));
			return;
		} else  {
			{
				std::lock_guard<std::mutex> user_cache_lock(creator->states_mutex);
				creator->GetBot()->core.log->debug("state pointer is {}", (int64_t)state);
				state = new state_t(creator);
				creator->GetBot()->core.log->debug("state pointer is now {}", (int64_t)state);
				state->start_time = time(NULL);
				state->shuffle_list = sl;
				state->gamestate = TRIV_ASK_QUESTION;
				state->numquestions = questions + 1;
				state->streak = 1;
				state->last_to_answer = 0;
				state->round = 1;
				state->interval = (quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL);
				state->channel_id = cmd.channel_id;
				state->curr_qid = 0;
				state->curr_answer = "";
				state->hintless = hintless;
				state->guild_id = cmd.guild_id;
				creator->bot->core.log->info("Started game on guild {}, channel {}, {} questions [{}]", cmd.guild_id, cmd.channel_id, questions, quickfire ? "quickfire" : "normal");
				creator->EmbedWithFields(fmt::format(_((state->hintless ? "NEWROUND_NH" : "NEWROUND"), settings), (state->hintless ? "**HARDCORE** " : (quickfire ? "**QUICKFIRE** " : "")), (resumed ? _("RESUMED", settings) : _("STARTED", settings)), (resumed ? _("ABOTADMIN", settings) : username)), {{_("QUESTION", settings), fmt::format("{}", questions), false}, {_("GETREADY", settings), _("FIRSTCOMING", settings), false}, {_("HOWPLAY", settings), _("INSTRUCTIONS", settings)}}, cmd.channel_id);
				creator->states[cmd.channel_id] = state;
				creator->GetBot()->core.log->debug("create new timer");
				state->timer = new std::thread(&state_t::tick, state);
				creator->GetBot()->core.log->debug("finished creating timer");
			}

			creator->CacheUser(cmd.author_id, cmd.channel_id);
			log_game_start(state->guild_id, state->channel_id, questions, quickfire, c->get_name(), cmd.author_id, state->shuffle_list, state->hintless);
			creator->GetBot()->core.log->debug("returning from start game");
			return;
		}
	} else {
		creator->SimpleEmbed(":warning:", fmt::format(_("ALREADYRUN", settings), username), cmd.channel_id);
		return;
	}
}

