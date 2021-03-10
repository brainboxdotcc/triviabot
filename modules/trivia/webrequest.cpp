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
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <array>
#include <locale>
#include <iostream>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include "webrequest.h"
#include <sporks/stringops.h>
#include <sporks/database.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "trivia.h"
#include "wlower.h"


#define START_STATUS 100
#define END_STATUS 600
#define FIRE_AND_FORGET_QUEUES 10

asio::io_context * _io_context = nullptr;
Bot* bot = nullptr;
TriviaModule* module = nullptr;
std::string apikey;

std::mutex faflock[FIRE_AND_FORGET_QUEUES];
std::mutex fafindex;
std::mutex interfaceindex;
std::mutex statsmutex;

uint32_t faf_index = 0;
uint32_t interface_index = 0;

std::thread* ft[FIRE_AND_FORGET_QUEUES] = { nullptr };
std::thread* statdumper;

std::map<std::string, std::map<uint32_t, uint64_t> > statuscodes;
std::map<std::string, uint64_t> requests;
std::map<std::string, uint64_t> errors;

std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body = "");
std::string fetch_page(const std::string &_endpoint, const std::string &body = "");
std::vector<std::string> getinterfaces();

question_t::question_t(int64_t _id, const std::string &_question, const std::string &_answer, const std::string &_hint1, const std::string &_hint2, const std::string &_catname, time_t _lastasked, int32_t _timesasked,
	const std::string &_lastcorrect, time_t _record_time, const std::string &_shuffle1, const std::string &_shuffle2, const std::string &_question_image, const std::string &_answer_image) :
	id(_id), question(_question), answer(_answer), customhint1(_hint1), customhint2(_hint2), catname(_catname), lastasked(_lastasked), timesasked(_timesasked), lastcorrect(_lastcorrect), recordtime(_record_time),
	shuffle1(_shuffle1), shuffle2(_shuffle2), question_image(_question_image), answer_image(_answer_image)
{
}

question_t::question_t() : id(0), lastasked(0), timesasked(0), recordtime(0)
{
}

/* Represents a fire-and-forget REST request. A fire-and-forget request can be executed in the future
 * and expects no result. It goes into a queue and will be executed in at least 100ms time. No guarantee
 * of in-order execution if theyre submitted within the same 100ms.
 *
 * There are ten fire and forget queues, each of which has a web request pushed into it, selected by
 * round robin.
 */
struct fire_and_forget_t {
	std::string host;
	std::string path;
	std::string body;
};

/* A queue of fire-and-forget requests waiting to be executed. */
std::queue<fire_and_forget_t> faf[FIRE_AND_FORGET_QUEUES];

void fireandforget(uint32_t queue_index)
{
	while (1) {
		bool something = false;
		fire_and_forget_t f;
		{
			std::lock_guard<std::mutex> fafguard(faflock[queue_index]);
			if (!faf[queue_index].empty()) {
				f = faf[queue_index].front();
				faf[queue_index].pop();
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

void statdump()
{
	while(1) {
		{
			std::lock_guard<std::mutex> sp(statsmutex);
			std::vector<std::string> inter = getinterfaces();
			for (auto i : inter) {
				uint64_t r = 0, e = 0;
				if (requests.find(i) != requests.end()) {
					r = requests[i];
				}
				if (errors.find(i) != errors.end()) {
					e = errors[i];
				}
				requests[i] = 0;
				errors[i] = 0;
				db::query("INSERT INTO http_requests (interface, hard_errors, requests) VALUES('?', ?, ?) ON DUPLICATE KEY UPDATE hard_errors = hard_errors + ?, requests = requests + ?", {i, e, r, e, r});
				if (statuscodes.find(i) != statuscodes.end()) {
					for (auto & codes : statuscodes[i]) {
						db::query("INSERT INTO http_status_codes (interface, status_code, requests) VALUES('?', ?, ?) ON DUPLICATE KEY UPDATE requests = requests + ?", {i, codes.first, codes.second, codes.second});
						statuscodes[i][codes.first] = 0;
					}
				}

			}
		}
		std::this_thread::sleep_for(std::chrono::seconds(60));
	}
}

/* Initialisation function */
void set_io_context(asio::io_context* ioc, const std::string &_apikey, Bot* _bot, TriviaModule* _module)
{
	_io_context = ioc;
	apikey = _apikey;
	bot = _bot;
	module = _module;
	for (uint32_t i = 0; i < FIRE_AND_FORGET_QUEUES; ++i) {
		ft[i] = new std::thread(&fireandforget, i);
	}
	statdumper = new std::thread(&statdump);
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

std::vector<std::string> getinterfaces()
{
	struct ifaddrs *ifaddr = NULL;
	char host[NI_MAXHOST];
	std::vector<std::string> rv;

	if (getifaddrs(&ifaddr) != -1) {
		for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
				if (!getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST)) {
					std::string ip = host;
					/* Exclude localhost and docker */
					if (ip != "127.0.0.1" && ip != "172.17.0.1") {
						rv.push_back(ip);
					}
				}
			}
		}
		freeifaddrs(ifaddr);
	}
	return rv;
}

std::string getinterface()
{
	int curindex;
	std::vector<std::string> interfaces = getinterfaces();
	{
		std::lock_guard<std::mutex> ii(interfaceindex);
		curindex = interface_index;
		if (++interface_index >= interfaces.size()) {
			interface_index = 0;
		}
		return interfaces[curindex];
	}
}

/* Make a REST web request (either GET or POST) to a HTTP server */
std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body)
{
	std::string iface = getinterface();
	try
	{
		httplib::Client cli(_host.c_str());
		cli.enable_server_certificate_verification(false);
		cli.set_interface(iface.c_str());
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
			if (auto res = cli.Post(_path.c_str(), _body.c_str(), "application/json")) {
				if (res->status == 200) {
					rv = res->body;
				}
				{
					std::lock_guard<std::mutex> sp(statsmutex);
					if (statuscodes.find(iface) == statuscodes.end()) {
						statuscodes[iface] = {};
					}
					if (statuscodes[iface].find(res->status) == statuscodes[iface].end()) {
						statuscodes[iface][res->status] = 1;
					} else {
						statuscodes[iface][res->status]++;
					}
					if (requests.find(iface) == requests.end()) {
						requests[iface] = 1;
					} else {
						requests[iface]++;
					}
				}
			} else {
				std::lock_guard<std::mutex> sp(statsmutex);
				if (errors.find(iface) == errors.end()) {
					errors[iface] = 1;
				} else {
					errors[iface]++;
				}
			}
		}
		
		return rv;
	}
	catch (std::exception& e)
	{
		std::cout << "Exception: " << e.what() << "\n";
		std::lock_guard<std::mutex> sp(statsmutex);
		if (errors.find(iface) == errors.end()) {
			errors[iface] = 1;
		} else {
			errors[iface]++;
		}
	}
	return "";
}

/* Execute a TriviaBot API call at a later time, putting it into the fire-and-forget queue */
void later(const std::string &_path, const std::string &_body)
{
	std::lock_guard<std::mutex> fi(fafindex);
	std::lock_guard<std::mutex> fafguard(faflock[faf_index]);
	if (bot->IsDevMode()) {
		faf[faf_index].push({BACKEND_HOST_DEV, fmt::format(BACKEND_PATH_DEV, _path), _body});
	} else {
		faf[faf_index].push({BACKEND_HOST_LIVE, fmt::format(BACKEND_PATH_LIVE, _path), _body});
	}
	faf_index++;
	if (faf_index > FIRE_AND_FORGET_QUEUES - 1) {
		faf_index = 0;
	}
}

/* Fetch the contents of a page from the TriviaBot API immediately */
std::string fetch_page(const std::string &_endpoint, const std::string &body)
{
	if (bot->IsDevMode()) {
		return web_request(BACKEND_HOST_DEV, fmt::format(BACKEND_PATH_DEV, _endpoint), body);
	} else {
		return web_request(BACKEND_HOST_LIVE, fmt::format(BACKEND_PATH_LIVE, _endpoint), body);
	}
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
	// Replaced with direct db query for perforamance increase - 27Dec20

	int64_t user_id = _user->get_id().get();
	int64_t guild_id = _guild->get_id().get();
	db::query("START TRANSACTION", {});

	db::query("INSERT INTO trivia_user_cache (snowflake_id, username, discriminator, icon) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE username = '?', discriminator = '?', icon = '?'",
			{user_id, _user->get_username(), _user->get_discriminator(), _user->get_avatar(), _user->get_username(), _user->get_discriminator(), _user->get_avatar()});

	db::query("INSERT INTO trivia_guild_cache (snowflake_id, name, icon, owner_id) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE name = '?', icon = '?', owner_id = '?', kicked = 0",
			{guild_id, _guild->get_name(), _guild->get_icon(),  _guild->get_owner().get(), _guild->get_name(), _guild->get_icon(),  _guild->get_owner().get()});

	std::string member_roles;
	std::string comma_roles;
	for (auto r = gi->roles.begin();r != gi->roles.end(); ++r) {
		member_roles.append(std::to_string(r->get())).append(" ");
	}
	member_roles = trim(member_roles);
	db::query("INSERT INTO trivia_guild_membership (guild_id, user_id, roles) VALUES('?', '?', '?') ON DUPLICATE KEY UPDATE roles = '?'",
			{guild_id, user_id, member_roles, member_roles});

	std::unordered_map<aegis::snowflake, aegis::gateway::objects::role> roles = _guild->get_roles();
	for (auto n = roles.begin(); n != roles.end(); ++n) {
		comma_roles.append(std::to_string(n->second.id)).append(",");
		db::query("INSERT INTO trivia_role_cache (id, guild_id, colour, permissions, position, hoist, managed, mentionable, name) VALUES('?', '?', '?', '?', '?', '?', '?', '?', '?') ON DUPLICATE KEY UPDATE colour = '?', permissions = '?', position = '?', hoist = '?', managed = '?', mentionable = '?', name = '?'",
			   {
				   n->second.id, guild_id, n->second.color, n->second._permission, n->second.position, (n->second.hoist ? 1 : 0), (n->second.managed ? 1 : 0), (n->second.mentionable ? 1 : 0), n->second.name,
				   n->second.color, n->second._permission, n->second.position, (n->second.hoist ? 1 : 0), (n->second.managed ? 1 : 0), (n->second.mentionable ? 1 : 0), n->second.name,
			   });
	}
	comma_roles = trim(comma_roles.substr(0, comma_roles.length() - 1));
	/* Delete any that have been deleted from discord */
	db::query("DELETE FROM trivia_role_cache WHERE guild_id = ? AND id NOT IN (" + comma_roles + ")", {guild_id});
	db::query("COMMIT", {});
}

/* Fetch a question by ID from the database */
question_t question_t::fetch(int64_t id, int64_t guild_id, const guild_settings_t &settings)
{
	// Replaced with direct db query for perforamance increase - 29Dec20
	db::query("INSERT INTO stats (id, lastasked, timesasked, lastcorrect, record_time) VALUES('?',UNIX_TIMESTAMP(),1,NULL,60000) ON DUPLICATE KEY UPDATE lastasked = UNIX_TIMESTAMP(), timesasked = timesasked + 1 ", {id});
	db::resultset question;
	if (settings.language == "en") {
		question = db::query("select questions.*, ans1.*, hin1.*, sta1.*, cat1.name as catname from questions left join hints as hin1 on questions.id=hin1.id left join answers as ans1 on questions.id=ans1.id left join stats as sta1 on questions.id=sta1.id left join categories as cat1 on questions.category=cat1.id where questions.id = ?", {id});
	} else {
		question = db::query("select questions.trans_" + settings.language + " as question, ans1.trans_" + settings.language + " as answer, hin1.trans1_" + settings.language + " as hint1, hin1.trans2_" + settings.language + " as hint2, question_img_url, answer_img_url, sta1.*, cat1.trans_" + settings.language + " as catname from questions left join hints as hin1 on questions.id=hin1.id left join answers as ans1 on questions.id=ans1.id left join stats as sta1 on questions.id=sta1.id left join categories as cat1 on questions.category=cat1.id where questions.id = ?", {id});
	}
	if (question.size() > 0) {
		db::query("UPDATE counters SET asked = asked + 1", {});
		return question_t(
			from_string<int64_t>(question[0]["id"], std::dec),
			homoglyph(question[0]["question"]),
			question[0]["answer"],
			question[0]["hint1"],
			question[0]["hint2"],
			question[0]["catname"],
			from_string<time_t>(question[0]["lastasked"], std::dec),
			from_string<int32_t>(question[0]["timesasked"], std::dec),
			question[0]["lastcorrect"],
			from_string<time_t>(question[0]["record_time"], std::dec),
			utf8shuffle(question[0]["answer"]),
			utf8shuffle(question[0]["answer"]),
			question[0]["question_img_url"],
			question[0]["answer_img_url"]
		);
	}
	return question_t();
}


std::vector<std::string> EnumCommandsDir()
{
	std::string path(getenv("HOME"));
	path += "/www/api/commands";
	std::vector<std::string> list;

	DIR *dir;
	struct dirent *ent;
	if ((dir = opendir (path.c_str())) != NULL) {
		while ((ent = readdir (dir)) != NULL) {
			std::string f = ent->d_name;
			f = ReplaceString(f, ".php", "");
			if (f != "." && f != "..") {
				list.push_back(f);
			}
		}
		closedir(dir);
	}
	return list;
}


/* Get a list of command names entirely handled via REST */
std::vector<std::string> get_api_command_names()
{
	return EnumCommandsDir();
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

/* Fetch a random insane round from the database, setting the question_id parameter and returning the question and all answers in a vector */
std::vector<std::string> fetch_insane_round(int64_t &question_id, int64_t guild_id, const guild_settings_t &settings)
{
	std::vector<std::string> list;
	db::resultset answers;
	db::resultset question;
	if (settings.language == "en") {
		question = db::query("select id,question from insane where deleted is null order by rand() limit 0,1", {});
		answers = db::query("select id,answer from insane_answers where question_id = ?", {question[0]["id"]});
	} else {
		question = db::query("select id,trans_" + settings.language + " AS question from insane where deleted is null order by rand() limit 0,1", {});
		answers = db::query("select id, trans_" + settings.language + " answer from insane_answers where question_id = ?", {question[0]["id"]});
	}

	question_id = from_string<int64_t>(question[0]["id"], std::dec);
	list.push_back(homoglyph(question[0]["question"]));

	for (auto a = answers.begin(); a != answers.end(); ++a) {
		list.push_back((*a)["answer"]);
	}
	list.push_back("***END***");

	return list;
}

/* Send a DM hint to a user. Because of promise crashing issues in aegis, we don't do this in-bot and we farm it out to API */
void send_hint(int64_t snowflake_id, const std::string &hint, uint32_t remaining)
{
	later(fmt::format("?opt=customhint&user_id={}&hint={}&remaining={}", snowflake_id, url_encode(hint), remaining), "");
}

std::string runcli(const std::string &command, uint64_t guild_id, uint64_t user_id, uint64_t channel_id, const std::string &parameters)
{
	std::array<char, 128> buffer;
	std::string result;
	std::string safe_parameters = ReplaceString(parameters, "\\", "\\\\");
	safe_parameters = ReplaceString(safe_parameters, "\"", "\\\"");
	std::string home(getenv("HOME"));
	std::string safe_command(fmt::format("/usr/bin/php \"{}/www/cli-run.php\" \"{}\" {} {} {} \"{}\"", home, command, guild_id, user_id, channel_id, safe_parameters));
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(safe_command.c_str(), "r"), pclose);
	if (!pipe) {
		throw std::runtime_error("popen() failed!");
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}
	return result;
}

/* Farm out a custom command to the API to be handled completely via a PHP script. Some things are better done this way as the code is cleaner and more stable, e.g. the !rank/!globalrank command
 * Note: These are now directly executed via commandline NOT via a REST reqeust, bypassing apache as this is a ton faster.
 */
std::string custom_command(const std::string &command, const std::string &parameters, int64_t user_id, int64_t channel_id, int64_t guild_id)
{
	return runcli(command, guild_id, user_id, channel_id, parameters);
}

/* Update the score only, for a user during insane round */
void update_score_only(int64_t snowflake_id, int64_t guild_id, int score, int64_t channel_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("INSERT INTO scores (name, guild_id, score, dayscore, weekscore, monthscore) VALUES('?', '?', '?', '?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, guild_id, score, score, score, score, score, score, score, score});
	db::query("INSERT INTO scores_lastgame (guild_id, user_id, score) VALUES('?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, snowflake_id, score, score});
	db::query("INSERT INTO insane_round_statistics (guild_id, channel_id, user_id, score) VALUES('?', '?', '?', '?') ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, channel_id, snowflake_id, score, score});
}

void check_achievement(const std::string &when, uint64_t user_id, uint64_t guild_id)
{
	later(fmt::format("?opt=ach&user_id={}&guild_id={}&when={}", user_id, guild_id, url_encode(when)), "");
}

/* Log the start of a game to the database via the API, used for resuming of games on crash or restart, plus the dashboard active games list */
void log_game_start(int64_t guild_id, int64_t channel_id, int64_t number_questions, bool quickfire, const std::string &channel_name, int64_t user_id, const std::vector<std::string> &questions, bool hintless)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);

	check_achievement("start", user_id, guild_id);
	uint32_t cluster_id = bot->GetClusterID();

	db::query("INSERT INTO active_games (cluster_id, guild_id, channel_id, hostname, quickfire, questions, channel_name, user_id, qlist, hintless) VALUES('?', '?', '?', '?', '?', '?', '?', '?', '?', '?')",
			{cluster_id, guild_id, channel_id, std::string(hostname), quickfire ? 1 : 0, number_questions, channel_name, user_id, json(questions).dump(), hintless ? 1 : 0});
	db::query("DELETE FROM scores_lastgame WHERE guild_id = ?", {guild_id});
}

/* Log the end of a game, used for resuming games on crash or restart, plus the dashboard active games list */
void log_game_end(int64_t guild_id, int64_t channel_id)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	
	/* Obtain and delete the active game entry */
	db::resultset gameinfo = db::query("SELECT * FROM active_games WHERE guild_id = '?' AND channel_id = '?' AND hostname = '?'", {guild_id, channel_id, std::string(hostname)});
	db::query("DELETE FROM active_games WHERE guild_id = '?' AND channel_id = '?' AND hostname = '?'", {guild_id, channel_id, std::string(hostname)});

	/* Collate the last game's scores into JSON for storage in the database for the stats pages */
	db::resultset lastgame = db::query("SELECT * FROM scores_lastgame WHERE guild_id = '?'",{guild_id});
	std::string scores = "[";
	for (auto r = lastgame.begin(); r != lastgame.end(); ++r) {
		scores += "{\"user_id\":\"" + (*r)["user_id"] + "\",\"score\":\"" + (*r)["score"] + "\"},";
	}
	scores = scores.substr(0, scores.length() - 1) + "]";
	if (gameinfo.size() > 0 && scores != "]") {
		db::query("INSERT INTO game_score_history (guild_id, timestarted, timefinished, scores) VALUES('?', '?', now(), '?')", {guild_id, gameinfo[0]["started"], scores});
	}

	/* Safeguard */
	db::query("DELETE FROM insane_round_statistics WHERE channel_id = '?'", {channel_id});
}

/* Update current question of a game, used for resuming games on crash or restart, plus the dashboard active games list */
bool log_question_index(int64_t guild_id, int64_t channel_id, int32_t index, uint32_t streak, int64_t lastanswered, uint32_t state, int32_t qid)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);

	uint32_t cluster_id = bot->GetClusterID();
	guild_settings_t settings = module->GetGuildSettings(guild_id);
	bool should_stop = false;

	/* Update game details */
	db::query("UPDATE active_games SET cluster_id = '?', question_index = '?', streak = '?', lastanswered = '?', state = '?' WHERE guild_id = '?' AND channel_id = '?' AND hostname = '?'",
			{cluster_id, index, streak, lastanswered, state, guild_id, channel_id, std::string(hostname)});

	/* Check if the dashboard has stopped this game */
	db::resultset st = db::query("SELECT stop FROM active_games WHERE guild_id = '?' AND channel_id = '?' AND hostname = '?' AND stop = 1", {guild_id, channel_id, std::string(hostname)});
	should_stop = (st.size() > 0);

	if (state == TRIV_ASK_QUESTION) {
		db::resultset ques = db::query("SELECT * FROM questions WHERE id = '?'", {qid});
		db::query("UPDATE counters SET asked_15_min = asked_15_min + 1", {});
		if (ques.size() > 0) {
			int32_t category_id = from_string<int32_t>(ques[0]["category"], std::dec);
			db::query("UPDATE categories SET questions_asked = questions_asked + 1 WHERE id = '?'", {category_id});
		}
	}

	return should_stop;
}

/* Update the score for a user and their team, during non-insane round */
int32_t update_score(int64_t snowflake_id, int64_t guild_id, time_t recordtime, int64_t id, int score)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("UPDATE stats SET lastcorrect='?', record_time='?' WHERE id = ?", {snowflake_id, recordtime, id});
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
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT count(id) as total FROM questions", {});
	return from_string<int32_t>(r[0]["total"], std::dec);
}

/* Return the current top ten players for the day for a guild, plus their scores */
std::vector<std::string> get_top_ten(uint64_t guild_id)
{
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
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("DELETE FROM team_membership WHERE nick = '?'", {snowflake_id});
}

/* Make a player join a team */
bool join_team(int64_t snowflake_id, const std::string &team, int64_t channel_id)
{
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
	db::query("INSERT INTO streaks (nick, guild_id, streak) VALUES('?','?','?') ON DUPLICATE KEY UPDATE streak='?'", {snowflake_id, guild_id, score, score});
	check_achievement("streak", snowflake_id, guild_id);
}

/* Get the current streak details for a player on a guild, and the best streak for the guild at present */
streak_t get_streak(int64_t snowflake_id, int64_t guild_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
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
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT name FROM teams WHERE name = '?'", {team});
	return (r.size());
}

/* Add points to a team */
void add_team_points(const std::string &team, int points, int64_t snowflake_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("UPDATE teams SET score = score + ? WHERE name = '?'", {points, team});
	if (snowflake_id) {
		db::query("UPDATE team_membership SET points_contributed = points_contributed + ? WHERE nick = '?'", {points, snowflake_id});
	}

}

/* Get the points of a team */
int32_t get_team_points(const std::string &team)
{
	// Replaced with direct db query for performance increase - 27Dec20
	db::resultset r = db::query("SELECT score FROM teams WHERE name = '?'", {team});
	if (r.size()) {
		return from_string<int32_t>(r[0]["score"], std::dec);
	} else {
		return 0;
	}
}

void CheckCreateWebhook(uint64_t channel_id)
{
	/* Create webhook */
	runcli("createwebhook", 0, 0, channel_id, "");
}

void PostWebhook(const std::string &webhook_url, const std::string &embed)
{
	std::lock_guard<std::mutex> fi(fafindex);
	std::lock_guard<std::mutex> fafguard(faflock[faf_index]);

	std::string host = webhook_url.substr(0, webhook_url.find("/api/"));
	std::string path = webhook_url.substr(host.length(), webhook_url.length());
	faf[faf_index].push({host, path, "{\"content\":\"\", \"embeds\":[" + embed + "]}"});
	faf_index++;
	if (faf_index > FIRE_AND_FORGET_QUEUES - 1) {
		faf_index = 0;
	}
}

