/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004,2005,2020,2021,2024 Craig Edwards <support@brainbox.cc>
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
#include <sporks/regex.h>
#include <string>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "state.h"
#include "trivia.h"
#include "commands.h"
#include "wlower.h"

command_votehint_t::command_votehint_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options, true) { }

void command_votehint_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);

	std::lock_guard<std::mutex> states_lock(creator->states_mutex);
	state_t* state = creator->GetState(cmd.channel_id);

	if (state) {
		/* Only provide hints when a non-insane round question is being asked */
		if ((state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) && (!state->is_insane_round(settings)) != 0 && state->question.answer != "") {
			db::resultset rs = db::query("SELECT *,(unix_timestamp(vote_time) + 43200 - unix_timestamp()) as remaining FROM infobot_votes WHERE snowflake_id = ? AND now() < vote_time + interval 12 hour", {cmd.author_id});
			/* Check the user has hints remaining */
			if (rs.size() == 0) {
				/* No vote? No hints for you... */
				std::string a = fmt::format(_("VOTEAD", settings), creator->GetBot()->user.id, settings.prefix);
				creator->SimpleEmbed(cmd.interaction_token.length() ? "EPHEMERAL" + cmd.interaction_token : cmd.interaction_token, cmd.command_id, settings, "<:wc_rs:667695516737470494>", _("NOTVOTED", settings) + "\n" + a, cmd.channel_id);
				return;
			} else {
				/* Compose hint */
				int64_t remaining_hints = from_string<int64_t>(rs[0]["dm_hints"], std::dec);
				int32_t secs = from_string<int32_t>(rs[0]["remaining"], std::dec);
				int32_t mins = secs / 60 % 60;
				float hours = floor(secs / 60 / 60);
				if (remaining_hints < 1) {
					/* Voted within 12 hours but no hints left */
					std::string a = fmt::format(_("NOMOREHINTS", settings), username);
					std::string b = fmt::format(_("VOTEAD", settings), creator->GetBot()->user.id, settings.prefix);
					creator->SimpleEmbed(cmd.interaction_token.length() ? "EPHEMERAL" + cmd.interaction_token : cmd.interaction_token, cmd.command_id, settings, ":warning:", a + "\n" + b, cmd.channel_id);
				} else {
					remaining_hints--;
					std::string text_extra;
					if (remaining_hints > 0) {
						text_extra = fmt::format(_("VH1", settings), remaining_hints, hours, mins);
					} else {
						text_extra = fmt::format(_("VH2", settings), hours, mins);
					}

					/* UTF8-safe personal hint */
					std::string personal_hint = utf8lower(state->question.answer, settings.language == "es");
					std::setlocale(LC_CTYPE, "en_US.UTF-8"); // the locale will be the UTF-8 enabled English
					std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
					std::wstring wide = converter.from_bytes(personal_hint.c_str());
					wide[0] = L'#';
					wide[wide.length() - 1] = L'#';
					for (auto w = wide.begin(); w != wide.end(); ++w) {
						if (*w == L' ') {
							*w = L'#';
						}
					}
					personal_hint = converter.to_bytes(wide);
					/* If the user requested the hint via a slash command, we can deliver their hint via an elphemeral message, in secret! */
					if (cmd.interaction_token.length()) {
						creator->SimpleEmbed(
							"EPHEMERAL" + cmd.interaction_token, cmd.command_id, settings, "",
							fmt::format(_("VH_HINT", settings),  personal_hint) + "\n" + BLANK_EMOJI + "\n" + text_extra +
							"\n" + BLANK_EMOJI + "\n**" + _("VH_TOPUP", settings) + "**",
							cmd.channel_id, _("VH_TITLE", settings), "", "https://triviabot.co.uk/images/crystalball.png");
					}  else {
						/* Boo, requested the hint via a message command, get with the times grandad. Deliver the hint via direct message */
						dpp::message direct_message;
						direct_message.add_embed(dpp::embed()
							.set_title(_("VH_TITLE", settings))
							.set_color(settings.embedcolour)
							.set_thumbnail("https://triviabot.co.uk/images/crystalball.png")
							.set_description(fmt::format(_("VH_HINT", settings),  personal_hint) +
							"\n" + BLANK_EMOJI + "\n" + text_extra +
							"\n" + BLANK_EMOJI + "\n**" + _("VH_BE_MODERN", settings) +"**")
							.set_footer(dpp::embed_footer().set_icon("https://triviabot.co.uk/images/triviabot_tl_icon.png").set_text(_("POWERED_BY", settings)))
						);
						creator->GetBot()->core->direct_message_create(cmd.author_id, direct_message, [this, cmd, settings](const auto& cc) {
							if (cc.is_error()) {
								/* The user turned off direct messages on all guilds where the bot is, so we can't DM them.
								 * Complain loudly at them on channel.
								 */
								this->creator->SimpleEmbed(settings, ":cry:", _("VH_DMS_BLOCKED", settings), cmd.channel_id);
							}
						});
					}
					/* Subtract hints */
					db::backgroundquery("UPDATE infobot_votes SET dm_hints = ? WHERE snowflake_id = ?", {remaining_hints, cmd.author_id});
					return;
				}
			}
		} else {
			/* Not the right point in the round for a hint */
			creator->SimpleEmbed(cmd.interaction_token.length() ? "EPHEMERAL" + cmd.interaction_token : cmd.interaction_token, cmd.command_id, settings, ":warning:", fmt::format(_("WAITABIT", settings), username), cmd.channel_id);
			return;
		}
	} else {
		/* No active round of trivia */
		std::string a = fmt::format(_("NOROUND", settings), username);
		std::string b = fmt::format(_("VOTEAD", settings), creator->GetBot()->user.id, settings.prefix);
		creator->SimpleEmbed(cmd.interaction_token.length() ? "EPHEMERAL" + cmd.interaction_token : cmd.interaction_token, cmd.command_id, settings, ":warning:", a + "\n" + b, cmd.channel_id);
		return;
	}
}

