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
 ************************************************************************************/

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <sporks/bot.h>
#include <sporks/includes.h>
#include <sporks/modules.h>
#include <sporks/stringops.h>

void Bot::onTypingStart (const dpp::typing_start_t &obj)
{
	FOREACH_MOD(I_OnTypingStart, OnTypingStart(obj));
}


void Bot::onMessageUpdate (const dpp::message_update_t &obj)
{
	FOREACH_MOD(I_OnMessageUpdate, OnMessageUpdate(obj));
}


void Bot::onMessageDelete (const dpp::message_delete_t &obj)
{
	FOREACH_MOD(I_OnMessageDelete, OnMessageDelete(obj));
}


void Bot::onMessageDeleteBulk (const dpp::message_delete_bulk_t &obj)
{
	FOREACH_MOD(I_OnMessageDeleteBulk, OnMessageDeleteBulk(obj));
}


void Bot::onGuildUpdate (const dpp::guild_update_t &obj)
{
	FOREACH_MOD(I_OnGuildUpdate, OnGuildUpdate(obj));
}


void Bot::onMessageReactionAdd (const dpp::message_reaction_add_t &obj)
{
	FOREACH_MOD(I_OnMessageReactionAdd, OnMessageReactionAdd(obj));
}


void Bot::onMessageReactionRemove (const dpp::message_reaction_remove_t &obj)
{
	FOREACH_MOD(I_OnMessageReactionRemove, OnMessageReactionRemove(obj));
}


void Bot::onMessageReactionRemoveAll (const dpp::message_reaction_remove_all_t &obj)
{
	FOREACH_MOD(I_OnMessageReactionRemoveAll, OnMessageReactionRemoveAll(obj));
}


void Bot::onUserUpdate (const dpp::user_update_t &obj)
{
	FOREACH_MOD(I_OnUserUpdate, OnUserUpdate(obj));
}


void Bot::onResumed (const dpp::resumed_t &obj)
{
	FOREACH_MOD(I_OnResumed, OnResumed(obj));
}


void Bot::onChannelUpdate (const dpp::channel_update_t &obj)
{
	FOREACH_MOD(I_OnChannelUpdate, OnChannelUpdate(obj));
}


void Bot::onChannelPinsUpdate (const dpp::channel_pins_update_t &obj)
{
	FOREACH_MOD(I_OnChannelPinsUpdate, OnChannelPinsUpdate(obj));
}


void Bot::onGuildBanAdd (const dpp::guild_ban_add_t &obj)
{
	FOREACH_MOD(I_OnGuildBanAdd, OnGuildBanAdd(obj));
}


void Bot::onGuildBanRemove (const dpp::guild_ban_remove_t &obj)
{
	FOREACH_MOD(I_OnGuildBanRemove, OnGuildBanRemove(obj));
}


void Bot::onGuildEmojisUpdate (const dpp::guild_emojis_update_t &obj)
{
	FOREACH_MOD(I_OnGuildEmojisUpdate, OnGuildEmojisUpdate(obj));
}


void Bot::onGuildIntegrationsUpdate (const dpp::guild_integrations_update_t &obj)
{
	FOREACH_MOD(I_OnGuildIntegrationsUpdate, OnGuildIntegrationsUpdate(obj));
}


void Bot::onGuildMemberRemove (const dpp::guild_member_remove_t &obj)
{
	FOREACH_MOD(I_OnGuildMemberRemove, OnGuildMemberRemove(obj));
}


void Bot::onGuildMemberUpdate (const dpp::guild_member_update_t &obj)
{
	FOREACH_MOD(I_OnGuildMemberUpdate, OnGuildMemberUpdate(obj));
}


void Bot::onGuildMembersChunk (const dpp::guild_members_chunk_t &obj)
{
	FOREACH_MOD(I_OnGuildMembersChunk, OnGuildMembersChunk(obj));
}


void Bot::onGuildRoleCreate (const dpp::guild_role_create_t &obj)
{
	FOREACH_MOD(I_OnGuildRoleCreate, OnGuildRoleCreate(obj));
}


void Bot::onGuildRoleUpdate (const dpp::guild_role_update_t &obj)
{
	FOREACH_MOD(I_OnGuildRoleUpdate, OnGuildRoleUpdate(obj));
}


void Bot::onGuildRoleDelete (const dpp::guild_role_delete_t &obj)
{
	FOREACH_MOD(I_OnGuildRoleDelete, OnGuildRoleDelete(obj));
}


void Bot::onPresenceUpdate (const dpp::presence_update_t &obj)
{
	FOREACH_MOD(I_OnPresenceUpdateWS, OnPresenceUpdateWS(obj));
}


void Bot::onVoiceStateUpdate (const dpp::voice_state_update_t &obj)
{
	FOREACH_MOD(I_OnVoiceStateUpdate, OnVoiceStateUpdate(obj));
}


void Bot::onVoiceServerUpdate (const dpp::voice_server_update_t &obj)
{
	FOREACH_MOD(I_OnVoiceServerUpdate, OnVoiceServerUpdate(obj));
}


void Bot::onWebhooksUpdate (const dpp::webhooks_update_t &obj)
{
	FOREACH_MOD(I_OnWebhooksUpdate, OnWebhooksUpdate(obj));
}

void Bot::onChannel(const dpp::channel_create_t& channel_create) {
	FOREACH_MOD(I_OnChannelCreate, OnChannelCreate(channel_create));
}

void Bot::onChannelDelete(const dpp::channel_delete_t& cd) {
	FOREACH_MOD(I_OnChannelDelete, OnChannelDelete(cd));
}

void Bot::onServerDelete(const dpp::guild_delete_t& gd) {
	FOREACH_MOD(I_OnGuildDelete, OnGuildDelete(gd));
}

void Bot::onEntitlementDelete(const dpp::entitlement_delete_t& ed) {
	FOREACH_MOD(I_OnEntitlementDelete, OnEntitlementDelete(ed));
}

void Bot::onEntitlementCreate(const dpp::entitlement_create_t& ed) {
	FOREACH_MOD(I_OnEntitlementCreate, OnEntitlementCreate(ed));
}

void Bot::onEntitlementUpdate(const dpp::entitlement_update_t& ed) {
	FOREACH_MOD(I_OnEntitlementUpdate, OnEntitlementUpdate(ed));
}

void Bot::onServer(const dpp::guild_create_t& gc) {
	FOREACH_MOD(I_OnGuildCreate, OnGuildCreate(gc));
}

/**
 * This runs its own thread that wakes up every 30 seconds (after an initial 2 minute warmup).
 * Modules can attach to it for a simple 30 second interval timer via the OnPresenceUpdate() method.
 */
void Bot::UpdatePresenceThread() {
	dpp::utility::set_thread_name("bot/presence_ev");
	std::this_thread::sleep_for(std::chrono::seconds(120));
	while (!this->terminate) {
		FOREACH_MOD(I_OnPresenceUpdate, OnPresenceUpdate());
		std::this_thread::sleep_for(std::chrono::seconds(30));
	}
}

/**
 * Stores a new guild member to the database for use in the dashboard
 */
void Bot::onMember(const dpp::guild_member_add_t& gma) {
	FOREACH_MOD(I_OnGuildMemberAdd, OnGuildMemberAdd(gma));
}

/**
 * Returns the bot's snowflake id
 */
int64_t Bot::getID() {
	return this->user.id;
}

/**
 * Announces that the bot is online. Each shard receives one of the events.
 */
void Bot::onReady(const dpp::ready_t& ready) {
	this->user = core->me;
	FOREACH_MOD(I_OnReady, OnReady(ready));

	/* Event broadcast when all shards are ready */
	shard_init_count++;

	core->log(dpp::ll_debug, fmt::format("onReady({}/{})", shard_init_count, core->numshards / (core->maxclusters ? core->maxclusters : 1)));

	/* Event broadcast when all shards are ready */
	/* BUGFIX: In a clustered environment, the shard max is divided by the number of clusters */
	if (shard_init_count == core->numshards / (core->maxclusters ? core->maxclusters : 1)) {
		core->log(dpp::ll_debug, fmt::format("OnAllShardsReady()!"));
		FOREACH_MOD(I_OnAllShardsReady, OnAllShardsReady());
	}
}

/**
 * Called on receipt of each message. We do our own cleanup of the message, sanitising any
 * mentions etc from the text before passing it along to modules. The bot's builtin ignore list
 * and a hard coded check against bots/webhooks and itself happen before any module calls,
 * and can't be overridden.
 */
void Bot::onMessage(const dpp::message_create_t &message) {

	if (!message.msg.author.id) {
		core->log(dpp::ll_info, fmt::format("Message dropped, no author: {}", message.msg.content));
		return;
	}
	/* Ignore self, and bots */
	if (message.msg.author.id != user.id && message.msg.author.is_bot() == false) {
		received_messages++;
		/* Replace all mentions with raw nicknames */
		bool mentioned = false;
		std::string mentions_removed = message.msg.content;
		std::vector<std::string> stringmentions;
		for (auto m = message.msg.mentions.begin(); m != message.msg.mentions.end(); ++m) {
			stringmentions.push_back(std::to_string(m->first.id));
			mentions_removed = ReplaceString(mentions_removed, std::string("<@") + std::to_string(m->first.id) + ">", m->first.username);
			mentions_removed = ReplaceString(mentions_removed, std::string("<@!") + std::to_string(m->first.id) + ">", m->first.username);
			if (m->first.id == user.id) {
				mentioned = true;
			}
		}

		std::string botusername = this->user.username;
		while (mentions_removed.substr(0, botusername.length()) == botusername) {
			mentions_removed = trim(mentions_removed.substr(botusername.length(), mentions_removed.length()));
		}
		mentions_removed = trim(mentions_removed);

		/* Call modules */
		FOREACH_MOD(I_OnMessage,OnMessage(message, mentions_removed, mentioned, stringmentions));
	}
}

