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
#include <dpp/nlohmann/json.hpp>
#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include "trivia.h"
#include "commands.h"

using json = nlohmann::json;

int64_t GetRSS() {
	int64_t ram = 0;
	std::ifstream self_status("/proc/self/status");
	while (self_status) {
		std::string token;
		self_status >> token;
		if (token == "VmRSS:") {
			self_status >> ram;
			break;
		}
	}
	self_status.close();
	return ram * 1024;
}

command_info_t::command_info_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_info_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	std::stringstream s;
	dpp::utility::uptime ut = creator->GetBot()->core->uptime();

	uint64_t servers = creator->GetGuildTotal();
	uint64_t users = creator->GetMemberTotal();

	char startstr[256];
	tm _tm;
	gmtime_r(&creator->startup, &_tm);
	strftime(startstr, 255, "%x, %I:%M%p", &_tm);

	uint64_t shard = (cmd.guild_id >> 22) % from_string<uint32_t>(Bot::GetConfig("shardcount"), std::dec);

	const statusfield status_fields[] = {
		statusfield(_("ACTIVEGAMES", settings), Comma(creator->GetActiveGames())),
		statusfield(_("TOTALSERVERS", settings), Comma(servers)),
		statusfield(_("CONNSINCE", settings), startstr),
		statusfield(_("ONLINEUSERS", settings), Comma(users)),
		statusfield(_("RSS", settings), Comma(GetRSS() / 1024 / 1024) + "M"),
		statusfield(_("UPTIME", settings), ut.to_string()),
		statusfield(_("CLUSTER", settings), Comma(creator->GetBot()->GetClusterID()) + "/" + Comma(creator->GetBot()->GetMaxClusters())),
		statusfield(_("SHARDS", settings), Comma(shard) + "/" + Bot::GetConfig("shardcount")),
		statusfield(_("MEMBERINTENT", settings), _((creator->GetBot()->HasMemberIntents() ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("MESSAGEINTENT", settings), _(((creator->GetBot()->core->intents & dpp::i_message_content) ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("TESTMODE", settings), _((creator->GetBot()->IsTestMode() ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("DEVMODE", settings), _((creator->GetBot()->IsDevMode() ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("MYPREFIX", settings), "`" + creator->escape_json(settings.prefix) + "`"),
		statusfield(_("BOTVER", settings), std::string(creator->GetVersion())),
		statusfield(_("LIBVER", settings), "<:DPP1:847152435399360583><:DPP2:847152435343523881> [" + std::string(DPP_VERSION_TEXT) + "](https://dpp.dev/)"),
		statusfield("", "")
	};

	s << R"({"title":")" << creator->GetBot()->user.username << " " << _("INFO", settings);
	s << R"(","thumbnail":{"url":"https:\/\/triviabot.co.uk\/images\/triviabot_tl_icon.png"},)";
	s << "\"color\":" << settings.embedcolour << R"(,"url":"https:\/\/triviabot.co.uk\/\/",)";
	s << R"("footer":{"link":"https:\/\/triviabot.co.uk\/","text":")" << _("POWERED_BY", settings) << R"(","icon_url":"https:\/\/triviabot.co.uk\/images\/triviabot_tl_icon.png"},"fields":[)";
	for (int i = 0; status_fields[i].name != ""; ++i) {
		s << "{\"name\":\"" << status_fields[i].name << R"(","value":")" << status_fields[i].value << R"(", "inline": )" << (i != 14 ? "true" : "false") << "}";
		if (status_fields[i + 1].name != "") {
			s << ",";
		}
	}
	s << "],\"description\":\"" << (settings.premium ? _("YAYPREMIUM", settings) : "") << "\"}";

	creator->ProcessEmbed(cmd.interaction_token, cmd.command_id, settings, s.str(), cmd.channel_id);
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}

