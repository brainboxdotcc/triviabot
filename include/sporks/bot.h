/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
 *
 * Copyright 2004 Craig Edwards <support@sporks.gg>
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
 * Sporks itself is a botnix instance, running the infobot and telnet modules. Rather
 * than rewrite this complex bit of code, we connect to it via this scalable connector,
 * which issues it commands over its telnet port. All the commands are queued, to prevent
 * swamping botnix with telnet connections and commands (like the PHP version has potential
 * to do by accident!) and outputs similarly queued to prevent flooding discord.
 *
 * There are two caching mechanisms for discord data, first is aegis itself, and the
 * second level is a mysql databse, which exists to feed information to the dashboard
 * website and give some persistence to the data. Note that from the bot's perspective
 * this data is write-only and the bot will only ever look at the data in aegis, and
 * from the website's perspective this data is read-only and it will look to the discord
 * API for authentication and current server list of a user.
 *
 * All major parts of this bot are modular, and can be hot-reloaded on the fly to prevent
 * having to restart the shards. Please see the modules directory for source code.
 *
 ************************************************************************************/

#pragma once

#include <aegis.hpp>
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

	/* Threads */
	std::thread* thr_presence;

	/* Set to true if all threads are to end */
	bool terminate;

	uint32_t shard_init_count;

	/* Thread handlers */
	void UpdatePresenceThread();	/* Updates the bot presence every 120 seconds */

	void SetSignals();

	uint32_t my_cluster_id;

public:

	asio::io_context* io;

	/* Aegis core */
	aegis::core &core;

	/* Generic named counters */
	std::map<std::string, uint64_t> counters;

	/* The bot's user details from ready event */
	aegis::gateway::objects::user user;

	uint64_t sent_messages;
	uint64_t received_messages;

	Bot(bool development, bool testing, bool intents, aegis::core &aegiscore, asio::io_context* _io);
	virtual ~Bot();

	bool IsDevMode();
	bool IsTestMode();
	bool HasMemberIntents();
	uint32_t GetClusterID();
	void SetClusterID(uint32_t c);

	ModuleLoader* Loader;

	/* Join and delete a non-null pointer to std::thread */
	void DisposeThread(std::thread* thread);

	/* Shorthand to get bot's user id */
	int64_t getID();

	void onReady(aegis::gateway::events::ready ready);
	void onServer(aegis::gateway::events::guild_create gc);
	void onMember(aegis::gateway::events::guild_member_add gma);
	void onChannel(aegis::gateway::events::channel_create channel);
	void onMessage(aegis::gateway::events::message_create message);
	void onChannelDelete(aegis::gateway::events::channel_delete cd);
	void onServerDelete(aegis::gateway::events::guild_delete gd);
	void onRestEnd(std::chrono::steady_clock::time_point start_time, uint16_t code);
	void onTypingStart (aegis::gateway::events::typing_start obj);
	void onMessageUpdate (aegis::gateway::events::message_update obj);
	void onMessageDelete (aegis::gateway::events::message_delete obj);
	void onMessageDeleteBulk (aegis::gateway::events::message_delete_bulk obj);
	void onGuildUpdate (aegis::gateway::events::guild_update obj);
	void onMessageReactionAdd (aegis::gateway::events::message_reaction_add obj);
	void onMessageReactionRemove (aegis::gateway::events::message_reaction_remove obj);
	void onMessageReactionRemoveAll (aegis::gateway::events::message_reaction_remove_all obj);
	void onUserUpdate (aegis::gateway::events::user_update obj);
	void onResumed (aegis::gateway::events::resumed obj);
	void onChannelUpdate (aegis::gateway::events::channel_update obj);
	void onChannelPinsUpdate (aegis::gateway::events::channel_pins_update obj);
	void onGuildBanAdd (aegis::gateway::events::guild_ban_add obj);
	void onGuildBanRemove (aegis::gateway::events::guild_ban_remove obj);
	void onGuildEmojisUpdate (aegis::gateway::events::guild_emojis_update obj);
	void onGuildIntegrationsUpdate (aegis::gateway::events::guild_integrations_update obj);
	void onGuildMemberRemove (aegis::gateway::events::guild_member_remove obj);
	void onGuildMemberUpdate (aegis::gateway::events::guild_member_update obj);
	void onGuildMembersChunk (aegis::gateway::events::guild_members_chunk obj);
	void onGuildRoleCreate (aegis::gateway::events::guild_role_create obj);
	void onGuildRoleUpdate (aegis::gateway::events::guild_role_update obj);
	void onGuildRoleDelete (aegis::gateway::events::guild_role_delete obj);
	void onPresenceUpdate (aegis::gateway::events::presence_update obj);
	void onVoiceStateUpdate (aegis::gateway::events::voice_state_update obj);
	void onVoiceServerUpdate (aegis::gateway::events::voice_server_update obj);
	void onWebhooksUpdate (aegis::gateway::events::webhooks_update obj);

	static std::string GetConfig(const std::string &name);

	static void SetSignal(int signal);
};

