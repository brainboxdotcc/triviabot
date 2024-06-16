/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004,2005,2020,2021,2024 Craig Edwards <support@brainbox.cc>
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

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <streambuf>
#include <sporks/stringops.h>
#include "trivia.h"
#include "commands.h"

command_shard_t::command_shard_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_shard_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	dpp::snowflake guild_id;
	tokens >> guild_id;
	if (!guild_id) {
		guild_id = cmd.guild_id;
	}
	uint64_t shard = (guild_id >> 22) % from_string<uint32_t>(Bot::GetConfig("shardcount"), std::dec);
	uint64_t cluster = shard % creator->GetBot()->GetMaxClusters();

	creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "",
	fmt::format(_("SHARD_IS", settings), shard) + "\n" + _("CLUSTER", settings) + " **" + std::to_string(cluster) + "**", cmd.channel_id, _("SHARD", settings));
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}
