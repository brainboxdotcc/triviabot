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

#include "trivia.h"
#include <sporks/database.h>
#include <dpp/dpp.h>

static void refresh_guild_premium(uint64_t guild_id)
{
	db::query(
		"UPDATE bot_guild_settings "
		"SET premium = EXISTS("
		"SELECT 1 "
		"FROM premium_credits "
		"WHERE guild_id = ? "
		"AND active = 1"
		") "
		"WHERE snowflake_id = ?",
		{ guild_id, guild_id }
	);
}

bool TriviaModule::OnEntitlementCreate(const dpp::entitlement_create_t& entitlement)
{
	db::query(
		"INSERT INTO premium_credits (user_id, guild_id, subscription_id, active, since, plan_id, payment_failed, created_at, updated_at) "
		"VALUES(?, ?, ?, 1, now(), 'ssod-monthly', 0, now(), now()) "
		"ON DUPLICATE KEY UPDATE subscription_id = ?, active = 1, updated_at = now()",
		{
			entitlement.created.user_id,
			entitlement.created.guild_id,
			entitlement.created.subscription_id,
			entitlement.created.subscription_id
		}
	);

	refresh_guild_premium(entitlement.created.guild_id);
	return true;
}

bool TriviaModule::OnEntitlementUpdate(const dpp::entitlement_update_t& entitlement)
{
	db::query(
		"UPDATE premium_credits "
		"SET active = ?, updated_at = now() "
		"WHERE user_id = ? AND subscription_id = ?",
		{
			entitlement.updating_entitlement.is_deleted() || entitlement.updating_entitlement.ends_at < time(nullptr) ? 0 : 1,
			entitlement.updating_entitlement.user_id,
			entitlement.updating_entitlement.subscription_id
		}
	);

	refresh_guild_premium(entitlement.updating_entitlement.guild_id);
	return true;
}

bool TriviaModule::OnEntitlementDelete(const dpp::entitlement_delete_t& entitlement)
{
	db::query(
		"UPDATE premium_credits "
		"SET active = 0, cancel_date = now(), updated_at = now() "
		"WHERE user_id = ? AND subscription_id = ?",
		{ entitlement.deleted.user_id, entitlement.deleted.subscription_id }
	);

	refresh_guild_premium(entitlement.deleted.guild_id);
	return true;
}
