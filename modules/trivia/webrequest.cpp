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
#include "webrequest.h"
#include <sporks/stringops.h>

std::unordered_map<std::string, asio::ip::basic_resolver<asio::ip::tcp>::results_type> _resolver_cache;
asio::io_context * _io_context = nullptr;
std::string apikey;

void set_io_context(asio::io_context* ioc, const std::string &_apikey)
{
	_io_context = ioc;
	apikey = _apikey;
}

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

std::string web_request(const std::string &_host, const std::string &_path, const std::string &_body)
{
	websocketpp::http::parser::response hresponse;

	try
	{
		asio::ip::basic_resolver<asio::ip::tcp>::results_type r;

		const std::string & tar_host = _host;

		auto it = _resolver_cache.find(tar_host);
		if (it == _resolver_cache.end())
		{
			asio::ip::tcp::resolver resolver(*_io_context);
			r = resolver.resolve(tar_host, "80");
			_resolver_cache.emplace(tar_host, r);
		}
		else
			r = it->second;


		asio::ip::tcp::socket socket(*_io_context);
		asio::connect(socket, r);

		asio::streambuf request;
		std::ostream request_stream(&request);
		request_stream << (_body.empty() ? "GET" : "POST") << " " << _path << " HTTP/1.0\r\n";
		request_stream << "Host: " << tar_host << "\r\n";
		request_stream << "Accept: */*\r\n";
		request_stream << "User-Agent: TriviaBot (1.0.0)\r\n";
		request_stream << "Connection: close\r\n";
		request_stream << "Content-Length: " << _body.length() << "\r\n";
		request_stream << "Content-Type: text/plain\r\n";
		request_stream << "X-API-Auth: " << apikey << "\r\n\r\n";
		request_stream << _body;

		asio::write(socket, request);
		asio::streambuf response;
		asio::read_until(socket, response, "\r\n");
		std::stringstream response_content;
		response_content << &response;

		asio::error_code error;
		while (asio::read(socket, response, asio::transfer_at_least(1), error))
			response_content << &response;

		std::istringstream istrm(response_content.str());
		hresponse.consume(istrm);
	}
	catch (std::exception& e)
	{
		std::cout << "Exception: " << e.what() << "\n";
	}

	return hresponse.get_body();
}

std::string fetch_page(const std::string &_endpoint, const std::string &body)
{
	return web_request(BACKEND_HOST, fmt::format("/api/{}", _endpoint), body);
}

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

void cache_user(const aegis::user *_user, const aegis::guild *_guild, const aegis::user::guild_info* gi)
{
	std::unordered_map<aegis::snowflake, aegis::gateway::objects::role> roles = _guild->get_roles();
	std::stringstream body;
	/* Serialise the guilds roles into a space separated list for POST to the cache endpoint */
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
	fetch_page(fmt::format("?opt=cache&user_id={}&username={}&discrim={}&icon={}&guild_id={}&guild_name={}&guild_icon={}&owner_id={}&roles={}", _user->get_id().get(), url_encode(_user->get_username()), _user->get_discriminator(), url_encode(_user->get_avatar()),
_guild->get_id().get(), url_encode(_guild->get_name()), url_encode(_guild->get_icon()), _guild->get_owner().get(), url_encode(member_roles)), body.str());
}

std::vector<std::string> fetch_question(int64_t id)
{
	return to_list(fetch_page(fmt::format("?id={}", id)));
}

std::vector<std::string> fetch_shuffle_list()
{
	return to_list(fetch_page("?opt=shuffle"));
}

std::vector<std::string> get_disabled_list()
{
	return to_list(fetch_page("?opt=listdis"));
}

std::vector<std::string> fetch_insane_round()
{
	return to_list(fetch_page("?opt=insane"));
}

void enable_all_categories()
{
	fetch_page("?opt=enableall");
}

void send_hint(int64_t snowflake_id, const std::string &hint, uint32_t remaining)
{
	fetch_page(fmt::format("?opt=customhint&user_id={}&hint={}&remaining={}", snowflake_id, url_encode(hint), remaining));
}

std::string custom_command(const std::string &command, const std::string &parameters, int64_t user_id, int64_t channel_id, int64_t guild_id)
{
	return fetch_page(fmt::format("?opt=command&user_id={}&channel_id={}&command={}&guild_id={}&parameters={}", user_id, channel_id, guild_id, url_encode(command), url_encode(parameters)));
}

void enable_category(const std::string &cat)
{
	fetch_page(fmt::format("?opt=enable&catname={}", cat));
}

void disable_category(const std::string &cat)
{
	fetch_page(fmt::format("?opt=disable&catname={}", cat));
}

int32_t update_score_only(int64_t snowflake_id, int64_t guild_id, int score)
{
	return from_string<int32_t>(fetch_page(fmt::format("?opt=scoreonly&nick={}&score={}&guild_id={}", snowflake_id, score, guild_id)), std::dec);
}

void log_game_start(int64_t guild_id, int64_t channel_id, int64_t number_questions, bool quickfire, const std::string &channel_name, int64_t user_id)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	fetch_page(fmt::format("?opt=gamestart&guild_id={}&channel_id={}&questions={}&quickfire={}&user_id={}&channel_name={}&hostname={}", guild_id, channel_id, number_questions, quickfire, user_id, url_encode(channel_name), url_encode(hostname)));
}

void log_game_end(int64_t guild_id, int64_t channel_id)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	fetch_page(fmt::format("?opt=gameend&guild_id={}&channel_id={}&hostname={}", guild_id, channel_id, url_encode(hostname)));
}

void log_question_index(int64_t guild_id, int64_t channel_id, int32_t index, uint32_t streak, int64_t lastanswered)
{
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	fetch_page(fmt::format("?opt=gameindex&guild_id={}&channel_id={}&index={}&hostname={}&streak={}&lastanswered={}", guild_id, channel_id, index, url_encode(hostname), streak, lastanswered));
}

int32_t update_score(int64_t snowflake_id, int64_t guild_id, time_t recordtime, int64_t id, int score)
{
	return from_string<int32_t>(fetch_page(fmt::format("?opt=score&nick={}&recordtime={}&id={}&score={}&guild_id={}", snowflake_id, recordtime, id, score, guild_id)), std::dec);
}

json get_active(const std::string &hostname)
{
	std::string active = fetch_page(fmt::format("?opt=getactive&hostname={}", hostname));
	return json::parse(active);
}

int32_t get_total_questions()
{
	return from_string<int32_t>(fetch_page("?opt=total"), std::dec);
}

std::vector<std::string> get_top_ten(int64_t guild_id)
{
	return to_list(fetch_page(fmt::format("?opt=table&guild={}", guild_id)));
}

int32_t get_score_average(int64_t guild_id)
{
	return from_string<int32_t>(fetch_page(fmt::format("?opt=scoreavg&guild={}", guild_id)), std::dec);
}

int64_t get_day_winner(int64_t guild_id)
{
	return from_string<int32_t>(fetch_page(fmt::format("?opt=currenthalfop&guild={}", guild_id)), std::dec);
}

std::string get_current_team(int64_t snowflake_id)
{
	return fetch_page(fmt::format("?opt=currentteam&nick={}", snowflake_id));
}

void leave_team(int64_t snowflake_id)
{
	fetch_page(fmt::format("?opt=leaveteam&nick={}", snowflake_id));
}

bool join_team(int64_t snowflake_id, const std::string &team)
{
	std::string r = trim(fetch_page(fmt::format("?opt=jointeam&name={}", url_encode(team))));
	if (r == "__OK__") {
		fetch_page(fmt::format("?opt=setteam&nick={}&team={}", snowflake_id, url_encode(team)));
		return true;
	} else {
		return false;
	}
}

std::string get_rank(int64_t snowflake_id, int64_t guild_id)
{
	return trim(fetch_page(fmt::format("?opt=rank&nick={}&guild_id={}", snowflake_id, guild_id)));
}

void change_streak(int64_t snowflake_id, int64_t guild_id, int score)
{
	fetch_page(fmt::format("?opt=changestreak&nick={}&score={}&guild_id={}", snowflake_id, score, guild_id));
}

streak_t get_streak(int64_t snowflake_id, int64_t guild_id)
{
	streak_t s;
	std::vector<std::string> data = to_list(ReplaceString(fetch_page(fmt::format("?opt=getstreak&nick={}&guild_id={}", snowflake_id, guild_id)), "/", "\n"));
	if (data.size() == 3) {
		s.personalbest = data[0].empty() ? 0 : from_string<int32_t>(data[0], std::dec);
		s.topstreaker = data[1].empty() ? 0 : from_string<int64_t>(data[1], std::dec);
		s.bigstreak = data[2].empty() ? 0 : from_string<int32_t>(data[2], std::dec);
	} else {
		/* Failed to retrieve correctly formatted data */
		s.personalbest = 0;
		s.topstreaker = 0;
		s.bigstreak = 99999999;
	}
	return s;
}

std::string create_new_team(const std::string &teamname)
{
	return trim(fetch_page(fmt::format("?opt=createteam&name={}", url_encode(teamname))));
}

bool check_team_exists(const std::string &team)
{
	std::string r = trim(fetch_page(fmt::format("?opt=jointeam&name={}", url_encode(team))));
	return (r != "__OK__");
}

void add_team_points(const std::string &team, int points, int64_t snowflake_id)
{
	fetch_page(fmt::format("?opt=addteampoints&name={}&score={}&nick={}", url_encode(team), points, snowflake_id));
}

int32_t get_team_points(const std::string &team)
{
	return from_string<int32_t>(fetch_page(fmt::format("?opt=getteampoints&name={}", url_encode(team))), std::dec);
}

