/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
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

#include <dpp/dpp.h>
#include <fmt/format.h>
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
#include <string>
#include <array>
#include <unistd.h>

int64_t GetRSS() {
	int64_t ram = 0;
	std::ifstream self_status("/proc/self/status");
	while (self_status) {
		std::string token;
		self_status >> token;
		if (token == "VmRSS:") {
			self_status >> ram;
			break;
		}
	}
	self_status.close();
	return ram * 1024;
}


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
	PCRE* diagnostic_message;
public:
	DiagnosticsModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml)
	{
		ml->Attach({ I_OnMessage }, this);
		diagnostic_message = new PCRE("^sudo(\\s+(.+?))$", true);
	}

	virtual ~DiagnosticsModule()
	{
		delete diagnostic_message;
	}

	virtual std::string GetVersion()
	{
		/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
		std::string version = "$ModVer 34$";
		return "1.0." + version.substr(8,version.length() - 9);
	}

	virtual std::string GetDescription()
	{
		return "Diagnostic Commands (sudo), '@Sporks sudo'";
	}

	virtual bool OnMessage(const dpp::message_create_t &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
	{
		std::vector<std::string> param;

		if (mentioned && diagnostic_message->Match(clean_message, param) && param.size() >= 3) {

			dpp::message msg = message.msg;
			std::stringstream tokens(trim(param[2]));
			std::string subcommand;
			tokens >> subcommand;

			bot->core->log(dpp::ll_info, fmt::format("SUDO: <{}> {}", msg.author.id ? msg.author.username : "", clean_message));

			/* Get owner snowflake id from config file */
			dpp::snowflake owner_id = from_string<int64_t>(Bot::GetConfig("owner"), std::dec);

			/* Only allow these commands to the bot owner */
			if (msg.author.id == owner_id) {

				if (param.size() < 3) {
					/* Invalid number of parameters */
					EmbedSimple("Sudo make me a sandwich.", msg.channel_id, msg.guild_id);
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

						if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == msg.guild_id) {
							bot->core->message_create(dpp::message(msg.channel_id, s.str()));
							bot->sent_messages++;
						}
						
					} else if (lowercase(subcommand) == "load") {
						/* Load a module */
						std::string modfile;
						tokens >> modfile;
						if (bot->Loader->Load(modfile)) {
							EmbedSimple("Loaded module: " + modfile, msg.channel_id, msg.guild_id);
						} else {
							EmbedSimple(std::string("Can't do that: ``") + bot->Loader->GetLastError() + "``", msg.channel_id, msg.guild_id);
						}
					} else if (lowercase(subcommand) == "unload") {
						/* Unload a module */
						std::string modfile;
						tokens >> modfile;
						if (modfile == "module_diagnostics.so") {
							EmbedSimple("I suppose you think that's funny, dont you? *I'm sorry. can't do that, dave.*", msg.channel_id, msg.guild_id);
						} else {
							if (bot->Loader->Unload(modfile)) {
								EmbedSimple("Unloaded module: " + modfile, msg.channel_id, msg.guild_id);
							} else {
								EmbedSimple(std::string("Can't do that: ``") + bot->Loader->GetLastError() + "``", msg.channel_id, msg.guild_id);
							}
						}
					} else if (lowercase(subcommand) == "reload") {
						/* Reload a currently loaded module */
						std::string modfile;
						tokens >> modfile;
						if (modfile == "module_diagnostics.so") {
							EmbedSimple("I suppose you think that's funny, dont you? *I'm sorry. can't do that, dave.*", msg.channel_id, msg.guild_id);
						} else {
							if (bot->Loader->Reload(modfile)) {
								EmbedSimple("Reloaded module: " + modfile, msg.channel_id, msg.guild_id);
							} else {
								EmbedSimple(std::string("Can't do that: `") + bot->Loader->GetLastError() + "`", msg.channel_id, msg.guild_id);
							}
						}
					} else if (lowercase(subcommand) == "sqlstats") {
						db::statistics stats = db::get_stats();
						std::ostringstream statstr;
						statstr << fmt::format("SQL Statistics\n---------------\n") << "\n";
						statstr << fmt::format("Total queries executed:  {:10d}", stats.queries_processed) << "\n";
						statstr << fmt::format("Total queries errored:   {:10d}", stats.queries_errored) << "\n";
						statstr << fmt::format("Background queue length: {:10d}", stats.bg_queue_length) << "\n\n";
						size_t n = 0;
						statstr << fmt::format("{0:7s} {1:7s}{2:9s}  {3:6s}       {4:s} {5:s}     ", "Conn#", "F/B", "Proc/Err", "Ready", "Avg Query Len", "Total Time") << "\n";
						statstr << fmt::format("----------------------------------------------------------------\n") << "\n";
						for (db::connection_info ci : stats.connections) {
							statstr << fmt::format("{0:02d} {1:7s} {2:8d}/{3:04d}  {4:6s} {5:12.06f} {6:16.03f}",
							n++,
							ci.background ? "     B " : "     F ",
							ci.queries_processed,
							ci.queries_errored,
							ci.ready ? "🟢" : "🔴",
							ci.avg_query_length,
							ci.busy_time
							) << "\n";
						}
						bot->core->message_create(dpp::message(msg.channel_id, "```\n" + statstr.str() + "\n```"));
						bot->sent_messages++;
					} else if (lowercase(subcommand) == "sql") {
						std::string sql;
						std::getline(tokens, sql);
						sql = trim(sql);
						bool had_error;
						/* Listen for error messages only during execution of this query */
						auto handler = bot->core->on_log([this, &had_error, sql, msg](const dpp::log_t& logmsg) {
							if (logmsg.message.find("SQL Error: ") != std::string::npos) {
								this->EmbedSimple(logmsg.message, msg.channel_id, msg.guild_id);
								had_error = true;
							}
						});
						db::resultset rs = db::query(sql, {});
						std::stringstream w;
						bot->core->on_log.detach(handler);
						if (rs.size() == 0) {
							if (!had_error) {
								EmbedSimple("Successfully executed, no rows returned.", msg.channel_id, msg.guild_id);
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
							if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == msg.guild_id) {
								bot->core->message_create(dpp::message(msg.channel_id, "```diff\n" + w.str() + "```"));
								bot->sent_messages++;
							}
						}
					} else if (lowercase(subcommand) == "restart") {
						EmbedSimple("Restarting...", msg.channel_id, msg.guild_id);
						::sleep(5);
						/* Note: exit here will restart, because we run the bot via run.sh which restarts the bot on quit. */
						exit(0);
					} else if (lowercase(subcommand) == "lookup") {
						int64_t gnum = 0;
						tokens >> gnum;
						EmbedSimple("use /shard instead", msg.channel_id, msg.guild_id);
					} else if (lowercase(subcommand) == "shardstats") {
						std::stringstream w;
						w << "```diff\n";

						uint64_t count = 0, u_count = 0;
						auto& shards = bot->core->get_shards();
						for (auto i = shards.begin(); i != shards.end(); ++i) {
							dpp::discord_client* shard = i->second;
							count += shard->get_bytes_in() + shard->get_bytes_out();
							u_count += shard->get_decompressed_bytes_in() + shard->get_bytes_out();
						}

						w << fmt::format("  Total transfer: {} (U: {} | {:.2f}%) Memory usage: {}\n", dpp::utility::bytes(count), dpp::utility::bytes(u_count), (count / (double)u_count)*100, dpp::utility::bytes(GetRSS()));
						w << fmt::format("- ╭──────┬──────────┬───────┬───────┬────────────────┬───────────┬──────────╮\n");
						w << fmt::format("- │shard#│  sequence│servers│members│uptime          │transferred│reconnects│\n");
						w << fmt::format("- ├──────┼──────────┼───────┼───────┼────────────────┼───────────┼──────────┤\n");

						for (auto i = shards.begin(); i != shards.end(); ++i)
						{
							dpp::discord_client* s = i->second;
							if (s->is_connected())
								w << "+ ";
							else
								w << "  ";
							w << fmt::format("|{:6}|{:10}|{:7}|{:7}|{:>16}|{:>11}|{:10}|",
											 s->shard_id,
											 s->last_seq,
											 s->get_guild_count(),
											 s->get_member_count(),
											 s->get_uptime().to_string(),
											 dpp::utility::bytes(s->get_bytes_in() + s->get_bytes_out()),
											 0);
							if (message.from->shard_id == s->shard_id) {
								w << " *\n";
							} else {
								w << "\n";
							}
						}
						w << fmt::format("+ ╰──────┴──────────┴───────┴───────┴────────────────┴───────────┴──────────╯\n");
						w << "```";
						if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == msg.guild_id) {
							bot->core->message_create(dpp::message(msg.channel_id, w.str()));
							bot->sent_messages++;
						}
					} else {
						/* Invalid command */
						EmbedSimple("Sudo **what**? I don't know what that command means.", msg.channel_id, msg.guild_id);
					}
				}
			} else {
				/* Access denied */
				EmbedSimple("Make your own sandwich, mortal.", msg.channel_id, msg.guild_id);
			}

			/* Eat the event */
			return false;
		}
		return true;
	}
};

ENTRYPOINT(DiagnosticsModule);

