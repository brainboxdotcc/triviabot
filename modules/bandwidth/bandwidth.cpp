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

#include <fmt/format.h>
#include <sporks/bot.h>
#include <sporks/modules.h>
#include <string>
#include <sporks/database.h>

/**
 * Updates presence and counters on a schedule
 */

class BandwidthModule : public Module
{
	uint64_t last_bandwidth_websocket;
	uint64_t half;
public:
	BandwidthModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), last_bandwidth_websocket(0), half(0)
	{
		ml->Attach({ I_OnPresenceUpdate }, this);
	}

	virtual ~BandwidthModule()
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
		return "Maintains a log of bandwidth use";
	}

	virtual bool OnPresenceUpdate()
	{
		half++;
		/* This method is called every 30 seconds, so we want every alternating call to get a 60 second timer */
		if (half % 2 == 0) {
			uint64_t bandwidth_websocket = 0;
			auto& shards = bot->core->get_shards();
			for (auto i = shards.begin(); i != shards.end(); ++i) {
				dpp::discord_client* shard = i->second;
				bandwidth_websocket += shard->get_bytes_in() + shard->get_bytes_out();
			}
			uint64_t bandwidth_last_60_seconds = bandwidth_websocket - last_bandwidth_websocket;
			last_bandwidth_websocket = bandwidth_websocket;
	
			/* Divide the websocket bandwidth by 60 to get bytes per second, then by 1024 to get kbps */
			double kbps_in = ((double)bandwidth_last_60_seconds / 60.0 / 1024.0);
	
			db::backgroundquery("INSERT INTO infobot_bandwidth (kbps_in) VALUES('?')", {fmt::format("{:.4f}", kbps_in)});
		}
		return true;
	}
};

ENTRYPOINT(BandwidthModule);

