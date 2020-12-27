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

#include <aegis.hpp>
#include <string>
#include <sstream>
#include <queue>
#include "webrequest.h"
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "httplib.h"

asio::io_context * _io_context = nullptr;
std::string apikey;
std::mutex faflock;
std::thread* ft = nullptr;


std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body = "");
std::string fetch_page(const std::string &_endpoint, const std::string &body = "");

/* Represents a fire-and-forget REST request. A fire-and-forget request can be executed in the future
 * and expects no result. It goes into a queue and will be executed in at least 100ms time. They can be
 * guaranteed to be executed in-order.
 */
struct fire_and_forget_t {
	std::string host;
	std::string path;
	std::string body;
};

/* A queue of fire-and-forget requests waiting to be executed. */
std::queue<fire_and_forget_t> faf;

void fireandforget()
{
	while (1) {
		bool something = false;
		fire_and_forget_t f;
		{
			std::lock_guard<std::mutex> fafguard(faflock);
			if (!faf.empty()) {
				f = faf.front();
				faf.pop();
				something = true;
			}
		}
		if (something) {
			web_request(f.host, f.path, f.body);
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

/* Initialisation function */
void set_io_context(asio::io_context* ioc, const std::string &_apikey)
{
	_io_context = ioc;
	apikey = _apikey;
	ft = new std::thread(&fireandforget);
}

/* Encodes a url parameter similar to php urlencode() */
std::string url_encode(const std::string &value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;

	for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		std::string::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << std::uppercase;
		escaped << '%' << std::setw(2) << int((unsigned char) c);
		escaped << std::nouppercase;
	}

	return escaped.str();
}

/* Make a REST web request (either GET or POST) to a HTTP server */
std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body)
{
	try
	{
		httplib::Client cli(_host.c_str(), 80);
		httplib::Headers headers = {
			{"X-API-Auth", apikey}
		};
		cli.set_default_headers(headers);

		std::string rv;
		int code = 0;

		if (_body.empty()) {
			if (auto res = cli.Get(_path.c_str())) {
				if (res->status == 200) {
					rv = res->body;
				}
			}
		}
		else {
			if (auto res = cli.Post(_path.c_str(), _body.c_str(), "text/plain")) {
				if (res->status == 200) {
					rv = res->body;
				}
				//std::cout << "status: " << res->status << "\n";
			} else {
				//std::cout << "error: " << res.error() << "\n";
			}
		}
			
		return rv;
	}
	catch (std::exception& e)
	{
		std::cout << "Exception: " << e.what() << "\n";
	}
	return "";
}

/* Execute a TriviaBot API call at a later time, putting it into the fire-and-forget queue */
void later(const std::string &_path, const std::string &_body)
{
	std::lock_guard<std::mutex> fafguard(faflock);
	faf.push({BACKEND_HOST, fmt::format("/api/{}", _path), _body});
}

/* Fetch the contents of a page from the TriviaBot API immediately */
std::string fetch_page(const std::string &_endpoint, const std::string &body)
{
	return web_request(BACKEND_HOST, fmt::format("/api/{}", _endpoint), body);
}

/* Convert a newline separated list to a vector of strings */
std::vector<std::string> to_list(const std::string &str)
{
	std::stringstream content(str);
	std::vector<std::string> response;
	std::string line;
	while (std::getline(content, line)) {
		response.push_back(line);
	}
	return response;
}

/* Store details about a user to the database. Executes when the user successfully answers a question, or when they issue a valid command */
void cache_user(const aegis::user *_user, const aegis::guild *_guild, const aegis::user::guild_info* gi)
{
	/*std::unordered_map<aegis::snowflake, aegis::gateway::objects::role> roles = _guild->get_roles();
	std::stringstream body;
	// Serialise the guilds roles into a space separated list for POST to the cache endpoint
	for (auto n = roles.begin(); n != roles.end(); ++n) {
		body << std::hex << n->second.color << std::dec << " " << n->second.id << " " << n->second._permission << " " << n->second.position << " ";
	       	body << (n->second.hoist ? 1 : 0) << " " << (n->second.managed ? 1 : 0) << " " << (n->second.mentionable ? 1 : 0) << " ";
		body << n->second.name << "\n";
	}
	std::string member_roles;
	for (auto r = gi->roles.begin();r != gi->roles.end(); ++r) {
		member_roles.append(std::to_string(r->get())).append(" ");
	}
	member_roles = trim(member_roles);
	later(fmt::format("?opt=cache&user_id={}&username={}&discrim={}&icon={}&guild_id={}&guild_name={}&guild_icon={}&owner_id={}&roles={}", _user->get_id().get(), url_encode(_user->get_username()), _user->get_discriminator(), url_encode(_user->get_avatar()),
_guild->get_id().get(), url_encode(_guild->get_name()), url_encode(_guild->get_icon()), _guild->get_owner().get(), url_encode(member_roles)), body.str());

	Replaced with direct db query for perforamance increase - 27Dec20
	*/


	db::query("START TRANSACTION", {});

	db::query("INSERT INTO trivia_user_cache (snowflake_id, username, discriminator, icon) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE username = '?', discriminator = '?', icon = '?'",
			{_user->get_id().get(), _user->get_username(), _user->get_discriminator(), _user->get_avatar(), _user->get_username(), _user->get_discriminator(), _user->get_avatar()});

	db::query("INSERT INTO trivia_guild_cache (snowflake_id, name, icon, owner_id) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE name = '?', icon = '?', owner_id = '?', kicked = 0",
			{_guild->get_id().get(), _guild->get_name(), _guild->get_icon(),  _guild->get_owner().get(), _guild->get_name(), _guild->get_icon(),  _guild->get_owner().get()});

	std::string member_roles;
	std::string comma_roles;
	for (auto r = gi->roles.begin();r != gi->roles.end(); ++r) {
		member_roles.append(std::to_string(r->get())).append(" ");
	}
	member_roles = trim(member_roles);
	db::query("INSERT INTO trivia_guild_membership (guild_id, user_id, roles) VALUES('?', '?', '?') ON DUPLICATE KEY UPDATE roles = '?'",
			{_guild->get_id().get(), _user->get_id().get(), member_roles, member_roles});

	std::unordered_map<aegis::snowflake, aegis::gateway::objects::role> roles = _guild->get_roles();
	for (auto n = roles.begin(); n != roles.end(); ++n) {
		comma_roles.append(std::to_string(n->second.id)).append(",");
		db::query("INSERT INTO trivia_role_cache (id, guild_id, colour, permissions, position, hoist, managed, mentionable, name) VALUES('?', '?', '?', '?', '?', '?', '?', '?', '?') ON DUPLICATE KEY UPDATE colour = '?', permissions = '?', position = '?', hoist = '?', managed = '?', mentionable = '?', name = '?'",
			   {
				   n->second.id, _guild->get_id().get(), n->second.color, n->second._permission, n->second.position, (n->second.hoist ? 1 : 0), (n->second.managed ? 1 : 0), (n->second.mentionable ? 1 : 0), n->second.name,
				   n->second.color, n->second._permission, n->second.position, (n->second.hoist ? 1 : 0), (n->second.managed ? 1 : 0), (n->second.mentionable ? 1 : 0), n->second.name,
			   });
	}
	comma_roles = trim(comma_roles.substr(0, comma_roles.length() - 1));
	/* Delete any that have been deleted from discord */
	db::query("DELETE FROM trivia_role_cache WHERE guild_id = ? AND id NOT IN (" + comma_roles + ")", {_guild->get_id().get()});
	db::query("COMMIT", {});
}

/* Fetch a question by ID from the TriviaBot API */
std::vector<std::string> fetch_question(int64_t id, int64_t guild_id)
{
	/* Requires full unicode support for shuffled hints (currently only API-side). Can't move this to a direct query yet */
	return to_list(fetch_page(fmt::format("?id={}&guild_id={}", id, guild_id)));
}

/* Get a list of command names entirely handled via REST */
std::vector<std::string> get_api_command_names()
{
	return to_list(fetch_page("?opt=listcommands"));
}

/* Fetch a shuffled list of question IDs from the API, which is dependant upon some statistics for the guild and the category selected. */
std::vector<std::string> fetch_shuffle_list(int64_t guild_id, const std::string &category)
{
	if (category.empty()) {
		return to_list(fetch_page(fmt::format("?opt=shuffle&guild_id={}",guild_id)));
	} else {
		return to_list(fetch_page(fmt::format("?opt=shuffle&guild_id={}&category={}",guild_id, url_encode(category))));
	}
}

/* Fetch an insane round by ID from the API */
std::vector<std::string> fetch_insane_round(int64_t &question_id, int64_t guild_id)
{
	std::vector<std::string> list = to_list(fetch_page(fmt::format("?opt=newinsane&guild_id={}", guild_id)));
	if (list.size() >= 3) {
		question_id = from_string<int64_t>(list[0], std::dec);
		list.erase(list.begin());

	} else {
		question_id = 0;
	}
	return list;
}

/* Return a list of number to text strings, e.g. "2 = number of teeth goofy has". Returns all language variants. */
json get_num_strs()
{
	return json::parse(fetch_page("?opt=numstrs"));
}

/* Send a DM hint to a user. Because of promise crashing issues in aegis, we don't do this in-bot and we farm it out to API */
void send_hint(int64_t snowflake_id, const std::string &hint, uint32_t remaining)
{
	later(fmt::format("?opt=customhint&user_id={}&hint={}&remaining={}", snowflake_id, url_encode(hint), remaining), "");
}

/* Farm out a custom command to the API to be handled completely via a REST call. Some things are better done this way as the code is cleaner and more stable, e.g. the !rank/!globalrank command */
std::string custom_command(const std::string &command, const std::string &parameters, int64_t user_id, int64_t channel_id, int64_t guild_id)
{
	return fetch_page(fmt::format("?opt=command&user_id={}&channel_id={}&command={}&guild_id={}&parameters={}", user_id, channel_id, guild_id, url_encode(command), url_encode(parameters)));
}

/* Update the score only, for a user during insane round */
void update_score_only(int64_t snowflake_id, int64_t guild_id, int score, int64_t channel_id)
{
	//later(fmt::format("?opt=scoreonly&nick={}&score={}&guild_id={}", snowflake_id, score, guild_id), "");
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("INSERT INTO scores (name, guild_id, score, dayscore, weekscore, monthscore) VALUES('?', '?', '?', '?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, guild_id, score, score, score, score, score, score, score, score});
	db::query("INSERT INTO scores_lastgame (guild_id, user_id, score) VALUES('?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, snowflake_id, score, score});
	db::query("INSERT INTO insane_round_statistics (guild_id, channel_id, user_id, score) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, channel_id, snowflake_id, score, score});
}

/* Log the start of a game to the database via the API, used for resuming of games on crash or restart, plus the dashboard active games list */
void log_game_start(int64_t guild_id, int64_t channel_id, int64_t number_questions, bool quickfire, const std::string &channel_name, int64_t user_id, const std::vector<std::string> &questions, bool hintless)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	fetch_page(fmt::format("?opt=gamestart&guild_id={}&channel_id={}&questions={}&quickfire={}&user_id={}&channel_name={}&hostname={}&hintless={}", guild_id, channel_id, number_questions, quickfire, user_id, url_encode(channel_name), url_encode(hostname), hintless ? 1 : 0), json(questions).dump());
}

/* Log the end of a game, used for resuming games on crash or restart, plus the dashboard active games list */
void log_game_end(int64_t guild_id, int64_t channel_id)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	later(fmt::format("?opt=gameend&guild_id={}&channel_id={}&hostname={}", guild_id, channel_id, url_encode(hostname)), "");
}

/* Update current question of a game, used for resuming games on crash or restart, plus the dashboard active games list */
bool log_question_index(int64_t guild_id, int64_t channel_id, int32_t index, uint32_t streak, int64_t lastanswered, int32_t state)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	std::string r = trim(fetch_page(fmt::format("?opt=gameindex&guild_id={}&channel_id={}&index={}&hostname={}&streak={}&lastanswered={}&state={}", guild_id, channel_id, index, url_encode(hostname), streak, lastanswered, state)));
	return r == "1";

}

/* Update the score for a user and their team, during non-insane round */
int32_t update_score(int64_t snowflake_id, int64_t guild_id, time_t recordtime, int64_t id, int score)
{
	//return from_string<int32_t>(fetch_page(fmt::format("?opt=score&nick={}&recordtime={}&id={}&score={}&guild_id={}", snowflake_id, recordtime, id, score, guild_id)), std::dec);
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("UPDATE stats SET lastasked = now(), lastcorrect='?', record_time='?', timesasked = timesasked + 1 WHERE id = ?", {snowflake_id, recordtime, id});
	db::query("INSERT INTO scores (name, guild_id, score, dayscore, weekscore, monthscore) VALUES('?', '?', '?', '?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, guild_id, score, score, score, score, score, score, score, score});
	db::query("INSERT INTO scores_lastgame (guild_id, user_id, score) VALUES('?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, snowflake_id, score, score});

	db::resultset r = db::query("SELECT dayscore FROM scores WHERE name = '?' AND guild_id = '?'", {snowflake_id, guild_id});
	if (r.size()) {
		return from_string<int32_t>(r[0]["dayscore"], std::dec);
	} else {
		return 0;
	}
}

/* Return a list of active games from the API, used for resuming on crash or restart and by the dashboard, populated by log_game_start(), log_game_end() and log_question_index() */
json get_active(const std::string &hostname, int64_t cluster_id)
{
	std::string active = fetch_page(fmt::format("?opt=getactive&hostname={}&cluster={}", hostname, cluster_id));
	return json::parse(active);
}

/* Return a total count of questions from the database. The query can be expensive, avoid calling too frequently */
int32_t get_total_questions()
{
	//return from_string<int32_t>(fetch_page("?opt=total"), std::dec);
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT count(id) as total FROM questions", {});
	return from_string<int32_t>(r[0]["total"], std::dec);
}

/* Return the current top ten players for the day for a guild, plus their scores */
std::vector<std::string> get_top_ten(uint64_t guild_id)
{
	//return to_list(fetch_page(fmt::format("?opt=table&guild={}", guild_id)));
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	std::vector<std::string> result;
	db::resultset r = db::query("SELECT dayscore, name FROM scores WHERE guild_id = '?' and dayscore > 0 ORDER BY dayscore DESC limit 10", {guild_id});
	for (auto v = r.begin(); v != r.end(); ++v) {
		result.push_back((*v)["dayscore"] + " " + (*v)["name"]);
	}
	return result;
}

/* Return the current team name for a player, or an empty string */
std::string get_current_team(int64_t snowflake_id)
{
	//return fetch_page(fmt::format("?opt=currentteam&nick={}", snowflake_id));
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT team FROM team_membership WHERE nick = '?'", {snowflake_id});
	if (r.size()) {
		return r[0]["team"];
	} else {
		return "";
	}
}

/* Make a player leave the current team if they are in one. REMOVES THE RECORD of their individual score contribution! */
void leave_team(int64_t snowflake_id)
{
	//later(fmt::format("?opt=leaveteam&nick={}", snowflake_id), "");
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("DELETE FROM team_membership WHERE nick = '?'", {snowflake_id});
}

/* Make a player join a team */
bool join_team(int64_t snowflake_id, const std::string &team, int64_t channel_id)
{
	//std::string r = trim(fetch_page(fmt::format("?opt=jointeam&name={}&channel_id={}&nick={}", url_encode(team), channel_id, snowflake_id)));
	if (check_team_exists(team)) {
		std::string r = trim(fetch_page(fmt::format("?opt=setteam&nick={}&team={}&channel_id={}", snowflake_id, url_encode(team), channel_id)));
		if (r == "__OK__") {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

/* Update the streak for a player on a guild */
void change_streak(int64_t snowflake_id, int64_t guild_id, int score)
{
	// achievement check is here, can't make this direct db query yet
	later(fmt::format("?opt=changestreak&nick={}&score={}&guild_id={}", snowflake_id, score, guild_id), "");
}

/* Get the current streak details for a player on a guild, and the best streak for the guild at present */
streak_t get_streak(int64_t snowflake_id, int64_t guild_id)
{
	/*streak_t s;
	std::vector<std::string> data = to_list(ReplaceString(fetch_page(fmt::format("?opt=getstreak&nick={}&guild_id={}", snowflake_id, guild_id)), "/", "\n"));
	if (data.size() == 3) {
		s.personalbest = data[0].empty() ? 0 : from_string<int32_t>(data[0], std::dec);
		s.topstreaker = data[1].empty() ? 0 : from_string<int64_t>(data[1], std::dec);
		s.bigstreak = data[2].empty() ? 0 : from_string<int32_t>(data[2], std::dec);
	} else {
		// Failed to retrieve correctly formatted data
		s.personalbest = 0;
		s.topstreaker = 0;
		s.bigstreak = 99999999;
	}
	
	Replaced with direct db query for perforamance increase - 27Dec20
	*/
	streak_t s;
	db::resultset streak = db::query("SELECT nick, streak FROM streaks WHERE guild_id = '?' ORDER BY streak DESC LIMIT 1", {guild_id});
	db::resultset ss2 = db::query("SELECT streak FROM streaks WHERE nick='?' AND guild_id = '?'", {snowflake_id, guild_id});
	s.personalbest = 0;
	s.topstreaker = 0;
	s.bigstreak = 9999999;
	if (ss2.size() > 0) {
		s.personalbest = from_string<uint32_t>(ss2[0]["streak"], std::dec);
	}
	if (streak.size() > 0) {
		s.topstreaker = from_string<uint64_t>(streak[0]["nick"], std::dec);
		s.bigstreak = from_string<uint32_t>(streak[0]["streak"], std::dec);
	}
	return s;
}

/* Create a new team, censors the team name using neutrino API */
std::string create_new_team(const std::string &teamname)
{
	return trim(fetch_page(fmt::format("?opt=createteam&name={}", url_encode(teamname))));
}

/* Returns true if a team name exists */
bool check_team_exists(const std::string &team)
{
	//std::string r = trim(fetch_page(fmt::format("?opt=jointeam&name={}", url_encode(team))));
	//return (r != "__OK__");
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT name FROM teams WHERE name = '?'", {team});
	return (r.size());
}

/* Add points to a team */
void add_team_points(const std::string &team, int points, int64_t snowflake_id)
{
	//fetch_page(fmt::format("?opt=addteampoints&name={}&score={}&nick={}", url_encode(team), points, snowflake_id));
	//
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("UPDATE teams SET score = score + ? WHERE name = '?'", {points, team});
	if (snowflake_id) {
		db::query("UPDATE team_membership SET points_contributed = points_contributed + ? WHERE nick = '?'", {points, snowflake_id});
	}

}

/* Get the points of a team */
int32_t get_team_points(const std::string &team)
{
	//return from_string<int32_t>(fetch_page(fmt::format("?opt=getteampoints&name={}", url_encode(team))), std::dec);
	//
	// Replaced with direct db query for performance increase - 27Dec20
	db::resultset r = db::query("SELECT score FROM teams WHERE name = '?'", {team});
	if (r.size()) {
		return from_string<int32_t>(r[0]["score"], std::dec);
	} else {
		return 0;
	}
}

