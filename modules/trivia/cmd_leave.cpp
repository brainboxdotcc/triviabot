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
#include "trivia.h"
#include "webrequest.h"
#include "commands.h"

command_leave_t::command_leave_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_leave_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	get_current_team(cmd.author_id, [this, cmd, settings, username](uint32_t, std::string teamname) {
		if (teamname.empty()) {
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("YOULONER", settings), username, settings.prefix), cmd.channel_id);
		} else {
			leave_team(cmd.author_id);
			creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, ":busts_in_silhouette:", fmt::format(_("LEFTTEAM", settings), username, teamname), cmd.channel_id, _("COMEBACK", settings));
		}
		creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
	});
}

