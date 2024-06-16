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

#include <sporks/regex.h>
#include <string>
#include <streambuf>
#include <sporks/stringops.h>
#include "trivia.h"
#include "commands.h"
#include <fmt/format.h>


command_servertime_t::command_servertime_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options) : command_t(_creator, _base_command, adm, descr, options) { }

void command_servertime_t::call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user)
{
	time_t now_time = time(nullptr);
	time_t seconds_until_reset = 86400 - (now_time % 86400);
	time_t reset_time = now_time + seconds_until_reset;
	std::string sf = fmt::format("{:02d}:{:02d}:{:02d}", seconds_until_reset / 60 / 60 % 24, seconds_until_reset / 60 % 60, seconds_until_reset % 60);

	creator->SimpleEmbed(cmd.interaction_token, cmd.command_id, settings, "",
		fmt::format(_("TIME_IS", settings), dpp::utility::current_date_time()) + "\n" + fmt::format(_("TIME_RESET", settings), sf),
	cmd.channel_id, _("SERVERTIME", settings), "", "https://triviabot.co.uk/images/triviabot_tl_icon.png");
	creator->CacheUser(cmd.author_id, cmd.user, cmd.member, cmd.channel_id);
}

/*
echo json_encode([
        "title" => __("SERVERTIME", $prefs),
        "type" => "rich",
        "url" => "https://triviabot.co.uk",
        "description" => sprintf(__("TIME_IS", $prefs), date('H:i:s')) . "\n" . sprintf(__("TIME_RESET", $prefs), $sf),
        "color" => $prefs->embedcolour,
        "footer" => [
                "text" => __("POWERED_BY", $prefs),
                "icon_url" => "https://triviabot.co.uk/images/triviabot_tl_icon.png"
        ],
        "thumbnail" => [
                "url"=>"https://triviabot.co.uk/images/triviabot_tl_icon.png"
        ]
]);
*/