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
#include "wlower.h"

in_msg::in_msg(const std::string &m, int64_t author, bool mention, const std::string &_username) : msg(m), author_id(author), mentions_bot(mention), username(_username)
{
}

state_t::state_t()
{
}

state_t::state_t(TriviaModule* _creator, uint32_t questions, uint32_t currstreak, int64_t lastanswered, uint32_t question_index, uint32_t _interval, int64_t _channel_id, bool _hintless, const std::vector<std::string> &_shuffle_list, trivia_state_t startstate,  int64_t _guild_id)
{
	next_tick = time(NULL);
	creator = _creator;
	creator->GetBot()->core.log->debug("state_t::state_t()");
	terminating = false;
	channel_id = _channel_id;
	guild_id = _guild_id;
	numquestions = questions;
       	round = question_index;
	score = 0;
	start_time = time(NULL);
	shuffle_list = _shuffle_list;
	gamestate = startstate;
	curr_qid = 0;
	recordtime = 0;
	curr_question = "";
	curr_answer = "";
	curr_customhint1 = "";
	curr_customhint2 = "";
	curr_category = "";
       	curr_lastasked = 0;
	curr_recordtime = 0;
	curr_lastcorrect = "";
	last_to_answer = lastanswered;
	streak = currstreak;
	asktime = 0;
	found = false;
	interval = _interval;
	insane_num = 0;
	insane_left = 0;
	curr_timesasked = 0;
	next_quickfire = 0;
	insane.clear();
	shuffle1 = "";
	shuffle2 = "";
	hintless = _hintless;
}

std::string state_t::_(const std::string &k, const guild_settings_t& settings)
{
	return creator->_(k, settings);
}

void state_t::queue_message(const std::string &message, int64_t author_id, const std::string &username, bool mentions_bot)
{
	// FIX: Check termination atomic flag to avoid race where object is deleted but its handle_message gets called
	if (!terminating) {
		handle_message(in_msg(message, author_id, mentions_bot, username));
	}
}

state_t::~state_t()
{
	terminating = true;
	gamestate = TRIV_END;
	/* XXX: These are safety values, so that if we access a deleted state at any point, it crashes sooner and can be identified easily in the debugger */
	creator = nullptr;
}

bool state_t::is_valid()
{
	return creator && creator->GetBot()->core.find_guild(guild_id) && creator->GetBot()->core.find_channel(channel_id);
}

void state_t::handle_message(const in_msg& m)
{
	if (this->terminating || !creator)
		return;

	if (this->gamestate == TRIV_ASK_QUESTION || this->gamestate == TRIV_FIRST_HINT || this->gamestate == TRIV_SECOND_HINT || this->gamestate == TRIV_TIME_UP) {
		guild_settings_t settings = creator->GetGuildSettings(guild_id);

		if (this->round % 10 == 0) {

			/* NOTE: This is mutexed to stop two people answering at the same time. Each game has its own answer mutex. */
			{
				/* Insane round */
				bool done = false;
				auto i = this->insane.find(utf8lower(m.msg, settings.language == "es"));
				if (i != this->insane.end()) {
					this->insane.erase(i);
	
					if (--this->insane_left < 1) {
						done = true;
						creator->SimpleEmbed(settings, ":thumbsup:", fmt::format(_("LAST_ONE", settings), m.username), this->channel_id);
						if (this->round <= this->numquestions - 1) {
							this->round++;
							this->gamestate = TRIV_ANSWER_CORRECT;
						} else {
							this->gamestate = TRIV_END;
						}
					} else {
						creator->SimpleEmbed(settings, ":thumbsup:", fmt::format(_("INSANE_CORRECT", settings), m.username, m.msg, this->insane_left, this->insane_num), this->channel_id);
					}
					update_score_only(m.author_id, this->guild_id, 1, this->channel_id);
					creator->CacheUser(m.author_id, this->channel_id);
					if (done) {
						/* Only save state if all answers have been found */
						if (log_question_index(this->guild_id, this->channel_id, this->round, this->streak, this->last_to_answer, this->gamestate, 0)) {
							StopGame(settings);
							return;
						}
					}
				}

			}
		} else {
			/* Normal round */
			std::string trivia_message = m.msg;
			int x = from_string<int>(creator->conv_num(m.msg, settings), std::dec);
			if (x > 0) {
				trivia_message = creator->conv_num(m.msg, settings);
			}
			trivia_message = creator->tidy_num(trivia_message);
			bool needs_spanish_hack = (settings.language == "es");

			/* NOTE: This is mutexed to stop two people answering at the same time. Each game has its own answer mutex. */
			{

				/* Answer on channel is an exact match for the current answer and/or it is numeric, OR, it's non-numeric and has a levenstein distance near enough to the current answer (account for misspellings) */
				if (!this->curr_answer.empty() && ((trivia_message.length() >= this->curr_answer.length() && utf8lower(this->curr_answer, needs_spanish_hack) == utf8lower(trivia_message, needs_spanish_hack)) || (!PCRE("^\\$(\\d+)$").Match(this->curr_answer) && !PCRE("^(\\d+)$").Match(this->curr_answer) && (this->curr_answer.length() > 5 && (utf8lower(this->curr_answer, needs_spanish_hack) == utf8lower(trivia_message, needs_spanish_hack) || creator->levenstein(trivia_message, this->curr_answer) < 2))))) {
	
					this->curr_answer = "";
	
					/* Correct answer */
					this->gamestate = TRIV_ANSWER_CORRECT;
					creator->CacheUser(m.author_id, this->channel_id);
					time_t time_to_answer = time(NULL) - this->asktime;
					std::string pts = (this->score > 1 ? _("POINTS", settings) : _("POINT", settings));
					time_t submit_time = this->recordtime;
					int32_t score = this->score;
	
					std::string ans_message;
					ans_message.append(fmt::format(_("NORM_CORRECT", settings), this->original_answer, score, pts, time_to_answer));
					if (time_to_answer < this->recordtime) {
						ans_message.append(fmt::format(_("RECORD_TIME", settings), m.username));
						submit_time = time_to_answer;
						}
					int32_t newscore = update_score(m.author_id, this->guild_id, submit_time, this->curr_qid, score);
					ans_message.append(fmt::format(_("SCORE_UPDATE", settings), m.username, newscore ? newscore : score));
	
					std::string teamname = get_current_team(m.author_id);
					if (!empty(teamname)) {
						add_team_points(teamname, score, m.author_id);
						int32_t newteamscore = get_team_points(teamname);
						ans_message.append(fmt::format(_("TEAM_SCORE", settings), teamname, score, pts, newteamscore));
					}
	
					if (this->last_to_answer == m.author_id) {
						/* Amend current streak */
						this->streak++;
						ans_message.append(fmt::format(_("ON_A_STREAK", settings), m.username, this->streak));
						streak_t s = get_streak(m.author_id, this->guild_id);
						if (this->streak > s.personalbest) {
							ans_message.append(_("BEATEN_BEST", settings));
							change_streak(m.author_id, this->guild_id, this->streak);
					} else {
							ans_message.append(fmt::format(_("NOT_THERE_YET", settings), s.personalbest));
						}
						if (this->streak > s.bigstreak && s.topstreaker != m.author_id) {
							ans_message.append(fmt::format(_("STREAK_BEATDOWN", settings), m.username, s.topstreaker, this->streak));
						}
					} else if (this->streak > 1 && this->last_to_answer && this->last_to_answer != m.author_id) {
						ans_message.append(fmt::format(_("STREAK_ENDER", settings), m.username, this->last_to_answer, this->streak));
						this->streak = 1;
					} else {
						this->streak = 1;
					}

					/* Update last person to answer */
					this->last_to_answer = m.author_id;
	
						creator->SimpleEmbed(settings, ":thumbsup:", ans_message, channel_id, fmt::format(_("CORRECT", settings), m.username), this->answer_image);

					if (log_question_index(this->guild_id, this->channel_id, this->round, this->streak, this->last_to_answer, this->gamestate, this->curr_qid)) {
						StopGame(settings);
						return;
					}
				}
			}
		}
	}
}

void state_t::tick()
{
	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	if (!is_valid()) {
		creator->GetBot()->core.log->warn("Invalid state (missing channel or guild), ending round.");
		log_game_end(guild_id, channel_id);
		terminating = true;
		gamestate = TRIV_END;
		return;
	}
	try {
		switch (gamestate) {
			case TRIV_ASK_QUESTION:
				if (!terminating) {
					if (round % 10 == 0) {
						do_insane_round(false);
					} else {
						do_normal_round(false);
					}
				}
			break;
			case TRIV_FIRST_HINT:
				if (!terminating) {
					do_first_hint();
				}
			break;
			case TRIV_SECOND_HINT:
				if (!terminating) {
					do_second_hint();
				}
			break;
			case TRIV_TIME_UP:
				if (!terminating) {
					do_time_up();
				}
			break;
			case TRIV_ANSWER_CORRECT:
				if (!terminating) {
					do_answer_correct();
				}
			break;
			case TRIV_END:
				do_end_game();
			break;
			default:
				creator->GetBot()->core.log->warn("Invalid state '{}', ending round.", gamestate);
				gamestate = TRIV_END;
				terminating = true;
			break;
		}

		if (gamestate == TRIV_ANSWER_CORRECT) {
			/* Correct answer shortcuts the timer */
			this->next_tick = time(NULL) + 5;
		} else {
			if (gamestate == TRIV_ASK_QUESTION && this->interval == TRIV_INTERVAL) {
				this->next_tick = time(NULL) + settings.question_interval;
			} else {
				this->next_tick = time(NULL) + this->interval;
			}
		}
	}
	catch (std::exception &e) {
		creator->GetBot()->core.log->debug("state_t exception! - {}", e.what());
	}
	catch (...) {
		creator->GetBot()->core.log->debug("state_t exception! - non-object");
	}
}


