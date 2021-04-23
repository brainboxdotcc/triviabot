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

