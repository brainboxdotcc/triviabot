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

#include <dpp/dpp.h>
#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <deque>
#include "settings.h"
#include "commands.h"
#include "state.h"

// Number of seconds after which a game is considered hung and its thread exits.
// This can happen if a game gets lost in a discord gateway outage (!)
#define GAME_REAP_SECS 20000

// Number of seconds between states in a normal round. Quickfire is 0.25 of this.
#define TRIV_INTERVAL 20

// Number of seconds between allowed API-bound calls, per channel
#define PER_CHANNEL_RATE_LIMIT 4

typedef std::map<dpp::snowflake, dpp::snowflake> teamlist_t;

struct field_t
{
	std::string name;
	std::string value;
	bool _inline;
};

struct guild_cache_queued_t
{
	dpp::snowflake guild_id;
	std::string name;
	std::string icon;
	dpp::snowflake owner_id;
};

struct last_streak_t {
	uint32_t streak;
	time_t time;
	dpp::snowflake lastanswered;
};

/**
 * Module class for trivia system
 */

class TriviaModule : public Module
{
	PCRE* number_tidy_dollars;
	PCRE* number_tidy_nodollars;
	PCRE* number_tidy_positive;
	PCRE* number_tidy_negative;
	PCRE* prefix_match;
	std::unordered_map<dpp::snowflake, time_t> limits;
	std::unordered_map<dpp::snowflake, time_t> last_rl_warning;
	std::vector<std::string> api_commands;
	std::thread* presence_update;
	bool terminating;
	std::mutex cmds_mutex;
	std::mutex cmdmutex;
	std::mutex guildqueuemutex;
	std::deque<in_cmd> commandqueue;
	std::deque<in_cmd> to_process;
	std::vector<guild_cache_queued_t> guilds_to_update;
	std::thread* command_processor;
	std::thread* game_tick_thread;
	std::thread* guild_queue_thread;
	std::mutex lang_mutex;
	time_t lastlang;
	command_list_t commands;
	void CheckLangReload();
	bool booted;
public:
	time_t startup;
	json* lang;
	std::mutex states_mutex;
	std::map<dpp::snowflake, state_t> states;

	std::mutex cs_mutex;
	std::mutex wh_mutex;
	std::unordered_map<dpp::snowflake, last_streak_t> last_channel_streaks;
	std::unordered_map<dpp::snowflake, std::string> webhooks;

	TriviaModule(Bot* instigator, ModuleLoader* ml);
	Bot* GetBot();
	virtual ~TriviaModule();
	void SetupCommands();
	void DoExternalCommands(std::vector<dpp::slashcommand>& normal, std::vector<dpp::slashcommand>& admin);
	void queue_command(const std::string &message, dpp::snowflake author, dpp::snowflake channel, dpp::snowflake guild, bool mention, const std::string &username, bool from_dashboard, dpp::user u, dpp::guild_member gm);
	void handle_command(const in_cmd &cmd);
	void ProcessCommands();
	void ProcessGuildQueue();
	virtual bool OnPresenceUpdate();
	std::string _(const std::string &k, const guild_settings_t& settings);
	virtual bool OnAllShardsReady();
	virtual bool OnChannelDelete(const dpp::channel_delete_t &cd);
	virtual bool OnGuildDelete(const dpp::guild_delete_t &gd);

	/* Returns a local count */
	uint64_t GetActiveLocalGames();

	/* These return a sum across all clusters using the database */
	uint64_t GetActiveGames();
	uint64_t GetGuildTotal();
	uint64_t GetMemberTotal();
	uint64_t GetChannelTotal();

	const guild_settings_t GetGuildSettings(dpp::snowflake guild_id);
	std::string escape_json(const std::string &s);
	void ProcessEmbed(const class guild_settings_t& settings, const std::string &embed_json, dpp::snowflake channelID);
	void SimpleEmbed(const class guild_settings_t& settings, const std::string &emoji, const std::string &text, dpp::snowflake channelID, const std::string &title = "", const std::string &image = "", const std::string &thumbnail = "");
	void EmbedWithFields(const class guild_settings_t& settings, const std::string &title, std::vector<field_t> fields, dpp::snowflake channelID, const std::string &url = "", const std::string &image = "", const std::string &thumbnail = "");
	virtual std::string GetVersion();
	virtual std::string GetDescription();
	int random(int min, int max);
	std::string dec_to_roman(unsigned int decimal, const guild_settings_t &settings);
	std::string tidy_num(std::string num);
	void UpdatePresenceLine();
	std::string conv_num(std::string datain, const guild_settings_t &settings);
	std::string letterlong(std::string text, const guild_settings_t &settings);
	std::string vowelcount(const std::string &text, const guild_settings_t &settings);
	std::string numbertoname(uint64_t number, const guild_settings_t& settings);
	std::string GetNearestNumber(uint64_t number, const guild_settings_t& settings);
	uint64_t GetNearestNumberVal(uint64_t number, const guild_settings_t& settings);
	int min3(int x, int y, int z);
	int levenstein(std::string str1, std::string str2);
	bool is_number(const std::string &s);
	std::string MakeFirstHint(const std::string &s, const guild_settings_t &settings,  bool indollars = false);
	void show_stats(dpp::snowflake guild_id, dpp::snowflake channel_id);
	void Tick();
	void DisposeThread(std::thread* t);
	void CheckForQueuedStarts();
	virtual bool OnMessage(const dpp::message_create_t &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions);
	virtual bool OnGuildCreate(const dpp::guild_create_t &guild);
	bool RealOnMessage(const dpp::message_create_t &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions, dpp::snowflake author_id = 0);
	void GetHelp(const std::string &section, dpp::snowflake channelID, const std::string &botusername, dpp::snowflake botid, const std::string &author, dpp::snowflake authorid, const guild_settings_t &settings);
	void CacheUser(dpp::snowflake user, dpp::user _user, dpp::guild_member gm, dpp::snowflake channel_id);
	void CheckReconnects();

	/** DO NOT CALL THIS METHOD without wrapping it with the states_mutex.
	 *
	 * This is important for thread safety and to prevent race conditions!
	 * Keep hold of the states_mutex until you're done with the object even if all you do is read from it!
	 *
	 * Returns nullptr if there is no active game on the given channel id.
	 */
	state_t* GetState(dpp::snowflake channel_id);
};

