#pragma once

#include <string>
#include "settings.h"
#include <dpp/dpp.h>

#define DECLARE_COMMAND_CLASS(__command_name__) class __command_name__ : public command_t { public: __command_name__(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options); virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user); };

class in_cmd
{
 public:
	std::string msg;
	std::string username;
	dpp::user user;
	dpp::guild_member member;
	uint64_t author_id;
	uint64_t channel_id;
	uint64_t guild_id;
	bool mentions_bot;
	bool from_dashboard;
	std::string interaction_token;
	dpp::snowflake command_id;
	in_cmd(const std::string &m, uint64_t author, uint64_t channel, uint64_t guild, bool mention, const std::string &username, bool dashboard, dpp::user u, dpp::guild_member gm);
};

class command_t 
{
 protected:
 	 class TriviaModule* creator;
	std::string base_command;
	std::string _(const std::string &str, const guild_settings_t &settings);
 public:
	bool admin;
 	std::string description;
	std::vector<dpp::command_option> opts;
	command_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options);
	virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user) = 0;
	virtual ~command_t();
};

DECLARE_COMMAND_CLASS(command_start_t);
DECLARE_COMMAND_CLASS(command_stop_t);
DECLARE_COMMAND_CLASS(command_vote_t);
DECLARE_COMMAND_CLASS(command_votehint_t);
DECLARE_COMMAND_CLASS(command_stats_t);
DECLARE_COMMAND_CLASS(command_info_t);
DECLARE_COMMAND_CLASS(command_join_t);
DECLARE_COMMAND_CLASS(command_create_t);
DECLARE_COMMAND_CLASS(command_leave_t);
DECLARE_COMMAND_CLASS(command_help_t);
DECLARE_COMMAND_CLASS(command_dashboard_t);
DECLARE_COMMAND_CLASS(command_global_t);
DECLARE_COMMAND_CLASS(command_enable_t);
DECLARE_COMMAND_CLASS(command_disable_t);
DECLARE_COMMAND_CLASS(command_privacy_t);
DECLARE_COMMAND_CLASS(command_invite_t);
DECLARE_COMMAND_CLASS(command_coins_t);
DECLARE_COMMAND_CLASS(command_language_t);
DECLARE_COMMAND_CLASS(command_prefix_t);
DECLARE_COMMAND_CLASS(command_forceleave_t);
DECLARE_COMMAND_CLASS(command_topteams_t);
DECLARE_COMMAND_CLASS(command_nitro_t);
DECLARE_COMMAND_CLASS(command_resetprefix_t);
DECLARE_COMMAND_CLASS(command_shard_t);
DECLARE_COMMAND_CLASS(command_give_t);
DECLARE_COMMAND_CLASS(command_queue_t);

typedef std::map<std::string, command_t*> command_list_t;
