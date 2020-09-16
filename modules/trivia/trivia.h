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

#include <string>
#include <map>
#include <vector>

#define TRIVIA_VERSION "1.0.0"

// Number of seconds after which a game is considered hung and its thread exits.
// This can happen if a game gets lost in a discord gateway outage (!)
#define GAME_REAP_SECS 20000

// Number of seconds between states in a normal round. Quickfire is 0.25 of this.
#define TRIV_INTERVAL 20

// Number of seconds between allowed API-bound calls, per channel
#define PER_CHANNEL_RATE_LIMIT 4

typedef std::map<int64_t, int64_t> teamlist_t;
typedef std::map<int64_t, std::string> numstrs_t;

enum trivia_state_t
{
	TRIV_ASK_QUESTION = 1,
	TRIV_FIRST_HINT = 2,
	TRIV_SECOND_HINT = 3,
	TRIV_TIME_UP = 4,
	TRIV_ANSWER_CORRECT = 5,
	TRIV_END = 6
};


struct field_t
{
	std::string name;
	std::string value;
	bool _inline;
};

class guild_settings_t
{
 public:
	int64_t guild_id;
	std::string prefix;
	uint32_t embedcolour;
	std::vector<int64_t> moderator_roles;
	bool premium;
	bool only_mods_stop;
	bool role_reward_enabled;
	int64_t role_reward_id;
	std::string custom_url;
	std::string language;
	guild_settings_t(int64_t _guild_id, const std::string &_prefix, const std::vector<int64_t> &_moderator_roles, uint32_t _embedcolour, bool _premium, bool _only_mods_stop, bool _role_reward_enabled, int64_t _role_reward_id, const std::string &_custom_url, const std::string &_language);
};

class state_t
{
	class TriviaModule* creator;
 public:
	bool terminating;
	uint64_t channel_id;
	uint64_t guild_id;
	uint32_t numquestions;
	uint32_t round;
	uint32_t score;
	time_t start_time;
	std::vector<std::string> shuffle_list;
	trivia_state_t gamestate;
	int64_t curr_qid;
	time_t recordtime;
	std::string curr_question;
	std::string curr_answer;
	std::string curr_customhint1;
	std::string curr_customhint2;
	std::string curr_category;
	std::string shuffle1;
	std::string shuffle2;
	time_t curr_lastasked;
	time_t curr_recordtime;
	std::string curr_lastcorrect;
	int64_t last_to_answer;
	uint32_t streak;
	time_t asktime;
	bool found;
	time_t interval;
	uint32_t insane_num;
	uint32_t insane_left;
	uint32_t curr_timesasked;
	time_t next_quickfire;
	std::map<std::string, bool> insane;
	std::thread* timer;

	state_t(class TriviaModule* _creator);
	~state_t();
	void tick();
};
