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


#include "settings.h"

guild_settings_t::guild_settings_t(int64_t _guild_id,
		const std::string &_prefix,
		const std::vector<int64_t> &_moderator_roles,
		uint32_t _embedcolour,
		bool _premium,
		bool _only_mods_stop,
		bool _only_mods_start,
		bool _role_reward_enabled,
		int64_t _role_reward_id,
		const std::string &_custom_url,
		const std::string &_language,
		int32_t _question_interval)
	        :
			guild_id(_guild_id),
			prefix(_prefix),
			moderator_roles(_moderator_roles),
			embedcolour(_embedcolour),
			premium(_premium),
			only_mods_stop(_only_mods_stop),
			only_mods_start(_only_mods_start),
			role_reward_enabled(_role_reward_enabled),
			role_reward_id(_role_reward_id),
			custom_url(_custom_url),
			language(_language),
			question_interval(_question_interval < 20 || _question_interval > 120 ? 20 : _question_interval)
{ }
