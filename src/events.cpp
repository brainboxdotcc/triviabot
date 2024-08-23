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

void Bot::onGuildUpdate (const dpp::guild_update_t &obj)
{
	FOREACH_MOD(I_OnGuildUpdate, OnGuildUpdate(obj));
}

void Bot::onResumed (const dpp::resumed_t &obj)
{
	FOREACH_MOD(I_OnResumed, OnResumed(obj));
}

void Bot::onPresenceUpdate (const dpp::presence_update_t &obj)
{
	FOREACH_MOD(I_OnPresenceUpdateWS, OnPresenceUpdateWS(obj));
}

void Bot::onWebhooksUpdate (const dpp::webhooks_update_t &obj)
{
	FOREACH_MOD(I_OnWebhooksUpdate, OnWebhooksUpdate(obj));
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
 * This wakes up every 30 seconds
 * Modules can attach to it for a simple 30 second interval timer via the OnPresenceUpdate() method.
 */
void Bot::UpdatePresenceTimerTick() {
	core->start_timer([this](dpp::timer t) {
		FOREACH_MOD(I_OnPresenceUpdate, OnPresenceUpdate());
	}, 30);
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
		for (const auto & mention : message.msg.mentions) {
			stringmentions.push_back(std::to_string(mention.first.id));
			mentions_removed = ReplaceString(mentions_removed, std::string("<@") + std::to_string(mention.first.id) + ">", mention.first.username);
			mentions_removed = ReplaceString(mentions_removed, std::string("<@!") + std::to_string(mention.first.id) + ">", mention.first.username);
			if (mention.first.id == user.id) {
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

