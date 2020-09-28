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

#pragma once

#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include "settings.h"

// Number of seconds after which a game is considered hung and its thread exits.
// This can happen if a game gets lost in a discord gateway outage (!)
#define GAME_REAP_SECS 20000

// Number of seconds between states in a normal round. Quickfire is 0.25 of this.
#define TRIV_INTERVAL 20

// Number of seconds between allowed API-bound calls, per channel
#define PER_CHANNEL_RATE_LIMIT 4

typedef std::map<int64_t, int64_t> teamlist_t;
typedef std::map<int64_t, std::string> numstrs_t;

struct field_t
{
	std::string name;
	std::string value;
	bool _inline;
};

/**
 * Module class for trivia system
 */

class TriviaModule : public Module
{
	PCRE* notvowel;
	PCRE* number_tidy_dollars;
	PCRE* number_tidy_nodollars;
	PCRE* number_tidy_positive;
	PCRE* number_tidy_negative;
	PCRE* prefix_match;
	std::map<int64_t, class state_t*> states;
	std::unordered_map<int64_t, time_t> limits;
	std::vector<std::string> api_commands;
	std::thread* presence_update;
	bool terminating;
	std::mutex states_mutex;
	std::mutex cmds_mutex;
	time_t startup;
	json numstrs;
	json* lang;
public:
	TriviaModule(Bot* instigator, ModuleLoader* ml);
	Bot* GetBot();
	virtual ~TriviaModule();
	virtual bool OnPresenceUpdate();
	std::string _(const std::string &k, const guild_settings_t& settings);
	virtual bool OnAllShardsReady();
	virtual bool OnChannelDelete(const modevent::channel_delete &cd);
	virtual bool OnGuildDelete(const modevent::guild_delete &gd);
	int64_t GetActiveGames();
	guild_settings_t GetGuildSettings(int64_t guild_id);
	std::string escape_json(const std::string &s);
	void ProcessEmbed(const std::string &embed_json, int64_t channelID);
	void SimpleEmbed(const std::string &emoji, const std::string &text, int64_t channelID, const std::string &title = "");
	void EmbedWithFields(const std::string &title, std::vector<field_t> fields, int64_t channelID, const std::string &url = "");
	virtual std::string GetVersion();
	virtual std::string GetDescription();
	int random(int min, int max);
	std::string dec_to_roman(unsigned int decimal, const guild_settings_t &settings);
	std::string tidy_num(std::string num);
	void UpdatePresenceLine();
	std::string conv_num(std::string datain, const guild_settings_t &settings);
	std::string letterlong(std::string text, const guild_settings_t &settings);
	std::string vowelcount(std::string text, const guild_settings_t &settings);
	std::string numbertoname(int64_t number, const guild_settings_t& settings);
	std::string GetNearestNumber(int64_t number, const guild_settings_t& settings);
	int64_t GetNearestNumberVal(int64_t number, const guild_settings_t& settings);
	int min3(int x, int y, int z);
	int levenstein(std::string str1, std::string str2);
	bool is_number(const std::string &s);
	std::string MakeFirstHint(const std::string &s, const guild_settings_t &settings,  bool indollars = false);
	void do_insane_round(class state_t* state, bool silent);
	void do_normal_round(class state_t* state, bool silent);
	void do_first_hint(class state_t* state);
	void do_second_hint(class state_t* state);
	void do_time_up(class state_t* state);
	void do_answer_correct(class state_t* state);
	void do_end_game(class state_t* state);
	void show_stats(int64_t guild_id, int64_t channel_id);
	void Tick(class state_t* state);
	void DisposeThread(std::thread* t);
	void StopGame(class state_t* state, const guild_settings_t &settings);
	void CheckForQueuedStarts();
	virtual bool OnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions);
	bool RealOnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions, int64_t author_id = 0);
	void GetHelp(const std::string &section, int64_t channelID, const std::string &botusername, int64_t botid, const std::string &author, int64_t authorid, const guild_settings_t &settings);
	void CacheUser(int64_t user, int64_t channel_id);
};

