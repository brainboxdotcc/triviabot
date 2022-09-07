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

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <dpp/nlohmann/json.hpp>
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
#include "webhook_icon.h"

using json = nlohmann::json;

#define START_STATUS 100
#define END_STATUS 600
#define FIRE_AND_FORGET_QUEUES 10

Bot* bot = nullptr;
TriviaModule* module = nullptr;
std::string apikey;

std::mutex faflock[FIRE_AND_FORGET_QUEUES];
std::mutex fafindex;
std::mutex interfaceindex;
std::mutex statsmutex;
std::mutex rlmutex;

uint32_t faf_index = 0;
uint32_t interface_index = 0;

std::thread* ft[FIRE_AND_FORGET_QUEUES] = { nullptr };
std::thread* statdumper;

std::map<uint64_t, std::pair<uint64_t, time_t> > channellock;


std::map<std::string, std::map<uint32_t, uint64_t> > statuscodes;
std::map<std::string, uint64_t> requests;
std::map<std::string, uint64_t> errors;

std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body = "", uint64_t channel_id = 0);
std::string fetch_page(const std::string &_endpoint, const std::string &body = "");
std::vector<std::string> getinterfaces();

question_t::question_t(uint64_t _id, dpp::snowflake _guild_id, const std::string &_question, const std::string &_answer, const std::string &_hint1, const std::string &_hint2, const std::string &_catname, time_t _lastasked, uint32_t _timesasked,
	const std::string &_lastcorrect, double _record_time, const std::string &_shuffle1, const std::string &_shuffle2, const std::string &_question_image, const std::string &_answer_image) :
	id(_id), guild_id(_guild_id), question(_question), answer(_answer), customhint1(_hint1), customhint2(_hint2), catname(_catname), lastasked(_lastasked), timesasked(_timesasked), lastcorrect(_lastcorrect), recordtime(_record_time),
	shuffle1(_shuffle1), shuffle2(_shuffle2), question_image(_question_image), answer_image(_answer_image)
{
}

question_t::question_t() : id(0), guild_id(0), lastasked(0), timesasked(0), recordtime(0)
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
	uint64_t channel_id;
};

/* A queue of fire-and-forget requests waiting to be executed. */
std::queue<fire_and_forget_t> faf[FIRE_AND_FORGET_QUEUES];

void fireandforget(uint32_t queue_index)
{
	dpp::utility::set_thread_name("bot/wh/" + std::to_string(queue_index));
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
			web_request(f.host, f.path, f.body, f.channel_id);
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

void statdump()
{
	dpp::utility::set_thread_name("bot/whstat");
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
				db::backgroundquery("INSERT INTO http_requests (interface, hard_errors, requests) VALUES(?, ?, ?) ON DUPLICATE KEY UPDATE hard_errors = hard_errors + ?, requests = requests + ?", {i, e, r, e, r});
				if (statuscodes.find(i) != statuscodes.end()) {
					for (auto & codes : statuscodes[i]) {
						db::backgroundquery("INSERT INTO http_status_codes (interface, status_code, requests) VALUES(?, ?, ?) ON DUPLICATE KEY UPDATE requests = requests + ?", {i, codes.first, codes.second, codes.second});
						statuscodes[i][codes.first] = 0;
					}
				}

			}
		}
		std::this_thread::sleep_for(std::chrono::seconds(60));
	}
}

/* Initialisation function */
void set_io_context(const std::string &_apikey, Bot* _bot, TriviaModule* _module)
{
	apikey = _apikey;
	bot = _bot;
	module = _module;
	for (uint32_t i = 0; i < FIRE_AND_FORGET_QUEUES; ++i) {
		ft[i] = new std::thread(&fireandforget, i);
	}
	statdumper = new std::thread(&statdump);
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

std::vector<std::string> interfaces = getinterfaces();

std::string getinterface()
{
	int curindex;
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
std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body, uint64_t channel_id)
{
	std::string iface = getinterface();
	try
	{
		/* Check that another queue isn't waiting on rate limit for this channel or interface */
		time_t when = 0;
		uint64_t seconds = 0;

		/* Check for existing rate limits that other queue threads have flagged */
		std::string type;
		{
			std::lock_guard<std::mutex> rl(rlmutex);
			/* Channel rate limit hit on another thread */
			if (channellock.find(channel_id) != channellock.end()) {
				when = channellock[channel_id].second;
				seconds = channellock[channel_id].first;
				type = fmt::format("channel {}", channel_id);
			}
		}
		/* If we have to wait, and the rate limit timeout has not already been reached, wait. */
		if (seconds > 0 && when + seconds > time(NULL)) {
			/* Rate limit waits that have expired will have negative seconds here */
			if (bot) {
				bot->core->log(dpp::ll_warning, fmt::format("Rate limit would be reached on other thread: Waiting {} seconds for rate limit to clear on {}", seconds, type));
			}
			std::this_thread::sleep_for(std::chrono::seconds(seconds));
		}

		/* Build httplib client */
		httplib::Client cli(_host.c_str());
		cli.enable_server_certificate_verification(false);
		cli.set_interface(iface.c_str());

		if (!channel_id) {
			httplib::Headers headers = {
				{"X-API-Auth", apikey}
			};
			cli.set_default_headers(headers);
		}

		std::string rv;
		int code = 0;

		if (_body.empty()) {
			if (auto res = cli.Get(_path.c_str())) {
				if (res->status < 400) {
					rv = res->body;
				} else {
					if (bot) {
						bot->core->log(dpp::ll_warning, fmt::format("HTTP Error {} on GET {}/{}", res->status, _host, _path));
					}
				}
			}
		}
		else {
			if (auto res = cli.Post(_path.c_str(), _body.c_str(), "application/json")) {
				if (res->status < 400) {
					rv = res->body;
				} else {
					if (bot) {
						bot->core->log(dpp::ll_warning, fmt::format("HTTP Error {} on POST {}/{} (channel_id={})", res->status, _host, _path, channel_id));
						if (res->status == 404) {
							{
								std::unique_lock lock(module->wh_mutex);
								auto i = module->webhooks.find(channel_id);
								if (i != module->webhooks.end()) {
									module->webhooks.erase(i);
								}
							}
							db::backgroundquery("DELETE FROM channel_webhooks WHERE channel_id = ?", {channel_id});
						}
					}
				}

				/* Check rate limits, global and per channel */
				if (res->get_header_value("X-RateLimit-Remaining") != "" && from_string<uint64_t>(res->get_header_value("X-RateLimit-Remaining"), std::dec) == 0) {

					uint64_t seconds = 0;
					/* If there's a retry-after (global ratelimit) we always prefer that over reset-after */
					if (res->get_header_value("X-RateLimit-Retry-After") != "") {
						seconds = from_string<time_t>(res->get_header_value("X-RateLimit-Retry-After"), std::dec);;
					} else {
						seconds = from_string<time_t>(res->get_header_value("X-RateLimit-Reset-After"), std::dec);
					}

					std::string type;

					if (res->get_header_value("X-RateLimit-Global") != "") {
						/* Global ratelimit hit (!) lock interface. 
						 * On single-homed systems this holds back all requests in all queues, in multi-homed
						 * systems this holds back all requests coming from the same network interface (generally
						 * the same IP, but may not be if production is using NIC teaming)
						*/
						db::backgroundquery("INSERT INTO http_ratelimit (interface, rl_when, rl_seconds) VALUES(?,?,?) ON DUPLICATE KEY UPDATE rl_when = ?, rl_seconds = ?", {iface, seconds, time(NULL), time(NULL), seconds});
						type = fmt::format("interface {}", iface);
					} else {
						/* Channel ratelimit hit (other webhooks are being fired by other bots/automations
						 * taking the number of requests in the last minute over 30)
						 */
						std::lock_guard<std::mutex> rl(rlmutex);
						channellock[channel_id] = std::make_pair(seconds, time(NULL));
						type = fmt::format("channel {}", channel_id);
					}
					if (seconds > 0) {
						/* If there are seconds to sleep, wait for them in this thread.
						 * Other threads are told to wait by the values stored to channellock
						 * map and http_ratelimit table.
						 */
						if (bot) {
							bot->core->log(dpp::ll_warning, fmt::format("Rate limit would be reached on this thread: waiting {} seconds for rate limit to clear on {}", seconds, type));
						}
						std::this_thread::sleep_for(std::chrono::seconds(seconds));
					}
				}

				/* Update mutexed counters */
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
		if (bot) {
			bot->core->log(dpp::ll_warning, fmt::format("Exception: {}", e.what()));
		}
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
		faf[faf_index].push({BACKEND_HOST_DEV, fmt::format(BACKEND_PATH_DEV, _path), _body, 0});
	} else {
		faf[faf_index].push({BACKEND_HOST_LIVE, fmt::format(BACKEND_PATH_LIVE, _path), _body, 0});
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
void cache_user(const dpp::user *_user, const dpp::guild *_guild, const dpp::guild_member* gi)
{
	// Replaced with direct db query for perforamance increase - 27Dec20

	uint64_t user_id = _user->id;
	uint64_t guild_id = _guild->id;

	db::backgroundquery("INSERT INTO trivia_user_cache (snowflake_id, username, discriminator, icon) VALUES(?, ?, ?, ?) ON DUPLICATE KEY UPDATE username = ?, discriminator = ?, icon = ?",
			{user_id, _user->username, _user->discriminator, _user->avatar.to_string(), _user->username, _user->discriminator, _user->avatar.to_string()});

	db::backgroundquery("INSERT INTO trivia_guild_cache (snowflake_id, name, icon, owner_id) VALUES(?, ?, ?, ?) ON DUPLICATE KEY UPDATE name = ?, icon = ?, owner_id = ?, kicked = 0",
			{guild_id, _guild->name, _guild->icon.to_string(),  _guild->owner_id, _guild->name, _guild->icon.to_string(),  _guild->owner_id});

	std::string member_roles;
	std::string comma_roles;
	for (auto r = gi->roles.begin();r != gi->roles.end(); ++r) {
		member_roles.append(std::to_string(*r)).append(" ");
	}
	member_roles = trim(member_roles);
	db::backgroundquery("INSERT INTO trivia_guild_membership (guild_id, user_id, roles) VALUES(?, ?, ?) ON DUPLICATE KEY UPDATE roles = ?",
			{guild_id, user_id, member_roles, member_roles});

	for (auto n = _guild->roles.begin(); n != _guild->roles.end(); ++n) {
		dpp::role* r = dpp::find_role(*n);
		if (r) {
			comma_roles.append(std::to_string(r->id)).append(",");
			db::backgroundquery("INSERT INTO trivia_role_cache (id, guild_id, colour, permissions, position, hoist, managed, mentionable, name) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE colour = ?, permissions = ?, position = ?, hoist = ?, managed = ?, mentionable = ?, name = ?",
			{
				r->id, guild_id, r->colour, r->permissions, r->position, (r->is_hoisted() ? 1 : 0), (r->is_managed() ? 1 : 0), (r->is_mentionable() ? 1 : 0), r->name,
				r->colour, r->permissions, r->position, (r->is_hoisted() ? 1 : 0), (r->is_managed() ? 1 : 0), (r->is_mentionable() ? 1 : 0), r->name
			});
		}
	}
	comma_roles = trim(comma_roles.substr(0, comma_roles.length() - 1));
	/* Delete any that have been deleted from discord */
	db::backgroundquery("DELETE FROM trivia_role_cache WHERE guild_id = ? AND id NOT IN (" + comma_roles + ")", {guild_id});
}

/* Fetch a question by ID from the database */
std::vector<question_t> question_t::fetch(const std::vector<uint64_t>& id, uint64_t guild_id, const guild_settings_t &settings)
{
	try {
		std::vector<question_t> rv;
		db::resultset qs;
		db::paramlist vals;
		std::string args, string_args;
		for (auto& _id : id) {
			vals.push_back(std::to_string(_id));
			args.append("?,");
			string_args.append(std::to_string(_id)).append("?,");
		}
		args = args.substr(0, args.length() - 1);
		string_args = string_args.substr(0, string_args.length() - 1);
		vals.emplace_back(string_args);
		if (settings.language == "en") {
			qs = db::query("select questions.*, ans1.*, hin1.*, sta1.*, cat1.name as catname from questions left join hints as hin1 on questions.id=hin1.id left join answers as ans1 on questions.id=ans1.id left join stats as sta1 on questions.id=sta1.id left join categories as cat1 on questions.category=cat1.id where questions.id in (" + args + ") order by FIND_IN_SET(questions.id, ?)", vals);
		} else {
			qs = db::query("select questions.trans_" + settings.language + " as question, ans1.trans_" + settings.language + " as answer, hin1.trans1_" + settings.language + " as hint1, hin1.trans2_" + settings.language + " as hint2, question_img_url, questions.guild_id, answer_img_url, sta1.*, cat1.trans_" + settings.language + " as catname from questions left join hints as hin1 on questions.id=hin1.id left join answers as ans1 on questions.id=ans1.id left join stats as sta1 on questions.id=sta1.id left join categories as cat1 on questions.category=cat1.id where questions.id in (" + args + ") order by FIND_IN_SET(questions.id, ?)", vals);
		}
		for (auto& question : qs) {
			rv.emplace_back(
				question_t(
					question["id"].getUInt(),
					question["guild_id"].getUInt(),
					homoglyph(question["question"]),
					question["answer"],
					question["hint1"],
					question["hint2"],
					question["catname"],
					question["lastasked"].getUInt(),
					question["timesasked"].getUInt(),
					question["lastcorrect"],
					from_string<double>(question["record_time"].getString(), std::dec),
					utf8shuffle(question["answer"]),
					utf8shuffle(question["answer"]),
					question["question_img_url"],
					question["answer_img_url"]
				)
			);
		}
		return rv;
	}
	catch (const std::exception &e) {
		if (bot) {
			bot->core->log(dpp::ll_error, fmt::format("Exception: {}", e.what()));
		} else {
			std::cout << "Exception: " << e.what() << std::endl;
		}
	}
	return std::vector<question_t>();
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

// Twister random generator
static std::random_device dev;
static std::mt19937_64 rng(dev());


template<class BidiIter> BidiIter random_unique(BidiIter begin, BidiIter end, size_t num_random) {
	size_t left = std::distance(begin, end);
	while (num_random--) {
		BidiIter r = begin;
		std::uniform_int_distribution<size_t> dist(0, left - 1);
		std::advance(r, dist(rng));
		std::swap(*begin, *r);
		++begin;
		--left;
	}
	return begin;
}

/* Fetch a shuffled list of question IDs from the API, which is dependant upon some statistics for the guild and the category selected. */
std::vector<std::string> fetch_shuffle_list(uint64_t guild_id, const std::string &category, const guild_settings_t &settings)
{
	if (guild_id == 0) {
		throw NoSuchCategoryException();
	}

	std::vector<std::string> return_value;
	uint32_t weekscore = 0;
	db::resultset r = db::query("SELECT guild_id, SUM(weekscore) AS weekscore FROM scores WHERE guild_id = ? GROUP BY guild_id", {guild_id});
	if (r.size()) {
		weekscore = from_string<uint32_t>(r[0]["weekscore"], std::dec);
	}

	bool premium = settings.premium;
	uint32_t min_questions = premium ? MIN_QUESTIONS_PREMIUM : MIN_QUESTIONS;

	if (!category.empty()) {
		// Play specific category by name
		std::string column(settings.language == "en" ? "name" : "trans_" + settings.language);
		if (category.find(";") != std::string::npos) {
			// multiple category names
			std::vector<std::string> cats = dpp::utility::tokenize(category, ";");
			std::string query("(");
			db::paramlist parameters;
			std::string sv;
			// Check for non-premium trying to use server category
			for (auto& cm : cats) {
				if (lowercase(trim(cm)) == "server" && !premium) {
					throw NoSuchCategoryException();
				}
			}
			for (auto& cm : cats) {
				if (lowercase(trim(cm)) == "server" && premium) {
					// Guild specific premium question list
					db::resultset result = db::query("SELECT questions.id FROM questions WHERE guild_id = ?", {guild_id});
					random_unique(result.begin(), result.end(), 200);
					for (auto& ans : result) {
						return_value.emplace_back(ans["id"]);
					}
				} else {
					query.append("(? LIKE ?%) OR ");
					parameters.emplace_back(column);
					parameters.emplace_back(trim(ReplaceString(cm, "%", "_")));
				}
			}
			if (query.substr(query.length() - 4, 4) == " OR ") {
				query = query.substr(0, query.length() - 4);
			}
			query.append(")");
			db::resultset cr = db::query("SELECT id FROM categories WHERE " + query + " AND disabled = 0", parameters);
			uint32_t count = 0;
			db::paramlist cl;
			query.clear();
			for (auto& cat : cr) {
				auto r = db::query("SELECT COUNT(id) AS c FROM questions WHERE category = ?", {cat["id"]});
				if (r.size()) {
					count += from_string<uint32_t>(r[0]["c"], std::dec);
					cl.emplace_back(cat["id"]);
					query.append("?,");
				}
			}
			if (query.length()) {
				query = query.substr(0, query.length() - 1);
			}
			if (count >= min_questions) {
				auto result = db::query("SELECT questions.id, questions.category FROM questions INNER JOIN categories ON questions.category = categories.id WHERE questions.guild_id IS NULL AND categories.disabled != 1 AND category IN (" + query + ")", cl);
				random_unique(result.begin(), result.end(), 200);
				for (auto & ans : result) {
					return_value.emplace_back(ans["id"]);
				}
			} else {
				throw CategoryTooSmallException();
			}
			return return_value;
		} else {
			// single category name
			if (lowercase(trim(category)) == "server" && !premium) {
				throw NoSuchCategoryException();
			} else if (lowercase(trim(category)) == "server" && premium) {
				// guild specific premium category list
				uint32_t qc = 0, iterations = 0;
				while (qc < 200 && iterations < 200) {
					auto result = db::query("SELECT questions.id FROM questions WHERE guild_id = ?", {settings.guild_id});
					random_unique(result.begin(), result.end(), 200);
					for (auto & ans : result) {
						return_value.emplace_back(ans["id"]);
						qc++;
					}
				}
				if (qc < 200) {
					throw CategoryTooSmallException();
				}
				return return_value;
			} else {
				db::resultset cat = db::query("SELECT id FROM categories WHERE (? LIKE ?%) AND disabled = 0", {column, trim(ReplaceString(category, "%", "_"))});
				if (cat.size()) {
					auto r = db::query("SELECT COUNT(id) AS c FROM questions WHERE category = ?", {cat[0]["id"]});
					if (r.size()) {
						if (from_string<uint32_t>(r[0]["c"], std::dec) >= min_questions) {
							auto result = db::query("SELECT questions.id, questions.category FROM questions INNER JOIN categories ON questions.category = categories.id WHERE questions.guild_id IS NULL AND categories.disabled != 1 AND category = ? ORDER BY", {cat[0]["id"]});
							random_unique(result.begin(), result.end(), 200);
							for (auto & ans : result) {
								return_value.emplace_back(ans["id"]);
							}
							return return_value;
						}
					}
					throw CategoryTooSmallException();
				} else {
					throw NoSuchCategoryException();
				}

			}
		}
	} else {
		// Play all enabled categories
		uint32_t lastcat = -1;
		uint32_t questions = 0;
		std::map<uint64_t, uint64_t> list;
		std::map<std::string, bool> variation_list;
		size_t variation = 0;
		db::resultset result;
		if (premium) {
			result = db::query("SELECT questions.id, questions.category FROM questions \
				INNER JOIN categories ON questions.category = categories.id AND categories.disabled = 0 \
				LEFT JOIN disabled_categories ON questions.category = disabled_categories.category_id and disabled_categories.guild_id = ? \
				WHERE (questions.guild_id IS NULL OR questions.guild_id = ?) AND disabled_categories.category_id is null", {guild_id, guild_id});
		} else {
			result = db::query("SELECT questions.id, questions.category FROM questions \
				INNER JOIN categories ON questions.category = categories.id AND categories.disabled = 0 \
				LEFT JOIN disabled_categories ON questions.category = disabled_categories.category_id and disabled_categories.guild_id = ? \
				WHERE (questions.guild_id IS NULL) AND disabled_categories.category_id is null", {guild_id});
		}
		random_unique(result.begin(), result.end(), 250);
		for (auto & ans : result) {
			list[from_string<uint64_t>(ans["id"], std::dec)] = from_string<uint64_t>(ans["category"], std::dec);
			variation_list[ans["category"]] = true;
		}
		variation = variation_list.size();
		if (variation < 10) {
			// Very few categories enabled, can't ensure category doesnt duplicate between questions!	
			size_t q_max = 0;
			for (auto & ans : list) {
				return_value.emplace_back(std::to_string(ans.first));
				if (++q_max > 201) {
					break;
				}
			}
		} else {
			// Ensure best we can that we don't  get two identical categories in a row
			for (size_t n = 0; n < 201;) {
				std::uniform_int_distribution<size_t> idDist(0, list.size() - 1);
				auto selected = list.begin();
				std::advance(selected, idDist(rng));
				if (lastcat != selected->second) {
					lastcat = selected->second;
					return_value.emplace_back(std::to_string(selected->first));
					n++;
					list.erase(selected);
				} else if (list.size() < 2) {
					return_value.emplace_back(std::to_string(selected->first));
					n++;
					lastcat = -1;
				}
				if (list.empty()) {
					break;
				}
			}
		}

		return return_value;
	}
}

/* Fetch a random insane round from the database, setting the question_id parameter and returning the question and all answers in a vector */
std::vector<std::string> fetch_insane_round(uint64_t &question_id, uint64_t guild_id, const guild_settings_t &settings)
{
	std::vector<std::string> list;
	db::resultset answers;
	db::resultset question;
	if (settings.language == "en") {
		question = db::query("select id,question from insane where deleted is null order by rand() limit 0,1");
		answers = db::query("select id,answer from insane_answers where question_id = ?", {question[0]["id"].getUInt()});
	} else {
		question = db::query("select id,trans_" + settings.language + " AS question from insane where deleted is null order by rand() limit 0,1");
		answers = db::query("select id, trans_" + settings.language + " answer from insane_answers where question_id = ?", {question[0]["id"].getUInt()});
	}

	question_id = question[0]["id"].getUInt();
	list.push_back(homoglyph(question[0]["question"]));

	for (auto a = answers.begin(); a != answers.end(); ++a) {
		list.push_back((*a)["answer"]);
	}

	return list;
}

void runcli(guild_settings_t settings, const std::string &command, uint64_t guild_id, uint64_t user_id, uint64_t channel_id, const std::string &parameters, const std::string& interaction_token, dpp::snowflake command_id)
{
	std::string home(getenv("HOME"));

	/* IMPORTANT: dpp::utility::exec makes parameters safe */
	dpp::utility::exec("/usr/bin/php", { fmt::format("{}/www/cli-run.php", home), command, std::to_string(guild_id), std::to_string(user_id), std::to_string(channel_id), parameters }, [channel_id, guild_id, interaction_token, command_id](const std::string &output) {
		guild_settings_t s = module->GetGuildSettings(guild_id);
		/* Output response as embed */
		std::string reply = trim(output);
		if (!reply.empty()) {
			module->ProcessEmbed(interaction_token, command_id, s, reply, channel_id);
		} else if (!interaction_token.empty()) {
			/* Empty reply but handled. delete "thinking" notification */
			bot->core->post_rest(API_PATH "/webhooks", std::to_string(bot->core->me.id), dpp::utility::url_encode(interaction_token) + "/messages/@original", dpp::m_delete, "", [&](auto& json, auto& request) {}, "", "");
		}
	});
}

/* Farm out a custom command to the API to be handled completely via a PHP script. Some things are better done this way as the code is cleaner and more stable, e.g. the !rank/!globalrank command
 * Note: These are now directly executed via commandline NOT via a REST reqeust, bypassing apache as this is a ton faster.
 */
void custom_command(const std::string& interaction_token, dpp::snowflake command_id, const guild_settings_t& settings, TriviaModule* tm, const std::string &command, const std::string &parameters, uint64_t user_id, uint64_t channel_id, uint64_t guild_id)
{
	runcli(settings, command, guild_id, user_id, channel_id, parameters, interaction_token, command_id);
}

/* Update the score only, for a user during insane round */
void update_score_only(uint64_t snowflake_id, uint64_t guild_id, int score, uint64_t channel_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::backgroundquery("INSERT INTO scores (name, guild_id, score, dayscore, weekscore, monthscore) VALUES(?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, guild_id, score, score, score, score, score, score, score, score});
	db::backgroundquery("INSERT INTO global_scores (name, score, dayscore, weekscore, monthscore) VALUES(?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, score, score, score, score, score, score, score, score});
	db::backgroundquery("INSERT INTO scores_lastgame (guild_id, user_id, score) VALUES(?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, snowflake_id, score, score});
	db::backgroundquery("INSERT INTO insane_round_statistics (guild_id, channel_id, user_id, score) VALUES(?, ?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, channel_id, snowflake_id, score, score});
}

void check_achievement(const std::string &when, uint64_t user_id, uint64_t guild_id)
{
	later(fmt::format("?opt=ach&user_id={}&guild_id={}&when={}", user_id, guild_id, dpp::utility::url_encode(when)), "");
}

/* Log the start of a game to the database via the API, used for resuming of games on crash or restart, plus the dashboard active games list */
void log_game_start(uint64_t guild_id, uint64_t channel_id, uint64_t number_questions, bool quickfire, const std::string &channel_name, uint64_t user_id, const std::vector<std::string> &questions, bool hintless)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);

	check_achievement("start", user_id, guild_id);
	uint32_t cluster_id = bot->GetClusterID();

	db::backgroundquery("INSERT INTO active_games (cluster_id, guild_id, channel_id, hostname, quickfire, questions, channel_name, user_id, qlist, hintless) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
			{cluster_id, guild_id, channel_id, std::string(hostname), quickfire ? 1 : 0, number_questions, channel_name, user_id, json(questions).dump(), hintless ? 1 : 0});
	db::backgroundquery("DELETE FROM scores_lastgame WHERE guild_id = ?", {guild_id});
}

/* Log the end of a game, used for resuming games on crash or restart, plus the dashboard active games list */
void log_game_end(uint64_t guild_id, uint64_t channel_id)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	
	/* Obtain and delete the active game entry */
	db::resultset gameinfo = db::query("SELECT * FROM active_games WHERE guild_id = ? AND channel_id = ? AND hostname = ?", {guild_id, channel_id, std::string(hostname)});
	db::backgroundquery("DELETE FROM active_games WHERE guild_id = ? AND channel_id = ? AND hostname = ?", {guild_id, channel_id, std::string(hostname)});

	/* Collate the last game's scores into JSON for storage in the database for the stats pages */
	db::resultset lastgame = db::query("SELECT * FROM scores_lastgame WHERE guild_id = ?",{guild_id});
	std::string scores = "[";
	for (auto r = lastgame.begin(); r != lastgame.end(); ++r) {
		scores += "{\"user_id\":\"" + (*r)["user_id"].getString() + "\",\"score\":\"" + (*r)["score"].getString() + "\"},";
	}
	scores = scores.substr(0, scores.length() - 1) + "]";
	if (gameinfo.size() > 0 && scores != "]") {
		db::backgroundquery("INSERT INTO game_score_history (guild_id, timestarted, timefinished, scores) VALUES(?, ?, now(), ?)", {guild_id, gameinfo[0]["started"].getString(), scores});
	}

	/* Safeguard */
	db::backgroundquery("DELETE FROM insane_round_statistics WHERE channel_id = ?", {channel_id});
}

/* Update current question of a game, used for resuming games on crash or restart, plus the dashboard active games list */
bool log_question_index(uint64_t guild_id, uint64_t channel_id, uint32_t index, uint32_t streak, uint64_t lastanswered, uint32_t state, uint32_t qid)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);

	uint32_t cluster_id = bot->GetClusterID();
	guild_settings_t settings = module->GetGuildSettings(guild_id);
	bool should_stop = false;

	{
		std::lock_guard<std::mutex> locker(module->cs_mutex);
		last_streak_t t;
		t.lastanswered = lastanswered;
		t.streak = streak;
		t.time = time(NULL);
		module->last_channel_streaks[channel_id] = t;
	}

	/* Update game details */
	db::backgroundquery("UPDATE active_games SET cluster_id = ?, question_index = ?, streak = ?, lastanswered = ?, state = ? WHERE guild_id = ? AND channel_id = ? AND hostname = ?",
			{cluster_id, index, streak, lastanswered, state, guild_id, channel_id, std::string(hostname)});

	/* Check if the dashboard has stopped this game */
	db::resultset st = db::query("SELECT stop FROM active_games WHERE guild_id = ? AND channel_id = ? AND hostname = ? AND stop = 1", {guild_id, channel_id, std::string(hostname)});
	should_stop = (st.size() > 0);

	if (state == TRIV_ASK_QUESTION) {
		db::backgroundquery("UPDATE counters SET asked_15_min = asked_15_min + 1");
		db::backgroundquery("UPDATE categories inner join questions on questions.category = categories.id SET questions_asked = questions_asked + 1 WHERE questions.id = ?", {qid});
	}

	return should_stop;
}

/* Update the score for a user and their team, during non-insane round */
uint32_t update_score(uint64_t snowflake_id, uint64_t guild_id, double recordtime, uint64_t id, int score, bool local_only)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::backgroundquery("UPDATE stats SET lastcorrect=?, record_time=? WHERE id = ?", {snowflake_id, recordtime, id});
	db::backgroundquery("INSERT INTO scores (name, guild_id, score, dayscore, weekscore, monthscore) VALUES(?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, guild_id, score, score, score, score, score, score, score, score});
	if (!local_only) {
		db::backgroundquery("INSERT INTO global_scores (name, score, dayscore, weekscore, monthscore) VALUES(?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?, weekscore = weekscore + ?, monthscore = monthscore + ?, dayscore = dayscore + ?",
			{snowflake_id, score, score, score, score, score, score, score, score});
	}
	db::backgroundquery("INSERT INTO scores_lastgame (guild_id, user_id, score) VALUES(?, ?, ?) ON DUPLICATE KEY UPDATE score = score + ?", {guild_id, snowflake_id, score, score});

	return 0;
}

/* Return a total count of questions from the database. The query can be expensive, avoid calling too frequently */
uint32_t get_total_questions()
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT count(id) as total FROM questions");
	return r[0]["total"].getUInt();
}

/* Return the current team name for a player, or an empty string */
std::string get_current_team(uint64_t snowflake_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT team FROM team_membership WHERE nick = ?", {snowflake_id});
	if (r.size()) {
		return r[0]["team"];
	} else {
		return "";
	}
}

/* Make a player leave the current team if they are in one. REMOVES THE RECORD of their individual score contribution! */
void leave_team(uint64_t snowflake_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::query("DELETE FROM team_membership WHERE nick = ?", {snowflake_id});
}

/* Make a player join a team */
bool join_team(uint64_t snowflake_id, const std::string &team, uint64_t channel_id)
{
	if (check_team_exists(team)) {
		auto teaminfo = db::query("SELECT * FROM teams WHERE name = ?", {team});
		if (teaminfo.size() && teaminfo[0]["qualifying_score"].getUInt() > 0) {
			uint64_t qualifying = teaminfo[0]["qualifying_score"].getUInt();
			auto rs_score = db::query("SELECT * FROM vw_scorechart WHERE name = ?", {team});
			uint64_t score = (rs_score.size() ? rs_score[0]["score"].getUInt() : 0);
			if (score < qualifying) {
				throw JoinNotQualifiedException(score, qualifying);
			}
		}
		auto rs = db::query("SELECT team FROM team_membership WHERE nick=?", {snowflake_id});
		if (rs.size()) {
			for (auto& t : rs) {
				db::backgroundquery("UPDATE teams SET owner_id = ? WHERE name = ? AND owner_id IS NULL", {snowflake_id, team});
				return true;
			}
		}
		db::query("DELETE FROM team_membership WHERE nick=?", {snowflake_id});
		db::query("INSERT INTO team_membership (nick, team, joined, points_contributed) VALUES(?,?,now(),0)", {snowflake_id, team});
		db::query("UPDATE teams SET owner_id = ? WHERE name = ? AND owner_id IS NULL", {snowflake_id, team});
		return true;
	} else {
		return false;
	}
}

/* Update the streak for a player on a guild */
void change_streak(uint64_t snowflake_id, uint64_t guild_id, int score)
{
	db::backgroundquery("INSERT INTO streaks (nick, guild_id, streak) VALUES(?,?,?) ON DUPLICATE KEY UPDATE streak=?", {snowflake_id, guild_id, score, score});
	check_achievement("streak", snowflake_id, guild_id);
}

/* Get the current streak details for a player on a guild, and the best streak for the guild at present */
streak_t get_streak(uint64_t snowflake_id, uint64_t guild_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	streak_t s;
	db::resultset streak = db::query("SELECT nick, streak FROM streaks WHERE guild_id = ? ORDER BY streak DESC LIMIT 1", {guild_id});
	db::resultset ss2 = db::query("SELECT streak FROM streaks WHERE nick=? AND guild_id = ?", {snowflake_id, guild_id});
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

/* Update the streak for a player on a guild */
void change_streak(uint64_t snowflake_id, int score)
{
	db::backgroundquery("INSERT INTO global_streaks (nick, streak) VALUES(?,?) ON DUPLICATE KEY UPDATE streak=?", {snowflake_id, score, score});
}

/* Get the current streak details for a player on a guild, and the best streak for the guild at present */
streak_t get_streak(uint64_t snowflake_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	streak_t s;
	db::resultset streak = db::query("SELECT nick, streak FROM global_streaks ORDER BY streak DESC LIMIT 1");
	db::resultset ss2 = db::query("SELECT streak FROM global_streaks WHERE nick=?", {snowflake_id});
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

/* Returns true if a team name exists */
bool check_team_exists(const std::string &team)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::resultset r = db::query("SELECT name FROM teams WHERE name = ?", {team});
	return (r.size());
}

/* Add points to a team */
void add_team_points(const std::string &team, int points, uint64_t snowflake_id)
{
	// Replaced with direct db query for perforamance increase - 27Dec20
	db::backgroundquery("UPDATE teams SET score = score + ? WHERE name = ?", {points, team});
	if (snowflake_id) {
		db::backgroundquery("UPDATE team_membership SET points_contributed = points_contributed + ? WHERE nick = ?", {points, snowflake_id});
	}

}

/* Get the points of a team */
uint32_t get_team_points(const std::string &team)
{
	// Replaced with direct db query for performance increase - 27Dec20
	db::resultset r = db::query("SELECT score FROM teams WHERE name = ?", {team});
	if (r.size()) {
		return from_string<uint32_t>(r[0]["score"], std::dec);
	} else {
		return 0;
	}
}

void check_create_webhook(const guild_settings_t & s, TriviaModule* t, uint64_t channel_id)
{
	/* Create new webhook for a channel, or update existing webhook.
	 * Store webhook details to database for future use.
	 * This function checks the existing webhook is usable and if not, creates a new one.
	 */
	dpp::cluster* c = t->GetBot()->core;

	c->log(dpp::ll_debug, fmt::format("Create or update webhook for channel {}", channel_id));

	auto create_wh = [c, channel_id]() {
		dpp::webhook wh;
		wh.name = "TriviaBot";
		wh.channel_id = channel_id;
		wh.load_image(WEBHOOK_ICON, dpp::i_png, true);
		c->create_webhook(wh, [channel_id, c](const dpp::confirmation_callback_t& data) {
			if (!data.is_error()) {
				dpp::webhook new_wh = std::get<dpp::webhook>(data.value);
				std::string url = "https://discord.com/api/webhooks/" + std::to_string(new_wh.id) + "/" + dpp::utility::url_encode(new_wh.token);
				db::query("INSERT INTO channel_webhooks (channel_id, webhook_id, webhook) VALUES(?,?,?) ON DUPLICATE KEY UPDATE webhook_id = ?, webhook = ?", {new_wh.channel_id, new_wh.id, url, new_wh.id, url});
				c->log(dpp::ll_debug, fmt::format("New webhook created for channel {}: {}", channel_id, new_wh.id));
			} else {
				c->log(dpp::ll_debug, fmt::format("Error creating webhook for channel {}: {}", channel_id, data.get_error().message));
			}
		});
	};

	db::resultset existing_hook = db::query("SELECT * FROM channel_webhooks WHERE channel_id = ?", {channel_id});
	if (existing_hook.size()) {
		c->log(dpp::ll_debug, fmt::format("Existing webhook found for channel {}", channel_id));
		/* Check if existing webhook is still valid */
		uint64_t wid = 0;
		try {
			wid = std::stoull(existing_hook[0]["webhook_id"]);
		}
		catch (const std::exception&) {
			wid = 0;
		}
		c->get_webhook(wid, [create_wh, channel_id, c](const dpp::confirmation_callback_t& data) {
			if (!data.is_error()) {
				dpp::webhook existing_wh = std::get<dpp::webhook>(data.value);
				db::backgroundquery("UPDATE channel_webhooks SET webhook = ? WHERE webhook_id = ? AND channel_id = ?", {"https://discord.com/api/webhooks/" + std::to_string(existing_wh.id) + "/" + dpp::utility::url_encode(existing_wh.token), existing_wh.id, existing_wh.channel_id});
				c->log(dpp::ll_debug, fmt::format("Existing webhook still valid for channel {}", channel_id));
				return;
			} else {
				/* Create new webhook */
				c->log(dpp::ll_debug, fmt::format("Existing webhook no longer valid for channel {}: {}", channel_id, data.get_error().message));
				create_wh();
			}

		});
	} else {
		/* No webhook at all, create new webhook */
		c->log(dpp::ll_debug, fmt::format("No existing webhook for channel {}", channel_id));
		create_wh();
	}
}

void post_webhook(const std::string &webhook_url, const std::string &embed, uint64_t channel_id)
{
	std::lock_guard<std::mutex> fi(fafindex);
	std::lock_guard<std::mutex> fafguard(faflock[faf_index]);

	std::string host = webhook_url.substr(0, webhook_url.find("/api/"));
	std::string path = webhook_url.substr(host.length(), webhook_url.length());
	faf[faf_index].push({host, path, "{\"content\":\"\", \"embeds\":[" + embed + "]}", channel_id});
	faf_index++;
	if (faf_index > FIRE_AND_FORGET_QUEUES - 1) {
		faf_index = 0;
	}
}

