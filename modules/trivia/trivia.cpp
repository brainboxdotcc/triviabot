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
#include "wlower.h"

TriviaModule::TriviaModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), terminating(false)
{
	/* TODO: Move to something better like mt-rand */
	srand(time(NULL) * time(NULL));

	/* Attach aegis events to module */
	ml->Attach({ I_OnMessage, I_OnPresenceUpdate, I_OnChannelDelete, I_OnGuildDelete, I_OnAllShardsReady }, this);

	/* Various regular expressions */
	notvowel = new PCRE("/[^aeiou_]/", true);
	number_tidy_dollars = new PCRE("^([\\d\\,]+)\\s+dollars$");
	number_tidy_nodollars = new PCRE("^([\\d\\,]+)\\s+(.+?)$");
	number_tidy_positive = new PCRE("^[\\d\\,]+$");
	number_tidy_negative = new PCRE("^\\-[\\d\\,]+$");
	prefix_match = new PCRE("prefix");

	startup = time(NULL);

	/* Check for and store API key */
	if (Bot::GetConfig("apikey") == "") {
		throw std::exception("TriviaBot API key missing");
		return;
	}
	set_io_context(bot->io, Bot::GetConfig("apikey"), bot, this);

	/* Create threads */
	presence_update = new std::thread(&TriviaModule::UpdatePresenceLine, this);
	command_processor = new std::thread(&TriviaModule::ProcessCommands, this);
	game_tick_thread = new std::thread(&TriviaModule::Tick, this);

	/* Get command list from API */
	{
		std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
		api_commands = get_api_command_names();
		bot->core.log->info("Initial API command count: {}", api_commands.size());
	}

	/* Read language strings */
	std::ifstream langfile("../lang.json");
	lang = new json();
	langfile >> *lang;
	bot->core.log->info("Language strings count: {}", lang->size());

	/* Setup built in commands */
	SetupCommands();
}

void TriviaModule::queue_command(const std::string &message, int64_t author, int64_t channel, int64_t guild, bool mention, const std::string &username)
{
	std::lock_guard<std::mutex> cmd_lock(cmdmutex);
	commandqueue.push_back(in_cmd(message, author, channel, guild, mention, username));
}

void TriviaModule::ProcessCommands()
{
	while (!terminating) {
		{
			std::lock_guard<std::mutex> cmd_lock(cmdmutex);
			if (!commandqueue.empty()) {
				to_process.clear();
				for (auto m = commandqueue.begin(); m != commandqueue.end(); ++m) {
					to_process.push_back(*m);
				}
				commandqueue.clear();
			}
		}
		if (!to_process.empty()) {
			for (auto m = to_process.begin(); m != to_process.end(); ++m) {
				handle_command(*m);
			}
			to_process.clear();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

Bot* TriviaModule::GetBot()
{
	return bot;
}

TriviaModule::~TriviaModule()
{
	/* We don't just delete threads, they must go through Bot::DisposeThread which joins them first */
	DisposeThread(game_tick_thread);
	DisposeThread(presence_update);
	DisposeThread(command_processor);

	/* This explicitly calls the destructor on all states */
	std::lock_guard<std::mutex> user_cache_lock(states_mutex);
	states.clear();

	/* Delete these misc pointers, mostly regexps */
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
	/* Note: Only updates this cluster's shards! */
	const aegis::shards::shard_mgr& s = bot->core.get_shard_mgr();
	const std::vector<std::unique_ptr<aegis::shards::shard>>& shards = s.get_shards();
	for (auto i = shards.begin(); i != shards.end(); ++i) {
		const aegis::shards::shard* shard = i->get();
		db::query("INSERT INTO infobot_shard_status (id, cluster_id, connected, online, uptime, transfer, transfer_compressed) VALUES('?','?','?','?','?','?','?') ON DUPLICATE KEY UPDATE cluster_id = '?', connected = '?', online = '?', uptime = '?', transfer = '?', transfer_compressed = '?'",
			{
				shard->get_id(),
				bot->GetClusterID(),
				shard->is_connected(),
				shard->is_online(),
				shard->uptime(),
				shard->get_transfer_u(),
				shard->get_transfer(),
				bot->GetClusterID(),
				shard->is_connected(),
				shard->is_online(),
				shard->uptime(),
				shard->get_transfer_u(),
				shard->get_transfer()
			}
		);
	}
	/* Curly brace scope is for readability, this call is mutexed */
	{
		std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
		api_commands = get_api_command_names();
	}
	return true;
}

std::string TriviaModule::_(const std::string &k, const guild_settings_t& settings)
{
	/* Find language string 'k' in lang.json for the language specified in 'settings' */
	auto o = lang->find(k);
	if (o != lang->end()) {
		auto v = o->find(settings.language);
		if (v != o->end()) {
			return v->get<std::string>();
		}
	}
	return k;
}

bool TriviaModule::OnAllShardsReady()
{
	/* Called when the framework indicates all shards are connected */
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	json active = get_active(hostname, bot->GetClusterID());

	if (bot->IsTestMode()) {
		/* Don't resume games in test mode */
		bot->core.log->debug("Not resuming games in test mode");
		return true;
	} else {
		bot->core.log->debug("Resuming {} games...", active.size());
	}

	/* Iterate all active games for this cluster id */
	for (auto game = active.begin(); game != active.end(); ++game) {

		int64_t guild_id = from_string<int64_t>((*game)["guild_id"].get<std::string>(), std::dec);
		bool quickfire = (*game)["quickfire"].get<std::string>() == "1";
		int64_t channel_id = from_string<int64_t>((*game)["channel_id"].get<std::string>(), std::dec);

		bot->core.log->info("Resuming id {}", channel_id);

		/* XXX: Note: The mutex here is VITAL to thread safety of the state list! DO NOT move it! */
		{
			std::lock_guard<std::mutex> states_lock(states_mutex);

			/* Check that impatient user didn't (re)start the round while bot was synching guilds! */
			if (states.find(channel_id) == states.end()) {

				std::vector<std::string> shuffle_list;

				/* Get shuffle list from state in db */
				if (!(*game)["qlist"].get<std::string>().empty()) {
					json shuffle = json::parse((*game)["qlist"].get<std::string>());
					for (auto s = shuffle.begin(); s != shuffle.end(); ++s) {
						shuffle_list.push_back(s->get<std::string>());
					}
				} else {
					/* No shuffle list to resume from, create a new one */
					shuffle_list = fetch_shuffle_list(from_string<int64_t>((*game)["guild_id"].get<std::string>(), std::dec));
				}
				int32_t round = from_string<uint32_t>((*game)["question_index"].get<std::string>(), std::dec);

				states[channel_id] = state_t(
					this,
					from_string<uint32_t>((*game)["questions"].get<std::string>(), std::dec) + 1,
					from_string<uint32_t>((*game)["streak"].get<std::string>(), std::dec),
					from_string<int64_t>((*game)["lastanswered"].get<std::string>(), std::dec),
					round,
					(quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL),
					channel_id,
					((*game)["hintless"].get<std::string>()) == "1",
					shuffle_list,
					(trivia_state_t)from_string<uint32_t>((*game)["state"].get<std::string>(), std::dec),
					guild_id
				);

				/* Force fetching of question */
				if (round % 10 == 0) {
					states[channel_id].do_insane_round(true);
				} else {
					states[channel_id].do_normal_round(true);
				}

				bot->core.log->info("Resumed game on guild {}, channel {}, {} questions [{}]", guild_id, channel_id, states[channel_id].numquestions, quickfire ? "quickfire" : "normal");
			}
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

int64_t TriviaModule::GetActiveLocalGames()
{
	/* Counts local games running on this cluster only */
	int64_t a = 0;
	std::lock_guard<std::mutex> user_cache_lock(states_mutex);
	for (auto state = states.begin(); state != states.end(); ++state) {
		if (state->second.gamestate != TRIV_END && !state->second.terminating) {
			++a;
		}
	}
	return a;
}

int64_t TriviaModule::GetActiveGames()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(games) AS games FROM infobot_discord_counts WHERE dev = ?", {bot->IsDevMode() ? 1 : 0});
	if (rs.size()) {
		return from_string<int64_t>(rs[0]["games"], std::dec);
	} else {
		return 0;
	}
}

int64_t TriviaModule::GetGuildTotal()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(server_count) AS server_count FROM infobot_discord_counts WHERE dev = ?", {bot->IsDevMode() ? 1 : 0});
	if (rs.size()) {
		return from_string<int64_t>(rs[0]["server_count"], std::dec);
	} else {
		return 0;
	}
}

int64_t TriviaModule::GetMemberTotal()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(user_count) AS user_count FROM infobot_discord_counts WHERE dev = ?", {bot->IsDevMode() ? 1 : 0});
	if (rs.size()) {
		return from_string<int64_t>(rs[0]["user_count"], std::dec);
	} else {
		return 0;
	}
}

int64_t TriviaModule::GetChannelTotal()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(channel_count) AS channel_count FROM infobot_discord_counts WHERE dev = ?", {bot->IsDevMode() ? 1 : 0});
	if (rs.size()) {
		return from_string<int64_t>(rs[0]["channel_count"], std::dec);
	} else {
		return 0;
	}
}



guild_settings_t TriviaModule::GetGuildSettings(int64_t guild_id)
{
	db::resultset r = db::query("SELECT * FROM bot_guild_settings WHERE snowflake_id = ?", {guild_id});
	if (!r.empty()) {
		std::stringstream s(r[0]["moderator_roles"]);
		int64_t role_id;
		std::vector<int64_t> role_list;
		while ((s >> role_id)) {
			role_list.push_back(role_id);
		}
		return guild_settings_t(from_string<int64_t>(r[0]["snowflake_id"], std::dec), r[0]["prefix"], role_list, from_string<uint32_t>(r[0]["embedcolour"], std::dec), (r[0]["premium"] == "1"), (r[0]["only_mods_stop"] == "1"), (r[0]["only_mods_start"] == "1"), (r[0]["role_reward_enabled"] == "1"), from_string<int64_t>(r[0]["role_reward_id"], std::dec), r[0]["custom_url"], r[0]["language"], from_string<uint32_t>(r[0]["question_interval"], std::dec));
	} else {
		db::query("INSERT INTO bot_guild_settings (snowflake_id) VALUES('?')", {guild_id});
		return guild_settings_t(guild_id, "!", {}, 3238819, false, false, false, false, 0, "", "en", 20);
	}
}

std::string TriviaModule::GetVersion()
{
	/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
	std::string version = "$ModVer 68$";
	return "3.0." + version.substr(8,version.length() - 9);
}

std::string TriviaModule::GetDescription()
{
	return "Trivia System";
}

void TriviaModule::UpdatePresenceLine()
{
	uint32_t ticks = 0;
	int32_t questions = get_total_questions();
	while (!terminating) {
		sleep(20);
		try {
			ticks++;
			if (ticks > 100) {
				questions = get_total_questions();
				ticks = 0;
			}
			bot->counters["activegames"] = GetActiveLocalGames();
			std::string presence = fmt::format("Trivia! {} questions, {} active games on {} servers through {} shards, cluster {}", Comma(questions), Comma(GetActiveGames()), Comma(this->GetGuildTotal()), Comma(bot->core.shard_max_count), bot->GetClusterID());
			bot->core.log->debug("PRESENCE: {}", presence);
			/* Can't translate this, it's per-shard! */
			bot->core.update_presence(presence, aegis::gateway::objects::activity::Game);
	
			if (!bot->IsTestMode()) {
				/* Don't handle shard reconnects or queued starts in test mode */
				CheckForQueuedStarts();
				CheckReconnects();
			}
		}
		catch (std::exception &e) {
			bot->core.log->error("Exception in UpdatePresenceLine: {}", e.what());
		}
	}
	bot->core.log->debug("Presence thread exited.");
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
	std::pair<int, int> counts = countvowel(text);
	return fmt::format(_("HINT_VOWELCOUNT", settings), counts.second, counts.first);
}

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
		answers = fetch_insane_round(curr_qid, guild_id, settings);
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
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, curr_qid) || tries >= 5) {
		StopGame(settings);
		return;
	}

	insane = {};
	for (auto n = answers.begin(); n != answers.end(); ++n) {
		if (n == answers.begin()) {
			curr_question = trim(*n);
		} else {
			if (*n != "***END***") {
				insane[utf8lower(trim(*n), settings.language == "es")] = true;
			}
		}
	}
	insane_left = insane.size();
	insane_num = insane.size();
	gamestate = TRIV_FIRST_HINT;


	creator->EmbedWithFields(settings, fmt::format(_("QUESTION_COUNTER", settings), round, numquestions - 1), {{_("INSANE_ROUND", settings), fmt::format(_("INSANE_ANS_COUNT", settings), insane_num), false}, {_("QUESTION", settings), curr_question, false}}, channel_id, fmt::format("https://triviabot.co.uk/report/?c={}&g={}&insane={}", channel_id, guild_id, curr_qid + channel_id));
}

void state_t::do_normal_round(bool silent)
{
	creator->GetBot()->core.log->debug("do_normal_round: G:{} C:{}", guild_id, channel_id);

	if (round >= numquestions) {
		gamestate = TRIV_END;
		score = 0;
		return;
	}

	bool valid = false;
	int32_t tries = 0;

	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	do {
		curr_qid = 0;
		creator->GetBot()->core.log->debug("do_normal_round: fetch_question: '{}'", shuffle_list[round - 1]);
		std::vector<std::string> data = fetch_question(from_string<int64_t>(shuffle_list[round - 1], std::dec), guild_id, settings);
		if (data.size() >= 14) {
			curr_qid = from_string<int64_t>(data[0], std::dec);
			curr_question = data[1];
			curr_answer = data[2];
			curr_customhint1 = data[3];
			curr_customhint2 = data[4];
			curr_category = data[5];
			curr_lastasked = from_string<time_t>(data[6],std::dec);
			curr_timesasked = from_string<int32_t>(data[7], std::dec);
			curr_lastcorrect = data[8];
			recordtime = from_string<time_t>(data[9],std::dec);
			shuffle1 = data[10];
			shuffle2 = data[11];
			question_image = data[12];
			answer_image = data[13];
			valid = !curr_question.empty();
			if (!valid) {
				curr_qid = 0;
				creator->GetBot()->core.log->debug("do_normal_round: Invalid question response size {} with empty question retrieving question {}, round {} shuffle size {}", data.size(), shuffle_list[round - 1], round - 1, shuffle_list.size());
				sleep(2);
				tries++;
			}
		} else {
			creator->GetBot()->core.log->debug("do_normal_round: Invalid question response size {} retrieving question {}, round {} shuffle size {}", data.size(), shuffle_list[round - 1], round - 1, shuffle_list.size());
			sleep(2);
			curr_qid = 0;
			tries++;
			valid = false;
		}
	} while (!valid && tries <= 3);

	if (curr_qid == 0) {
		gamestate = TRIV_END;
		score = 0;
		curr_answer = "";
		creator->GetBot()->core.log->warn("do_normal_round(): Attempted to retrieve question id {} but got a malformed response. Round was aborted.", shuffle_list[round - 1]);
		if (!silent) {
			creator->EmbedWithFields(settings, fmt::format(_("Q_FETCH_ERROR", settings)), {{_("Q_SPOOPY", settings), _("Q_CONTACT_DEVS", settings), false}, {_("ROUND_STOPPING", settings), _("ERROR_BROKE_IT", settings), false}}, channel_id);
		}
		return;
	}

	if (curr_question != "") {
		asktime = time(NULL);
		curr_answer = trim(curr_answer);
		original_answer = curr_answer;
		std::string t = creator->conv_num(curr_answer, settings);
		if (creator->is_number(t) && t != "0") {
			curr_answer = t;
		}
		curr_answer = creator->tidy_num(curr_answer);
		/* Handle hints */
		if (curr_customhint1.empty()) {
			/* No custom first hint, build one */
			curr_customhint1 = "";
			if (creator->is_number(curr_answer)) {
				curr_customhint1 = creator->MakeFirstHint(curr_answer, settings);
			} else {
				int32_t r = creator->random(1, 12);
				if (settings.language == "bg") {
					curr_customhint1 = fmt::format(_("SCRAMBLED_ANSWER", settings), shuffle1);
				} else {
					if (r <= 4) {
						/* Leave only capital letters */
						curr_customhint1 = curr_answer;
						for (int x = 0; x < curr_customhint1.length(); ++x) {
							if ((curr_customhint1[x] >= 'a' && curr_customhint1[x] <= 'z') || curr_customhint1[x] == '1' || curr_customhint1[x] == '3' || curr_customhint1[x] == '5' || curr_customhint1[x]  == '7' || curr_customhint1[x] == '9') {
									curr_customhint1[x] = '#';
							}
						}
					} else if (r >= 5 && r <= 8) {
					curr_customhint1 = creator->letterlong(curr_answer, settings);
					} else {
						curr_customhint1 = fmt::format(_("SCRAMBLED_ANSWER", settings), shuffle1);
					}
				}
			}
		}
		if (curr_customhint2.empty()) {
			/* No custom second hint, build one */
			curr_customhint2 = "";
			if (creator->is_number(curr_answer) || PCRE("^\\$(\\d+)$").Match(curr_answer)) {
				std::string currency;
				std::vector<std::string> matches;
				curr_customhint2 = curr_answer;
				if (PCRE("^\\$(\\d+)$").Match(curr_customhint2, matches)) {
					curr_customhint2 = matches[1];
					currency = "$";
				}
				curr_customhint2 = currency + curr_customhint2;
				int32_t r = creator->random(1, 13);
				if ((r < 3 && from_string<int32_t>(curr_customhint2, std::dec) <= 10000)) {
					curr_customhint2 = creator->dec_to_roman(from_string<unsigned int>(curr_customhint2, std::dec), settings);
				} else if ((r >= 3 && r < 6) || from_string<int32_t>(curr_customhint2, std::dec) > 10000) {
					curr_customhint2 = fmt::format(_("HEX", settings), from_string<int32_t>(curr_customhint2, std::dec));
				} else if (r >= 6 && r <= 10) {
					curr_customhint2 = fmt::format(_("OCT", settings), from_string<int32_t>(curr_customhint2, std::dec));
				} else {
					curr_customhint2 = fmt::format(_("BIN", settings), from_string<int32_t>(curr_customhint2, std::dec));
				}
			} else {
				int32_t r = creator->random(1, 12);
				if (r <= 4 && settings.language != "bg") {
					/* Transpose only the vowels */
					curr_customhint2 = curr_answer;
					for (int x = 0; x < curr_customhint2.length(); ++x) {
						if (toupper(curr_customhint2[x]) == 'A' || toupper(curr_customhint2[x]) == 'E' || toupper(curr_customhint2[x]) == 'I' || toupper(curr_customhint2[x]) == 'O' || toupper(curr_customhint2[x]) == 'U' || toupper(curr_customhint2[x]) == '2' || toupper(curr_customhint2[x]) == '4' || toupper(curr_customhint2[x]) == '6' || toupper(curr_customhint2[x]) == '8' || toupper(curr_customhint2[x]) == '0') {
							curr_customhint2[x] = '#';
						}
					}
				} else if ((r >= 5 && r <= 6) || settings.language != "en") {
					curr_customhint2 = creator->vowelcount(curr_answer, settings);
				} else {
					/* settings.language check for en above, because piglatin only makes sense in english */
					curr_customhint2 = piglatin(curr_answer);
				}

			}
		}

		if (!silent) {
			creator->EmbedWithFields(settings, fmt::format(_("QUESTION_COUNTER", settings), round, numquestions - 1), {{_("CATEGORY", settings), curr_category, false}, {_("QUESTION", settings), curr_question, false}}, channel_id, fmt::format("https://triviabot.co.uk/report/?c={}&g={}&normal={}", channel_id, guild_id, curr_qid + channel_id), question_image);
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
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, curr_qid)) {
		StopGame(settings);
	}

}

void state_t::do_first_hint()
{
	creator->GetBot()->core.log->debug("do_first_hint: G:{} C:{}", guild_id, channel_id);
	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	if (round % 10 == 0) {
		/* Insane round countdown */
		creator->SimpleEmbed(settings, ":clock10:", fmt::format(_("SECS_LEFT", settings), interval * 2), channel_id);
	} else {
		/* First hint, not insane round */
		creator->SimpleEmbed(settings, ":clock10:", curr_customhint1, channel_id, _("FIRST_HINT", settings));
	}
	gamestate = TRIV_SECOND_HINT;
	score = (interval == TRIV_INTERVAL ? 2 : 4);
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, curr_qid)) {
		StopGame(settings);
	}
}

void state_t::do_second_hint()
{
	creator->GetBot()->core.log->debug("do_second_hint: G:{} C:{}", guild_id, channel_id);
	guild_settings_t settings = creator->GetGuildSettings(guild_id);
	if (round % 10 == 0) {
		/* Insane round countdown */
		creator->SimpleEmbed(settings, ":clock1030:", fmt::format(_("SECS_LEFT", settings), interval), channel_id);
	} else {
		/* Second hint, not insane round */
		creator->SimpleEmbed(settings, ":clock1030:", curr_customhint2, channel_id, _("SECOND_HINT", settings));
	}
	gamestate = TRIV_TIME_UP;
	score = (interval == TRIV_INTERVAL ? 1 : 2);
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, curr_qid)) {
		StopGame(settings);
	}
}

void state_t::do_time_up()
{
	creator->GetBot()->core.log->debug("do_time_up: G:{} C:{}", guild_id, channel_id);
	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	{
		if (round % 10 == 0) {
			int32_t found = insane_num - insane_left;
			creator->SimpleEmbed(settings, ":alarm_clock:", fmt::format(_("INSANE_FOUND", settings), found), channel_id, _("TIME_UP", settings));
		} else if (curr_answer != "") {
			creator->SimpleEmbed(settings, ":alarm_clock:", fmt::format(_("ANS_WAS", settings), curr_answer), channel_id, _("OUT_OF_TIME", settings), answer_image);
		}
		/* FIX: You can only lose your streak on a non-insane round */
		if (curr_answer != "" && round % 10 != 0 && streak > 1 && last_to_answer) {
			creator->SimpleEmbed(settings, ":octagonal_sign:", fmt::format(_("STREAK_SMASHED", settings), fmt::format("<@{}>", last_to_answer), streak), channel_id);
		}
	
		if (curr_answer != "") {
			curr_answer = "";
			if (round % 10 != 0) {
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
	//score = 0;
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, curr_qid)) {
		StopGame(settings);
	}
}

void state_t::do_answer_correct()
{
	creator->GetBot()->core.log->debug("do_answer_correct: G:{} C:{}", guild_id, channel_id);

	guild_settings_t settings = creator->GetGuildSettings(guild_id);

	{
		round++;
		//score = 0;
		curr_answer = "";
		if (round <= numquestions - 2) {
			creator->SimpleEmbed(settings, "<a:loading:658667224067735562>", fmt::format(_("COMING_UP", settings), interval), channel_id, _("REST", settings));
		}
	}

	gamestate = TRIV_ASK_QUESTION;
	if (log_question_index(guild_id, channel_id, round, streak, last_to_answer, gamestate, curr_qid)) {
		StopGame(settings);
	}
}

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
		db::resultset rs = db::query("SELECT * FROM trivia_user_cache WHERE snowflake_id = ?", {snowflake_id});
		if (rs.size() > 0) {
			msg.append(fmt::format("{0}. `{1}#{2:04d}` ({3})\n", count++, rs[0]["username"], from_string<uint32_t>(rs[0]["discriminator"], std::dec), points));
		} else {
			msg.append(fmt::format("{}. <@{}> ({})\n", count++, snowflake_id, points));
		}
	}
	if (msg.empty()) {
		msg = "Nobody has played here today! :cry:";
	}
	guild_settings_t settings = GetGuildSettings(guild_id);
	if (settings.premium && !settings.custom_url.empty()) {
		EmbedWithFields(settings, _("LEADERBOARD", settings), {{_("TOP_TEN", settings), msg, false}, {_("MORE_INFO", settings), fmt::format(_("LEADER_LINK", settings), settings.custom_url), false}}, channel_id);
	} else {
		EmbedWithFields(settings, _("LEADERBOARD", settings), {{_("TOP_TEN", settings), msg, false}, {_("MORE_INFO", settings), fmt::format(_("LEADER_LINK", settings), guild_id), false}}, channel_id);
	}
}

void TriviaModule::Tick()
{
	while (!terminating) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		try
		{
			std::lock_guard<std::mutex> states_lock(states_mutex);
			std::vector<int64_t> expired;
			time_t now = time(NULL);

			for (auto & s : states) {
				if (now >= s.second.next_tick) {
					bot->core.log->trace("Ticking state id {} (now={}, next_tick={})", s.first, now, s.second.next_tick);
					s.second.tick();
					if (s.second.terminating) {
						expired.push_back(s.first);
					}
				}
			}

			for (auto e : expired) {
				bot->core.log->debug("Terminating state id {}", e);
				states.erase(e);
			}
		}
		catch (const std::exception &e) {
			bot->core.log->warn("Uncaught std::exception in TriviaModule::Tick(): {}", e.what());
		}
	}
}

void TriviaModule::DisposeThread(std::thread* t)
{
	bot->DisposeThread(t);
}

void state_t::StopGame(const guild_settings_t &settings)
{
	if (gamestate != TRIV_END) {
		creator->SimpleEmbed(settings, ":octagonal_sign:", _("DASH_STOP", settings), channel_id, _("STOPPING", settings));
		gamestate = TRIV_END;
		terminating = false;
	}
}

void TriviaModule::CheckForQueuedStarts()
{
	db::resultset rs = db::query("SELECT * FROM start_queue ORDER BY queuetime", {});
	for (auto r = rs.begin(); r != rs.end(); ++r) {
		uint64_t guild_id = from_string<uint64_t>((*r)["guild_id"], std::dec);
		/* Check that this guild is on this cluster, if so we can start this game */
		aegis::guild* g = bot->core.find_guild(guild_id);
		if (g) {

			uint64_t channel_id = from_string<uint64_t>((*r)["channel_id"], std::dec);
			uint64_t user_id = from_string<uint64_t>((*r)["user_id"], std::dec);
			uint32_t questions = from_string<uint32_t>((*r)["questions"], std::dec);
			uint32_t quickfire = from_string<uint32_t>((*r)["quickfire"], std::dec);
			uint32_t hintless = from_string<uint32_t>((*r)["hintless"], std::dec);
			std::string category = (*r)["category"];

			bot->core.log->info("Remote start, guild_id={} channel_id={} user_id={} questions={} type={} category='{}'", guild_id, channel_id, user_id, questions, hintless ? "hardcore" : (quickfire ? "quickfire" : "normal"), category);
			guild_settings_t settings = GetGuildSettings(guild_id);
			aegis::channel* channel = bot->core.find_channel(channel_id);

			aegis::gateway::objects::message m(fmt::format("{}{} {}{}", settings.prefix, (hintless ? "hardcore" : (quickfire ? "quickfire" : "start")), questions, (category.empty() ? "" : (std::string(" ") + category))), channel, g);

			struct modevent::message_create msg = {
				*(bot->core.get_shard_mgr().get_shards()[0]),
				*(bot->core.find_user(user_id)),
				*(channel),
				m
			};

			RealOnMessage(msg, msg.msg.get_content(), false, {}, user_id);
	
			/* Delete just this entry as we've processed it */
			db::query("DELETE FROM start_queue WHERE channel_id = ?", {channel_id});
		}
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
	std::string username;
	aegis::gateway::objects::message msg = message.msg;

	// Allow overriding of author id from remote start code
	int64_t author_id = _author_id ? _author_id : msg.get_author_id().get();

	bool isbot = msg.author.is_bot();

	if (!message.has_user()) {
		bot->core.log->debug("Message has no user! Message id {} author id {}", msg.get_id().get(), author_id);
		return true;
	}

	aegis::user* user = bot->core.find_user(author_id);
	if (user) {
		username = user->get_username();
		if (isbot) {
			/* Drop bots here */
			return true;
		}
	}
	
	int64_t guild_id = 0;
	int64_t channel_id = 0;
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
			bot->core.log->warn("Channel is missing!!! C:{} A:{}", msg.get_channel_id().get(), author_id);
			return true;
		}
	} else {
		/* No channel! */
		bot->core.log->debug("Message without channel, M:{} A:{}", msg.get_id().get(), author_id);
		return true;
	}

	if (c) {
		channel_id = c->get_id().get();
	}

	guild_settings_t settings = GetGuildSettings(guild_id);

	if (mentioned && prefix_match->Match(clean_message)) {
		if (c) {
			c->create_message(fmt::format(_("PREFIX", settings), settings.prefix, settings.prefix));
		}
		bot->core.log->debug("Respond to prefix request on channel C:{} A:{}", channel_id, author_id);
		return false;
	}

	// Commands
	if (lowercase(clean_message.substr(0, settings.prefix.length())) == lowercase(settings.prefix)) {

		std::string command = clean_message.substr(settings.prefix.length(), clean_message.length() - settings.prefix.length());
		if (user != nullptr) {
			queue_command(command, author_id, channel_id, guild_id, mentioned, username);
			bot->core.log->info("CMD (USER={}, GUILD={}): <{}> {}", author_id, guild_id, username, clean_message);
		} else {
			bot->core.log->debug("User is null when handling command. C:{} A:{}", channel_id, author_id);
		}

	}
	
	// Answers for active games
	{
		std::lock_guard<std::mutex> states_lock(states_mutex);
		state_t* state = GetState(channel_id);
		if (state) {
			/* The state_t class handles potential answers, but only when a game is running on this guild */
			state->queue_message(clean_message, author_id, username, mentioned);
			bot->core.log->debug("Processed potential answer message from A:{} on C:{}", author_id, channel_id);
		}
	}

	return true;
}

state_t* TriviaModule::GetState(int64_t channel_id) {

	auto state_iter = states.find(channel_id);
	if (state_iter != states.end()) {
		return &state_iter->second;
	}
	return nullptr;
}

ENTRYPOINT(TriviaModule);

