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
#include <fmt/format.h>
#include <dpp/nlohmann/json.hpp>
#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <fstream>
#include <streambuf>
#include <unistd.h>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include "state.h"
#include "trivia.h"
#include "webrequest.h"
#include "wlower.h"
#include "time.h"

using json = nlohmann::json;

TriviaModule::TriviaModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), terminating(false), booted(false)
{
	/* TODO: Move to something better like mt-rand */
	srand(time(NULL) * time(NULL));

	/* Attach D++ events to module */
	ml->Attach({ I_OnMessage,
		     I_OnPresenceUpdate,
		     I_OnChannelDelete,
		     I_OnGuildDelete,
		     I_OnGuildUpdate,
		     I_OnAllShardsReady,
		     I_OnGuildCreate,
		     I_OnEntitlementCreate,
		     I_OnEntitlementDelete,
		     I_OnEntitlementUpdate
	}, this);

	/* Various regular expressions */
	number_tidy_dollars = new PCRE("^([\\d\\,]+)\\s+dollars$");
	number_tidy_nodollars = new PCRE("^([\\d\\,]+)\\s+(.+?)$");
	number_tidy_positive = new PCRE("^[\\d\\,]+$");
	number_tidy_negative = new PCRE("^\\-[\\d\\,]+$");
	prefix_match = new PCRE("prefix");

	startup = lastlang = time(NULL);

	/* Check for and store API key */
	if (Bot::GetConfig("apikey") == "") {
		throw "TriviaBot API key missing";
	}
	set_io_context(Bot::GetConfig("apikey"), bot, this);

	/* Create threads */
	presence_update = new std::thread(&TriviaModule::UpdatePresenceLine, this);
	game_tick_thread = new std::thread(&TriviaModule::Tick, this);
	guild_queue_thread = new std::thread(&TriviaModule::ProcessGuildQueue, this);

	/* Get command list from API */
	{
		std::unique_lock cmd_list_lock(cmds_mutex);
		api_commands = get_api_command_names();
		bot->core->log(dpp::ll_info, fmt::format("Initial API command count: {}", api_commands.size()));
	}
	{
		std::unique_lock lang_lock(lang_mutex);
		/* Read language strings */
		std::ifstream langfile("../lang.json");
		lang = new json();
		langfile >> *lang;
		bot->core->log(dpp::ll_info, fmt::format("Language strings count: {}", lang->size()));
	}

	achievements = new json();
	std::ifstream achievements_json("../achievements.json");
	achievements_json >> *achievements;

	censor = new neutrino(instigator->core, Bot::GetConfig("neutrino_user"), Bot::GetConfig("neutrino_key"));

	/* Setup built in commands */
	SetupCommands();

	/* Load numeric hints */
	ReloadNumStrs();
}

void TriviaModule::queue_command(const std::string &message, dpp::snowflake author, dpp::snowflake channel, dpp::snowflake guild, bool mention, const std::string &username, bool from_dashboard, dpp::user u, dpp::guild_member gm)
{
	handle_command(in_cmd(message, author, channel, guild, mention, username, from_dashboard, u, gm), dpp::interaction_create_t(nullptr, ""));
}

void TriviaModule::ProcessCommands()
{
}

Bot* TriviaModule::GetBot()
{
	return bot;
}

TriviaModule::~TriviaModule()
{
	/* We don't just delete threads, they must go through Bot::DisposeThread which joins them first */
	DisposeThread(game_tick_thread);
	DisposeThread(presence_update);
	DisposeThread(guild_queue_thread);

	/* This explicitly calls the destructor on all states */
	std::lock_guard<std::mutex> state_lock(states_mutex);
	states.clear();

	/* Delete these misc pointers, mostly regexps */
	delete number_tidy_dollars;
	delete number_tidy_nodollars;
	delete number_tidy_positive;
	delete number_tidy_negative;
	delete prefix_match;
	delete lang;
	delete achievements;
	delete censor;
}


bool TriviaModule::OnPresenceUpdate()
{
	/* Note: Only updates this cluster's shards! */
	const dpp::shard_list& shards = bot->core->get_shards();
	for (auto i = shards.begin(); i != shards.end(); ++i) {
		dpp::discord_client* shard = i->second;
		uint64_t uptime = shard->get_uptime().secs + (shard->get_uptime().mins * 60) + (shard->get_uptime().hours * 60 * 60) + (shard->get_uptime().days * 60 * 60 * 24);
		db::backgroundquery("INSERT INTO infobot_shard_status (id, cluster_id, connected, online, uptime, transfer, transfer_compressed) VALUES('?','?','?','?','?','?','?') ON DUPLICATE KEY UPDATE cluster_id = '?', connected = '?', online = '?', uptime = '?', transfer = '?', transfer_compressed = '?'",
			{
				shard->shard_id,
				bot->GetClusterID(),
				shard->is_connected(),
				shard->is_connected(),
				uptime,
				(uint64_t)(shard->get_decompressed_bytes_in() + shard->get_bytes_out()),
				(uint64_t)(shard->get_bytes_in() + shard->get_bytes_out()),
				bot->GetClusterID(),
				shard->is_connected(),
				shard->is_connected(),
				uptime,
				(uint64_t)(shard->get_decompressed_bytes_in() + shard->get_bytes_out()),
				(uint64_t)(shard->get_bytes_in() + shard->get_bytes_out())
			}
		);
	}
	/* Curly brace scope is for readability, this call is mutexed */
	{
		std::unique_lock cmd_list_lock(cmds_mutex);
		api_commands = get_api_command_names();
	}
	CheckLangReload();
	return true;
}

std::string TriviaModule::_(const std::string &k, const guild_settings_t& settings)
{
	/* Find language string 'k' in lang.json for the language specified in 'settings' */
	std::shared_lock lang_lock(lang_mutex);
	auto o = lang->find(k);
	if (o != lang->end()) {
		auto v = o->find(settings.language);
		if (v != o->end()) {
			return v->get<std::string>();
		}
	}
	return k;
}

bool TriviaModule::OnGuildCreate(const dpp::guild_create_t &guild)
{
	if (guild.created->is_unavailable()) {
		bot->core->log(dpp::ll_error, fmt::format("Guild ID {} is unavailable due to an outage!", guild.created->id));
	} else {
		cache_guild(*guild.created);
	}
	return true;
}

bool TriviaModule::OnGuildUpdate(const dpp::guild_update_t &guild)
{
	cache_guild(*guild.updated);
	return true;
}

bool TriviaModule::OnAllShardsReady()
{
	/* Called when the framework indicates all shards are connected */
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	db::resultset active = db::query("SELECT * FROM active_games WHERE hostname = '?' AND cluster_id = '?'", {hostname, bot->GetClusterID()});

	this->booted = true;

	if (bot->IsTestMode()) {
		/* Don't resume games in test mode */
		bot->core->log(dpp::ll_debug, fmt::format("Not resuming games in test mode"));
		return true;
	} else {
		bot->core->log(dpp::ll_debug, fmt::format("Resuming {} games...", active.size()));
	}

	/* Iterate all active games for this cluster id */
	for (auto game = active.begin(); game != active.end(); ++game) {

		uint64_t guild_id = from_string<uint64_t>((*game)["guild_id"], std::dec);
		bool quickfire = (*game)["quickfire"] == "1";
		uint64_t channel_id = from_string<uint64_t>((*game)["channel_id"], std::dec);

		bot->core->log(dpp::ll_info, fmt::format("Resuming id {}", channel_id));

		/* XXX: Note: The mutex here is VITAL to thread safety of the state list! DO NOT move it! */
		{
			std::lock_guard<std::mutex> states_lock(states_mutex);

			/* Check that impatient user didn't (re)start the round while bot was synching guilds! */
			if (states.find(channel_id) == states.end()) {

				std::vector<std::string> shuffle_list;
				guild_settings_t s = GetGuildSettings(guild_id);

				/* Get shuffle list from state in db */
				if (!(*game)["qlist"].empty()) {
					json shuffle = json::parse((*game)["qlist"]);
					for (auto s = shuffle.begin(); s != shuffle.end(); ++s) {
						shuffle_list.push_back(s->get<std::string>());
					}
				} else {
					/* No shuffle list to resume from, create a new one */
					try {
						shuffle_list = fetch_shuffle_list(from_string<uint64_t>((*game)["guild_id"], std::dec), "");
					}
					catch (const std::exception&) {
						shuffle_list = {};
					}
				}
				int32_t round = from_string<uint32_t>((*game)["question_index"], std::dec);

				states[channel_id] = state_t(
					this,
					from_string<uint32_t>((*game)["questions"], std::dec) + 1,
					from_string<uint32_t>((*game)["streak"], std::dec),
					from_string<uint64_t>((*game)["lastanswered"], std::dec),
					round,
					(quickfire ? (TRIV_INTERVAL / 4) : TRIV_INTERVAL),
					channel_id,
					((*game)["hintless"]) == "1",
					shuffle_list,
					(trivia_state_t)from_string<uint32_t>((*game)["state"], std::dec),
					guild_id
				);
				/* Force fetching of question */
				states[channel_id].build_question_cache(s);
				if (states[channel_id].is_insane_round(s)) {
					states[channel_id].do_insane_round(true, s);
				} else {
					states[channel_id].do_normal_round(true, s);
				}

				bot->core->log(dpp::ll_info, fmt::format("Resumed game on guild {}, channel {}, {} questions [{}]", guild_id, channel_id, states[channel_id].numquestions, quickfire ? "quickfire" : "normal"));
			}
		}
	}
	return true;
}

bool TriviaModule::OnChannelDelete(const dpp::channel_delete_t &cd)
{
	return true;
}


bool TriviaModule::OnGuildDelete(const dpp::guild_delete_t &gd)
{
	/* Unavailable guilds means an outage. We don't remove them if it's just an outage */
	if (!gd.deleted.is_unavailable()) {
		{
			std::unique_lock locker(settingcache_mutex);
			settings_cache.erase(gd.deleted.id);
			auto s = settings_cache;
			settings_cache = s;
		}
		db::backgroundquery("UPDATE trivia_guild_cache SET kicked = 1 WHERE snowflake_id = ?", {gd.deleted.id});
		bot->core->log(dpp::ll_info, fmt::format("Kicked from guild id {}", gd.deleted.id));
	} else {
		bot->core->log(dpp::ll_info, fmt::format("Outage on guild id {}", gd.deleted.id));
	}
	return true;
}

uint64_t TriviaModule::GetActiveLocalGames()
{
	/* Counts local games running on this cluster only */
	uint64_t a = 0;
	std::lock_guard<std::mutex> states_lock(states_mutex);
	for (auto state = states.begin(); state != states.end(); ++state) {
		if (state->second.gamestate != TRIV_END && !state->second.terminating) {
			++a;
		}
	}
	return a;
}

uint64_t TriviaModule::GetActiveGames()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(games) AS games FROM infobot_discord_counts WHERE dev = ?", {bot->IsDevMode() ? 1 : 0});
	if (rs.size()) {
		return from_string<uint64_t>(rs[0]["games"], std::dec);
	} else {
		return 0;
	}
}

uint64_t TriviaModule::GetGuildTotal()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT COUNT(id) AS server_count FROM guild_temp_cache", {});
	if (rs.size()) {
		return from_string<uint64_t>(rs[0]["server_count"], std::dec);
	} else {
		return 0;
	}
}

uint64_t TriviaModule::GetMemberTotal()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(user_count) AS user_count FROM guild_temp_cache", {});
	if (rs.size()) {
		return from_string<uint64_t>(rs[0]["user_count"], std::dec);
	} else {
		return 0;
	}
}

uint64_t TriviaModule::GetChannelTotal()
{
	/* Counts all games across all clusters */
	auto rs = db::query("SELECT SUM(channel_count) AS channel_count FROM infobot_discord_counts WHERE dev = ?", {bot->IsDevMode() ? 1 : 0});
	if (rs.size()) {
		return from_string<uint64_t>(rs[0]["channel_count"], std::dec);
	} else {
		return 0;
	}
}

void TriviaModule::eraseCache(dpp::snowflake guild_id)
{
	std::unique_lock locker(settingcache_mutex);
	auto i = settings_cache.find(guild_id);
	if (i != settings_cache.end()) {
		settings_cache.erase(i);
		auto s = settings_cache;
		settings_cache = s;
	}
}

const guild_settings_t TriviaModule::GetGuildSettings(dpp::snowflake guild_id)
{
	bool expired = false;
	auto i = settings_cache.end();
	{
		std::shared_lock locker(settingcache_mutex);
		i = settings_cache.find(guild_id);
		if (i != settings_cache.end()) {
			if (time(nullptr) > i->second.time + 60) {
				expired = true;
			} else {
				return i->second;
			}
		}
	}
	if (expired) {
		this->eraseCache(guild_id);
	}
	
	db::resultset r = db::query("SELECT * FROM bot_guild_settings WHERE snowflake_id = ?", {guild_id});
	if (!r.empty()) {
		std::stringstream s(r[0]["moderator_roles"]);
		uint64_t role_id;
		std::vector<uint64_t> role_list;
		while ((s >> role_id)) {
			role_list.push_back(role_id);
		}
		std::string max_n = r[0]["max_normal_round"], max_q = r[0]["max_quickfire_round"], max_h = r[0]["max_hardcore_round"];
		guild_settings_t gs(time(nullptr), from_string<uint64_t>(r[0]["snowflake_id"], std::dec), r[0]["prefix"], role_list, from_string<uint32_t>(r[0]["embedcolour"], std::dec), (r[0]["premium"] == "1"), (r[0]["only_mods_stop"] == "1"), (r[0]["only_mods_start"] == "1"), (r[0]["role_reward_enabled"] == "1"), from_string<uint64_t>(r[0]["role_reward_id"], std::dec), r[0]["custom_url"], r[0]["language"], from_string<uint32_t>(r[0]["question_interval"], std::dec), max_n.empty() ? 200 : from_string<uint32_t>(max_n, std::dec), max_q.empty() ? (r[0]["premium"] == "1" ? 200 : 15) : from_string<uint32_t>(max_q, std::dec), max_h.empty() ? 200 : from_string<uint32_t>(max_h, std::dec), r[0]["disable_insane_rounds"] == "1");
		{
			std::unique_lock locker(settingcache_mutex);
			settings_cache.emplace(guild_id, gs);
		}
		return gs;
	} else {
		db::backgroundquery("INSERT INTO bot_guild_settings (snowflake_id) VALUES('?') ON DUPLICATE KEY UPDATE prefix = prefix", {guild_id});
		guild_settings_t gs(time(nullptr), guild_id, "!", {}, 3238819, false, false, false, false, 0, "", "en", 20, 200, 15, 200, false);
		{
			std::unique_lock locker(settingcache_mutex);
			settings_cache.emplace(guild_id, gs);
		}
		return gs;
	}
}

std::string TriviaModule::GetVersion()
{
	/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
	std::string version = "$ModVer 103$";
	return "3.0." + version.substr(8,version.length() - 9);
}

std::string TriviaModule::GetDescription()
{
	return "Trivia System";
}

void TriviaModule::UpdatePresenceLine()
{
	uint32_t ticks = 0;
	int32_t questions = get_total_questions();
	while (!terminating) {
		try {
			ticks++;
			if (ticks > 100) {
				questions = get_total_questions();
				ticks = 0;
			}
			bot->counters["activegames"] = GetActiveLocalGames();
			std::string presence = fmt::format("Trivia! {} questions, {} active games on {} servers through {} shards, cluster {}", Comma(questions), Comma(GetActiveGames()), Comma(this->GetGuildTotal()), Comma(bot->core->numshards), bot->GetClusterID());
			bot->core->log(dpp::ll_debug, fmt::format("PRESENCE: {}", presence));
			/* Can't translate this, it's per-shard! */
			bot->core->set_presence(dpp::presence(dpp::ps_online, dpp::at_game, presence));
	
			if (!bot->IsTestMode()) {
				/* Don't handle shard reconnects or queued starts in test mode */
				CheckForQueuedStarts();
				CheckReconnects();
			}
		}
		catch (std::exception &e) {
			bot->core->log(dpp::ll_error, fmt::format("Exception in UpdatePresenceLine: {}", e.what()));
		}
		sleep(120);
	}
	bot->core->log(dpp::ll_debug, fmt::format("Presence thread exited."));
}

std::string TriviaModule::letterlong(std::string text, const guild_settings_t &settings)
{
	text = ReplaceString(text, " ", "");
	if (text.length()) {
		return fmt::format(_("HINT_LETTERLONG", settings), wlength(text), wfirst(text), wlast(text));
	} else {
		return "An empty answer";
	}
}

std::string TriviaModule::vowelcount(const std::string &text, const guild_settings_t &settings)
{
	std::pair<int, int> counts = countvowel(text);
	return fmt::format(_("HINT_VOWELCOUNT", settings), counts.second, counts.first);
}

void TriviaModule::show_stats(const std::string& interaction_token, dpp::snowflake command_id, dpp::snowflake guild_id, dpp::snowflake channel_id)
{
	db::resultset topten = db::query("SELECT dayscore, name, emojis, trivia_user_cache.* FROM scores LEFT JOIN trivia_user_cache ON snowflake_id = name LEFT JOIN vw_emojis ON name = user_id WHERE guild_id = ? and dayscore > 0 ORDER BY dayscore DESC limit 10", {guild_id});
	size_t count = 1;
	std::string msg;
	for(auto& r : topten) {
		if (!r["username"].empty()) {
			msg.append(fmt::format("{0}. `{1}` ({2}) {3}\n", count++, r["username"], r["dayscore"], r["emojis"]));
		} else {
			msg.append(fmt::format("{}. <@{}> ({})\n", count++, r["snowflake_id"], r["dayscore"]));
		}
	}
	if (msg.empty()) {
		msg = "Nobody has played here today! :cry:";
	}
	guild_settings_t settings = GetGuildSettings(guild_id);
	if (settings.premium && !settings.custom_url.empty()) {
		EmbedWithFields(interaction_token, command_id, settings, _("LEADERBOARD", settings), {{_("TOP_TEN", settings), msg, false}, {_("MORE_INFO", settings), fmt::format(_("LEADER_LINK", settings), settings.custom_url), false}}, channel_id);
	} else {
		EmbedWithFields(interaction_token, command_id, settings, _("LEADERBOARD", settings), {{_("TOP_TEN", settings), msg, false}, {_("MORE_INFO", settings), fmt::format(_("LEADER_LINK", settings), guild_id), false}}, channel_id);
	}
}

void TriviaModule::Tick()
{
	while (!terminating) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		try
		{
			std::lock_guard<std::mutex> states_lock(states_mutex);
			std::vector<uint64_t> expired;
			time_t now = time(NULL);

			for (auto & s : states) {
				if (now >= s.second.next_tick) {
					bot->core->log(dpp::ll_trace, fmt::format("Ticking state id {} (now={}, next_tick={})", s.first, now, s.second.next_tick));
					s.second.tick();
					if (s.second.terminating) {
						uint64_t e = s.first;
						expired.push_back(e);
					}
				}
			}

			for (auto e : expired) {
				bot->core->log(dpp::ll_debug, fmt::format("Terminating state id {}", e));
				states.erase(e);
				if (states.size() == 0) {
					states = {};
				}
			}
		}
		catch (const std::exception &e) {
			bot->core->log(dpp::ll_warning, fmt::format("Uncaught std::exception in TriviaModule::Tick(): {}", e.what()));
		}
	}
}

void TriviaModule::DisposeThread(std::thread* t)
{
	bot->DisposeThread(t);
}

void state_t::StopGame(const guild_settings_t &settings)
{
	if (gamestate != TRIV_END) {
		creator->SimpleEmbed(settings, ":octagonal_sign:", _("DASH_STOP", settings), channel_id, _("STOPPING", settings));
		gamestate = TRIV_END;
		terminating = false;
	}
}

dpp::user dummyuser;

void TriviaModule::CheckForQueuedStarts()
{
	uint64_t max_shards = from_string<uint32_t>(Bot::GetConfig("shardcount"), std::dec);
	db::resultset rs = db::query("SELECT * FROM start_queue ORDER BY queuetime", {});
	for (auto r = rs.begin(); r != rs.end(); ++r) {
		uint64_t guild_id = from_string<uint64_t>((*r)["guild_id"], std::dec);
		/* Check that this guild is on this cluster, if so we can start this game */
		uint64_t shard = (guild_id >> 22) % max_shards;
		uint64_t cluster = shard % bot->GetMaxClusters();
		if (cluster == bot->GetClusterID()) {

			uint64_t channel_id = from_string<uint64_t>((*r)["channel_id"], std::dec);
			uint64_t user_id = from_string<uint64_t>((*r)["user_id"], std::dec);
			uint32_t questions = from_string<uint32_t>((*r)["questions"], std::dec);
			uint32_t quickfire = from_string<uint32_t>((*r)["quickfire"], std::dec);
			uint32_t hintless = from_string<uint32_t>((*r)["hintless"], std::dec);
			std::string category = (*r)["category"];

			bot->core->log(dpp::ll_info, fmt::format("Remote start, guild_id={} channel_id={} user_id={} questions={} type={} category='{}'", guild_id, channel_id, user_id, questions, hintless ? "hardcore" : (quickfire ? "quickfire" : "normal"), category));

			queue_command(fmt::format("{} {}{}", (hintless ? "hardcore" : (quickfire ? "quickfire" : "start")), questions, (category.empty() ? "" : (std::string(" ") + category))), user_id, channel_id, guild_id, false, "Dashboard", true, dpp::user(), dpp::guild_member());

			/* Delete just this entry as we've processed it */
			db::query("DELETE FROM start_queue WHERE channel_id = ?", {channel_id});
		}
	}
}

void TriviaModule::CacheUser(dpp::snowflake user, dpp::user _user, dpp::guild_member gm, dpp::snowflake channel_id)
{
	cache_user(&_user, &gm, gm.guild_id);
}

bool TriviaModule::OnMessage(const dpp::message_create_t &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
{
	return RealOnMessage(message, clean_message, mentioned, stringmentions, 0);
}

bool TriviaModule::RealOnMessage(const dpp::message_create_t &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions, dpp::snowflake _author_id)
{
	std::string username;
	dpp::message msg = message.msg;
	bool is_from_dashboard = (_author_id != 0);
	double start = time_f();

	// Allow overriding of author id from remote start code
	uint64_t author_id = _author_id ? _author_id : msg.author.id;

	bool isbot = msg.author.is_bot();
	username = message.msg.author.username;
	if (isbot) {
		/* Drop bots here */
		return true;
	}
	
	dpp::snowflake guild_id = message.msg.guild_id;
	dpp::snowflake channel_id = message.msg.channel_id;
	dpp::guild_member gm = message.msg.member;

	if (msg.channel_id.empty()) {
		/* No channel! */
		bot->core->log(dpp::ll_debug, fmt::format("Message without channel, M:{} A:{}", msg.id, author_id));
	} else {

		if (mentioned && prefix_match->Match(clean_message)) {
			guild_settings_t settings = GetGuildSettings(guild_id);
			bot->core->message_create(dpp::message(channel_id, fmt::format(_("PREFIX", settings), settings.prefix, settings.prefix)));
			bot->core->log(dpp::ll_debug, fmt::format("Respond to prefix request on channel C:{} A:{}", channel_id, author_id));
		} else {

			guild_settings_t settings = GetGuildSettings(guild_id);

			// Commands
			if (lowercase(clean_message.substr(0, settings.prefix.length())) == lowercase(settings.prefix)) {
				std::string command = clean_message.substr(settings.prefix.length(), clean_message.length() - settings.prefix.length());
				queue_command(command, author_id, channel_id, guild_id, mentioned, username, is_from_dashboard, message.msg.author, gm);
				bot->core->log(dpp::ll_info, fmt::format("CMD (USER={}, GUILD={}): <{}> {}", author_id, guild_id, username, clean_message));
			}
		
			// Answers for active games
			{
				std::lock_guard<std::mutex> states_lock(states_mutex);
				state_t* state = GetState(channel_id);
				if (state) {
					/* The state_t class handles potential answers, but only when a game is running on this guild */
					state->queue_message(settings, clean_message, author_id, username, mentioned, message.msg.author, gm);
					bot->core->log(dpp::ll_debug, fmt::format("Processed potential answer message from A:{} on C:{}", author_id, channel_id));
				}
			}
		}
	}

	double end = time_f();
	double time_taken = end - start;

	if (bot->IsDevMode()) {
		bot->core->log(dpp::ll_debug, fmt::format("Message processing took {:.7f} seconds, channel: {}", time_taken, msg.channel_id));
	} else {
		if (time_taken > 0.1) {
			 bot->core->log(dpp::ll_warning, fmt::format("Message processing took {:.7f} seconds!!! Channel: {}", time_taken, msg.channel_id));
		}
	}

	return true;
}

state_t* TriviaModule::GetState(dpp::snowflake channel_id) {

	auto state_iter = states.find(channel_id);
	if (state_iter != states.end()) {
		return &state_iter->second;
	}
	return nullptr;
}

ENTRYPOINT(TriviaModule);

