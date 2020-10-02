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

command_votehint_t::command_votehint_t(class TriviaModule* _creator, const std::string &_base_command) : command_t(_creator, _base_command) { }

void command_votehint_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, aegis::channel* c, aegis::user* user, state_t* state)
{
	if (state) {
		if ((state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) && (state->round % 10) != 0 && state->curr_answer != "") {
			db::resultset rs = db::query("SELECT *,(unix_timestamp(vote_time) + 43200 - unix_timestamp()) as remaining FROM infobot_votes WHERE snowflake_id = ? AND now() < vote_time + interval 12 hour", {cmd.author_id});
			if (rs.size() == 0) {
				std::string a = fmt::format(_("VOTEAD", settings), creator->bot->user.id.get(), settings.prefix);
				creator->SimpleEmbed("<:wc_rs:667695516737470494>", _("NOTVOTED", settings) + "\n" + a, cmd.channel_id);
				return;
			} else {
				int64_t remaining_hints = from_string<int64_t>(rs[0]["dm_hints"], std::dec);
				int32_t secs = from_string<int32_t>(rs[0]["remaining"], std::dec);
				int32_t mins = secs / 60 % 60;
				float hours = floor(secs / 60 / 60);
				if (remaining_hints < 1) {
					std::string a = fmt::format(_("NOMOREHINTS", settings), username);
					std::string b = fmt::format(_("VOTEAD", settings), creator->bot->user.id.get(), settings.prefix);
					creator->SimpleEmbed(":warning:", a + "\n" + b, cmd.channel_id);
				} else {
					remaining_hints--;
					if (remaining_hints > 0) {
						creator->SimpleEmbed(":white_check_mark:", fmt::format(_("VH1", settings), username, remaining_hints, hours, mins), cmd.channel_id);
					} else {
						creator->SimpleEmbed(":white_check_mark:", fmt::format(_("VH2", settings), username, hours, mins), cmd.channel_id);
					}
					std::string personal_hint = state->curr_answer;
					personal_hint = lowercase(personal_hint);
					personal_hint[0] = '#';
					personal_hint[personal_hint.length() - 1] = '#';
					personal_hint = ReplaceString(personal_hint, " ", "#");
					// Get the API to do this, because DMs in aegis are unreliable right now.
					send_hint(cmd.author_id, personal_hint, remaining_hints);
					db::query("UPDATE infobot_votes SET dm_hints = ? WHERE snowflake_id = ?", {remaining_hints, cmd.author_id});
					creator->CacheUser(cmd.author_id, cmd.channel_id);
					return;
				}
			}
		} else {
			creator->SimpleEmbed(":warning:", fmt::format(_("WAITABIT", settings), username), cmd.channel_id);
			return;
		}
	} else {
		std::string a = fmt::format(_("NOROUND", settings), username);
		std::string b = fmt::format(_("VOTEAD", settings), creator->bot->user.id.get(), settings.prefix);
		creator->SimpleEmbed(":warning:", a + "\n" + b, cmd.channel_id);
		return;
	}
}

