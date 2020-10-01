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
#include "state.h"
#include "trivia.h"
#include "webrequest.h"
#include "piglatin.h"

in_cmd::in_cmd(const std::string &m, int64_t author, int64_t channel, int64_t guild, bool mention) : msg(m), author_id(author), channel_id(channel), guild_id(guild), mentions_bot(mention)
{
}

void TriviaModule::handle_command(const in_cmd &cmd) {

	state_t* state = GetState(cmd.channel_id);

	if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == cmd.channel_id) {

		std::stringstream tokens(cmd.msg);
		std::string base_command;
		std::string username;
		tokens >> base_command;

		aegis::channel* c = bot->core.find_channel(cmd.channel_id);
		aegis::user* user = bot->core.find_user(cmd.author_id);
		if (user) {
			username = user->get_username();
		}

		guild_settings_t settings = GetGuildSettings(cmd.guild_id);

		/* Check for moderator status - first check if owner */
		aegis::guild* g = bot->core.find_guild(cmd.guild_id);
		bool moderator = (g && g->get_owner() == cmd.author_id);
		/* Now iterate the list of moderator roles from settings */
		if (!moderator) {
			for (auto x = settings.moderator_roles.begin(); x != settings.moderator_roles.end(); ++x) {
				if (c->get_guild().member_has_role(cmd.author_id, *x)) {
					moderator = true;
					break;
				}
			}
		}

		base_command = lowercase(base_command);

		/* Support for old-style commands e.g. !trivia start instead of !start */
		if (base_command == "trivia") {
			tokens >> base_command;
			base_command = lowercase(base_command);
		}

		if (base_command == "qf") {
			base_command = "quickfire";
		}

		if (base_command == "start" || base_command == "quickfire" || base_command == "trivia" || base_command == "fstart") {

			int32_t questions;
			std::string str_q;
			tokens >> str_q;
			if (str_q.empty()) {
				questions = 10;
			} else {
				questions = from_string<int32_t>(str_q, std::dec);
			}

			bool quickfire = (base_command == "quickfire");
			bool resumed = false;

			json document;
			std::ifstream configfile("../config.json");
			configfile >> document;
			json shitlist = document["shitlist"];
			aegis::channel* c = bot->core.find_channel(cmd.channel_id);
			for (auto entry = shitlist.begin(); entry != shitlist.end(); ++entry) {
				int64_t sl_guild_id = from_string<int64_t>(entry->get<std::string>(), std::dec);
						if (cmd.channel_id == sl_guild_id) {
					SimpleEmbed(":warning:", fmt::format(_("SHITLISTED", settings), username, bot->user.id.get()), cmd.channel_id);
					return;
				}
			}

			if (!settings.premium) {
				std::lock_guard<std::mutex> user_cache_lock(states_mutex);
				for (auto j = states.begin(); j != states.end(); ++j) {
					if (j->second->guild_id == c->get_guild().get_id() && j->second->gamestate != TRIV_END) {
						aegis::channel* active_channel = bot->core.find_channel(j->second->channel_id);
						if (active_channel) {
							EmbedWithFields(_("NOWAY", settings), {{_("ALREADYACTIVE", settings), fmt::format(_("CHANNELREF", settings), active_channel->get_id().get()), false},
									{_("GETPREMIUM", settings), _("PREMDETAIL1", settings), false}}, cmd.channel_id);
							return;
						}
					}
				}
			}

			if (!state) {
				int32_t max_quickfire = (settings.premium ? 200 : 15);
				if ((!quickfire && (questions < 5 || questions > 200)) || (quickfire && (questions < 5 || questions > max_quickfire))) {
					if (quickfire) {
						if (questions > max_quickfire && !settings.premium) {
							EmbedWithFields(_("MAX15", settings), {{_("GETPREMIUM", settings), _("PREMDETAIL2", settings), false}}, cmd.channel_id);
						} else {
							SimpleEmbed(":warning:", fmt::format(_("MAX15DETAIL", settings), username, max_quickfire), cmd.channel_id);
						}
					} else {
						SimpleEmbed(":warning:", fmt::format(_("MAX200", settings), username), cmd.channel_id);
					}
					return;
				}

				std::vector<std::string> sl = fetch_shuffle_list(c->get_guild().get_id());
				if (sl.size() < 50) {
					SimpleEmbed(":warning:", fmt::format(_("SPOOPY2", settings), username), c->get_id().get(), _("BROKED", settings));
					return;
				} else  {
					state = new state_t(this);
					state->start_time = time(NULL);
					state->shuffle_list = sl;
					state->gamestate = TRIV_ASK_QUESTION;
					state->numquestions = questions + 1;
					state->streak = 1;
					state->last_to_answer = 0;
					state->round = 1;
					state->interval = (quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL);
					state->channel_id = cmd.channel_id;
					state->curr_qid = 0;
					state->curr_answer = "";
					state->guild_id = cmd.guild_id;
					bot->core.log->info("Started game on guild {}, channel {}, {} questions [{}]", cmd.guild_id, cmd.channel_id, questions, quickfire ? "quickfire" : "normal");
					EmbedWithFields(fmt::format(_("NEWROUND", settings), (quickfire ? "**QUICKFIRE** " : ""), (resumed ? _("RESUMED", settings) : _("STARTED", settings)), (resumed ? _("ABOTADMIN", settings) : username)), {{_("QUESTION", settings), fmt::format("{}", questions), false}, {_("GETREADY", settings), _("FIRSTCOMING", settings), false}, {_("HOWPLAY", settings), _("INSTRUCTIONS", settings)}}, cmd.channel_id);
					{
						std::lock_guard<std::mutex> user_cache_lock(states_mutex);
						states[cmd.channel_id] = state;
						state->timer = new std::thread(&state_t::tick, state);
					}

					CacheUser(cmd.author_id, cmd.channel_id);
					log_game_start(state->guild_id, state->channel_id, questions, quickfire, c->get_name(), cmd.author_id, state->shuffle_list);
					return;
				}
			} else {
				SimpleEmbed(":warning:", fmt::format(_("ALREADYRUN", settings), username), cmd.channel_id);
				return;
			}
		} else if (base_command == "stop") {
			if (state) {
				if (settings.only_mods_stop) {
					if (!moderator) {
						SimpleEmbed(":warning:", fmt::format(_("CANTSTOPMEIMTHEGINGERBREADMAN", settings), username), cmd.channel_id);
						return;
					}
				}
				SimpleEmbed(":octagonal_sign:", fmt::format(_("STOPOK", settings), username), cmd.channel_id);
				{
					std::lock_guard<std::mutex> user_cache_lock(states_mutex);
					auto i = states.find(cmd.channel_id);
					if (i != states.end()) {
						i->second->terminating = true;
					} else {
						bot->core.log->error("Channel deleted while game stopping on this channel, cid={}", cmd.channel_id);
					}
					state = nullptr;
				}
				CacheUser(cmd.author_id, cmd.channel_id);
				log_game_end(cmd.guild_id, cmd.channel_id);
			} else {
				SimpleEmbed(":warning:", fmt::format(_("NOTRIVIA", settings), username), cmd.channel_id);
			}
			return;
		} else if (base_command == "vote") {
			SimpleEmbed(":white_check_mark:", fmt::format(fmt::format("{}\n{}", _("PRIVHINT", settings), _("VOTEAD", settings)), bot->user.id.get(), settings.prefix), cmd.channel_id);
			CacheUser(cmd.author_id, cmd.channel_id);
		} else if (base_command == "votehint" || base_command == "vh") {
			if (state) {
				if ((state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) && (state->round % 10) != 0 && state->curr_answer != "") {
					db::resultset rs = db::query("SELECT *,(unix_timestamp(vote_time) + 43200 - unix_timestamp()) as remaining FROM infobot_votes WHERE snowflake_id = ? AND now() < vote_time + interval 12 hour", {cmd.author_id});
					if (rs.size() == 0) {
						SimpleEmbed("<:wc_rs:667695516737470494>", fmt::format(fmt::format("{}\n{}", _("NOTVOTED", settings), _("VOTEAD", settings)), bot->user.id.get(), settings.prefix), cmd.channel_id);
						return;
					} else {
						int64_t remaining_hints = from_string<int64_t>(rs[0]["dm_hints"], std::dec);
						int32_t secs = from_string<int32_t>(rs[0]["remaining"], std::dec);
						int32_t mins = secs / 60 % 60;
						float hours = floor(secs / 60 / 60);
						if (remaining_hints < 1) {
							SimpleEmbed(":warning:", fmt::format(fmt::format("{}\n{}", fmt::format(_("NOMOREHINTS", settings), username), _("VOTEAD", settings)), bot->user.id.get(), hours, mins), cmd.channel_id);
						} else {
							remaining_hints--;
							if (remaining_hints > 0) {
								SimpleEmbed(":white_check_mark:", fmt::format(_("VH1", settings), username, remaining_hints, hours, mins), cmd.channel_id);
							} else {
								SimpleEmbed(":white_check_mark:", fmt::format(_("VH2", settings), username, hours, mins), cmd.channel_id);
							}
							std::string personal_hint = state->curr_answer;
							personal_hint = lowercase(personal_hint);
							personal_hint[0] = '#';
							personal_hint[personal_hint.length() - 1] = '#';
							personal_hint = ReplaceString(personal_hint, " ", "#");
							// Get the API to do this, because DMs in aegis are unreliable right now.
							send_hint(cmd.author_id, personal_hint, remaining_hints);
							db::query("UPDATE infobot_votes SET dm_hints = ? WHERE snowflake_id = ?", {remaining_hints, cmd.author_id});
							CacheUser(cmd.author_id, cmd.channel_id);

							return;
						}
					}
				} else {
					SimpleEmbed(":warning:", fmt::format(_("WAITABIT", settings), username), cmd.channel_id);
					return;
				}
			} else {
				SimpleEmbed(":warning:", fmt::format(fmt::format("{}\n{}", fmt::format(_("NOROUND", settings), username), _("VOTEAD", settings)), bot->user.id.get()), cmd.channel_id);
				return;
			}
		} else if (base_command == "stats") {
			show_stats(cmd.guild_id, cmd.channel_id);
			CacheUser(cmd.author_id, cmd.channel_id);
		} else if (base_command == "info") {
			std::stringstream s;
			time_t diff = bot->core.uptime() / 1000;
			int seconds = diff % 60;
			diff /= 60;
			int minutes = diff % 60;
			diff /= 60;
			int hours = diff % 24;
			diff /= 24;
			int days = diff;

			/* TODO: Make these cluster-safe */
			int64_t servers = bot->core.get_guild_count();
			int64_t users = bot->core.get_member_count();

			char uptime[32];
			snprintf(uptime, 32, "%d day%s, %02d:%02d:%02d", days, (days != 1 ? "s" : ""), hours, minutes, seconds);
			char startstr[256];
			tm _tm;
			gmtime_r(&startup, &_tm);
			strftime(startstr, 255, "%x, %I:%M%p", &_tm);

			const statusfield statusfields[] = {
				statusfield(_("ACTIVEGAMES", settings), Comma(GetActiveGames())),
				statusfield(_("TOTALSERVERS", settings), Comma(servers)),
				statusfield(_("CONNSINCE", settings), startstr),
				statusfield(_("ONLINEUSERS", settings), Comma(users)),
				statusfield(_("UPTIME", settings), std::string(uptime)),
				statusfield(_("SHARDS", settings), Comma(bot->core.shard_max_count)),
				statusfield(_("MEMBERINTENT", settings), _((bot->HasMemberIntents() ? "TICKYES" : "CROSSNO"), settings)),
				statusfield(_("TESTMODE", settings), _((bot->IsTestMode() ? "TICKYES" : "CROSSNO"), settings)),
				statusfield(_("DEVMODE", settings), _((bot->IsDevMode() ? "TICKYES" : "CROSSNO"), settings)),
				statusfield(_("MYPREFIX", settings), "``" + escape_json(settings.prefix) + "``"),
				statusfield(_("BOTVER", settings), std::string(GetVersion())),
				statusfield(_("LIBVER", settings), std::string(AEGIS_VERSION_TEXT)),
				statusfield("", "")
			};

			s << "{\"title\":\"" << bot->user.username << " " << _("INFO", settings);
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
				bot->core.log->error("Malformed json created when reporting info: {}", s.str());
			}
			if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == cmd.guild_id) {
				c->create_message_embed("", embed_json);
				bot->sent_messages++;
			}
			CacheUser(cmd.author_id, cmd.channel_id);
		} else if (base_command == "join") {
			std::string teamname;
			std::getline(tokens, teamname);
			teamname = trim(teamname);
			if (join_team(cmd.author_id, teamname, cmd.channel_id)) {
				SimpleEmbed(":busts_in_silhouette:", fmt::format(_("JOINED", settings), teamname, username), cmd.channel_id, _("CALLFORBACKUP", settings));
			} else {
				SimpleEmbed(":warning:", fmt::format(_("CANTJOIN", settings), username), cmd.channel_id);
			}
			CacheUser(cmd.author_id, cmd.channel_id);
		} else if (base_command == "create") {
			std::string newteamname;
			std::getline(tokens, newteamname);
			newteamname = trim(newteamname);
			std::string teamname = get_current_team(cmd.author_id);
			if (teamname.empty() || teamname == "!NOTEAM") {
				newteamname = create_new_team(newteamname);
				if (newteamname != "__NO__") {
					join_team(cmd.author_id, newteamname, cmd.channel_id);
					SimpleEmbed(":busts_in_silhouette:", fmt::format(_("CREATED", settings), newteamname, username), c->get_id().get(), _("ZELDAREFERENCE", settings));
				} else {
					SimpleEmbed(":warning:", fmt::format(_("CANTCREATE", settings), username), cmd.channel_id);
				}
			} else {
				SimpleEmbed(":warning:", fmt::format(_("ALREADYMEMBER", settings), username, teamname), cmd.channel_id);
			}
			CacheUser(cmd.author_id, cmd.channel_id);
		} else if (base_command == "leave") {
			std::string teamname = get_current_team(cmd.author_id);
			if (teamname.empty() || teamname == "!NOTEAM") {
				SimpleEmbed(":warning:", fmt::format(_("YOULONER", settings), username, settings.prefix), cmd.channel_id);
			} else {
				leave_team(cmd.author_id);
				SimpleEmbed(":busts_in_silhouette:", fmt::format(_("LEFTTEAM", settings), username, teamname), cmd.channel_id, _("COMEBACK", settings));
			}
			CacheUser(cmd.author_id, cmd.channel_id);
		} else if (base_command == "help") {
			std::string section;
			tokens >> section;
			GetHelp(section, cmd.channel_id, bot->user.username, bot->user.id.get(), username, cmd.author_id, settings);
			CacheUser(cmd.author_id, cmd.channel_id);
		} else {
			/* Custom commands handled completely by the API */
			bool command_exists = false;
			{
				std::lock_guard<std::mutex> cmd_list_lock(cmds_mutex);
				command_exists = (std::find(api_commands.begin(), api_commands.end(), trim(lowercase(base_command))) != api_commands.end());
			}
			if (command_exists) {
				bool can_execute = false;
				auto check = limits.find(cmd.channel_id);
				if (check == limits.end()) {
					can_execute = true;
					limits[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
				} else if (time(NULL) > check->second) {
					can_execute = true;
					limits[cmd.channel_id] = time(NULL) + PER_CHANNEL_RATE_LIMIT;
				}
				if (can_execute) {
					std::string rest;
					std::getline(tokens, rest);
					rest = trim(rest);
					CacheUser(cmd.author_id, cmd.channel_id);
					std::string reply = trim(custom_command(base_command, trim(rest), cmd.author_id, cmd.channel_id, cmd.guild_id));
					if (!reply.empty()) {
						ProcessEmbed(reply, cmd.channel_id);
					}
				} else {
					/* Display rate limit message */
					SimpleEmbed(":snail:", fmt::format(_("RATELIMITED", settings), PER_CHANNEL_RATE_LIMIT, base_command), cmd.channel_id, _("WOAHTHERE", settings));
					bot->core.log->debug("Command '{}' not sent to API, rate limited", trim(lowercase(base_command)));
				}
			} else {
				bot->core.log->debug("Command '{}' not known to API", trim(lowercase(base_command)));
			}
		}
		if (base_command == "reloadlang") {
			db::resultset rs = db::query("SELECT * FROM trivia_access WHERE user_id = ? AND enabled = 1", {cmd.author_id});
			if (rs.size() > 0) {
				std::ifstream langfile("../lang.json");
				json* newlang = new json();
				json* oldlang = this->lang;

				langfile >> *newlang;

				this->lang = newlang;
				sleep(1);
				delete oldlang;

				bot->core.log->info("Language strings count: {}", lang->size());
				SimpleEmbed(":flags:", fmt::format("Reloaded lang.json, **{}** containing language strings.", lang->size()), cmd.channel_id);
			} else {
				SimpleEmbed(":warning:", _("STAFF_ONLY", settings), cmd.channel_id);
			}
		}
	}
}

/**
 * Emit help using a json file in the help/ directory. Missing help files emit a generic error message.
 */
void TriviaModule::GetHelp(const std::string &section, int64_t channelID, const std::string &botusername, int64_t botid, const std::string &author, int64_t authorid, const guild_settings_t &settings)
{
	json embed_json;
	char timestamp[256];
	time_t timeval = time(NULL);
	aegis::channel* channel = bot->core.find_channel(channelID);
	int32_t colour = settings.embedcolour;

	if (!channel) {
		bot->core.log->error("Can't find channel {}!", channelID);
		return;
	}

	std::ifstream t("../help/" + settings.language + "/" + (section.empty() ? "basic" : section) + ".json");
	if (!t) {
		t = std::ifstream("../help/" + settings.language + "/error.json");
	}
	std::string json((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());

	tm _tm;
	gmtime_r(&timeval, &_tm);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &_tm);

	json = ReplaceString(json, ":section:" , section);
	json = ReplaceString(json, ":user:", botusername);
	json = ReplaceString(json, ":id:", std::to_string(botid));
	json = ReplaceString(json, ":author:", author);
	json = ReplaceString(json, ":ts:", timestamp);
	json = ReplaceString(json, ":color:", std::to_string(colour));

	try {
		embed_json = json::parse(json);
	}
	catch (const std::exception &e) {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
			channel->create_message(fmt::format(_("HERPDERP", settings), authorid));
			bot->sent_messages++;
		}
		bot->core.log->error("Malformed help file {}.json!", section);
		return;
	}

	if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
		channel->create_message_embed("", embed_json);
		bot->sent_messages++;
	}
}

