/************************************************************************************
 * 
 * Sporks, the learning, scriptable Discord bot!
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

#include <sporks/bot.h>
#include <sporks/modules.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/database.h>

/**
 * Provides a role on the home server when someone votes for the bot on various websites such as top.gg
 */

class VotingModule : public Module
{
public:
	VotingModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml)
	{
		ml->Attach({ I_OnPresenceUpdate }, this);
	}

	virtual ~VotingModule()
	{
	}

	virtual std::string GetVersion()
	{
		/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
		std::string version = "$ModVer 5$";
		return "1.0." + version.substr(8,version.length() - 9);
	}

	virtual std::string GetDescription()
	{
		return "Awards roles in exchange for votes";
	}

	virtual bool OnPresenceUpdate()
	{
		db::resultset rs_votes = db::query("SELECT id, snowflake_id, UNIX_TIMESTAMP(vote_time) AS vote_time, origin, rolegiven FROM infobot_votes", {});
		aegis::guild* home = bot->core.find_guild(from_string<int64_t>(Bot::GetConfig("home"), std::dec));
		if (home) {
			/* Process removals first */
			for (auto vote = rs_votes.begin(); vote != rs_votes.end(); ++vote) {
				int64_t member_id = from_string<int64_t>((*vote)["snowflake_id"], std::dec);
				aegis::user* user = bot->core.find_user(member_id);
				if (user) {
					if ((*vote)["rolegiven"] == "1") {
						/* Role was already given, take away the role and remove the vote IF the date is too far in the past.
						 * Votes last 24 hours.
						 */
						uint64_t role_timestamp = from_string<uint64_t>((*vote)["vote_time"], std::dec);
						if (time(NULL) - role_timestamp > 86400) {
							db::query("DELETE FROM infobot_votes WHERE id = ?", {(*vote)["id"]});
							home->remove_guild_member_role(member_id, from_string<int64_t>(Bot::GetConfig("vote_role"), std::dec));
							bot->core.log->info("Removing vanity role from {}", member_id);
						}
					}
				}
			}
			/* Now additions, so that if they've re-voted, it doesnt remove it */
			for (auto vote = rs_votes.begin(); vote != rs_votes.end(); ++vote) {
				int64_t member_id = from_string<int64_t>((*vote)["snowflake_id"], std::dec);
				aegis::user* user = bot->core.find_user(member_id);
				if (user) {
					if ((*vote)["rolegiven"] == "0") {
						/* Role not yet given, give the role and set rolegiven to 1 */
						bot->core.log->info("Adding vanity role to {}", member_id);
						home->add_guild_member_role(member_id, from_string<int64_t>(Bot::GetConfig("vote_role"), std::dec));
						db::query("UPDATE infobot_votes SET rolegiven = 1 WHERE snowflake_id = ?", {(*vote)["snowflake_id"]});
					}
				}
			}
		}
		return true;
	}	
};

ENTRYPOINT(VotingModule);

