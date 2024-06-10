#include "trivia.h"
#include <sporks/database.h>
#include <dpp/dpp.h>

bool TriviaModule::OnEntitlementCreate(const dpp::entitlement_create_t& entitlement)
{
	db::query(
		"INSERT INTO premium_credits (user_id, guild_id, subscription_id, active, since, plan_id, payment_failed, created_at, updated_at)"
		"VALUES(?, ?, ?, 1, now(), 'ssod-monthly', 0, now(), now()) ON DUPLICATE KEY UPDATE subscription_id = ?, active = 1",
		{ entitlement.created.user_id, entitlement.created.guild_id, entitlement.created.subscription_id, entitlement.created.subscription_id }
	);
	return true;
}

bool TriviaModule::OnEntitlementUpdate(const dpp::entitlement_update_t& entitlement)
{
	db::query(
		"UPDATE premium_credits SET active = ?, updated_at = now() WHERE user_id = ? AND subscription_id = ?",
		{
			entitlement.updating_entitlement.is_deleted() || entitlement.updating_entitlement.ends_at < time(nullptr) ? 0 : 1,
			entitlement.updating_entitlement.user_id,
			entitlement.updating_entitlement.subscription_id
		}
	);
	return true;
}

bool TriviaModule::OnEntitlementDelete(const dpp::entitlement_delete_t& entitlement)
{
	db::query(
		"UPDATE premium_credits SET active = 0, cancel_date = now(), updated_at = now() WHERE user_id = ? AND subscription_id = ?",
		{ entitlement.deleted.user_id, entitlement.deleted.subscription_id }
	);
	return true;
}

