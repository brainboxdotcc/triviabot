/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2020 Craig Edwards <support@brainbox.cc>
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
#include "numstrs.h"
#include "webrequest.h"

/**
 * Module class for trivia system
 */

class TriviaModule : public Module
{
	PCRE* notvowel;
	PCRE* number_tidy_dollars;
	PCRE* number_tidy_nodollars;
	PCRE* number_tidy_positive;
	PCRE* number_tidy_negative;
	PCRE* prefix_match;
	std::map<int64_t, state_t*> states;
	std::thread* presence_update;
	bool terminating;
	std::mutex states_mutex;
	time_t startup;
public:
	TriviaModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), terminating(false)
	{
		srand(time(NULL) * time(NULL));
		ml->Attach({ I_OnMessage, I_OnPresenceUpdate, I_OnChannelDelete, I_OnGuildDelete }, this);
		notvowel = new PCRE("/[^aeiou_]/", true);
		number_tidy_dollars = new PCRE("^([\\d\\,]+)\\s+dollars$");
		number_tidy_nodollars = new PCRE("^([\\d\\,]+)\\s+(.+?)$");
		number_tidy_positive = new PCRE("^[\\d\\,]+$");
		number_tidy_negative = new PCRE("^\\-[\\d\\,]+$");
		prefix_match = new PCRE("prefix");
		set_io_context(bot->io, Bot::GetConfig("apikey"));
		presence_update = new std::thread(&TriviaModule::UpdatePresenceLine, this);
		startup = time(NULL);
	}

	Bot* GetBot()
	{
		return bot;
	}

	virtual ~TriviaModule()
	{
		DisposeThread(presence_update);
		std::lock_guard<std::mutex> user_cache_lock(states_mutex);
		for (auto state = states.begin(); state != states.end(); ++state) {
			delete state->second;
		}
		states.clear();
		delete notvowel;
		delete number_tidy_dollars;
		delete number_tidy_nodollars;
		delete number_tidy_positive;
		delete number_tidy_negative;
		delete prefix_match;
	}


	virtual bool OnPresenceUpdate()
	{
		const aegis::shards::shard_mgr& s = bot->core.get_shard_mgr();
		const std::vector<std::unique_ptr<aegis::shards::shard>>& shards = s.get_shards();
		for (auto i = shards.begin(); i != shards.end(); ++i) {
			const aegis::shards::shard* shard = i->get();
			db::query("INSERT INTO infobot_shard_status (id, connected, online, uptime, transfer, transfer_compressed) VALUES('?','?','?','?','?','?') ON DUPLICATE KEY UPDATE connected = '?', online = '?', uptime = '?', transfer = '?', transfer_compressed = '?'",
				{
					shard->get_id(),
					shard->is_connected(),
					shard->is_online(),
					shard->uptime(),
					shard->get_transfer_u(),
					shard->get_transfer(),
					shard->is_connected(),
					shard->is_online(),
					shard->uptime(),
					shard->get_transfer_u(),
					shard->get_transfer()
				}
			);
		}
		return true;
	}

	virtual bool OnChannelDelete(const modevent::channel_delete cd)
	{
		bool one_deleted = false;
		do {
			std::lock_guard<std::mutex> user_cache_lock(states_mutex);
			for (auto state = states.begin(); state != states.end(); ++state) {
				if (state->second->channel_id == cd.channel.id.get()) {
					auto s = state->second;
					states.erase(state);
					delete s;
					one_deleted = true;
					break;
				}
			}
		} while (one_deleted);
		return true;
	}

	virtual bool OnGuildDelete(const modevent::guild_delete gd)
	{
		std::lock_guard<std::mutex> user_cache_lock(states_mutex);
		for (auto state = states.begin(); state != states.end(); ++state) {
			if (state->second->guild_id = gd.guild_id.get()) {
				auto s = state->second;
				states.erase(state);
				delete s;
				break;
			}
		}
		return true;
	}

	int64_t GetActiveGames()
	{
		int64_t a = 0;
		std::lock_guard<std::mutex> user_cache_lock(states_mutex);
		for (auto state = states.begin(); state != states.end(); ++state) {
			if (state->second->gamestate != TRIV_END) {
				++a;
			}
		}
		return a;
	}

	guild_settings_t GetGuildSettings(int64_t guild_id)
	{
		aegis::guild* guild = bot->core.find_guild(guild_id);
		if (guild == nullptr) {
			return guild_settings_t(guild_id, "!", {}, 3238819, false, false, false, 0, "");
		} else {
			db::resultset r = db::query("SELECT * FROM bot_guild_settings WHERE snowflake_id = ?", {guild_id});
			if (!r.empty()) {
				std::stringstream s(r[0]["moderator_roles"]);
				int64_t role_id;
				std::vector<int64_t> role_list;
				while ((s >> role_id)) {
					role_list.push_back(role_id);
				}
				return guild_settings_t(from_string<int64_t>(r[0]["snowflake_id"], std::dec), r[0]["prefix"], role_list, from_string<uint32_t>(r[0]["embedcolour"], std::dec), (r[0]["premium"] == "1"), (r[0]["only_mods_stop"] == "1"), (r[0]["role_reward_enabled"] == "1"), from_string<int64_t>(r[0]["role_reward_id"], std::dec), r[0]["custom_url"]);
			} else {
				db::query("INSERT INTO bot_guild_settings (snowflake_id) VALUES('?')", {guild_id});
				return guild_settings_t(guild_id, "!", {}, 3238819, false, false, false, 0, "");
			}
		}
	}

	/* Make a string safe to send as a JSON literal */
	std::string escape_json(const std::string &s) {
		std::ostringstream o;
		for (auto c = s.cbegin(); c != s.cend(); c++) {
			switch (*c) {
			case '"': o << "\\\""; break;
			case '\\': o << "\\\\"; break;
			case '\b': o << "\\b"; break;
			case '\f': o << "\\f"; break;
			case '\n': o << "\\n"; break;
			case '\r': o << "\\r"; break;
			case '\t': o << "\\t"; break;
			default:
				if ('\x00' <= *c && *c <= '\x1f') {
					o << "\\u"
					  << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
				} else {
					o << *c;
				}
			}
		}
		return o.str();
	}

	/* Create an embed from a JSON string and send it to a channel */
	void ProcessEmbed(const std::string &embed_json, int64_t channelID)
	{
		json embed;
		std::string cleaned_json = embed_json;
		/* Put unicode zero-width spaces in @everyone and @here */
		cleaned_json = ReplaceString(cleaned_json, "@everyone", "@‎everyone");
		cleaned_json = ReplaceString(cleaned_json, "@here", "@‎here");
		aegis::channel* channel = bot->core.find_channel(channelID);
		if (!channel) {
			return;
		}
		try {
			/* Tabs to spaces */
			cleaned_json = ReplaceString(cleaned_json, "\t", " ");
			embed = json::parse(cleaned_json);
		}
		catch (const std::exception &e) {
			if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
				channel->create_message("<:sporks_error:664735896251269130> I can't make an **embed** from this: ```js\n" + cleaned_json + "\n```**Error:** ``" + e.what() + "``");
				bot->sent_messages++;
			}
		}
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
			channel->create_message_embed("", embed);
			bot->sent_messages++;
		}
	}

	void SimpleEmbed(const std::string &emoji, const std::string &text, int64_t channelID, const std::string title = "")
	{
		aegis::channel* c = bot->core.find_channel(channelID);
		uint32_t colour = 3238819;
		if (c) {
			guild_settings_t settings = GetGuildSettings(c->get_guild().get_id().get());
			colour = settings.embedcolour;
			if (!title.empty()) {
				ProcessEmbed(fmt::format("{{\"title\":\"{}\",\"color\":{},\"description\":\"{} {}\"}}", escape_json(title), colour, emoji, escape_json(text)), channelID);
			} else {
				ProcessEmbed(fmt::format("{{\"color\":{},\"description\":\"{} {}\"}}", colour, emoji, escape_json(text)), channelID);
			}
		}
	}
	
	/* Send an embed containing one or more fields */
	void EmbedWithFields(const std::string &title, std::vector<field_t> fields, int64_t channelID)
	{
		aegis::channel* c = bot->core.find_channel(channelID);
		uint32_t colour = 3238819;
		if (c) {
			guild_settings_t settings = GetGuildSettings(c->get_guild().get_id().get());
			colour = settings.embedcolour;
		}
		std::string json = fmt::format("{{\"title\":\"{}\",\"color\":{},\"fields\":[", escape_json(title), colour);
		for (auto v = fields.begin(); v != fields.end(); ++v) {
			json += fmt::format("{{\"name\":\"{}\",\"value\":\"{}\",\"inline\":{}}}", escape_json(v->name), escape_json(v->value), v->_inline ? "true" : "false");
			auto n = v;
			if (++n != fields.end()) {
				json += ",";
			}
		}
		json += "],\"footer\":{\"link\":\"https://triviabot.co.uk/\",\"text\":\"Powered by TriviaBot\"}}";
		ProcessEmbed(json, channelID);
	}



	virtual std::string GetVersion()
	{
		/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
		std::string version = "$ModVer 0$";
		return "1.0." + version.substr(8,version.length() - 9);
	}

	virtual std::string GetDescription()
	{
		return "Trivia System";
	}

	int random(int min, int max)
	{
		return min + rand() % (( max + 1 ) - min);
	}

	std::string dec_to_roman(unsigned int decimal)
	{
		std::vector<int> numbers =  { 1, 4 ,5, 9, 10, 40, 50, 90, 100, 400, 500, 900, 1000 };
		std::vector<std::string> romans = { "I", "IV", "V", "IX", "X", "XL", "L", "XC", "C", "CD", "D", "CM", "M" };
		std::string result;
		for (int x = 12; x >= 0; x--) {
			while (decimal >= numbers[x]) {
				decimal -= numbers[x];
				result.append(romans[x]);
			}
		}
		return std::string("Roman numerals: ") + result;
	}

	std::string tidy_num(std::string num)
	{
		std::vector<std::string> param;
		if (number_tidy_dollars->Match(num, param)) {
			num = "$" + ReplaceString(param[1], ",", "");
		}
		if (num.length() > 1 && num[0] == '$') {
			num = ReplaceString(num, ",", "");
		}
		if (number_tidy_nodollars->Match(num, param)) {
			std::string numbers = param[1];
			std::string suffix = param[2];
			numbers = ReplaceString(numbers, ",", "");
			num = numbers + " " + suffix;
		}
		if (number_tidy_positive->Match(num) || number_tidy_negative->Match(num)) {
			num = ReplaceString(num, ",", "");
		}
		return num;
	}

	void UpdatePresenceLine()
	{
		while (!terminating) {
			sleep(30);
			int32_t questions = get_total_questions();
			bot->core.update_presence(fmt::format("Trivia! {} questions, {} active games on {} servers through {} shards", Comma(questions), Comma(GetActiveGames()), Comma(bot->core.get_guild_count()), Comma(bot->core.shard_max_count)), aegis::gateway::objects::activity::Game);
		}
	}

	std::string conv_num(std::string datain)
	{
		std::map<std::string, int> nn = {
			{ "one", 1 },
			{ "two", 2 },
			{ "three", 3 },
			{ "four", 4 },
			{ "five", 5 },
			{ "six", 6 },
			{ "seven", 7 },
			{ "eight", 8 },
			{ "nine", 9 },
			{ "ten", 10 },
			{ "eleven", 11 },
			{ "twelve", 12 },
			{ "thirteen", 13 },
			{ "fourteen", 14 },
			{ "forteen", 14 },
			{ "fifteen", 15 },
			{ "sixteen", 16 },
			{ "seventeen", 17 },
			{ "eighteen", 18 },
			{ "nineteen", 19 },
			{ "twenty", 20 },
			{ "thirty", 30 },
			{ "fourty", 40 },
			{ "forty", 40 },
			{ "fifty", 50 },
			{ "sixty", 60 },
			{ "seventy", 70 },
			{ "eighty", 80 },
			{ "ninety", 90 }
		};
		if (datain.empty()) {
			datain = "zero";
		}
		datain = ReplaceString(datain, "  ", " ");
		datain = ReplaceString(datain, "-", "");
		datain = ReplaceString(datain, " and ", " ");
		int last = 0;
		int initial = 0;
		std::string currency;
		std::vector<std::string> nums;
		std::stringstream str(datain);
		std::string v;
		while ((str >> v)) {
			nums.push_back(v);
		}
		for (auto x = nums.begin(); x != nums.end(); ++x) {
			if (nn.find(lowercase(*x)) == nn.end() && !PCRE("million", true).Match(*x) && !PCRE("thousand", true).Match(*x) && !PCRE("hundred", true).Match(*x) && !PCRE("dollars", true).Match(*x)) {
				return "0";
			}
		}
		for (auto next = nums.begin(); next != nums.end(); ++next) {
			std::string nextnum = lowercase(*next);
			auto ahead = next;
			ahead++;
			std::string lookahead = "";
			if (ahead != nums.end()) {
				lookahead = *ahead;
			}
			if (nn.find(nextnum) != nn.end()) {
				last = nn.find(nextnum)->second;
			}
			if (PCRE("dollars", true).Match(nextnum)) {
				currency = "$";
				last = 0;
			}
			if (!PCRE("hundred|thousand|million", true).Match(lookahead)) {
				initial += last;
				last = 0;
			} else {
				if (PCRE("hundred", true).Match(lookahead)) {
					initial += last * 100;
					last = 0;
				} else if (PCRE("thousand", true).Match(lookahead)) {
					initial += last * 1000;
					last = 0;
				} else if (PCRE("million", true).Match(lookahead)) {
					initial += last * 1000000;
					last = 0;
				}
			}
		}
		return currency + std::to_string(initial);
	}

	std::string scramble(std::string str)
	{
		int x = str.length();
		for(int y = x; y > 0; y--) 
		{ 
			int pos = rand()%x;
			char tmp = str[y-1];
			str[y-1] = str[pos];
			str[pos] = tmp;
		}
		return std::string("Scrambled answer: ") + lowercase(str);
	}

	bool isVowel(char c) 
	{ 
		return (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'); 
	}

	std::string piglatinword(std::string s) 
	{ 
		int len = s.length();
		int index = -1; 
		for (int i = 0; i < len; i++) { 
			if (isVowel(s[i])) { 
				index = i; 
				break; 
			} 
		} 
		if (index == -1) 
			return s;
  
		return s.substr(index) + s.substr(0, index) + "ay"; 
	}

	std::string piglatin(std::string s) {
		std::stringstream str(s);
		std::string word;
		std::string ret;
		while ((str >> word)) {
			ret.append(piglatinword(word)).append(" ");
		}
		return std::string("Pig latin: ") + lowercase(ret);
	}

	std::string letterlong(std::string text)
	{
		text = ReplaceString(text, " ", "");
		if (text.length()) {
			return fmt::format("{} letters long. Starts with '{}' and ends with '{}'.", text.length(), text[0], text[text.length() - 1]);
		} else {
			return "An empty answer";
		}
	}

	std::string vowelcount(std::string text)
	{
		text = ReplaceString(lowercase(text), " ", "");
		int v = 0;
		for (auto x = text.begin(); x != text.end(); ++x) {
			if (isVowel(*x)) {
				++v;
			}
		}
		return fmt::format("{} letters long and contains {} vowels.", text.length(), v);
	}

	std::string numbertoname(int64_t number)
	{
		if (numstrs.find(number) != numstrs.end()) {
			return numstrs.find(number)->second;
		}
		return std::to_string(number);
	}

	std::string GetNearestNumber(int64_t number)
	{
		for (numstrs_t::reverse_iterator x = numstrs.rbegin(); x != numstrs.rend(); ++x) {
			if (x->first <= number) {
				return x->second;
			}
		}
		return "0";
	}

	int64_t GetNearestNumberVal(int64_t number)
	{
		for (numstrs_t::reverse_iterator x = numstrs.rbegin(); x != numstrs.rend(); ++x) {
			if (x->first <= number) {
				return x->first;
			}
		}
		return 0;
	}

	int min3(int x, int y, int z) 
	{ 
		return std::min(std::min(x, y), z); 
	} 
  
	int levenstein(std::string str1, std::string str2) 
	{
		// Create a table to store results of subproblems
		str1 = uppercase(str1);
		str2 = uppercase(str2);
		int m = str1.length();
		int n = str2.length();
		int dp[m + 1][n + 1];

		// Fill d[][] in bottom up manner
		for (int i = 0; i <= m; i++) {
			for (int j = 0; j <= n; j++) {
				// If first string is empty, only option is to
				// insert all characters of second string
				if (i == 0)
					dp[i][j] = j; // Min. operations = j

				// If second string is empty, only option is to
				// remove all characters of second string
				else if (j == 0)
					dp[i][j] = i; // Min. operations = i

				// If last characters are same, ignore last char
				// and recur for remaining string
				else if (str1[i - 1] == str2[j - 1])
					dp[i][j] = dp[i - 1][j - 1];

				// If the last character is different, consider all
				// possibilities and find the minimum
				else
					dp[i][j] = 1 + min3(dp[i][j - 1], // Insert
						   dp[i - 1][j], // Remove
						   dp[i - 1][j - 1]); // Replace
			}
		}
		return dp[m][n]; 
	}

	bool is_number(const std::string &s)
	{
		return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
	}

	std::string MakeFirstHint(std::string s, bool indollars = false)
	{
		std::string Q;
		if (is_number(s)) {
			int64_t n = from_string<int64_t>(s, std::dec);
			while (GetNearestNumberVal(n) != 0 && n > 0) {
				Q.append(GetNearestNumber(n)).append(", plus ");
				n -= GetNearestNumberVal(n);
			}
			if (n > 0) {
				Q.append(numbertoname(n));
			}
			Q = Q.substr(0, Q.length() - 7);
		}
		if (Q.empty()) {
			return "The lowest non-negative number";
		}
		if (indollars) {
			return Q + ", in DOLLARS";
		} else {
			return Q;
		}
	}

	void t()
	{
		std::cout << "\n\nMakeFirstHint(12345): " << MakeFirstHint("12345") << "\n";
		std::cout << "MakeFirstHint(0): " << MakeFirstHint("0") << "\n";
		std::cout << "dec_to_roman(15): " << dec_to_roman(15) << "\n";
		std::cout << "conv_num('two thousand one hundred and fifty four'): " << conv_num("two thousand one hundred and fifty four") << "\n";
		std::cout << "conv_num('five'): " << conv_num("five") << "\n";
		std::cout << "conf_num('ten pin bowling'): " << conv_num("ten pin bowling") << "\n";
		std::cout << "conv_num('zero'): " << conv_num("zero") << "\n";
		std::cout << "scramble('abcdef'): " << scramble("abcdef") <<"\n";
		std::cout << "scramble('A'): " << scramble("A") << "\n";
       		std::cout << "piglatin('easy with the pig latin my friend'): " << piglatin("easy with the pig latin my friend") << "\n";
		std::cout << "conv_num('one million dollars'): " << conv_num("one million dollars") << "\n";
		std::cout << "tidy_num('$1,000,000'): " << tidy_num("$1,000,000") << "\n";
		std::cout << "tidy_num('1,000'): " << tidy_num("1,000") << "\n";
		std::cout << "tidy_num('1000'): " << tidy_num("1000") << "\n";
		std::cout << "tidy_num('asdfghjk'): " << tidy_num("asdfghjk") << "\n";
		std::cout << "tidy_num('abc def ghi'): " << tidy_num("abc def ghi") << "\n";
		std::cout << "tidy_num('1000 dollars') " << tidy_num("1000 dollars") << "\n";
		std::cout << "tidy_num('1,000 dollars') " << tidy_num("1,000 dollars") << "\n";
		std::cout << "tidy_num('1,000 armadillos') " << tidy_num("1,000 armadillos") << "\n";
		std::cout << "tidy_num('27 feet') " << tidy_num("27 feet") << "\n";
		std::cout << "tidy_num('twenty seven feet') " << tidy_num("twenty seven feet") << "\n";
		std::cout << "letterlong('a herd of gnus') " << letterlong("a herd of gnus") << "\n";
		std::cout << "vowelcount('a herd of gnus') " << vowelcount("a herd of gnus") << "\n";
		std::cout << "levenstein('a herd of cows','a herd of wocs') " << levenstein("a herd of cows","a herd of wocs") << "\n";
		std::cout << "levenstein('Cows','coWz')  " << levenstein("Cows","coWz") << "\n";
		exit(0);
	}

	void do_insane_round(state_t* state)
	{
		bot->core.log->debug("do_insane_round: G:{} C:{}", state->guild_id, state->channel_id);

		if (state->round >= state->numquestions) {
			state->gamestate = TRIV_END;
			state->score = 0;
			return;
		}

		std::vector<std::string> answers = fetch_insane_round();
		state->insane = {};
		for (auto n = answers.begin(); n != answers.end(); ++n) {
			if (n == answers.begin()) {
				state->curr_question = trim(*n);
			} else {
				if (*n != "***END***") {
					state->insane[lowercase(trim(*n))] = true;
				}
			}
		}
		state->insane_left = state->insane.size();
		state->insane_num = state->insane.size();
		state->gamestate = TRIV_FIRST_HINT;

		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			EmbedWithFields(fmt::format(":question: Question {} of {}", state->round, state->numquestions - 1), {{"Insane Round!", fmt::format("Total of {} possible answers", state->insane_num), false}, {"Question", state->curr_question, false}}, c->get_id().get());
		} else {
			bot->core.log->warn("do_insane_round(): Channel {} was deleted", state->channel_id);
		}
	}

	void do_normal_round(state_t* state)
	{
		bot->core.log->debug("do_normal_round: G:{} C:{}", state->guild_id, state->channel_id);

		if (state->round >= state->numquestions) {
			state->gamestate = TRIV_END;
			state->score = 0;
			return;
		}

		bool valid = false;
		int32_t tries = 0;

		do {
			state->curr_qid = 0;
			bot->core.log->debug("do_normal_round: fetch_question: '{}'", state->shuffle_list[state->round - 1]);
			std::vector<std::string> data = fetch_question(from_string<int64_t>(state->shuffle_list[state->round - 1], std::dec));
			if (data.size() >= 10) {
				state->curr_qid = from_string<int64_t>(data[0], std::dec);
				state->curr_question = data[1];
				state->curr_answer = data[2];
				state->curr_customhint1 = data[3];
				state->curr_customhint2 = data[4];
				state->curr_category = data[5];
				state->curr_lastasked = from_string<time_t>(data[6],std::dec);
				state->curr_timesasked = from_string<int32_t>(data[7], std::dec);
				state->curr_lastcorrect = data[8];
				state->recordtime = from_string<time_t>(data[9],std::dec);
				valid = true;
			} else {
				bot->core.log->debug("do_normal_round: Invalid question response size {} retrieving question {}", data.size(), state->shuffle_list[state->round - 1]);
				sleep(1);
				tries++;
				valid = false;
			}
		} while (!valid && tries <= 3);

		if (state->curr_qid == 0) {
			state->gamestate = TRIV_END;
			state->score = 0;
			state->curr_answer = "";
			bot->core.log->warn("do_normal_round(): Attempted to retrieve question id {} but got a malformed response. Round was aborted.", state->shuffle_list[state->round - 1]);
			aegis::channel* c = bot->core.find_channel(state->channel_id);
			if (c) {
				EmbedWithFields(fmt::format(":question: Couldn't fetch question from API"), {{"This seems spoopy", "[Please contact the developers](https://discord.gg/brainbox)", false}, {"Round stopping", "Error has stopped play!", false}}, c->get_id().get());
			}
			return;
		}

		if (state->curr_question != "") {
			state->asktime = time(NULL);
			state->curr_answer = trim(state->curr_answer);
			std::string t = conv_num(state->curr_answer);
			if (is_number(t) && t != "0") {
				state->curr_answer = t;
			}
			state->curr_answer = tidy_num(state->curr_answer);
			/* Handle hints */
			if (state->curr_customhint1.empty()) {
				/* No custom first hint, build one */
				state->curr_customhint1 = state->curr_answer;
				if (is_number(state->curr_customhint1)) {
					state->curr_customhint1 = MakeFirstHint(state->curr_customhint1);
				} else {
					int32_t r = random(1, 12);
					if (r <= 4) {
						/* Leave only capital letters */
						for (int x = 0; x < state->curr_customhint1.length(); ++x) {
							if ((state->curr_customhint1[x] >= 'a' && state->curr_customhint1[x] <= 'z') || state->curr_customhint1[x] == '1' || state->curr_customhint1[x] == '3' || state->curr_customhint1[x] == '5' || state->curr_customhint1[x]  == '7' || state->curr_customhint1[x] == '9') {
								state->curr_customhint1[x] = '#';
							}
						}
					} else if (r >= 5 && r <= 8) {
						state->curr_customhint1 = letterlong(state->curr_customhint1);
					} else {
						state->curr_customhint1 = scramble(state->curr_customhint1);
					}
				}
			}
			if (state->curr_customhint2.empty()) {
				/* No custom second hint, build one */
				state->curr_customhint2 = state->curr_answer;
				if (is_number(state->curr_customhint2) || PCRE("^\\$(\\d+)$").Match(state->curr_customhint2)) {
					std::string currency = "";
					std::vector<std::string> matches;
					if (PCRE("^\\$(\\d+)$").Match(state->curr_customhint2, matches)) {
						state->curr_customhint2 = matches[1];
						currency = "$";
					}
					int32_t r = random(1, 13);
					if ((r < 3 && from_string<int32_t>(state->curr_customhint2, std::dec) <= 10000)) {
						state->curr_customhint2 = dec_to_roman(from_string<unsigned int>(state->curr_customhint2, std::dec));
					} else if ((r >= 3 && r < 6) || from_string<int32_t>(state->curr_customhint2, std::dec) > 10000) {
						state->curr_customhint2 = fmt::format("Hexadecimal: {0:x}", from_string<int32_t>(state->curr_customhint2, std::dec));
					} else if (r >= 6 && r <= 10) {
						state->curr_customhint2 = fmt::format("Octal: {0:o}", from_string<int32_t>(state->curr_customhint2, std::dec));
					} else {
						state->curr_customhint2 = fmt::format("Binary: {0:b}", from_string<int32_t>(state->curr_customhint2, std::dec));
					}
				} else {
					int32_t r = random(1, 12);
					if (r <= 4) {
						/* Transpose only the vowels */
						for (int x = 0; x < state->curr_customhint2.length(); ++x) {
							if (toupper(state->curr_customhint2[x]) == 'A' || toupper(state->curr_customhint2[x]) == 'E' || toupper(state->curr_customhint2[x]) == 'I' || toupper(state->curr_customhint2[x]) == 'O' || toupper(state->curr_customhint2[x]) == 'U' || toupper(state->curr_customhint2[x]) == '2' || toupper(state->curr_customhint2[x]) == '4' || toupper(state->curr_customhint2[x]) == '6' || toupper(state->curr_customhint2[x]) == '8' || toupper(state->curr_customhint2[x]) == '0') {
								state->curr_customhint2[x] = '#';
							}
						}
					} else if (r >= 5 && r <= 6) {
						state->curr_customhint2 = vowelcount(state->curr_customhint2);
					} else {
						state->curr_customhint2 = piglatin(state->curr_customhint2);
					}

				}
			}

			aegis::channel* c = bot->core.find_channel(state->channel_id);
			if (c) {
				EmbedWithFields(fmt::format(":question: Question {} of {}", state->round, state->numquestions - 1), {{"Category", state->curr_category, false}, {"Question", state->curr_question, false}}, c->get_id().get());
			} else {
				bot->core.log->warn("do_normal_round(): Channel {} was deleted", state->channel_id);
			}

		} else {
			aegis::channel* c = bot->core.find_channel(state->channel_id);
			if (c) {
				SimpleEmbed(":ghost:", "Something's up, got a question with no text! This shouldn't happen...", c->get_id().get(), "Second Hint");
			} else {
				bot->core.log->debug("do_normal_round: G:{} C:{} channel vanished! -- question with no text!", state->guild_id, state->channel_id);
			}
		}

		state->score = (state->interval == TRIV_INTERVAL ? 4 : 8);
		/* Advance state to first hint */
		state->gamestate = TRIV_FIRST_HINT;

	}

	void do_first_hint(state_t* state)
	{
		bot->core.log->debug("do_first_hint: G:{} C:{}", state->guild_id, state->channel_id);
		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			if (state->round % 10 == 0) {
				/* Insane round countdown */
				SimpleEmbed(":clock10:", fmt::format("You have **{}** seconds remaining!", state->interval * 2), c->get_id().get());
			} else {
				/* First hint, not insane round */
				SimpleEmbed(":clock10:", state->curr_customhint1, c->get_id().get(), "First Hint");
			}
		} else {
			 bot->core.log->warn("do_first_hint(): Channel {} was deleted", state->channel_id);
		}
		state->gamestate = TRIV_SECOND_HINT;
		state->score = (state->interval == TRIV_INTERVAL ? 2 : 4);
	}

	void do_second_hint(state_t* state)
	{
		bot->core.log->debug("do_second_hint: G:{} C:{}", state->guild_id, state->channel_id);
		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			if (state->round % 10 == 0) {
				/* Insane round countdown */
				SimpleEmbed(":clock1030:", fmt::format("You have **{}** seconds remaining!", state->interval), c->get_id().get());
			} else {
				/* Second hint, not insane round */
				SimpleEmbed(":clock1030:", state->curr_customhint2, c->get_id().get(), "Second Hint");
			}
		} else {
			 bot->core.log->warn("do_second_hint: Channel {} was deleted", state->channel_id);
		}
		state->gamestate = TRIV_TIME_UP;
		state->score = (state->interval == TRIV_INTERVAL ? 1 : 2);
	}

	void do_time_up(state_t* state)
	{
		bot->core.log->debug("do_time_up: G:{} C:{}", state->guild_id, state->channel_id);

		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			if (state->round % 10 == 0) {
				int32_t found = state->insane_num - state->insane_left;
				SimpleEmbed(":alarm_clock:", fmt::format("**{}** answers were found!", found), c->get_id().get(), "Time Up!");
			} else {
				SimpleEmbed(":alarm_clock:", fmt::format("The answer was: **{}**", state->curr_answer), c->get_id().get(), "Out of time!");
			}
		}
		if (state->streak > 1 && state->last_to_answer) {
			aegis::user* u = bot->core.find_user(state->last_to_answer);
			if (u) {
				SimpleEmbed(":octagonal_sign:", fmt::format("**{}**'s streak of **{}** answers in a row comes to a grinding halt!", u->get_username(), state->streak), c->get_id().get());
			}
		}

		state->curr_answer = "";
		state->last_to_answer = 0;
		state->streak = 1;

		if (c && state->round <= state->numquestions - 1) {
			SimpleEmbed("<a:loading:658667224067735562>", fmt::format("Next question coming up in about **{}** seconds...", state->interval), c->get_id().get(), "A little time to rest your fingers...");
		}

		state->gamestate = (state->round > state->numquestions ? TRIV_END : TRIV_ASK_QUESTION);
		state->round++;
		state->score = 0;
	}

	void do_answer_correct(state_t* state)
	{
		bot->core.log->debug("do_answer_correct: G:{} C:{}", state->guild_id, state->channel_id);

		aegis::channel* c = bot->core.find_channel(state->channel_id);

		state->round++;
		state->score = 0;

		if (state->round <= state->numquestions - 1) {
			if (c) {
				SimpleEmbed("<a:loading:658667224067735562>", fmt::format("Next question coming up in about **{}** seconds...", state->interval), c->get_id().get(), "A little time to rest your fingers...");
			} else {
				bot->core.log->warn("do_answer_correct(): Channel {} was deleted", state->channel_id);
			}
			state->gamestate = TRIV_ASK_QUESTION;
		} else {
			state->gamestate = TRIV_END;
		}
	}

	void do_end_game(state_t* state)
	{
		bot->core.log->debug("do_end_game: G:{} C:{}", state->guild_id, state->channel_id);

		aegis::channel* c = bot->core.find_channel(state->channel_id);
		if (c) {
			bot->core.log->info("End of game on guild {}, channel {} after {} seconds", state->guild_id, state->channel_id, time(NULL) - state->start_time);
			SimpleEmbed(":stop_button:", fmt::format("End of round of **{}** questions", state->numquestions - 1), c->get_id().get(), "End of the round");
			show_stats(c->get_guild().get_id(), state->channel_id);
		} else {
			bot->core.log->warn("do_end_game(): Channel {} was deleted", state->channel_id);
		}
		state->terminating = true;
	}

	void show_stats(int64_t guild_id, int64_t channel_id)
	{
		std::vector<std::string> topten = get_top_ten(guild_id);
		size_t count = 1;
		std::string msg;
		std::vector<field_t> fields;
		for(auto r = topten.begin(); r != topten.end(); ++r) {
			std::stringstream score(*r);
			std::string points;
			int64_t snowflake_id;
			score >> points;
			score >> snowflake_id;
			aegis::user* u = bot->core.find_user(snowflake_id);
			if (u) {
				msg.append(fmt::format("{}. **{}** ({})\n", count++, u->get_full_name(), points));
			} else {
				msg.append(fmt::format("{}. **Deleted User#0000** ({})\n", count++, points));
			}
		}
		if (msg.empty()) {
			msg = "Nobody has played here today! :cry:";
		}
		aegis::channel* c = bot->core.find_channel(channel_id);
		if (c) {
			guild_settings_t settings = GetGuildSettings(guild_id);
			if (settings.premium && !settings.custom_url.empty()) {
				EmbedWithFields("Trivia Leaderboard", {{"Today's Top Ten", msg, false}, {"More information", fmt::format("[View server leaderboards](https://triviabot.co.uk/stats/{})\nDaily scores reset at midnight GMT.", settings.custom_url), false}}, c->get_id().get());
			} else {
				EmbedWithFields("Trivia Leaderboard", {{"Today's Top Ten", msg, false}, {"More information", fmt::format("[View server leaderboards](https://triviabot.co.uk/stats/{})\nDaily scores reset at midnight GMT.", guild_id), false}}, c->get_id().get());
			}
		}
	}

	void Tick(state_t* state)
	{
		if (state->terminating) {
			return;
		}

		uint32_t waits = 0;
		while ((bot->core.find_guild(state->guild_id) == nullptr || bot->core.find_channel(state->channel_id) == nullptr) && !state->terminating) {
			bot->core.log->warn("Guild or channel are missing!!! Waiting 5 seconds for connection to re-establish to guild/channel: G:{} C:{}", state->guild_id, state->channel_id);
			sleep(5);
			if (waits++ > 30) {
				bot->core.log->warn("Waited too long for re-connection of G:{} C:{}, ending round.", state->guild_id, state->channel_id);
				state->terminating = true;
			}
		}

		if (!state->terminating) {
			switch (state->gamestate) {
				case TRIV_ASK_QUESTION:
					if (state->round % 10 == 0) {
						do_insane_round(state);
					} else {
						do_normal_round(state);
					}
				break;
				case TRIV_FIRST_HINT:
					do_first_hint(state);
				break;
				case TRIV_SECOND_HINT:
					do_second_hint(state);
				break;
				case TRIV_TIME_UP:
					do_time_up(state);
				break;
				case TRIV_ANSWER_CORRECT:
					do_answer_correct(state);
				break;
					case TRIV_END:
					do_end_game(state);
				break;
				default:
					bot->core.log->warn("Invalid state '{}', ending round.", state->gamestate);
					state->terminating = true;
				break;
			}
		}
	}

	void DisposeThread(std::thread* t)
	{
		bot->DisposeThread(t);
	}

	virtual bool OnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
	{
		std::vector<std::string> param;
		std::string botusername = bot->user.username;
		aegis::gateway::objects::message msg = message.msg;
		const aegis::user& user = message.get_user();
		bool game_in_progress = false;

		std::string trivia_message = clean_message;
		int x = from_string<int>(conv_num(clean_message), std::dec);
		if (x > 0) {
			trivia_message = conv_num(clean_message);
		}
		trivia_message = tidy_num(trivia_message);

		/* Retrieve current state for channel, if there is no state object, no game is in progress */
		int64_t channel_id = msg.get_channel_id().get();
		state_t* state = nullptr;
		int64_t guild_id = 0;

		aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
		if (c) {
			guild_id = c->get_guild().get_id().get();
		}
		guild_settings_t settings = GetGuildSettings(guild_id);

		{
			std::lock_guard<std::mutex> user_cache_lock(states_mutex);
			auto state_iter = states.find(channel_id);
			if (state_iter != states.end()) {
				state = state_iter->second;
				/* Tombstoned session */
				if (state->terminating) {
					delete state;
					states.erase(state_iter);
					state = nullptr;
				} else {
					game_in_progress = true;
				}
			}
		}

		/* Check for moderator status - first check if owner */
		bool moderator = (c->get_guild().get_owner() == user.get_id());
		/* Now iterate the list of moderator roles from settings */
		for (auto x = settings.moderator_roles.begin(); x != settings.moderator_roles.end(); ++x) {
			if (c->get_guild().member_has_role(user.get_id(), *x)) {
				moderator = true;
				break;
			}
		}

		if (mentioned && prefix_match->Match(clean_message)) {
			c->create_message("My prefix on this server is ``" + settings.prefix + "``. Type ``" + settings.prefix + "help`` for help.");
			return false;
		}

		if (game_in_progress) {
			if (state->gamestate == TRIV_ASK_QUESTION || state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) {
				
				if (state->round % 10 == 0) {
					/* Insane round */
					auto i = state->insane.find(lowercase(trivia_message));
					if (i != state->insane.end()) {
						state->insane.erase(i);
						aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
						aegis::user::guild_info& gi = bot->core.find_user(user.get_id())->get_guild_info(c->get_guild().get_id());
						cache_user(&user, &c->get_guild(), &gi);

						if (--state->insane_left < 1) {
							if (c) {
								SimpleEmbed(":thumbsup:", fmt::format("**{}** found the last answer!", user.get_username()), c->get_id().get());
							}
							if (state->round <= state->numquestions - 1) {
								state->round++;
								state->gamestate = TRIV_ANSWER_CORRECT;
							} else {
								state->gamestate = TRIV_END;
							}
						} else {
							if (c) {
								SimpleEmbed(":thumbsup:", fmt::format("**{}** was correct with **{}**! **{}** answers remaining out of **{}**.", user.get_username(), trivia_message, state->insane_left, state->insane_num), c->get_id().get());
							}
						}
						update_score_only(user.get_id().get(), state->guild_id, 1);
					}
				} else {
					/* Normal round */

					/* Answer on channel is an exact match for the current answer and/or it is numeric, OR, it's non-numeric and has a levenstein distance near enough to the current answer (account for misspellings) */
					if (!state->curr_answer.empty() && ((trivia_message.length() >= state->curr_answer.length() && lowercase(state->curr_answer) == lowercase(trivia_message)) || (!PCRE("^\\$(\\d+)$").Match(state->curr_answer) && !PCRE("^(\\d+)$").Match(state->curr_answer) && levenstein(trivia_message, state->curr_answer) < 2))) {
						/* Correct answer */
						state->gamestate = TRIV_ANSWER_CORRECT;
						time_t time_to_answer = time(NULL) - state->asktime;
						std::string pts = (state->score > 1 ? "points" : "point");
						time_t submit_time = state->recordtime;
						int32_t score = state->score;


						std::string ans_message;
						ans_message.append(fmt::format("The answer was **{}**. You gain **{}** {} for answering in **{}** seconds!", state->curr_answer, score, pts, time_to_answer));
						if (time_to_answer < state->recordtime) {
							ans_message.append(fmt::format("\n**{}** has broken the record time for this question!", user.get_username()));
							submit_time = time_to_answer;
						}
						int32_t newscore = update_score(user.get_id().get(), state->guild_id, submit_time, state->curr_qid, score);
						ans_message.append(fmt::format("\n**{}**'s score is now **{}**.", user.get_username(), newscore ? newscore : score));

						std::string teamname = get_current_team(user.get_id().get());
						if (!empty(teamname) && teamname != "!NOTEAM") {
							add_team_points(teamname, score, user.get_id().get());
							int32_t newteamscore = get_team_points(teamname);
							ans_message.append(fmt::format("\nTeam **{}** also gains **{}** {} and is now on **{}**", teamname, score, pts, newteamscore));
						}

						if (state->last_to_answer == user.get_id().get()) {
							/* Amend current streak */
							state->streak++;
							ans_message.append(fmt::format("\n**{}** is on a streak! **{}** questions and counting", user.get_username(), state->streak));
							streak_t s = get_streak(user.get_id().get(), state->guild_id);
							if (state->streak > s.personalbest) {
								ans_message.append(fmt::format(", and has beaten their personal best!"));
								change_streak(user.get_id().get(), state->guild_id, state->streak);
							} else {
								ans_message.append(fmt::format(", but has some way to go yet before they beat their personal best of **{}**", s.personalbest));
							}
							if (state->streak > s.bigstreak && s.topstreaker != user.get_id().get()) {
								ans_message.append(fmt::format("\n**{}** just beat <@{}>'s record streak of {} answers!", user.get_username(), s.topstreaker, state->streak));
							}
						} else if (state->streak > 1 && !state->last_to_answer) {
							ans_message.append(fmt::format("\n**{}** just ended <@{}>'s record streak of {} answers in a row!", user.get_username(), state->last_to_answer, state->streak));
						}

						/* Update last person to answer */
						state->last_to_answer = user.get_id().get();


						aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
						if (c) {
							SimpleEmbed(":thumbsup:", ans_message, c->get_id().get(), fmt::format("Correct, {}!", user.get_username()));
						}



						state->curr_answer = "";
					}
				}

			}
		}

		if (lowercase(clean_message.substr(0, settings.prefix.length())) == lowercase(settings.prefix)) {
			/* Command */
			std::string command = clean_message.substr(settings.prefix.length(), clean_message.length() - settings.prefix.length());
			aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
			if (c) {

				bot->core.log->info("CMD (USER={}, GUILD={}): <{}> {}", user.get_id().get(), c->get_guild().get_id().get(), user.get_username(), clean_message);

				aegis::user::guild_info& gi = bot->core.find_user(user.get_id())->get_guild_info(c->get_guild().get_id());
				cache_user(&user, &c->get_guild(), &gi);

				if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {


					std::stringstream tokens(command);

					std::string base_command;
					std::string subcommand;
				
					tokens >> base_command;

					if (lowercase(base_command) == "help") {
						std::string section;
						tokens >> section;
						GetHelp(section, message.msg.get_channel_id().get(), bot->user.username, bot->user.id.get(), msg.get_user().get_username(), msg.get_user().get_id().get(), false, settings.embedcolour);
					} else if (lowercase(base_command) == "trivia") {
						tokens >> subcommand;
						subcommand = lowercase(subcommand);

						if (subcommand == "start" || subcommand == "quickfire") {

							int32_t questions;
							tokens >> questions;
							bool quickfire = (subcommand == "quickfire");

							json document;
							std::ifstream configfile("../config.json");
							configfile >> document;
							json shitlist = document["shitlist"];
							aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
							for (auto entry = shitlist.begin(); entry != shitlist.end(); ++entry) {
								int64_t sl_guild_id = from_string<int64_t>(entry->get<std::string>(), std::dec);
						                if (c->get_guild().get_id().get() == sl_guild_id) {
									SimpleEmbed(":warning:", fmt::format("**{}**, you can't start a round of trivia here, as the bot owner has explicitly blocked games from being started on this server.\nIf you want to start a round of trivia, first [invite the bot](https://discord.com/oauth2/authorize?client_id={}&scope=bot+identify&permissions=268848192&redirect_uri=https%3A%2F%2Ftriviabot.co.uk%2F&response_type=code) to your own server!", user.get_username(), bot->user.id.get()), c->get_id().get());
									return false;
								}
						        }

							if (!settings.premium) {
								std::lock_guard<std::mutex> user_cache_lock(states_mutex);
								for (auto j = states.begin(); j != states.end(); ++j) {
									if (j->second->guild_id == c->get_guild().get_id() && j->second->gamestate != TRIV_END) {
										aegis::channel* active_channel = bot->core.find_channel(j->second->channel_id);
										if (active_channel) {
											EmbedWithFields(":warning: Can't start two rounds at once on one server!", {{"A round of trivia is already active", fmt::format("Please see <#{}> to join the game", active_channel->get_id().get()), false},
													{"Get TriviaBot Premium!", "If you want to do this, TriviaBot Premium lets you run as many concurrent games on the same server as you want. For more information please see the [TriviaBot Premium Page](https://triviabot.co.uk/premium/).", false}}, c->get_id().get());
											return false;
										}
									}
								}
							}

							if (!game_in_progress) {
								int32_t max_quickfire = (settings.premium ? 200 : 15);
								if ((!quickfire && (questions < 5 || questions > 200)) || (quickfire && (questions < 5 || questions > max_quickfire))) {
									if (quickfire) {
										if (questions > max_quickfire && !settings.premium) {
											EmbedWithFields(":warning: Can't start a quickfire round of more than 15 questions", {{"Get TriviaBot Premium!", "If you want to do this, TriviaBot Premium lets you run quickfire rounds of up to 200 questions! For more information please see the [TriviaBot Premium Page](https://triviabot.co.uk/premium/).", false}}, c->get_id().get());
										} else {
											SimpleEmbed(":warning:", fmt::format("**{}**, you can't create a quickfire trivia round of less than 5 or more than {} questions!", user.get_username(), max_quickfire), c->get_id().get());
										}
									} else {
										SimpleEmbed(":warning:", fmt::format("**{}**, you can't create a normal trivia round of less than 5 or more than 200 questions!", user.get_username()), c->get_id().get());
									}
									return false;
								}

								std::vector<std::string> sl = fetch_shuffle_list();
								if (sl.size() < 50) {
									SimpleEmbed(":warning:", fmt::format("**{}**, something spoopy happened. Please try again in a couple of minutes!", user.get_username()), c->get_id().get(), "That wasn't supposed to happen...");
									return false;
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
									state->channel_id = channel_id;
									state->curr_qid = 0;
									state->curr_answer = "";
									aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
									{
										std::lock_guard<std::mutex> user_cache_lock(states_mutex);
										states[channel_id] = state;
									}
									if (c) {
										state->guild_id = c->get_guild().get_id();
										bot->core.log->info("Started game on guild {}, channel {}, {} questions [{}]", state->guild_id, channel_id, questions, quickfire ? "quickfire" : "normal");

										EmbedWithFields(fmt::format(":question: New {}trivia round started by {}!", (quickfire ? "**QUICKFIRE** " : ""), user.get_username()), {{"Questions", fmt::format("{}", questions), false}, {"Get Ready", "First question coming up!", false}}, c->get_id().get());
										state->timer = new std::thread(&state_t::tick, state);
									}
	
									return false;
								}

							} else {
								SimpleEmbed(":warning:", fmt::format("Buhhh... a round is already running here, **{}**!", user.get_username()), c->get_id().get());
								return false;
							}
						} else if (subcommand == "stop") {
							if (game_in_progress) {
								if (settings.only_mods_stop) {
									if (!moderator) {
										SimpleEmbed(":warning:", fmt::format("**{}**, only trivia moderators can stop trivia on this server!", user.get_username()), c->get_id().get());
										return false;
									}
								}
								SimpleEmbed(":octagonal_sign:", fmt::format("**{}** has stopped the round of trivia!", user.get_username()), c->get_id().get());
								{
									std::lock_guard<std::mutex> user_cache_lock(states_mutex);
									states.erase(states.find(channel_id));
								}
								delete state;
							} else {
								SimpleEmbed(":warning:", fmt::format("No trivia round is running here, **{}**!", user.get_username()), c->get_id().get());
							}
							return false;
						} else if (subcommand == "votehint" || subcommand == "vh") {
							if (game_in_progress) {
								if ((state->gamestate == TRIV_FIRST_HINT || state->gamestate == TRIV_SECOND_HINT || state->gamestate == TRIV_TIME_UP) && (state->round % 10) != 0 && state->curr_answer != "") {
									db::resultset rs = db::query("SELECT *,(unix_timestamp(vote_time) + 43200 - unix_timestamp()) as remaining FROM infobot_votes WHERE snowflake_id = ? AND now() < vote_time + interval 12 hour", {user.get_id().get()});
									if (rs.size() == 0) {
										SimpleEmbed("<:wc_rs:667695516737470494>", "You haven't voted for the bot today! Vote for the bot every 12 hours to get eight uses of command, which delivers a personal hint for the current question to you via direct message.", c->get_id().get());
										return false;
									} else {
										int64_t remaining_hints = from_string<int64_t>(rs[0]["dm_hints"], std::dec);
										int32_t secs = from_string<int32_t>(rs[0]["remaining"], std::dec);
										int32_t mins = secs / 60 % 60;
										float hours = floor(secs / 60 / 60);
										if (remaining_hints < 1) {
											SimpleEmbed(":warning:", fmt::format("You are out of voter hints, **{}**!\n[Vote again](https://top.gg/bot/{}/vote) in **{} hours and {} minutes** to get eight uses of this command, which delivers a personal hint for the current question to you via direct message.", user.get_username(), bot->user.id.get(), hours, mins), c->get_id().get());
										} else {
											remaining_hints--;
											if (remaining_hints > 0) {
												SimpleEmbed(":white_check_mark:", fmt::format("**{}**, i've sent you a hint via DM!\nYou have **{}** voter hint(s) remaining which expire in **{} hours and {} minutes**.", user.get_username(), remaining_hints, hours, mins), c->get_id().get());
											} else {
												SimpleEmbed(":white_check_mark:", fmt::format("**{}**, i've sent you a hint via DM!\nYou have **NO MORE** voter hints remaining and can vote again in **{} hours and {} minutes**.", user.get_username(), hours, mins), c->get_id().get());
											}
											std::string personal_hint = state->curr_answer;
											personal_hint = lowercase(personal_hint);
											personal_hint[0] = '#';
											personal_hint[personal_hint.length() - 1] = '#';
											personal_hint = ReplaceString(personal_hint, " ", "#");
											bot->core.create_dm_message(user.get_id().get(), fmt::format("Your personal hint is:\n**{}**", personal_hint));
											db::query("UPDATE infobot_votes SET dm_hints = ? WHERE snowflake_id = ?", {remaining_hints, user.get_id().get()});
											return false;
										}
									}
								} else {
									SimpleEmbed(":warning:", fmt::format("You should probaly wait until a non-insane round question is being asked, **{}**!", user.get_username()), c->get_id().get());
									return false;
								}
							} else {
								SimpleEmbed(":warning:", fmt::format("No trivia round is running here, **{}**!\n[Vote for the bot every 12 hours](https://top.gg/bot/{}/vote) to get eight uses of command, which delivers a personal hint for the current question to you via direct message.", user.get_username(), bot->user.id.get()), c->get_id().get());
								return false;
							}
						} else if (subcommand == "rank") {
							SimpleEmbed(":bar_chart:", get_rank(user.get_id().get(), c->get_guild().get_id().get()), c->get_id().get());
						} else if (subcommand == "stats") {
							show_stats(c->get_guild().get_id(), channel_id);
						} else if (subcommand == "info") {
							std::stringstream s;
							time_t diff = bot->core.uptime() / 1000;
							int seconds = diff % 60;
							diff /= 60;
							int minutes = diff % 60;
							diff /= 60;
							int hours = diff % 24;
							diff /= 24;
							int days = diff;
							int64_t servers = bot->core.get_guild_count();
							int64_t users = bot->core.get_member_count();
							char uptime[32];
							snprintf(uptime, 32, "%d day%s, %02d:%02d:%02d", days, (days != 1 ? "s" : ""), hours, minutes, seconds);
							char startstr[256];
							tm _tm;
							gmtime_r(&startup, &_tm);
							strftime(startstr, 255, "%x, %I:%M%p", &_tm);

							const statusfield statusfields[] = {
								statusfield("Active Games", Comma(GetActiveGames())),
								statusfield("Total Servers", Comma(servers)),
								statusfield("Connected Since", startstr),
								statusfield("Online Users", Comma(users)),
								statusfield("Uptime", std::string(uptime)),
								statusfield("Shards", Comma(bot->core.shard_max_count)),
								statusfield("Member Intent", bot->HasMemberIntents() ? ":white_check_mark: Yes" : "<:wc_rs:667695516737470494> No"),
								statusfield("Test Mode", bot->IsTestMode() ? ":white_check_mark: Yes" : "<:wc_rs:667695516737470494> No"),
								statusfield("Developer Mode", bot->IsDevMode() ? ":white_check_mark: Yes" : "<:wc_rs:667695516737470494> No"),
								statusfield("Prefix", "``" + escape_json(settings.prefix) + "``"),
								statusfield("Bot Version", std::string(TRIVIA_VERSION)),
								statusfield("Library Version", std::string(AEGIS_VERSION_TEXT)),
								statusfield("", "")
							};

							s << "{\"title\":\"" << bot->user.username << " Information";
							s << "\",\"color\":" << settings.embedcolour << ",\"url\":\"https:\\/\\/triviabot.co.uk\\/\\/\",";
							s << "\"footer\":{\"link\":\"https:\\/\\/triviabot.co.uk\\/\",\"text\":\"Powered by TriviaBot!\",\"icon_url\":\"https:\\/\\/triviabot.co.uk\\/images\\/triviabot_tl_icon.png\"},\"fields\":[";
							for (int i = 0; statusfields[i].name != ""; ++i) {
								s << "{\"name\":\"" +  statusfields[i].name + "\",\"value\":\"" + statusfields[i].value + "\", \"inline\": true}";
								if (statusfields[i + 1].name != "") {
									s << ",";
								}
							}

							s << "],\"description\":\"" << (settings.premium ? ":star: This server has TriviaBot Premium! They rock! :star:" : "") << "\"}";

							json embed_json;
							try {
								embed_json = json::parse(s.str());
							}
							catch (const std::exception &e) {
								bot->core.log->error("Malformed json created when reporting info: {}", s.str());
							}
							if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {
								c->create_message_embed("", embed_json);
								bot->sent_messages++;
							}
						} else if (subcommand == "active") {
							db::resultset rs = db::query("SELECT * FROM trivia_access WHERE user_id = ? AND enabled = 1", {user.get_id().get()});
							if (rs.size() > 0) {
								std::stringstream w;
								w << "```diff\n";
								int32_t state_id = 0;

								const std::vector<std::string> state_name = {
									"<unknown>",
									"ASK_QUES",
									"1ST_HINT",
									"2ND_HINT",
									"TIME_UP",
									"ANS_RIGHT",
									"END"
								};

								w << fmt::format("- ╭──────┬──────────┬──────────────────┬──────────────────┬────────────────────┬────────────┬───────┬────────────╮\n");
								w << fmt::format("- │state#│game state│guild_id          │channel_id        │start time          │# questions │type   │question_id │\n");
								w << fmt::format("- ├──────┼──────────┼──────────────────┼──────────────────┼────────────────────┼────────────┼───────┼────────────┤\n");

								{
									std::lock_guard<std::mutex> user_cache_lock(states_mutex);
									for (auto s = states.begin(); s != states.end(); ++s) {
										state_t* st = s->second;
										if (st->gamestate == TRIV_END) {
											continue;
										}
										char timestamp[255];
										tm _tm;
										gmtime_r(&st->start_time, &_tm);
										strftime(timestamp, sizeof(timestamp), "%H:%M:%S %d-%b-%Y", &_tm);
	
										w << fmt::format("+ |{:6}|{:10}|{:11}|{:12}|{:>16}|{:12}|{:7}|{:12}|\n", state_id++, state_name[st->gamestate], st->guild_id, st->channel_id, timestamp, fmt::format("{}/{}", st->round, st->numquestions - 1), (st->interval == TRIV_INTERVAL ? "normal" : "quick"), st->curr_qid);
									}
								}

								w << fmt::format("+ ╰──────┴──────────┴──────────────────┴──────────────────┴────────────────────┴────────────┴───────┴────────────╯\n");
								w << "```";
								c->create_message(w.str());
								bot->sent_messages++;

							} else {
								SimpleEmbed(":warning:", fmt::format("**{}**, this command is restricted to the bot administration team only", user.get_username()), c->get_id().get());
							}
						} else if (subcommand == "join") {
							std::string teamname;
							tokens >> teamname;
							if (join_team(user.get_id().get(), teamname)) {
								SimpleEmbed(":busts_in_silhouette:", fmt::format("You have successfully joined the team \"**{}**\", **{}**", teamname, user.get_username()), c->get_id().get(), "Call for backup!");
							} else {
								SimpleEmbed(":warning:", fmt::format("I cannot bring about world peace, make you a sandwich, or join that team, **{}**", user.get_username()), c->get_id().get());
							}
						} else if (subcommand == "create") {
							std::string newteamname;
							tokens >> newteamname;
							std::string teamname = get_current_team(user.get_id().get());
							if (teamname.empty() || teamname == "!NOTEAM") {
								if (create_new_team(newteamname)) {
									join_team(user.get_id().get(), newteamname);
									SimpleEmbed(":busts_in_silhouette:", fmt::format("You have successfully **created** and joined the team \"**{}**\", **{}**", newteamname, user.get_username()), c->get_id().get(), "It's unsafe to go alone...");
								} else {
									SimpleEmbed(":warning:", fmt::format("I couldn't create that team, **{}**...", user.get_username()), c->get_id().get());
								}
							} else {
								SimpleEmbed(":warning:", fmt::format("**{}**, you are already a member of team \"**{}**\"!", user.get_username(), teamname), c->get_id().get());
							}
						} else if (subcommand == "leave") {
							std::string teamname = get_current_team(user.get_id().get());
							if (teamname.empty() || teamname == "!NOTEAM") {
								SimpleEmbed(":warning:", fmt::format("**{}**, you aren't a member of any team! Use **{}trivia join** to join a team!", user.get_username(), settings.prefix), c->get_id().get());
							} else {
								leave_team(user.get_id().get());
								SimpleEmbed(":busts_in_silhouette:", fmt::format("**{}** has left team **{}**", user.get_username(), teamname), c->get_id().get(), "Come back, we'll miss you! :cry:");
							}

						} else if (subcommand == "help") {
							std::string section;
							tokens >> section;
							GetHelp(section, message.msg.get_channel_id().get(), bot->user.username, bot->user.id.get(), msg.get_user().get_username(), msg.get_user().get_id().get(), false, settings.embedcolour);
						} else {
							SimpleEmbed(":warning:", fmt::format("**{}**, I don't know that command! Try ``{}trivia start 20`` :slight_smile:", user.get_username(), settings.prefix), c->get_id().get(), "Need some help?");
						}
					}
				}

			}

		}

		return true;
	}

	/**
	 * Emit help using a json file in the help/ directory. Missing help files emit a generic error message.
	 */
	void GetHelp(const std::string &section, int64_t channelID, const std::string &botusername, int64_t botid, const std::string &author, int64_t authorid, bool dm, uint32_t colour)
	{
		bool found = true;
		json embed_json;
		char timestamp[256];
		time_t timeval = time(NULL);
		aegis::channel* channel = bot->core.find_channel(channelID);

		if (!channel) {
			bot->core.log->error("Can't find channel {}!", channelID);
			return;
		}

		std::ifstream t("../help/" + (section.empty() ? "basic" : section) + ".json");
		if (!t) {
			found = dm = false;
			t = std::ifstream("../help/error.json");
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
				channel->create_message("<@" + std::to_string(authorid) + ">, herp derp, theres a malformed help file. Please contact a developer on the official support server: https://discord.gg/brainbox");
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

};

state_t::state_t(TriviaModule* _creator) : creator(_creator), terminating(false), timer(nullptr)
{
}

state_t::~state_t()
{
	terminating = true;
	creator->GetBot()->core.log->debug("state_t::~state(): G:{} C:{}", guild_id, channel_id);
	creator->DisposeThread(timer);
}

void state_t::tick()
{
	while (!terminating) {
		sleep(this->interval);
		creator->Tick(this);
		int64_t game_length = time(NULL) - start_time;
		if (game_length >= GAME_REAP_SECS) {
			terminating = true;
			gamestate = TRIV_END;
			creator->GetBot()->core.log->debug("state_t::tick(): G:{} C:{} reaped game of length {} seconds", guild_id, channel_id, game_length);
		}
	}
}

guild_settings_t::guild_settings_t(int64_t _guild_id, const std::string &_prefix, const std::vector<int64_t> &_moderator_roles, uint32_t _embedcolour, bool _premium, bool _only_mods_stop, bool _role_reward_enabled, int64_t _role_reward_id, const std::string &_custom_url)
	: guild_id(_guild_id), prefix(_prefix), moderator_roles(_moderator_roles), embedcolour(_embedcolour), premium(_premium), only_mods_stop(_only_mods_stop), role_reward_enabled(_role_reward_enabled), role_reward_id(_role_reward_id), custom_url(_custom_url)
{
}

ENTRYPOINT(TriviaModule);

