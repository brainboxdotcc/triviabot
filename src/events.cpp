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
 ************************************************************************************/

#include <aegis.hpp>
#include <sporks/bot.h>
#include <sporks/includes.h>
#include <sporks/modules.h>

void Bot::onTypingStart (aegis::gateway::events::typing_start obj)
{
	FOREACH_MOD(I_OnTypingStart, OnTypingStart(obj));
}


void Bot::onMessageUpdate (aegis::gateway::events::message_update obj)
{
	FOREACH_MOD(I_OnMessageUpdate, OnMessageUpdate(obj));
}


void Bot::onMessageDelete (aegis::gateway::events::message_delete obj)
{
	FOREACH_MOD(I_OnMessageDelete, OnMessageDelete(obj));
}


void Bot::onMessageDeleteBulk (aegis::gateway::events::message_delete_bulk obj)
{
	FOREACH_MOD(I_OnMessageDeleteBulk, OnMessageDeleteBulk(obj));
}


void Bot::onGuildUpdate (aegis::gateway::events::guild_update obj)
{
	FOREACH_MOD(I_OnGuildUpdate, OnGuildUpdate(obj));
}


void Bot::onMessageReactionAdd (aegis::gateway::events::message_reaction_add obj)
{
	FOREACH_MOD(I_OnMessageReactionAdd, OnMessageReactionAdd(obj));
}


void Bot::onMessageReactionRemove (aegis::gateway::events::message_reaction_remove obj)
{
	FOREACH_MOD(I_OnMessageReactionRemove, OnMessageReactionRemove(obj));
}


void Bot::onMessageReactionRemoveAll (aegis::gateway::events::message_reaction_remove_all obj)
{
	FOREACH_MOD(I_OnMessageReactionRemoveAll, OnMessageReactionRemoveAll(obj));
}


void Bot::onUserUpdate (aegis::gateway::events::user_update obj)
{
	FOREACH_MOD(I_OnUserUpdate, OnUserUpdate(obj));
}


void Bot::onResumed (aegis::gateway::events::resumed obj)
{
	FOREACH_MOD(I_OnResumed, OnResumed(obj));
}


void Bot::onChannelUpdate (aegis::gateway::events::channel_update obj)
{
	FOREACH_MOD(I_OnChannelUpdate, OnChannelUpdate(obj));
}


void Bot::onChannelPinsUpdate (aegis::gateway::events::channel_pins_update obj)
{
	FOREACH_MOD(I_OnChannelPinsUpdate, OnChannelPinsUpdate(obj));
}


void Bot::onGuildBanAdd (aegis::gateway::events::guild_ban_add obj)
{
	FOREACH_MOD(I_OnGuildBanAdd, OnGuildBanAdd(obj));
}


void Bot::onGuildBanRemove (aegis::gateway::events::guild_ban_remove obj)
{
	FOREACH_MOD(I_OnGuildBanRemove, OnGuildBanRemove(obj));
}


void Bot::onGuildEmojisUpdate (aegis::gateway::events::guild_emojis_update obj)
{
	FOREACH_MOD(I_OnGuildEmojisUpdate, OnGuildEmojisUpdate(obj));
}


void Bot::onGuildIntegrationsUpdate (aegis::gateway::events::guild_integrations_update obj)
{
	FOREACH_MOD(I_OnGuildIntegrationsUpdate, OnGuildIntegrationsUpdate(obj));
}


void Bot::onGuildMemberRemove (aegis::gateway::events::guild_member_remove obj)
{
	FOREACH_MOD(I_OnGuildMemberRemove, OnGuildMemberRemove(obj));
}


void Bot::onGuildMemberUpdate (aegis::gateway::events::guild_member_update obj)
{
	FOREACH_MOD(I_OnGuildMemberUpdate, OnGuildMemberUpdate(obj));
}


void Bot::onGuildMembersChunk (aegis::gateway::events::guild_members_chunk obj)
{
	FOREACH_MOD(I_OnGuildMembersChunk, OnGuildMembersChunk(obj));
}


void Bot::onGuildRoleCreate (aegis::gateway::events::guild_role_create obj)
{
	FOREACH_MOD(I_OnGuildRoleCreate, OnGuildRoleCreate(obj));
}


void Bot::onGuildRoleUpdate (aegis::gateway::events::guild_role_update obj)
{
	FOREACH_MOD(I_OnGuildRoleUpdate, OnGuildRoleUpdate(obj));
}


void Bot::onGuildRoleDelete (aegis::gateway::events::guild_role_delete obj)
{
	FOREACH_MOD(I_OnGuildRoleDelete, OnGuildRoleDelete(obj));
}


void Bot::onPresenceUpdate (aegis::gateway::events::presence_update obj)
{
	FOREACH_MOD(I_OnPresenceUpdateWS, OnPresenceUpdateWS(obj));
}


void Bot::onVoiceStateUpdate (aegis::gateway::events::voice_state_update obj)
{
	FOREACH_MOD(I_OnVoiceStateUpdate, OnVoiceStateUpdate(obj));
}


void Bot::onVoiceServerUpdate (aegis::gateway::events::voice_server_update obj)
{
	FOREACH_MOD(I_OnVoiceServerUpdate, OnVoiceServerUpdate(obj));
}


void Bot::onWebhooksUpdate (aegis::gateway::events::webhooks_update obj)
{
	FOREACH_MOD(I_OnWebhooksUpdate, OnWebhooksUpdate(obj));
}

