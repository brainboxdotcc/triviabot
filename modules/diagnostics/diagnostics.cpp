/************************************************************************************
 * 
 * Sporks, the learning, scriptable Discord bot!
 *
 * Copyright 2019 Craig Edwards <support@sporks.gg>
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

#include <sporks/bot.h>
#include <sporks/regex.h>
#include <sporks/modules.h>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

struct guild_count_data
{
	size_t guilds;
	size_t members;
};

struct shard_data
{
	std::chrono::time_point<std::chrono::steady_clock> last_message;
};

std::string exec(const char* cmd) {
	std::array<char, 128> buffer;
	std::string result;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
	if (!pipe) {
		return "";
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}
	return result;
}

/**
 * Provides diagnostic commands for monitoring the bot and debugging it interactively while it's running.
 */

class DiagnosticsModule : public Module
{
	PCRE* diagnosticmessage;
	std::vector<shard_data> shards;
	double microseconds_ping;
public:
	DiagnosticsModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml)
	{
		ml->Attach({ I_OnMessage, I_OnRestEnd }, this);
		diagnosticmessage = new PCRE("^sudo(\\s+(.+?))$", true);


		for (uint32_t i = 0; i < bot->core.get_shard_mgr().shard_max_count; ++i) {
			shards.push_back({});
		}
	}

	virtual ~DiagnosticsModule()
	{
		delete diagnosticmessage;
	}

	virtual std::string GetVersion()
	{
		/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
		std::string version = "$ModVer 24$";
		return "1.0." + version.substr(8,version.length() - 9);
	}

	virtual std::string GetDescription()
	{
		return "Diagnostic Commands (sudo), '@mention sudo'";
	}

	virtual bool OnRestEnd(std::chrono::steady_clock::time_point start_time, uint16_t code)
	{
		microseconds_ping = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
		return true;
	}

	virtual bool OnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
	{
		std::vector<std::string> param;
		std::string botusername = bot->user.username;
		aegis::gateway::objects::message msg = message.msg;

		shards[message.shard.get_id()].last_message = std::chrono::steady_clock::now();

		if (mentioned && diagnosticmessage->Match(clean_message, param) && param.size() >= 3) {

			aegis::gateway::objects::message msg = message.msg;
			std::stringstream tokens(trim(param[2]));
			std::string subcommand;
			tokens >> subcommand;

			bot->core.log->info("SUDO: <{}> {}", msg.get_user().get_username(), clean_message);

			/* Get owner snowflake id from config file */
			int64_t owner_id = from_string<int64_t>(Bot::GetConfig("owner"), std::dec);

			/* Only allow these commands to the bot owner */
			if (msg.author.id.get() == owner_id) {

				if (param.size() < 3) {
					/* Invalid number of parameters */
					EmbedSimple("Sudo make me a sandwich.", msg.get_channel_id().get());
				} else {
					/* Module list command */
					if (lowercase(subcommand) == "modules") {
						std::stringstream s;

						// NOTE: GetModuleList's reference is safe from within a module event
						const ModMap& modlist = bot->Loader->GetModuleList();

						s << "```diff" << std::endl;
						s << fmt::format("- ╭─────────────────────────┬───────────┬────────────────────────────────────────────────╮") << std::endl;
						s << fmt::format("- │ Filename                | Version   | Description                                    |") << std::endl;
						s << fmt::format("- ├─────────────────────────┼───────────┼────────────────────────────────────────────────┤") << std::endl;

						for (auto mod = modlist.begin(); mod != modlist.end(); ++mod) {
							s << fmt::format("+ │ {:23} | {:9} | {:46} |", mod->first, mod->second->GetVersion(), mod->second->GetDescription()) << std::endl;
						}
						s << fmt::format("+ ╰─────────────────────────┴───────────┴────────────────────────────────────────────────╯") << std::endl;
						s << "```";

						aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
						if (c) {
							if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {
								c->create_message(s.str());
								bot->sent_messages++;
							}
						}
						
					} else if (lowercase(subcommand) == "load") {
						/* Load a module */
						std::string modfile;
						tokens >> modfile;
						if (bot->Loader->Load(modfile)) {
							EmbedSimple("Loaded module: " + modfile, msg.get_channel_id().get());
						} else {
							EmbedSimple(std::string("Can't do that: ``") + bot->Loader->GetLastError() + "``", msg.get_channel_id().get());
						}
					} else if (lowercase(subcommand) == "unload") {
						/* Unload a module */
						std::string modfile;
						tokens >> modfile;
						if (modfile == "module_diagnostics.so") {
							EmbedSimple("I suppose you think that's funny, dont you? *I'm sorry. can't do that, dave.*", msg.get_channel_id().get());
						} else {
							if (bot->Loader->Unload(modfile)) {
								EmbedSimple("Unloaded module: " + modfile, msg.get_channel_id().get());
							} else {
								EmbedSimple(std::string("Can't do that: ``") + bot->Loader->GetLastError() + "``", msg.get_channel_id().get());
							}
						}
					} else if (lowercase(subcommand) == "reload") {
						/* Reload a currently loaded module */
						std::string modfile;
						tokens >> modfile;
						if (modfile == "module_diagnostics.so") {
							EmbedSimple("I suppose you think that's funny, dont you? *I'm sorry. can't do that, dave.*", msg.get_channel_id().get());
						} else {
							if (bot->Loader->Reload(modfile)) {
								EmbedSimple("Reloaded module: " + modfile, msg.get_channel_id().get());
							} else {
								EmbedSimple(std::string("Can't do that: ``") + bot->Loader->GetLastError() + "``", msg.get_channel_id().get());
							}
						}
					} else if (lowercase(subcommand) == "threadstats") {
						std::string result = exec("top -b -n1 -d0 | head -n7 && top -b -n1 -d0 -H | grep \"./bot\\|run.sh\" | grep -v grep | grep -v perl");
						aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
						if (c) {
							if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {
								c->create_message("```" + result + "```");
								bot->sent_messages++;
							}
						}
					} else if (lowercase(subcommand) == "lock") {
						std::string keyword;
						std::getline(tokens, keyword);
						keyword = trim(keyword);
						db::query("UPDATE infobot SET locked = 1 WHERE key_word = '?'", {keyword});
						EmbedSimple("**Locked** key word: " + keyword, msg.get_channel_id().get());
					} else if (lowercase(subcommand) == "unlock") {
						std::string keyword;
						std::getline(tokens, keyword);
						keyword = trim(keyword);
						db::query("UPDATE infobot SET locked = 0 WHERE key_word = '?'", {keyword});
						EmbedSimple("**Unlocked** key word: " + keyword, msg.get_channel_id().get());
					} else if (lowercase(subcommand) == "sql") {
						std::string sql;
						std::getline(tokens, sql);
						sql = trim(sql);
						db::resultset rs = db::query(sql, {});
						std::stringstream w;
						if (rs.size() == 0) {
							if (db::error() != "") {
								EmbedSimple("SQL Error: " + db::error(), msg.get_channel_id().get());
							} else {
								EmbedSimple("Successfully executed, no rows returned.", msg.get_channel_id().get());
							}
						} else {
							w << "- " << sql << std::endl;
							auto check = rs[0].begin();
							w << "+ Rows Returned: " << rs.size() << std::endl;
							for (auto name = rs[0].begin(); name != rs[0].end(); ++name) {
								if (name == rs[0].begin()) {
									w << "  ╭";
								}
								w << "────────────────────";
								check = name;
								w << (++check != rs[0].end() ? "┬" : "╮\n");
							}
							w << "  ";
							for (auto name = rs[0].begin(); name != rs[0].end(); ++name) {
								w << fmt::format("│{:20}", name->first.substr(0, 20));
							}
							w << "│" << std::endl;
							for (auto name = rs[0].begin(); name != rs[0].end(); ++name) {
								if (name == rs[0].begin()) {
									w << "  ├";
								}
								w << "────────────────────";
								check = name;
								w << (++check != rs[0].end() ? "┼" : "┤\n");
							}
							for (auto row : rs) {
								if (w.str().length() < 1900) {
									w << "  ";
									for (auto field : row) {
										w << fmt::format("│{:20}", field.second.substr(0, 20));
									}
									w << "│" << std::endl;
								}
							}
							for (auto name = rs[0].begin(); name != rs[0].end(); ++name) {
								if (name == rs[0].begin()) {
									w << "  ╰";
								}
								w << "────────────────────";
								check = name;
								w << (++check != rs[0].end() ? "┴" : "╯\n");
							}
							aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
							if (c) {
								if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == c->get_guild().get_id()) {
									c->create_message("```diff\n" + w.str() + "```");
									bot->sent_messages++;
								}
							}
						}
					} else if (lowercase(subcommand) == "reconnect") {
						uint32_t snum = 0;
						tokens >> snum;
						auto & s = bot->core.get_shard_by_id(snum);
						if (s.is_connected()) {
							EmbedSimple("Shard is already connected.", msg.get_channel_id().get());
						} else {
							bot->core.get_shard_mgr().queue_reconnect(s);
						}
					} else if (lowercase(subcommand) == "forcereconnect") {
						uint32_t snum = 0;
						tokens >> snum;
						auto & s = bot->core.get_shard_by_id(snum);
						if (s.is_connected()) {
							EmbedSimple("Shard is already connected.", msg.get_channel_id().get());
						} else {
							s.connect();
						}
					} else if (lowercase(subcommand) == "disconnect") {
						uint32_t snum = 0;
						tokens >> snum;
						auto & s = bot->core.get_shard_by_id(snum);
						bot->core.get_shard_mgr().close(s);
						EmbedSimple("Shard disconnected.", msg.get_channel_id().get());
					} else if (lowercase(subcommand) == "restart") {
						EmbedSimple("Restarting...", msg.get_channel_id().get());
						::sleep(5);
						/* Note: exit here will restart, because we run the bot via run.sh which restarts the bot on quit. */
						exit(0);
					} else if (lowercase(subcommand) == "ping") {
						aegis::channel* c = bot->core.find_channel(msg.get_channel_id().get());
						if (c) {
							std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
							aegis::gateway::objects::message m = c->create_message("Pinging...", msg.get_channel_id().get()).get();
							double microseconds_ping = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
							m.delete_message();
							EmbedSimple(fmt::format("**Pong!** REST Response time: {:.3f} ms", microseconds_ping / 1000, 4), msg.get_channel_id().get());
						}
					} else if (lowercase(subcommand) == "lookup") {
						int64_t gnum = 0;
						tokens >> gnum;
						aegis::guild* guild = bot->core.find_guild(gnum);
						if (guild) {
							EmbedSimple(fmt::format("**Guild** {} is on **shard** #{}", gnum, guild->shard_id), msg.get_channel_id().get());
						} else {
							EmbedSimple(fmt::format("**Guild** {} is not in my list!", gnum), msg.get_channel_id().get());
						}
					} else if (lowercase(subcommand) == "shardstats") {
						std::stringstream w;
						w << "```diff\n";

						uint64_t count = 0, u_count = 0;
						count = bot->core.get_shard_transfer();
						u_count = bot->core.get_shard_u_transfer();

						std::vector<guild_count_data> shard_guild_c(bot->core.shard_max_count);

						for (auto & v : bot->core.guilds)
						{
							++shard_guild_c[v.second->shard_id].guilds;
							shard_guild_c[v.second->shard_id].members += v.second->get_members().size();
						}

						w << fmt::format("  Total transfer: {} (U: {} | {:.2f}%) Memory usage: {}\n", aegis::utility::format_bytes(count), aegis::utility::format_bytes(u_count), (count / (double)u_count)*100, aegis::utility::format_bytes(aegis::utility::getCurrentRSS()));
						w << fmt::format("- ╭──────┬──────────┬───────┬───────┬────────────────┬────────────┬───────────┬──────────╮\n");
						w << fmt::format("- │shard#│  sequence│servers│members│uptime          │last message│transferred│reconnects│\n");
						w << fmt::format("- ├──────┼──────────┼───────┼───────┼────────────────┼────────────┼───────────┼──────────┤\n");

						for (uint32_t i = 0; i < bot->core.shard_max_count; ++i)
						{
							auto & s = bot->core.get_shard_by_id(i);
							auto time_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shards[s.get_id()].last_message).count();
							std::string divisor = "ms";
							if (time_count > 1000) {
								time_count /= 1000;
								divisor = "s ";
							}
							if (s.is_connected())
								w << "+ ";
							else
								w << "  ";
							w << fmt::format("|{:6}|{:10}|{:7}|{:7}|{:>16}|{:10}{:2}|{:>11}|{:10}|",
											 s.get_id(),
											 s.get_sequence(),
											 shard_guild_c[s.get_id()].guilds,
											 shard_guild_c[s.get_id()].members,
											 s.uptime_str(),
											 time_count,
											 divisor,
											 s.get_transfer_str(),
											 s.counters.reconnects);
							if (message.shard.get_id() == s.get_id()) {
								w << " *\n";
							} else {
								w << "\n";
							}
						}
						w << fmt::format("+ ╰──────┴──────────┴───────┴───────┴────────────────┴────────────┴───────────┴──────────╯\n");
						w << "```";
						aegis::channel *channel = bot->core.find_channel(msg.get_channel_id());
						if (channel) {
							if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
								channel->create_message(w.str());
								bot->sent_messages++;
							}
						}
					} else {
						/* Invalid command */
						EmbedSimple("Sudo **what**? I don't know what that command means.", msg.get_channel_id().get());
					}
				}
			} else {
				/* Access denied */
				EmbedSimple("Make your own sandwich, mortal.", msg.get_channel_id().get());
			}

			/* Eat the event */
			return false;
		}
		return true;
	}
};

ENTRYPOINT(DiagnosticsModule);

