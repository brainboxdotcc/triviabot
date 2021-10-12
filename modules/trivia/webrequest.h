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
#include <string>
#include "trivia.h"

/* Live API endpoint URL */
#define BACKEND_HOST_LIVE	"triviabot.co.uk"
#define BACKEND_PATH_LIVE	"/api/{0}"

/* Development API endpoint URL */
#define BACKEND_HOST_DEV	"beta.brainbox.cc"
#define BACKEND_PATH_DEV	"/api/{0}"

using json = nlohmann::json;

// Represents the current streak data for a guild
struct streak_t
{
	uint32_t personalbest;
	uint64_t topstreaker;
	uint32_t bigstreak;
};

typedef std::function<void(uint64_t, std::vector<std::string>)> insane_cb;
typedef std::function<void(uint32_t, std::string)> get_curr_team_callback_t;
typedef std::function<void(uint32_t)> int_callback;
typedef std::function<void(bool)> bool_callback;
typedef std::function<void(std::string)> string_callback;
typedef std::function<void(streak_t)> streak_callback;
typedef std::function<void(nlohmann::json)> json_callback;
typedef std::function<void(std::vector<std::string>)> vector_callback;

void set_io_context(const std::string &apikey, class Bot* _bot, class TriviaModule* _module);


// These functions used to query the REST API but are more efficient doing direct database queries.

void fetch_insane_round(uint64_t guild_id, const class guild_settings_t &settings, insane_cb callback);
void update_score_only(uint64_t snowflake_id, uint64_t guild_id, int score, uint64_t channel_id);
uint32_t update_score(uint64_t snowflake_id, uint64_t guild_id, double recordtime, uint64_t id, int score);
void get_current_team(uint64_t snowflake_id, get_curr_team_callback_t callback);
void leave_team(uint64_t snowflake_id);
void get_streak(uint64_t snowflake_id, uint64_t guild_id, streak_callback callback);
void check_team_exists(const std::string &team, bool_callback callback);
void add_team_points(const std::string &team, int points, uint64_t snowflake_id);
void cache_user(const class dpp::user *_user, const class dpp::guild *_guild, const class dpp::guild_member* gi);
void log_question_index(guild_settings_t settings, uint64_t guild_id, uint64_t channel_id, uint32_t index, uint32_t streak, uint64_t lastanswered, uint32_t state, uint32_t qid);
void log_game_start(uint64_t guild_id, uint64_t channel_id, uint64_t number_questions, bool quickfire, const std::string &channel_name, uint64_t user_id, const std::vector<std::string> &questions, bool hintless);
void log_game_end(uint64_t guild_id, uint64_t channel_id);
void change_streak(uint64_t snowflake_id, uint64_t guild_id, int score);
std::vector<std::string> get_api_command_names();

// These execute external PHP scripts, through a special handler. They bypass REST.
// Non-blocking in own thread. Only a few commands still use this e.g. /globalrank.

void custom_command(const std::string& interaction_token, dpp::snowflake command_id, const guild_settings_t& settings, TriviaModule* tm, const std::string &command, const std::string &parameters, uint64_t user_id, uint64_t channel_id, uint64_t guild_id);

// These functions query the REST API and are not as performant as the functions above. Some of these cannot
// currently be rewritten as direct queries, as they use external apis like neutrino, or are hooked into the
// achievement system, or are REST by design such as those that use graphics APIs.

void fetch_shuffle_list(uint64_t guild_id, const std::string &category, vector_callback callback);		/* Converted to non-blocking */
void join_team(uint64_t snowflake_id, const std::string &team, uint64_t channel_id, bool_callback callback);	/* Converted to non-blocking */
void create_new_team(const std::string &teamname, string_callback callback);					/* Converted to non-blocking */
void send_hint(uint64_t snowflake_id, const std::string &hint, uint32_t remaining);				/* Fire-and-forget, scalable */
void get_active(const std::string &hostname, uint64_t cluster_id, json_callback callback);			/* Converted to non-blocking */
void check_achievement(const std::string &when, uint64_t user_id, uint64_t guild_id);				/* Fire-and-forget, scalable */

// These two functions use Discord API, but not with the bot token
void CheckCreateWebhook(const guild_settings_t & s, TriviaModule* t, uint64_t channel_id);			/* Uses DPP calls */
void PostWebhook(const std::string &webhook_url, const std::string &embed, uint64_t channel_id);		/* Fire-and-forget, scalable */

