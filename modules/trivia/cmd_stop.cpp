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

command_stop_t::command_stop_t(class TriviaModule* _creator, const std::string &_base_command, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, descr, options) { }

void command_stop_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::lock_guard<std::mutex> states_lock(creator->states_mutex);
	state_t* state = creator->GetState(cmd.channel_id);

	if (state) {
		if (settings.only_mods_stop) {
			if (!is_moderator) {
				creator->SimpleEmbed(settings, ":warning:", fmt::format(_("CANTSTOPMEIMTHEGINGERBREADMAN", settings), username), cmd.channel_id);
				return;
			}
		}
		creator->SimpleEmbed(settings, ":octagonal_sign:", fmt::format(_("STOPOK", settings), username), cmd.channel_id);
		{
			auto i = creator->states.find(cmd.channel_id);
			if (i != creator->states.end()) {
				i->second.terminating = true;
				i->second.next_tick = time(NULL);
			}
			state = nullptr;
		}
		creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
		log_game_end(cmd.guild_id, cmd.channel_id);
	} else {
		creator->SimpleEmbed(settings, ":warning:", fmt::format(_("NOTRIVIA", settings), username), cmd.channel_id);
	}
}

