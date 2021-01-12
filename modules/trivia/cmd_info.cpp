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

#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include <sporks/database.h>
#include "trivia.h"
#include "webrequest.h"
#include "commands.h"

command_info_t::command_info_t(class TriviaModule* _creator, const std::string &_base_command) : command_t(_creator, _base_command) { }

void command_info_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, aegis::channel* c, aegis::user* user)
{
	std::stringstream s;
	time_t diff = creator->bot->core.uptime() / 1000;
	int seconds = diff % 60;
	diff /= 60;
	int minutes = diff % 60;
	diff /= 60;
	int hours = diff % 24;
	diff /= 24;
	int days = diff;

	/* TODO: Make these cluster-safe */
	int64_t servers = creator->GetGuildTotal();
	int64_t users = creator->GetMemberTotal();

	char uptime[32];
	snprintf(uptime, 32, "%d day%s, %02d:%02d:%02d", days, (days != 1 ? "s" : ""), hours, minutes, seconds);
	char startstr[256];
	tm _tm;
	gmtime_r(&creator->startup, &_tm);
	strftime(startstr, 255, "%x, %I:%M%p", &_tm);

	const statusfield statusfields[] = {
		statusfield(_("ACTIVEGAMES", settings), Comma(creator->GetActiveGames())),
		statusfield(_("TOTALSERVERS", settings), Comma(servers)),
		statusfield(_("CONNSINCE", settings), startstr),
		statusfield(_("ONLINEUSERS", settings), Comma(users)),
		statusfield(_("UPTIME", settings), std::string(uptime)),
		statusfield(_("CLUSTER", settings), Comma(creator->bot->GetClusterID())),
		statusfield(_("SHARDS", settings), Comma(creator->bot->core.shard_max_count)),
		statusfield(_("MEMBERINTENT", settings), _((creator->bot->HasMemberIntents() ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("TESTMODE", settings), _((creator->bot->IsTestMode() ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("DEVMODE", settings), _((creator->bot->IsDevMode() ? "TICKYES" : "CROSSNO"), settings)),
		statusfield(_("MYPREFIX", settings), "``" + creator->escape_json(settings.prefix) + "``"),
		statusfield(_("BOTVER", settings), std::string(creator->GetVersion())),
		statusfield(_("LIBVER", settings), std::string(AEGIS_VERSION_TEXT)),
		statusfield("", "")
	};

	s << "{\"title\":\"" << creator->bot->user.username << " " << _("INFO", settings);
	s << "\",\"color\":" << settings.embedcolour << ",\"url\":\"https:\\/\\/triviabot.co.uk\\/\\/\",";
	s << "\"footer\":{\"link\":\"https:\\/\\/triviabot.co.uk\\/\",\"text\":\"" << _("POWERED_BY", settings) << "\",\"icon_url\":\"https:\\/\\/triviabot.co.uk\\/images\\/triviabot_tl_icon.png\"},\"fields\":[";
	for (int i = 0; statusfields[i].name != ""; ++i) {
		s << "{\"name\":\"" <<  statusfields[i].name << "\",\"value\":\"" << statusfields[i].value << "\", \"inline\": true}";
		if (statusfields[i + 1].name != "") {
			s << ",";
		}
	}
	s << "],\"description\":\"" << (settings.premium ? _("YAYPREMIUM", settings) : "") << "\"}";

	json embed_json;
	try {
		embed_json = json::parse(s.str());
	}
	catch (const std::exception &e) {
		creator->bot->core.log->error("Malformed json created when reporting info: {}", s.str());
	}
	if (!creator->bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == cmd.guild_id) {
		c->create_message_embed("", embed_json);
		creator->bot->sent_messages++;
	}
	creator->CacheUser(cmd.author_id, cmd.channel_id);
}

