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
#include "piglatin.h"

/**
 * Module class for trivia system
 */

TriviaModule::TriviaModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), terminating(false)
{
	srand(time(NULL) * time(NULL));
	ml->Attach({ I_OnMessage, I_OnPresenceUpdate, I_OnChannelDelete, I_OnGuildDelete, I_OnAllShardsReady }, this);
	notvowel = new PCRE("/[^aeiou_]/", true);
	number_tidy_dollars = new PCRE("^([\\d\\,]+)\\s+dollars$");
	number_tidy_nodollars = new PCRE("^([\\d\\,]+)\\s+(.+?)$");
	number_tidy_positive = new PCRE("^[\\d\\,]+$");
	number_tidy_negative = new PCRE("^\\-[\\d\\,]+$");
	prefix_match = new PCRE("prefix");
	set_io_context(bot->io, Bot::GetConfig("apikey"));
	presence_update = new std::thread(&TriviaModule::UpdatePresenceLine, this);
	startup = time(NULL);
	{
		std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
		api_commands = get_api_command_names();
		bot->core.log->info("Initial API command count: {}", api_commands.size());
	}
	numstrs = get_num_strs();
	bot->core.log->info("Numstrs count: {}", numstrs.size());

	std::ifstream langfile("../lang.json");
	lang = new json();
	langfile >> *lang;
	bot->core.log->info("Language strings count: {}", lang->size());
}

Bot* TriviaModule::GetBot()
{
	return bot;
}

TriviaModule::~TriviaModule()
{
	DisposeThread(presence_update);
	std::lock_guard<std::mutex> user_cache_lock(states_mutex);
	for (auto state = states.begin(); state != states.end(); ++state) {
		delete state->second;
	}
	states.clear();
	delete notvowel;
	delete number_tidy_dollars;
	delete number_tidy_nodollars;
	delete number_tidy_positive;
	delete number_tidy_negative;
	delete prefix_match;
	delete lang;
}


bool TriviaModule::OnPresenceUpdate()
{
	const aegis::shards::shard_mgr& s = bot->core.get_shard_mgr();
	const std::vector<std::unique_ptr<aegis::shards::shard>>& shards = s.get_shards();
	for (auto i = shards.begin(); i != shards.end(); ++i) {
		const aegis::shards::shard* shard = i->get();
		db::query("INSERT INTO infobot_shard_status (id, connected, online, uptime, transfer, transfer_compressed) VALUES('?','?','?','?','?','?') ON DUPLICATE KEY UPDATE connected = '?', online = '?', uptime = '?', transfer = '?', transfer_compressed = '?'",
			{
				shard->get_id(),
				shard->is_connected(),
				shard->is_online(),
				shard->uptime(),
				shard->get_transfer_u(),
				shard->get_transfer(),
				shard->is_connected(),
				shard->is_online(),
				shard->uptime(),
				shard->get_transfer_u(),
				shard->get_transfer()
			}
		);
	}
	{
		std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
		api_commands = get_api_command_names();
	}
	return true;
}

std::string TriviaModule::_(const std::string &k, const guild_settings_t& settings)
{
	auto o = lang->find(k);
	if (o != lang->end()) {
		auto v = o->find(settings.language);
		if (v != o->end()) {
			return v->get<std::string>();
		} else {
			bot->core.log->debug("Missing language '{}' in string '{}'!", settings.language, k);
		}
	} else {
		bot->core.log->debug("Missing language string '{}'", k);
	}
	return k;
}

bool TriviaModule::OnAllShardsReady()
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	json active = get_active(hostname);
	for (auto game = active.begin(); game != active.end(); ++game) {

		bool quickfire = (*game)["quickfire"].get<std::string>() == "1";

		state_t* state = new state_t(this);
		state->start_time = time(NULL);

		/* Get shuffle list from state */
		if (!(*game)["qlist"].get<std::string>().empty()) {
			json shuffle = json::parse((*game)["qlist"].get<std::string>());

			for (auto s = shuffle.begin(); s != shuffle.end(); ++s) {
				state->shuffle_list.push_back(s->get<std::string>());
			}
			bot->core.log->debug("Resume shuffle list length: {}", state->shuffle_list.size());
			state->gamestate = (trivia_state_t)from_string<uint32_t>((*game)["state"].get<std::string>(), std::dec);
		} else {
			/* No shuffle list to resume from, create a new one */
			state->shuffle_list = fetch_shuffle_list(from_string<int64_t>((*game)["guild_id"].get<std::string>(), std::dec));
			state->gamestate = TRIV_ASK_QUESTION;
		}

		state->numquestions = from_string<uint32_t>((*game)["questions"].get<std::string>(), std::dec) + 1;
		state->streak = from_string<uint32_t>((*game)["streak"].get<std::string>(), std::dec);
		state->last_to_answer = from_string<int64_t>((*game)["lastanswered"].get<std::string>(), std::dec);
		state->round = from_string<uint32_t>((*game)["question_index"].get<std::string>(), std::dec);
		state->interval = (quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL);
		state->channel_id = from_string<int64_t>((*game)["channel_id"].get<std::string>(), std::dec);
		state->guild_id = from_string<int64_t>((*game)["guild_id"].get<std::string>(), std::dec);
		state->curr_qid = 0;
		state->curr_answer = "";
		/* Force fetching of question */
		if (state->round % 10 == 0) {
			do_insane_round(state, true);
		} else {
			do_normal_round(state, true);
		}
		aegis::channel* c = bot->core.find_channel(state->channel_id);
		{
			std::lock_guard<std::mutex> user_cache_lock(states_mutex);
			states[state->channel_id] = state;
		}
		if (c) {
			bot->core.log->info("Resumed game on guild {}, channel {}, {} questions [{}]", state->guild_id, state->channel_id, state->numquestions, quickfire ? "quickfire" : "normal");
			state->timer = new std::thread(&state_t::tick, state);
		} else {
			state->terminating = true;
		}
	}
	return true;
}

bool TriviaModule::OnChannelDelete(const modevent::channel_delete &cd)
{
	return true;
}

bool TriviaModule::OnGuildDelete(const modevent::guild_delete &gd)
{
	return true;
}

int64_t TriviaModule::GetActiveGames()
{
	int64_t a = 0;
	std::lock_guard<std::mutex> user_cache_lock(states_mutex);
	for (auto state = states.begin(); state != states.end(); ++state) {
		if (state->second && state->second->gamestate != TRIV_END && !state->second->terminating) {
			++a;
		}
	}
	return a;
}

guild_settings_t TriviaModule::GetGuildSettings(int64_t guild_id)
{
	aegis::guild* guild = bot->core.find_guild(guild_id);
	if (guild == nullptr) {
		return guild_settings_t(guild_id, "!", {}, 3238819, false, false, false, 0, "", "en");
	} else {
		db::resultset r = db::query("SELECT * FROM bot_guild_settings WHERE snowflake_id = ?", {guild_id});
		if (!r.empty()) {
			std::stringstream s(r[0]["moderator_roles"]);
			int64_t role_id;
			std::vector<int64_t> role_list;
			while ((s >> role_id)) {
				role_list.push_back(role_id);
			}
			return guild_settings_t(from_string<int64_t>(r[0]["snowflake_id"], std::dec), r[0]["prefix"], role_list, from_string<uint32_t>(r[0]["embedcolour"], std::dec), (r[0]["premium"] == "1"), (r[0]["only_mods_stop"] == "1"), (r[0]["role_reward_enabled"] == "1"), from_string<int64_t>(r[0]["role_reward_id"], std::dec), r[0]["custom_url"], r[0]["language"]);
		} else {
			db::query("INSERT INTO bot_guild_settings (snowflake_id) VALUES('?')", {guild_id});
			return guild_settings_t(guild_id, "!", {}, 3238819, false, false, false, 0, "", "en");
		}
	}
}

std::string TriviaModule::GetVersion()
{
	/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
	std::string version = "$ModVer 23$";
	return "3.0." + version.substr(8,version.length() - 9);
}

std::string TriviaModule::GetDescription()
{
	return "Trivia System";
}

void TriviaModule::UpdatePresenceLine()
{
	bot->counters["activegames"] = 0;
	while (!terminating) {
		sleep(20);
		int32_t questions = get_total_questions();
		bot->counters["activegames"] = GetActiveGames();
		/* Can't translate this, it's per-shard! */
		bot->core.update_presence(fmt::format("Trivia! {} questions, {} active games on {} servers through {} shards", Comma(questions), Comma(GetActiveGames()), Comma(bot->core.get_guild_count()), Comma(bot->core.shard_max_count)), aegis::gateway::objects::activity::Game);
		CheckForQueuedStarts();
	}
}

std::string TriviaModule::letterlong(std::string text, const guild_settings_t &settings)
{
	text = ReplaceString(text, " ", "");
	if (text.length()) {
		return fmt::format(_("HINT_LETTERLONG", settings), text.length(), text[0], text[text.length() - 1]);
	} else {
		return "An empty answer";
	}
}

std::string TriviaModule::vowelcount(std::string text, const guild_settings_t &settings)
{
	text = ReplaceString(lowercase(text), " ", "");
	int v = 0;
	for (auto x = text.begin(); x != text.end(); ++x) {
		if (isVowel(*x)) {
			++v;
		}
	}
	return fmt::format(_("HINT_VOWELCOUNT", settings), text.length(), v);
}

void TriviaModule::do_insane_round(state_t* state, bool silent)
{
	bot->core.log->debug("do_insane_round: G:{} C:{}", state->guild_id, state->channel_id);

	if (state->round >= state->numquestions) {
		state->gamestate = TRIV_END;
		state->score = 0;
		return;
	}

	guild_settings_t settings = GetGuildSettings(state->guild_id);

	std::vector<std::string> answers = fetch_insane_round(state->curr_qid, state->guild_id);
	if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
		StopGame(state, settings);
		return;
	}

	state->insane = {};
	for (auto n = answers.begin(); n != answers.end(); ++n) {
		if (n == answers.begin()) {
			state->curr_question = trim(*n);
		} else {
			if (*n != "***END***") {
				state->insane[lowercase(trim(*n))] = true;
			}
		}
	}
	state->insane_left = state->insane.size();
	state->insane_num = state->insane.size();
	state->gamestate = TRIV_FIRST_HINT;

	aegis::channel* c = bot->core.find_channel(state->channel_id);
	if (c) {
		EmbedWithFields(fmt::format(_("QUESTION_COUNTER", settings), state->round, state->numquestions - 1), {{_("INSANE_ROUND", settings), fmt::format(_("INSANE_ANS_COUNT", settings), state->insane_num), false}, {_("QUESTION", settings), state->curr_question, false}}, c->get_id().get(), fmt::format("https://triviabot.co.uk/report/?c={}&g={}&insane={}", state->channel_id, state->guild_id, state->curr_qid + state->channel_id));
	} else {
		bot->core.log->warn("do_insane_round(): Channel {} was deleted", state->channel_id);
	}
}

void TriviaModule::do_normal_round(state_t* state, bool silent)
{
	bot->core.log->debug("do_normal_round: G:{} C:{}", state->guild_id, state->channel_id);

	if (state->round >= state->numquestions) {
		state->gamestate = TRIV_END;
		state->score = 0;
		return;
	}

	bool valid = false;
	int32_t tries = 0;

	guild_settings_t settings = GetGuildSettings(state->guild_id);

	do {
		state->curr_qid = 0;
		bot->core.log->debug("do_normal_round: fetch_question: '{}'", state->shuffle_list[state->round - 1]);
		std::vector<std::string> data = fetch_question(from_string<int64_t>(state->shuffle_list[state->round - 1], std::dec), state->guild_id);
		if (data.size() >= 12) {
			state->curr_qid = from_string<int64_t>(data[0], std::dec);
			state->curr_question = data[1];
			state->curr_answer = data[2];
			state->curr_customhint1 = data[3];
			state->curr_customhint2 = data[4];
			state->curr_category = data[5];
			state->curr_lastasked = from_string<time_t>(data[6],std::dec);
			state->curr_timesasked = from_string<int32_t>(data[7], std::dec);
			state->curr_lastcorrect = data[8];
			state->recordtime = from_string<time_t>(data[9],std::dec);
			state->shuffle1 = data[10];
			state->shuffle2 = data[11];
			valid = !state->curr_question.empty();
			if (!valid) {
				state->curr_qid = 0;
				bot->core.log->debug("do_normal_round: Invalid question response size {} with empty question retrieving question {}, round {} shuffle size {}", data.size(), state->shuffle_list[state->round - 1], state->round - 1, state->shuffle_list.size());
				sleep(2);
				tries++;
			}
		} else {
			bot->core.log->debug("do_normal_round: Invalid question response size {} retrieving question {}, round {} shuffle size {}", data.size(), state->shuffle_list[state->round - 1], state->round - 1, state->shuffle_list.size());
			sleep(2);
			state->curr_qid = 0;
			tries++;
			valid = false;
		}
	} while (!valid && tries <= 3);

	if (state->curr_qid == 0) {
		state->gamestate = TRIV_END;
		state->score = 0;
		state->curr_answer = "";
		bot->core.log->warn("do_normal_round(): Attempted to retrieve question id {} but got a malformed response. Round was aborted.", state->shuffle_list[state->round - 1]);
		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c && !silent) {
			EmbedWithFields(fmt::format(_("Q_FETCH_ERROR", settings)), {{_("Q_SPOOPY", settings), _("Q_CONTACT_DEVS", settings), false}, {_("ROUND_STOPPING", settings), _("ERROR_BROKE_IT", settings), false}}, c->get_id().get());
		}
		return;
	}

	if (state->curr_question != "") {
		state->asktime = time(NULL);
		guild_settings_t settings = GetGuildSettings(state->guild_id);
		state->curr_answer = trim(state->curr_answer);
		std::string t = conv_num(state->curr_answer, settings);
		if (is_number(t) && t != "0") {
			state->curr_answer = t;
		}
		state->curr_answer = tidy_num(state->curr_answer);
		/* Handle hints */
		if (state->curr_customhint1.empty()) {
			/* No custom first hint, build one */
			state->curr_customhint1 = state->curr_answer;
			if (is_number(state->curr_customhint1)) {
				state->curr_customhint1 = MakeFirstHint(state->curr_customhint1, settings);
			} else {
				int32_t r = random(1, 12);
				if (r <= 4) {
					/* Leave only capital letters */
					for (int x = 0; x < state->curr_customhint1.length(); ++x) {
						if ((state->curr_customhint1[x] >= 'a' && state->curr_customhint1[x] <= 'z') || state->curr_customhint1[x] == '1' || state->curr_customhint1[x] == '3' || state->curr_customhint1[x] == '5' || state->curr_customhint1[x]  == '7' || state->curr_customhint1[x] == '9') {
							state->curr_customhint1[x] = '#';
						}
					}
				} else if (r >= 5 && r <= 8) {
					state->curr_customhint1 = letterlong(state->curr_customhint1, settings);
				} else {
					state->curr_customhint1 = fmt::format(_("SCRAMBLED_ANSWER", settings), state->shuffle1);
				}
			}
		}
		if (state->curr_customhint2.empty()) {
			/* No custom second hint, build one */
			state->curr_customhint2 = state->curr_answer;
			if (is_number(state->curr_customhint2) || PCRE("^\\$(\\d+)$").Match(state->curr_customhint2)) {
				std::string currency;
				std::vector<std::string> matches;
				if (PCRE("^\\$(\\d+)$").Match(state->curr_customhint2, matches)) {
					state->curr_customhint2 = matches[1];
					currency = "$";
				}
				state->curr_customhint2 = currency + state->curr_customhint2;
				int32_t r = random(1, 13);
				if ((r < 3 && from_string<int32_t>(state->curr_customhint2, std::dec) <= 10000)) {
					state->curr_customhint2 = dec_to_roman(from_string<unsigned int>(state->curr_customhint2, std::dec), settings);
				} else if ((r >= 3 && r < 6) || from_string<int32_t>(state->curr_customhint2, std::dec) > 10000) {
					state->curr_customhint2 = fmt::format(_("HEX", settings), from_string<int32_t>(state->curr_customhint2, std::dec));
				} else if (r >= 6 && r <= 10) {
					state->curr_customhint2 = fmt::format(_("OCT", settings), from_string<int32_t>(state->curr_customhint2, std::dec));
				} else {
					state->curr_customhint2 = fmt::format(_("BIN", settings), from_string<int32_t>(state->curr_customhint2, std::dec));
				}
			} else {
				int32_t r = random(1, 12);
				if (r <= 4) {
					/* Transpose only the vowels */
					for (int x = 0; x < state->curr_customhint2.length(); ++x) {
						if (toupper(state->curr_customhint2[x]) == 'A' || toupper(state->curr_customhint2[x]) == 'E' || toupper(state->curr_customhint2[x]) == 'I' || toupper(state->curr_customhint2[x]) == 'O' || toupper(state->curr_customhint2[x]) == 'U' || toupper(state->curr_customhint2[x]) == '2' || toupper(state->curr_customhint2[x]) == '4' || toupper(state->curr_customhint2[x]) == '6' || toupper(state->curr_customhint2[x]) == '8' || toupper(state->curr_customhint2[x]) == '0') {
							state->curr_customhint2[x] = '#';
						}
					}
				} else if ((r >= 5 && r <= 6) || settings.language != "en") {
					state->curr_customhint2 = vowelcount(state->curr_customhint2, settings);
				} else {
					/* settings.language check for en above, because piglatin only makes sense in english */
					state->curr_customhint2 = piglatin(state->curr_customhint2);
				}

			}
		}

		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			if (!silent) {
				EmbedWithFields(fmt::format(_("QUESTION_COUNTER", settings), state->round, state->numquestions - 1), {{_("CATEGORY", settings), state->curr_category, false}, {_("QUESTION", settings), state->curr_question, false}}, c->get_id().get(), fmt::format("https://triviabot.co.uk/report/?c={}&g={}&normal={}", state->channel_id, state->guild_id, state->curr_qid + state->channel_id));
			}
		} else {
			bot->core.log->warn("do_normal_round(): Channel {} was deleted", state->channel_id);
		}

	} else {
		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			if (!silent) {
				SimpleEmbed(":ghost:", _("BRAIN_BROKE_IT", settings), c->get_id().get(), _("FETCH_Q", settings));
			}
		} else {
			bot->core.log->debug("do_normal_round: G:{} C:{} channel vanished! -- question with no text!", state->guild_id, state->channel_id);
		}
	}

	state->score = (state->interval == TRIV_INTERVAL ? 4 : 8);
	/* Advance state to first hint */
	state->gamestate = TRIV_FIRST_HINT;
	if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
		StopGame(state, settings);
	}

}

void TriviaModule::do_first_hint(state_t* state)
{
	bot->core.log->debug("do_first_hint: G:{} C:{}", state->guild_id, state->channel_id);
	aegis::channel* c = bot->core.find_channel(state->channel_id);
	guild_settings_t settings = GetGuildSettings(state->guild_id);
	if (c) {
		if (state->round % 10 == 0) {
			/* Insane round countdown */
			SimpleEmbed(":clock10:", fmt::format(_("SECS_LEFT", settings), state->interval * 2), c->get_id().get());
		} else {
			/* First hint, not insane round */
			SimpleEmbed(":clock10:", state->curr_customhint1, c->get_id().get(), _("FIRST_HINT", settings));
		}
	} else {
		 bot->core.log->warn("do_first_hint(): Channel {} was deleted", state->channel_id);
	}
	state->gamestate = TRIV_SECOND_HINT;
	state->score = (state->interval == TRIV_INTERVAL ? 2 : 4);
	if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
		StopGame(state, settings);
	}
}

void TriviaModule::do_second_hint(state_t* state)
{
	bot->core.log->debug("do_second_hint: G:{} C:{}", state->guild_id, state->channel_id);
	aegis::channel* c = bot->core.find_channel(state->channel_id);
	guild_settings_t settings = GetGuildSettings(state->guild_id);
	if (c) {
		if (state->round % 10 == 0) {
			/* Insane round countdown */
			SimpleEmbed(":clock1030:", fmt::format(_("SECS_LEFT", settings), state->interval), c->get_id().get());
		} else {
			/* Second hint, not insane round */
			SimpleEmbed(":clock1030:", state->curr_customhint2, c->get_id().get(), _("SECOND_HINT", settings));
		}
	} else {
		 bot->core.log->warn("do_second_hint: Channel {} was deleted", state->channel_id);
	}
	state->gamestate = TRIV_TIME_UP;
	state->score = (state->interval == TRIV_INTERVAL ? 1 : 2);
	if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
		StopGame(state, settings);
	}
}

void TriviaModule::do_time_up(state_t* state)
{
	bot->core.log->debug("do_time_up: G:{} C:{}", state->guild_id, state->channel_id);
	aegis::channel* c = bot->core.find_channel(state->channel_id);
	guild_settings_t settings = GetGuildSettings(state->guild_id);

	if (c) {
		if (state->round % 10 == 0) {
			int32_t found = state->insane_num - state->insane_left;
			SimpleEmbed(":alarm_clock:", fmt::format(_("INSANE_FOUND", settings), found), c->get_id().get(), _("TIME_UP", settings));
		} else if (state->curr_answer != "") {
			SimpleEmbed(":alarm_clock:", fmt::format(_("ANS_WAS", settings), state->curr_answer), c->get_id().get(), _("OUT_OF_TIME", settings));
		}
	}
	/* FIX: You can only lose your streak on a non-insane round */
	if (state->curr_answer != "" && state->round % 10 != 0 && state->streak > 1 && state->last_to_answer) {
		aegis::user* u = bot->core.find_user(state->last_to_answer);
		if (u) {
			SimpleEmbed(":octagonal_sign:", fmt::format(_("STREAK_SMASHED", settings), u->get_username(), state->streak), c->get_id().get());
		}
	}

	if (state->curr_answer != "") {
		state->curr_answer = "";
		if (state->round % 10 != 0) {
			state->last_to_answer = 0;
			state->streak = 1;
		}
	}

	if (c && state->round <= state->numquestions - 2) {
		SimpleEmbed("<a:loading:658667224067735562>", fmt::format(_("COMING_UP", settings), state->interval), c->get_id().get(), _("REST", settings));
	}

	state->gamestate = (state->round > state->numquestions ? TRIV_END : TRIV_ASK_QUESTION);
	state->round++;
	state->score = 0;
	if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
		StopGame(state, settings);
	}
}

void TriviaModule::do_answer_correct(state_t* state)
{
	bot->core.log->debug("do_answer_correct: G:{} C:{}", state->guild_id, state->channel_id);

	aegis::channel* c = bot->core.find_channel(state->channel_id);
	guild_settings_t settings = GetGuildSettings(state->guild_id);

	state->round++;
	state->score = 0;

	if (state->round <= state->numquestions - 2) {
		if (c) {
			SimpleEmbed("<a:loading:658667224067735562>", fmt::format(_("COMING_UP", settings), state->interval), c->get_id().get(), _("REST", settings));
		} else {
			bot->core.log->warn("do_answer_correct(): Channel {} was deleted", state->channel_id);
		}
	}
	state->gamestate = TRIV_ASK_QUESTION;
	if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
		StopGame(state, settings);
	}
}

void TriviaModule::do_end_game(state_t* state)
{
	bot->core.log->debug("do_end_game: G:{} C:{}", state->guild_id, state->channel_id);

	log_game_end(state->guild_id, state->channel_id);

	aegis::channel* c = bot->core.find_channel(state->channel_id);
	if (c) {
		guild_settings_t settings = GetGuildSettings(state->guild_id);
		bot->core.log->info("End of game on guild {}, channel {} after {} seconds", state->guild_id, state->channel_id, time(NULL) - state->start_time);
		SimpleEmbed(":stop_button:", fmt::format(_("END1", settings), state->numquestions - 1), c->get_id().get(), _("END_TITLE", settings));
		show_stats(c->get_guild().get_id(), state->channel_id);
	} else {
		bot->core.log->warn("do_end_game(): Channel {} was deleted", state->channel_id);
	}
	state->terminating = true;
}

void TriviaModule::show_stats(int64_t guild_id, int64_t channel_id)
{
	std::vector<std::string> topten = get_top_ten(guild_id);
	size_t count = 1;
	std::string msg;
	std::vector<field_t> fields;
	for(auto r = topten.begin(); r != topten.end(); ++r) {
		std::stringstream score(*r);
		std::string points;
		int64_t snowflake_id;
		score >> points;
		score >> snowflake_id;
		aegis::user* u = bot->core.find_user(snowflake_id);
		if (u) {
			msg.append(fmt::format("{}. **{}** ({})\n", count++, u->get_full_name(), points));
		} else {
			msg.append(fmt::format("{}. **Deleted User#0000** ({})\n", count++, points));
		}
	}
	if (msg.empty()) {
		msg = "Nobody has played here today! :cry:";
	}
	aegis::channel* c = bot->core.find_channel(channel_id);
	if (c) {
		guild_settings_t settings = GetGuildSettings(guild_id);
		if (settings.premium && !settings.custom_url.empty()) {
			EmbedWithFields(_("LEADERBOARD", settings), {{_("TOP_TEN", settings), msg, false}, {_("MORE_INFO", settings), fmt::format(_("LEADER_LINK", settings), settings.custom_url), false}}, c->get_id().get());
		} else {
			EmbedWithFields(_("LEADERBOARD", settings), {{_("TOP_TEN", settings), msg, false}, {_("MORE_INFO", settings), fmt::format(_("LEADER_LINK", settings), guild_id), false}}, c->get_id().get());
		}
	}
}

void TriviaModule::Tick(state_t* state)
{
	if (state->terminating) {
		return;
	}

	uint32_t waits = 0;
	while ((bot->core.find_guild(state->guild_id) == nullptr || bot->core.find_channel(state->channel_id) == nullptr) && !state->terminating) {
		bot->core.log->warn("Guild or channel are missing!!! Waiting 5 seconds for connection to re-establish to guild/channel: G:{} C:{}", state->guild_id, state->channel_id);
		sleep(5);
		if (waits++ > 30) {
			bot->core.log->warn("Waited too long for re-connection of G:{} C:{}, ending round.", state->guild_id, state->channel_id);
			state->gamestate = TRIV_END;
			state->terminating = true;
		}
	}
	

	if (!state->terminating) {
		switch (state->gamestate) {
			case TRIV_ASK_QUESTION:
				if (state->round % 10 == 0) {
					do_insane_round(state, false);
				} else {
					do_normal_round(state, false);
				}
			break;
			case TRIV_FIRST_HINT:
				do_first_hint(state);
			break;
			case TRIV_SECOND_HINT:
				do_second_hint(state);
			break;
			case TRIV_TIME_UP:
				do_time_up(state);
			break;
			case TRIV_ANSWER_CORRECT:
				do_answer_correct(state);
			break;
				case TRIV_END:
				do_end_game(state);
			break;
			default:
				bot->core.log->warn("Invalid state '{}', ending round.", state->gamestate);
				state->gamestate = TRIV_END;
				state->terminating = true;
			break;
		}
	}
}

void TriviaModule::DisposeThread(std::thread* t)
{
	bot->DisposeThread(t);
}

void TriviaModule::StopGame(state_t* state, const guild_settings_t &settings)
{
	if (state->gamestate != TRIV_END) {
		SimpleEmbed(":octagonal_sign:", _("DASH_STOP", settings), state->channel_id, _("STOPPING", settings));
		state->gamestate = TRIV_END;
		state->terminating = false;
	}
}

void TriviaModule::CheckForQueuedStarts()
{
	db::resultset rs = db::query("SELECT * FROM start_queue ORDER BY queuetime", {});
	for (auto r = rs.begin(); r != rs.end(); ++r) {
		int64_t guild_id = from_string<int64_t>((*r)["guild_id"], std::dec);
		int64_t channel_id = from_string<int64_t>((*r)["channel_id"], std::dec);
		int64_t user_id = from_string<int64_t>((*r)["user_id"], std::dec);
		int32_t questions = from_string<int32_t>((*r)["questions"], std::dec);
		int32_t quickfire = from_string<int32_t>((*r)["quickfire"], std::dec);
		bot->core.log->info("Remote start, guild_id={} channel_id={} user_id={} questions={} type={}", guild_id, channel_id, user_id, questions, quickfire ? "quickfire" : "normal");
		guild_settings_t settings = GetGuildSettings(guild_id);
		aegis::gateway::objects::message m(fmt::format("{}{} {}", settings.prefix, (quickfire ? "quickfire" : "start"), questions), bot->core.find_channel(channel_id), bot->core.find_guild(guild_id));

		struct modevent::message_create msg = {
			*(bot->core.get_shard_mgr().get_shards()[0]),
			*(bot->core.find_user(user_id)),
			*(bot->core.find_channel(channel_id)),
			m
		};

		RealOnMessage(msg, msg.msg.get_content(), false, {}, user_id);

		db::query("DELETE FROM start_queue WHERE channel_id = ?", {channel_id});
	}
}

void TriviaModule::CacheUser(int64_t user, int64_t channel_id)
{
	aegis::channel* c = bot->core.find_channel(channel_id);
	aegis::user* _user = bot->core.find_user(user);
	if (_user && c) {
		aegis::user::guild_info& gi = _user->get_guild_info(c->get_guild().get_id());
		cache_user(_user, &c->get_guild(), &gi);
	} else {
		bot->core.log->debug("Command with no user!");
	}
}

bool TriviaModule::OnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
{
	return RealOnMessage(message, clean_message, mentioned, stringmentions, 0);
}

bool TriviaModule::RealOnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions, int64_t _author_id)
{
	std::vector<std::string> param;
	std::string botusername = bot->user.username;
	std::string username;
	aegis::gateway::objects::message msg = message.msg;

	// Allow overriding of author id from remote start code
	int64_t author_id = _author_id ? _author_id : msg.get_author_id().get();

	if (!message.has_user()) {
		return true;
	}

	aegis::user* user = bot->core.find_user(author_id);
	if (user) {
		username = user->get_username();
	}
	
	bool game_in_progress = false;

	/* Retrieve current state for channel, if there is no state object, no game is in progress */
	state_t* state = nullptr;
	int64_t guild_id = 0;
	aegis::channel* c = nullptr;

	if (msg.has_channel()) {
		if (msg.get_channel_id().get() == 0) {
			c = bot->core.find_channel(msg.get_channel().get_id().get());
		} else {
			c = bot->core.find_channel(msg.get_channel_id().get());
		}
		if (c) {
			guild_id = c->get_guild().get_id().get();
		} else {
			bot->core.log->warn("Channel is missing!!! C:{}", msg.get_channel_id().get());
			return true;
		}
	} else {
		/* No channel! */
		bot->core.log->debug("Message without channel, {}", msg.get_id().get());
		return true;
	}

	int64_t channel_id = c->get_id().get();

	guild_settings_t settings = GetGuildSettings(guild_id);

	std::string trivia_message = clean_message;
	int x = from_string<int>(conv_num(clean_message, settings), std::dec);
	if (x > 0) {
		trivia_message = conv_num(clean_message, settings);
	}
	trivia_message = tidy_num(trivia_message);
	{
		std::lock_guard<std::mutex> user_cache_lock(states_mutex);

		std::vector<int64_t> removals;
		for (auto s : states) {
			if (s.second && s.second->terminating) {
				delete s.second;
				removals.push_back(s.first);
			} else if (!s.second) {
				removals.push_back(s.first);
			}
		}
		for (auto r : removals) {
			states.erase(r);
		}

		auto state_iter = states.find(channel_id);
		if (state_iter != states.end()) {
			state = state_iter->second;
			game_in_progress = true;
		}
	}

	if (mentioned && prefix_match->Match(clean_message)) {
		c->create_message(fmt::format(_("PREFIX", settings), settings.prefix, settings.prefix));
		return false;
	}

	if (game_in_progress) {
		if (state->gamestate == TRIV_ASK_QUESTION || state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) {

			aegis::channel* c = bot->core.find_channel(channel_id);
			
			if (state->round % 10 == 0) {
				/* Insane round */
				auto i = state->insane.find(lowercase(trivia_message));
				if (i != state->insane.end()) {
					state->insane.erase(i);

					if (--state->insane_left < 1) {
						if (c) {
							SimpleEmbed(":thumbsup:", fmt::format(_("LAST_ONE", settings), username), c->get_id().get());
						}
						if (state->round <= state->numquestions - 1) {
							state->round++;
							state->gamestate = TRIV_ANSWER_CORRECT;
						} else {
							state->gamestate = TRIV_END;
						}
					} else {
						if (c) {
							SimpleEmbed(":thumbsup:", fmt::format(_("INSANE_CORRECT", settings), username, trivia_message, state->insane_left, state->insane_num), c->get_id().get());
						}
					}
					update_score_only(author_id, state->guild_id, 1);
					if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
						StopGame(state, settings);
						return false;
					}
				}
			} else {
				/* Normal round */

				/* Answer on channel is an exact match for the current answer and/or it is numeric, OR, it's non-numeric and has a levenstein distance near enough to the current answer (account for misspellings) */
				if (!state->curr_answer.empty() && ((trivia_message.length() >= state->curr_answer.length() && lowercase(state->curr_answer) == lowercase(trivia_message)) || (!PCRE("^\\$(\\d+)$").Match(state->curr_answer) && !PCRE("^(\\d+)$").Match(state->curr_answer) && (state->curr_answer.length() > 5 && levenstein(trivia_message, state->curr_answer) < 2)))) {
					/* Correct answer */
					state->gamestate = TRIV_ANSWER_CORRECT;
					time_t time_to_answer = time(NULL) - state->asktime;
					std::string pts = (state->score > 1 ? _("POINTS", settings) : _("POINT", settings));
					time_t submit_time = state->recordtime;
					int32_t score = state->score;

					/* Clear the answer here or there is a race condition where two may answer at the same time during the web requests below */
					std::string saved_answer = state->curr_answer;
					state->curr_answer = "";

					std::string ans_message;
					ans_message.append(fmt::format(_("NORM_CORRECT", settings), saved_answer, score, pts, time_to_answer));
					if (time_to_answer < state->recordtime) {
						ans_message.append(fmt::format(_("RECORD_TIME", settings), username));
						submit_time = time_to_answer;
					}
					int32_t newscore = update_score(author_id, state->guild_id, submit_time, state->curr_qid, score);
					ans_message.append(fmt::format(_("SCORE_UPDATE", settings), username, newscore ? newscore : score));

					std::string teamname = get_current_team(author_id);
					if (!empty(teamname) && teamname != "!NOTEAM") {
						add_team_points(teamname, score, author_id);
						int32_t newteamscore = get_team_points(teamname);
						ans_message.append(fmt::format(_("TEAM_SCORE", settings), teamname, score, pts, newteamscore));
					}

					if (state->last_to_answer == author_id) {
						/* Amend current streak */
						state->streak++;
						ans_message.append(fmt::format(_("ON_A_STREAK", settings), username, state->streak));
						streak_t s = get_streak(author_id, state->guild_id);
						if (state->streak > s.personalbest) {
							ans_message.append(_("BEATEN_BEST", settings));
							change_streak(author_id, state->guild_id, state->streak);
						} else {
							ans_message.append(fmt::format(_("NOT_THERE_YET", settings), s.personalbest));
						}
						if (state->streak > s.bigstreak && s.topstreaker != author_id) {
							ans_message.append(fmt::format(_("STREAK_BEATDOWN", settings), username, s.topstreaker, state->streak));
						}
					} else if (state->streak > 1 && state->last_to_answer && state->last_to_answer != author_id) {
						ans_message.append(fmt::format(_("STREAK_ENDER", settings), username, state->last_to_answer, state->streak));
						state->streak = 1;
					} else {
						state->streak = 1;
					}

					/* Update last person to answer */
					state->last_to_answer = author_id;

					aegis::channel* c = bot->core.find_channel(channel_id);
					if (c) {
						SimpleEmbed(":thumbsup:", ans_message, c->get_id().get(), fmt::format(_("CORRECT", settings), username));
					}

					if (log_question_index(state->guild_id, state->channel_id, state->round, state->streak, state->last_to_answer, state->gamestate)) {
						StopGame(state, settings);
						return false;
					}
				}
			}

		}
	}

	if (lowercase(clean_message.substr(0, settings.prefix.length())) == lowercase(settings.prefix)) {
		/* Command */

		std::string command = clean_message.substr(settings.prefix.length(), clean_message.length() - settings.prefix.length());
		aegis::channel* c = bot->core.find_channel(channel_id);
		if (c && user != nullptr) {

			bot->core.log->info("CMD (USER={}, GUILD={}): <{}> {}", author_id, c->get_guild().get_id().get(), username, clean_message);

			if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {

				std::stringstream tokens(command);

				std::string base_command;
			
				tokens >> base_command;

				/* Check for moderator status - first check if owner */
				aegis::guild* g = bot->core.find_guild(guild_id);
				bool moderator = (g && g->get_owner() == author_id);
				/* Now iterate the list of moderator roles from settings */
				if (!moderator) {
					for (auto x = settings.moderator_roles.begin(); x != settings.moderator_roles.end(); ++x) {
						if (c->get_guild().member_has_role(author_id, *x)) {
							moderator = true;
							break;
						}
					}
				}

				base_command = lowercase(base_command);

				/* Support for old-style commands e.g. !trivia start instead of !start */
				if (base_command == "trivia") {
					tokens >> base_command;
					base_command = lowercase(base_command);
				}

				if (base_command == "qf") {
					base_command = "quickfire";
				}

				if (base_command == "start" || base_command == "quickfire" || base_command == "trivia" || base_command == "fstart") {

					int32_t questions;
					std::string str_q;
					tokens >> str_q;
					if (str_q.empty()) {
						questions = 10;
					} else {
						questions = from_string<int32_t>(str_q, std::dec);
					}

					bool quickfire = (base_command == "quickfire");
					bool resumed = false;

					if (base_command == "fstart") {
						db::resultset rs = db::query("SELECT * FROM trivia_access WHERE user_id = ? AND enabled = 1", {author_id});
						if (rs.size() > 0) {
							int64_t cid;
							tokens >> cid;
							auto newc = bot->core.find_channel(cid);
							if (!newc) {
								SimpleEmbed(":warning:", fmt::format(_("CHANNEL_WUT", settings), username), c->get_id().get());
								return false;
							} else {
								SimpleEmbed(":white_check_mark:", fmt::format(_("ROUND_START_ID", settings), questions, cid), c->get_id().get());
								c = newc;
								channel_id = cid;
								resumed = true;
								guild_id = c->get_guild().get_id().get();
								settings = GetGuildSettings(guild_id);
								SimpleEmbed(":white_check_mark:", _("RESTART_RESUME", settings), c->get_id().get());
							}
						} else {
							SimpleEmbed(":warning:", _("STAFF_ONLY", settings), c->get_id().get());
						}
					}

					json document;
					std::ifstream configfile("../config.json");
					configfile >> document;
					json shitlist = document["shitlist"];
					aegis::channel* c = bot->core.find_channel(channel_id);
					for (auto entry = shitlist.begin(); entry != shitlist.end(); ++entry) {
						int64_t sl_guild_id = from_string<int64_t>(entry->get<std::string>(), std::dec);
								if (c->get_guild().get_id().get() == sl_guild_id) {
							SimpleEmbed(":warning:", fmt::format(_("SHITLISTED", settings), username, bot->user.id.get()), c->get_id().get());
							return false;
						}
						}

					if (!settings.premium) {
						std::lock_guard<std::mutex> user_cache_lock(states_mutex);
						for (auto j = states.begin(); j != states.end(); ++j) {
							if (j->second->guild_id == c->get_guild().get_id() && j->second->gamestate != TRIV_END) {
								aegis::channel* active_channel = bot->core.find_channel(j->second->channel_id);
								if (active_channel) {
									EmbedWithFields(_("NOWAY", settings), {{_("ALREADYACTIVE", settings), fmt::format(_("CHANNELREF", settings), active_channel->get_id().get()), false},
											{_("GETPREMIUM", settings), _("PREMDETAIL1", settings), false}}, c->get_id().get());
									return false;
								}
							}
						}
					}

					if (!game_in_progress) {
						int32_t max_quickfire = (settings.premium ? 200 : 15);
						if ((!quickfire && (questions < 5 || questions > 200)) || (quickfire && (questions < 5 || questions > max_quickfire))) {
							if (quickfire) {
								if (questions > max_quickfire && !settings.premium) {
									EmbedWithFields(_("MAX15", settings), {{_("GETPREMIUM", settings), _("PREMDETAIL2", settings), false}}, c->get_id().get());
								} else {
									SimpleEmbed(":warning:", fmt::format(_("MAX15DETAIL", settings), username, max_quickfire), c->get_id().get());
								}
							} else {
								SimpleEmbed(":warning:", fmt::format(_("MAX200", settings), username), c->get_id().get());
							}
							return false;
						}

						std::vector<std::string> sl = fetch_shuffle_list(c->get_guild().get_id());
						if (sl.size() < 50) {
							SimpleEmbed(":warning:", fmt::format(_("SPOOPY2", settings), username), c->get_id().get(), _("BROKED", settings));
							return false;
						} else  {
							state = new state_t(this);
							state->start_time = time(NULL);
							state->shuffle_list = sl;
							state->gamestate = TRIV_ASK_QUESTION;
							state->numquestions = questions + 1;
							state->streak = 1;
							state->last_to_answer = 0;
							state->round = 1;
							state->interval = (quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL);
							state->channel_id = channel_id;
							state->curr_qid = 0;
							state->curr_answer = "";
							aegis::channel* c = bot->core.find_channel(channel_id);
							{
								std::lock_guard<std::mutex> user_cache_lock(states_mutex);
								states[channel_id] = state;
							}
							if (c) {
								state->guild_id = c->get_guild().get_id();
								bot->core.log->info("Started game on guild {}, channel {}, {} questions [{}]", state->guild_id, channel_id, questions, quickfire ? "quickfire" : "normal");
								EmbedWithFields(fmt::format(_("NEWROUND", settings), (quickfire ? "**QUICKFIRE** " : ""), (resumed ? _("RESUMED", settings) : _("STARTED", settings)), (resumed ? _("ABOTADMIN", settings) : username)), {{_("QUESTION", settings), fmt::format("{}", questions), false}, {_("GETREADY", settings), _("FIRSTCOMING", settings), false}, {_("HOWPLAY", settings), _("INSTRUCTIONS", settings)}}, c->get_id().get());
								state->timer = new std::thread(&state_t::tick, state);

								CacheUser(author_id, channel_id);
								log_game_start(state->guild_id, state->channel_id, questions, quickfire, c->get_name(), author_id, state->shuffle_list);
							} else {
								state->terminating = true;
							}

							return false;
						}
					} else {
						SimpleEmbed(":warning:", fmt::format(_("ALREADYRUN", settings), username), c->get_id().get());
						return false;
					}
				} else if (base_command == "stop") {
					if (game_in_progress) {
						if (settings.only_mods_stop) {
							if (!moderator) {
								SimpleEmbed(":warning:", fmt::format(_("CANTSTOPMEIMTHEGINGERBREADMAN", settings), username), c->get_id().get());
								return false;
							}
						}
						SimpleEmbed(":octagonal_sign:", fmt::format(_("STOPOK", settings), username), c->get_id().get());
						{
							std::lock_guard<std::mutex> user_cache_lock(states_mutex);
							auto i = states.find(channel_id);
							if (i != states.end()) {
								i->second->terminating = true;
							} else {
								bot->core.log->error("Channel deleted while game stopping on this channel, cid={}", channel_id);
							}
							state = nullptr;
						}
						CacheUser(author_id, channel_id);
						log_game_end(c->get_guild().get_id().get(), c->get_id().get());
					} else {
						SimpleEmbed(":warning:", fmt::format(_("NOTRIVIA", settings), username), c->get_id().get());
					}
					return false;
				} else if (base_command == "vote") {
					SimpleEmbed(":white_check_mark:", fmt::format(fmt::format("{}\n{}", _("PRIVHINT", settings), _("VOTEAD", settings)), bot->user.id.get(), settings.prefix), c->get_id().get());
					CacheUser(author_id, channel_id);
				} else if (base_command == "votehint" || base_command == "vh") {
					if (game_in_progress) {
						if ((state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) && (state->round % 10) != 0 && state->curr_answer != "") {
							db::resultset rs = db::query("SELECT *,(unix_timestamp(vote_time) + 43200 - unix_timestamp()) as remaining FROM infobot_votes WHERE snowflake_id = ? AND now() < vote_time + interval 12 hour", {author_id});
							if (rs.size() == 0) {
								SimpleEmbed("<:wc_rs:667695516737470494>", fmt::format(fmt::format("{}\n{}", _("NOTVOTED", settings), _("VOTEAD", settings)), bot->user.id.get(), settings.prefix), c->get_id().get());
								return false;
							} else {
								int64_t remaining_hints = from_string<int64_t>(rs[0]["dm_hints"], std::dec);
								int32_t secs = from_string<int32_t>(rs[0]["remaining"], std::dec);
								int32_t mins = secs / 60 % 60;
								float hours = floor(secs / 60 / 60);
								if (remaining_hints < 1) {
									SimpleEmbed(":warning:", fmt::format(fmt::format("{}\n{}", fmt::format(_("NOMOREHINTS", settings), username), _("VOTEAD", settings)), bot->user.id.get(), hours, mins), c->get_id().get());
								} else {
									remaining_hints--;
									if (remaining_hints > 0) {
										SimpleEmbed(":white_check_mark:", fmt::format(_("VH1", settings), username, remaining_hints, hours, mins), c->get_id().get());
									} else {
										SimpleEmbed(":white_check_mark:", fmt::format(_("VH2", settings), username, hours, mins), c->get_id().get());
									}
									std::string personal_hint = state->curr_answer;
									personal_hint = lowercase(personal_hint);
									personal_hint[0] = '#';
									personal_hint[personal_hint.length() - 1] = '#';
									personal_hint = ReplaceString(personal_hint, " ", "#");
									// Get the API to do this, because DMs in aegis are unreliable right now.
									send_hint(author_id, personal_hint, remaining_hints);
									db::query("UPDATE infobot_votes SET dm_hints = ? WHERE snowflake_id = ?", {remaining_hints, author_id});
									CacheUser(author_id, channel_id);

									return false;
								}
							}
						} else {
							SimpleEmbed(":warning:", fmt::format(_("WAITABIT", settings), username), c->get_id().get());
							return false;
						}
					} else {
						SimpleEmbed(":warning:", fmt::format(fmt::format("{}\n{}", fmt::format(_("NOROUND", settings), username), _("VOTEAD", settings)), bot->user.id.get()), c->get_id().get());
						return false;
					}
				} else if (base_command == "stats") {
					show_stats(c->get_guild().get_id(), channel_id);
					CacheUser(author_id, channel_id);
				} else if (base_command == "info") {
					std::stringstream s;
					time_t diff = bot->core.uptime() / 1000;
					int seconds = diff % 60;
					diff /= 60;
					int minutes = diff % 60;
					diff /= 60;
					int hours = diff % 24;
					diff /= 24;
					int days = diff;
					int64_t servers = bot->core.get_guild_count();
					int64_t users = bot->core.get_member_count();
					char uptime[32];
					snprintf(uptime, 32, "%d day%s, %02d:%02d:%02d", days, (days != 1 ? "s" : ""), hours, minutes, seconds);
					char startstr[256];
					tm _tm;
					gmtime_r(&startup, &_tm);
					strftime(startstr, 255, "%x, %I:%M%p", &_tm);

					const statusfield statusfields[] = {
						statusfield(_("ACTIVEGAMES", settings), Comma(GetActiveGames())),
						statusfield(_("TOTALSERVERS", settings), Comma(servers)),
						statusfield(_("CONNSINCE", settings), startstr),
						statusfield(_("ONLINEUSERS", settings), Comma(users)),
						statusfield(_("UPTIME", settings), std::string(uptime)),
						statusfield(_("SHARDS", settings), Comma(bot->core.shard_max_count)),
						statusfield(_("MEMBERINTENT", settings), _((bot->HasMemberIntents() ? "TICKYES" : "CROSSNO"), settings)),
						statusfield(_("TESTMODE", settings), _((bot->IsTestMode() ? "TICKYES" : "CROSSNO"), settings)),
						statusfield(_("DEVMODE", settings), _((bot->IsDevMode() ? "TICKYES" : "CROSSNO"), settings)),
						statusfield(_("MYPREFIX", settings), "``" + escape_json(settings.prefix) + "``"),
						statusfield(_("BOTVER", settings), std::string(GetVersion())),
						statusfield(_("LIBVER", settings), std::string(AEGIS_VERSION_TEXT)),
						statusfield("", "")
					};

					s << "{\"title\":\"" << bot->user.username << " " << _("INFO", settings);
					s << "\",\"color\":" << settings.embedcolour << ",\"url\":\"https:\\/\\/triviabot.co.uk\\/\\/\",";
					s << "\"footer\":{\"link\":\"https:\\/\\/triviabot.co.uk\\/\",\"text\":\"" << _("POWERED_BY", settings) << "\",\"icon_url\":\"https:\\/\\/triviabot.co.uk\\/images\\/triviabot_tl_icon.png\"},\"fields\":[";
					for (int i = 0; statusfields[i].name != ""; ++i) {
						s << "{\"name\":\"" <<  statusfields[i].name << "\",\"value\":\"" << statusfields[i].value << "\", \"inline\": true}";
						if (statusfields[i + 1].name != "") {
							s << ",";
						}
					}

					s << "],\"description\":\"" << (settings.premium ? _("YAYPREMIUM", settings) : "") << "\"}";

					json embed_json;
					try {
						embed_json = json::parse(s.str());
					}
					catch (const std::exception &e) {
						bot->core.log->error("Malformed json created when reporting info: {}", s.str());
					}
					if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {
						c->create_message_embed("", embed_json);
						bot->sent_messages++;
					}
					CacheUser(author_id, channel_id);
				} else if (base_command == "join") {
					std::string teamname;
					std::getline(tokens, teamname);
					teamname = trim(teamname);
					if (join_team(author_id, teamname, c->get_id().get())) {
						SimpleEmbed(":busts_in_silhouette:", fmt::format(_("JOINED", settings), teamname, username), c->get_id().get(), _("CALLFORBACKUP", settings));
					} else {
						SimpleEmbed(":warning:", fmt::format(_("CANTJOIN", settings), username), c->get_id().get());
					}
					CacheUser(author_id, channel_id);
				} else if (base_command == "create") {
					std::string newteamname;
					std::getline(tokens, newteamname);
					newteamname = trim(newteamname);
					std::string teamname = get_current_team(author_id);
					if (teamname.empty() || teamname == "!NOTEAM") {
						newteamname = create_new_team(newteamname);
						if (newteamname != "__NO__") {
							join_team(author_id, newteamname, c->get_id().get());
							SimpleEmbed(":busts_in_silhouette:", fmt::format(_("CREATED", settings), newteamname, username), c->get_id().get(), _("ZELDAREFERENCE", settings));
						} else {
							SimpleEmbed(":warning:", fmt::format(_("CANTCREATE", settings), username), c->get_id().get());
						}
					} else {
						SimpleEmbed(":warning:", fmt::format(_("ALREADYMEMBER", settings), username, teamname), c->get_id().get());
					}
					CacheUser(author_id, channel_id);
				} else if (base_command == "leave") {
					std::string teamname = get_current_team(author_id);
					if (teamname.empty() || teamname == "!NOTEAM") {
						SimpleEmbed(":warning:", fmt::format(_("YOULONER", settings), username, settings.prefix), c->get_id().get());
					} else {
						leave_team(author_id);
						SimpleEmbed(":busts_in_silhouette:", fmt::format(_("LEFTTEAM", settings), username, teamname), c->get_id().get(), _("COMEBACK", settings));
					}
					CacheUser(author_id, channel_id);
				} else if (base_command == "help") {
					std::string section;
					tokens >> section;
					GetHelp(section, channel_id, bot->user.username, bot->user.id.get(), msg.get_user().get_username(), msg.get_user().get_id().get(), settings);
					CacheUser(author_id, channel_id);
				} else {
					/* Custom commands handled completely by the API */
					bool command_exists = false;
					{
						std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
						command_exists = (std::find(api_commands.begin(), api_commands.end(), trim(lowercase(base_command))) != api_commands.end());
					}
					if (command_exists) {
						bool can_execute = false;
						auto check = limits.find(channel_id);
						if (check == limits.end()) {
							can_execute = true;
							limits[channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
						} else if (time(NULL) > check->second) {
							can_execute = true;
							limits[channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
						}
						if (can_execute) {
							std::string rest;
							std::getline(tokens, rest);
							rest = trim(rest);
							CacheUser(author_id, channel_id);
							std::string reply = trim(custom_command(base_command, trim(rest), author_id, channel_id, c->get_guild().get_id().get()));
							if (!reply.empty()) {
								ProcessEmbed(reply, channel_id);
							}
						} else {
							/* Display rate limit message */
							SimpleEmbed(":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), c->get_id().get(), _("WOAHTHERE", settings));
							bot->core.log->debug("Command '{}' not sent to API, rate limited", trim(lowercase(base_command)));
						}
					} else {
						bot->core.log->debug("Command '{}' not known to API", trim(lowercase(base_command)));
					}
				}
				if (base_command == "reloadlang") {
					db::resultset rs = db::query("SELECT * FROM trivia_access WHERE user_id = ? AND enabled = 1", {author_id});
					if (rs.size() > 0) {
						std::ifstream langfile("../lang.json");
						json* newlang = new json();
						json* oldlang = this->lang;

						langfile >> *newlang;

						this->lang = newlang;
						sleep(1);
						delete oldlang;

						bot->core.log->info("Language strings count: {}", lang->size());
						SimpleEmbed(":flags:", fmt::format("Reloaded lang.json, **{}** containing language strings.", lang->size()), c->get_id().get());
					} else {
						SimpleEmbed(":warning:", _("STAFF_ONLY", settings), c->get_id().get());
					}
				}
			}

		} else {
			bot->core.log->error("Invalid user record in command");
		}
	}

	return true;
}

/**
 * Emit help using a json file in the help/ directory. Missing help files emit a generic error message.
 */
void TriviaModule::GetHelp(const std::string &section, int64_t channelID, const std::string &botusername, int64_t botid, const std::string &author, int64_t authorid, const guild_settings_t &settings)
{
	json embed_json;
	char timestamp[256];
	time_t timeval = time(NULL);
	aegis::channel* channel = bot->core.find_channel(channelID);
	int32_t colour = settings.embedcolour;

	if (!channel) {
		bot->core.log->error("Can't find channel {}!", channelID);
		return;
	}

	std::ifstream t("../help/" + settings.language + "/" + (section.empty() ? "basic" : section) + ".json");
	if (!t) {
		t = std::ifstream("../help/" + settings.language + "/error.json");
	}
	std::string json((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());

	tm _tm;
	gmtime_r(&timeval, &_tm);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &_tm);

	json = ReplaceString(json, ":section:" , section);
	json = ReplaceString(json, ":user:", botusername);
	json = ReplaceString(json, ":id:", std::to_string(botid));
	json = ReplaceString(json, ":author:", author);
	json = ReplaceString(json, ":ts:", timestamp);
	json = ReplaceString(json, ":color:", std::to_string(colour));

	try {
		embed_json = json::parse(json);
	}
	catch (const std::exception &e) {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
			channel->create_message(fmt::format(_("HERPDERP", settings), authorid));
			bot->sent_messages++;
		}
		bot->core.log->error("Malformed help file {}.json!", section);
		return;
	}

	if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
		channel->create_message_embed("", embed_json);
		bot->sent_messages++;
	}
}


ENTRYPOINT(TriviaModule);

