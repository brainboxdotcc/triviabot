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

#define SPDLOG_FMT_EXTERNAL
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <sporks/bot.h>
#include <sporks/includes.h>
#include <iostream>
#include <fstream>
#include <mutex>
#include <queue>
#include <cstdlib>
#include <getopt.h>
#include <sys/types.h>
#include <sporks/database.h>
#include <sporks/stringops.h>
#include <sporks/modules.h>
#include <malloc.h>

using json = nlohmann::json;

/**
 * Parsed configuration file
 */
json configdocument;

/**
 * Constructor (creates threads, loads all modules)
 */
Bot::Bot(bool development, bool testing, bool intents, dpp::cluster* dppcluster, uint32_t cluster_id) : dev(development), test(testing), memberintents(intents), shard_init_count(0), core(dppcluster), sent_messages(0), received_messages(0), my_cluster_id(cluster_id) {
	Loader = new ModuleLoader(this);
	Loader->LoadAll();
	UpdatePresenceTimerTick();
}

/**
 * Join and delete a thread
 */
void Bot::DisposeThread(std::thread* t) {
	if (t) {
		try {
			t->join();
		}
		catch (const std::exception &e) {
		}
		delete t;
	}

}

/**
 * Destructor
 */
Bot::~Bot() {
	delete Loader;
}

/**
 * Returns the named string value from config.json
 */
std::string Bot::GetConfig(const std::string &name) {
	return configdocument[name].get<std::string>();
}

/**
 * Returns true if the bot is running in development mode (different token)
 */
bool Bot::IsDevMode() const {
	return dev;
}

/**
 * Returns true if the bot is running in testing mode (live token, ignoring messages except on specific server)
 */
bool Bot::IsTestMode() const {
	return test;
}

/** 
 * Returns true if the bot has member intents enabled, "GUILD_MEMBERS" which will eventually require discord HQ approval process.
 */
bool Bot::HasMemberIntents() const {
	return memberintents;
}

uint32_t Bot::GetMaxClusters() const {
       return core->maxclusters;
}

int main(int argc, char** argv) {

	int dev = 0;	/* Note: getopt expects ints, this is actually treated as bool */
	int test = 0;
	int members = 0;

	/* Set this specifically so that stringstreams don't do weird things on other locales printing decimal numbers for SQL */
	std::setlocale(LC_ALL, "en_GB.UTF-8");

	/* Parse command line parameters using getopt() */
	struct option longopts[] =
	{
		{ "dev",	no_argument,		&dev,		1 },
		{ "test",	no_argument,		&test,		1 },
		{ "members",	no_argument,		&members,	1 },
		{ "clusterid",  required_argument,      NULL,	   'c' },
		{ "maxclusters",required_argument,      NULL,	   'm' },
		{ 0, 0, 0, 0 }
	};

	/* These are our default intents for the bot, basically just receive messages, see reactions to the messages and see who's in our guilds */
	uint32_t intents = dpp::i_default_intents | dpp::i_message_content;

	/* Yes, getopt is ugly, but what you gonna do... */
	int index{};
	int arg{};
	bool clusters_defined = false;
	uint32_t clusterid = 0;
	uint32_t maxclusters = 1;

	/* opterr is an extern int. Doesn't smell of thread safety to me, bad GNU bad! */
	opterr = 0;
	while ((arg = getopt_long_only(argc, argv, "", longopts, &index)) != -1) {
		switch (arg) {
			case 0:
				/* getopt_long_only() set an int variable, just keep going */
			break;
			case 'c':
				/* Cluster id */
				clusterid = from_string<uint32_t>(optarg, std::dec);
				clusters_defined = true;
			break;
			case 'm':
				/* Number of clusters */
				maxclusters = from_string<uint32_t>(optarg, std::dec);
			break;
			case '?':
			default:
				std::cerr << "Unknown parameter '" << argv[optind - 1] << "'\n";
				std::cerr << "Usage: " << argv[0] << " [-dev] [-test] [-members]\n\n";
				std::cerr << "-dev	  Run using development token\n";
				std::cerr << "-test:	Run using live token, but eat all outbound messages except on test server\n";
				std::cerr << "-members:     Issue a GUILD_MEMBERS intent on shard registration\n";
				std::cerr << "-clusterid:   The current cluster id to identify for, must be set with -maxclusters\n";
				std::cerr << "-maxclusters: The maximum number of clusters the bot is running, must be set with -clusterid\n";
				exit(1);
			break;
		}
	}

	/* This will eventually need approval from discord HQ, so make sure it's a command line parameter we have to explicitly enable */
	if (members) {
		intents |= dpp::i_guild_members;
	}

	if (clusters_defined && maxclusters == 0) {
		std::cerr << "ERROR: You have defined a cluster id with -clusterid but no cluster count with -maxclusters.\n";
		exit(2);
	}

	/* Load configuration file */
	std::ifstream configfile("../config.json");
	configfile >> configdocument;

	/* Set up spdlog logger */
	std::shared_ptr<spdlog::logger> log;
	spdlog::init_thread_pool(8192, 2);
	std::vector<spdlog::sink_ptr> sinks;
	auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt >();
	auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("logs/triviabot{:02d}.log", clusterid), 1024 * 1024 * 5, 10);
	sinks.push_back(stdout_sink);
	sinks.push_back(rotating);
	log = std::make_shared<spdlog::async_logger>("test", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
	spdlog::register_logger(log);
	log->set_pattern("%^%Y-%m-%d %H:%M:%S.%e [%L] [th#%t]%$ : %v");
	log->set_level(spdlog::level::level_enum::debug);	

	/* Get the correct token from config file for either development or production environment */
	std::string token = (dev ? Bot::GetConfig("devtoken") : Bot::GetConfig("livetoken"));

	/* It's go time! */
	while (true) {

		/* Set cache policy for D++ library
		 * --------------------------------
		 * User caching:     none
		 * Emoji caching:    none
		 * Role caching:     none
		 * Channel caching:  none
		 * Guild caching:    none
		 */
		dpp::cache_policy_t cp = { dpp::cp_none, dpp::cp_none, dpp::cp_none, dpp::cp_none, dpp::cp_none };
		const bool compressed = false;

		/* Construct cluster */
		dpp::cluster bot(token, intents, dev ? 1 : from_string<uint32_t>(Bot::GetConfig("shardcount"), std::dec), clusterid, maxclusters, compressed, cp);

		/* Connect to SQL database */
		if (!db::connect(&bot, Bot::GetConfig("dbhost"), Bot::GetConfig("dbuser"), Bot::GetConfig("dbpass"), Bot::GetConfig("dbname"), from_string<uint32_t>(Bot::GetConfig("dbport"), std::dec))) {
			std::cerr << "Database connection failed\n";
			exit(2);
		}

		/* Integrate spdlog logger to D++ log events */
		bot.on_log([&bot, &log](const dpp::log_t & event) {
			switch (event.severity) {
				case dpp::ll_trace:
					log->trace("{}", event.message);
				break;
				case dpp::ll_debug:
					log->debug("{}", event.message);
				break;
				case dpp::ll_info:
					log->info("{}", event.message);
				break;
				case dpp::ll_warning:
					log->warn("{}", event.message);
				break;
				case dpp::ll_error:
					log->error("{}", event.message);
				break;
				case dpp::ll_critical:
				default:
					log->critical("{}", event.message);
				break;
			}
		});

		Bot client(dev, test, members, &bot, clusterid);

		/* Attach events to the Bot class methods */
		bot.on_message_create(std::bind(&Bot::onMessage, &client, std::placeholders::_1));
		bot.on_ready(std::bind(&Bot::onReady, &client, std::placeholders::_1));
		bot.on_guild_create(std::bind(&Bot::onServer, &client, std::placeholders::_1));
		bot.on_guild_delete(std::bind(&Bot::onServerDelete, &client, std::placeholders::_1));
		bot.on_entitlement_create(std::bind(&Bot::onEntitlementCreate, &client, std::placeholders::_1));
		bot.on_entitlement_update(std::bind(&Bot::onEntitlementUpdate, &client, std::placeholders::_1));
		bot.on_entitlement_delete(std::bind(&Bot::onEntitlementDelete, &client, std::placeholders::_1));

		bot.set_websocket_protocol(dpp::ws_etf);

		bot.start_timer([](dpp::timer t) {
			/* Garbage collect free memory by consolidating free malloc() blocks */
			malloc_trim(0);
		}, 600);
	
		try {
			/* Actually connect and start the event loop */
			bot.start(false);
		}
		catch (const std::exception &e) {
			bot.log(dpp::ll_error, fmt::format("Oof! {}", e.what()));
		}

		/* Reconnection delay to prevent hammering discord */
		::sleep(30);
	}
}

uint32_t Bot::GetClusterID() {
	return my_cluster_id;
}

void Bot::SetClusterID(uint32_t c) {
	my_cluster_id = c;
}

