/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
 *
 * Copyright 2019 Craig Edwards <support@sporks.gg>
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
 * There are two caching mechanisms for discord data, first is aegis itself, and the
 * second level is a mysql databse, which exists to feed information to the dashboard
 * website and give some persistence to the data. Note that from the bot's perspective
 * this data is write-only and the bot will only ever look at the data in the bot, and
 * from the website's perspective this data is read-only and it will look to the discord
 * API for authentication and current server list of a user.
 *
 * All major parts of this bot are modular, and can be hot-reloaded on the fly to prevent
 * having to restart the shards. Please see the modules directory for source code.
 *
 ************************************************************************************/

#pragma once

#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>

using json = nlohmann::json;

class Module;
class ModuleLoader;

class Bot {

	/* True if bot is running in development mode */
	bool dev;

	/* True if bot is running in testing mode */
	bool test;

	/* True if the bot has member intents */
	bool memberintents;

	uint32_t my_cluster_id;

	uint32_t shard_init_count;

	/* Thread handlers */
	void UpdatePresenceTimerTick();	/* Updates the bot presence every 30 seconds */

public:
	/* D++ cluster */
	class dpp::cluster* core;

	/* Generic named counters */
	std::map<std::string, uint64_t> counters;

	/* The bot's user details from ready event */
	dpp::user user;

	uint64_t sent_messages;
	uint64_t received_messages;

	Bot(bool development, bool testing, bool intents, dpp::cluster* dppcluster, uint32_t cluster_id);
	virtual ~Bot();

	bool IsDevMode() const;
	bool IsTestMode() const;
	bool HasMemberIntents() const;

	ModuleLoader* Loader;

	/* Join and delete a non-null pointer to std::thread */
	void DisposeThread(std::thread* thread);

	/* Shorthand to get bot's user id */
	int64_t getID();

	uint32_t GetClusterID();
	void SetClusterID(uint32_t c);
	uint32_t GetMaxClusters() const;

	void onReady(const dpp::ready_t &ready);
	void onServer(const dpp::guild_create_t &gc);
	void onMessage(const dpp::message_create_t &message);
	void onServerDelete(const dpp::guild_delete_t &gd);
	void onGuildUpdate (const dpp::guild_update_t &event);
	void onResumed (const dpp::resumed_t &event);
	void onPresenceUpdate (const dpp::presence_update_t &event);
	void onWebhooksUpdate (const dpp::webhooks_update_t &event);
	void onEntitlementDelete(const dpp::entitlement_delete_t& ed);
	void onEntitlementCreate(const dpp::entitlement_create_t& ed);
	void onEntitlementUpdate(const dpp::entitlement_update_t& ed);

	static std::string GetConfig(const std::string &name);

	static void SetSignal(int signal);
};
