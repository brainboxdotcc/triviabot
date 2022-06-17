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
#include <unistd.h>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include <sporks/database.h>
#include "state.h"
#include "trivia.h"
#include "webrequest.h"
#include "wlower.h"
#include "piglatin.h"
#include "time.h"

std::unordered_map<uint64_t, bool> banlist;

in_msg::in_msg(const std::string &m, uint64_t author, bool mention, const std::string &_username, dpp::user u, dpp::guild_member gm) : msg(m), author_id(author), mentions_bot(mention), username(_username), user(u), member(gm)
{
}

state_t::state_t()
{
}

state_t::state_t(TriviaModule* _creator, uint32_t questions, uint32_t currstreak, uint64_t lastanswered, uint32_t question_index, uint32_t _interval, uint64_t _channel_id, bool _hintless, const std::vector<std::string> &_shuffle_list, trivia_state_t startstate,  uint64_t _guild_id) :

	next_tick(time(NULL)),
	creator(_creator),
	terminating(false),
	channel_id(_channel_id),
	guild_id(_guild_id),
	numquestions(questions),
       	round(question_index),
	score(0),
	start_time(time(NULL)),
	shuffle_list(_shuffle_list),
	gamestate(startstate),
	streak(currstreak),
	asktime(0.0),
	found(false),
	interval(_interval),
	insane_num(0),
	insane_left(0),
	next_quickfire(0),
	hintless(_hintless),
	last_to_answer(lastanswered)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("state_t::state_t()"));
	insane.clear();
	db::resultset rs = db::query("SELECT name, dayscore FROM scores WHERE guild_id = ? AND dayscore > 0", {guild_id});
	if (rs.size()) {
		for (auto s = rs.begin(); s != rs.end(); ++s) {
			scores[from_string<uint64_t>((*s)["name"], std::dec)] = from_string<uint64_t>((*s)["dayscore"], std::dec);
		}
		creator->GetBot()->core->log(dpp::ll_debug, fmt::format("Cached {} guild scores for game on channel {}", rs.size(), channel_id));
	}
	/* This doesnt need to be done every time */
	db::resultset rs2 = db::query("SELECT * FROM bans WHERE play_ban = 1", {});
	if (rs2.size()) {
		banlist.clear();
		for (auto s = rs2.begin(); s != rs2.end(); ++s) {
			banlist[from_string<uint64_t>((*s)["snowflake_id"], std::dec)] = true;
		}
	}
}

uint64_t state_t::get_score(dpp::snowflake uid)
{
	auto i = scores.find(uid);
	if (i != scores.end()) {
		return i->second;
	} else {
		return 0;
	}
}

void state_t::set_score(dpp::snowflake uid, uint64_t score)
{
	scores[uid] = score;
}

void state_t::add_score(dpp::snowflake uid, uint64_t addition)
{
	uint64_t oldscore = get_score(uid);
	set_score(uid, oldscore + addition);
}

void state_t::clear_insane_stats()
{
	insane_round_stats = {};
}

void state_t::add_insane_stats(dpp::snowflake uid)
{
	auto i = insane_round_stats.find(uid);
	if (i != insane_round_stats.end()) {
		insane_round_stats[uid] = i->second + 1;
	} else {
		insane_round_stats[uid] = 1;
	}
}


std::string state_t::_(const std::string &k, const guild_settings_t& settings)
{
	return creator->_(k, settings);
}

void state_t::queue_message(const guild_settings_t& settings, const std::string &message, uint64_t author_id, const std::string &username, bool mentions_bot, dpp::user u, dpp::guild_member gm)
{
	// FIX: Check termination atomic flag to avoid race where object is deleted but its handle_message gets called
	if (!terminating) {
		handle_message(in_msg(message, author_id, mentions_bot, username, u, gm), settings);
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
	return creator && dpp::find_guild(guild_id);
}

/* Returs the number of players to attempt a question (right or wrong) in the past 60 seconds */
uint32_t state_t::get_activity()
{
	time_t now = time(NULL);
	uint32_t act = 0;
	for (const auto &a : activity) {
		if (now - a.second < 60) {
			act++;
		}
	}
	return act;
}

/* Returns true if the bot should drop a coin, probability is a percentage based on number of active players
 * in last 60 second rolling window
 */
bool state_t::should_drop_coin()
{
	uint32_t activity = get_activity();
	uint32_t probability = 0;
	if (activity >= 2 && activity <= 3) {
		probability = 1;
	} else if (activity >= 4 && activity <= 7) {
		probability = 2;
	} else if (activity >= 8 && activity <= 11) {
		probability = 3;
	} else if (activity >= 12 && activity <= 15) {
		probability = 4;
	} else if (activity >= 16) {
		probability = 5;
	}
	return (creator->random(1, 100) <= probability);
}

void state_t::record_activity(uint64_t user_id)
{
	activity[user_id] = time(NULL);		
}

bool state_t::user_banned(uint64_t user_id)
{
	return (banlist.find(user_id) != banlist.end());
}

/* Handle inbound message */
void state_t::handle_message(const in_msg& m, const guild_settings_t& settings)
{
	if (this->terminating || !creator)
		return;

	if (user_banned(m.author_id)) {
		return;
	}

	if (gamestate == TRIV_ASK_QUESTION || gamestate == TRIV_FIRST_HINT || gamestate == TRIV_SECOND_HINT || gamestate == TRIV_TIME_UP) {

		/* Flag activity of user */
		record_activity(m.author_id);

		if (is_insane_round(settings)) {

			/* Insane round */
			std::string answered = utf8lower(removepunct(m.msg), settings.language == "es");
			auto i = this->insane.find(answered);
			if (i != this->insane.end()) {
				bool done = false;

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
					creator->SimpleEmbed(settings, ":thumbsup:", fmt::format(_("INSANE_CORRECT", settings), m.username, homoglyph(m.msg), this->insane_left, this->insane_num), channel_id);
				}
				creator->CacheUser(m.author_id, m.user, m.member, channel_id);
				update_score_only(m.author_id, guild_id, 1, channel_id);
				add_score(m.author_id, 1);
				add_insane_stats(m.author_id);

				if (done) {
					do_insane_board(settings);
					clear_insane_stats();
					/* Only save state if all answers have been found */
					if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, 0)) {
						StopGame(settings);
						return;
					}
				}
			}
		} else {
			/* Normal round */
			std::string trivia_message = removepunct(m.msg);
			std::string answer = removepunct(question.answer);

			int x = from_string<int>(creator->conv_num(m.msg, settings), std::dec);
			if (x > 0) {
				trivia_message = creator->conv_num(m.msg, settings);
			}
			trivia_message = creator->tidy_num(trivia_message);
			bool needs_spanish_hack = (settings.language == "es");

			/* Answer on channel is an exact match for the current answer and/or it is numeric, OR, it's non-numeric and has a levenstein distance near enough to the current answer (account for misspellings) */
			if (!answer.empty() && 
					(
					 /* Answer is a direct match */
					 (trivia_message.length() >= answer.length() && utf8lower(answer, needs_spanish_hack) == utf8lower(trivia_message, needs_spanish_hack))
					 ||
					 
					 (!PCRE("^\\$(\\d+)$").Match(answer) && !PCRE("^(\\d+)$").Match(answer) && (answer.length() > 5 &&
					(utf8lower(answer, needs_spanish_hack) == utf8lower(trivia_message, needs_spanish_hack) ||
					(trivia_message.length() >= answer.length() && creator->levenstein(trivia_message, answer) < 2))))
					 )) {

				question.answer = "";

				/* Correct answer */
				gamestate = TRIV_ANSWER_CORRECT;
				creator->CacheUser(m.author_id, m.user, m.member, channel_id);
				double time_to_answer = time_f() - this->asktime;
				std::string pts = (this->score > 1 ? _("POINTS", settings) : _("POINT", settings));
				double submit_time = question.recordtime;
				uint32_t score = this->score;

				std::string ans_message;

				ans_message.append(fmt::format(_("NORM_CORRECT", settings), homoglyph(this->original_answer), score, pts, time_to_answer));
				if (time_to_answer < question.recordtime) {
					ans_message.append(fmt::format(_("RECORD_TIME", settings), m.username));
					submit_time = time_to_answer;
				}
				update_score(m.author_id, guild_id, submit_time, question.id, score, question.guild_id != 0);
				add_score(m.author_id, score);
				uint64_t newscore = get_score(m.author_id);
				ans_message.append(fmt::format(_("SCORE_UPDATE", settings), m.username, newscore ? newscore : score));

				std::string teamname = get_current_team(m.author_id);
				if (!empty(teamname) && question.guild_id == 0) {
					add_team_points(teamname, score, m.author_id);
					uint32_t newteamscore = get_team_points(teamname);
					ans_message.append(fmt::format(_("TEAM_SCORE", settings), teamname, score, pts, newteamscore));
				}

				if (last_to_answer == m.author_id) {
					/* Amend current streak */
					streak++;
					ans_message.append(fmt::format(_("ON_A_STREAK", settings), m.username, streak));
					streak_t s = get_streak(m.author_id, guild_id);	// Guild streak
					if (streak > s.personalbest) {
						// Guild streak
						ans_message.append(_("BEATEN_BEST", settings));
						change_streak(m.author_id, guild_id, streak);
					} else {
						ans_message.append(fmt::format(_("NOT_THERE_YET", settings), s.personalbest));
					}
					if (question.guild_id == 0) {
						streak_t s2 = get_streak(m.author_id);		// Global streak
						if (streak > s2.personalbest) {
							// Global streak
							change_streak(m.author_id, streak);
						}
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

				bool coin = question.guild_id == 0 && should_drop_coin();
				std::string thumbnail = "";
				if (coin) {
					/* Player got a coin drop! */
					thumbnail = "https://triviabot.co.uk/images/coin.gif";
					/* TODO: Award 100 + rand coins */
					uint32_t coins = 100 + creator->random(0, 50);
					db::resultset rs = db::query("SELECT * FROM coins WHERE user_id = ?", {m.author_id});
					db::backgroundquery("INSERT INTO coins (user_id, balance) VALUES(?, ?) ON DUPLICATE KEY UPDATE balance = balance + ?", {m.author_id, coins});
					uint64_t current = 0;
					if (rs.size()) {
						current = from_string<uint64_t>(rs[0]["balance"], std::dec);
					}
					ans_message.append("\n\n**").append(fmt::format(_(std::string("COIN_DROP_") + std::to_string(creator->random(1, 4)), settings), m.username, coins, current + coins)).append("**");

				}

				/* Update last person to answer */
				last_to_answer = m.author_id;

                		if (round + 1 <= numquestions - 2) {
		                        ans_message += "\n\n" + fmt::format(_("COMING_UP", settings), interval);
		                }	

				creator->SimpleEmbed(settings, ":thumbsup:", ans_message, channel_id, fmt::format(_("CORRECT", settings), m.username), question.answer_image, thumbnail);

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
		if (question_cache.empty()) {
				build_question_cache(settings);
		}
		switch (gamestate) {
			case TRIV_ASK_QUESTION:
				if (!terminating) {
					if (is_insane_round(settings)) {
						do_insane_round(false, settings);
					} else {
						do_normal_round(false, settings);
					}
				}
			break;
			case TRIV_FIRST_HINT:
				if (!terminating) {
					do_first_hint(settings);
				}
			break;
			case TRIV_SECOND_HINT:
				if (!terminating) {
					do_second_hint(settings);
				}
			break;
			case TRIV_TIME_UP:
				if (!terminating) {
					do_time_up(settings);
				}
			break;
			case TRIV_ANSWER_CORRECT:
				if (!terminating) {
					do_answer_correct(settings);
				}
			break;
			case TRIV_END:
				do_end_game(settings);
			break;
			default:
				creator->GetBot()->core->log(dpp::ll_warning, fmt::format("Invalid state '{}', ending round.", gamestate));
				gamestate = TRIV_END;
				terminating = true;
			break;
		}

		if (gamestate == TRIV_ANSWER_CORRECT) {
			/* Correct answer shortcuts the timer */
			next_tick = time(NULL);
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
		creator->GetBot()->core->log(dpp::ll_debug, fmt::format("state_t exception! - {}", e.what()));
	}
	catch (...) {
		creator->GetBot()->core->log(dpp::ll_debug, fmt::format("state_t exception! - non-object"));
	}
}

/* State machine event for insane round question */
void state_t::do_insane_round(bool silent, const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_insane_round: G:{} C:{}", guild_id, channel_id));

	if (round >= numquestions) {
		gamestate = TRIV_END;
		score = 0;
		return;
	}

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
				std::string a = utf8lower(removepunct(*n), settings.language == "es");
				insane[a] = true;
			}
		}
	}
	insane_left = insane.size();
	insane_num = insane.size();
	gamestate = TRIV_FIRST_HINT;


	creator->EmbedWithFields(settings, fmt::format(_("QUESTION_COUNTER", settings), round, numquestions - 1), {{_("INSANE_ROUND", settings), fmt::format(_("INSANE_ANS_COUNT", settings), insane_num), false}, {_("QUESTION", settings), question.question, false}}, channel_id, fmt::format("https://triviabot.co.uk/report/?c={}&g={}&insane={}", channel_id, guild_id, question.id + channel_id));
}

/* State machine event for normal round question */
void state_t::do_normal_round(bool silent, const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_normal_round: G:{} C:{}", guild_id, channel_id));

	if (round >= numquestions) {
		gamestate = TRIV_END;
		score = 0;
		return;
	}

	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_normal_round: fetch_question: '{}'", shuffle_list[round - 1]));
	question = question_cache[round - 1];
	db::backgroundquery("INSERT INTO stats (id, lastasked, timesasked, lastcorrect, record_time) VALUES('?',UNIX_TIMESTAMP(),1,NULL,60000) ON DUPLICATE KEY UPDATE lastasked = UNIX_TIMESTAMP(), timesasked = timesasked + 1 ", {question.id});
	db::backgroundquery("UPDATE counters SET asked = asked + 1", {});

	if (question.id == 0) {
		gamestate = TRIV_END;
		score = 0;
		question.answer = "";
		creator->GetBot()->core->log(dpp::ll_warning, fmt::format("do_normal_round(): Attempted to retrieve question id {} but got a malformed response. Round was aborted.", shuffle_list[round - 1]));
		if (!silent) {
			creator->EmbedWithFields(settings, fmt::format(_("Q_FETCH_ERROR", settings)), {{_("Q_SPOOPY", settings), _("Q_CONTACT_DEVS", settings), false}, {_("ROUND_STOPPING", settings), _("ERROR_BROKE_IT", settings), false}}, channel_id);
		}
		return;
	}

	if (question.question != "") {
		asktime = time_f();
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
				uint32_t r = creator->random(1, 12);
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
					} else if (r <= 8) {
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
				uint32_t r = creator->random(1, 13);
				if ((r < 3 && from_string<uint32_t>(question.customhint2, std::dec) <= 10000)) {
					question.customhint2 = creator->dec_to_roman(from_string<unsigned int>(question.customhint2, std::dec), settings);
				} else if ((r >= 3 && r < 6) || from_string<uint32_t>(question.customhint2, std::dec) > 10000) {
					question.customhint2 = fmt::format(_("HEX", settings), from_string<uint32_t>(question.customhint2, std::dec));
				} else if (r >= 6 && r <= 10) {
					question.customhint2 = fmt::format(_("OCT", settings), from_string<uint32_t>(question.customhint2, std::dec));
				} else {
					question.customhint2 = fmt::format(_("BIN", settings), from_string<uint32_t>(question.customhint2, std::dec));
				}
			} else {
				uint32_t r = creator->random(1, 12);
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
void state_t::do_first_hint(const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_first_hint: G:{} C:{}", guild_id, channel_id));
	if (is_insane_round(settings)) {
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

void state_t::do_insane_board(const guild_settings_t& settings) {
	/* Last round was an insane round, display insane round score table embed if there were any participants */
	std::string desc;
	uint32_t i = 1;
	std::multimap<uint64_t, dpp::snowflake> ordered;
	/* Sort by largest first */
	for (auto s = insane_round_stats.begin(); s != insane_round_stats.end(); ++s) {
		ordered.insert(std::make_pair(s->second, s->first));
	}
	for (std::multimap<uint64_t, dpp::snowflake>::reverse_iterator sc = ordered.rbegin(); sc != ordered.rend(); ++sc) {
		db::resultset info = db::query("SELECT username, discriminator, get_emojis(trivia_user_cache.snowflake_id) as emojis FROM trivia_user_cache WHERE snowflake_id = ?", {sc->second});
		if (info.size()) {
			desc += fmt::format("**#{0}** `{1}#{2:04d}` (*{3}*) {4}\n", i, info[0]["username"], from_string<uint32_t>(info[0]["discriminator"], std::dec), Comma(sc->first), info[0]["emojis"]);
		}
		i++;
	}
	if (!desc.empty()) {
		creator->SimpleEmbed(settings, "", desc, channel_id, _("INSANESTATS", settings));
	}
	db::backgroundquery("DELETE FROM insane_round_statistics WHERE channel_id = '?'", {channel_id});
}

/* State machine event for second hint */
void state_t::do_second_hint(const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_second_hint: G:{} C:{}", guild_id, channel_id));
	if (is_insane_round(settings)) {
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

void state_t::build_question_cache(const guild_settings_t& settings)
{
	double start = dpp::utility::time_f();
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("Build question cache start: G:{} C:{}", guild_id, channel_id));
	for (size_t i = 0; i < shuffle_list.size(); ++i) {
		question_cache.emplace_back(question_t::fetch(from_string<uint64_t>(shuffle_list[i], std::dec), guild_id, settings));
	}
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("Build question cache end in {:.04f} secs: G:{} C:{}", dpp::utility::time_f() - start, guild_id, channel_id));
}

/* State machine event for question time up */
void state_t::do_time_up(const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_time_up: G:{} C:{}", guild_id, channel_id));

	std::string content;
	std::string title;
	std::string image;

	{
		if (is_insane_round(settings)) {
			/* Insane round */
			uint32_t found = insane_num - insane_left;
			content += fmt::format(_("INSANE_FOUND", settings), found);
			title = _("TIME_UP", settings);
			do_insane_board(settings);
			clear_insane_stats();

		} else if (question.answer != "") {
			/* Not insane round */

			content += fmt::format(_("ANS_WAS", settings), homoglyph(question.answer));
			title = _("OUT_OF_TIME", settings);
			image = question.answer_image;

		}
		/* FIX: You can only lose your streak on a non-insane round */
		if (question.answer != "" && !is_insane_round(settings) && streak > 1 && last_to_answer) {
			content += "\n\n" + fmt::format(_("STREAK_SMASHED", settings), fmt::format("<@{}>", last_to_answer), streak);
		}

		/* Clear current answer so the question becomes unanswerable */
		if (question.answer != "") {
			question.answer = "";
			/* For non-insane rounds reset the streak back to 1 */
			if (!is_insane_round(settings)) {
				last_to_answer = 0;
				streak = 1;
			}
		}
	}

	if (round <= numquestions - 2) {
		content += "\n\n" + fmt::format(_("COMING_UP", settings), interval == TRIV_INTERVAL ? settings.question_interval : interval);
	}

	creator->SimpleEmbed(settings, ":alarm_clock:", content, channel_id, title, image);

	gamestate = (round > numquestions ? TRIV_END : TRIV_ASK_QUESTION);
	round++;
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}
}

/* State machine event for answer correct */
void state_t::do_answer_correct(const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_answer_correct: G:{} C:{}", guild_id, channel_id));

	round++;
	question.answer = "";
	gamestate = TRIV_ASK_QUESTION;

	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, question.id)) {
		StopGame(settings);
	}
}

/* State machine event for end of game */
void state_t::do_end_game(const guild_settings_t& settings)
{
	creator->GetBot()->core->log(dpp::ll_debug, fmt::format("do_end_game: G:{} C:{}", guild_id, channel_id));

	log_game_end(guild_id, channel_id);

	creator->GetBot()->core->log(dpp::ll_info, fmt::format("End of game on guild {}, channel {} after {} seconds", guild_id, channel_id, time(NULL) - start_time));
	creator->SimpleEmbed(settings, ":stop_button:", fmt::format(_("END1", settings), numquestions - 1), channel_id, _("END_TITLE", settings));
	creator->show_stats("", 0, guild_id, channel_id);

	terminating = true;
}

/* Returns true if the current round is an insane round */
bool state_t::is_insane_round(const guild_settings_t& s)
{
	/* Insane rounds are every tenth question */
	return (s.disable_insane_rounds == false && (round % 10 == 0));
}
