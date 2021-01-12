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
#include "piglatin.h"

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
	streak = currstreak;
	asktime = 0;
	found = false;
	interval = _interval;
	insane_num = 0;
	insane_left = 0;
	next_quickfire = 0;
	insane.clear();
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

/* Returns true if the state_t is associated with a valid channel and guild */
bool state_t::is_valid()
{
	return creator && creator->GetBot()->core.find_guild(guild_id) && creator->GetBot()->core.find_channel(channel_id);
}

/* Handle inbound message */
void state_t::handle_message(const in_msg& m)
{
	if (this->terminating || !creator)
		return;

	if (gamestate == TRIV_ASK_QUESTION || gamestate == TRIV_FIRST_HINT || gamestate == TRIV_SECOND_HINT || gamestate == TRIV_TIME_UP) {
		guild_settings_t settings = creator->GetGuildSettings(guild_id);

		if (is_insane_round()) {

			/* Insane round */
			bool done = false;
			auto i = this->insane.find(utf8lower(m.msg, settings.language == "es"));
			if (i != this->insane.end()) {
				this->insane.erase(i);

				if (--this->insane_left < 1) {
					done = true;
					creator->SimpleEmbed(settings, ":thumbsup:", fmt::format(_("LAST_ONE", settings), m.username), channel_id);
					if (round <= this->numquestions - 1) {
						round++;
						gamestate = TRIV_ANSWER_CORRECT;
					} else {
						gamestate = TRIV_END;
					}
				} else {
					creator->SimpleEmbed(settings, ":thumbsup:", fmt::format(_("INSANE_CORRECT", settings), m.username, m.msg, this->insane_left, this->insane_num), channel_id);
				}
				update_score_only(m.author_id, guild_id, 1, channel_id);
				creator->CacheUser(m.author_id, channel_id);
				if (done) {
					/* Only save state if all answers have been found */
					if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, 0)) {
						StopGame(settings);
						return;
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

			/* Answer on channel is an exact match for the current answer and/or it is numeric, OR, it's non-numeric and has a levenstein distance near enough to the current answer (account for misspellings) */
			if (!question.answer.empty() && ((trivia_message.length() >= question.answer.length() && utf8lower(question.answer, needs_spanish_hack) == utf8lower(trivia_message, needs_spanish_hack)) || (!PCRE("^\\$(\\d+)$").Match(question.answer) && !PCRE("^(\\d+)$").Match(question.answer) && (question.answer.length() > 5 && (utf8lower(question.answer, needs_spanish_hack) == utf8lower(trivia_message, needs_spanish_hack) || creator->levenstein(trivia_message, question.answer) < 2))))) {

				question.answer = "";

				/* Correct answer */
				gamestate = TRIV_ANSWER_CORRECT;
				creator->CacheUser(m.author_id, channel_id);
				time_t time_to_answer = time(NULL) - this->asktime;
				std::string pts = (this->score > 1 ? _("POINTS", settings) : _("POINT", settings));
				time_t submit_time = question.recordtime;
				int32_t score = this->score;

				std::string ans_message;
				ans_message.append(fmt::format(_("NORM_CORRECT", settings), this->original_answer, score, pts, time_to_answer));
				if (time_to_answer < question.recordtime) {
					ans_message.append(fmt::format(_("RECORD_TIME", settings), m.username));
					submit_time = time_to_answer;
					}
				int32_t newscore = update_score(m.author_id, guild_id, submit_time, question.id, score);
				ans_message.append(fmt::format(_("SCORE_UPDATE", settings), m.username, newscore ? newscore : score));

				std::string teamname = get_current_team(m.author_id);
				if (!empty(teamname)) {
					add_team_points(teamname, score, m.author_id);
					int32_t newteamscore = get_team_points(teamname);
					ans_message.append(fmt::format(_("TEAM_SCORE", settings), teamname, score, pts, newteamscore));
				}

				if (last_to_answer == m.author_id) {
					/* Amend current streak */
					streak++;
					ans_message.append(fmt::format(_("ON_A_STREAK", settings), m.username, streak));
					streak_t s = get_streak(m.author_id, guild_id);
					if (streak > s.personalbest) {
						ans_message.append(_("BEATEN_BEST", settings));
						change_streak(m.author_id, guild_id, streak);
					} else {
						ans_message.append(fmt::format(_("NOT_THERE_YET", settings), s.personalbest));
					}
					if (streak > s.bigstreak && s.topstreaker != m.author_id) {
						ans_message.append(fmt::format(_("STREAK_BEATDOWN", settings), m.username, s.topstreaker, streak));
					}
				} else if (streak > 1 && last_to_answer && last_to_answer != m.author_id) {
					/* Player beat someone elses streak */
					ans_message.append(fmt::format(_("STREAK_ENDER", settings), m.username, last_to_answer, streak));
					streak = 1;
				} else {
					streak = 1;
				}

				/* Update last person to answer */
				last_to_answer = m.author_id;

					creator->SimpleEmbed(settings, ":thumbsup:", ans_message, channel_id, fmt::format(_("CORRECT", settings), m.username), question.answer_image);

				if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
					StopGame(settings);
					return;
				}
			}
		}
	}
}

/* Games are a finite state machine, where the tick() function is called periodically on each state_t object. The next_tick
 * value indicates when the tick() method should next be called, if the terminating flag is set then the object is removed
 * from the list of current games. Each cluster only stores a game list for itself.
 */
void state_t::tick()
{
	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	if (!is_valid()) {
		log_game_end(guild_id, channel_id);
		terminating = true;
		gamestate = TRIV_END;
		return;
	}
	try {
		switch (gamestate) {
			case TRIV_ASK_QUESTION:
				if (!terminating) {
					if (is_insane_round()) {
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
			next_tick = time(NULL) + 5;
		} else {
			/* Set time for next tick */
			if (gamestate == TRIV_ASK_QUESTION && interval == TRIV_INTERVAL) {
				next_tick = time(NULL) + settings.question_interval;
			} else {
				next_tick = time(NULL) + interval;
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

/* State machine event for insane round question */
void state_t::do_insane_round(bool silent)
{
	creator->GetBot()->core.log->debug("do_insane_round: G:{} C:{}", guild_id, channel_id);

	if (round >= numquestions) {
		gamestate = TRIV_END;
		score = 0;
		return;
	}

	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	// Attempt up to 5 times to fetch an insane round, with 3 second delay between tries
	std::vector<std::string> answers;
	uint32_t tries = 0;
	do {
		answers = fetch_insane_round(question.id, guild_id, settings);
		if (answers.size() >= 2) {
			// Got a valid answer list, bail out
			tries = 0;
			break;
		} else {
			// Request error, try again
			tries++;
			sleep(3);
		}
	} while (tries < 5);
	// 5 or more tries stops the game
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id) || tries >= 5) {
		StopGame(settings);
		return;
	}

	insane = {};
	for (auto n = answers.begin(); n != answers.end(); ++n) {
		if (n == answers.begin()) {
			question.question = trim(*n);
		} else {
			if (*n != "***END***") {
				insane[utf8lower(trim(*n), settings.language == "es")] = true;
			}
		}
	}
	insane_left = insane.size();
	insane_num = insane.size();
	gamestate = TRIV_FIRST_HINT;


	creator->EmbedWithFields(settings, fmt::format(_("QUESTION_COUNTER", settings), round, numquestions - 1), {{_("INSANE_ROUND", settings), fmt::format(_("INSANE_ANS_COUNT", settings), insane_num), false}, {_("QUESTION", settings), question.question, false}}, channel_id, fmt::format("https://triviabot.co.uk/report/?c={}&g={}&insane={}", channel_id, guild_id, question.id + channel_id));
}

/* State machine event for normal round question */
void state_t::do_normal_round(bool silent)
{
	creator->GetBot()->core.log->debug("do_normal_round: G:{} C:{}", guild_id, channel_id);

	if (round >= numquestions) {
		gamestate = TRIV_END;
		score = 0;
		return;
	}

	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	creator->GetBot()->core.log->debug("do_normal_round: fetch_question: '{}'", shuffle_list[round - 1]);
	question = question_t::fetch(from_string<int64_t>(shuffle_list[round - 1], std::dec), guild_id, settings);

	if (question.id == 0) {
		gamestate = TRIV_END;
		score = 0;
		question.answer = "";
		creator->GetBot()->core.log->warn("do_normal_round(): Attempted to retrieve question id {} but got a malformed response. Round was aborted.", shuffle_list[round - 1]);
		if (!silent) {
			creator->EmbedWithFields(settings, fmt::format(_("Q_FETCH_ERROR", settings)), {{_("Q_SPOOPY", settings), _("Q_CONTACT_DEVS", settings), false}, {_("ROUND_STOPPING", settings), _("ERROR_BROKE_IT", settings), false}}, channel_id);
		}
		return;
	}

	if (question.question != "") {
		asktime = time(NULL);
		question.answer = trim(question.answer);
		original_answer = question.answer;
		std::string t = creator->conv_num(question.answer, settings);
		if (creator->is_number(t) && t != "0") {
			question.answer = t;
		}
		question.answer = creator->tidy_num(question.answer);
		/* Handle hints */
		if (question.customhint1.empty()) {
			/* No custom first hint, build one */
			question.customhint1 = "";
			if (creator->is_number(question.answer)) {
				question.customhint1 = creator->MakeFirstHint(question.answer, settings);
			} else {
				int32_t r = creator->random(1, 12);
				if (settings.language == "bg") {
					question.customhint1 = fmt::format(_("SCRAMBLED_ANSWER", settings), question.shuffle1);
				} else {
					if (r <= 4) {
						/* Leave only capital letters */
						question.customhint1 = question.answer;
						for (int x = 0; x < question.customhint1.length(); ++x) {
							if ((question.customhint1[x] >= 'a' && question.customhint1[x] <= 'z') || question.customhint1[x] == '1' || question.customhint1[x] == '3' || question.customhint1[x] == '5' || question.customhint1[x]  == '7' || question.customhint1[x] == '9') {
									question.customhint1[x] = '#';
							}
						}
					} else if (r >= 5 && r <= 8) {
					question.customhint1 = creator->letterlong(question.answer, settings);
					} else {
						question.customhint1 = fmt::format(_("SCRAMBLED_ANSWER", settings), question.shuffle1);
					}
				}
			}
		}
		if (question.customhint2.empty()) {
			/* No custom second hint, build one */
			question.customhint2 = "";
			if (creator->is_number(question.answer) || PCRE("^\\$(\\d+)$").Match(question.answer)) {
				std::string currency;
				std::vector<std::string> matches;
				question.customhint2 = question.answer;
				if (PCRE("^\\$(\\d+)$").Match(question.customhint2, matches)) {
					question.customhint2 = matches[1];
					currency = "$";
				}
				question.customhint2 = currency + question.customhint2;
				int32_t r = creator->random(1, 13);
				if ((r < 3 && from_string<int32_t>(question.customhint2, std::dec) <= 10000)) {
					question.customhint2 = creator->dec_to_roman(from_string<unsigned int>(question.customhint2, std::dec), settings);
				} else if ((r >= 3 && r < 6) || from_string<int32_t>(question.customhint2, std::dec) > 10000) {
					question.customhint2 = fmt::format(_("HEX", settings), from_string<int32_t>(question.customhint2, std::dec));
				} else if (r >= 6 && r <= 10) {
					question.customhint2 = fmt::format(_("OCT", settings), from_string<int32_t>(question.customhint2, std::dec));
				} else {
					question.customhint2 = fmt::format(_("BIN", settings), from_string<int32_t>(question.customhint2, std::dec));
				}
			} else {
				int32_t r = creator->random(1, 12);
				if (r <= 4 && settings.language != "bg") {
					/* Transpose only the vowels */
					question.customhint2 = question.answer;
					for (int x = 0; x < question.customhint2.length(); ++x) {
						if (toupper(question.customhint2[x]) == 'A' || toupper(question.customhint2[x]) == 'E' || toupper(question.customhint2[x]) == 'I' || toupper(question.customhint2[x]) == 'O' || toupper(question.customhint2[x]) == 'U' || toupper(question.customhint2[x]) == '2' || toupper(question.customhint2[x]) == '4' || toupper(question.customhint2[x]) == '6' || toupper(question.customhint2[x]) == '8' || toupper(question.customhint2[x]) == '0') {
							question.customhint2[x] = '#';
						}
					}
				} else if ((r >= 5 && r <= 6) || settings.language != "en") {
					question.customhint2 = creator->vowelcount(question.answer, settings);
				} else {
					/* settings.language check for en above, because piglatin only makes sense in english */
					question.customhint2 = piglatin(question.answer);
				}

			}
		}

		if (!silent) {
			creator->EmbedWithFields(settings, fmt::format(_("QUESTION_COUNTER", settings), round, numquestions - 1), {{_("CATEGORY", settings), question.catname, false}, {_("QUESTION", settings), question.question, false}}, channel_id, fmt::format("https://triviabot.co.uk/report/?c={}&g={}&normal={}", channel_id, guild_id, question.id + channel_id), question.question_image);
		}

	} else {
		if (!silent) {
			creator->SimpleEmbed(settings, ":ghost:", _("BRAIN_BROKE_IT", settings), channel_id, _("FETCH_Q", settings));
		}
	}

	score = (hintless ? 6 : (interval == TRIV_INTERVAL ? 4 : 8));
	/* Advance state to first hint, if hints are enabled, otherwise go straight to time up */
	if (hintless) {
		gamestate = TRIV_TIME_UP;
	} else {
		gamestate = TRIV_FIRST_HINT;
	}
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}

}

/* State machine event for first hint */
void state_t::do_first_hint()
{
	creator->GetBot()->core.log->debug("do_first_hint: G:{} C:{}", guild_id, channel_id);
	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	if (is_insane_round()) {
		/* Insane round countdown */
		creator->SimpleEmbed(settings, ":clock10:", fmt::format(_("SECS_LEFT", settings), interval * 2), channel_id);
	} else {
		/* First hint, not insane round */
		creator->SimpleEmbed(settings, ":clock10:", question.customhint1, channel_id, _("FIRST_HINT", settings));
	}
	gamestate = TRIV_SECOND_HINT;
	score = (interval == TRIV_INTERVAL ? 2 : 4);
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}
}

/* State machine event for second hint */
void state_t::do_second_hint()
{
	creator->GetBot()->core.log->debug("do_second_hint: G:{} C:{}", guild_id, channel_id);
	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	if (is_insane_round()) {
		/* Insane round countdown */
		creator->SimpleEmbed(settings, ":clock1030:", fmt::format(_("SECS_LEFT", settings), interval), channel_id);
	} else {
		/* Second hint, not insane round */
		creator->SimpleEmbed(settings, ":clock1030:", question.customhint2, channel_id, _("SECOND_HINT", settings));
	}
	gamestate = TRIV_TIME_UP;
	score = (interval == TRIV_INTERVAL ? 1 : 2);
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}
}

/* State machine event for question time up */
void state_t::do_time_up()
{
	creator->GetBot()->core.log->debug("do_time_up: G:{} C:{}", guild_id, channel_id);
	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	{
		if (is_insane_round()) {
			/* Insane round */
			int32_t found = insane_num - insane_left;
			creator->SimpleEmbed(settings, ":alarm_clock:", fmt::format(_("INSANE_FOUND", settings), found), channel_id, _("TIME_UP", settings));
		} else if (question.answer != "") {
			/* Not insane round */
			creator->SimpleEmbed(settings, ":alarm_clock:", fmt::format(_("ANS_WAS", settings), question.answer), channel_id, _("OUT_OF_TIME", settings), question.answer_image);
		}
		/* FIX: You can only lose your streak on a non-insane round */
		if (question.answer != "" && !is_insane_round() && streak > 1 && last_to_answer) {
			creator->SimpleEmbed(settings, ":octagonal_sign:", fmt::format(_("STREAK_SMASHED", settings), fmt::format("<@{}>", last_to_answer), streak), channel_id);
		}

		/* Clear current answer so the question becomes unanswerable */
		if (question.answer != "") {
			question.answer = "";
			/* For non-insane rounds reset the streak back to 1 */
			if (!is_insane_round()) {
				last_to_answer = 0;
				streak = 1;
			}
		}
	}

	if (round <= numquestions - 2) {
		creator->SimpleEmbed(settings, "<a:loading:658667224067735562>", fmt::format(_("COMING_UP", settings), interval == TRIV_INTERVAL ? settings.question_interval : interval), channel_id, _("REST", settings));
	}

	gamestate = (round > numquestions ? TRIV_END : TRIV_ASK_QUESTION);
	round++;
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}
}

/* State machine event for answer correct */
void state_t::do_answer_correct()
{
	creator->GetBot()->core.log->debug("do_answer_correct: G:{} C:{}", guild_id, channel_id);

	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	{
		round++;
		question.answer = "";
		if (round <= numquestions - 2) {
			creator->SimpleEmbed(settings, "<a:loading:658667224067735562>", fmt::format(_("COMING_UP", settings), interval), channel_id, _("REST", settings));
		}
	}

	gamestate = TRIV_ASK_QUESTION;
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}
}

/* State machine event for end of game */
void state_t::do_end_game()
{
	creator->GetBot()->core.log->debug("do_end_game: G:{} C:{}", guild_id, channel_id);

	log_game_end(guild_id, channel_id);

	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	creator->GetBot()->core.log->info("End of game on guild {}, channel {} after {} seconds", guild_id, channel_id, time(NULL) - start_time);
	creator->SimpleEmbed(settings, ":stop_button:", fmt::format(_("END1", settings), numquestions - 1), channel_id, _("END_TITLE", settings));
	creator->show_stats(guild_id, channel_id);

	terminating = true;
}

/* Returns true if the current round is an insane round */
bool state_t::is_insane_round()
{
	/* Insane rounds are every tenth question */
	return (round % 10 == 0);
}
